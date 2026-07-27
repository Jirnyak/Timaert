#include "sub/vk_renderer_3d.h"
#include "sub/vk_camera_math.h"
#include "sub/base_generator.h"
#include "sub/camera.h"
#include "sub/lighting.h"
#include "sub/map_data.h"
#include "sub/seamless_manager.h"
#include "sub/tree_atlas.h"
#include "gpu/vk_device.h"
#include "macro/state.h"
#include "ecs/components.h"
#include "ecs/world.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sm::sub {

namespace {

// Physical scale — must match the GL Renderer3D exactly.
constexpr float kTileMeters  = 1.0f;
constexpr float kWorldExtent = float(kFullSize) * kTileMeters * 0.5f; // 1536 m
constexpr float kHeightScale = 1500.0f;

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

enum TerrainMaterial : std::uint8_t {
    TM_Tundra = 0, TM_Taiga, TM_Snow, TM_Valley, TM_Meadow,
    TM_Swamp, TM_Desert, TM_Steppe, TM_Tropics,
    TM_Field, TM_Shore, TM_Rock, TM_Road, TM_Water,
};

float terrain_material_for(std::uint8_t tile, Biome biome) {
    switch (tile) {
        case TILE_FIELD:  return float(TM_Field);
        case TILE_SHORE:  return float(TM_Shore);
        case TILE_ROCK:   return float(TM_Rock);
        case TILE_ROAD:
        case TILE_SQUARE: return float(TM_Road);
        case TILE_WATER:  return float(TM_Water);
        default: break;
    }
    switch (biome) {
        case Biome::Tundra:  return float(TM_Tundra);
        case Biome::Taiga:   return float(TM_Taiga);
        case Biome::Snow:    return float(TM_Snow);
        case Biome::Valley:  return float(TM_Valley);
        case Biome::Swamp:   return float(TM_Swamp);
        case Biome::Desert:  return float(TM_Desert);
        case Biome::Steppe:  return float(TM_Steppe);
        case Biome::Tropics: return float(TM_Tropics);
        case Biome::Water:   return float(TM_Water);
        case Biome::Meadow:
        default:             return float(TM_Meadow);
    }
}

// Per-material base colours now live in mesh.frag (materialBase) and are picked
// per-fragment from the sampled tile id, so the CPU only needs the id mapping
// (terrain_material_for) — no CPU-side colour table or edge-preserving blur.
// Biome per tile is resolved inline in upload() (constant per 1024-tile cell).

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
    float layer;
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

// Push constants for shadow casters (depth-only pass).
struct ShadowPush {
    float lightMvp[16]; // 64B
};
struct ShadowBbPush {
    float lightMvp[16];
    float lightRight[4]; // billboard orientation in light space
};

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

    std::vector<NpcInstance> dummyNpcs(512);
    if (!npcInstBuf_.create_device_local(dev, dummyNpcs.data(),
                                         dummyNpcs.size() * sizeof(NpcInstance),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] npc buffer FAILED\n");
    }
    std::vector<CreatureInstance> dummyCreatures(512);
    if (!creatureInstBuf_.create_device_local(
            dev, dummyCreatures.data(),
            dummyCreatures.size() * sizeof(CreatureInstance),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] creature buffer FAILED\n");
    }
    if (!paperdoll_.init(dev)) {
        std::fprintf(stderr, "[Renderer3DVk] paperdoll atlas FAILED\n");
    }

