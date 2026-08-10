// Vulkan Phase 5 subworld foundation smoke: a heightmap terrain mesh rendered
// with a depth-tested 3D mesh pipeline (device-local vertex/index buffers), lit
// by a quantised sun, viewed by an orbiting perspective camera. Proves the 3D
// path (depth attachment + vertex buffers + MVP) the subworld renderer needs.
// Runs GPU_SMOKE_FRAMES frames then auto-exits (default 600, so a bare headless
// run self-terminates); GPU_SMOKE_FRAMES=0 is the explicit unbounded escape hatch.
#include "gpu/vk_buffer.h"
#include "gpu/vk_device.h"
#include "gpu/vk_pipeline.h"
#include "gpu/vk_renderer.h"
#include "gpu/vk_shadow.h"
#include "gpu/bb_instance.h"
#include "gpu/vk_sprite_array.h"
#include "gpu/vk_texture.h"

// Paper-doll pool — npc.frag samples one composited sampler2DArray layer at
// set 1; the smoke mirrors the SHIPPING pool (assets/character/atlas.* through
// PaperdollAtlas) instead of a stub so the harness enforces the real npc
// shader contract.
// The stb_image implementation lives in sprite_atlas.cpp in the shipping
// binary; this harness doesn't link that TU, so it hosts its own.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "assets/paperdoll_atlas.h"

