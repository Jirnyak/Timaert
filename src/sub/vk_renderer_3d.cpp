#include "sub/vk_renderer_3d.h"
#include "sub/vk_camera_math.h"
#include "sub/base_generator.h"
#include "sub/camera.h"
#include "sub/height.h"
#include "sub/lighting.h"
#include "sub/material.h"
#include "sub/map_data.h"
#include "sub/particles.h"
#include "sub/seamless_manager.h"
#include "sub/tree_atlas.h"
#include "gpu/vk_device.h"
#include "gpu/vk_renderer.h"
#include "macro/state.h"
#include "ecs/components.h"
#include "ecs/world.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace sm::sub {

// The per-frame light-SSBO ring is indexed by the renderer's currentFrame, so
// its depth must match the swapchain's frames-in-flight exactly or two frames
// would alias one buffer. Pin them at compile time.
static_assert(Renderer3DVk::kFramesInFlight
                  == gpu::VulkanRenderer::kMaxFramesInFlight,
              "light-SSBO ring depth must equal renderer frames-in-flight");

namespace {

// Physical scale — must match the GL Renderer3D exactly.
constexpr float kTileMeters  = 1.0f;
constexpr float kWorldExtent = float(kFullSize) * kTileMeters * 0.5f; // 1536 m

// Per-vertex layout: position (3) + normal (3) + grid UV (2). The material id
// is NOT carried per-vertex — the fragment shader samples a full-resolution
// tile material texture at this UV instead, so thin features (roads, field
// bands) stay crisp regardless of how coarse the terrain mesh is. This mirrors
// the TS renderer, which samples a per-fragment u_tileGrid rather than baking
// material into the coarse mesh.
struct Vtx {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// Material ids + the (tile, biome) mapping + the cross-seam biome dither all
// live in sub/material.h — per-material base colours are mesh.frag's
// materialBase, picked per-fragment from the sampled tile id.

// Push-constant block for the terrain mesh — matches mesh.frag / mesh.vert.
// 176 bytes (= 11 × vec4), within MoltenVK's ≥256 B limit.
struct MeshPush {
    float mvp[16];
    float sunDir[4];
    float sunColor[4];
    float ambient[4];
    float lightMvp[16];
};

// Push-constant block for the procedural sky — matches sky.frag.
// 80 bytes (= 5 × vec4).
struct SkyPush {
    float forward[4];
    float right[4];
    float up[4];
    float p0[4]; // resX, resY, fov, tod
    float p1[4]; // fogR, fogG, fogB, time(sec)
};

// Push-constant block for the transparent water plane — matches water.vert/frag.
// 128 bytes (= 8 × vec4).
struct WaterPush {
    float mvp[16];
    float camPos[4];
    float sunDir[4];
    float sunColor[4];
    float params[4]; // time, ambient, waterLevel, extent
};

// Per-instance data for tree billboards — matches billboard.vert input.
struct TreeInstance {
    float px, py, pz;
    float size;
    float species;
    float seed;
};

// Push-constant block for billboard pass (trees/NPCs) — matches billboard.vert.
// 176 bytes (= 11 × vec4).
struct BbPush {
    float mvp[16];
    float camRight[4];
    float sunColor[4];
    float ambient[4];
    float lightMvp[16];
};

// Per-instance data for structure boxes — matches struct.vert input.
struct StructInstance {
    float px, py, pz; // box centre (world)
    float hx, hy, hz; // half-extents
    float type;       // 0 = wall, 1 = house
    float seed;
};

struct NpcInstance {
    float px, py, pz;
    float size;
    std::uint32_t descIndex;
    std::uint32_t anim;
};

// Per-instance data for procedural creature billboards — matches creature.vert.
// 36 bytes. archetype/seed/tint drive the analytic silhouette in
// shaders/creature_sprite.glsl; the shadow caster reads only the first 4 floats.
struct CreatureInstance {
    float px, py, pz;          // feet world position (metres)
    float size;                // overall billboard scale (metres)
    float archetype;           // CreatureArchetype 0..6 (cast from uint8)
    float seed;                // per-instance variation
    float tintR, tintG, tintB; // base colour 0..1 (from Sprite.rgb / 255)
};

// Maximum entity instances per category (NPCs, creatures). 16384 = 2^14.
// The GPU buffers are allocated once at this size; the per-frame staging vectors
// grow to fit. vkCmdUpdateBuffer is capped at 65536 bytes per call, so uploads
// are batched automatically by update_instance_buffer below.
constexpr std::uint32_t kMaxEntityInstances = 16384;

// Batch vkCmdUpdateBuffer into <=65536-byte chunks for large instance arrays.
template <typename T>
void update_instance_buffer(VkCommandBuffer cmd, VkBuffer buf,
                           const T* data, std::uint32_t count) {
    constexpr VkDeviceSize kLimit = 65536;
    constexpr VkDeviceSize stride = sizeof(T);
    constexpr std::uint32_t perChunk = std::uint32_t(kLimit / stride);
    VkDeviceSize offset = 0;
    std::uint32_t remaining = count;
    while (remaining > 0) {
        const std::uint32_t n = std::min(remaining, perChunk);
        vkCmdUpdateBuffer(cmd, buf, offset, n * stride, data);
        data += n;
        offset += n * stride;
        remaining -= n;
    }
}

// Push constants for shadow casters (depth-only pass).
struct ShadowPush {
    float lightMvp[16]; // 64B
};
struct ShadowBbPush {
    float lightMvp[16];
    float lightRight[4]; // billboard orientation in light space
};

// Push-constant block for the additive particle pass — matches particle.vert.
// 96 bytes (= mat4 + 2×vec4). Particles are centred + fully camera-facing, so
// they need BOTH camera axes (camRight AND camUp), unlike the cylindrical tree
// billboards which use world-up.
struct ParticlePush {
    float mvp[16];
    float camRight[4];
    float camUp[4];
};

// The renderer's per-frame particle instance layout is exactly the sim's packed
// record; pin them so a change to one can't silently desync the vertex attrs.
static_assert(sizeof(ParticleInstance) == 32,
              "ParticleInstance must match the particle.vert attribute layout");
static_assert(Renderer3DVk::kMaxParticleInstances
                  == static_cast<std::uint32_t>(ParticleSystem::kMaxParticles),
              "renderer particle ceiling must match the sim pool size");

character::Direction direction_from_velocity(float vx, float vy) {
    if (std::fabs(vx) > std::fabs(vy)) {
        return vx < 0.0f ? character::Direction::Left
                         : character::Direction::Right;
    }
    if (std::fabs(vy) > 0.001f) {
        return vy > 0.0f ? character::Direction::Back
                         : character::Direction::Front;
    }
    return character::Direction::Front;
}

vec3 transform_point(const mat4& m, vec3 p) {
    const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12];
    const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13];
    const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    if (std::fabs(w) > 1e-6f) return {x / w, y / w, z / w};
    return {x, y, z};
}

void compute_shadow_basis(const Camera& cam, const WorldTime& time,
                          std::uint32_t shadowSize, mat4& lightMvp,
                          vec3& lightRight) {
    constexpr float kShadowRadiusM = 1024.0f;
    constexpr float kShadowBelowM = 600.0f;
    constexpr float kShadowAboveM = 900.0f;
    constexpr float kShadowMarginM = 80.0f;
    constexpr float kShadowEyeDistanceM = 4200.0f;

    const SunInfo sun = compute_sun(time);
    vec3 toSun = normalize({sun.sunDir.x, sun.sunDir.y, sun.sunDir.z});
    if (length(toSun) <= 1e-5f || sun.sunIntensity <= 0.01f) {
        toSun = normalize(vec3{-0.35f, 0.75f, -0.55f});
    }
    const vec3 lightForward = toSun * -1.0f; // light rays travel sun -> world
    const vec3 worldUp = {0.0f, 1.0f, 0.0f};

    const vec3 sunHorizontal = {toSun.x, 0.0f, toSun.z};
    if (length(sunHorizontal) > 1e-4f) {
        lightRight = normalize(cross(worldUp, sunHorizontal));
        if (dot(lightRight, {0.0f, 0.0f, 1.0f}) < 0.0f) {
            lightRight = lightRight * -1.0f;
        }
    } else {
        lightRight = {0.0f, 0.0f, 1.0f};
    }
    vec3 lightUp = normalize(cross(lightRight, lightForward));
    if (length(lightUp) <= 1e-5f) lightUp = {1.0f, 0.0f, 0.0f};

    const vec3 boxMin = {cam.pos.x - kShadowRadiusM,
                         cam.pos.y - kShadowBelowM,
                         cam.pos.z - kShadowRadiusM};
    const vec3 boxMax = {cam.pos.x + kShadowRadiusM,
                         cam.pos.y + kShadowAboveM,
                         cam.pos.z + kShadowRadiusM};
    const vec3 boxCenter = {(boxMin.x + boxMax.x) * 0.5f,
                            (boxMin.y + boxMax.y) * 0.5f,
                            (boxMin.z + boxMax.z) * 0.5f};
    const vec3 eye = boxCenter + toSun * kShadowEyeDistanceM;
    const mat4 lightView = mat4_lookAt(eye, boxCenter, lightUp);

    float minX =  1.0e30f, minY =  1.0e30f, minZ =  1.0e30f;
    float maxX = -1.0e30f, maxY = -1.0e30f, maxZ = -1.0e30f;
    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                const vec3 c = {ix ? boxMax.x : boxMin.x,
                                iy ? boxMax.y : boxMin.y,
                                iz ? boxMax.z : boxMin.z};
                const vec3 v = transform_point(lightView, c);
                minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
                minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
                minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
            }
        }
    }

    float span = std::max(maxX - minX, maxY - minY) + kShadowMarginM * 2.0f;
    span = std::max(span, 1.0f);
    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    const float texel = span / float(std::max(shadowSize, std::uint32_t{1}));
    centerX = std::floor(centerX / texel + 0.5f) * texel;
    centerY = std::floor(centerY / texel + 0.5f) * texel;

    const float nearD = std::max(0.5f, -maxZ - kShadowMarginM);
    const float farD = std::max(nearD + 10.0f, -minZ + kShadowMarginM);
    lightMvp = mat4_mul(vk_ortho(centerX - span * 0.5f,
                                 centerX + span * 0.5f,
                                 centerY - span * 0.5f,
                                 centerY + span * 0.5f,
                                 nearD, farD),
                        lightView);
}

// Helper: load SPIR-V path relative to the executable.
void spv_path(char* dst, std::size_t n, const char* name) {
    char* base = SDL_GetBasePath();
    std::snprintf(dst, n, "%sshaders/%s.spv", base ? base : "./", name);
    if (base) SDL_free(base);
}

} // namespace