    // ── A6: Shadow map + descriptor set (created first so main pipelines
    //    can reference shadowSetLayout_). ──
    if (!shadow_.init(dev, 4096)) {
        std::fprintf(stderr, "[Renderer3DVk] shadow map FAILED\n");
    }
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
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &shadowSetLayout_);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &shadowPool_);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = shadowPool_;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &shadowSetLayout_;
        vkAllocateDescriptorSets(dev.device, &dsai, &shadowSet_);

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        dii.imageView = shadow_.view;
        dii.sampler = shadow_.sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = shadowSet_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
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
    spv_path(vpath, sizeof vpath, "water.vert");
    spv_path(fpath, sizeof fpath, "water.frag");
    if (!waterPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                sizeof(WaterPush), 0, nullptr, 0,
                                /*instanced=*/false, /*depthTest=*/true,
                                /*depthWrite=*/false, /*blend=*/true,
                                /*cullBack=*/false)) {
        std::fprintf(stderr, "[Renderer3DVk] water pipeline FAILED\n");
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
        VkVertexInputAttributeDescription nAttrs[3]{};
        nAttrs[0].location = 0; nAttrs[0].binding = 0;
        nAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; nAttrs[0].offset = 0;
        nAttrs[1].location = 1; nAttrs[1].binding = 0;
        nAttrs[1].format = VK_FORMAT_R32_SFLOAT; nAttrs[1].offset = sizeof(float) * 3;
        nAttrs[2].location = 2; nAttrs[2].binding = 0;
        nAttrs[2].format = VK_FORMAT_R32_SFLOAT; nAttrs[2].offset = sizeof(float) * 4;
        VkDescriptorSetLayout npcSets[2] = {
            shadowSetLayout_, paperdoll_.set_layout()
        };
        if (!npcPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(NpcInstance),
                                   nAttrs, 3, /*instanced=*/true,
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

    spv_path(vpath, sizeof vpath, "shadow_npc.vert");
    spv_path(fpath, sizeof fpath, "shadow_npc.frag");
    {
        VkVertexInputAttributeDescription nAttrs[3]{};
        nAttrs[0].location = 0; nAttrs[0].binding = 0;
        nAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; nAttrs[0].offset = 0;
        nAttrs[1].location = 1; nAttrs[1].binding = 0;
        nAttrs[1].format = VK_FORMAT_R32_SFLOAT; nAttrs[1].offset = sizeof(float) * 3;
        nAttrs[2].location = 2; nAttrs[2].binding = 0;
        nAttrs[2].format = VK_FORMAT_R32_SFLOAT; nAttrs[2].offset = sizeof(float) * 4;
        if (!shadowNpcPipe_.create_shadow(dev, shadow_.renderPass, vpath, fpath,
                                            sizeof(ShadowBbPush), sizeof(NpcInstance),
                                            nAttrs, 3, /*instanced=*/true,
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
    paperdoll_.destroy(dev);
    shadow_.destroy(dev);
    if (shadowPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev.device, shadowPool_, nullptr);
        shadowPool_ = VK_NULL_HANDLE;
        shadowSet_  = VK_NULL_HANDLE;
    }
    if (shadowSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout_, nullptr);
        shadowSetLayout_ = VK_NULL_HANDLE;
    }
    materialTex_.destroy(dev);
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
    paperdoll_.begin_frame();
    npcCount_ = 0;
    if (ecs && uploaded_) {
        std::vector<NpcInstance> npcs;
        npcs.reserve(512);
        const float tMs = elapsed * 1000.0f;
        auto view = ecs->reg.view<ecs::Position, ecs::NpcCharacter>();
        for (auto e : view) {
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
            const int layer = paperdoll_.layer_for(
                paperdoll_.descriptor_for_seed(ch.visualSeed), anim);
            if (layer < 0) continue;
            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            const float baseM = sample_height_m(pos.x, pos.y);
            if (npcs.size() < 512) {
                npcs.push_back({wx, baseM, wz, 2.0f, float(layer)});
            }
        }
        npcCount_ = static_cast<std::uint32_t>(npcs.size());
        if (npcCount_ > 0) {
            vkCmdUpdateBuffer(cmd, npcInstBuf_.buffer, 0,
                              npcs.size() * sizeof(NpcInstance), npcs.data());
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
        creatures.reserve(256);
        auto cview = ecs->reg.view<ecs::Position, ecs::Sprite>();
        std::uint32_t idx = 0;
        for (auto e : cview) {
            const auto& spr = cview.get<ecs::Sprite>(e);
            if (spr.archetype == 0xFF) continue; // not a procedural creature
            const auto& pos = cview.get<ecs::Position>(e);
            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            const float baseM = sample_height_m(pos.x, pos.y);
            if (creatures.size() < 512) {
                CreatureInstance ci{};
                ci.px = wx; ci.py = baseM; ci.pz = wz;
                ci.size = spr.scale * 1.5f;
                ci.archetype = float(spr.archetype);
                ci.seed = float(spr.atlasId) * 2.17f + float(idx & 63u) * 0.5f;
                ci.tintR = float(spr.r) / 255.0f;
                ci.tintG = float(spr.g) / 255.0f;
                ci.tintB = float(spr.b) / 255.0f;
                creatures.push_back(ci);
            }
            ++idx;
        }
        creatureCount_ = static_cast<std::uint32_t>(creatures.size());
        if (creatureCount_ > 0) {
            vkCmdUpdateBuffer(cmd, creatureInstBuf_.buffer, 0,
                              creatures.size() * sizeof(CreatureInstance),
                              creatures.data());
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }
    paperdoll_.flush_uploads(cmd);
}

// ──────────────────────────────────────────────────────────────────────
// upload — rebuild device-local terrain mesh from the real composite
// heightmap. Exact port of GL Renderer3D::upload terrain section
// (renderer_3d.cpp lines 853-935).
// ──────────────────────────────────────────────────────────────────────
void Renderer3DVk::upload(const gpu::VulkanDevice& dev, const SeamlessSubworldManager& mgr) {
    const int N   = kMeshDim;
    const int Nv  = N + 1;
    const int step = kFullSize / N;
    const auto& hm = mgr.heightmap();
    const auto& tiles = mgr.tiles();
    if (hm.empty()) return;

    // ── Sample heights into a vertex grid in metres ──
    // Box-average each vertex over its step-sized footprint (same as GL).
    const auto vertexCount = std::size_t(Nv) * Nv;
    heightVtxM_.resize(vertexCount);
    const int half = std::max(1, step / 2);

    for (int y = 0; y < Nv; ++y) {
        const int cy = std::min(kFullSize - 1, y * step);
        const int y0 = std::max(0, cy - half);
        const int y1 = std::min(kFullSize - 1, cy + half);
        for (int x = 0; x < Nv; ++x) {
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
            heightVtxM_[std::size_t(y) * Nv + x]
                = (count > 0 ? sum / float(count) : 0.0f) * kHeightScale;
        }
    }

    // ── Build interleaved Position+Normal vertex buffer ──
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

    // ── Index buffer (two triangles per quad) ──
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

    // Destroy old buffers before recreating (handles seamless re-centre).
    terrainVtx_.destroy(dev);
    terrainIdx_.destroy(dev);

    if (!terrainVtx_.create_device_local(dev, verts.data(),
                                          verts.size() * sizeof(Vtx),
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        || !terrainIdx_.create_device_local(dev, idx.data(),
                                             idx.size() * sizeof(std::uint32_t),
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[Renderer3DVk] terrain buffers FAILED\n");
        terrainVtx_.destroy(dev);
        terrainIdx_.destroy(dev);
        indexCount_ = 0;
        uploaded_ = false;
        return;
    }

    indexCount_ = static_cast<std::uint32_t>(idx.size());
    uploaded_ = true;

    // ── Full-resolution tile material texture (sampled per-fragment by
    //    mesh.frag). One byte per tile = material id; NEAREST + clamp so road /
    //    field bands stay crisp at any distance no matter how coarse the terrain
    //    mesh is. This is the per-fragment analogue of the TS u_tileGrid and the
    //    reason roads read as connected lines instead of blobs between verts. ──
    if (tiles.size() == std::size_t(kFullSize) * kFullSize) {
        std::vector<std::uint8_t> matPix(std::size_t(kFullSize) * kFullSize);
        // Biome is constant per 1024-tile cell (3×3 grid) — resolve the 9 cell
        // biomes once instead of per tile across all ~9.4M texels.
        Biome cellBiome[9];
        for (int c = 0; c < 9; ++c) cellBiome[c] = mgr.cell_biome(c);
        for (int ty = 0; ty < kFullSize; ++ty) {
            const int cellRow = std::min(2, ty / kCellSize);
            const std::size_t row = std::size_t(ty) * kFullSize;
            for (int tx = 0; tx < kFullSize; ++tx) {
                const int cellCol = std::min(2, tx / kCellSize);
                const std::uint8_t tile = tiles[row + std::size_t(tx)];
                const float mat = terrain_material_for(
                    tile, cellBiome[cellRow * 3 + cellCol]);
                matPix[row + std::size_t(tx)] = static_cast<std::uint8_t>(mat);
            }
        }
        // Re-centre reuses the pre-allocated set: drop the old image, bake the
        // new one, then repoint the set. Callers already fence upload() against
        // in-flight frames (same contract as the terrain vertex/index buffers).
        materialTex_.destroy(dev);
        if (!materialTex_.create_r8(dev, kFullSize, kFullSize, matPix.data(),
                                    /*linearFilter=*/false, /*repeat=*/false)) {
            std::fprintf(stderr, "[Renderer3DVk] material texture FAILED\n");
        } else {
            VkDescriptorImageInfo dii{};
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dii.imageView = materialTex_.view;
            dii.sampler = materialTex_.sampler;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = materialSet_;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &dii;
            vkUpdateDescriptorSets(dev.device, 1, &w, 0, nullptr);
        }
    }

    // ── A4: Tree billboard instances from real Structure::Tree records ──
    {
        const auto& structs = mgr.structures();
        std::vector<TreeInstance> trees;
        trees.reserve(structs.size());
        for (const auto& s : structs) {
            if (s.kind != Structure::Tree) continue;
            float wx, wz;
            tile_to_world(s.x, s.y, wx, wz);
            const float baseM = sample_height_m(s.x, s.y);
            if (baseM < WATER_LEVEL * kHeightScale - 0.5f) continue;
            const float sinkM = std::max(1.25f, s.height * 0.08f);
            // Stable hash for seed (same as GL renderer).
            const float absX = float((mgr.center_cx() - 1) * kCellSize) + s.x;
            const float absY = float((mgr.center_cy() - 1) * kCellSize) + s.y;
            std::uint32_t h = std::uint32_t(absX * 374761.0f)
                * std::uint32_t{2246822519}
                ^ std::uint32_t(absY * 668265.0f)
                * std::uint32_t{3266489917};
            h ^= h >> 13; h *= std::uint32_t{1274126177}; h ^= h >> 16;
            const float hash01 =
                float(h & std::uint32_t{0x00ffffff}) / float(0x00ffffff);
            // Species from macro temperature (same as GL).
            const int cellCol = std::min(2, std::max(0, int(s.x) / kCellSize));
            const int cellRow = std::min(2, std::max(0, int(s.y) / kCellSize));
            const float temp = mgr.cell_temperature(cellRow * 3 + cellCol);
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

    // ── A5: Structure instances from real House/Wall records ──
    {
        const auto& structs = mgr.structures();
        std::vector<StructInstance> boxes;
        boxes.reserve(structs.size());
        for (const auto& s : structs) {
            if (s.kind != Structure::House && s.kind != Structure::Wall) continue;
            const float baseM = sample_height_m(s.x, s.y);
            if (baseM < WATER_LEVEL * kHeightScale - 0.5f) continue;
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
                               ecs::World* /*ecs*/, bool /*haste*/,
                               bool /*flight*/, float /*px*/, float /*py*/,
                               float elapsed) {
    if (!uploaded_ || dev_ == nullptr || indexCount_ == 0) return;

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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      terrainPipe_.pipeline);
    // set 0 = shadow sampler, set 1 = full-res tile material texture.
    if (shadowSet_ != VK_NULL_HANDLE && materialSet_ != VK_NULL_HANDLE) {
        const VkDescriptorSet sets[2] = {shadowSet_, materialSet_};
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
        if (shadowSet_ != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    treePipe_.layout, 0, 1, &shadowSet_,
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
        if (shadowSet_ != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    structPipe_.layout, 0, 1, &shadowSet_,
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
        if (shadowSet_ != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    npcPipe_.layout, 0, 1, &shadowSet_,
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
        if (shadowSet_ != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    creaturePipe_.layout, 0, 1, &shadowSet_,
                                    0, nullptr);
        VkDeviceSize cio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &creatureInstBuf_.buffer, &cio);
        vkCmdPushConstants(cmd, creaturePipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(cb), &cb);
        vkCmdDraw(cmd, 6, creatureCount_, 0, 0);
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
        wp.params[2] = waterLevel * kHeightScale;  // world-space water Y
        wp.params[3] = kWorldExtent;               // half terrain span
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          waterPipe_.pipeline);
        vkCmdPushConstants(cmd, waterPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(wp), &wp);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
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