#include "core/math.h"
#include "sub/lighting.h" // GpuLightBuffer — exact std430 layout for set0/binding1
#include "sub/sky.h"      // SkyStarsUbo + celestial context for the sky pass

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

    // ── Headless visual-capture helpers (opt-in; default run is unchanged) ──
    // A point-light billboard proof needs to be LOOKED at, but the shipping game
    // window stalls in CAMetalLayer nextDrawable when launched head-less/back-
    // grounded (no compositor drains the swapchain). This offscreen-style PPM
    // capture piggybacks on the reliable smoke loop — same shaders, same set-0
    // light SSBO — mirroring tools/macro_shot so the frame can be inspected with
    // zero swapchain-timing risk. All gated on env vars; unset ⇒ byte-identical.
    bool write_ppm_bgra(const char* path, const std::uint8_t* px, int w, int h,
                        bool bgra)
    {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) {
            std::fprintf(stderr, "[gpu_smoke3d] shot: cannot open %s\n", path);
            return false;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        std::vector<std::uint8_t> rgb(std::size_t(w) * std::size_t(h) * 3u);
        for (std::size_t i = 0, n = std::size_t(w) * std::size_t(h); i < n; ++i) {
            const std::uint8_t b0 = px[i * 4 + 0];
            const std::uint8_t b1 = px[i * 4 + 1];
            const std::uint8_t b2 = px[i * 4 + 2];
            rgb[i * 3 + 0] = bgra ? b2 : b0; // R
            rgb[i * 3 + 1] = b1;             // G
            rgb[i * 3 + 2] = bgra ? b0 : b2; // B
        }
        const std::size_t wrote = std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
        return wrote == rgb.size();
    }
    int env_int(const char* name, int fallback)
    {
        const char* v = SDL_getenv(name);
        if (!v || !*v) return fallback;
        return std::atoi(v);
    }
    float env_float(const char* name, float fallback)
    {
        const char* v = SDL_getenv(name);
        if (!v || !*v) return fallback;
        return static_cast<float>(std::atof(v));
    }

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

    // Billboard instances (trees AND paper-doll NPCs here) are the shipping
    // contract itself — gpu/bb_instance.h, the same header the renderer
    // compiles. There is no mirrored struct left to go stale.
    using gpu::BbInstance;

    struct StructInstance
    {
        float px, py, pz; // box centre (world)
        float hx, hy, hz; // half-extents
        float type;       // 0 = wall, 1 = house
        float seed;
        float yaw = 0.0f; // rotation about vertical (shipping parity; the
                          // harness scene keeps its boxes axis-aligned)
    };

    // Distinct paper-doll identities preloaded into the sprite pool; instances
    // reference them round-robin (a fresh pool assigns layers 0..N-1 in
    // layer_for_now call order, so doll ordinal == pool layer here).
    constexpr std::uint32_t kNpcDollCount = 8;

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
        float forward[4]; // xyz camera forward, w = moonCount
        float right[4];   // xyz camera right,   w = starSizeScale
        float up[4];      // xyz camera up,      w = (reserved: weather)
        float p0[4];      // resX, resY, fov, tod
        float p1[4];      // fogR, fogG, fogB, time
        float sun[4];     // xyz = toward the sun (celestial sun_dir)
        float moonDirSize[3][4];  // xyz toward moon, w baseSize
        float moonColIllum[3][4]; // rgb tint, w illuminated fraction
        float p2[4];      // cloudiness01, windX, windZ, precip01
        float p3[4];      // seasonal day-sky tint multiplier rgb, w = flash
    };

    // Mirrors the shipping renderer's PrecipPush (precip.frag).
    struct PrecipPush
    {
        float p0[4]; // resX, resY, time(sec), precip01
        float p1[4]; // kind, tilt, flash01, reserved
    };

    // ── FX additive particles (GPU_SMOKE_FX) — mirrors the shipping renderer's
    //    particle pass EXACTLY (sub/vk_renderer_3d.cpp: ParticlePush + the sim's
    //    32-byte sub::ParticleInstance). The harness hand-builds it (the
    //    shared-shader contract) so a change to particle.vert/.frag or the
    //    instance layout is validated here too. Kept byte-identical to the sim's
    //    ParticleInstance so this stays a faithful proof of the real path. ──
    struct ParticlePush
    {
        float mvp[16];
        float camRight[4];
        float camUp[4];
    };
    struct ParticleInstanceGpu
    {
        float px, py, pz;          // world position
        float size;                // world-space half-size (m)
        float r, g, b, alpha;      // emissive colour + envelope alpha
    };
    static_assert(sizeof(ParticleInstanceGpu) == 32,
                  "must match sub::ParticleInstance / particle.vert attrs");

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

    // Shared descriptor set 0: binding 0 = the shadow map (sampled by every lit
    // pass); binding 1 = the point-light SSBO. The shipping renderer carries both
    // (src/sub/vk_renderer_3d), and the shared lighting.glsl declares binding 1 —
    // so mesh.frag / struct.frag now READ it. Per the shared-shader contract this
    // harness MUST mirror the exact set-0 layout or its pipelines mismatch the
    // shader interface. The buffer here is a benign zero-count GpuLightBuffer:
    // point_lights() sums over count==0 and returns black, so the harness frame is
    // byte-identical to before — it only proves the binding-1 path is valid.
    VkDescriptorSetLayout shadowSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool shadowPool = VK_NULL_HANDLE;
    VkDescriptorSet shadowSet = VK_NULL_HANDLE;
    gpu::VulkanBuffer lightBuf;
    // 1×1 stand-in for the shipping heightfield at binding 2 (a regular
    // sampled texture — the shadow map's comparison sampler is not legal in a
    // plain sampler2D slot on Metal). terrainParams.x = 0 keeps the march off.
    gpu::VulkanTexture dummyHeight;
    {
        const float zero = 0.0f;
        if (!dummyHeight.create_r32f(dev, 1, 1, &zero, false, false)) {
            std::fprintf(stderr, "[gpu_smoke3d] dummy height tex FAILED\n");
            return 9;
        }
    }
    {
        if (!lightBuf.create_host_mapped(dev, sizeof(sm::sub::GpuLightBuffer),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
            std::fprintf(stderr, "[gpu_smoke3d] light SSBO alloc FAILED\n");
            return 9;
        }
        static_cast<sm::sub::GpuLightBuffer*>(lightBuf.mapped)->count = 0;

        // Binding 2 mirrors the shipping heightfield slot (lighting.glsl
        // u_heightM). The harness's toy world has no heightfield texture, so
        // it binds its shadow map as a placeholder and sets terrainParams.x=0
        // — terrain_visibility() early-outs to 1.0 and the frame stays
        // byte-identical while the 3-binding contract is enforced.
        // Binding 3 mirrors the shipping WIDE shadow level. The harness has
        // one map, so it binds it to BOTH levels and fills lightMvpFar with
        // the same matrix: the handoff reduces to the single-map result and
        // the frame stays byte-identical while the 4-binding contract holds.
        VkDescriptorSetLayoutBinding b[5]{};
        b[0].binding = 0;
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        // Immutable comparison sampler — Metal requires it (see the shipping
        // renderer's identical binding); shadowMap.init ran above.
        b[0].pImmutableSamplers = &shadowMap.sampler;
        b[1].binding = 1;
        b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[1].descriptorCount = 1;
        b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[2].binding = 2;
        b[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[2].descriptorCount = 1;
        b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[3].binding = 3;
        b[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[3].descriptorCount = 1;
        b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b[3].pImmutableSamplers = &shadowMap.sampler;
        b[4].binding = 4; // light field slot: dummy (span=0 gates it off)
        b[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[4].descriptorCount = 1;
        b[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 5;
        dlci.pBindings = b;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &shadowSetLayout);

        VkDescriptorPoolSize ps[2] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = ps;
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
        VkDescriptorBufferInfo dbi{};
        dbi.buffer = lightBuf.buffer;
        dbi.offset = 0;
        dbi.range = sizeof(sm::sub::GpuLightBuffer);
        VkWriteDescriptorSet writes[5]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = shadowSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &dii;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = shadowSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &dbi;
        VkDescriptorImageInfo diiHeight{};
        diiHeight.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        diiHeight.imageView = dummyHeight.view;
        diiHeight.sampler = dummyHeight.sampler;
        writes[2] = writes[0]; // heightfield slot: the 1×1 stand-in
        writes[2].dstBinding = 2;
        writes[2].pImageInfo = &diiHeight;
        writes[3] = writes[0]; // wide shadow level: the same single map
        writes[3].dstBinding = 3;
        writes[4] = writes[2]; // light field slot: the same 1x1 stand-in
        writes[4].dstBinding = 4;
        vkUpdateDescriptorSets(dev.device, 5, writes, 0, nullptr);
    }

    // Opt-in dynamic-lighting proof (default OFF ⇒ count stays 0, frame is
    // byte-identical to the pre-point-light harness):
    //   GPU_SMOKE_LIGHT=1  inject ONE warm point light at the NPC/tree cluster
    //                      centre so tree/NPC billboards (billboard.frag /
    //                      npc.frag, point_lights_flat) glow with it.
    //   GPU_SMOKE_NIGHT=1  pin time-of-day to deep night so the ONLY warm light
    //                      in frame is the point light — unmistakable proof.
    //   GPU_SMOKE_SHOT=<p> after GPU_SMOKE_SHOT_FRAME frames (default 90), copy
    //                      the rendered frame to PPM <p> and exit 0.
    //   GPU_SMOKE_LIGHT_WATER=1  aim the camera to graze low across the deepest
    //                      water cell (found by scanning the heightmap) and, when
    //                      GPU_SMOKE_LIGHT is also on, place that light over the
    //                      same cell — so water.frag's reflected point-light
    //                      glint (point_lights_spec) is staged in frame. Kept
    //                      INDEPENDENT of GPU_SMOKE_LIGHT so a same-camera control
    //                      (LIGHT_WATER=1 LIGHT=0) yields a pixel-comparable
    //                      before/after on the water. Default OFF.
    const bool  optLight  = env_int("GPU_SMOKE_LIGHT", 0) != 0;
    const bool  optNight  = env_int("GPU_SMOKE_NIGHT", 0) != 0;
    const bool  optSky    = env_int("GPU_SMOKE_SKY", 0) != 0;
    // Precipitation overrides: force an amount/kind/storm so a capture can
    // stage rain, snow, hail or lightning regardless of the calendar.
    const char* precipEnv = SDL_getenv("GPU_SMOKE_PRECIP");
    const float optPrecip = precipEnv ? float(std::atof(precipEnv)) : 0.0f;
    const int   optPrecipKind = env_int("GPU_SMOKE_PRECIP_KIND", 0);
    const float optStorm  = env_int("GPU_SMOKE_STORM", 0) != 0 ? 1.0f : 0.0f;
    const bool  optLightWater = env_int("GPU_SMOKE_LIGHT_WATER", 0) != 0;
    //   GPU_SMOKE_NPC_CLOSE=1  stand the camera among the paper-doll crowd
    //                      instead of orbiting the whole island. At the island
    //                      framing a body is a handful of pixels, so a change to
    //                      npc.frag (lighting) or to the body's DRAWN HEIGHT is
    //                      invisible in a diff of the wide shot — the frame that
    //                      is supposed to prove it proves nothing. Combine with
    //                      GPU_SMOKE_NIGHT / GPU_SMOKE_LIGHT for the before/after
    //                      pair that shows a torch falling on people. Default OFF
    //                      ⇒ every existing capture is unchanged.
    const bool  optNpcClose = env_int("GPU_SMOKE_NPC_CLOSE", 0) != 0;
    //   GPU_SMOKE_FX=1  stage a standing additive-particle burst hovering over
    //                   the cluster centre so the additive pass (particle.vert/
    //                   .frag) is proven: overlapping emissive cards accumulate
    //                   into a bloom. Pair with GPU_SMOKE_NIGHT for an
    //                   unmistakable glow against a dark scene, and with
    //                   GPU_SMOKE_SHOT for a LOOK-able frame. Default OFF ⇒
    //                   particleCount 0 ⇒ frame byte-identical to before.
    const bool  optFx     = env_int("GPU_SMOKE_FX", 0) != 0;
    //   GPU_SMOKE_FIELD=1  paint two ploughed patches beside the crossroads,
    //                      one per furrow orientation (material ids 9 / 14) —
    //                      the LOOK frame for the per-cell field furrows
    //                      (mesh.frag; orientation picked by the C++ material
    //                      builder in the shipping renderer).
    const bool  optField  = env_int("GPU_SMOKE_FIELD", 0) != 0;
    // GPU_SMOKE_FX_TRAIL=1 stages the Inc-B shapes instead of the standing burst:
    // a spell-bolt TRAIL (evenly-spaced tinted motes along the bolt's path, what
    // ParticleSystem::emit_streak lays down) capped by a brighter IMPACT burst at
    // the head. Same additive pass/shaders — just a different, Inc-B-shaped
    // deterministic layout so the trail+impact can be LOOKED at head-on. Implies
    // optFx. Tint defaults warm (fireball); GPU_SMOKE_FX_VIOLET=1 makes it arcane.
    const bool  optFxTrail  = env_int("GPU_SMOKE_FX_TRAIL", 0) != 0;
    const bool  optFxViolet = env_int("GPU_SMOKE_FX_VIOLET", 0) != 0;
    // GPU_SMOKE_FX_BLOOD=1 stages the Inc-C impact spray instead: a ground-level
    // BLOOD burst (dark-red droplets flung out + falling) with, to its right, a
    // DUST puff (grey, the Undead/Hulk archetype spray) — the two damage-hit
    // effects side by side using the EXACT kFxPresets[] Blood/Dust rows, so a
    // head-on LOOK judges whether dark blood reads on the additive pass. Implies
    // the FX pass on.
    const bool  optFxBlood  = env_int("GPU_SMOKE_FX_BLOOD", 0) != 0;
    const char* optShot   = SDL_getenv("GPU_SMOKE_SHOT");
    const int   shotFrame = env_int("GPU_SMOKE_SHOT_FRAME", 90);
    if (optLight || optNight || optShot) {
        std::fprintf(stderr,
                     "[gpu_smoke3d] capture opts: light=%d night=%d shot=%s "
                     "shotFrame=%d\n",
                     int(optLight), int(optNight), optShot ? optShot : "(none)",
                     shotFrame);
        std::fflush(stderr);
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
    // Locate the deepest water cell (lowest heightmap sample) so GPU_SMOKE_LIGHT
    // _WATER can stage the light over open water — the water glint (water.frag
    // point_lights_spec) needs the light ABOVE a submerged cell, not the central
    // hill the default cluster light sits on. Pure scan, no hardcoded coords: the
    // map's own lowest point IS its water. waterY mirrors the water plane draw.
    constexpr float kHarnessWaterY = 0.16f;
    float deepX = 0.0f, deepZ = 0.0f, deepH = 1e9f;
    for (int j = 0; j <= N; ++j) {
        for (int i = 0; i <= N; ++i) {
            float h = heightAt(i, j);
            if (h < deepH) { deepH = h; deepX = -S + i * cell; deepZ = -S + j * cell; }
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
            if (optField && !onRoad && h >= 0.20f && h < 0.78f) {
                // Two field patches flanking the road: east = E-W furrows
                // (id 9), west = N-S furrows (id 14).
                if (ty > MT / 2 + 6 && ty < MT / 2 + 86) {
                    if (tx > MT / 2 + 6 && tx < MT / 2 + 86)  m = 9;
                    if (tx > MT / 2 - 86 && tx < MT / 2 - 6)  m = 14;
                }
            }
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
    std::vector<BbInstance> trees;
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
            std::uint32_t sp;
            if (y > 0.68f) sp = r < 0.6f ? 4u : 2u;
            else if (y > 0.42f) sp = r < 0.4f ? 0u : (r < 0.7f ? 5u : 3u);
            else sp = r < 0.35f ? 1u : (r < 0.7f ? 0u : 6u);
            float sz = 0.16f + rnd() * 0.10f;
            // Harness units, not metres: keep the historical quad (half-width
            // sz, height sz*3.2) so before/after light captures stay comparable.
            trees.push_back({x, y, z, sz, sz * 3.2f, sp,
                             gpu::bb_seed_bits(static_cast<float>(t) * 1.37f
                                               + rnd() * 5.0f),
                             0xFFFFFFFFu});
        }
        // GPU_SMOKE_FIELD: wheat stands (sprite row 7, the Crop prop) on the
        // two ploughed patches so the LOOK frame shows crops on furrows.
        if (optField) {
            for (int t = 0; t < 260; ++t) {
                const float side = (t & 1) ? 1.0f : -1.0f;
                const float px = side * (0.06f + rnd() * 0.55f) * S * 0.32f
                                 + side * S * 0.045f;
                const float pz = (0.06f + rnd() * 0.55f) * S * 0.32f
                                 + S * 0.045f;
                const int gi = clampi(int((px + S) / cell), 3, N - 3);
                const int gj = clampi(int((pz + S) / cell), 3, N - 3);
                const float y = heightAt(gi, gj);
                if (y < 0.20f || y > 0.78f) continue;
                const float sz = 0.05f + rnd() * 0.02f;
                trees.push_back({px, y, pz, sz, sz * 2.6f, 7u,
                                 gpu::bb_seed_bits(static_cast<float>(t) * 2.13f),
                                 0xFFFFFFFFu});
            }
        }
    }
    const std::uint32_t treeCount = static_cast<std::uint32_t>(trees.size());
    gpu::VulkanBuffer instBuf;
    if (treeCount > 0
        && !instBuf.create_device_local(dev, trees.data(),
                                        trees.size() * sizeof(BbInstance),
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
    std::vector<BbInstance> npcs;
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
            // The doll quad is square: halfW = height/2, like the shipping
            // prepare_frame fill.
            npcs.push_back({wx, y, wz, 0.06f, 0.12f,
                            static_cast<std::uint32_t>(k) % kNpcDollCount,
                            0u, 0xFFFFFFFFu});
        }
        for (int k = 0; k < 30; ++k) { // scattered wanderers
            int i = 4 + static_cast<int>(rnd() * (N - 8));
            int j = 4 + static_cast<int>(rnd() * (N - 8));
            float y = heightAt(i, j);
            if (y < 0.20f || y > 0.85f) continue;
            npcs.push_back({-S + i * cell, y, -S + j * cell, 0.055f, 0.11f,
                            static_cast<std::uint32_t>(k + 3) % kNpcDollCount,
                            0u, 0xFFFFFFFFu});
        }
    }
    const std::uint32_t npcCount = static_cast<std::uint32_t>(npcs.size());
    // Where the crowd actually stands, measured rather than guessed: the
    // GPU_SMOKE_NPC_CLOSE framing has to seat the camera at eye level with a
    // body, and a body here is 0.12 world units tall — a camera placed by
    // eyeball ends up inside the hill.
    float npcCenterX = 0.0f, npcCenterY = 0.0f, npcCenterZ = 0.0f, npcSize = 0.12f;
    if (npcCount > 0) {
        npcCenterX = npcs[0].px;
        npcCenterY = npcs[0].py;
        npcCenterZ = npcs[0].pz;
        npcSize = npcs[0].height;
    }
    gpu::VulkanBuffer npcBuf;
    if (npcCount > 0
        && !npcBuf.create_device_local(dev, npcs.data(),
                                       npcs.size() * sizeof(BbInstance),
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
        std::snprintf(fpath, sizeof fpath, "%sshaders/tree.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!bbPipeline.create_mesh(dev, renderer.renderPass, vpath, fpath,
                                    sizeof(BbPush), sizeof(BbInstance),
                                    gpu::kBbInstanceAttrs,
                                    gpu::kBbInstanceAttrCount,
                                    /*instanced=*/true, /*depthTest=*/true,
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

    // Procedural sky pipeline (fullscreen, drawn before geometry, depth off)
    // + the static constellation-star UBO at set 0 — mirrors the shipping
    // renderer byte-for-byte (shared-shader contract).
    gpu::VulkanPipeline skyPipeline;
    VkDescriptorSetLayout skySetLayout = VK_NULL_HANDLE;
    VkDescriptorPool skyPool = VK_NULL_HANDLE;
    VkDescriptorSet skySet = VK_NULL_HANDLE;
    gpu::VulkanBuffer skyStarsBuf;
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
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &skySetLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &skyPool);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = skyPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &skySetLayout;
        vkAllocateDescriptorSets(dev.device, &dsai, &skySet);

        sm::sub::SkyStarsUbo stars{};
        sm::sub::fill_sky_stars(stars);
        skyStarsBuf.create_device_local(dev, &stars, sizeof(stars),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorBufferInfo bi{};
        bi.buffer = skyStarsBuf.buffer;
        bi.range  = sizeof(stars);
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = skySet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(dev.device, 1, &w, 0, nullptr);
    }
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/fullscreen.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/sky.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!skyPipeline.create(dev, renderer.renderPass, vpath, fpath,
                                sizeof(SkyPush), skySetLayout)) {
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

    // Precipitation overlay pipeline — mirrors the shipping renderer's A2b
    // pass (fullscreen.vert + precip.frag, depth off, alpha over).
    gpu::VulkanPipeline precipPipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/fullscreen.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/precip.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!precipPipeline.create_mesh(dev, renderer.renderPass, vpath, fpath,
                                        sizeof(PrecipPush), 0, nullptr, 0,
                                        /*instanced=*/false,
                                        /*depthTest=*/false,
                                        /*depthWrite=*/false, /*blend=*/true,
                                        /*cullBack=*/false)) {
            std::fprintf(stderr, "[gpu_smoke3d] precip pipeline FAILED\n");
            precipPipeline.destroy(dev);
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
        std::snprintf(sbf, sizeof sbf, "%sshaders/shadow_tree.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription mAttr{};
        mAttr.location = 0;
        mAttr.binding = 0;
        mAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        mAttr.offset = 0;
        bool sok =
            shadowMeshPipeline.create_shadow(dev, shadowMap.renderPass, smv, smf,
                                             sizeof(ShadowPush), sizeof(Vtx),
                                             &mAttr, 1, /*instanced=*/false)
            && shadowBbPipeline.create_shadow(dev, shadowMap.renderPass, sbv, sbf,
                                              sizeof(ShadowBbPush),
                                              sizeof(BbInstance),
                                              gpu::kBbInstanceAttrs,
                                              gpu::kBbInstanceAttrCount,
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
            lightBuf.destroy(dev);
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
                                       /*cullBack=*/false, shadowSetLayout)) {
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
            lightBuf.destroy(dev);
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
        VkVertexInputAttributeDescription sa[5]{};
        for (std::uint32_t i = 0; i < 5; ++i) {
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
        sa[4].format = VK_FORMAT_R32_SFLOAT;
        sa[4].offset = sizeof(float) * 8;
        bool ok =
            structPipeline.create_mesh(dev, renderer.renderPass, vp, fp,
                                       sizeof(MeshPush), sizeof(StructInstance),
                                       sa, 5, /*instanced=*/true,
                                       /*depthTest=*/true, /*depthWrite=*/true,
                                       /*blend=*/false, /*cullBack=*/false,
                                       shadowSetLayout)
            && structShadowPipeline.create_shadow(
                   dev, shadowMap.renderPass, sv, sf, sizeof(ShadowPush),
                   sizeof(StructInstance), sa, 5, /*instanced=*/true);
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
            lightBuf.destroy(dev);
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
    // The REAL paper-doll pool (assets/character/atlas.* composed into the
    // sprite pool): npc.frag samples it at set 1 in the lit pass, shadow_npc
    // at set 0 in the depth-only pass. Mirroring the shipping pool keeps this
    // harness honest about the npc shader contract — a stub sprite source
    // silently went stale once before and turned the whole smoke red.
    sm::character::PaperdollAtlas npcDolls;
    {
        char* base = SDL_GetBasePath();
        char vp[1024], fp[1024], sv[1024], sf[1024];
        std::snprintf(vp, sizeof vp, "%sshaders/billboard.vert.spv",
                      base ? base : "./");
        std::snprintf(fp, sizeof fp, "%sshaders/npc.frag.spv",
                      base ? base : "./");
        std::snprintf(sv, sizeof sv, "%sshaders/shadow_bb.vert.spv",
                      base ? base : "./");
        std::snprintf(sf, sizeof sf, "%sshaders/shadow_npc.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);

        bool dollsOk = npcDolls.init(dev);
        if (dollsOk) {
            // Preload the identities the instances reference: a fresh pool
            // assigns layers 0..kNpcDollCount-1 in call order, matching the
            // round-robin `layer` baked into the instance buffer above.
            const sm::character::AnimationState idle{};
            for (std::uint32_t k = 0; k < kNpcDollCount && dollsOk; ++k) {
                const std::uint32_t layer = npcDolls.layer_for_now(
                    dev,
                    npcDolls.descriptor_for_seed(0x9E3779B9u + k * 0x85EBCA6Bu),
                    idle);
                dollsOk = (layer == k);
            }
        }
        if (!dollsOk) {
            std::fprintf(stderr, "[gpu_smoke3d] paperdoll atlas FAILED\n");
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
            lightBuf.destroy(dev);
            vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
            shadowMap.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 15;
        }

        const VkDescriptorSetLayout npcSets[2] = {shadowSetLayout,
                                                  npcDolls.set_layout()};
        if (!npcPipeline.create_mesh(dev, renderer.renderPass, vp, fp,
                                     sizeof(BbPush), sizeof(BbInstance),
                                     gpu::kBbInstanceAttrs,
                                     gpu::kBbInstanceAttrCount,
                                     /*instanced=*/true, /*depthTest=*/true,
                                     /*depthWrite=*/true, /*blend=*/true,
                                     /*cullBack=*/false, npcSets, 2)
            || !npcShadowPipeline.create_shadow(dev, shadowMap.renderPass, sv, sf,
                                                sizeof(ShadowBbPush),
                                                sizeof(BbInstance),
                                                gpu::kBbInstanceAttrs,
                                                gpu::kBbInstanceAttrCount,
                                                /*instanced=*/true,
                                                npcDolls.set_layout())) {
            std::fprintf(stderr, "[gpu_smoke3d] npc pipeline FAILED\n");
            npcShadowPipeline.destroy(dev);
            npcPipeline.destroy(dev);
            npcDolls.destroy(dev);
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
            lightBuf.destroy(dev);
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

    // ── FX additive-particle pass (GPU_SMOKE_FX): pipeline + a standing burst of
    //    emissive cards hovering over the cluster centre. Mirrors the shipping
    //    renderer's particle pass byte-for-byte (same shaders, same 3 attrs, same
    //    additive/depth flags, same ParticlePush) so it validates that path. A
    //    failed pipeline is non-fatal (matches shipping): particleCount stays 0
    //    and the pass self-skips. Built here, after every other pipeline, so it
    //    reuses the established teardown ordering below. ──
    gpu::VulkanPipeline particlePipeline;
    gpu::VulkanBuffer   particleBuf;
    std::uint32_t       particleCount = 0;
    if (optFx || optFxTrail || optFxBlood) {
        // Deterministic (fixed layout, no RNG) so the A/B capture is stable.
        std::vector<ParticleInstanceGpu> burst;
        const float cx = 0.0f, cy = 1.2f, cz = 1.5f; // over the NPC/tree cluster
        if (optFxBlood) {
            // ── Inc-C shape: a melee/spell IMPACT spray. Two bursts side by side
            //    at ~mid-body height, built from the EXACT kFxPresets[] Blood and
            //    Dust rows (cross-check src/sub/particles.cpp) so the LOOK judges
            //    the real effect. Blood: dark-red droplets flung out in a full
            //    sphere (spread=1), heavy gravity so they arc down within a short
            //    life. Dust: grey, slower, gravity-bound, grows as it disperses —
            //    the Undead/Hulk bloodless spray. Position them so both sit in
            //    frame just above the ground the camera grazes.
            const float gy = 1.1f;               // kSprayHeightM (engine mid-body)
            struct Spray { float ox; float r, g, b; int n; float spd; float grow; };
            const Spray sprays[2] = {
                // Blood row: countMax 16, colour {0.55,0.03,0.03}, size 0.30→0.55.
                {-0.9f, 0.55f, 0.03f, 0.03f, 16, 3.2f, 0.0f},
                // Dust row: countMax 14, colour {0.55,0.48,0.40}, size 0.45→0.90.
                {+0.9f, 0.55f, 0.48f, 0.40f, 14, 1.7f, 1.0f},
            };
            for (const Spray& s : sprays) {
                for (int i = 0; i < s.n; ++i) {
                    // Deterministic pseudo-scatter over a full sphere (spread=1):
                    // no RNG so the A/B is stable, but enough angular variety to
                    // read as a spray, not a ring.
                    const float u = (static_cast<float>(i) + 0.5f)
                                    / static_cast<float>(s.n);
                    const float ph = u * kTau * 2.0f + s.ox;      // azimuth churn
                    const float el = (u - 0.5f) * 3.14159265f;    // -π/2..π/2
                    const float t = u;                            // fake age 0..1
                    const float rad = 0.12f + t * s.spd * 0.14f;  // flung outward
                    ParticleInstanceGpu p{};
                    p.px = cx + s.ox + std::cos(ph) * std::cos(el) * rad;
                    // droplets have arced DOWN by mid-life (heavy Blood gravity);
                    // dust hangs a touch higher. Bias downward with age.
                    p.py = gy + std::sin(el) * rad * 0.6f - t * 0.35f;
                    p.pz = cz + std::sin(ph) * std::cos(el) * rad;
                    // size lerps start→end like pack(): dust grows, blood shrinks.
                    const float szStart = (s.grow > 0.5f) ? 0.45f : 0.30f;
                    const float szEnd   = (s.grow > 0.5f) ? 0.90f : 0.55f;
                    p.size = szStart + (szEnd - szStart) * t;
                    p.r = s.r; p.g = s.g; p.b = s.b;
                    // pack()'s alpha envelope: quick ramp then fade. Mimic mid-life.
                    p.alpha = (t < 0.15f) ? (t / 0.15f)
                                          : (1.0f - (t - 0.15f) / 0.85f);
                    if (p.alpha < 0.0f) p.alpha = 0.0f;
                    burst.push_back(p);
                }
            }
        } else if (optFxTrail) {
            // ── Inc-B shape: a spell-bolt TRAIL + IMPACT. The trail is a line of
            //    evenly-spaced motes (exactly what ParticleSystem::emit_streak
            //    lays down as a bolt crosses a segment), fading from the muzzle
            //    toward the head; the impact is a denser bright pop at the head
            //    (emit(FireBurst/MagicBurst)). Tinted from one colour so trail and
            //    burst share a hue — the universal "bolt colours its own FX" rule.
            const float tr = optFxViolet ? 0.75f : 1.00f; // arcane vs fire
            const float tg = optFxViolet ? 0.45f : 0.55f;
            const float tb = optFxViolet ? 1.00f : 0.15f;
            // Trail: 16 motes from behind the camera-left toward the head at cx.
            constexpr int kTrail = 16;
            const float x0 = -2.4f, x1 = 0.0f;           // muzzle → head (world X)
            for (int i = 0; i < kTrail; ++i) {
                const float t = static_cast<float>(i) / float(kTrail - 1);
                ParticleInstanceGpu p{};
                p.px = x0 + (x1 - x0) * t;
                p.py = cy + std::sin(t * 6.0f) * 0.04f;   // faint waver
                p.pz = cz;
                p.size = 0.14f + t * 0.06f;               // a touch bigger near head
                p.alpha = 0.20f + t * 0.45f;              // fades toward the tail
                p.r = tr; p.g = tg; p.b = tb;
                burst.push_back(p);
            }
            // Impact burst at the head: two concentric rings, brighter/denser.
            constexpr int kRings = 4;
            for (int ring = 0; ring < kRings; ++ring) {
                const float t = static_cast<float>(ring) / float(kRings - 1);
                const int   n = 8 + ring * 5;
                const float rad = 0.10f + t * 0.55f;
                const float sz = 0.30f - t * 0.16f;
                const float a = 0.90f - t * 0.55f;
                for (int i = 0; i < n; ++i) {
                    const float ph = (static_cast<float>(i) / float(n)) * kTau
                                     + float(ring) * 0.6f;
                    ParticleInstanceGpu p{};
                    p.px = cx + std::cos(ph) * rad;
                    p.py = cy + std::sin(ph * 1.7f) * 0.14f + t * 0.15f;
                    p.pz = cz + std::sin(ph) * rad;
                    p.size = sz;
                    // White-hot core → tinted edge (mix toward the bolt hue).
                    p.r = tr + (1.0f - tr) * (1.0f - t);
                    p.g = tg + (1.0f - tg) * (1.0f - t);
                    p.b = tb + (1.0f - tb) * (1.0f - t);
                    p.alpha = a;
                    burst.push_back(p);
                }
            }
        } else {
            // A compact, overlapping cloud so additive accumulation is visible as
            // a bright core fading to a halo — the signature of the additive pass.
            constexpr int kRings = 6;
            for (int ring = 0; ring < kRings; ++ring) {
                const float t = static_cast<float>(ring) / float(kRings - 1);
                const int   n = 6 + ring * 4;
                const float rad = 0.15f + t * 0.75f;         // grows outward
                const float sz = 0.34f - t * 0.20f;          // shrinks outward
                const float a = 0.85f - t * 0.62f;           // fades outward
                // warm fire core (white-hot centre) → deep ember edge.
                const float r = 1.0f;
                const float g = 0.35f + (1.0f - t) * 0.55f;
                const float b = 0.10f + (1.0f - t) * 0.45f;
                for (int i = 0; i < n; ++i) {
                    const float ph = (static_cast<float>(i) / float(n)) * kTau
                                     + float(ring) * 0.6f;
                    ParticleInstanceGpu p{};
                    p.px = cx + std::cos(ph) * rad;
                    p.py = cy + (t - 0.35f) * 0.6f + std::sin(ph * 1.7f) * 0.12f;
                    p.pz = cz + std::sin(ph) * rad;
                    p.size = sz;
                    p.r = r; p.g = g; p.b = b; p.alpha = a;
                    burst.push_back(p);
                }
            }
        }
        particleCount = static_cast<std::uint32_t>(burst.size());
        char* base = SDL_GetBasePath();
        char vp[1024], fp[1024];
        std::snprintf(vp, sizeof vp, "%sshaders/particle.vert.spv",
                      base ? base : "./");
        std::snprintf(fp, sizeof fp, "%sshaders/particle.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        VkVertexInputAttributeDescription pa[3]{};
        pa[0].location = 0; pa[0].binding = 0;
        pa[0].format = VK_FORMAT_R32G32B32_SFLOAT;    pa[0].offset = 0;
        pa[1].location = 1; pa[1].binding = 0;
        pa[1].format = VK_FORMAT_R32_SFLOAT;          pa[1].offset = sizeof(float) * 3;
        pa[2].location = 2; pa[2].binding = 0;
        pa[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; pa[2].offset = sizeof(float) * 4;
        const bool ok =
            particleBuf.create_device_local(
                dev, burst.data(), burst.size() * sizeof(ParticleInstanceGpu),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            && particlePipeline.create_mesh(
                   dev, renderer.renderPass, vp, fp, sizeof(ParticlePush),
                   sizeof(ParticleInstanceGpu), pa, 3, /*instanced=*/true,
                   /*depthTest=*/true, /*depthWrite=*/false, /*blend=*/true,
                   /*cullBack=*/false, VK_NULL_HANDLE, /*additive=*/true);
        if (!ok) {
            std::fprintf(stderr, "[gpu_smoke3d] FX particle pipeline FAILED "
                                 "(non-fatal, pass skipped)\n");
            particleCount = 0;
        } else {
            std::fprintf(stderr, "[gpu_smoke3d] FX: %u additive particles staged\n",
                         particleCount);
        }
    }

    // Bounded by default so a bare headless/agent invocation self-terminates: with
    // no window manager to deliver SDL_QUIT/ESC, an uncapped loop spins forever and
    // the "terrain loop OK" invariant below is never reached. Override with
    // GPU_SMOKE_FRAMES=N; GPU_SMOKE_FRAMES=0 restores the unbounded interactive run.
    int frameCap = 600;
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
            if (optLightWater) {
                // Look down toward the lit water cell from a modest height: steep
                // enough that the pond fills frame unoccluded by the surrounding
                // trees, shallow enough that the reflected torch still smears into
                // a shimmering streak on the ripples. Slow orbit keeps it dynamic.
                center = sm::v3(deepX, kHarnessWaterY, deepZ);
                eye = sm::v3(deepX + std::cos(ang) * 4.0f, kHarnessWaterY + 3.4f,
                             deepZ + std::sin(ang) * 4.0f);
            } else if (optNpcClose) {
                // Eye level with the crowd, close enough that ONE body fills a
                // good part of the frame: the only framing in which a
                // paper-doll's shading or its drawn HEIGHT can be judged by
                // looking. Distances are expressed in body heights, so this
                // framing survives any change to how tall a body is.
                center = sm::v3(npcCenterX, npcCenterY + npcSize * 0.5f,
                                npcCenterZ);
                eye = sm::v3(npcCenterX + std::cos(ang) * npcSize * 5.0f,
                             npcCenterY + npcSize * 0.8f,
                             npcCenterZ + std::sin(ang) * npcSize * 5.0f);
            } else if (optSky) {
                // GPU_SMOKE_SKY: stand at the terrain centre and look UP at a
                // slow full-circle pan, ~45° above the horizon — the framing
                // in which the sky pass itself (moons, phases, star field,
                // clouds) can be judged by looking. Pair with GPU_SMOKE_NIGHT
                // to pin the moonlit sky, or leave the cycle running to watch
                // sunrise/set sweep the dome.
                eye = sm::v3(0.0f, 6.0f, 0.0f);
                center = sm::v3(std::cos(ang) * 10.0f, 6.0f + 10.0f,
                                std::sin(ang) * 10.0f);
            }
            sm::vec3 worldUp = sm::v3(0.0f, 1.0f, 0.0f);
            sm::mat4 view = sm::mat4_lookAt(eye, center, worldUp);
            float aspect = static_cast<float>(ext.width)
                           / static_cast<float>(ext.height);
            const float fov = 60.0f * kPi / 180.0f;
            sm::mat4 proj = vk_perspective(fov, aspect, 0.1f, 100.0f);
            sm::mat4 mvp = sm::mat4_mul(proj, view);

            // Day/night cycle drives the sun + ambient (dynamic lighting).
            // GPU_SMOKE_NIGHT pins deep night so a point light is the only warm
            // source in frame (proof capture); otherwise the usual slow cycle.
            float tod = optNight
                            ? 0.0f
                            : std::fmod(static_cast<float>(frame) * 0.0005f, 1.0f);
            // celestial.h owns the sun arc — same vector the sky pass draws.
            const sm::SkyDir sunSd = sm::sun_dir(tod);
            sm::vec3 sunDir = sm::v3(sunSd.x, sunSd.y, sunSd.z);
            float dayI = sm::clamp01((sunSd.y + 0.10f) / 0.35f);
            float highness = sm::clamp01(sunSd.y / 0.5f);
            sm::vec3 warm = sm::v3(1.0f, 0.55f, 0.25f);
            sm::vec3 white = sm::v3(1.0f, 0.96f, 0.88f);
            sm::vec3 sunColor = (warm + (white - warm) * highness) * dayI;
            sm::vec3 nightAmb = sm::v3(0.10f, 0.13f, 0.22f);
            sm::vec3 dayAmb = sm::v3(0.35f, 0.35f, 0.38f);
            sm::vec3 ambient = nightAmb + (dayAmb - nightAmb) * dayI;
            // Lightning (GPU_SMOKE_STORM=1): same pure envelope the shipping
            // renderer uses — the whole world blinks through ambient.
            const float stormFlash = sm::sub::storm_flash01(
                static_cast<float>(frame) * 0.02f, optStorm);
            if (stormFlash > 0.0f) {
                ambient.x += 0.55f * stormFlash;
                ambient.y += 0.60f * stormFlash;
                ambient.z += 0.75f * stormFlash;
            }

            // GPU_SMOKE_LIGHT: one warm point light hovering over the NPC/tree
            // cluster centre (world ~origin, see the npc/tree scatter above),
            // written straight into the shared set-0 SSBO exactly as the shipping
            // renderer's gather_point_lights does. Billboards read it via
            // point_lights_flat(); terrain/structs via point_lights(). A slow
            // bob makes the pool obviously dynamic in an interactive run.
            {
                auto* lb = static_cast<sm::sub::GpuLightBuffer*>(lightBuf.mapped);
                // Cloud-shadow context lane (see sub/lighting.h GpuLightBuffer):
                // weather constants from THE source (build_sky_context), time
                // from the harness clock the sky pass also drifts on.
                {
                    const sm::sub::SkyContext wctx =
                        sm::sub::build_sky_context(sm::world_time_at(15, 0, 0));
                    lb->skyParams[0] = static_cast<float>(frame) * 0.02f;
                    lb->skyParams[1] = wctx.windX;
                    lb->skyParams[2] = wctx.windZ;
                    lb->skyParams[3] = wctx.cloudiness01;
                    // Terrain-occlusion lanes: real sun dir for hygiene, span
                    // 0 = march disabled (no heightfield in the toy world).
                    lb->sunDirW[0] = sunDir.x;
                    lb->sunDirW[1] = sunDir.y;
                    lb->sunDirW[2] = sunDir.z;
                    lb->sunDirW[3] = 0.0f;
                    lb->terrainParams[0] = 0.0f;
                    lb->terrainParams[1] = 0.0f;
                    lb->terrainParams[2] = 0.0f;
                    lb->terrainParams[3] = 0.0f;
                }
                if (optLight) {
                    const float bob = 0.9f + 0.15f * std::sin(t * 1.3f);
                    lb->count = 1;
                    if (optLightWater) {
                        // Hover a low torch just over the deepest water cell so
                        // its reflection streaks across the rippling surface.
                        lb->lights[0].pos[0] = deepX;
                        lb->lights[0].pos[1] = kHarnessWaterY + 0.6f
                                               + 0.1f * std::sin(t * 1.3f);
                        lb->lights[0].pos[2] = deepZ;
                    } else if (optNpcClose) {
                        // Sit the torch beside the body it must light, at chest
                        // height and one body-height away, so the crowd itself
                        // is what the pool falls on.
                        lb->lights[0].pos[0] = npcCenterX + npcSize;
                        lb->lights[0].pos[1] = npcCenterY + npcSize * 0.6f;
                        lb->lights[0].pos[2] = npcCenterZ;
                    } else {
                        lb->lights[0].pos[0] = 0.0f;   // x
                        lb->lights[0].pos[1] = bob;    // y (hover height)
                        lb->lights[0].pos[2] = 1.5f;   // z (cluster centre)
                    }
                    lb->lights[0].pos[3] = 6.0f;   // radius (m)
                    lb->lights[0].color[0] = 1.00f; // warm torch RGB
                    lb->lights[0].color[1] = 0.72f;
                    lb->lights[0].color[2] = 0.42f;
                    lb->lights[0].color[3] = 2.5f;  // intensity
                } else {
                    lb->count = 0;
                }
            }

            // Sun-view orthographic light matrix for the shadow map.
            sm::vec3 lightEye = center + sunDir * 20.0f;
            sm::mat4 lightView =
                sm::mat4_lookAt(lightEye, center, sm::v3(0.0f, 0.0f, 1.0f));
            sm::mat4 lightMvp = sm::mat4_mul(
                vk_ortho(-14.0f, 14.0f, -14.0f, 14.0f, 1.0f, 45.0f), lightView);
            // Wide-level lane = the same matrix (one map bound to both shadow
            // slots, so the handoff reduces to the single-map result — parity).
            {
                auto* lbm = static_cast<sm::sub::GpuLightBuffer*>(lightBuf.mapped);
                std::memcpy(lbm->lightMvpFar, lightMvp.m, sizeof(lbm->lightMvpFar));
            }

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
                    const VkDescriptorSet sdolls = npcDolls.descriptor_set();
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
                // Celestial context, mirroring sub/sky.h's build_sky_context:
                // the same procedural orbits the shipping renderer feeds the
                // shader (shared-shader contract — the harness must exercise
                // the real push layout). Day pinned to the Pale moon's FULL
                // night (day 15; the Crimson moon is gibbous then) so
                // GPU_SMOKE_NIGHT frames show both discs and the moon bloom.
                {
                    const int skyDay = 15;
                    const sm::SkyDir sd = sm::sun_dir(tod);
                    sky.sun[0] = sd.x; sky.sun[1] = sd.y; sky.sun[2] = sd.z;
                    sky.forward[3] = static_cast<float>(int(sm::MoonId::Count));
                    sky.right[3] = sm::kSkyStarSizeScale;
                    for (int mi = 0; mi < int(sm::MoonId::Count); ++mi) {
                        const sm::MoonId m = sm::MoonId(mi);
                        const sm::SkyDir md = sm::moon_dir(m, skyDay, tod);
                        sky.moonDirSize[mi][0] = md.x;
                        sky.moonDirSize[mi][1] = md.y;
                        sky.moonDirSize[mi][2] = md.z;
                        sky.moonDirSize[mi][3] = sm::moon_def(m).baseSize;
                        float rgb[3];
                        sm::moon_color_rgb(m, rgb);
                        sky.moonColIllum[mi][0] = rgb[0];
                        sky.moonColIllum[mi][1] = rgb[1];
                        sky.moonColIllum[mi][2] = rgb[2];
                        sky.moonColIllum[mi][3] = sm::moon_illumination01f(
                            m, float(skyDay) + tod);
                    }
                    const sm::sub::SkyContext wctx =
                        sm::sub::build_sky_context(sm::world_time_at(15, 0, 0));
                    sky.p2[0] = wctx.cloudiness01;
                    sky.p2[1] = wctx.windX;
                    sky.p2[2] = wctx.windZ;
                    sky.p2[3] = wctx.precip01;
                    sky.p3[0] = wctx.seasonTint[0];
                    sky.p3[1] = wctx.seasonTint[1];
                    sky.p3[2] = wctx.seasonTint[2];
                    sky.p3[3] = stormFlash;
                }
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  skyPipeline.pipeline);
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        skyPipeline.layout, 0, 1, &skySet,
                                        0, nullptr);
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
                const VkDescriptorSet ndolls = npcDolls.descriptor_set();
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

            // ---- FX additive particles (GPU_SMOKE_FX) — after opaque, before
            //      water, exactly as the shipping renderer. Emissive: binds NO
            //      descriptor set, depth-test on / write off, additive blend so
            //      overlapping cards accumulate into a glow with no sort. ----
            if (particleCount > 0) {
                ParticlePush pp{};
                std::memcpy(pp.mvp, mvp.m, sizeof(pp.mvp));
                sm::vec3 cr = sm::normalize(
                    sm::v3(view.m[0], view.m[4], view.m[8]));
                sm::vec3 cu = sm::normalize(
                    sm::v3(view.m[1], view.m[5], view.m[9]));
                pp.camRight[0] = cr.x; pp.camRight[1] = cr.y; pp.camRight[2] = cr.z;
                pp.camUp[0] = cu.x;    pp.camUp[1] = cu.y;    pp.camUp[2] = cu.z;
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  particlePipeline.pipeline);
                VkDeviceSize po = 0;
                vkCmdBindVertexBuffers(c, 0, 1, &particleBuf.buffer, &po);
                vkCmdPushConstants(c, particlePipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pp), &pp);
                vkCmdDraw(c, 6, particleCount, 0, 0);
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
                vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        waterPipeline.layout, 0, 1, &shadowSet, 0,
                                        nullptr);
                vkCmdPushConstants(c, waterPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(wp), &wp);
                vkCmdDraw(c, 6, 1, 0, 0);
            }

            // ---- Precipitation (fullscreen overlay, LAST; GPU_SMOKE_PRECIP
            // stages it — default 0 keeps the frame byte-identical to a build
            // without this pass, the module-isolation control). ----
            if (optPrecip > 0.001f) {
                PrecipPush prp{};
                prp.p0[0] = static_cast<float>(ext.width);
                prp.p0[1] = static_cast<float>(ext.height);
                prp.p0[2] = static_cast<float>(frame) * 0.02f;
                prp.p0[3] = optPrecip;
                prp.p1[0] = static_cast<float>(optPrecipKind);
                prp.p1[1] = 0.12f;   // a mild fixed wind tilt for captures
                prp.p1[2] = stormFlash;
                vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  precipPipeline.pipeline);
                vkCmdPushConstants(c, precipPipeline.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT
                                       | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(prp), &prp);
                vkCmdDraw(c, 3, 1, 0, 0);
            }

            // GPU_SMOKE_SHOT: on the target frame, copy the rendered image into
            // the renderer's host-visible capture buffer (armed BEFORE present,
            // drained after) and write it to PPM, then end the run. The shipping
            // window can stall in nextDrawable when head-less; this smoke loop
            // presents reliably, so it is the dependable path to a LOOK-able frame.
            const bool doShot = optShot && frame == shotFrame;
            if (doShot) renderer.request_capture();
            if (!renderer.end_frame(win)) running = false;
            if (doShot) {
                std::vector<std::uint8_t> px;
                int cw = 0, ch = 0;
                VkFormat cfmt = VK_FORMAT_UNDEFINED;
                if (renderer.take_capture(px, cw, ch, cfmt)) {
                    const bool bgra = (cfmt == VK_FORMAT_B8G8R8A8_UNORM
                                       || cfmt == VK_FORMAT_B8G8R8A8_SRGB);
                    if (write_ppm_bgra(optShot, px.data(), cw, ch, bgra))
                        std::fprintf(stderr,
                                     "[gpu_smoke3d] shot wrote %s (%dx%d fmt=%d)\n",
                                     optShot, cw, ch, int(cfmt));
                    else
                        std::fprintf(stderr, "[gpu_smoke3d] shot WRITE FAILED\n");
                } else {
                    std::fprintf(stderr, "[gpu_smoke3d] shot: no capture (swapchain "
                                         "lacks TRANSFER_SRC?)\n");
                }
                std::fflush(stderr);
                running = false;
            }
        }

        ++frame;
        if (frameCap > 0 && frame >= frameCap) running = false;
    }

    vkDeviceWaitIdle(dev.device);
    particlePipeline.destroy(dev); // no-op when GPU_SMOKE_FX was off (null handle)
    particleBuf.destroy(dev);
    npcShadowPipeline.destroy(dev);
    npcPipeline.destroy(dev);
    npcDolls.destroy(dev);
    structShadowPipeline.destroy(dev);
    structPipeline.destroy(dev);
    waterPipeline.destroy(dev);
    shadowBbPipeline.destroy(dev);
    shadowMeshPipeline.destroy(dev);
    lightBuf.destroy(dev);
    dummyHeight.destroy(dev);
    vkDestroyDescriptorPool(dev.device, shadowPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, shadowSetLayout, nullptr);
    vkDestroyDescriptorPool(dev.device, materialPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, materialSetLayout, nullptr);
    skyStarsBuf.destroy(dev);
    vkDestroyDescriptorPool(dev.device, skyPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, skySetLayout, nullptr);
    materialTex.destroy(dev);
    shadowMap.destroy(dev);
    skyPipeline.destroy(dev);
    precipPipeline.destroy(dev);
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
