// Vulkan Phase 4b smoke: a CPU-generated macroworld master texture (height /
// moisture / temperature) is uploaded as a sampled image and coloured by biome
// in a Vulkan fragment shader (descriptor set 0 = combined image sampler). This
// is the real macro data pipeline, minus main.cpp wiring. Dear ImGui overlays
// on top. MoltenVK ICD auto-detected on macOS. GPU_SMOKE_FRAMES=N auto-exits.
#include "gpu/vk_device.h"
#include "gpu/vk_pipeline.h"
#include "gpu/vk_renderer.h"
#include "gpu/vk_texture.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
    // MUST mirror shaders/macro.frag's push_constant Push block byte for byte
    // (shared-shader contract: this harness compiles the SHIPPING shader). The
    // old 32-byte layout drifted when the shader grew cam/viewSize/zoom and
    // the night lanes — the pipeline then failed to build and the smoke could
    // not validate anything.
    struct Push
    {
        float resolution[2];
        float mapSize[2];
        float cam[2];       // world-pixel offset at screen centre
        float viewSize[2];  // viewport size in pixels
        float zoom;         // pixels per world cell
        float seaLevel;
        float seed;
        float timeOfDay;    // 0..1
        float nightDarken;  // 0..1
        float elapsed;      // real seconds
    };
    static_assert(sizeof(Push) == 56,
                  "macro.frag push block is 56 bytes — keep this in lockstep");

    // A compact toroidal value-noise world: RGBA8 = height, moisture,
    // temperature, mask. Same spirit as the game's master texture (a real,
    // varied world) without pulling in the GL generator.
    std::vector<std::uint8_t> gen_master(std::uint32_t w, std::uint32_t h,
                                         std::uint32_t seed)
    {
        auto vhash = [](int x, int y, std::uint32_t s) -> float {
            std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u
                              + static_cast<std::uint32_t>(y) * 668265263u
                              + s * 362437u;
            n = (n ^ (n >> 13)) * 1274126177u;
            return static_cast<float>((n ^ (n >> 16)) & 0xffffffu)
                   / static_cast<float>(0xffffff);
        };
        auto vnoise = [&](float fx, float fy, std::uint32_t s) -> float {
            int x0 = static_cast<int>(std::floor(fx));
            int y0 = static_cast<int>(std::floor(fy));
            float tx = fx - x0, ty = fy - y0;
            tx = tx * tx * (3.0f - 2.0f * tx);
            ty = ty * ty * (3.0f - 2.0f * ty);
            float a = vhash(x0, y0, s), b = vhash(x0 + 1, y0, s);
            float c = vhash(x0, y0 + 1, s), d = vhash(x0 + 1, y0 + 1, s);
            return (a * (1 - tx) + b * tx) * (1 - ty)
                   + (c * (1 - tx) + d * tx) * ty;
        };
        auto fbm = [&](float fx, float fy, int oct, std::uint32_t s) -> float {
            float sum = 0, amp = 0.5f, freq = 1.0f, norm = 0;
            for (int i = 0; i < oct; ++i) {
                sum += amp * vnoise(fx * freq, fy * freq, s + i * 101u);
                norm += amp;
                amp *= 0.5f;
                freq *= 2.0f;
            }
            return norm > 0 ? sum / norm : 0.0f;
        };
        auto clamp01 = [](float v) {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };

        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
        const float scale = 6.0f;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                float u = static_cast<float>(x) / w;
                float v = static_cast<float>(y) / h;
                float height = fbm(u * scale, v * scale, 6, seed);
                float moist = fbm(u * scale + 13.0f, v * scale + 7.0f, 4,
                                  seed + 900u);
                float temp = fbm(u * scale + 31.0f, v * scale + 19.0f, 3,
                                 seed + 1700u);
                // latitude gradient: cold at the poles, warm at the equator.
                float lat = 1.0f - std::fabs(v - 0.5f) * 2.0f;
                temp = temp * 0.4f + lat * 0.6f;
                std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                px[i + 0] = static_cast<std::uint8_t>(clamp01(height) * 255.0f);
                px[i + 1] = static_cast<std::uint8_t>(clamp01(moist) * 255.0f);
                px[i + 2] = static_cast<std::uint8_t>(clamp01(temp) * 255.0f);
                px[i + 3] = 255;
            }
        }
        return px;
    }

    // Smooth value-noise in [0,1] for the stand-in data maps.
    float world_noise(float fx, float fy, std::uint32_t s)
    {
        auto hsh = [](int X, int Y, std::uint32_t S) -> float {
            std::uint32_t n = static_cast<std::uint32_t>(X) * 374761393u
                              + static_cast<std::uint32_t>(Y) * 668265263u
                              + S * 362437u;
            n = (n ^ (n >> 13)) * 1274126177u;
            return static_cast<float>((n ^ (n >> 16)) & 0xffffffu)
                   / static_cast<float>(0xffffff);
        };
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0, ty = fy - y0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        float a = hsh(x0, y0, s), b = hsh(x0 + 1, y0, s);
        float c = hsh(x0, y0 + 1, s), d = hsh(x0 + 1, y0 + 1, s);
        return (a * (1 - tx) + b * tx) * (1 - ty)
               + (c * (1 - tx) + d * tx) * ty;
    }

    // FeatureType byte map (1=road, 2=tree, 3=mountain) packed in R.
    std::vector<std::uint8_t> gen_feature(const std::vector<std::uint8_t>& m,
                                          std::uint32_t w, std::uint32_t h,
                                          float sea)
    {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                px[i + 3] = 255;
                float H = m[i] / 255.0f;
                if (H < sea) continue;
                if (H > 0.80f) { px[i] = 3; continue; }
                float moist = m[i + 1] / 255.0f;
                float td = world_noise(x * 0.06f + 11.0f, y * 0.06f + 3.0f, 7u);
                if (moist > 0.45f && H > 0.42f && H < 0.74f && td > 0.55f)
                    px[i] = 2;
            }
        }
        auto stampRoad = [&](int cx, int cy) {
            cx = ((cx % int(w)) + int(w)) % int(w);
            cy = ((cy % int(h)) + int(h)) % int(h);
            std::size_t i = (static_cast<std::size_t>(cy) * w + cx) * 4;
            if (m[i] / 255.0f >= sea) px[i] = 1;
        };
        int prevY = -1;
        for (std::uint32_t x = 0; x < w; ++x) {
            float yy = h * 0.42f + std::sin(x * 0.05f) * h * 0.10f
                       + std::sin(x * 0.017f) * h * 0.06f;
            int yc = static_cast<int>(yy);
            if (prevY >= 0) {
                int lo = std::min(prevY, yc), hi = std::max(prevY, yc);
                for (int yv = lo; yv <= hi; ++yv) stampRoad(int(x), yv);
            }
            stampRoad(int(x), yc);
            prevY = yc;
        }
        int prevX = -1;
        for (std::uint32_t y = 0; y < h; ++y) {
            float xx = w * 0.58f + std::sin(y * 0.045f) * w * 0.09f;
            int xc = static_cast<int>(xx);
            if (prevX >= 0) {
                int lo = std::min(prevX, xc), hi = std::max(prevX, xc);
                for (int xv = lo; xv <= hi; ++xv) stampRoad(xv, int(y));
            }
            stampRoad(xc, int(y));
            prevX = xc;
        }
        return px;
    }

    // Danger-zone byte map (0..9) packed in R; high on mountains.
    std::vector<std::uint8_t> gen_zone(const std::vector<std::uint8_t>& m,
                                       std::uint32_t w, std::uint32_t h,
                                       float sea)
    {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                px[i + 3] = 255;
                float H = m[i] / 255.0f;
                if (H < sea) continue;
                float z = H * 8.5f
                          + world_noise(x * 0.03f + 5.0f, y * 0.03f + 9.0f, 22u) * 2.5f
                          - 1.2f;
                z = z < 0.0f ? 0.0f : (z > 9.0f ? 9.0f : z);
                px[i] = static_cast<std::uint8_t>(z);
            }
        }
        return px;
    }

} // namespace

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "timaert gpu_smoke (Vulkan macro biome synth)", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1024, 640,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    gpu::VulkanDevice dev;
    if (!dev.init(win, /*enableValidation=*/true)) {
        std::fprintf(stderr, "[gpu_smoke] device init FAILED\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }

    gpu::VulkanRenderer renderer;
    if (!renderer.init(dev, win)) {
        std::fprintf(stderr, "[gpu_smoke] renderer init FAILED\n");
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 3;
    }

    // World data textures: master (climate) + feature/zone R8 byte maps
    // packed in the R channel. feature/zone sampled nearest, master linear.
    // This is the full data pipeline the macro fragment synth consumes.
    constexpr std::uint32_t kWorld = 256;
    constexpr float kSea = 0.40f;
    constexpr std::uint32_t kSeed = 1337u;
    // macro.frag samples FIVE maps (set 0, bindings 0-4): master, feature,
    // zone, light field, tree map (the dead u_riverMap binding is gone —
    // rivers are honest water cells). The harness feeds all five or the
    // pipeline does not build; light field + tree map are zeroed (no night
    // glow, no forests) — inert, honest placeholders.
    constexpr std::uint32_t kMacroMaps = 5;
    gpu::VulkanTexture master, featureTex, zoneTex, lightTex, treeTex;
    {
        std::vector<std::uint8_t> mp = gen_master(kWorld, kWorld, kSeed);
        std::vector<std::uint8_t> fp = gen_feature(mp, kWorld, kWorld, kSea);
        std::vector<std::uint8_t> zp = gen_zone(mp, kWorld, kWorld, kSea);
        std::vector<std::uint8_t> zeros(std::size_t(kWorld) * kWorld * 4u, 0u);
        bool ok = master.create_rgba8(dev, kWorld, kWorld, mp.data(), true, true)
               && featureTex.create_rgba8(dev, kWorld, kWorld, fp.data(), false, true)
               && zoneTex.create_rgba8(dev, kWorld, kWorld, zp.data(), false, true)
               && lightTex.create_rgba8(dev, kWorld, kWorld, zeros.data(), true, true)
               && treeTex.create_r8(dev, kWorld, kWorld, zeros.data(), true, true);
        if (!ok) {
            std::fprintf(stderr, "[gpu_smoke] world textures FAILED\n");
            treeTex.destroy(dev); lightTex.destroy(dev);
            zoneTex.destroy(dev);
            featureTex.destroy(dev); master.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 4;
        }
    }

    // Descriptor set 0 = the five world maps, bound once (static for the world).
    VkDescriptorSetLayout macroSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool macroPool = VK_NULL_HANDLE;
    VkDescriptorSet macroSet = VK_NULL_HANDLE;
    {
        VkDescriptorSetLayoutBinding bindings[kMacroMaps]{};
        for (std::uint32_t i = 0; i < kMacroMaps; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = kMacroMaps;
        dlci.pBindings = bindings;
        vkCreateDescriptorSetLayout(dev.device, &dlci, nullptr, &macroSetLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                kMacroMaps};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        vkCreateDescriptorPool(dev.device, &dpci, nullptr, &macroPool);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = macroPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &macroSetLayout;
        vkAllocateDescriptorSets(dev.device, &dsai, &macroSet);

        const gpu::VulkanTexture* texes[kMacroMaps] = {
            &master, &featureTex, &zoneTex, &lightTex, &treeTex};
        VkDescriptorImageInfo dii[kMacroMaps]{};
        VkWriteDescriptorSet writes[kMacroMaps]{};
        for (std::uint32_t i = 0; i < kMacroMaps; ++i) {
            dii[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dii[i].imageView = texes[i]->view;
            dii[i].sampler = texes[i]->sampler;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = macroSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &dii[i];
        }
        vkUpdateDescriptorSets(dev.device, kMacroMaps, writes, 0, nullptr);
    }

    // Biome synth pipeline (glslc-compiled SPIR-V from <exe dir>/shaders/).
    gpu::VulkanPipeline pipeline;
    {
        char* base = SDL_GetBasePath();
        char vpath[1024], fpath[1024];
        std::snprintf(vpath, sizeof vpath, "%sshaders/fullscreen.vert.spv",
                      base ? base : "./");
        std::snprintf(fpath, sizeof fpath, "%sshaders/macro.frag.spv",
                      base ? base : "./");
        if (base) SDL_free(base);
        if (!pipeline.create(dev, renderer.renderPass, vpath, fpath,
                             sizeof(Push), macroSetLayout)) {
            std::fprintf(stderr, "[gpu_smoke] pipeline FAILED\n");
            vkDestroyDescriptorPool(dev.device, macroPool, nullptr);
            vkDestroyDescriptorSetLayout(dev.device, macroSetLayout, nullptr);
                    zoneTex.destroy(dev);
            featureTex.destroy(dev);
            master.destroy(dev);
            renderer.destroy();
            dev.destroy();
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 5;
        }
    }

    // ImGui descriptor pool.
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}};
        VkDescriptorPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = 16;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = sizes;
        vkCreateDescriptorPool(dev.device, &pci, nullptr, &imguiPool);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(win);

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = dev.instance;
    info.PhysicalDevice = dev.physical;
    info.Device = dev.device;
    info.QueueFamily = dev.families.graphics;
    info.Queue = dev.graphicsQueue;
    info.DescriptorPool = imguiPool;
    info.RenderPass = renderer.renderPass;
    info.MinImageCount = 2;
    info.ImageCount =
        static_cast<std::uint32_t>(renderer.swapchain.images.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&info);

    // Bounded by default so a bare headless/agent invocation self-terminates (same
    // unbounded-loop hazard as gpu_smoke3d — no SDL_QUIT/ESC arrives in a headless
    // run). Override with GPU_SMOKE_FRAMES=N; GPU_SMOKE_FRAMES=0 restores unbounded.
    int frameCap = 600;
    if (const char* e = SDL_getenv("GPU_SMOKE_FRAMES")) frameCap = std::atoi(e);

    bool running = true;
    bool showDemo = false;
    int frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
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

        if (renderer.begin_frame(win, 0.0f, 0.0f, 0.0f)) {
            VkCommandBuffer c = renderer.current_command_buffer();
            const VkExtent2D ext = renderer.swapchain.extent;

            VkViewport vpt{};
            vpt.width = static_cast<float>(ext.width);
            vpt.height = static_cast<float>(ext.height);
            vpt.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.extent = ext;
            vkCmdSetViewport(c, 0, 1, &vpt);
            vkCmdSetScissor(c, 0, 1, &scissor);

            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline.pipeline);
            vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline.layout, 0, 1, &macroSet, 0, nullptr);
            Push pc{};
            pc.resolution[0] = static_cast<float>(ext.width);
            pc.resolution[1] = static_cast<float>(ext.height);
            pc.mapSize[0] = static_cast<float>(kWorld);
            pc.mapSize[1] = static_cast<float>(kWorld);
            // Camera centred on the world, ~48 cells across the viewport —
            // the same framing the old viewCells lane asked for, expressed in
            // the shader's current cam/viewSize/zoom vocabulary.
            pc.zoom = static_cast<float>(ext.width) / 48.0f;
            pc.cam[0] = static_cast<float>(kWorld) * 0.5f * pc.zoom;
            pc.cam[1] = static_cast<float>(kWorld) * 0.5f * pc.zoom;
            pc.viewSize[0] = static_cast<float>(ext.width);
            pc.viewSize[1] = static_cast<float>(ext.height);
            pc.seaLevel = kSea;
            pc.seed = static_cast<float>(kSeed);
            pc.timeOfDay = 0.5f;   // noon — full daylight, night lanes inert
            pc.nightDarken = 0.0f;
            pc.elapsed = static_cast<float>(frame) * 0.05f;
            vkCmdPushConstants(c, pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            vkCmdDraw(c, 3, 1, 0, 0);

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::Begin("Timaert GPU (Vulkan)");
            ImGui::Text("Backend: Vulkan / MoltenVK");
            ImGui::Text("GPU: %s", dev.props.deviceName);
            ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate,
                        1000.0f / ImGui::GetIO().Framerate);
            ImGui::Text("Macro synth: master+feature+zone+river+light+tree "
                        "(6 images)");
            ImGui::Checkbox("ImGui demo window", &showDemo);
            ImGui::End();
            if (showDemo) ImGui::ShowDemoWindow(&showDemo);
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), c);

            if (!renderer.end_frame(win)) running = false;
        }

        ++frame;
        if (frameCap > 0 && frame >= frameCap) running = false;
    }

    vkDeviceWaitIdle(dev.device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(dev.device, imguiPool, nullptr);
    pipeline.destroy(dev);
    vkDestroyDescriptorPool(dev.device, macroPool, nullptr);
    vkDestroyDescriptorSetLayout(dev.device, macroSetLayout, nullptr);
    treeTex.destroy(dev);
    lightTex.destroy(dev);
    zoneTex.destroy(dev);
    featureTex.destroy(dev);
    master.destroy(dev);
    renderer.destroy();
    dev.destroy();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::fprintf(stderr, "[gpu_smoke] macro biome synth loop OK (%d frames)\n",
                 frame);
    return 0;
}