// ──────────────────────────────────────────────────────────────────────
// init — create pipelines (load-time, called once after device + pass exist).
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::init(const gpu::VulkanDevice& dev, VkRenderPass mainPass) {
    dev_  = &dev;
    pass_ = mainPass;

    char vpath[1024], fpath[1024];

    std::vector<NpcInstance> dummyNpcs(kMaxEntityInstances);
    if (!npcInstBuf_.create_device_local(dev, dummyNpcs.data(),
                                         dummyNpcs.size() * sizeof(NpcInstance),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] npc buffer FAILED\n");
    }
    std::vector<CreatureInstance> dummyCreatures(kMaxEntityInstances);
    if (!creatureInstBuf_.create_device_local(
            dev, dummyCreatures.data(),
            dummyCreatures.size() * sizeof(CreatureInstance),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] creature buffer FAILED\n");
    }
    // Particle instance buffer: sized once at the pool ceiling (64 KiB), refilled
    // per frame in-place via vkCmdUpdateBuffer + barrier (same path as NPCs).
    std::vector<ParticleInstance> dummyParticles(kMaxParticleInstances);
    if (!particleInstBuf_.create_device_local(
            dev, dummyParticles.data(),
            dummyParticles.size() * sizeof(ParticleInstance),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] particle buffer FAILED\n");
    }
    // A4: Paperdoll NPC pool (instanced billboards + UI portraits).
    // The pool texture (SpriteArray) backs the pipeline layout below.
    if (!paperdoll_.init(dev)) {
        std::fprintf(stderr, "[Renderer3DVk] paperdoll init FAILED\n");
    }

    // ── A6: Shadow map + descriptor set (created first so main pipelines
    //    can reference shadowSetLayout_). ──
    if (!shadow_.init(dev, 4096)) {
        std::fprintf(stderr, "[Renderer3DVk] shadow map FAILED\n");
    }
    {
        // Set 0 = { binding 0: shadow-map sampler (immutable),
        //           binding 1: per-frame point-light SSBO }. This one set is
        // bound by every lit pass, so adding the light buffer here lights them
        // all with a single shader addition (Inc 2) rather than six.
        VkDescriptorSetLayoutBinding b[2]{};
        b[0].binding = 0;
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1;
        b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[1].descriptorCount = 1;
        b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 2;
        dlci.pBindings = b;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &shadowSetLayout_);

        // One shadow sampler + one light SSBO per frame in flight.
        VkDescriptorPoolSize ps[2]{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = kFramesInFlight;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &shadowPool_);

        VkDescriptorSetLayout layouts[kFramesInFlight];
        for (int i = 0; i < kFramesInFlight; ++i) layouts[i] = shadowSetLayout_;
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = shadowPool_;
        dsai.descriptorSetCount = kFramesInFlight;
        dsai.pSetLayouts = layouts;
        vkAllocateDescriptorSets(dev.device, &dsai, shadowSet_);

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        dii.imageView = shadow_.view;
        dii.sampler = shadow_.sampler;
        for (int i = 0; i < kFramesInFlight; ++i) {
            // Persistently-mapped light SSBO for this frame slot. Start empty
            // (count = 0) so every lit pass falls through to directional-only —
            // byte-identical to the pre-point-light renderer until Inc 2/3 feed
            // it real lights.
            if (!lightBuf_[i].create_host_mapped(
                    dev, sizeof(GpuLightBuffer),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
                std::fprintf(stderr, "[Renderer3DVk] light SSBO %d FAILED\n", i);
            } else {
                static_cast<GpuLightBuffer*>(lightBuf_[i].mapped)->count = 0;
            }
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = lightBuf_[i].buffer;
            dbi.offset = 0;
            dbi.range  = sizeof(GpuLightBuffer);
            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = shadowSet_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &dii;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = shadowSet_[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &dbi;
            vkUpdateDescriptorSets(dev.device, 2, writes, 0, nullptr);
        }
    }

    // Material texture descriptor set (set 1 on the terrain pipeline). Allocated
    // once here; upload() bakes the full-res tile texture and (re)writes this set
    // to point at it. The layout must exist before the terrain pipeline below.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 1;
        dlci.pBindings = &b;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &materialSetLayout_);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &materialPool_);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = materialPool_;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &materialSetLayout_;
        vkAllocateDescriptorSets(dev.device, &dsai, &materialSet_);
        // Image view/sampler are bound in upload() once the tile texture exists.
    }

    // A1: Terrain mesh pipeline (mesh.vert + mesh.frag).
    spv_path(vpath, sizeof vpath, "mesh.vert");
    spv_path(fpath, sizeof fpath, "mesh.frag");

    // pos (vec3) @0, normal (vec3) @12, grid uv (vec2) @24 — see Vtx.
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = sizeof(float) * 3;
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset   = sizeof(float) * 6;

    // set 0 = shadow sampler (shared), set 1 = full-res tile material texture.
    const VkDescriptorSetLayout terrainSets[2] = {
        shadowSetLayout_, materialSetLayout_
    };
    if (!terrainPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                  sizeof(MeshPush), sizeof(Vtx), attrs, 3,
                                  /*instanced=*/false, /*depthTest=*/true,
                                  /*depthWrite=*/true, /*blend=*/false,
                                  /*cullBack=*/false, terrainSets, 2)) {
        std::fprintf(stderr, "[Renderer3DVk] terrain pipeline FAILED\n");
    }

    // A2: Sky pipeline (fullscreen.vert + sky.frag, no vertex input, depth off).
    spv_path(vpath, sizeof vpath, "fullscreen.vert");
    spv_path(fpath, sizeof fpath, "sky.frag");
    if (!skyPipe_.create(dev, mainPass, vpath, fpath, sizeof(SkyPush))) {
        std::fprintf(stderr, "[Renderer3DVk] sky pipeline FAILED\n");
    }

    // A3: Water pipeline (water.vert + water.frag, stride=0, depth test, no depth write, blend).
    // Binds set 0 (shadow sampler + point-light SSBO) so water.frag can reflect
    // positional lights (point_lights_spec) — a torch / spell glints on the waves.
    spv_path(vpath, sizeof vpath, "water.vert");
    spv_path(fpath, sizeof fpath, "water.frag");
    if (!waterPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                sizeof(WaterPush), 0, nullptr, 0,
                                /*instanced=*/false, /*depthTest=*/true,
                                /*depthWrite=*/false, /*blend=*/true,
                                /*cullBack=*/false, shadowSetLayout_)) {
        std::fprintf(stderr, "[Renderer3DVk] water pipeline FAILED\n");
    }

    // FX: Additive particle pipeline (particle.vert + particle.frag, instanced).
    // Emissive — binds NO descriptor set (no lighting/shadow). depthTest on so
    // terrain/creatures occlude particles; depthWrite OFF + additive blend so
    // overlapping particles accumulate into a glow and need no back-to-front
    // sort. Same 6-vertex gl_VertexIndex quad as billboards.
    spv_path(vpath, sizeof vpath, "particle.vert");
    spv_path(fpath, sizeof fpath, "particle.frag");
    {
        VkVertexInputAttributeDescription pAttrs[3]{};
        pAttrs[0].location = 0; pAttrs[0].binding = 0;
        pAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    pAttrs[0].offset = 0;
        pAttrs[1].location = 1; pAttrs[1].binding = 0;
        pAttrs[1].format = VK_FORMAT_R32_SFLOAT;          pAttrs[1].offset = sizeof(float) * 3;
        pAttrs[2].location = 2; pAttrs[2].binding = 0;
        pAttrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; pAttrs[2].offset = sizeof(float) * 4;
        if (!particlePipe_.create_mesh(dev, mainPass, vpath, fpath,
                                       sizeof(ParticlePush),
                                       sizeof(ParticleInstance),
                                       pAttrs, 3, /*instanced=*/true,
                                       /*depthTest=*/true, /*depthWrite=*/false,
                                       /*blend=*/true, /*cullBack=*/false,
                                       /*descriptorSetLayout=*/VK_NULL_HANDLE,
                                       /*additive=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] particle pipeline FAILED\n");
        }
    }

    // A4: Tree billboard pipeline (billboard.vert + billboard.frag, instanced).
    spv_path(vpath, sizeof vpath, "billboard.vert");
    spv_path(fpath, sizeof fpath, "billboard.frag");
    {
        VkVertexInputAttributeDescription tAttrs[4]{};
        tAttrs[0].location = 0; tAttrs[0].binding = 0;
        tAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; tAttrs[0].offset = 0;
        tAttrs[1].location = 1; tAttrs[1].binding = 0;
        tAttrs[1].format = VK_FORMAT_R32_SFLOAT; tAttrs[1].offset = sizeof(float) * 3;
        tAttrs[2].location = 2; tAttrs[2].binding = 0;
        tAttrs[2].format = VK_FORMAT_R32_SFLOAT; tAttrs[2].offset = sizeof(float) * 4;
        tAttrs[3].location = 3; tAttrs[3].binding = 0;
        tAttrs[3].format = VK_FORMAT_R32_SFLOAT; tAttrs[3].offset = sizeof(float) * 5;
        if (!treePipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(TreeInstance),
                                   tAttrs, 4, /*instanced=*/true,
                                   /*depthTest=*/true, /*depthWrite=*/true,
                                   /*blend=*/false, /*cullBack=*/false,
                                   shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] tree pipeline FAILED\n");
        }
    }

    // A5: Structure pipeline (struct.vert + struct.frag, instanced boxes).
    spv_path(vpath, sizeof vpath, "struct.vert");
    spv_path(fpath, sizeof fpath, "struct.frag");
    {
        VkVertexInputAttributeDescription sAttrs[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            sAttrs[i].location = i; sAttrs[i].binding = 0;
        }
        sAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[0].offset = 0;
        sAttrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[1].offset = sizeof(float) * 3;
        sAttrs[2].format = VK_FORMAT_R32_SFLOAT;       sAttrs[2].offset = sizeof(float) * 6;
        sAttrs[3].format = VK_FORMAT_R32_SFLOAT;       sAttrs[3].offset = sizeof(float) * 7;
        if (!structPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                     sizeof(MeshPush), sizeof(StructInstance),
                                     sAttrs, 4, /*instanced=*/true,
                                     /*depthTest=*/true, /*depthWrite=*/true,
                                     /*blend=*/false, /*cullBack=*/false,
                                     shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] struct pipeline FAILED\n");
        }
    }

    // A7: NPC billboard pipeline (npc.vert + npc.frag, instanced).
    spv_path(vpath, sizeof vpath, "npc.vert");
    spv_path(fpath, sizeof fpath, "npc.frag");
    {
        VkVertexInputAttributeDescription nAttrs[4]{};
        nAttrs[0].location = 0; nAttrs[0].binding = 0;
        nAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; nAttrs[0].offset = 0;
        nAttrs[1].location = 1; nAttrs[1].binding = 0;
        nAttrs[1].format = VK_FORMAT_R32_SFLOAT; nAttrs[1].offset = sizeof(float) * 3;
        nAttrs[2].location = 2; nAttrs[2].binding = 0;
        nAttrs[2].format = VK_FORMAT_R32_UINT; nAttrs[2].offset = sizeof(float) * 4;
        nAttrs[3].location = 3; nAttrs[3].binding = 0;
        nAttrs[3].format = VK_FORMAT_R32_UINT; nAttrs[3].offset = sizeof(float) * 5;
        VkDescriptorSetLayout npcSets[2] = {
            shadowSetLayout_, paperdoll_.set_layout()
        };
        if (!npcPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(NpcInstance),
                                   nAttrs, 4, /*instanced=*/true,
                                   /*depthTest=*/true, /*depthWrite=*/true,
                                   /*blend=*/true, /*cullBack=*/false,
                                   npcSets, 2)) {
            std::fprintf(stderr, "[Renderer3DVk] npc pipeline FAILED\n");
        }
    }

    // A8: Creature billboard pipeline (creature.vert + creature.frag, instanced).
    // Shares the shadow-sampler set (0) with trees. Alpha-tested via discard, so
    // blend=false + depthWrite=true keep the silhouette crisp and depth-correct.
    spv_path(vpath, sizeof vpath, "creature.vert");
    spv_path(fpath, sizeof fpath, "creature.frag");
    {
        VkVertexInputAttributeDescription cAttrs[5]{};
        cAttrs[0].location = 0; cAttrs[0].binding = 0;
        cAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; cAttrs[0].offset = 0;
        cAttrs[1].location = 1; cAttrs[1].binding = 0;
        cAttrs[1].format = VK_FORMAT_R32_SFLOAT; cAttrs[1].offset = sizeof(float) * 3;
        cAttrs[2].location = 2; cAttrs[2].binding = 0;
        cAttrs[2].format = VK_FORMAT_R32_SFLOAT; cAttrs[2].offset = sizeof(float) * 4;
        cAttrs[3].location = 3; cAttrs[3].binding = 0;
        cAttrs[3].format = VK_FORMAT_R32_SFLOAT; cAttrs[3].offset = sizeof(float) * 5;
        cAttrs[4].location = 4; cAttrs[4].binding = 0;
        cAttrs[4].format = VK_FORMAT_R32G32B32_SFLOAT; cAttrs[4].offset = sizeof(float) * 6;
        if (!creaturePipe_.create_mesh(dev, mainPass, vpath, fpath,
                                       sizeof(BbPush), sizeof(CreatureInstance),
                                       cAttrs, 5, /*instanced=*/true,
                                       /*depthTest=*/true, /*depthWrite=*/true,
                                       /*blend=*/false, /*cullBack=*/false,
                                       shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] creature pipeline FAILED\n");
        }
    }

    // A6: Shadow caster pipelines (depth-only, into shadow_.renderPass).
    spv_path(vpath, sizeof vpath, "shadow_mesh.vert");
    spv_path(fpath, sizeof fpath, "shadow_mesh.frag");
    // Shadow caster reads only pos (loc 0) + normal (loc 1); uv is irrelevant
    // to a depth-only pass, so bind just the first 2 attrs of the terrain Vtx.
    if (!shadowMeshPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                        sizeof(ShadowPush), sizeof(Vtx),
                                        attrs, 2, /*instanced=*/false)) {
        std::fprintf(stderr, "[Renderer3DVk] shadow mesh pipeline FAILED\n");
    }

    spv_path(vpath, sizeof vpath, "shadow_bb.vert");
    spv_path(fpath, sizeof fpath, "shadow_bb.frag");
    {
        VkVertexInputAttributeDescription tAttrs[4]{};
        tAttrs[0].location = 0; tAttrs[0].binding = 0;
        tAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; tAttrs[0].offset = 0;
        tAttrs[1].location = 1; tAttrs[1].binding = 0;
        tAttrs[1].format = VK_FORMAT_R32_SFLOAT; tAttrs[1].offset = sizeof(float) * 3;
        tAttrs[2].location = 2; tAttrs[2].binding = 0;
        tAttrs[2].format = VK_FORMAT_R32_SFLOAT; tAttrs[2].offset = sizeof(float) * 4;
        tAttrs[3].location = 3; tAttrs[3].binding = 0;
        tAttrs[3].format = VK_FORMAT_R32_SFLOAT; tAttrs[3].offset = sizeof(float) * 5;
        if (!shadowTreePipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                            sizeof(ShadowBbPush), sizeof(TreeInstance),
                                            tAttrs, 4, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow tree pipeline FAILED\n");
        }
    }

    spv_path(vpath, sizeof vpath, "shadow_struct.vert");
    spv_path(fpath, sizeof fpath, "shadow_struct.frag");
    {
        VkVertexInputAttributeDescription sAttrs[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            sAttrs[i].location = i; sAttrs[i].binding = 0;
        }
        sAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[0].offset = 0;
        sAttrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[1].offset = sizeof(float) * 3;
        sAttrs[2].format = VK_FORMAT_R32_SFLOAT;       sAttrs[2].offset = sizeof(float) * 6;
        sAttrs[3].format = VK_FORMAT_R32_SFLOAT;       sAttrs[3].offset = sizeof(float) * 7;
        if (!shadowStructPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                              sizeof(ShadowPush), sizeof(StructInstance),
                                              sAttrs, 4, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow struct pipeline FAILED\n");
        }
    }

    // A9: NPC shadow casters (shadow_npc.vert + shadow_npc.frag, instanced).
    spv_path(vpath, sizeof vpath, "shadow_npc.vert");
    spv_path(fpath, sizeof fpath, "shadow_npc.frag");
    {
        VkVertexInputAttributeDescription nAttrs[4]{};
        nAttrs[0].location = 0; nAttrs[0].binding = 0;
        nAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; nAttrs[0].offset = 0;
        nAttrs[1].location = 1; nAttrs[1].binding = 0;
        nAttrs[1].format = VK_FORMAT_R32_SFLOAT; nAttrs[1].offset = sizeof(float) * 3;
        nAttrs[2].location = 2; nAttrs[2].binding = 0;
        nAttrs[2].format = VK_FORMAT_R32_UINT; nAttrs[2].offset = sizeof(float) * 4;
        nAttrs[3].location = 3; nAttrs[3].binding = 0;
        nAttrs[3].format = VK_FORMAT_R32_UINT; nAttrs[3].offset = sizeof(float) * 5;
        if (!shadowNpcPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                            sizeof(ShadowBbPush), sizeof(NpcInstance),
                                            nAttrs, 4, /*instanced=*/true,
                                            paperdoll_.set_layout())) {
            std::fprintf(stderr, "[Renderer3DVk] shadow npc pipeline FAILED\n");
        }
    }

    // A8: Creature shadow caster (shadow_creature.vert/frag, no descriptor).
    // Reads only pos/size/archetype/seed (4 attrs); the frag discards the same
    // silhouette as the lit pass so the cast shadow matches the billboard.
    spv_path(vpath, sizeof vpath, "shadow_creature.vert");
    spv_path(fpath, sizeof fpath, "shadow_creature.frag");
    {
        VkVertexInputAttributeDescription cAttrs[4]{};
        cAttrs[0].location = 0; cAttrs[0].binding = 0;
        cAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; cAttrs[0].offset = 0;
        cAttrs[1].location = 1; cAttrs[1].binding = 0;
        cAttrs[1].format = VK_FORMAT_R32_SFLOAT; cAttrs[1].offset = sizeof(float) * 3;
        cAttrs[2].location = 2; cAttrs[2].binding = 0;
        cAttrs[2].format = VK_FORMAT_R32_SFLOAT; cAttrs[2].offset = sizeof(float) * 4;
        cAttrs[3].location = 3; cAttrs[3].binding = 0;
        cAttrs[3].format = VK_FORMAT_R32_SFLOAT; cAttrs[3].offset = sizeof(float) * 5;
        if (!shadowCreaturePipe_.create_shadow(
                dev, shadow_.renderPass, vpath, fpath,
                sizeof(ShadowBbPush), sizeof(CreatureInstance),
                cAttrs, 4, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow creature pipeline FAILED\n");
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// destroy — free all GPU resources.
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::destroy(const gpu::VulkanDevice& dev) {
    terrainIdx_.destroy(dev);
    terrainVtx_.destroy(dev);
    terrainPipe_.destroy(dev);
    skyPipe_.destroy(dev);
    waterPipe_.destroy(dev);
    treePipe_.destroy(dev);
    treeInstBuf_.destroy(dev);
    treeCount_ = 0;
    structPipe_.destroy(dev);
    structInstBuf_.destroy(dev);
    structCount_ = 0;
    npcPipe_.destroy(dev);
    npcInstBuf_.destroy(dev);
    npcCount_ = 0;
    shadowMeshPipe_.destroy(dev);
    shadowTreePipe_.destroy(dev);
    shadowStructPipe_.destroy(dev);
    shadowNpcPipe_.destroy(dev);
    creaturePipe_.destroy(dev);
    creatureInstBuf_.destroy(dev);
    creatureCount_ = 0;
    shadowCreaturePipe_.destroy(dev);
    particlePipe_.destroy(dev);
    particleInstBuf_.destroy(dev);
    particleCount_ = 0;
    paperdoll_.destroy(dev);
    shadow_.destroy(dev);
    for (int i = 0; i < kFramesInFlight; ++i) lightBuf_[i].destroy(dev);
    if (shadowPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev.device, shadowPool_, nullptr);
        shadowPool_ = VK_NULL_HANDLE;
        for (int i = 0; i < kFramesInFlight; ++i)
            shadowSet_[i] = VK_NULL_HANDLE;
    }
    if (shadowSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout_, nullptr);
        shadowSetLayout_ = VK_NULL_HANDLE;
    }
    materialTex_.destroy(dev);
    materialTexAlt_.destroy(dev);
    if (materialPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev.device, materialPool_, nullptr);
        materialPool_ = VK_NULL_HANDLE;
        materialSet_  = VK_NULL_HANDLE;
    }
    if (materialSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev.device, materialSetLayout_, nullptr);
        materialSetLayout_ = VK_NULL_HANDLE;
    }
    indexCount_ = 0;
    heightVtxM_.clear();
    dev_  = nullptr;
    pass_ = VK_NULL_HANDLE;
    uploaded_ = false;
}


