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
#include "sub/sky.h"
#include "sub/tree_atlas.h"
#include "gpu/bb_instance.h"
#include "gpu/vk_device.h"
#include "gpu/vk_renderer.h"
#include "macro/fauna.h"
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
// 224 bytes (= 14 × vec4), within MoltenVK's ≥256 B limit. Filled verbatim
// from sub/sky.h's SkyContext — the sky submodule's one door.
struct SkyPush {
    float forward[4]; // xyz camera forward, w = moonCount
    float right[4];   // xyz camera right,   w = starSizeScale
    float up[4];      // xyz camera up,      w = (reserved: weather)
    float p0[4];      // resX, resY, fov, tod
    float p1[4];      // fogR, fogG, fogB, time(sec)
    float sun[4];     // xyz = lighting.h's sunDir — the ONE celestial vector
    float moonDirSize[sub::kSkyMaxMoons][4];  // xyz toward moon, w baseSize
    float moonColIllum[sub::kSkyMaxMoons][4]; // rgb tint, w illuminated frac
    float p2[4];      // cloudiness01, windX, windZ, precip01 (weather seam)
    float p3[4];      // seasonal day-sky tint multiplier rgb, w = storm flash
};

// Push-constant block for the precipitation overlay — matches precip.frag.
// 32 bytes (= 2 × vec4). The atmosphere submodule's SECOND fullscreen pass.
struct PrecipPush {
    float p0[4]; // resX, resY, time(sec), precip01
    float p1[4]; // kind (0 rain, 1 snow, 2 hail), tilt, flash01, reserved
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

// The three billboard passes (trees, creatures, NPCs) share ONE instance
// record and ONE attribute table: gpu/bb_instance.h — the same header the
// smoke harness includes, so the contract cannot drift by copy. Width and
// height are BOTH carried in metres per instance: every aspect decision (tree
// atlas law, creature body plan) is the CPU's business, so the lit pass and
// the shadow caster that share the buffer cannot drift apart on a ratio.

// Push-constant block for billboard pass (trees/NPCs) — matches billboard.vert.
// 176 bytes (= 11 × vec4).
struct BbPush {
    float mvp[16];
    float camRight[4];
    float sunColor[4];
    float ambient[4];
    float lightMvp[16];
};

// Per-instance data for structure boxes/cylinders — matches struct.vert /
// struct_cyl.vert input. Boxes and cylinders (Structure::Shape) share this
// layout and split into two instance buffers, one draw each.
struct StructInstance {
    float px, py, pz; // centre (world)
    float hx, hy, hz; // half-extents (cylinder: hx = hz = radius)
    float type;       // 0 = wall, 1 = house
    float seed;
    float yaw;        // rotation about vertical, radians (unused on cylinders)
};

// Vertex count of the procedural cylinder prism (struct_cyl.vert /
// shadow_cyl.vert): kSides(12) × (6 side + 3 cap) verts.
constexpr std::uint32_t kCylVertexCount = 12u * 9u;

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

// Returns the fitted light-space span (metres) so the caller can report the
// map's real texel density (TIMAERT_SHADOW_STATS).
//
// minYM/maxYM bound the volume VERTICALLY to the real world: the window's
// terrain range plus a structure allowance. This used to be a constant
// ±600/900 m box around the camera, and at a low sun that fictitious
// 1.5-kilometre vertical projected almost fully into the light's 2D span —
// the map was metres-per-texel every morning and evening no matter how tight
// the horizontal radius was.
float compute_shadow_basis(const Camera& cam, const WorldTime& time,
                           std::uint32_t shadowSize, float minYM, float maxYM,
                           mat4& lightMvp, vec3& lightRight) {
    constexpr float kShadowRadiusM = 1024.0f;
    // Headroom above the tallest terrain vertex for what stands ON it: walls,
    // towers, the spire, trees (tree_atlas heights are well under this).
    constexpr float kShadowStructureAllowanceM = 35.0f;
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
                         minYM - 5.0f,
                         cam.pos.z - kShadowRadiusM};
    const vec3 boxMax = {cam.pos.x + kShadowRadiusM,
                         maxYM + kShadowStructureAllowanceM,
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
    return span;
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

    std::vector<gpu::BbInstance> dummyNpcs(kMaxEntityInstances);
    if (!npcInstBuf_.create_device_local(dev, dummyNpcs.data(),
                                         dummyNpcs.size() * sizeof(gpu::BbInstance),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] npc buffer FAILED\n");
    }
    std::vector<gpu::BbInstance> dummyCreatures(kMaxEntityInstances);
    if (!creatureInstBuf_.create_device_local(
            dev, dummyCreatures.data(),
            dummyCreatures.size() * sizeof(gpu::BbInstance),
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
    // The pool's staging-ring slicing must match OUR frame ring exactly —
    // a deeper swapchain ring with an unchanged pool would reopen the
    // staging WAR race (the 2026-08-10 flickering villagers).
    static_assert(character::PaperdollAtlas::kPoolFramesInFlight
                      == kFramesInFlight,
                  "paperdoll pool staging slices must match frames in flight");
    if (!paperdoll_.init(dev)) {
        std::fprintf(stderr, "[Renderer3DVk] paperdoll init FAILED\n");
    }

    // ── A6: Shadow map + descriptor set (created first so main pipelines
    //    can reference shadowSetLayout_). ──
    if (!shadow_.init(dev, 4096)) {
        std::fprintf(stderr, "[Renderer3DVk] shadow map FAILED\n");
    }
    // The window heightfield for the terrain-occlusion march. Created ZEROED
    // here because the set-0 descriptor below needs a valid view before the
    // first upload(); flat-zero heights occlude nothing, so frames rendered
    // before the first upload are simply unoccluded — honest and inert.
    {
        const std::uint32_t Nv = std::uint32_t(kMeshDim + 1);
        std::vector<float> zeros(std::size_t(Nv) * Nv, 0.0f);
        if (!heightTex_.create_r32f(dev, Nv, Nv, zeros.data(),
                                    /*linearFilter=*/true, /*repeat=*/false)) {
            std::fprintf(stderr, "[Renderer3DVk] height texture FAILED\n");
        }
    }
    {
        // Set 0 = { binding 0: shadow-map sampler,
        //           binding 1: per-frame point-light SSBO,
        //           binding 2: window heightfield (terrain-occlusion march) }.
        // This one set is bound by every lit pass, so adding the light buffer
        // here lit them all with a single shader addition (Inc 2) rather than
        // six — and the heightfield arrived the same way (one binding here,
        // one function in lighting.glsl).
        VkDescriptorSetLayoutBinding b[3]{};
        b[0].binding = 0;
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[1].binding = 1;
        b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[1].descriptorCount = 1;
        b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[2].binding = 2;
        b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[2].descriptorCount = 1;
        b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 3;
        dlci.pBindings = b;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &shadowSetLayout_);

        // Shadow sampler + heightfield sampler + one light SSBO per frame.
        VkDescriptorPoolSize ps[2]{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight * 2},
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
        VkDescriptorImageInfo diiHeight{};
        diiHeight.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        diiHeight.imageView = heightTex_.view;
        diiHeight.sampler = heightTex_.sampler;
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
            VkWriteDescriptorSet writes[3]{};
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
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = shadowSet_[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].pImageInfo = &diiHeight;
            vkUpdateDescriptorSets(dev.device, 3, writes, 0, nullptr);
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

    // A2: Sky pipeline (fullscreen.vert + sky.frag, no vertex input, depth
    // off) + the constellation-star UBO at set 0: authored star directions
    // (macro/celestial.h) written ONCE here — static data, static buffer.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 1;
        dlci.pBindings = &b;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &skySetLayout_);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &skyPool_);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = skyPool_;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &skySetLayout_;
        vkAllocateDescriptorSets(dev.device, &dsai, &skySet_);

        sub::SkyStarsUbo stars{};
        sub::fill_sky_stars(stars);
        skyStarsBuf_.create_device_local(dev, &stars, sizeof(stars),
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorBufferInfo bi{};
        bi.buffer = skyStarsBuf_.buffer;
        bi.range  = sizeof(stars);
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = skySet_;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(dev.device, 1, &w, 0, nullptr);
    }
    spv_path(vpath, sizeof vpath, "fullscreen.vert");
    spv_path(fpath, sizeof fpath, "sky.frag");
    if (!skyPipe_.create(dev, mainPass, vpath, fpath, sizeof(SkyPush),
                         skySetLayout_)) {
        std::fprintf(stderr, "[Renderer3DVk] sky pipeline FAILED\n");
    }

    // A2b: Precipitation overlay (fullscreen.vert + precip.frag) — the
    // atmosphere submodule's second pass: drawn LAST in the main pass (the
    // weather falls between the camera and the world), depth off, alpha
    // over. Skipped entirely on a dry day, so it is provably inert then.
    spv_path(vpath, sizeof vpath, "fullscreen.vert");
    spv_path(fpath, sizeof fpath, "precip.frag");
    if (!precipPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                 sizeof(PrecipPush), 0, nullptr, 0,
                                 /*instanced=*/false, /*depthTest=*/false,
                                 /*depthWrite=*/false, /*blend=*/true,
                                 /*cullBack=*/false)) {
        std::fprintf(stderr, "[Renderer3DVk] precip pipeline FAILED\n");
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

    // A4: Tree billboard pipeline (the universal billboard.vert + tree.frag,
    // instanced). All three billboard passes share the vertex stage and the
    // gpu/bb_instance.h attribute table — only the fragment stage is per-kind.
    spv_path(vpath, sizeof vpath, "billboard.vert");
    spv_path(fpath, sizeof fpath, "tree.frag");
    {
        if (!treePipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(gpu::BbInstance),
                                   gpu::kBbInstanceAttrs,
                                   gpu::kBbInstanceAttrCount,
                                   /*instanced=*/true,
                                   /*depthTest=*/true, /*depthWrite=*/true,
                                   /*blend=*/false, /*cullBack=*/false,
                                   shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] tree pipeline FAILED\n");
        }
    }

