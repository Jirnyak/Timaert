// Vulkan Phase 5 subworld foundation smoke: a heightmap terrain mesh rendered
// with a depth-tested 3D mesh pipeline (device-local vertex/index buffers), lit
// by a quantised sun, viewed by an orbiting perspective camera. Proves the 3D
// path (depth attachment + vertex buffers + MVP) the subworld renderer needs.
// GPU_SMOKE_FRAMES=N auto-exits.
#include "gpu/vk_buffer.h"
#include "gpu/vk_device.h"
#include "gpu/vk_pipeline.h"
#include "gpu/vk_renderer.h"
#include "gpu/vk_shadow.h"
#include "gpu/vk_sprite_array.h"
#include "gpu/vk_texture.h"

#include "core/math.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTau = 6.28318530717959f;

    struct Vtx
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v; // grid UV (0..1) for the per-fragment material lookup
    };

    struct MeshPush
    {
        float mvp[16];
        float sunDir[4];
        float sunColor[4]; // rgb = sun colour
        float ambient[4];  // rgb = ambient (sky / moon)
        float lightMvp[16];
    };

    struct ShadowPush
    {
        float lightMvp[16];
    };

    struct ShadowBbPush
    {
        float lightMvp[16];
        float lightRight[4];
    };

    struct WaterPush
    {
        float mvp[16];
        float camPos[4];
        float sunDir[4];
        float sunColor[4];
        float params[4]; // time, ambient, waterLevel, extent
    };

    struct TreeInstance
    {
        float px, py, pz;
        float size;
        float species;
        float seed;
    };

    struct StructInstance
    {
        float px, py, pz; // box centre (world)
        float hx, hy, hz; // half-extents
        float type;       // 0 = wall, 1 = house
        float seed;
    };

    struct NpcInstance
    {
        float px, py, pz; // feet world position
        float size;
        float seed;
    };

    struct BbPush
    {
        float mvp[16];
        float camRight[4];
        float sunColor[4];
        float ambient[4];
        float lightMvp[16];
    };

    struct SkyPush
    {
        float forward[4];
        float right[4];
        float up[4];
        float p0[4]; // resX, resY, fov, tod
        float p1[4]; // fogR, fogG, fogB, time
    };

    // Vulkan-correct perspective: right-handed, depth 0..1, Y flipped for the
    // Vulkan clip convention (vs the GL-style mat4_perspective in core/math.h).
    sm::mat4 vk_perspective(float fovy, float aspect, float zn, float zf)
    {
        sm::mat4 r{};
        float f = 1.0f / std::tan(fovy * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = -f;
        r.m[10] = zf / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (zn * zf) / (zn - zf);
        return r;
    }

    // Vulkan-correct orthographic (depth 0..1) for the sun's shadow view.
    sm::mat4 vk_ortho(float l, float r, float b, float t, float zn, float zf)
    {
        sm::mat4 m{};
        m.m[0] = 2.0f / (r - l);
        m.m[5] = 2.0f / (t - b);
        m.m[10] = -1.0f / (zf - zn);
        m.m[12] = -(r + l) / (r - l);
        m.m[13] = -(t + b) / (t - b);
        m.m[14] = -zn / (zf - zn);
        m.m[15] = 1.0f;
        return m;
    }

    float vhash(int x, int y)
    {
        std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u
                          + static_cast<std::uint32_t>(y) * 668265263u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return static_cast<float>((n ^ (n >> 16)) & 0xffffffu)
               / static_cast<float>(0xffffff);
    }
    float vnoise(float fx, float fy)
    {
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0, ty = fy - y0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        float a = vhash(x0, y0), b = vhash(x0 + 1, y0);
        float c = vhash(x0, y0 + 1), d = vhash(x0 + 1, y0 + 1);
        return (a * (1 - tx) + b * tx) * (1 - ty)
               + (c * (1 - tx) + d * tx) * ty;
    }
    float fbm(float fx, float fy)
    {
        float s = 0, a = 0.5f, f = 1.0f, norm = 0;
        for (int i = 0; i < 5; ++i) {
            s += a * vnoise(fx * f, fy * f);
            norm += a;
            a *= 0.5f;
            f *= 2.0f;
        }
        return norm > 0 ? s / norm : 0.0f;
    }
} // namespace

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        "timaert gpu_smoke3d (Vulkan subworld terrain)", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1024, 640,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    gpu::VulkanDevice dev;
    if (!dev.init(win, /*enableValidation=*/true)) {
        std::fprintf(stderr, "[gpu_smoke3d] device init FAILED\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }
    gpu::VulkanRenderer renderer;
    if (!renderer.init(dev, win)) {
        std::fprintf(stderr, "[gpu_smoke3d] renderer init FAILED\n");
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 3;
    }

    gpu::VulkanShadowMap shadowMap;
    if (!shadowMap.init(dev, 2048)) {
        std::fprintf(stderr, "[gpu_smoke3d] shadow map FAILED\n");
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 9;
    }

    // Descriptor set 0 binding 0 = the shadow map, sampled by the terrain.
    VkDescriptorSetLayout shadowSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool shadowPool = VK_NULL_HANDLE;
    VkDescriptorSet shadowSet = VK_NULL_HANDLE;
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
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &shadowSetLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &shadowPool);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = shadowPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &shadowSetLayout;
        vkAllocateDescriptorSets(dev.device, &dsai, &shadowSet);

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        dii.imageView = shadowMap.view;
        dii.sampler = shadowMap.sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = shadowSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
    }

    // Build a heightmap terrain mesh (128x128 quads over a 16x16 world span).
    constexpr int N = 128;      // quads per side
    constexpr int V = N + 1;    // vertices per side
    constexpr float S = 8.0f;   // half extent
    const float cell = (2.0f * S) / N;
    auto heightAt = [&](int i, int j) -> float {
        float u = static_cast<float>(i) / N, v = static_cast<float>(j) / N;
        return fbm(u * 4.0f + 3.0f, v * 4.0f + 7.0f) * 1.2f;
    };
    auto clampi = [](int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    std::vector<Vtx> verts(static_cast<std::size_t>(V) * V);
    for (int j = 0; j < V; ++j) {
        for (int i = 0; i < V; ++i) {
            float x = -S + i * cell, z = -S + j * cell, y = heightAt(i, j);
            float hl = heightAt(clampi(i - 1, 0, N), j);
            float hr = heightAt(clampi(i + 1, 0, N), j);
            float hd = heightAt(i, clampi(j - 1, 0, N));
            float hu = heightAt(i, clampi(j + 1, 0, N));
            sm::vec3 nrm = sm::normalize(sm::v3(hl - hr, 2.0f * cell, hd - hu));
            verts[static_cast<std::size_t>(j) * V + i] =
                {x, y, z, nrm.x, nrm.y, nrm.z,
                 static_cast<float>(i) / N, static_cast<float>(j) / N};
        }
    }
    std::vector<std::uint32_t> idx;
    idx.reserve(static_cast<std::size_t>(N) * N * 6);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            std::uint32_t a = static_cast<std::uint32_t>(j) * V + i;
            std::uint32_t b = a + 1;
            std::uint32_t c = a + V;
            std::uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }

    gpu::VulkanBuffer vbuf, ibuf;
    if (!vbuf.create_device_local(dev, verts.data(),
                                  verts.size() * sizeof(Vtx),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        || !ibuf.create_device_local(dev, idx.data(),
                                     idx.size() * sizeof(std::uint32_t),
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[gpu_smoke3d] mesh buffers FAILED\n");
        vbuf.destroy(dev);
        ibuf.destroy(dev);
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 4;
    }

    // Per-fragment terrain material id texture (set 1). The shipping renderer
    // bakes this from the seamless tile grid; the harness has no grid, so it
    // derives biome-like material bands from elevation and paints a thin
    // crossroads of road (id 12) — the exact ~1-quad-wide feature the
    // per-fragment lookup keeps crisp instead of dissolving into blobs the way a
    // per-vertex colour on this coarse (128²) mesh would.
    constexpr int MT = 512; // material texture resolution (4 texels / mesh quad)
    std::vector<std::uint8_t> matPix(static_cast<std::size_t>(MT) * MT);
    for (int ty = 0; ty < MT; ++ty) {
        for (int tx = 0; tx < MT; ++tx) {
            int gi = clampi(tx * N / (MT - 1), 0, N);
            int gj = clampi(ty * N / (MT - 1), 0, N);
            float h = heightAt(gi, gj);
            std::uint8_t m;
            if (h < 0.14f)      m = 13; // water bed
            else if (h < 0.20f) m = 10; // shore
            else if (h < 0.55f) m = 8;  // tropics (lush green lowland)
            else if (h < 0.78f) m = 7;  // steppe
            else if (h < 0.95f) m = 11; // rock
            else                m = 2;  // snow-biome ground
            const bool onRoad = (tx >= MT / 2 - 2 && tx <= MT / 2 + 1)
                                || (ty >= MT / 2 - 2 && ty <= MT / 2 + 1);
            if (onRoad && h >= 0.20f && h < 0.95f) m = 12; // road/square
            matPix[static_cast<std::size_t>(ty) * MT + tx] = m;
        }
    }
    gpu::VulkanTexture materialTex;
    if (!materialTex.create_r8(dev, MT, MT, matPix.data(),
                               /*linearFilter=*/false, /*repeat=*/false)) {
        std::fprintf(stderr, "[gpu_smoke3d] material texture FAILED\n");
        materialTex.destroy(dev);
        vbuf.destroy(dev);
        ibuf.destroy(dev);
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 4;
    }

    // Descriptor set 1 binding 0 = the terrain material id texture (mirrors the
    // shipping renderer's set-1 material binding).
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool materialPool = VK_NULL_HANDLE;
    VkDescriptorSet materialSet = VK_NULL_HANDLE;
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
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &materialSetLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &materialPool);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = materialPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &materialSetLayout;
        vkAllocateDescriptorSets(dev.device, &dsai, &materialSet);

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii.imageView = materialTex.view;
        dii.sampler = materialTex.sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = materialSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
    }

    // Scatter instanced trees on land (skip water-ish lows and snow peaks).
    // Species assigned by elevation + noise so all 7 kinds appear.
    std::vector<TreeInstance> trees;
    {
        std::uint32_t rs = 0x51ed3f17u;
        auto rnd = [&]() {
            rs = rs * 1664525u + 1013904223u;
            return static_cast<float>((rs >> 8) & 0xffffffu)
                   / static_cast<float>(0x1000000);
        };
        for (int t = 0; t < 1400; ++t) {
            int i = 3 + static_cast<int>(rnd() * (N - 6));
            int j = 3 + static_cast<int>(rnd() * (N - 6));
            float y = heightAt(i, j);
            if (y < 0.14f || y > 0.98f) continue;
            float x = -S + i * cell + (rnd() - 0.5f) * cell;
            float z = -S + j * cell + (rnd() - 0.5f) * cell;
            float r = rnd();
            float sp;
            if (y > 0.68f) sp = r < 0.6f ? 4.0f : 2.0f;
            else if (y > 0.42f) sp = r < 0.4f ? 0.0f : (r < 0.7f ? 5.0f : 3.0f);
            else sp = r < 0.35f ? 1.0f : (r < 0.7f ? 0.0f : 6.0f);
            float sz = 0.16f + rnd() * 0.10f;
            trees.push_back({x, y, z, sz, sp,
                             static_cast<float>(t) * 1.37f + rnd() * 5.0f});
        }
    }
    const std::uint32_t treeCount = static_cast<std::uint32_t>(trees.size());
    gpu::VulkanBuffer instBuf;
    if (treeCount > 0
        && !instBuf.create_device_local(dev, trees.data(),
                                        trees.size() * sizeof(TreeInstance),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[gpu_smoke3d] tree buffer FAILED\n");
        instBuf.destroy(dev);
        vbuf.destroy(dev);
        ibuf.destroy(dev);
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 6;
    }

    // Structure meshes: a small walled settlement of instanced boxes. Extensible
    // to any landmark -- each new structure kind is one more `type` value fed to
    // the same pipeline, mirroring how more biomes/features/landmarks will add
    // context without new engine code.
    std::vector<StructInstance> structs;
    {
        auto hAtWorld = [&](float wx, float wz) {
            int i = clampi(static_cast<int>((wx + S) / cell), 0, N);
            int j = clampi(static_cast<int>((wz + S) / cell), 0, N);
            return heightAt(i, j);
        };
        const float cxw = -2.0f, czw = 1.5f; // settlement centre (world)
        const float extent = 2.2f;           // half-size of the wall ring
        const int segs = 9;
        for (int s = 0; s < segs; ++s) {
            float f = -extent + (2.0f * extent) * s / (segs - 1);
            float seg = (2.0f * extent / segs) * 0.6f;
            for (int side = 0; side < 2; ++side) { // north + south walls
                float wx = cxw + f, wz = czw + (side ? extent : -extent);
                structs.push_back({wx, hAtWorld(wx, wz) + 0.18f, wz, seg, 0.18f,
                                   0.09f, 0.0f,
                                   static_cast<float>(s * 2 + side)});
            }
            for (int side = 0; side < 2; ++side) { // east + west walls
                float wx = cxw + (side ? extent : -extent), wz = czw + f;
                structs.push_back({wx, hAtWorld(wx, wz) + 0.18f, wz, 0.09f, 0.18f,
                                   seg, 0.0f,
                                   static_cast<float>(100 + s * 2 + side)});
            }
        }
        for (int hxi = 0; hxi < 4; ++hxi) // houses in an inner grid
            for (int hzi = 0; hzi < 4; ++hzi) {
                float wx = cxw - extent * 0.55f + (extent * 1.1f / 3.0f) * hxi;
                float wz = czw - extent * 0.55f + (extent * 1.1f / 3.0f) * hzi;
                float y = hAtWorld(wx, wz);
                if (y < 0.18f) continue; // skip anything at/under the water line
                structs.push_back({wx, y + 0.15f, wz, 0.22f, 0.15f, 0.22f, 1.0f,
                                   static_cast<float>(hxi * 4 + hzi)});
            }
    }
    const std::uint32_t structCount = static_cast<std::uint32_t>(structs.size());
    gpu::VulkanBuffer structBuf;
    if (structCount > 0
        && !structBuf.create_device_local(
               dev, structs.data(), structs.size() * sizeof(StructInstance),
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[gpu_smoke3d] structure buffer FAILED\n");
        structBuf.destroy(dev);
        instBuf.destroy(dev);
        vbuf.destroy(dev);
        ibuf.destroy(dev);
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 12;
    }

    // Instanced paper-doll NPC billboards: a small crowd around the settlement
    // plus scattered wanderers on land. Same instanced pattern as the trees.
    std::vector<NpcInstance> npcs;
    {
        std::uint32_t rs = 0x9E3779B9u;
        auto rnd = [&]() {
            rs = rs * 1664525u + 1013904223u;
            return static_cast<float>((rs >> 8) & 0xffffffu)
                   / static_cast<float>(0x1000000);
        };
        for (int k = 0; k < 26; ++k) { // cluster near the settlement centre
            float wx = -2.0f + (rnd() - 0.5f) * 4.0f;
            float wz = 1.5f + (rnd() - 0.5f) * 4.0f;
            int i = clampi(static_cast<int>((wx + S) / cell), 0, N);
            int j = clampi(static_cast<int>((wz + S) / cell), 0, N);
            float y = heightAt(i, j);
            if (y < 0.18f) continue;
            npcs.push_back({wx, y, wz, 0.12f,
                            static_cast<float>(k) * 3.1f + rnd() * 7.0f});
        }
        for (int k = 0; k < 30; ++k) { // scattered wanderers
            int i = 4 + static_cast<int>(rnd() * (N - 8));
            int j = 4 + static_cast<int>(rnd() * (N - 8));
            float y = heightAt(i, j);
            if (y < 0.20f || y > 0.85f) continue;
            npcs.push_back({-S + i * cell, y, -S + j * cell, 0.11f,
                            static_cast<float>(k) * 5.7f + 100.0f + rnd() * 9.0f});
        }
    }
    const std::uint32_t npcCount = static_cast<std::uint32_t>(npcs.size());
    gpu::VulkanBuffer npcBuf;
    if (npcCount > 0
        && !npcBuf.create_device_local(dev, npcs.data(),
                                       npcs.size() * sizeof(NpcInstance),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        std::fprintf(stderr, "[gpu_smoke3d] npc buffer FAILED\n");
        npcBuf.destroy(dev);
        structBuf.destroy(dev);
        instBuf.destroy(dev);
        vbuf.destroy(dev);
        ibuf.destroy(dev);
        renderer.destroy();
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 14;
    }

    gpu::VulkanPipeline pipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/mesh.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/mesh.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = sizeof(float) * 3;
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = sizeof(float) * 6;
        const VkDescriptorSetLayout terrainSetLayouts[2] = {shadowSetLayout,
                                                            materialSetLayout};
        if (!pipeline.create_mesh(dev, renderer.renderPass, vpath, fpath,
                                  sizeof(MeshPush), sizeof(Vtx), attrs, 3,
                                  /*instanced=*/false, /*depthTest=*/true,
                                  /*depthWrite=*/true, /*blend=*/false,
                                  /*cullBack=*/false, terrainSetLayouts, 2)) {
            std::fprintf(stderr, "[gpu_smoke3d] pipeline FAILED\n");
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 5;
        }
    }

    // Instanced procedural-tree billboard pipeline (one draw for all trees).
    gpu::VulkanPipeline bbPipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/billboard.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/billboard.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription attrs[4]{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32_SFLOAT;
        attrs[1].offset = sizeof(float) * 3;
        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32_SFLOAT;
        attrs[2].offset = sizeof(float) * 4;
        attrs[3].location = 3;
        attrs[3].binding = 0;
        attrs[3].format = VK_FORMAT_R32_SFLOAT;
        attrs[3].offset = sizeof(float) * 5;
        if (!bbPipeline.create_mesh(dev, renderer.renderPass, vpath, fpath,
                                    sizeof(BbPush), sizeof(TreeInstance), attrs,
                                    4, /*instanced=*/true, /*depthTest=*/true,
                                    /*depthWrite=*/true, /*blend=*/false,
                                    /*cullBack=*/false, shadowSetLayout)) {
            std::fprintf(stderr, "[gpu_smoke3d] billboard pipeline FAILED\n");
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 7;
        }
    }

    // Procedural sky pipeline (fullscreen, drawn before geometry, depth off).
    gpu::VulkanPipeline skyPipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/fullscreen.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/sky.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!skyPipeline.create(dev, renderer.renderPass, vpath, fpath,
                                sizeof(SkyPush))) {
            std::fprintf(stderr, "[gpu_smoke3d] sky pipeline FAILED\n");
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 8;
        }
    }

    // Shadow-caster pipelines (depth-only, rendered from the sun's viewpoint).
    gpu::VulkanPipeline shadowMeshPipeline, shadowBbPipeline;
    {
        char* base = SDL_GetBasePath();
        char smv[1024], smf[1024], sbv[1024], sbf[1024];
        std::snprintf(smv, sizeof smv, "%sshaders/shadow_mesh.vert.spv",
                      base ? base : "./");
        std::snprintf(smf, sizeof smf, "%sshaders/shadow_mesh.frag.spv",
                      base ? base : "./");
        std::snprintf(sbv, sizeof sbv, "%sshaders/shadow_bb.vert.spv",
                      base ? base : "./");
        std::snprintf(sbf, sizeof sbf, "%sshaders/shadow_bb.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription mAttr{};
        mAttr.location = 0;
        mAttr.binding = 0;
        mAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        mAttr.offset = 0;
        VkVertexInputAttributeDescription bAttr[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            bAttr[i].location = i;
            bAttr[i].binding = 0;
        }
        bAttr[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        bAttr[0].offset = 0;
        bAttr[1].format = VK_FORMAT_R32_SFLOAT;
        bAttr[1].offset = sizeof(float) * 3;
        bAttr[2].format = VK_FORMAT_R32_SFLOAT;
        bAttr[2].offset = sizeof(float) * 4;
        bAttr[3].format = VK_FORMAT_R32_SFLOAT;
        bAttr[3].offset = sizeof(float) * 5;
        bool sok =
            shadowMeshPipeline.create_shadow(dev, shadowMap.renderPass, smv, smf,
                                             sizeof(ShadowPush), sizeof(Vtx),
                                             &mAttr, 1, /*instanced=*/false)
            && shadowBbPipeline.create_shadow(dev, shadowMap.renderPass, sbv, sbf,
                                              sizeof(ShadowBbPush),
                                              sizeof(TreeInstance), bAttr, 4,
                                              /*instanced=*/true);
        if (!sok) {
            std::fprintf(stderr, "[gpu_smoke3d] shadow pipelines FAILED\n");
            shadowBbPipeline.destroy(dev);
            shadowMeshPipeline.destroy(dev);
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 10;
        }
    }

    // Transparent water plane (depth test, no depth write, alpha blend).
    gpu::VulkanPipeline waterPipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/water.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/water.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!waterPipeline.create_mesh(dev, renderer.renderPass, vpath, fpath,
                                       sizeof(WaterPush), 0, nullptr, 0,
                                       /*instanced=*/false, /*depthTest=*/true,
                                       /*depthWrite=*/false, /*blend=*/true,
                                       /*cullBack=*/false)) {
            std::fprintf(stderr, "[gpu_smoke3d] water pipeline FAILED\n");
            waterPipeline.destroy(dev);
            shadowBbPipeline.destroy(dev);
            shadowMeshPipeline.destroy(dev);
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 11;
        }
    }

    // Structure pipelines: lit boxes (receive shadow) + depth-only caster. Same
    // instanced-geometry-from-gl_VertexIndex pattern as the trees/water.
    gpu::VulkanPipeline structPipeline, structShadowPipeline;
    {
        char* base = SDL_GetBasePath();
        char vp[1024], fp[1024], sv[1024], sf[1024];
        std::snprintf(vp, sizeof vp, "%sshaders/struct.vert.spv",
                      base ? base : "./");
        std::snprintf(fp, sizeof fp, "%sshaders/struct.frag.spv",
                      base ? base : "./");
        std::snprintf(sv, sizeof sv, "%sshaders/shadow_struct.vert.spv",
                      base ? base : "./");
        std::snprintf(sf, sizeof sf, "%sshaders/shadow_struct.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription sa[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            sa[i].location = i;
            sa[i].binding = 0;
        }
        sa[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        sa[0].offset = 0;
        sa[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        sa[1].offset = sizeof(float) * 3;
        sa[2].format = VK_FORMAT_R32_SFLOAT;
        sa[2].offset = sizeof(float) * 6;
        sa[3].format = VK_FORMAT_R32_SFLOAT;
        sa[3].offset = sizeof(float) * 7;
        bool ok =
            structPipeline.create_mesh(dev, renderer.renderPass, vp, fp,
                                       sizeof(MeshPush), sizeof(StructInstance),
                                       sa, 4, /*instanced=*/true,
                                       /*depthTest=*/true, /*depthWrite=*/true,
                                       /*blend=*/false, /*cullBack=*/false,
                                       shadowSetLayout)
            && structShadowPipeline.create_shadow(
                   dev, shadowMap.renderPass, sv, sf, sizeof(ShadowPush),
                   sizeof(StructInstance), sa, 2, /*instanced=*/true);
        if (!ok) {
            std::fprintf(stderr, "[gpu_smoke3d] structure pipelines FAILED\n");
            structShadowPipeline.destroy(dev);
            structPipeline.destroy(dev);
            waterPipeline.destroy(dev);
            shadowBbPipeline.destroy(dev);
            shadowMeshPipeline.destroy(dev);
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            structBuf.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 13;
        }
    }

    // NPC paper-doll billboard pipeline (instanced, receives shadow) + its
    // depth-only shadow caster (reuses the shared npc sprite coverage).
    gpu::VulkanPipeline npcPipeline, npcShadowPipeline;
    // Paper-doll sprite pool sampled by the shared npc shaders (sampler2DArray
    // u_paperdolls): set 1 in the lit pass, set 0 in the depth-only shadow pass.
    // The shipping renderer fills this from PaperdollAtlas; the smoke only needs
    // a valid, visible pool, so it uploads one opaque silhouette per layer. The
    // instance "seed" is consumed as the array layer and clamps into range, so
    // every NPC samples opaque art regardless of its seed value.
    gpu::SpriteArray npcSprites;
    {
        char* base = SDL_GetBasePath();
        char vp[1024], fp[1024], sv[1024], sf[1024];
        std::snprintf(vp, sizeof vp, "%sshaders/npc.vert.spv",
                      base ? base : "./");
        std::snprintf(fp, sizeof fp, "%sshaders/npc.frag.spv",
                      base ? base : "./");
        std::snprintf(sv, sizeof sv, "%sshaders/shadow_npc.vert.spv",
                      base ? base : "./");
        std::snprintf(sf, sizeof sf, "%sshaders/shadow_npc.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);

        // Build the paper-doll pool. init() leaves every layer valid (cleared
        // transparent) so the pipeline is legal even before any upload; we then
        // paint one opaque humanoid silhouette per layer so the crowd is
        // actually visible in the smoke.
        constexpr std::uint32_t kNpcLayers = 8;
        if (!npcSprites.init(dev, 48, 48, kNpcLayers, /*linearFilter=*/false)) {
            std::fprintf(stderr, "[gpu_smoke3d] npc sprite pool FAILED\n");
            structShadowPipeline.destroy(dev);
            structPipeline.destroy(dev);
            waterPipeline.destroy(dev);
            shadowBbPipeline.destroy(dev);
            shadowMeshPipeline.destroy(dev);
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            npcBuf.destroy(dev);
            structBuf.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 15;
        }
        {
            std::vector<std::uint8_t> doll(48u * 48u * 4u, 0);
            for (std::uint32_t L = 0; L < kNpcLayers; ++L) {
                const float tint =
                    0.55f + 0.45f * (static_cast<float>(L)
                                     / static_cast<float>(kNpcLayers - 1));
                for (int qy = 0; qy < 48; ++qy) {
                    for (int qx = 0; qx < 48; ++qx) {
                        const float u = (qx - 24.0f) / 24.0f; // -1..1
                        const float vv = qy / 48.0f;          // 0 top .. 1 feet
                        bool solid;
                        if (vv < 0.30f) { // round head
                            const float hu = u, hv = (vv - 0.15f) / 0.15f;
                            solid = (hu * hu + hv * hv) < 1.0f;
                        } else { // tapering body
                            const float halfW = 0.30f + 0.35f * (vv - 0.30f);
                            solid = std::fabs(u) < halfW;
                        }
                        std::uint8_t* p =
                            &doll[(static_cast<std::size_t>(qy) * 48 + qx) * 4];
                        if (solid) {
                            p[0] = static_cast<std::uint8_t>(150.0f * tint);
                            p[1] = static_cast<std::uint8_t>(110.0f * tint);
                            p[2] = static_cast<std::uint8_t>(90.0f * tint);
                            p[3] = 255;
                        } else {
                            p[0] = p[1] = p[2] = p[3] = 0;
                        }
                    }
                }
                npcSprites.upload_layer_now(dev, L, doll.data());
            }
        }

        VkVertexInputAttributeDescription na[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            na[i].location = i;
            na[i].binding = 0;
        }
        na[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        na[0].offset = 0;
        na[1].format = VK_FORMAT_R32_SFLOAT;
        na[1].offset = sizeof(float) * 3;
        na[2].format = VK_FORMAT_R32_SFLOAT;
        na[2].offset = sizeof(float) * 4;
        const VkDescriptorSetLayout npcSets[2] = {shadowSetLayout,
                                                  npcSprites.setLayout};
        if (!npcPipeline.create_mesh(dev, renderer.renderPass, vp, fp,
                                     sizeof(BbPush), sizeof(NpcInstance), na, 3,
                                     /*instanced=*/true, /*depthTest=*/true,
                                     /*depthWrite=*/true, /*blend=*/true,
                                     /*cullBack=*/false, npcSets, 2)
            || !npcShadowPipeline.create_shadow(dev, shadowMap.renderPass, sv, sf,
                                                sizeof(ShadowBbPush),
                                                sizeof(NpcInstance), na, 3,
                                                /*instanced=*/true,
                                                npcSprites.setLayout)) {
            std::fprintf(stderr, "[gpu_smoke3d] npc pipeline FAILED\n");
            npcShadowPipeline.destroy(dev);
            npcPipeline.destroy(dev);
            npcSprites.destroy(dev);
            structShadowPipeline.destroy(dev);
            structPipeline.destroy(dev);
            waterPipeline.destroy(dev);
            shadowBbPipeline.destroy(dev);
            shadowMeshPipeline.destroy(dev);
            skyPipeline.destroy(dev);
            bbPipeline.destroy(dev);
            pipeline.destroy(dev);
            npcBuf.destroy(dev);
            structBuf.destroy(dev);
            instBuf.destroy(dev);
            vbuf.destroy(dev);
            ibuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 15;
        }
    }

    int frameCap = 0;
    if (const char* e = SDL_getenv("GPU_SMOKE_FRAMES")) frameCap = std::atoi(e);

    bool running = true;
    int frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = false;
            else if (ev.type == SDL_KEYDOWN
                     && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            else if (ev.type == SDL_WINDOWEVENT
                     && (ev.window.event == SDL_WINDOWEVENT_RESIZED
                         || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
                renderer.framebufferResized = true;
        }

        if (renderer.acquire_frame(win)) {
            VkCommandBuffer c = renderer.current_command_buffer();
            const VkExtent2D ext = renderer.swapchain.extent;

            float t = static_cast<float>(frame) * 0.01f;
            float ang = t * 0.5f;
            sm::vec3 eye = sm::v3(std::cos(ang) * 14.0f, 8.0f,
                                  std::sin(ang) * 14.0f);
            sm::vec3 center = sm::v3(0.0f, 0.4f, 0.0f);
            sm::vec3 worldUp = sm::v3(0.0f, 1.0f, 0.0f);
            sm::mat4 view = sm::mat4_lookAt(eye, center, worldUp);
            float aspect = static_cast<float>(ext.width)
                           / static_cast<float>(ext.height);
            const float fov = 60.0f * kPi / 180.0f;
            sm::mat4 proj = vk_perspective(fov, aspect, 0.1f, 100.0f);
            sm::mat4 mvp = sm::mat4_mul(proj, view);

            // Day/night cycle drives the sun + ambient (dynamic lighting).
            float tod = std::fmod(static_cast<float>(frame) * 0.0005f, 1.0f);
            float sunAng = (tod - 0.25f) * kTau;
            sm::vec3 sunDir = sm::v3(std::cos(sunAng), std::sin(sunAng), 0.0f);
            float dayI = sm::clamp01((std::sin(sunAng) + 0.10f) / 0.35f);
            float highness = sm::clamp01(std::sin(sunAng) / 0.5f);
            sm::vec3 warm = sm::v3(1.0f, 0.55f, 0.25f);
            sm::vec3 white = sm::v3(1.0f, 0.96f, 0.88f);
            sm::vec3 sunColor = (warm + (white - warm) * highness) * dayI;
            sm::vec3 nightAmb = sm::v3(0.10f, 0.13f, 0.22f);
            sm::vec3 dayAmb = sm::v3(0.35f, 0.35f, 0.38f);
            sm::vec3 ambient = nightAmb + (dayAmb - nightAmb) * dayI;

            // Sun-view orthographic light matrix for the shadow map.
            sm::vec3 lightEye = center + sunDir * 20.0f;
            sm::mat4 lightView =
                sm::mat4_lookAt(lightEye, center, sm::v3(0.0f, 0.0f, 1.0f));
            sm::mat4 lightMvp = sm::mat4_mul(
                vk_ortho(-14.0f, 14.0f, -14.0f, 14.0f, 1.0f, 45.0f), lightView);

            // ---- Shadow pass: terrain + trees cast into the sun-view depth ----
            shadowMap.begin(c);
            // The shadow pipelines enable dynamic depth bias (slope-scaled, to
            // keep cast shadows off the receiver surface); the bias MUST be set
            // on the command buffer before any shadow draw. One call covers all
            // shadow pipelines in the pass (mirrors the shipping renderer).
            vkCmdSetDepthBias(c, 1.0f, 0.0f, 1.5f);
            {
                ShadowPush sp{};
                std::memcpy(sp.lightMvp, lightMvp.m, sizeof(sp.lightMvp));
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  shadowMeshPipeline.pipeline);
                VkDeviceSize so = 0;
                vkCmdBindVertexBuffers(c, 0, 1, &vbuf.buffer, &so);
                vkCmdBindIndexBuffer(c, ibuf.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(c, shadowMeshPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(sp), &sp);
                vkCmdDrawIndexed(c, static_cast<std::uint32_t>(idx.size()), 1, 0,
                                 0, 0);

                if (treeCount > 0) {
                    ShadowBbPush sbb{};
                    std::memcpy(sbb.lightMvp, lightMvp.m, sizeof(sbb.lightMvp));
                    sbb.lightRight[2] = 1.0f; // world z: perp. to the sun (xy)
                    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      shadowBbPipeline.pipeline);
                    VkDeviceSize sio = 0;
                    vkCmdBindVertexBuffers(c, 0, 1, &instBuf.buffer, &sio);
                    vkCmdPushConstants(c, shadowBbPipeline.layout,
                                       VK_SHADER_STAGE_VERTEX_BIT
                                           | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(sbb), &sbb);
                    vkCmdDraw(c, 6, treeCount, 0, 0);
                }

                if (structCount > 0) {
                    ShadowPush ssp{};
                    std::memcpy(ssp.lightMvp, lightMvp.m, sizeof(ssp.lightMvp));
                    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      structShadowPipeline.pipeline);
                    VkDeviceSize sso = 0;
                    vkCmdBindVertexBuffers(c, 0, 1, &structBuf.buffer, &sso);
                    vkCmdPushConstants(c, structShadowPipeline.layout,
                                       VK_SHADER_STAGE_VERTEX_BIT
                                           | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(ssp), &ssp);
                    vkCmdDraw(c, 36, structCount, 0, 0);
                }

                if (npcCount > 0) {
                    ShadowBbPush snp{};
                    std::memcpy(snp.lightMvp, lightMvp.m, sizeof(snp.lightMvp));
                    snp.lightRight[2] = 1.0f; // world z: perp. to the sun (xy)
                    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      npcShadowPipeline.pipeline);
                    // shadow_npc.frag samples the paper-doll atlas at set 0
                    // (the shadow pass has no shadow-map sampler of its own).
                    const VkDescriptorSet sdolls = npcSprites.descriptorSet;
                    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            npcShadowPipeline.layout, 0, 1,
                                            &sdolls, 0, nullptr);
                    VkDeviceSize sno = 0;
                    vkCmdBindVertexBuffers(c, 0, 1, &npcBuf.buffer, &sno);
                    vkCmdPushConstants(c, npcShadowPipeline.layout,
                                       VK_SHADER_STAGE_VERTEX_BIT
                                           | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(snp), &snp);
                    vkCmdDraw(c, 6, npcCount, 0, 0);
                }
            }
            shadowMap.end(c);

            // ---- Main pass ----
            renderer.begin_render_pass(0.45f, 0.62f, 0.85f);
            VkViewport vpt{};
            vpt.width = static_cast<float>(ext.width);
            vpt.height = static_cast<float>(ext.height);
            vpt.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.extent = ext;
            vkCmdSetViewport(c, 0, 1, &vpt);
            vkCmdSetScissor(c, 0, 1, &scissor);

            // ---- Sky (fullscreen, behind everything) ----
            {
                sm::vec3 fwd = sm::normalize(center - eye);
                sm::vec3 rgt = sm::normalize(sm::cross(fwd, worldUp));
                sm::vec3 upv = sm::cross(rgt, fwd);
                SkyPush sky{};
                sky.forward[0] = fwd.x;
                sky.forward[1] = fwd.y;
                sky.forward[2] = fwd.z;
                sky.right[0] = rgt.x;
                sky.right[1] = rgt.y;
                sky.right[2] = rgt.z;
                sky.up[0] = upv.x;
                sky.up[1] = upv.y;
                sky.up[2] = upv.z;
                sky.p0[0] = static_cast<float>(ext.width);
                sky.p0[1] = static_cast<float>(ext.height);
                sky.p0[2] = fov;
                sky.p0[3] = tod;
                sky.p1[0] = 0.45f;
                sky.p1[1] = 0.62f;
                sky.p1[2] = 0.85f;
                sky.p1[3] = static_cast<float>(frame) * 0.02f;
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  skyPipeline.pipeline);
                vkCmdPushConstants(c, skyPipeline.layout,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sky),
                                   &sky);
                vkCmdDraw(c, 3, 1, 0, 0);
            }

            // ---- Terrain (receives shadows) ----
            MeshPush push{};
            std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
            push.sunDir[0] = sunDir.x;
            push.sunDir[1] = sunDir.y;
            push.sunDir[2] = sunDir.z;
            push.sunColor[0] = sunColor.x;
            push.sunColor[1] = sunColor.y;
            push.sunColor[2] = sunColor.z;
            push.ambient[0] = ambient.x;
            push.ambient[1] = ambient.y;
            push.ambient[2] = ambient.z;
            std::memcpy(push.lightMvp, lightMvp.m, sizeof(push.lightMvp));
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline.pipeline);
            const VkDescriptorSet terrainSets[2] = {shadowSet, materialSet};
            vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline.layout, 0, 2, terrainSets, 0,
                                    nullptr);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(c, 0, 1, &vbuf.buffer, &off);
            vkCmdBindIndexBuffer(c, ibuf.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(c, pipeline.layout,
                               VK_SHADER_STAGE_VERTEX_BIT
                                   | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);
            vkCmdDrawIndexed(c, static_cast<std::uint32_t>(idx.size()), 1, 0, 0,
                             0);

            // ---- Trees ----
            if (treeCount > 0) {
                BbPush bb{};
                std::memcpy(bb.mvp, mvp.m, sizeof(bb.mvp));
                sm::vec3 cr = sm::normalize(
                    sm::v3(view.m[0], view.m[4], view.m[8]));
                bb.camRight[0] = cr.x;
                bb.camRight[1] = cr.y;
                bb.camRight[2] = cr.z;
                bb.sunColor[0] = sunColor.x;
                bb.sunColor[1] = sunColor.y;
                bb.sunColor[2] = sunColor.z;
                bb.ambient[0] = ambient.x;
                bb.ambient[1] = ambient.y;
                bb.ambient[2] = ambient.z;
                std::memcpy(bb.lightMvp, lightMvp.m, sizeof(bb.lightMvp));
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  bbPipeline.pipeline);
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        bbPipeline.layout, 0, 1, &shadowSet, 0,
                                        nullptr);
                VkDeviceSize io = 0;
                vkCmdBindVertexBuffers(c, 0, 1, &instBuf.buffer, &io);
                vkCmdPushConstants(c, bbPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(bb), &bb);
                vkCmdDraw(c, 6, treeCount, 0, 0);
            }

            // ---- Structures (city walls + houses; cast + receive shadows) ----
            if (structCount > 0) {
                MeshPush sp2{};
                std::memcpy(sp2.mvp, mvp.m, sizeof(sp2.mvp));
                sp2.sunDir[0] = sunDir.x;
                sp2.sunDir[1] = sunDir.y;
                sp2.sunDir[2] = sunDir.z;
                sp2.sunColor[0] = sunColor.x;
                sp2.sunColor[1] = sunColor.y;
                sp2.sunColor[2] = sunColor.z;
                sp2.ambient[0] = ambient.x;
                sp2.ambient[1] = ambient.y;
                sp2.ambient[2] = ambient.z;
                std::memcpy(sp2.lightMvp, lightMvp.m, sizeof(sp2.lightMvp));
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  structPipeline.pipeline);
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        structPipeline.layout, 0, 1, &shadowSet,
                                        0, nullptr);
                VkDeviceSize so2 = 0;
                vkCmdBindVertexBuffers(c, 0, 1, &structBuf.buffer, &so2);
                vkCmdPushConstants(c, structPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(sp2), &sp2);
                vkCmdDraw(c, 36, structCount, 0, 0);
            }

            // ---- NPCs (paper-doll billboards; receive shadows) ----
            if (npcCount > 0) {
                BbPush np{};
                std::memcpy(np.mvp, mvp.m, sizeof(np.mvp));
                sm::vec3 cr = sm::normalize(
                    sm::v3(view.m[0], view.m[4], view.m[8]));
                np.camRight[0] = cr.x;
                np.camRight[1] = cr.y;
                np.camRight[2] = cr.z;
                np.sunColor[0] = sunColor.x;
                np.sunColor[1] = sunColor.y;
                np.sunColor[2] = sunColor.z;
                np.ambient[0] = ambient.x;
                np.ambient[1] = ambient.y;
                np.ambient[2] = ambient.z;
                std::memcpy(np.lightMvp, lightMvp.m, sizeof(np.lightMvp));
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  npcPipeline.pipeline);
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        npcPipeline.layout, 0, 1, &shadowSet, 0,
                                        nullptr);
                // npc.frag samples the paper-doll atlas at set 1.
                const VkDescriptorSet ndolls = npcSprites.descriptorSet;
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        npcPipeline.layout, 1, 1, &ndolls, 0,
                                        nullptr);
                VkDeviceSize no = 0;
                vkCmdBindVertexBuffers(c, 0, 1, &npcBuf.buffer, &no);
                vkCmdPushConstants(c, npcPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(np), &np);
                vkCmdDraw(c, 6, npcCount, 0, 0);
            }

            // ---- Water (transparent, fills the valleys below the water line) ----
            {
                WaterPush wp{};
                std::memcpy(wp.mvp, mvp.m, sizeof(wp.mvp));
                wp.camPos[0] = eye.x;
                wp.camPos[1] = eye.y;
                wp.camPos[2] = eye.z;
                wp.sunDir[0] = sunDir.x;
                wp.sunDir[1] = sunDir.y;
                wp.sunDir[2] = sunDir.z;
                wp.sunColor[0] = sunColor.x;
                wp.sunColor[1] = sunColor.y;
                wp.sunColor[2] = sunColor.z;
                wp.params[0] = static_cast<float>(frame) * 0.02f;
                wp.params[1] = ambient.y;
                wp.params[2] = 0.16f; // water level
                wp.params[3] = 8.0f;  // extent (half terrain span)
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  waterPipeline.pipeline);
                vkCmdPushConstants(c, waterPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(wp), &wp);
                vkCmdDraw(c, 6, 1, 0, 0);
            }

            if (!renderer.end_frame(win)) running = false;
        }

        ++frame;
        if (frameCap > 0 && frame >= frameCap) running = false;
    }

    vkDeviceWaitIdle(dev.device);
    npcShadowPipeline.destroy(dev);
    npcPipeline.destroy(dev);
    npcSprites.destroy(dev);
    structShadowPipeline.destroy(dev);
    structPipeline.destroy(dev);
    waterPipeline.destroy(dev);
    shadowBbPipeline.destroy(dev);
    shadowMeshPipeline.destroy(dev);
    vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
    vkDestroyDescriptorPool(dev.device, materialPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, materialSetLayout, nullptr);
    materialTex.destroy(dev);
    shadowMap.destroy(dev);
    skyPipeline.destroy(dev);
    bbPipeline.destroy(dev);
    pipeline.destroy(dev);
    npcBuf.destroy(dev);
    structBuf.destroy(dev);
    instBuf.destroy(dev);
    vbuf.destroy(dev);
    ibuf.destroy(dev);
    renderer.destroy();
    dev.destroy();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::fprintf(stderr, "[gpu_smoke3d] terrain loop OK (%d frames)\n", frame);
    return 0;
}