void Renderer3DVk::prepare_frame(VkCommandBuffer cmd, ecs::World* ecs,
                                  float elapsed) {
    npcCount_ = 0;
    if (ecs && uploaded_) {
        std::vector<NpcInstance> npcs;
        npcs.reserve(kMaxEntityInstances);
        const float tMs = elapsed * 1000.0f;
        // Exclude the player body: first-person, the camera sits at it, so a
        // possessed humanoid (Inc 5c — a foreign body carrying PlayerTag now
        // also has an NpcCharacter) must not be drawn over the lens. The hero
        // husk carries no NpcCharacter and never matched anyway.
        auto view = ecs->reg.view<ecs::Position, ecs::NpcCharacter>(
            entt::exclude<ecs::PlayerTag>);
        for (auto e : view) {
            if (npcs.size() >= kMaxEntityInstances) break;
            const auto& pos = view.get<ecs::Position>(e);
            const auto& ch = view.get<ecs::NpcCharacter>(e);
            float vx = 0.0f;
            float vy = 0.0f;
            if (const ecs::SubworldAi* ai = ecs->reg.try_get<ecs::SubworldAi>(e)) {
                vx = ai->vx;
                vy = ai->vy;
            }
            const bool moving = vx * vx + vy * vy > 0.0001f;
            const character::AnimationState anim =
                character::make_animation_state(
                    moving ? character::AnimationType::Walk
                           : character::AnimationType::Idle,
                    direction_from_velocity(vx, vy), tMs);
            
            // Quantize the visual seed into the 256-seed pool for visual diversity
            const std::uint32_t quantizedSeed = (ch.visualSeed % 256) + 1;
            
            const auto& desc = paperdoll_.descriptor_for_seed(quantizedSeed);
            std::uint32_t descIndex = paperdoll_.register_descriptor(desc);

            int tileIndex = character::calculate_tile_index(anim.animation, anim.direction, anim.frame);
            std::uint32_t packedAnim = (std::uint32_t(tileIndex) << 16) | (std::uint32_t(anim.direction) & 0xFF);

            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            NpcInstance inst{};
            inst.px = wx;
            inst.py = pos.z;
            inst.pz = wz;
            inst.size = 2.0f;
            inst.descIndex = descIndex;
            inst.anim = packedAnim;
            npcs.push_back(inst);
        }
        npcCount_ = static_cast<std::uint32_t>(npcs.size());
        if (npcCount_ > 0) {
            update_instance_buffer(cmd, npcInstBuf_.buffer,
                                   npcs.data(), npcCount_);
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }

    // ── A8: Creatures (procedural fauna billboards) — same per-frame upload
    //    path as NPCs. Any Sprite with archetype != 0xFF is a procedural
    //    creature (fauna/monsters); 0xFF (town paper-doll NPCs, spell
    //    projectiles, engine sprites) is skipped and drawn by its own pass. ──
    creatureCount_ = 0;
    if (ecs && uploaded_) {
        std::vector<CreatureInstance> creatures;
        creatures.reserve(kMaxEntityInstances);
        // Exclude the player body (Inc 5c): possessing a monster/creature moves
        // PlayerTag onto a Sprite-bearing body; in first-person it must not be
        // billboarded at the camera. Hero husk carries no Sprite and never matched.
        auto cview = ecs->reg.view<ecs::Position, ecs::Sprite>(
            entt::exclude<ecs::PlayerTag>);
        for (auto e : cview) {
            if (creatures.size() >= kMaxEntityInstances) break;
            const auto& spr = cview.get<ecs::Sprite>(e);
            if (spr.archetype == 0xFF) continue; // not a procedural creature
            const auto& pos = cview.get<ecs::Position>(e);
            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            CreatureInstance ci{};
            ci.px = wx; ci.py = pos.z; ci.pz = wz;
            ci.size = spr.scale * 1.5f;
            ci.archetype = float(spr.archetype);
            // Use entity id for stable per-instance variation — iteration
            // order in EnTT views is not guaranteed stable across frames.
            const std::uint32_t eid = entt::to_integral(e);
            ci.seed = float(spr.atlasId) * 2.17f + float(eid & 63u) * 0.5f;
            ci.tintR = float(spr.r) / 255.0f;
            ci.tintG = float(spr.g) / 255.0f;
            ci.tintB = float(spr.b) / 255.0f;
            creatures.push_back(ci);
        }
        creatureCount_ = static_cast<std::uint32_t>(creatures.size());
        if (creatureCount_ > 0) {
            update_instance_buffer(cmd, creatureInstBuf_.buffer,
                                   creatures.data(), creatureCount_);
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }
}

void Renderer3DVk::stage_particles(VkCommandBuffer cmd, const void* data,
                                   std::uint32_t count) {
    particleCount_ = std::min(count, kMaxParticleInstances);
    if (particleCount_ == 0 || data == nullptr) return;
    // Same per-frame path as NPC/creature instances: overwrite the device-local
    // buffer in place, then barrier TRANSFER_WRITE → VERTEX_ATTRIBUTE_READ.
    vkCmdUpdateBuffer(cmd, particleInstBuf_.buffer, 0,
                      VkDeviceSize(particleCount_) * sizeof(ParticleInstance),
                      data);
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ──────────────────────────────────────────────────────────────────────
// upload — rebuild device-local terrain mesh from the real composite
// heightmap. Exact port of GL Renderer3D::upload terrain section
// (renderer_3d.cpp lines 853-935).
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::upload(const gpu::VulkanDevice& dev, const SeamlessSubworldManager& mgr,
                          const CompositeDirty& dirty) {
    const int N   = kMeshDim;
    const int Nv  = N + 1;
    const int step = kFullSize / N;
    const auto& hm = mgr.heightmap();
    const auto& tiles = mgr.tiles();
    if (hm.empty()) return;

    // Seam-profiling: per-section wall-clock, printed when TIMAERT_SEAM_TRACE set.
    const bool kProf = std::getenv("TIMAERT_SEAM_TRACE") != nullptr;
    // Self-check: recompute a full reference and byte-compare the incremental
    // result (height + material) to prove the per-cell paths are identical.
    const bool kSelfCheck = std::getenv("TIMAERT_SEAM_SELFCHECK") != nullptr;
    using ProfClock = std::chrono::steady_clock;
    auto profNow = [] { return ProfClock::now(); };
    auto profMs = [](ProfClock::time_point a, ProfClock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto pStart = profNow();
    double msHeight = 0, msVerts = 0, msTerrainBuf = 0, msMat = 0, msTree = 0,
           msStruct = 0, msMatFill = 0;

    // ── Incremental scope ──
    // The first upload (device buffers / image not yet created) forces a full
    // build — the create paths need the whole mesh + material. After that,
    // dirty.full* (seam shift / height smooth) or the per-cell flags (async
    // drains) decide what to rebuild. Rebuilding only the 1–3 stitched cells is
    // what keeps a crossing's drain cascade off the frame-time budget.
    const bool vtxExists = terrainVtx_.buffer != VK_NULL_HANDLE;
    const bool matExists = materialTex_.image != VK_NULL_HANDLE;
    // Seam-crossing relocation. The manager already toroidally shifted its CPU
    // composite arrays; a nonzero shift lets us slide our GPU material image +
    // CPU height grid the same way and rebuild only the fresh cells, instead of
    // a full 3k×3k rebuild. `haveState` gates it to non-first uploads (the first
    // build has nothing to slide). Wired incrementally: `shiftHeight` (3c-2) and
    // `shiftMaterial` (3c-3) flip each kind from full-fallback to the shift path;
    // whatever is not yet wired forces a full rebuild, which re-reads the
    // manager's already-shifted source arrays and is therefore always correct.
    const bool haveState = vtxExists && matExists;
    const bool wantShift = (dirty.shiftX != 0 || dirty.shiftY != 0) && haveState;
    const bool shiftHeight = wantShift;    // 3c-2: height grid slides on a shift
    const bool shiftMaterial = wantShift;  // 3c-3: material image slides on a shift
    const bool doFullHeight =
        dirty.fullHeight || !vtxExists || (wantShift && !shiftHeight);
    const bool doFullMaterial =
        dirty.fullMaterial || !matExists || (wantShift && !shiftMaterial);
    // Material relocation (3c-3): slide the GPU image for the overlap and fill
    // only the fresh cells, instead of rebuilding the whole 3072² material.
    // Gated off when a full rebuild is already happening (first build / explicit
    // fullMaterial / the merge() two-crossing fallback) — the full path re-reads
    // the manager's already-shifted arrays and is always safe.
    const bool doShiftMaterial = shiftMaterial
        && (dirty.shiftX != 0 || dirty.shiftY != 0) && !doFullMaterial;
    const bool doStructs = dirty.structs || !vtxExists;
    bool anyHeightCell = false, anyMatCell = false;
    for (int i = 0; i < 9; ++i) {
        anyHeightCell |= dirty.heightCells[std::size_t(i)];
        anyMatCell |= dirty.materialCells[std::size_t(i)];
    }
    const bool doHeight = doFullHeight || anyHeightCell;
    const bool doMaterial = doFullMaterial || anyMatCell;

    // Absolute origin of this composite (metres). Same anchor the trees use
    // ((centre-1)*kCellSize): fed to mesh.frag so ground synth is keyed to
    // absolute coords and stays put across a seam recentre. The per-cross delta
    // of ±kCellSize cancels vWorld's reindex.
    groundOriginX_ = float((mgr.center_cx() - 1) * kCellSize) * kTileMeters;
    groundOriginY_ = float((mgr.center_cy() - 1) * kCellSize) * kTileMeters;

    // ── Sample heights into a vertex grid in metres ──
    // Box-average each vertex over its step-sized footprint (same as GL), as a
    // pure per-vertex fn so the full and per-cell paths are byte-identical.
    const auto vertexCount = std::size_t(Nv) * Nv;
    heightVtxM_.resize(vertexCount);
    const int half = std::max(1, step / 2);
    // A vertex samples composite tile (x*step); one macro cell spans this many
    // vertices per axis. A cell's re-blit changes exactly the inclusive block
    // [c*cellVerts, (c+1)*cellVerts] — the ±half footprint of a boundary vertex
    // reaches one cell in and no further.
    const int cellVerts = kCellSize / step;
    auto sampleVertex = [&](int x, int y) -> float {
        const int cy = std::min(kFullSize - 1, y * step);
        const int y0 = std::max(0, cy - half);
        const int y1 = std::min(kFullSize - 1, cy + half);
        const int cx = std::min(kFullSize - 1, x * step);
        const int x0 = std::max(0, cx - half);
        const int x1 = std::min(kFullSize - 1, cx + half);
        float sum = 0.0f;
        int count = 0;
        for (int sy = y0; sy <= y1; ++sy) {
            const auto row = std::size_t(sy) * kFullSize;
            for (int sx = x0; sx <= x1; ++sx) {
                sum += hm[row + std::size_t(sx)];
                ++count;
            }
        }
        return (count > 0 ? sum / float(count) : 0.0f) * kHeightScaleM;
    };

    if (doHeight) {
        const auto s = profNow();
        if (doFullHeight) {
            for (int y = 0; y < Nv; ++y)
                for (int x = 0; x < Nv; ++x)
                    heightVtxM_[std::size_t(y) * Nv + x] = sampleVertex(x, y);
        } else {
            // Seam crossing (3c): the manager already toroidally shifted
            // composite_height_; slide our persistent vertex grid the same way
            // (a memmove, in VERTICES: one cell = cellVerts steps) so the 6/9
            // (axis) or 4/9 (diagonal) overlap keeps its heights without
            // resampling, then resample only the fresh cells below. Mirrors
            // shift_buffer() exactly. No-op when shiftX==shiftY==0 (plain drain).
            if (shiftHeight && (dirty.shiftX != 0 || dirty.shiftY != 0)) {
                const int vpx = -dirty.shiftX * cellVerts;
                const int vpy = -dirty.shiftY * cellVerts;
                const int adx = vpx < 0 ? -vpx : vpx;
                const int ady = vpy < 0 ? -vpy : vpy;
                const int copyW = Nv - adx;
                const int copyH = Nv - ady;
                if (copyW > 0 && copyH > 0) {
                    const int srcX = vpx > 0 ? 0 : -vpx;
                    const int dstX = vpx > 0 ? vpx : 0;
                    float* d = heightVtxM_.data();
                    if (vpy > 0) {
                        for (int srcY = copyH - 1; srcY >= 0; --srcY)
                            std::memmove(&d[std::size_t(srcY + vpy) * Nv + dstX],
                                         &d[std::size_t(srcY) * Nv + srcX],
                                         std::size_t(copyW) * sizeof(float));
                    } else {
                        const int srcY0 = -vpy;
                        for (int y = 0; y < copyH; ++y)
                            std::memmove(&d[std::size_t(y) * Nv + dstX],
                                         &d[std::size_t(srcY0 + y) * Nv + srcX],
                                         std::size_t(copyW) * sizeof(float));
                    }
                }
                // sampleVertex clamps its ±half footprint at the composite edge,
                // so an outer-ring vertex needs a smaller footprint than the
                // interior value the memmove slid into it. half<step ⇒ only the
                // 1-vertex-thick border ring clamps — resample it (idempotent
                // with the fresh-cell pass, ~4·Nv verts, all shift directions).
                for (int x = 0; x < Nv; ++x) {
                    heightVtxM_[std::size_t(x)] = sampleVertex(x, 0);
                    heightVtxM_[std::size_t(Nv - 1) * Nv + x] =
                        sampleVertex(x, Nv - 1);
                }
                for (int y = 0; y < Nv; ++y) {
                    heightVtxM_[std::size_t(y) * Nv] = sampleVertex(0, y);
                    heightVtxM_[std::size_t(y) * Nv + (Nv - 1)] =
                        sampleVertex(Nv - 1, y);
                }
            }
            for (int idx = 0; idx < 9; ++idx) {
                if (!dirty.heightCells[std::size_t(idx)]) continue;
                const int ox = idx % 3, oy = idx / 3;
                const int vx0 = ox * cellVerts, vx1 = (ox + 1) * cellVerts;
                const int vy0 = oy * cellVerts, vy1 = (oy + 1) * cellVerts;
                for (int y = vy0; y <= vy1; ++y)
                    for (int x = vx0; x <= vx1; ++x)
                        heightVtxM_[std::size_t(y) * Nv + x] = sampleVertex(x, y);
            }
            if (kSelfCheck) {
                // The shipped game builds this TU with -ffast-math, so
                // sampleVertex is NOT bit-reproducible across its inlined call
                // sites (the reduction reassociates). Bit-exact "==" therefore
                // reports phantom mismatches on the resampled border ring even
                // when the incremental grid is correct. Verify to floating-point
                // tolerance instead — far below any perceptible or structural
                // error — and surface the worst delta so a real regression
                // (wrong cell / off-by-one footprint ⇒ ≥1 world-unit) still
                // stands out against the ~1e-4-unit fast-math noise floor.
                std::size_t mism = 0;
                float maxDiff = 0.0f;
                for (int y = 0; y < Nv; ++y)
                    for (int x = 0; x < Nv; ++x) {
                        const float a = heightVtxM_[std::size_t(y) * Nv + x];
                        const float b = sampleVertex(x, y);
                        const float d = std::fabs(a - b);
                        if (d > maxDiff) maxDiff = d;
                        if (d > 1e-2f + 1e-5f * std::fabs(b)) ++mism;
                    }
                std::fprintf(stderr,
                    "[seam-selfcheck] height incremental mismatch=%zu/%zu "
                    "maxdiff=%.6g\n",
                    mism, vertexCount, double(maxDiff));
                std::fflush(stderr);
            }
        }
        // The window's height content changed (full rebuild, seam shift or
        // dirty-cell resample) — refresh the window max the flight ceiling
        // reads. One pass over ~37k floats, negligible next to the resample.
        maxHeightM_ = 0.0f;
        for (const float h : heightVtxM_)
            if (h > maxHeightM_) maxHeightM_ = h;
        if (kProf) msHeight = profMs(s, profNow());
    }

    // ── Rebuild the vertex buffer (full) whenever any height changed ──
    // The per-vertex build is trivial (~0.1 ms) and every normal reads the now-
    // correct persistent heightVtxM_, so we always rebuild the whole array from
    // it; the savings are above, in only resampling the dirty cells' heights.
    // Index topology is constant → build + upload EXACTLY ONCE. When only the
    // material or structures changed the buffer is untouched (already uploaded).
    if (doHeight) {
        const auto sv = profNow();
        const float cell = 2.0f * kWorldExtent / float(N);
        std::vector<Vtx> verts(vertexCount);
        for (int y = 0; y < Nv; ++y) {
            for (int x = 0; x < Nv; ++x) {
                const auto i = std::size_t(y) * Nv + x;
                float wx = -kWorldExtent + float(x) * cell;
                float wz = -kWorldExtent + float(y) * cell;
                float wy = heightVtxM_[i];

                int xm = std::max(0, x - 1), xp = std::min(Nv - 1, x + 1);
                int ym = std::max(0, y - 1), yp = std::min(Nv - 1, y + 1);
                float hL = heightVtxM_[std::size_t(y) * Nv + xm];
                float hR = heightVtxM_[std::size_t(y) * Nv + xp];
                float hD = heightVtxM_[std::size_t(ym) * Nv + x];
                float hU = heightVtxM_[std::size_t(yp) * Nv + x];
                vec3 n = normalize({hL - hR, 2.0f * cell, hD - hU});

                // Grid UV in [0,1]; mesh.frag samples the full-res tile material
                // texture here (tileX = x*step, so u = x/N maps to the exact tile).
                verts[i] = {wx, wy, wz, n.x, n.y, n.z,
                            float(x) / float(N), float(y) / float(N)};
            }
        }

        if (terrainIdx_.buffer == VK_NULL_HANDLE) {
            std::vector<std::uint32_t> idx;
            idx.reserve(std::size_t(N) * N * 6);
            for (int y = 0; y < N; ++y) {
                for (int x = 0; x < N; ++x) {
                    auto a = std::uint32_t(y) * std::uint32_t(Nv) + std::uint32_t(x);
                    auto b = a + 1;
                    auto c = a + std::uint32_t(Nv);
                    auto d = c + 1;
                    idx.push_back(a); idx.push_back(c); idx.push_back(b);
                    idx.push_back(b); idx.push_back(c); idx.push_back(d);
                }
            }
            if (!terrainIdx_.create_device_local(
                     dev, idx.data(), idx.size() * sizeof(std::uint32_t),
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
                std::fprintf(stderr, "[Renderer3DVk] terrain index buffer FAILED\n");
                indexCount_ = 0;
                uploaded_ = false;
                return;
            }
            indexCount_ = static_cast<std::uint32_t>(idx.size());
        }
        if (kProf) msVerts = profMs(sv, profNow());

        // Vertex buffer is fixed-size (Nv×Nv): create once, then overwrite IN
        // PLACE (no realloc / no queue-idle churn on a fresh buffer).
        const auto sb = profNow();
        const VkDeviceSize vtxBytes = verts.size() * sizeof(Vtx);
        const bool vtxOk =
            (terrainVtx_.buffer == VK_NULL_HANDLE)
                ? terrainVtx_.create_device_local(dev, verts.data(), vtxBytes,
                                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
                : terrainVtx_.update(dev, verts.data(), vtxBytes);
        if (!vtxOk) {
            std::fprintf(stderr, "[Renderer3DVk] terrain vertex buffer FAILED\n");
            uploaded_ = false;
            return;
        }
        uploaded_ = true;
        if (kProf) msTerrainBuf = profMs(sb, profNow());
    }

    // ── Full-resolution tile material texture (sampled per-fragment by
    //    mesh.frag). One byte per tile = material id; NEAREST + clamp so road /
    //    field bands stay crisp regardless of the coarse terrain mesh — the
    //    per-fragment analogue of the TS u_tileGrid (roads read as connected
    //    lines, not blobs between verts). Persistent image: created once, then
    //    overwritten in place — fully on a shift, or per 1024-tile cell on an
    //    async drain. Callers fence upload() against in-flight frames (same
    //    contract as the terrain vertex/index buffers). ──
    if (doMaterial && tiles.size() == std::size_t(kFullSize) * kFullSize) {
        // ONE material fill per cell (sub/material.h): authored tiles map
        // straight through a 256-entry LUT; biome-ground tiles pick their
        // biome via the seam dither over the cell's captured 3×3 ring
        // (pick_ground_biome — early-outs to the owner deep inside the
        // cell, so only the ~256-tile border band pays for the hash).
        // Keyed to ABSOLUTE tile coords + the cell's own ring, both
        // window-independent, so the GPU toroidal shift keeps relocated
        // bytes valid and the selfcheck recompute matches byte-for-byte.
        // `dst` is written at [(dstY0+y)*dstStride + dstX0 + x].
        // Axis tables are cell-size-invariant → build once per upload.
        static_assert(kCellSize > 0, "");
        std::vector<GroundAxis> groundAxis(kCellSize);
        ground_axis_table(kCellSize, groundAxis.data());
        auto fillCellMaterial = [&](int idx, std::uint8_t* dst,
                                    std::size_t dstStride,
                                    int dstX0, int dstY0) {
            const int ox = idx % 3, oy = idx / 3;
            const Biome* ring = mgr.cell_biome_ring(idx);
            std::uint8_t lut[256];
            for (int t = 0; t < 256; ++t)
                lut[t] = static_cast<std::uint8_t>(terrain_material_for(
                    static_cast<std::uint8_t>(t), ring[4]));
            bool uniform = true;
            for (int i = 0; i < 9; ++i) uniform &= ring[i] == ring[4];
            const long long absX0 =
                (long long)(mgr.center_cx() - 1 + ox) * kCellSize;
            const long long absY0 =
                (long long)(mgr.center_cy() - 1 + oy) * kCellSize;
            for (int y = 0; y < kCellSize; ++y) {
                const std::size_t srcRow =
                    std::size_t(oy * kCellSize + y) * kFullSize
                    + std::size_t(ox * kCellSize);
                std::uint8_t* d =
                    dst + std::size_t(dstY0 + y) * dstStride + dstX0;
                if (uniform) {
                    for (int x = 0; x < kCellSize; ++x)
                        d[x] = lut[tiles[srcRow + std::size_t(x)]];
                    continue;
                }
                const GroundAxis ay = groundAxis[std::size_t(y)];
                for (int x = 0; x < kCellSize; ++x) {
                    const std::uint8_t t = tiles[srcRow + std::size_t(x)];
                    if (material_is_authored(t)) {
                        d[x] = lut[t];
                    } else {
                        const Biome b = pick_ground_biome_axis(
                            ring, groundAxis[std::size_t(x)], ay,
                            absX0 + x, absY0 + y);
                        d[x] = b == ring[4]
                            ? lut[t]
                            : static_cast<std::uint8_t>(
                                  terrain_material_for(t, b));
                    }
                }
            }
        };
        auto fillFullMaterial = [&](std::uint8_t* dst) {
            for (int idx = 0; idx < 9; ++idx)
                fillCellMaterial(idx, dst, kFullSize, (idx % 3) * kCellSize,
                                 (idx / 3) * kCellSize);
        };
        // Point material set (set 1, binding 0) at a texture's view+sampler.
        // Used on the first create and after each ping-pong swap. Safe here:
        // upload() runs at the same fenced point as the in-place image updates,
        // so no in-flight frame samples the set (same contract as update_*).
        auto writeMaterialDescriptor = [&](const gpu::VulkanTexture& tex) {
            VkDescriptorImageInfo dii{};
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dii.imageView = tex.view;
            dii.sampler = tex.sampler;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = materialSet_;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &dii;
            vkUpdateDescriptorSets(dev.device, 1, &w, 0, nullptr);
        };

        if (doFullMaterial) {
            const auto sf = profNow();
            std::vector<std::uint8_t> matPix(std::size_t(kFullSize) * kFullSize);
            fillFullMaterial(matPix.data());
            if (kProf) msMatFill = profMs(sf, profNow());
            const auto sg = profNow();
            if (materialTex_.image == VK_NULL_HANDLE) {
                if (!materialTex_.create_r8(dev, kFullSize, kFullSize, matPix.data(),
                                            /*linearFilter=*/false, /*repeat=*/false)) {
                    std::fprintf(stderr, "[Renderer3DVk] material texture FAILED\n");
                } else {
                    // Ping-pong sibling (3c-3): same size/format, seeded with the
                    // same pixels so it is a valid sampled image immediately. It
                    // becomes the GPU copy destination on the first seam crossing.
                    if (!materialTexAlt_.create_r8(
                            dev, kFullSize, kFullSize, matPix.data(),
                            /*linearFilter=*/false, /*repeat=*/false))
                        std::fprintf(stderr,
                            "[Renderer3DVk] material alt texture FAILED\n");
                    writeMaterialDescriptor(materialTex_);
                }
            } else {
                materialTex_.update_region(dev, 0, 0, kFullSize, kFullSize,
                                           matPix.data());
            }
            if (kProf) msMat = profMs(sg, profNow());
        } else if (doShiftMaterial) {
            // Seam crossing (3c-3): the manager already shifted composite_tiles_
            // and per-cell biome by (shiftX,shiftY), so material_new[cell] ==
            // material_old[shifted-from cell] over the overlap — slide it on the
            // GPU (image→image copy, no host round-trip) and rebuild only the
            // fresh cells' LUT. px,py are in TILES, matching shift_buffer() /
            // shift_composite_buffers() exactly (same rect the height grid slid).
            const auto sf = profNow();
            const int px = -dirty.shiftX * kCellSize;
            const int py = -dirty.shiftY * kCellSize;
            const int adx = px < 0 ? -px : px;
            const int ady = py < 0 ? -py : py;
            const int copyW = kFullSize - adx;
            const int copyH = kFullSize - ady;
            const int srcX = px > 0 ? 0 : -px;
            const int dstX = px > 0 ? px : 0;
            const int srcY = py > 0 ? 0 : -py;
            const int dstY = py > 0 ? py : 0;

            // Fill each fresh cell's 1024² material into its own buffer; the
            // FreshRegion pointers must outlive the blit, so keep them all here.
            std::vector<std::uint8_t> freshPix[9];
            std::vector<gpu::FreshRegion> fresh;
            fresh.reserve(9);
            for (int idx = 0; idx < 9; ++idx) {
                if (!dirty.materialCells[std::size_t(idx)]) continue;
                const int ox = idx % 3, oy = idx / 3;
                auto& buf = freshPix[idx];
                buf.resize(std::size_t(kCellSize) * kCellSize);
                fillCellMaterial(idx, buf.data(), kCellSize, 0, 0);
                fresh.push_back({std::uint32_t(ox * kCellSize),
                                 std::uint32_t(oy * kCellSize),
                                 std::uint32_t(kCellSize),
                                 std::uint32_t(kCellSize), buf.data()});
            }
            if (kProf) msMatFill = profMs(sf, profNow());

            const auto sg = profNow();
            const bool blitOk =
                copyW > 0 && copyH > 0
                && gpu::blit_shift_r8(dev, materialTex_, materialTexAlt_,
                                      std::uint32_t(srcX), std::uint32_t(srcY),
                                      std::uint32_t(dstX), std::uint32_t(dstY),
                                      std::uint32_t(copyW), std::uint32_t(copyH),
                                      fresh.data(), fresh.size());
            if (blitOk) {
                // materialTexAlt_ now holds the relocated + refreshed material;
                // make it the sampled image and keep the old one as next scratch.
                std::swap(materialTex_, materialTexAlt_);
                writeMaterialDescriptor(materialTex_);
            } else {
                // Vulkan failure: fall back to a full in-place refresh so the
                // image is never left stale (correctness over speed).
                std::fprintf(stderr,
                    "[Renderer3DVk] material shift blit FAILED — full fallback\n");
                std::vector<std::uint8_t> matPix(
                    std::size_t(kFullSize) * kFullSize);
                fillFullMaterial(matPix.data());
                materialTex_.update_region(dev, 0, 0, kFullSize, kFullSize,
                                           matPix.data());
            }
            if (kProf) msMat = profMs(sg, profNow());

            if (kSelfCheck) {
                // Definitive end-to-end proof for the riskiest sub-step: read the
                // GPU material image back and compare to a from-scratch recompute
                // from the manager's shifted tiles + biome. Catches a wrong copy
                // rect, a missed fresh cell, OR a MoltenVK copy fault — none of
                // which a CPU-only mirror would see. Material ids are exact bytes
                // (no fast-math), so any nonzero mismatch is a real defect.
                std::vector<std::uint8_t> gpuPix;
                std::size_t selfMism = 0;
                if (materialTex_.read_back(dev, gpuPix)
                    && gpuPix.size() == std::size_t(kFullSize) * kFullSize) {
                    std::vector<std::uint8_t> want(
                        std::size_t(kFullSize) * kFullSize);
                    fillFullMaterial(want.data());
                    for (std::size_t i = 0; i < want.size(); ++i)
                        if (gpuPix[i] != want[i]) ++selfMism;
                } else {
                    selfMism = static_cast<std::size_t>(-1);  // readback failed
                }
                std::fprintf(stderr,
                    "[seam-selfcheck] material shift mismatch=%zu\n", selfMism);
                std::fflush(stderr);
            }
        } else {
            // Incremental: recompute + upload only the dirty cells' 1024² rects.
            const auto sf = profNow();
            std::vector<std::uint8_t> refPix;  // full reference, self-check only
            if (kSelfCheck) {
                refPix.assign(std::size_t(kFullSize) * kFullSize, 0);
                fillFullMaterial(refPix.data());
            }
            std::vector<std::uint8_t> sub(std::size_t(kCellSize) * kCellSize);
            double gpuMs = 0.0;
            std::size_t selfMism = 0;
            for (int idx = 0; idx < 9; ++idx) {
                if (!dirty.materialCells[std::size_t(idx)]) continue;
                const int ox = idx % 3, oy = idx / 3;
                fillCellMaterial(idx, sub.data(), kCellSize, 0, 0);
                if (kSelfCheck) {
                    for (int y = 0; y < kCellSize; ++y) {
                        const std::size_t refRow =
                            std::size_t(oy * kCellSize + y) * kFullSize
                            + std::size_t(ox * kCellSize);
                        const std::size_t dstRow = std::size_t(y) * kCellSize;
                        for (int x = 0; x < kCellSize; ++x)
                            if (sub[dstRow + std::size_t(x)]
                                != refPix[refRow + std::size_t(x)]) ++selfMism;
                    }
                }
                const auto sg = profNow();
                materialTex_.update_region(dev, ox * kCellSize, oy * kCellSize,
                                           kCellSize, kCellSize, sub.data());
                gpuMs += profMs(sg, profNow());
            }
            if (kProf) {
                msMat = gpuMs;
                msMatFill = profMs(sf, profNow()) - gpuMs;
            }
            if (kSelfCheck) {
                std::fprintf(stderr,
                    "[seam-selfcheck] material incremental mismatch=%zu\n",
                    selfMism);
                std::fflush(stderr);
            }
        }
    }

    // ── A4/A5: Tree + structure instances from real Structure records ──
    // Both derive from mgr.structures() and sample_height_m(heightVtxM_), so a
    // structure-set change OR any height change (dirty.structs — always set
    // alongside heightCells / fullHeight) rebuilds them together. On a
    // material-only update the instance buffers are left as-is (already uploaded).
    if (doStructs) {
        // A4: tree billboards.
        const auto st = profNow();
        {
            const auto& structs = mgr.structures();
            std::vector<TreeInstance> trees;
            trees.reserve(structs.size());
            for (const auto& s : structs) {
                if (s.kind != Structure::Tree) continue;
                float wx, wz;
                tile_to_world(s.x, s.y, wx, wz);
                const float baseM = sample_height_m(s.x, s.y);
                if (baseM < kSeaLevelM - 0.5f) continue;
                const float sinkM = std::max(1.25f, s.height * 0.08f);
                // Stable hash for seed (same as GL renderer).
                const float absX = float((mgr.center_cx() - 1) * kCellSize) + s.x;
                const float absY = float((mgr.center_cy() - 1) * kCellSize) + s.y;
                std::uint32_t h = std::uint32_t(absX) * 374761u
                    * std::uint32_t{2246822519}
                    ^ std::uint32_t(absY) * 668265u
                    * std::uint32_t{3266489917};
                h ^= h >> 13; h *= std::uint32_t{1274126177}; h ^= h >> 16;
                const float hash01 =
                    float(h & std::uint32_t{0x00ffffff}) / float(0x00ffffff);
                // Species from macro temperature, bilinearly blended across
                // the 3×3 window (cell centres at 0.5/1.5/2.5 cell units) —
                // the old per-cell lookup flipped species along a straight
                // line at every seam (taiga pines snapping to tropic palms).
                // With a smooth temperature the per-tree hash makes species
                // MIX through the band instead.
                const float gxc = std::clamp(s.x / float(kCellSize) - 0.5f,
                                             0.0f, 2.0f);
                const float gyc = std::clamp(s.y / float(kCellSize) - 0.5f,
                                             0.0f, 2.0f);
                const int tx0 = std::min(1, int(gxc)), ty0 = std::min(1, int(gyc));
                const float tfx = gxc - float(tx0), tfy = gyc - float(ty0);
                auto cellT = [&](int cxi, int cyi) {
                    return mgr.cell_temperature(cyi * 3 + cxi);
                };
                const float temp =
                      cellT(tx0, ty0) * (1.0f - tfx) * (1.0f - tfy)
                    + cellT(tx0 + 1, ty0) * tfx * (1.0f - tfy)
                    + cellT(tx0, ty0 + 1) * (1.0f - tfx) * tfy
                    + cellT(tx0 + 1, ty0 + 1) * tfx * tfy;
                const int typeIdx = tree_type_for_temperature(temp, hash01);
                trees.push_back({wx, baseM - sinkM, wz,
                                 s.radius, float(typeIdx),
                                 float(h & 0xffffu) * 0.01f + hash01 * 5.0f});
            }
            treeInstBuf_.destroy(dev);
            treeCount_ = static_cast<std::uint32_t>(trees.size());
            if (treeCount_ > 0) {
                if (!treeInstBuf_.create_device_local(
                         dev, trees.data(), trees.size() * sizeof(TreeInstance),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
                    std::fprintf(stderr, "[Renderer3DVk] tree buffer FAILED\n");
                    treeCount_ = 0;
                }
            }
        }
        if (kProf) msTree = profMs(st, profNow());

        // A5: structure boxes (houses / walls).
        const auto ss = profNow();
        {
            const auto& structs = mgr.structures();
            std::vector<StructInstance> boxes;
            boxes.reserve(structs.size());
            for (const auto& s : structs) {
                if (s.kind != Structure::House && s.kind != Structure::Wall) continue;
                const float baseM = sample_height_m(s.x, s.y);
                if (baseM < kSeaLevelM - 0.5f) continue;
                float wx, wz;
                tile_to_world(s.x, s.y, wx, wz);
                const float radius = std::max(s.kind == Structure::Wall ? 1.2f : 1.6f,
                                              s.radius);
                const float height = std::max(s.kind == Structure::Wall ? 4.0f : 3.5f,
                                              s.height);
                // Per-instance seed hash (same as GL).
                std::uint32_t h = std::uint32_t(s.x * 110351.0f)
                    ^ (std::uint32_t(s.y * 66821.0f) * std::uint32_t{2654435761});
                h ^= h >> 16;
                const float shade = 0.86f + 0.20f * (float(h & 0xffu) / 255.0f);
                boxes.push_back({wx, baseM - 0.05f, wz,
                                 radius, height, radius,
                                 s.kind == Structure::Wall ? 0.0f : 1.0f,
                                 shade});
            }
            structInstBuf_.destroy(dev);
            structCount_ = static_cast<std::uint32_t>(boxes.size());
            if (structCount_ > 0) {
                if (!structInstBuf_.create_device_local(
                         dev, boxes.data(), boxes.size() * sizeof(StructInstance),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
                    std::fprintf(stderr, "[Renderer3DVk] struct buffer FAILED\n");
                    structCount_ = 0;
                }
            }
        }
        if (kProf) msStruct = profMs(ss, profNow());
    }

    if (kProf) {
        std::fprintf(stderr,
            "[upload3d-prof] height=%.3f verts=%.3f terrainBuf=%.3f "
            "matFill=%.3f matGpu=%.3f tree=%.3f struct=%.3f TOTAL=%.3f (ms)\n",
            msHeight, msVerts, msTerrainBuf, msMatFill, msMat,
            msTree, msStruct, profMs(pStart, profNow()));
        std::fflush(stderr);
    }
}

// ──────────────────────────────────────────────────────────────────────
// record_shadow — depth-only casters into the shadow map.
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::record_shadow(VkCommandBuffer cmd, const Camera& cam,
                                  const WorldTime& time) {
    if (!uploaded_ || shadow_.image == VK_NULL_HANDLE) return;

    vec3 lightRight{};
    compute_shadow_basis(cam, time, shadow_.size, lightMvp_, lightRight);
    shadow_.begin(cmd);
    vkCmdSetDepthBias(cmd, 1.0f, 0.0f, 1.5f);

    // Terrain does NOT cast into this single full-subworld map. Letting the
    // receiver mesh shadow itself creates light/dark zebra bands along the coarse
    // terrain triangles; object shadows are the visible gameplay requirement here.

    // Trees (instanced billboards).
    if (treeCount_ > 0) {
        ShadowBbPush sbb{};
        std::memcpy(sbb.lightMvp, lightMvp_.m, sizeof(sbb.lightMvp));
        sbb.lightRight[0] = lightRight.x;
        sbb.lightRight[1] = lightRight.y;
        sbb.lightRight[2] = lightRight.z;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowTreePipe_.pipeline);
        VkDeviceSize sio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &treeInstBuf_.buffer, &sio);
        vkCmdPushConstants(cmd, shadowTreePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sbb), &sbb);
        vkCmdDraw(cmd, 6, treeCount_, 0, 0);
    }

    // Structures (instanced boxes).
    if (structCount_ > 0) {
        ShadowPush ssp{};
        std::memcpy(ssp.lightMvp, lightMvp_.m, sizeof(ssp.lightMvp));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowStructPipe_.pipeline);
        VkDeviceSize sso = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &structInstBuf_.buffer, &sso);
        vkCmdPushConstants(cmd, shadowStructPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(ssp), &ssp);
        vkCmdDraw(cmd, 36, structCount_, 0, 0);
    }

    // NPCs (instanced billboards).
    if (npcCount_ > 0) {
        ShadowBbPush sbb{};
        std::memcpy(sbb.lightMvp, lightMvp_.m, sizeof(sbb.lightMvp));
        sbb.lightRight[0] = lightRight.x;
        sbb.lightRight[1] = lightRight.y;
        sbb.lightRight[2] = lightRight.z;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowNpcPipe_.pipeline);
        const VkDescriptorSet dolls = paperdoll_.descriptor_set();
        if (dolls != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    shadowNpcPipe_.layout, 0, 1, &dolls,
                                    0, nullptr);
        VkDeviceSize sio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &npcInstBuf_.buffer, &sio);
        vkCmdPushConstants(cmd, shadowNpcPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sbb), &sbb);
        vkCmdDraw(cmd, 6, npcCount_, 0, 0);
    }

    // Creatures (instanced billboards) — same silhouette as the lit pass.
    if (creatureCount_ > 0) {
        ShadowBbPush sbb{};
        std::memcpy(sbb.lightMvp, lightMvp_.m, sizeof(sbb.lightMvp));
        sbb.lightRight[0] = lightRight.x;
        sbb.lightRight[1] = lightRight.y;
        sbb.lightRight[2] = lightRight.z;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowCreaturePipe_.pipeline);
        VkDeviceSize sio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &creatureInstBuf_.buffer, &sio);
        vkCmdPushConstants(cmd, shadowCreaturePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sbb), &sbb);
        vkCmdDraw(cmd, 6, creatureCount_, 0, 0);
    }

    shadow_.end(cmd);
}