    // A5: Structure pipelines. Boxes (struct.vert) and round prisms
    // (struct_cyl.vert — towers, gate jambs, the spire) share the instance
    // layout and the struct.frag material; the shape picks the buffer + draw.
    {
        VkVertexInputAttributeDescription sAttrs[5]{};
        for (std::uint32_t i = 0; i < 5; ++i) {
            sAttrs[i].location = i; sAttrs[i].binding = 0;
        }
        sAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[0].offset = 0;
        sAttrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[1].offset = sizeof(float) * 3;
        sAttrs[2].format = VK_FORMAT_R32_SFLOAT;       sAttrs[2].offset = sizeof(float) * 6;
        sAttrs[3].format = VK_FORMAT_R32_SFLOAT;       sAttrs[3].offset = sizeof(float) * 7;
        sAttrs[4].format = VK_FORMAT_R32_SFLOAT;       sAttrs[4].offset = sizeof(float) * 8;
        spv_path(vpath, sizeof vpath, "struct.vert");
        spv_path(fpath, sizeof fpath, "struct.frag");
        if (!structPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                     sizeof(MeshPush), sizeof(StructInstance),
                                     sAttrs, 5, /*instanced=*/true,
                                     /*depthTest=*/true, /*depthWrite=*/true,
                                     /*blend=*/false, /*cullBack=*/false,
                                     shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] struct pipeline FAILED\n");
        }
        spv_path(vpath, sizeof vpath, "struct_cyl.vert");
        if (!cylPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                  sizeof(MeshPush), sizeof(StructInstance),
                                  sAttrs, 5, /*instanced=*/true,
                                  /*depthTest=*/true, /*depthWrite=*/true,
                                  /*blend=*/false, /*cullBack=*/false,
                                  shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] cylinder pipeline FAILED\n");
        }
    }

    // A7: NPC billboard pipeline (billboard.vert + npc.frag, instanced). Set 1
    // is the paper-doll sprite pool (one sampler2DArray of composited frames).
    spv_path(vpath, sizeof vpath, "billboard.vert");
    spv_path(fpath, sizeof fpath, "npc.frag");
    {
        VkDescriptorSetLayout npcSets[2] = {
            shadowSetLayout_, paperdoll_.set_layout()
        };
        if (!npcPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(gpu::BbInstance),
                                   gpu::kBbInstanceAttrs,
                                   gpu::kBbInstanceAttrCount,
                                   /*instanced=*/true,
                                   /*depthTest=*/true, /*depthWrite=*/true,
                                   /*blend=*/true, /*cullBack=*/false,
                                   npcSets, 2)) {
            std::fprintf(stderr, "[Renderer3DVk] npc pipeline FAILED\n");
        }
    }

    // A8: Creature billboard pipeline (billboard.vert + creature.frag,
    // instanced). Shares the shadow-sampler set (0) with trees. Alpha-tested
    // via discard, so blend=false + depthWrite=true keep the silhouette crisp
    // and depth-correct.
    spv_path(fpath, sizeof fpath, "creature.frag");
    {
        if (!creaturePipe_.create_mesh(dev, mainPass, vpath, fpath,
                                       sizeof(BbPush), sizeof(gpu::BbInstance),
                                       gpu::kBbInstanceAttrs,
                                       gpu::kBbInstanceAttrCount,
                                       /*instanced=*/true,
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

    // Billboard shadow casters share the universal shadow_bb.vert + the ONE
    // gpu/bb_instance.h attribute table; only the depth fragment is per-kind.
    spv_path(vpath, sizeof vpath, "shadow_bb.vert");
    spv_path(fpath, sizeof fpath, "shadow_tree.frag");
    {
        if (!shadowTreePipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                            sizeof(ShadowBbPush), sizeof(gpu::BbInstance),
                                            gpu::kBbInstanceAttrs,
                                            gpu::kBbInstanceAttrCount,
                                            /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow tree pipeline FAILED\n");
        }
    }

    spv_path(vpath, sizeof vpath, "shadow_struct.vert");
    spv_path(fpath, sizeof fpath, "shadow_struct.frag");
    {
        VkVertexInputAttributeDescription sAttrs[5]{};
        for (std::uint32_t i = 0; i < 5; ++i) {
            sAttrs[i].location = i; sAttrs[i].binding = 0;
        }
        sAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[0].offset = 0;
        sAttrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; sAttrs[1].offset = sizeof(float) * 3;
        sAttrs[2].format = VK_FORMAT_R32_SFLOAT;       sAttrs[2].offset = sizeof(float) * 6;
        sAttrs[3].format = VK_FORMAT_R32_SFLOAT;       sAttrs[3].offset = sizeof(float) * 7;
        sAttrs[4].format = VK_FORMAT_R32_SFLOAT;       sAttrs[4].offset = sizeof(float) * 8;
        if (!shadowStructPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                              sizeof(ShadowPush), sizeof(StructInstance),
                                              sAttrs, 5, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow struct pipeline FAILED\n");
        }
        spv_path(vpath, sizeof vpath, "shadow_cyl.vert");
        if (!shadowCylPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                          sizeof(ShadowPush), sizeof(StructInstance),
                                          sAttrs, 5, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow cylinder pipeline FAILED\n");
        }
    }

    // A9: NPC shadow caster (universal shadow_bb.vert + shadow_npc.frag).
    // Set 0 is the same sprite pool: the silhouette is the composited alpha.
    spv_path(vpath, sizeof vpath, "shadow_bb.vert");
    spv_path(fpath, sizeof fpath, "shadow_npc.frag");
    {
        if (!shadowNpcPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                            sizeof(ShadowBbPush), sizeof(gpu::BbInstance),
                                            gpu::kBbInstanceAttrs,
                                            gpu::kBbInstanceAttrCount,
                                            /*instanced=*/true,
                                            paperdoll_.set_layout())) {
            std::fprintf(stderr, "[Renderer3DVk] shadow npc pipeline FAILED\n");
        }
    }

    // A8: Creature shadow caster (universal shadow_bb.vert +
    // shadow_creature.frag): the frag discards the same silhouette as the lit
    // pass, from the same instance extents, so the cast shadow cannot drift.
    spv_path(fpath, sizeof fpath, "shadow_creature.frag");
    {
        if (!shadowCreaturePipe_.create_shadow(
                dev, shadow_.renderPass, vpath, fpath,
                sizeof(ShadowBbPush), sizeof(gpu::BbInstance),
                gpu::kBbInstanceAttrs, gpu::kBbInstanceAttrCount,
                /*instanced=*/true)) {
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
    precipPipe_.destroy(dev);
    skyStarsBuf_.destroy(dev);
    if (skyPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev.device, skyPool_, nullptr);
        skyPool_ = VK_NULL_HANDLE;
        skySet_ = VK_NULL_HANDLE;
    }
    if (skySetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev.device, skySetLayout_, nullptr);
        skySetLayout_ = VK_NULL_HANDLE;
    }
    waterPipe_.destroy(dev);
    treePipe_.destroy(dev);
    treeInstBuf_.destroy(dev);
    treeInstCap_ = 0;
    treeCount_ = 0;
    structPipe_.destroy(dev);
    structInstBuf_.destroy(dev);
    structInstCap_ = 0;
    structCount_ = 0;
    cylPipe_.destroy(dev);
    cylInstBuf_.destroy(dev);
    cylInstCap_ = 0;
    cylCount_ = 0;
    npcPipe_.destroy(dev);
    npcInstBuf_.destroy(dev);
    npcCount_ = 0;
    shadowMeshPipe_.destroy(dev);
    shadowTreePipe_.destroy(dev);
    shadowStructPipe_.destroy(dev);
    shadowCylPipe_.destroy(dev);
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
    heightTex_.destroy(dev);
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
        std::vector<gpu::BbInstance> npcs;
        npcs.reserve(kMaxEntityInstances);
        const float tMs = elapsed * 1000.0f;
        // Advance the sprite-pool LRU clock; every layer_for below may record
        // a layer upload on `cmd`, which is legal only before the render pass.
        paperdoll_.begin_frame();
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
            // Per-body animation phase, rolled from the face it already wears.
            // Every walker used to animate off the SAME global clock, so a
            // whole town advanced its frame key on the SAME tick — a burst of
            // hundreds of pool misses landing in one frame, which is what
            // starved the staging slice and blinked the overflow bodies out
            // (2026-08-10). The phase spreads those misses into a trickle,
            // and a marching crowd stops walking in lockstep for free.
            const float phaseMs = float(ch.visualSeed & 0x7FFu);
            const character::AnimationState anim =
                character::make_animation_state(
                    moving ? character::AnimationType::Walk
                           : character::AnimationType::Idle,
                    direction_from_velocity(vx, vy), tMs + phaseMs);

            // Quantize the visual seed into the 256-seed pool for visual diversity
            const std::uint32_t quantizedSeed = (ch.visualSeed % 256) + 1;

            const auto& desc = paperdoll_.descriptor_for_seed(quantizedSeed);
            // Resolve the composited frame to its pool layer (composes +
            // records the upload on a miss). If the pool cannot take THIS
            // frame right now (staging slice full), fall back to the body's
            // canonical frame (Idle/Front/0 — resident after its first use):
            // a starved body shows a one-frame-stale pose instead of
            // VANISHING for a frame. A body may only disappear if even the
            // canonical frame cannot be served, which a 256-descriptor pool
            // cannot make happen in practice.
            std::uint32_t layer = paperdoll_.layer_for(cmd, desc, anim);
            if (layer == character::PaperdollAtlas::kNoLayer) {
                layer = paperdoll_.layer_for(cmd, desc,
                                             character::AnimationState{});
            }
            if (layer == character::PaperdollAtlas::kNoLayer) continue;

            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            gpu::BbInstance inst{};
            inst.px = wx;
            inst.py = pos.z;
            inst.pz = wz;
            // How tall this body actually is (ecs::Sprite.height, filled at
            // birth from its table row × its own shape). This used to be the
            // literal 2.0f for every humanoid alive — a lord, a child-sized
            // peasant and a possessed goblin all exactly two metres — while the
            // physics beside it read a real width out of the table. A body with
            // no sprite record (a bare face in a fixture) keeps the old
            // constant, which is the only place it survives.
            const auto* spr = ecs->reg.try_get<ecs::Sprite>(e);
            const float size = (spr && spr->height > 0.0f) ? spr->height : 2.0f;
            inst.halfW = size * 0.5f; // the doll quad is square
            inst.height = size;
            inst.kind = layer;
            inst.seed = 0u;
            inst.tint = 0xFFFFFFFFu;
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
        std::vector<gpu::BbInstance> creatures;
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
            gpu::BbInstance ci{};
            ci.px = wx; ci.py = pos.z; ci.pz = wz;
            // Height from the row when it states one; otherwise the very
            // proportion this line used to hardcode, so a creature that has not
            // opted in is drawn exactly as before (sub/body.h).
            const float size = spr.height > 0.0f ? spr.height : spr.scale * 1.5f;
            // Body-plan aspect applied HERE, once (macro/fauna.h) — it used to
            // live as twin GLSL tables in the lit and shadow vertex stages.
            // (size·w)·0.5 stays bit-exact with the shader's old size·w: a
            // halving and the vert's ·2.0 are exact fp32 inverses.
            const auto asp = creature_arch_aspect(spr.archetype);
            ci.halfW = size * asp.w * 0.5f;
            ci.height = size * asp.h;
            ci.kind = spr.archetype;
            // Use entity id for stable per-instance variation — iteration
            // order in EnTT views is not guaranteed stable across frames.
            const std::uint32_t eid = entt::to_integral(e);
            ci.seed = gpu::bb_seed_bits(float(spr.atlasId) * 2.17f
                                        + float(eid & 63u) * 0.5f);
            ci.tint = gpu::bb_pack_tint(spr.r, spr.g, spr.b);
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

namespace {

// Upload an instance set into a REUSED device-local buffer.
//
// The set changes on every seam crossing and every async cell drain, and the
// old code destroyed and re-created the buffer each time. Measured, that
// allocate-bind-stage-submit-wait was ~90% of the whole rebuild: the CPU loop
// that builds ten thousand tree instances costs 0.08 ms, the buffer churn
// around it cost 0.7-1.2 ms. Keeping the allocation and overwriting it in place
// is the entire difference.
//
// Grows by half again plus a floor, so a world that gains a few trees per
// crossing stops reallocating almost immediately. Spare capacity past `count`
// is never read — the draw uses the count, not the buffer size.
template <typename Inst>
bool upload_instances(const gpu::VulkanDevice& dev, gpu::VulkanBuffer& buf,
                      std::size_t& capacity, const std::vector<Inst>& src,
                      const char* what) {
    if (src.empty()) return true;
    if (src.size() > capacity) {
        buf.destroy(dev);
        capacity = src.size() + src.size() / 2 + 64;
        std::vector<Inst> padded(capacity);
        std::copy(src.begin(), src.end(), padded.begin());
        if (!buf.create_device_local(dev, padded.data(),
                                     padded.size() * sizeof(Inst),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
            std::fprintf(stderr, "[Renderer3DVk] %s buffer FAILED\n", what);
            capacity = 0;
            return false;
        }
        return true;
    }
    if (!buf.update(dev, src.data(), src.size() * sizeof(Inst))) {
        std::fprintf(stderr, "[Renderer3DVk] %s buffer update FAILED\n", what);
        return false;
    }
    return true;
}

} // namespace

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
                // A freshly exposed cell is a placeholder: ONE height across all
                // 1024² of its tiles. sampleVertex would box-average 289 copies
                // of that number per vertex, ~4.2k vertices per cell — three
                // fresh cells on an axis crossing came to 3.7 million scattered
                // reads of a 37 MB array, which is where the crossing frame's
                // milliseconds went. The mean of a constant is the constant.
                //
                // Only the cell's four shared EDGES need honest sampling: a
                // vertex's ±half footprint reaches into the neighbour there,
                // and half < step means it reaches no further than the first
                // interior vertex. Everything strictly inside is the constant.
                float flatH = 0.0f;
                if (mgr.cell_flat_height(idx, flatH)) {
                    const float hM = flatH * kHeightScaleM;
                    for (int y = vy0 + 1; y <= vy1 - 1; ++y) {
                        float* row = &heightVtxM_[std::size_t(y) * Nv];
                        for (int x = vx0 + 1; x <= vx1 - 1; ++x) row[x] = hM;
                    }
                    for (int x = vx0; x <= vx1; ++x) {
                        heightVtxM_[std::size_t(vy0) * Nv + x] = sampleVertex(x, vy0);
                        heightVtxM_[std::size_t(vy1) * Nv + x] = sampleVertex(x, vy1);
                    }
                    for (int y = vy0; y <= vy1; ++y) {
                        heightVtxM_[std::size_t(y) * Nv + vx0] = sampleVertex(vx0, y);
                        heightVtxM_[std::size_t(y) * Nv + vx1] = sampleVertex(vx1, y);
                    }
                    continue;
                }
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
        // dirty-cell resample) — refresh the window min/max (flight ceiling
        // reads the max, the shadow volume's vertical fit reads both). One
        // pass over ~37k floats, negligible next to the resample.
        maxHeightM_ = 0.0f;
        minHeightM_ = 1.0e30f;
        for (const float h : heightVtxM_) {
            if (h > maxHeightM_) maxHeightM_ = h;
            if (h < minHeightM_) minHeightM_ = h;
        }
        if (minHeightM_ > maxHeightM_) minHeightM_ = 0.0f;
        // Push the fresh heightfield to the GPU copy the terrain-occlusion
        // march samples (lighting.glsl u_heightM). Same blocking-update
        // contract as materialTex_ right below in this function.
        if (heightTex_.image != VK_NULL_HANDLE) {
            heightTex_.update_region(
                dev, 0, 0, heightTex_.width, heightTex_.height,
                reinterpret_cast<const std::uint8_t*>(heightVtxM_.data()));
        }
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
            // Ploughed fields furrow the way the MAP paints this cell:
            // orientation was resolved from the wrapped macro coords at
            // resolve time (field_furrows_vertical) and travels with the
            // cell — here it just picks which of the two field materials
            // the whole cell's TILE_FIELD bytes map to.
            lut[TILE_FIELD] = static_cast<std::uint8_t>(
                mgr.cell_field_furrows_vertical(idx) ? TM_FieldV : TM_Field);
            const long long absX0 =
                (long long)(mgr.center_cx() - 1 + ox) * kCellSize;
            const long long absY0 =
                (long long)(mgr.center_cy() - 1 + oy) * kCellSize;
            bool uniform = true;
            for (int i = 0; i < 9; ++i) uniform &= ring[i] == ring[4];
            const bool ringUniform = uniform;
            // A placeholder cell is ONE height and ONE tile id across all of
            // its 1024² (place_placeholder). Everything below then has a
            // constant answer: the peak scan is a single comparison, and the
            // fill is a memset instead of a million LUT lookups. Same law as
            // the height path — a constant does not need to be recovered from
            // a million copies of itself.
            float flatH = 0.0f;
            std::uint8_t flatTile = 0;
            const bool cellIsFlat = mgr.cell_flat_height(idx, flatH)
                                 && mgr.cell_flat_tile(idx, flatTile);
            // A flat cell's material varies with the tile coordinate in ONE
            // place: the treeline dither band, where stone gains ground by a
            // coordinate hash. Below the band and above it the answer is the
            // same everywhere. Inside it there are exactly TWO answers, chosen
            // per tile by that hash — still nothing like the general case,
            // because a uniform ring pins the biome pick to ring[4] and the
            // material lookup to two precomputable bytes.
            const bool flatInDitherBand =
                cellIsFlat && ring[4] != Biome::Water
                && !material_is_authored(flatTile)
                && flatH > kMtnGrassTopH && flatH < kMtnRockBaseH;
            // Both fast paths below rest on claims the whole-image self-check
            // cannot test, because it recomputes through THIS function and would
            // simply agree with itself. So verify them directly: every tile of a
            // flat cell really is flatTile, and every byte written really is
            // what the honest per-tile expression produces.
            auto verifyFlatCell = [&](const std::uint8_t* out) {
                std::size_t badTile = 0, badVal = 0;
                for (int y = 0; y < kCellSize; ++y) {
                    const std::size_t row =
                        std::size_t(oy * kCellSize + y) * kFullSize
                        + std::size_t(ox * kCellSize);
                    for (int x = 0; x < kCellSize; ++x)
                        if (tiles[row + std::size_t(x)] != flatTile) ++badTile;
                }
                for (int y = 0; y < kCellSize; y += 37) {
                    for (int x = 0; x < kCellSize; x += 31) {
                        std::uint8_t want;
                        if (material_is_authored(flatTile)) {
                            want = lut[flatTile];
                        } else {
                            Biome b = pick_ground_biome_axis(
                                ring, groundAxis[std::size_t(x)],
                                groundAxis[std::size_t(y)],
                                absX0 + x, absY0 + y);
                            b = apply_mountain_treeline(b, flatH,
                                                        absX0 + x, absY0 + y);
                            want = b == ring[4]
                                ? lut[flatTile]
                                : static_cast<std::uint8_t>(
                                      terrain_material_for(flatTile, b));
                        }
                        if (out[std::size_t(dstY0 + y) * dstStride
                                + std::size_t(dstX0 + x)] != want) ++badVal;
                    }
                }
                std::fprintf(stderr,
                    "[seam-selfcheck] flat cell %d tile=%u h=%.3f band=%d "
                    "tileMismatch=%zu/%d valMismatch=%zu\n",
                    idx, unsigned(flatTile), double(flatH),
                    flatInDitherBand ? 1 : 0, badTile, kCellSize * kCellSize,
                    badVal);
                std::fflush(stderr);
            };

            if (uniform && cellIsFlat) {
                uniform = !flatInDitherBand;
            } else if (uniform) {
                // Real terrain: the uniform LUT shortcut also needs the cell to
                // sit ENTIRELY below the stone band, because ground above it
                // turns to rock by ALTITUDE regardless of biome and the LUT
                // cannot express that. Strided peak scan is ~16k reads.
                for (int y = 0; y < kCellSize && uniform; y += 8) {
                    const std::size_t row =
                        std::size_t(oy * kCellSize + y) * kFullSize
                        + std::size_t(ox * kCellSize);
                    for (int x = 0; x < kCellSize; x += 8) {
                        if (hm[row + std::size_t(x)] >= kMtnGrassTopH) {
                            uniform = false;
                            break;
                        }
                    }
                }
            }
            if (uniform && cellIsFlat) {
                // The constant this whole cell resolves to — the slow path's
                // own expression, evaluated once. A uniform ring makes
                // pick_ground_biome_axis independent of the coordinate, and
                // outside the dither band so is the treeline; those two are the
                // only things in it that could have varied.
                std::uint8_t v;
                if (material_is_authored(flatTile)) {
                    v = lut[flatTile];
                } else {
                    const Biome b = apply_mountain_treeline(ring[4], flatH, 0, 0);
                    v = b == ring[4]
                        ? lut[flatTile]
                        : static_cast<std::uint8_t>(
                              terrain_material_for(flatTile, b));
                }
                for (int y = 0; y < kCellSize; ++y)
                    std::memset(dst + std::size_t(dstY0 + y) * dstStride + dstX0,
                                v, std::size_t(kCellSize));
                if (kSelfCheck) verifyFlatCell(dst);
                return;
            }
            if (cellIsFlat && ringUniform && flatInDitherBand) {
                // One height, one tile, one biome — so the only thing left that
                // can differ between two tiles of this cell is which side of the
                // treeline dither they land on. Two bytes, precomputed; the
                // dither itself stays where it belongs (apply_mountain_treeline
                // owns that formula, we do not copy it).
                auto matFor = [&](Biome b) -> std::uint8_t {
                    return b == ring[4]
                        ? lut[flatTile]
                        : static_cast<std::uint8_t>(
                              terrain_material_for(flatTile, b));
                };
                const std::uint8_t vRock = matFor(Biome::Mountain);
                const std::uint8_t vBelow = matFor(
                    ring[4] == Biome::Mountain ? Biome::Meadow : ring[4]);
                const float bandT = treeline_t(flatH);
                for (int y = 0; y < kCellSize; ++y) {
                    std::uint8_t* d =
                        dst + std::size_t(dstY0 + y) * dstStride + dstX0;
                    for (int x = 0; x < kCellSize; ++x) {
                        d[x] = treeline_is_rock(bandT, absX0 + x, absY0 + y)
                                   ? vRock : vBelow;
                    }
                }
                if (kSelfCheck) verifyFlatCell(dst);
                return;
            }
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
                        Biome b = pick_ground_biome_axis(
                            ring, groundAxis[std::size_t(x)], ay,
                            absX0 + x, absY0 + y);
                        b = apply_mountain_treeline(
                            b, hm[srcRow + std::size_t(x)],
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
            std::vector<gpu::BbInstance> trees;
            trees.reserve(structs.size());
            for (const auto& s : structs) {
                // Crops ride the tree billboard pass — same quad, same
                // shadow caster, their own sprite row picked by KIND.
                const bool isCrop = s.kind == Structure::Crop;
                if (s.kind != Structure::Tree && !isCrop) continue;
                float wx, wz;
                tile_to_world(s.x, s.y, wx, wz);
                const float baseM = sample_height_m(s.x, s.y);
                if (baseM < kSeaLevelM - 0.5f) continue;
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
                const int typeIdx = isCrop
                    ? kCropSpriteRow
                    : tree_type_for_temperature(temp, hash01);
                // Metric build — height (metres) from the record's own roll
                // scaled by the species, width and seat sink derived from it
                // by the ONE law in sub/tree_atlas.h. The base sits on the
                // same surface a body's feet do (sample_height_m), sunk by
                // less than one sprite row so the trunk stays in daylight.
                const TreeBillboard tb =
                    tree_billboard(s.height, s.radius, typeIdx);
                trees.push_back({wx, baseM - tb.sinkM, wz,
                                 tb.halfWidthM, tb.heightM,
                                 std::uint32_t(typeIdx),
                                 gpu::bb_seed_bits(float(h & 0xffffu) * 0.01f
                                                   + hash01 * 5.0f),
                                 0xFFFFFFFFu});
            }
            treeCount_ = static_cast<std::uint32_t>(trees.size());
            if (!upload_instances(dev, treeInstBuf_, treeInstCap_, trees, "tree"))
                treeCount_ = 0;
        }
        if (kProf) msTree = profMs(st, profNow());

        // A5: structure instances (houses / walls / towers). Geometry comes
        // from the SHARED map_data.h helpers — the same oriented footprint and
        // vertical span the collision index (sub/collide.h) uses, so the
        // visible silhouette is exactly the solid one. Shape splits the set:
        // boxes → struct.vert, cylinders → struct_cyl.vert.
        const auto ss = profNow();
        {
            const auto& structs = mgr.structures();
            std::vector<StructInstance> boxes;
            std::vector<StructInstance> cyls;
            boxes.reserve(structs.size());
            for (const auto& s : structs) {
                if (s.kind != Structure::House && s.kind != Structure::Wall
                    && s.kind != Structure::Fence) continue;
                const float baseM = sample_height_m(s.x, s.y);
                if (baseM < kSeaLevelM - 0.5f) continue;
                float wx, wz;
                tile_to_world(s.x, s.y, wx, wz);
                const float minR = structure_min_half_xy(s.kind);
                const float halfX = std::max(minR, structure_half_x(s));
                const float halfZ = std::max(minR, structure_half_y(s));
                const float height = structure_visible_height(s);
                // Grounded bodies keep the legacy seat (centre AT ground,
                // half-height = full height, lower half buried — hides the
                // downhill gap on slopes). A zBase-lifted body (gate lintel,
                // deck) floats honestly: bottom at seat + zBase.
                float py, halfY;
                if (s.zBase > 0.0f) {
                    halfY = height * 0.5f;
                    py = baseM + s.zBase + halfY;
                } else {
                    halfY = height;
                    py = baseM - 0.05f;
                }
                // Per-instance shade, keyed to ABSOLUTE tile coords.
                //
                // It used to be keyed to the window-relative s.x/s.y, and that
                // was a seam defect, not a style: a crossing reindexes the
                // composite, every structure's coordinate moves by a whole
                // cell, the hash changes and EVERY BUILDING IN VIEW jumps to a
                // new shade at the moment the player steps over the boundary.
                // Trees had the same bug keyed the same way and were fixed when
                // their species snapped at seams; structures were missed.
                //
                // The rule itself lives in sub/material.h next to the ground
                // dither it shares a hash with, so it can be tested; here we
                // only supply the absolute position.
                const double absX =
                    double((mgr.center_cx() - 1) * kCellSize) + double(s.x);
                const double absY =
                    double((mgr.center_cy() - 1) * kCellSize) + double(s.y);
                const float shade = structure_shade(absX, absY);
                auto& list = (s.shape == Structure::Cylinder) ? cyls : boxes;
                list.push_back({wx, py, wz,
                                halfX, halfY, halfZ,
                                s.kind == Structure::House ? 1.0f : 0.0f,
                                shade, s.yaw});
            }
            structCount_ = static_cast<std::uint32_t>(boxes.size());
            if (!upload_instances(dev, structInstBuf_, structInstCap_, boxes,
                                  "struct"))
                structCount_ = 0;
            cylCount_ = static_cast<std::uint32_t>(cyls.size());
            if (!upload_instances(dev, cylInstBuf_, cylInstCap_, cyls,
                                  "cylinder"))
                cylCount_ = 0;
        }
        if (kProf) msStruct = profMs(ss, profNow());
    }

    if (kProf) {
        std::fprintf(stderr,
            "[upload3d-prof] shift=%d,%d fullH=%d cells=%d "
            "height=%.3f verts=%.3f terrainBuf=%.3f "
            "matFill=%.3f matGpu=%.3f tree=%.3f struct=%.3f TOTAL=%.3f (ms)\n",
            dirty.shiftX, dirty.shiftY, doFullHeight ? 1 : 0,
            (dirty.heightCells[0]?1:0)+(dirty.heightCells[1]?1:0)+(dirty.heightCells[2]?1:0)
            +(dirty.heightCells[3]?1:0)+(dirty.heightCells[4]?1:0)+(dirty.heightCells[5]?1:0)
            +(dirty.heightCells[6]?1:0)+(dirty.heightCells[7]?1:0)+(dirty.heightCells[8]?1:0),
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
    const float span = compute_shadow_basis(cam, time, shadow_.size,
                                            minHeightM_, maxHeightM_,
                                            lightMvp_, lightRight);
    // TIMAERT_SHADOW_STATS=1: one stderr line per ~2 s — the map's REAL texel
    // density in this scene right now, so density claims are measured, never
    // reasoned about.
    static const bool statsOn = std::getenv("TIMAERT_SHADOW_STATS") != nullptr;
    if (statsOn) {
        static std::uint32_t frames = 0;
        if (++frames % 120u == 0u) {
            std::fprintf(stderr,
                         "[shadow] span=%.0fm texel=%.2fm heights=[%.0f..%.0f]m\n",
                         span, span / float(shadow_.size),
                         minHeightM_, maxHeightM_);
        }
    }
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

    // Structures (instanced boxes + cylinders).
    if (structCount_ > 0 || cylCount_ > 0) {
        ShadowPush ssp{};
        std::memcpy(ssp.lightMvp, lightMvp_.m, sizeof(ssp.lightMvp));
        if (structCount_ > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              shadowStructPipe_.pipeline);
            VkDeviceSize sso = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &structInstBuf_.buffer, &sso);
            vkCmdPushConstants(cmd, shadowStructPipe_.layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ssp), &ssp);
            vkCmdDraw(cmd, 36, structCount_, 0, 0);
        }
        if (cylCount_ > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              shadowCylPipe_.pipeline);
            VkDeviceSize sso = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &cylInstBuf_.buffer, &sso);
            vkCmdPushConstants(cmd, shadowCylPipe_.layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ssp), &ssp);
            vkCmdDraw(cmd, kCylVertexCount, cylCount_, 0, 0);
        }
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
    // The sky context is built ONCE here and feeds both halves of the cloud
    // system: the sky pass (SkyPush below) and the cloud-shadow lane of the
    // light SSBO — one wind, one cloudiness, by construction.
    const sub::SkyContext skyCtx = sub::build_sky_context(time);
    const float skyParams[4] = { elapsed, skyCtx.windX, skyCtx.windZ,
                                 skyCtx.cloudiness01 };
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

    // ── Lighting ── (skyCtx was built above)
    SunInfo sun = compute_sun(time);
    const float tod = skyCtx.tod;

    // The light SSBO write needs the frame's celestial direction (the
    // terrain-occlusion march follows it), so the gather runs here, after
    // compute_sun and before any draw. camPos (world metres) is the cull
    // origin — see gather_point_lights: when more than kSubworldMaxLights
    // emitters are live, the nearest to the camera survive, so the player's
    // own light (riding the camera) is never dropped.
    gather_point_lights(ecs, slot, cam.pos, skyParams, sun.sunDir);

    // Lightning: a pure function of the render clock (sub/sky.h). The flash
    // is ADDED TO AMBIENT — the one channel every lit pass already receives —
    // so the whole world blinks cool-white for the instant with zero new
    // plumbing; the sky dome and the falling rain get the same value through
    // their own push lanes below.
    const float stormFlash = sub::storm_flash01(elapsed, skyCtx.storm01);
    if (stormFlash > 0.0f) {
        sun.ambientColor.x += 0.55f * stormFlash;
        sun.ambientColor.y += 0.60f * stormFlash;
        sun.ambientColor.z += 0.75f * stormFlash;
    }

    mat4 lightMvp = lightMvp_;

    // ── A2: Sky (fullscreen, behind everything, drawn first) ──
    // The push block is SkyContext + the camera basis, copied verbatim — the
    // renderer adds no sky knowledge of its own.
    {
        SkyPush sky{};
        sky.forward[0] = fwd.x;  sky.forward[1] = fwd.y;  sky.forward[2] = fwd.z;
        sky.right[0]   = rgt.x;  sky.right[1]   = rgt.y;  sky.right[2]   = rgt.z;
        sky.up[0]      = upv.x;  sky.up[1]      = upv.y;  sky.up[2]      = upv.z;
        sky.forward[3] = static_cast<float>(skyCtx.moonCount);
        sky.right[3]   = skyCtx.starSizeScale;
        sky.p0[0] = static_cast<float>(ext.width);
        sky.p0[1] = static_cast<float>(ext.height);
        sky.p0[2] = fovRad;
        sky.p0[3] = tod;
        sky.p1[0] = sun.ambientColor.x; // fog = ambient for now
        sky.p1[1] = sun.ambientColor.y;
        sky.p1[2] = sun.ambientColor.z;
        sky.p1[3] = elapsed;
        // The sun the sky DRAWS is the sun the world is LIT by — celestial.h's
        // arc, not a second in-shader copy of the formula.
        sky.sun[0] = skyCtx.sunDir[0];
        sky.sun[1] = skyCtx.sunDir[1];
        sky.sun[2] = skyCtx.sunDir[2];
        for (int i = 0; i < skyCtx.moonCount; ++i) {
            const sub::SkyMoonCtx& m = skyCtx.moons[i];
            sky.moonDirSize[i][0] = m.dir[0];
            sky.moonDirSize[i][1] = m.dir[1];
            sky.moonDirSize[i][2] = m.dir[2];
            sky.moonDirSize[i][3] = m.size;
            sky.moonColIllum[i][0] = m.rgb[0];
            sky.moonColIllum[i][1] = m.rgb[1];
            sky.moonColIllum[i][2] = m.rgb[2];
            sky.moonColIllum[i][3] = m.illum;
        }
        sky.p2[0] = skyCtx.cloudiness01;
        sky.p2[1] = skyCtx.windX;
        sky.p2[2] = skyCtx.windZ;
        sky.p2[3] = skyCtx.precip01;
        sky.p3[0] = skyCtx.seasonTint[0];
        sky.p3[1] = skyCtx.seasonTint[1];
        sky.p3[2] = skyCtx.seasonTint[2];
        sky.p3[3] = stormFlash;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          skyPipe_.pipeline);
        if (skySet_ != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    skyPipe_.layout, 0, 1, &skySet_,
                                    0, nullptr);
        }
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

    // ── A5: Structures (instanced boxes + cylinders, after trees, before
    // water). Same push/material for both shapes. ──
    if (structCount_ > 0 || cylCount_ > 0) {
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
        if (structCount_ > 0) {
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
        if (cylCount_ > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              cylPipe_.pipeline);
            if (litSet != VK_NULL_HANDLE)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        cylPipe_.layout, 0, 1, &litSet,
                                        0, nullptr);
            VkDeviceSize sio = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &cylInstBuf_.buffer, &sio);
            vkCmdPushConstants(cmd, cylPipe_.layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(sp), &sp);
            vkCmdDraw(cmd, kCylVertexCount, cylCount_, 0, 0);
        }
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

    // ── A2b: Precipitation (fullscreen overlay, LAST — the weather falls
    // between the camera and everything above). Skipped on a dry day: the
    // frame is then byte-identical to a build without this pass at all —
    // the atmosphere submodule's isolation, provable by capture diff.
    if (skyCtx.precip01 > 0.001f) {
        PrecipPush pp{};
        pp.p0[0] = static_cast<float>(ext.width);
        pp.p0[1] = static_cast<float>(ext.height);
        pp.p0[2] = elapsed;
        pp.p0[3] = skyCtx.precip01;
        pp.p1[0] = static_cast<float>(skyCtx.precipKind);
        // Streak tilt: the cloud wind projected onto the camera's right axis
        // — the same wind that drifts the clouds drives the rain sideways.
        pp.p1[1] = (rgt.x * skyCtx.windX + rgt.z * skyCtx.windZ) * 30.0f;
        pp.p1[2] = stormFlash;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          precipPipe_.pipeline);
        vkCmdPushConstants(cmd, precipPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
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
                                       const sm::vec3& camPos,
                                       const float (&skyParams)[4],
                                       const sm::vec3& sunDir) {
    if (slot >= kFramesInFlight || lightBuf_[slot].mapped == nullptr) return;
    auto* buf = static_cast<GpuLightBuffer*>(lightBuf_[slot].mapped);
    buf->skyParams[0] = skyParams[0];
    buf->skyParams[1] = skyParams[1];
    buf->skyParams[2] = skyParams[2];
    buf->skyParams[3] = skyParams[3];
    // Terrain-occlusion context: the celestial direction the march follows
    // (sun by day, the dominant moon by night — mountains occlude moonlight
    // through the very same lane) and the window's world span for the
    // world→heightfield-UV mapping in lighting.glsl.
    buf->sunDirW[0] = sunDir.x;
    buf->sunDirW[1] = sunDir.y;
    buf->sunDirW[2] = sunDir.z;
    buf->sunDirW[3] = 0.0f;
    buf->terrainParams[0] = float(kFullSize) * kTileMeters;
    buf->terrainParams[1] = 0.0f;
    buf->terrainParams[2] = 0.0f;
    buf->terrainParams[3] = 0.0f;
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
