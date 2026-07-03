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
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sm::sub {

namespace {

// Physical scale — must match the GL Renderer3D exactly.
constexpr float kTileMeters  = 1.0f;
constexpr float kWorldExtent = float(kFullSize) * kTileMeters * 0.5f; // 1536 m
constexpr float kHeightScale = 1500.0f;

// Per-vertex layout: position (3) + normal (3).
struct Vtx {
    float px, py, pz;
    float nx, ny, nz;
};

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
    float seed;
};

// Push constants for shadow casters (depth-only pass).
struct ShadowPush {
    float lightMvp[16]; // 64B
};
struct ShadowBbPush {
    float lightMvp[16];
    float lightRight[4]; // billboard orientation in light space
};

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

    // ── A6: Shadow map + descriptor set (created first so main pipelines
    //    can reference shadowSetLayout_). ──
    if (!shadow_.init(dev, 2048)) {
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

    // A1: Terrain mesh pipeline (mesh.vert + mesh.frag).
    spv_path(vpath, sizeof vpath, "mesh.vert");
    spv_path(fpath, sizeof fpath, "mesh.frag");

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = sizeof(float) * 3;

    if (!terrainPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                  sizeof(MeshPush), sizeof(Vtx), attrs, 2,
                                  /*instanced=*/false, /*depthTest=*/true,
                                  /*depthWrite=*/true, /*blend=*/false,
                                  /*cullBack=*/false, shadowSetLayout_)) {
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
        if (!npcPipe_.create_mesh(dev, mainPass, vpath, fpath,
                                   sizeof(BbPush), sizeof(NpcInstance),
                                   nAttrs, 3, /*instanced=*/true,
                                   /*depthTest=*/true, /*depthWrite=*/true,
                                   /*blend=*/false, /*cullBack=*/false,
                                   shadowSetLayout_)) {
            std::fprintf(stderr, "[Renderer3DVk] npc pipeline FAILED\n");
        }
    }

    // A6: Shadow caster pipelines (depth-only, into shadow_.renderPass).
    spv_path(vpath, sizeof vpath, "shadow_mesh.vert");
    spv_path(fpath, sizeof fpath, "shadow_mesh.frag");
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
                                            nAttrs, 3, /*instanced=*/true)) {
            std::fprintf(stderr, "[Renderer3DVk] shadow npc pipeline FAILED\n");
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
    indexCount_ = 0;
    heightVtxM_.clear();
    dev_  = nullptr;
    pass_ = VK_NULL_HANDLE;
    uploaded_ = false;
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

            verts[i] = {wx, wy, wz, n.x, n.y, n.z};
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
void Renderer3DVk::record_shadow(VkCommandBuffer cmd) {
    if (!uploaded_ || shadow_.image == VK_NULL_HANDLE) return;

    shadow_.begin(cmd);

    // Terrain (indexed mesh).
    if (indexCount_ > 0) {
        ShadowPush sp{};
        std::memcpy(sp.lightMvp, lightMvp_.m, sizeof(sp.lightMvp));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowMeshPipe_.pipeline);
        VkDeviceSize so = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &terrainVtx_.buffer, &so);
        vkCmdBindIndexBuffer(cmd, terrainIdx_.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, shadowMeshPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sp), &sp);
        vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
    }

    // Trees (instanced billboards).
    if (treeCount_ > 0) {
        ShadowBbPush sbb{};
        std::memcpy(sbb.lightMvp, lightMvp_.m, sizeof(sbb.lightMvp));
        sbb.lightRight[2] = 1.0f; // world-Z is perpendicular to sun direction
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
        sbb.lightRight[2] = 1.0f; // world-Z is perpendicular to sun direction
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowNpcPipe_.pipeline);
        VkDeviceSize sio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &npcInstBuf_.buffer, &sio);
        vkCmdPushConstants(cmd, shadowNpcPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sbb), &sbb);
        vkCmdDraw(cmd, 6, npcCount_, 0, 0);
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
                               float elapsed) {
    if (!uploaded_ || dev_ == nullptr || indexCount_ == 0) return;

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

    // Light MVP for shadow mapping — ortho from the sun's POV.
    const vec3 center = {cam.pos.x, cam.pos.y, cam.pos.z};
    const vec3 lightEye = {
        center.x + sun.sunDir.x * 20.0f,
        center.y + sun.sunDir.y * 20.0f,
        center.z + sun.sunDir.z * 20.0f
    };
    const vec3 lightUp = {0.0f, 0.0f, 1.0f};
    mat4 lightView = mat4_lookAt(lightEye, center, lightUp);
    mat4 lightMvp  = mat4_mul(
        vk_ortho(-14.0f, 14.0f, -14.0f, 14.0f, 1.0f, 45.0f), lightView);
    lightMvp_ = lightMvp; // cache for record_shadow

    // Update NPCs.
    if (ecs) {
        std::vector<NpcInstance> npcs;
        npcs.reserve(512);
        auto view = ecs->reg.view<ecs::Position, ecs::NpcCharacter>();
        for (auto e : view) {
            const auto& pos = view.get<ecs::Position>(e);
            float wx = 0.0f, wz = 0.0f;
            tile_to_world(pos.x, pos.y, wx, wz);
            const float baseM = sample_height_m(pos.x, pos.y);
            const ecs::NpcCharacter* ch = ecs->reg.try_get<ecs::NpcCharacter>(e);
            if (npcs.size() < 512) {
                npcs.push_back({wx, baseM, wz, 3.2f, ch ? ch->visualSeed : 0.0f});
            }
        }
        npcCount_ = static_cast<std::uint32_t>(npcs.size());
        if (npcCount_ > 0) {
            vkCmdUpdateBuffer(cmd, npcInstBuf_.buffer, 0, npcs.size() * sizeof(NpcInstance), npcs.data());
            // Add a memory barrier after update to ensure it's visible to vertex shader.
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 1, &mb, 0, nullptr, 0, nullptr);
        }
    }

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
    // mesh.frag expects sunDir pointing FROM sun TOWARD world (negated).
    push.sunDir[0] = -sun.sunDir.x;
    push.sunDir[1] = -sun.sunDir.y;
    push.sunDir[2] = -sun.sunDir.z;
    push.sunColor[0] = sun.sunColor.x;
    push.sunColor[1] = sun.sunColor.y;
    push.sunColor[2] = sun.sunColor.z;
    push.ambient[0] = sun.ambientColor.x;
    push.ambient[1] = sun.ambientColor.y;
    push.ambient[2] = sun.ambientColor.z;
    std::memcpy(push.lightMvp, lightMvp.m, sizeof(push.lightMvp));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      terrainPipe_.pipeline);
    if (shadowSet_ != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                terrainPipe_.layout, 0, 1, &shadowSet_,
                                0, nullptr);
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
        sp.sunDir[0] = -sun.sunDir.x;
        sp.sunDir[1] = -sun.sunDir.y;
        sp.sunDir[2] = -sun.sunDir.z;
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
        VkDeviceSize nio = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &npcInstBuf_.buffer, &nio);
        vkCmdPushConstants(cmd, npcPipe_.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(nb), &nb);
        vkCmdDraw(cmd, 6, npcCount_, 0, 0);
    }

    // ── A3: Water (transparent quad, drawn last, depth test but no write) ──
    {
        WaterPush wp{};
        std::memcpy(wp.mvp, mvp.m, sizeof(wp.mvp));
        wp.camPos[0] = cam.pos.x;
        wp.camPos[1] = cam.pos.y;
        wp.camPos[2] = cam.pos.z;
        wp.sunDir[0] = -sun.sunDir.x;
        wp.sunDir[1] = -sun.sunDir.y;
        wp.sunDir[2] = -sun.sunDir.z;
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