// ──────────────────────────────────────────────────────────────────────
// record_main — record the main-pass draw commands.
// Currently: terrain only (A1). A2-A7 add sky/water/trees/structs/NPCs.
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::record_main(VkCommandBuffer cmd, VkExtent2D ext,
                               const Camera& cam, const WorldTime& time,
                               float waterLevel,
                               const SeamlessSubworldManager* /*mgr*/,
                               ecs::World* ecs, bool /*haste*/,
                               bool /*flight*/, float /*px*/, float /*py*/,
                               float elapsed, std::uint32_t frameIndex) {
    if (!uploaded_ || dev_ == nullptr || indexCount_ == 0) return;

    // Select this frame's light-SSBO / descriptor-set ring slot. acquire_frame
    // has already reset this frame's fence, so lightBuf_[slot] is GPU-idle and
    // safe to overwrite with no stall (the no-stall transfer contract of
    // create_host_mapped). We write it in place through the persistent mapping —
    // no staging, no barrier — then bind its ring set. Guard the index so a
    // ring-depth mismatch can never OOB.
    const std::uint32_t slot = frameIndex % kFramesInFlight;
    const VkDescriptorSet litSet = shadowSet_[slot];
    // camPos (world metres) is the cull origin — see gather_point_lights: when
    // more than kSubworldMaxLights emitters are live, the nearest to the camera
    // survive, so the player's own light (riding the camera) is never dropped.
    gather_point_lights(ecs, slot, cam.pos);

    // Fullscreen viewport + scissor so the subworld covers the entire
    // swapchain extent (the macro renderer sets its own; we must match).
    VkViewport vp{};
    vp.width    = static_cast<float>(ext.width);
    vp.height   = static_cast<float>(ext.height);
    vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.extent = ext;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // ── Camera matrices (Vulkan conventions) ──
    const float aspect = static_cast<float>(ext.width)
                         / static_cast<float>(std::max(ext.height, 1u));
    const float fovRad = cam.fovDeg * 0.0174533f;
    mat4 proj = vk_perspective(fovRad, aspect, 0.5f, 1500.0f);
    vec3 fwd  = cam.forward();
    vec3 rgt  = cam.right();
    vec3 upv  = cross(rgt, fwd);
    mat4 view = mat4_lookAt(cam.pos, cam.pos + fwd, {0, 1, 0});
    mat4 mvp  = mat4_mul(proj, view);

    // ── Lighting ──
    SunInfo sun = compute_sun(time);
    const float tod = (float(time.hour) + float(time.minute) / 60.0f) / 24.0f;

    mat4 lightMvp = lightMvp_;

    // ── A2: Sky (fullscreen, behind everything, drawn first) ──
    {
        SkyPush sky{};
        sky.forward[0] = fwd.x;  sky.forward[1] = fwd.y;  sky.forward[2] = fwd.z;
        sky.right[0]   = rgt.x;  sky.right[1]   = rgt.y;  sky.right[2]   = rgt.z;
        sky.up[0]      = upv.x;  sky.up[1]      = upv.y;  sky.up[2]      = upv.z;
        sky.p0[0] = static_cast<float>(ext.width);
        sky.p0[1] = static_cast<float>(ext.height);
        sky.p0[2] = fovRad;
        sky.p0[3] = tod;
        sky.p1[0] = sun.ambientColor.x; // fog = ambient for now
        sky.p1[1] = sun.ambientColor.y;
        sky.p1[2] = sun.ambientColor.z;
        sky.p1[3] = elapsed;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          skyPipe_.pipeline);
        vkCmdPushConstants(cmd, skyPipe_.layout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sky), &sky);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // ── A1: Terrain mesh ──
    MeshPush push{};
    std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
    // Shaders use sunDir as L in N·L: direction FROM world TOWARD the sun.
    push.sunDir[0] = sun.sunDir.x;
    push.sunDir[1] = sun.sunDir.y;
    push.sunDir[2] = sun.sunDir.z;
    push.sunColor[0] = sun.sunColor.x;
    push.sunColor[1] = sun.sunColor.y;
    push.sunColor[2] = sun.sunColor.z;
    push.ambient[0] = sun.ambientColor.x;
    push.ambient[1] = sun.ambientColor.y;
    push.ambient[2] = sun.ambientColor.z;
    std::memcpy(push.lightMvp, lightMvp.m, sizeof(push.lightMvp));
    // Absolute ground origin packed into the unused sunDir.w / sunColor.w lanes
    // (see header + mesh.frag): keeps procedural ground detail world-anchored so
    // it does not "pop" when the seamless window recentres at a seam crossing.
    push.sunDir[3]   = groundOriginX_;
    push.sunColor[3] = groundOriginY_;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      terrainPipe_.pipeline);
    // set 0 = shadow sampler + light SSBO, set 1 = full-res tile material tex.
    if (litSet != VK_NULL_HANDLE && materialSet_ != VK_NULL_HANDLE) {
        const VkDescriptorSet sets[2] = {litSet, materialSet_};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                terrainPipe_.layout, 0, 2, sets, 0, nullptr);
    }
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &terrainVtx_.buffer, &off);
    vkCmdBindIndexBuffer(cmd, terrainIdx_.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cmd, terrainPipe_.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);

    // ── A4: Trees (instanced billboards, after terrain, before water) ──
    if (treeCount_ > 0) {
        BbPush bb{};
        std::memcpy(bb.mvp, mvp.m, sizeof(bb.mvp));
        bb.camRight[0] = rgt.x;
        bb.camRight[1] = rgt.y;
        bb.camRight[2] = rgt.z;
        bb.sunColor[0] = sun.sunColor.x;
        bb.sunColor[1] = sun.sunColor.y;
        bb.sunColor[2] = sun.sunColor.z;
        bb.ambient[0] = sun.ambientColor.x;
        bb.ambient[1] = sun.ambientColor.y;
        bb.ambient[2] = sun.ambientColor.z;
        std::memcpy(bb.lightMvp, lightMvp.m, sizeof(bb.lightMvp));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          treePipe_.pipeline);
        if (litSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    treePipe_.layout, 0, 1, &litSet,
                                    0, nullptr);
        VkDeviceSize tio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &treeInstBuf_.buffer, &tio);
        vkCmdPushConstants(cmd, treePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(bb), &bb);
        vkCmdDraw(cmd, 6, treeCount_, 0, 0);
    }

    // ── A5: Structures (instanced boxes, after trees, before water) ──
    if (structCount_ > 0) {
        MeshPush sp{};
        std::memcpy(sp.mvp, mvp.m, sizeof(sp.mvp));
        sp.sunDir[0] = sun.sunDir.x;
        sp.sunDir[1] = sun.sunDir.y;
        sp.sunDir[2] = sun.sunDir.z;
        sp.sunColor[0] = sun.sunColor.x;
        sp.sunColor[1] = sun.sunColor.y;
        sp.sunColor[2] = sun.sunColor.z;
        sp.ambient[0] = sun.ambientColor.x;
        sp.ambient[1] = sun.ambientColor.y;
        sp.ambient[2] = sun.ambientColor.z;
        std::memcpy(sp.lightMvp, lightMvp.m, sizeof(sp.lightMvp));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          structPipe_.pipeline);
        if (litSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    structPipe_.layout, 0, 1, &litSet,
                                    0, nullptr);
        VkDeviceSize sio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &structInstBuf_.buffer, &sio);
        vkCmdPushConstants(cmd, structPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sp), &sp);
        vkCmdDraw(cmd, 36, structCount_, 0, 0);
    }

    // ── A7: NPCs (drawn after structures) ──
    if (npcCount_ > 0) {
        BbPush nb{};
        std::memcpy(nb.mvp, mvp.m, sizeof(nb.mvp));
        nb.camRight[0] = rgt.x;
        nb.camRight[1] = rgt.y;
        nb.camRight[2] = rgt.z;
        nb.camRight[3] = 0.0f;
        nb.sunColor[0] = sun.sunColor.x;
        nb.sunColor[1] = sun.sunColor.y;
        nb.sunColor[2] = sun.sunColor.z;
        nb.ambient[0] = sun.ambientColor.x;
        nb.ambient[1] = sun.ambientColor.y;
        nb.ambient[2] = sun.ambientColor.z;
        std::memcpy(nb.lightMvp, lightMvp.m, sizeof(nb.lightMvp));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          npcPipe_.pipeline);
        if (litSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    npcPipe_.layout, 0, 1, &litSet,
                                    0, nullptr);
        const VkDescriptorSet dolls = paperdoll_.descriptor_set();
        if (dolls != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    npcPipe_.layout, 1, 1, &dolls,
                                    0, nullptr);
        VkDeviceSize nio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &npcInstBuf_.buffer, &nio);
        vkCmdPushConstants(cmd, npcPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(nb), &nb);
        vkCmdDraw(cmd, 6, npcCount_, 0, 0);
    }

    // ── A8: Creatures (procedural fauna billboards, after NPCs, before water) ──
    if (creatureCount_ > 0) {
        BbPush cb{};
        std::memcpy(cb.mvp, mvp.m, sizeof(cb.mvp));
        cb.camRight[0] = rgt.x;
        cb.camRight[1] = rgt.y;
        cb.camRight[2] = rgt.z;
        cb.camRight[3] = 0.0f;
        cb.sunColor[0] = sun.sunColor.x;
        cb.sunColor[1] = sun.sunColor.y;
        cb.sunColor[2] = sun.sunColor.z;
        cb.ambient[0] = sun.ambientColor.x;
        cb.ambient[1] = sun.ambientColor.y;
        cb.ambient[2] = sun.ambientColor.z;
        std::memcpy(cb.lightMvp, lightMvp.m, sizeof(cb.lightMvp));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          creaturePipe_.pipeline);
        if (litSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    creaturePipe_.layout, 0, 1, &litSet,
                                    0, nullptr);
        VkDeviceSize cio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &creatureInstBuf_.buffer, &cio);
        vkCmdPushConstants(cmd, creaturePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(cb), &cb);
        vkCmdDraw(cmd, 6, creatureCount_, 0, 0);
    }

    // ── FX: Additive particles (after creatures, before water). Emissive: no
    //    descriptor set, no shadow. depth-test on / write off so terrain and
    //    creatures occlude them but they never occlude each other; additive
    //    blend is order-independent so no sort. Skips itself when the pool is
    //    empty (the common case ⇒ zero cost). ──
    if (particleCount_ > 0) {
        ParticlePush pp{};
        std::memcpy(pp.mvp, mvp.m, sizeof(pp.mvp));
        pp.camRight[0] = rgt.x; pp.camRight[1] = rgt.y; pp.camRight[2] = rgt.z;
        pp.camUp[0] = upv.x;    pp.camUp[1] = upv.y;    pp.camUp[2] = upv.z;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          particlePipe_.pipeline);
        VkDeviceSize pio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &particleInstBuf_.buffer, &pio);
        vkCmdPushConstants(cmd, particlePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 6, particleCount_, 0, 0);
    }

    // ── A3: Water (transparent quad, drawn last, depth test but no write) ──
    {
        WaterPush wp{};
        std::memcpy(wp.mvp, mvp.m, sizeof(wp.mvp));
        wp.camPos[0] = cam.pos.x;
        wp.camPos[1] = cam.pos.y;
        wp.camPos[2] = cam.pos.z;
        wp.sunDir[0] = sun.sunDir.x;
        wp.sunDir[1] = sun.sunDir.y;
        wp.sunDir[2] = sun.sunDir.z;
        wp.sunColor[0] = sun.sunColor.x;
        wp.sunColor[1] = sun.sunColor.y;
        wp.sunColor[2] = sun.sunColor.z;
        wp.params[0] = elapsed;                   // animated wave time
        wp.params[1] = sun.ambientColor.y;         // ambient intensity
        wp.params[2] = waterLevel * kHeightScaleM;  // world-space water Y
        wp.params[3] = kWorldExtent;               // half terrain span
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          waterPipe_.pipeline);
        // set 0 = shadow sampler + point-light SSBO (reflected as glints on the
        // waves via point_lights_spec). Same ring slot as every other lit pass.
        if (litSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    waterPipe_.layout, 0, 1, &litSet, 0, nullptr);
        vkCmdPushConstants(cmd, waterPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(wp), &wp);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// gather_point_lights — the ONE universal point-light gather.
// ──────────────────────────────────────────────────────────────────────
// Pack every LightEmitter-bearing subworld entity into the frame's host-mapped
// light SSBO (set 0, binding 1) that shaders/lighting.glsl point_lights() sums.
// This is the single place any positional light enters the renderer: the player
// lantern, NPC torches, spell / projectile glows and lit windows all attach the
// same ecs::LightEmitter and are treated identically here — no per-emitter code.
//
// Positions are built in the SAME window/composite space as the terrain's
// vWorld (tile_to_world for XZ, sample_height_m for the ground Y), then the
// emitter's world-space offset is added — so a light lines up exactly with the
// surface the shader lights. Count is clamped to the SSBO budget.
void Renderer3DVk::gather_point_lights(ecs::World* ecs, std::uint32_t slot,
                                       const sm::vec3& camPos) {
    if (slot >= kFramesInFlight || lightBuf_[slot].mapped == nullptr) return;
    auto* buf = static_cast<GpuLightBuffer*>(lightBuf_[slot].mapped);
    std::uint32_t n = 0;
    if (ecs != nullptr && uploaded_) {
        // SubworldTag scopes to the live scene; the player entity carries it too,
        // so its lantern is gathered through this very view with no special-case.
        auto view = ecs->reg.view<ecs::Position, ecs::LightEmitter,
                                   ecs::SubworldTag>(entt::exclude<ecs::Dead>);
        // Gather EVERY candidate first (each is one GpuLight built exactly as
        // before), with NO upper bound while collecting, then cull to the SSBO
        // budget by nearest-to-camera (cull_nearest_lights, a pure Vulkan-free
        // helper unit-tested in point_light_cull_test). When the scene has at
        // most kSubworldMaxLights emitters the cull is a no-op and the buffer is
        // written verbatim in gather order — byte-identical to the pre-cull
        // path. Only on OVERFLOW do the nearest win, so the player's own light
        // (riding the camera at ≈0 distance) is never dropped for a distant
        // torch. The reused thread_local vector keeps this allocation-free after
        // the first frame that hits its high-water mark.
        static thread_local std::vector<GpuLight> cands;
        cands.clear();
        for (auto e : view) {
            const auto& pos = view.get<ecs::Position>(e);
            const auto& le  = view.get<ecs::LightEmitter>(e);
            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            const float wy = pos.z;
            GpuLight g{};
            g.pos[0] = wx + le.offX;
            g.pos[1] = wy + le.offY;
            g.pos[2] = wz + le.offZ;
            g.pos[3] = le.radius;
            g.color[0] = le.r;
            g.color[1] = le.g;
            g.color[2] = le.b;
            g.color[3] = le.intensity;
            cands.push_back(g);
        }
        cull_nearest_lights(cands, camPos,
                            static_cast<std::size_t>(kSubworldMaxLights));
        for (const auto& g : cands) {
            buf->lights[n] = g;
            ++n;
        }
    }
    buf->count = n;
}

// ──────────────────────────────────────────────────────────────────────
// tile_to_world — composite tile coords → world metres.
// Exact copy from GL Renderer3D.
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::tile_to_world(float tileX, float tileY,
                                 float& wx, float& wz) {
    wx = (tileX - float(kFullSize) * 0.5f) * kTileMeters;
    wz = (tileY - float(kFullSize) * 0.5f) * kTileMeters;
}

// ──────────────────────────────────────────────────────────────────────
// sample_height_m — bilinear sample of cached heightmap (metres).
// Exact copy from GL Renderer3D.
// ──────────────────────────────────────────────────────────────────────
float Renderer3DVk::sample_height_m(float tileX, float tileY) const {
    if (heightVtxM_.empty()) return 0.0f;
    const int Nv = kMeshDim + 1;
    float fx = tileX * float(kMeshDim) / float(kFullSize);
    float fy = tileY * float(kMeshDim) / float(kFullSize);
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > float(Nv - 1)) fx = float(Nv - 1);
    if (fy > float(Nv - 1)) fy = float(Nv - 1);
    int xi = int(fx), yi = int(fy);
    int xn = xi + 1; if (xn > Nv - 1) xn = Nv - 1;
    int yn = yi + 1; if (yn > Nv - 1) yn = Nv - 1;
    float tx = fx - float(xi), ty = fy - float(yi);
    float h00 = heightVtxM_[std::size_t(yi) * Nv + xi];
    float h10 = heightVtxM_[std::size_t(yi) * Nv + xn];
    float h01 = heightVtxM_[std::size_t(yn) * Nv + xi];
    float h11 = heightVtxM_[std::size_t(yn) * Nv + xn];
    float a = h00 * (1.0f - tx) + h10 * tx;
    float b = h01 * (1.0f - tx) + h11 * tx;
    return a * (1.0f - ty) + b * ty;
}

} // namespace sm::sub
