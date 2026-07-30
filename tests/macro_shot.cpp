// Offscreen macro-map screenshot tool.
//
// Renders the SHIPPING macro fragment synth (shaders/macro.frag, driven by the
// production MacroRendererVk) into an offscreen RGBA8 colour image, reads it
// back, and writes PPM frames -- no swapchain, no visible window. It is a
// headless visual-regression capture for the 2D world map: generate a real
// climate master on the CPU (map_generator); mountains are an elevation-
// classified biome (land && height >= kMountainBiomeLevel), derived in the
// shader straight from the height master exactly as the game does. Auto-frame
// the densest mountain block and dump day + night frames so the relief render
// can be inspected without a live game session.
//
// PPM (binary P6) keeps the tool dependency-free; convert with ffmpeg if a PNG
// is wanted. Usage: macro_shot [out_dir]   (default out dir: /tmp/macro_shots)
// Env: MACRO_SHOT_SEED (float, default 1), MACRO_SHOT_MAP (cells, default 1024),
//      MACRO_SHOT_RESW / MACRO_SHOT_RESH (pixels, default 1600x1000).

#include "gpu/vk_device.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "macro/vk_macro_renderer.h"
#include "macro/zones.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

std::uint32_t find_memory_type(VkPhysicalDevice pd, std::uint32_t bits,
                               VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

float env_float(const char* name, float fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    return float(std::atof(v));
}
int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    return std::atoi(v);
}

bool write_ppm(const char* path, const std::uint8_t* rgba, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[macro_shot] cannot open %s\n", path);
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<std::uint8_t> rgb(std::size_t(w) * std::size_t(h) * 3u);
    for (std::size_t i = 0, n = std::size_t(w) * std::size_t(h); i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    const std::size_t wrote = std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    return wrote == rgb.size();
}

// Per-block mountain statistics used for framing. Mountains are an elevation-
// classified biome (not a feature), so a cell is "rock" when it is land and its
// height clears kMountainBiomeLevel -- read straight from the terrain master,
// exactly as the shader classifies the Mountain biome.
struct BlockStat {
    long count = 0;       // mountain-biome cells in the block
    float meanTemp = 0.f; // mean temperature (0..1) over those cells
    float meanH = 0.f;    // mean height (0..1) over those cells
};
BlockStat block_stat(const sm::TerrainData& td,
                     int mapW, int bx, int by, int B) {
    BlockStat s;
    double t = 0.0, h = 0.0;
    for (int y = by; y < by + B; ++y) {
        const std::size_t row = std::size_t(y) * std::size_t(mapW);
        for (int x = bx; x < bx + B; ++x) {
            const std::size_t i = row + std::size_t(x);
            const bool land = td.rgba[i * 4 + 3] >= 128;
            if (!land || float(td.rgba[i * 4 + 0]) / 255.0f
                             < sm::kMountainBiomeLevel) continue;
            ++s.count;
            h += double(td.rgba[i * 4 + 0]) / 255.0;
            t += double(td.rgba[i * 4 + 2]) / 255.0;
        }
    }
    if (s.count > 0) {
        s.meanTemp = float(t / double(s.count));
        s.meanH = float(h / double(s.count));
    }
    return s;
}

// Centre of the densest B×B block of mountain-biome cells (falls back to map
// centre when the world has no mountains). Guarantees a shot is framed on rock.
void densest_mountain_center(const sm::TerrainData& td,
                             int mapW, int mapH, int B, float& cx, float& cy,
                             BlockStat& stat) {
    cx = float(mapW) * 0.5f;
    cy = float(mapH) * 0.5f;
    stat = BlockStat{};
    if (!td.has_rgba_storage() || mapW <= 0 || mapH <= 0) return;
    for (int by = 0; by + B <= mapH; by += B) {
        for (int bx = 0; bx + B <= mapW; bx += B) {
            const BlockStat s = block_stat(td, mapW, bx, by, B);
            if (s.count > stat.count) {
                stat = s;
                cx = float(bx) + float(B) * 0.5f;
                cy = float(by) + float(B) * 0.5f;
            }
        }
    }
}

// Centre of the COLDEST well-populated mountain block -- the frame that must
// show snow if the temperature+height snow contract holds. Requires a minimum
// mountain density so we frame a real massif, not a lone cold peak.
void coldest_mountain_center(const sm::TerrainData& td,
                             int mapW, int mapH, int B, float& cx, float& cy,
                             BlockStat& stat) {
    cx = float(mapW) * 0.5f;
    cy = float(mapH) * 0.5f;
    stat = BlockStat{};
    if (!td.has_rgba_storage() || mapW <= 0 || mapH <= 0) return;
    const long minCount = long(B) * long(B) / 4; // >=25% of the block is rock
    float coldest = 2.0f;
    for (int by = 0; by + B <= mapH; by += B) {
        for (int bx = 0; bx + B <= mapW; bx += B) {
            const BlockStat s = block_stat(td, mapW, bx, by, B);
            if (s.count < minCount) continue;
            if (s.meanTemp < coldest) {
                coldest = s.meanTemp;
                stat = s;
                cx = float(bx) + float(B) * 0.5f;
                cy = float(by) + float(B) * 0.5f;
            }
        }
    }
    // Fall back to the densest block if nothing met the density floor.
    if (stat.count == 0)
        densest_mountain_center(td, mapW, mapH, B, cx, cy, stat);
}

// Centre of the B×B block with the most river cells -- frames a river + its
// banks so the river rendering (now Biome::Water, formerly the buggy overlay)
// can be judged. River cells are CARVED below sea level in generation (that is
// what makes them honest water), so we frame by river-mask presence, not by
// height -- the old "> seaByte" filter matched nothing post-carve.
long densest_river_center(const sm::TerrainData& td, int mapW, int mapH, int B,
                          float seaLevel, float& cx, float& cy) {
    cx = float(mapW) * 0.5f;
    cy = float(mapH) * 0.5f;
    if (!td.has_river_storage() || mapW <= 0 || mapH <= 0) return 0;
    (void)seaLevel;
    long best = 0;
    for (int by = 0; by + B <= mapH; by += B) {
        for (int bx = 0; bx + B <= mapW; bx += B) {
            long n = 0;
            for (int y = by; y < by + B; ++y) {
                const std::size_t row = std::size_t(y) * std::size_t(mapW);
                for (int x = bx; x < bx + B; ++x) {
                    const std::size_t i = row + std::size_t(x);
                    if (td.riverData[i] > 0)
                        ++n;
                }
            }
            if (n > best) {
                best = n;
                cx = float(bx) + float(B) * 0.5f;
                cy = float(by) + float(B) * 0.5f;
            }
        }
    }
    return best;
}

struct Offscreen {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::uint32_t w = 0, h = 0;

    bool create(const gpu::VulkanDevice& dev, std::uint32_t width,
                std::uint32_t height) {
        w = width;
        h = height;
        const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = fmt;
        ici.extent = {w, h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev.device, &ici, nullptr, &image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(dev.device, image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = find_memory_type(dev.physical, mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        if (vkAllocateMemory(dev.device, &ai, nullptr, &memory) != VK_SUCCESS)
            return false;
        vkBindImageMemory(dev.device, image, memory, 0);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(dev.device, &vci, nullptr, &view) != VK_SUCCESS)
            return false;

        // One colour attachment: clear -> store, leaving the image ready to be
        // copied out (final layout TRANSFER_SRC_OPTIMAL).
        VkAttachmentDescription att{};
        att.format = fmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        // External deps: acquire before the colour write; release the store to
        // the transfer (readback) stage.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        rpci.dependencyCount = 2;
        rpci.pDependencies = deps;
        if (vkCreateRenderPass(dev.device, &rpci, nullptr, &pass) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &view;
        fci.width = w;
        fci.height = h;
        fci.layers = 1;
        if (vkCreateFramebuffer(dev.device, &fci, nullptr, &fb) != VK_SUCCESS)
            return false;

        // Host-visible readback staging (image -> buffer -> host).
        const VkDeviceSize size = VkDeviceSize(w) * h * 4u;
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev.device, &bci, nullptr, &staging) != VK_SUCCESS)
            return false;
        VkMemoryRequirements bmr{};
        vkGetBufferMemoryRequirements(dev.device, staging, &bmr);
        VkMemoryAllocateInfo bai{};
        bai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bai.allocationSize = bmr.size;
        bai.memoryTypeIndex =
            find_memory_type(dev.physical, bmr.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (bai.memoryTypeIndex == UINT32_MAX) return false;
        if (vkAllocateMemory(dev.device, &bai, nullptr, &stagingMem) != VK_SUCCESS)
            return false;
        vkBindBufferMemory(dev.device, staging, stagingMem, 0);

        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = dev.families.graphics;
        if (vkCreateCommandPool(dev.device, &pci, nullptr, &pool) != VK_SUCCESS)
            return false;
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(dev.device, &cai, &cmd) != VK_SUCCESS)
            return false;

        VkFenceCreateInfo fenceci{};
        fenceci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(dev.device, &fenceci, nullptr, &fence) != VK_SUCCESS)
            return false;
        return true;
    }

    // Render one frame and read it back into `out` (w*h*4 RGBA bytes).
    bool shoot(const gpu::VulkanDevice& dev, sm::MacroRendererVk& mr,
               const sm::TerrainData& td, float camX, float camY, float zoom,
               float seaLevel, float tod, std::vector<std::uint8_t>& out) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = pass;
        rp.framebuffer = fb;
        rp.renderArea.extent = {w, h};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        mr.record(cmd, {w, h}, td, camX, camY, zoom, seaLevel, tod,
                  /*elapsed*/ 0.0f);
        vkCmdEndRenderPass(cmd);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &region);
        vkEndCommandBuffer(cmd);

        vkResetFences(dev.device, 1, &fence);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        if (vkQueueSubmit(dev.graphicsQueue, 1, &si, fence) != VK_SUCCESS)
            return false;
        vkWaitForFences(dev.device, 1, &fence, VK_TRUE, UINT64_MAX);

        const VkDeviceSize size = VkDeviceSize(w) * h * 4u;
        out.resize(std::size_t(size));
        void* mapped = nullptr;
        vkMapMemory(dev.device, stagingMem, 0, size, 0, &mapped);
        std::memcpy(out.data(), mapped, std::size_t(size));
        vkUnmapMemory(dev.device, stagingMem);
        return true;
    }

    void destroy(const gpu::VulkanDevice& dev) {
        if (fence) vkDestroyFence(dev.device, fence, nullptr);
        if (pool) vkDestroyCommandPool(dev.device, pool, nullptr);
        if (staging) vkDestroyBuffer(dev.device, staging, nullptr);
        if (stagingMem) vkFreeMemory(dev.device, stagingMem, nullptr);
        if (fb) vkDestroyFramebuffer(dev.device, fb, nullptr);
        if (pass) vkDestroyRenderPass(dev.device, pass, nullptr);
        if (view) vkDestroyImageView(dev.device, view, nullptr);
        if (image) vkDestroyImage(dev.device, image, nullptr);
        if (memory) vkFreeMemory(dev.device, memory, nullptr);
    }
};

enum FrameKind { FRAME_DENSE, FRAME_COLD, FRAME_RIVER };
struct Shot {
    const char* name;
    float tod;         // time of day (0.5 = full daylight, 0.0 = deep night)
    float cellsAcross; // world cells spanning the frame width (zoom = resW / this)
    FrameKind frame;   // which auto-framed subject the camera centres on
};

} // namespace

int main(int argc, char** argv) {
    const char* outDir = (argc > 1) ? argv[1] : "/tmp/macro_shots";
    const float seed = env_float("MACRO_SHOT_SEED", 1.0f);
    const int mapN = env_int("MACRO_SHOT_MAP", 1024);
    const std::uint32_t resW = std::uint32_t(env_int("MACRO_SHOT_RESW", 1600));
    const std::uint32_t resH = std::uint32_t(env_int("MACRO_SHOT_RESH", 1000));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    // Hidden window: we need a Vulkan-capable surface for device bring-up, but
    // never present -- all rendering goes to the offscreen image.
    SDL_Window* win = SDL_CreateWindow("timaert macro_shot", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 64, 64,
                                       SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    gpu::VulkanDevice dev;
    if (!dev.init(win, /*enableValidation=*/true)) {
        std::fprintf(stderr, "[macro_shot] device init FAILED\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }

    // Real world: CPU climate master. Mountains are the elevation-classified
    // Mountain biome (land && height >= kMountainBiomeLevel), derived in the
    // shader from the height master -- the feature layer carries only overlays.
    sm::LayerParameters lp;
    lp.seed = seed;
    sm::TerrainData td = sm::generate_terrain(mapN, mapN, lp);
    if (!td.has_rgba_storage()) {
        std::fprintf(stderr, "[macro_shot] terrain generation FAILED\n");
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 3;
    }
    sm::FeatureLayer features;
    features.resize(td.width, td.height);
    {
        // Mountains are the elevation-classified Mountain biome now: the shader
        // derives them straight from the height master, NOT from the feature
        // layer. Count them here only for the framing log; the feature layer
        // carries just the composable overlays (trees + roads) added below.
        const std::size_t total = features.cell_count();
        long mtn = 0;
        for (std::size_t i = 0; i < total; ++i) {
            const bool land = td.rgba[i * 4 + 3] >= 128 &&
                              float(td.rgba[i * 4 + 0]) / 255.0f >= lp.seaLevel;
            if (land && float(td.rgba[i * 4 + 0]) / 255.0f
                            >= sm::kMountainBiomeLevel) {
                ++mtn;
            }
        }
        std::fprintf(stderr, "[macro_shot] map %dx%d seed %.1f -> %ld mountain cells\n",
                     td.width, td.height, double(seed), mtn);
    }
    sm::ZoneLayer zones; // empty -> renderer binds a 1x1 blank zone map

    const int kFrameBlock = 32;
    float denseX = 0.f, denseY = 0.f, coldX = 0.f, coldY = 0.f;
    BlockStat denseStat, coldStat;
    densest_mountain_center(td, td.width, td.height, kFrameBlock,
                            denseX, denseY, denseStat);
    coldest_mountain_center(td, td.width, td.height, kFrameBlock,
                            coldX, coldY, coldStat);
    std::fprintf(stderr,
                 "[macro_shot] densest massif cell (%.0f, %.0f): %ld rock, "
                 "meanTemp %.2f meanH %.2f\n",
                 double(denseX), double(denseY), denseStat.count,
                 double(denseStat.meanTemp), double(denseStat.meanH));
    std::fprintf(stderr,
                 "[macro_shot] coldest massif cell (%.0f, %.0f): %ld rock, "
                 "meanTemp %.2f meanH %.2f\n",
                 double(coldX), double(coldY), coldStat.count,
                 double(coldStat.meanTemp), double(coldStat.meanH));

    float riverX = 0.f, riverY = 0.f;
    const long riverCells = densest_river_center(td, td.width, td.height,
                                                 kFrameBlock, lp.seaLevel,
                                                 riverX, riverY);
    std::fprintf(stderr, "[macro_shot] densest river cell (%.0f, %.0f): %ld river\n",
                 double(riverX), double(riverY), riverCells);

    // -- Synthesize forests + roads so the headless capture exercises EVERY macro
    //    subsystem. The game derives these from politik/pathfinding (not linked in
    //    this standalone tool); here forests clump via a coarse value-noise lattice
    //    biased by moisture, and roads are a few deterministic Bresenham runs threaded
    //    through the framed subjects (incl. a river crossing). Roads now compose
    //    ON TOP of the massif (mountains are a biome), so a road threaded through
    //    the densest block exercises the overlay layering. Test-only synthesis. --
    {
        auto sh = [](int x, int y, std::uint32_t salt) -> float {
            std::uint32_t h = std::uint32_t(x) * 374761393u +
                              std::uint32_t(y) * 668265263u + salt * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return float(h & 0xffffffu) / float(0xffffff);
        };
        auto vnoise = [&](float fx, float fy, int period, std::uint32_t salt) -> float {
            const int p = period > 0 ? period : 1;
            const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
            float tx = fx - float(x0), ty = fy - float(y0);
            tx = tx * tx * (3.f - 2.f * tx);
            ty = ty * ty * (3.f - 2.f * ty);
            auto latt = [&](int gx, int gy) {
                const int wx = ((gx % p) + p) % p, wy = ((gy % p) + p) % p;
                return sh(wx, wy, salt);
            };
            const float a = latt(x0, y0), b = latt(x0 + 1, y0);
            const float c = latt(x0, y0 + 1), d = latt(x0 + 1, y0 + 1);
            return (a + (b - a) * tx) * (1.f - ty) + (c + (d - c) * tx) * ty;
        };
        long trees = 0;
        for (int y = 0; y < td.height; ++y) {
            for (int x = 0; x < td.width; ++x) {
                const std::size_t i = std::size_t(y) * td.width + x;
                if (features.data[i] != sm::FT_None) continue; // skip assigned cells
                const bool water = td.rgba[i * 4 + 3] == 0 ||
                                   float(td.rgba[i * 4 + 0]) / 255.0f < lp.seaLevel;
                if (water) continue;
                const float h = float(td.rgba[i * 4 + 0]) / 255.0f;
                // Keep forests below the mountain foot -> clean iso-height border
                // where the Mountain biome takes over from the forested lowland.
                if (h >= sm::kMountainBiomeLevel) continue;
                const float moist = float(td.rgba[i * 4 + 1]) / 255.0f;
                const float clump = vnoise(float(x) / 24.f, float(y) / 24.f,
                                           std::max(1, td.width / 24), 11u);
                const float detail = vnoise(float(x) / 6.f, float(y) / 6.f,
                                            std::max(1, td.width / 6), 23u);
                const float dens = clump * 0.72f + detail * 0.28f;
                if (dens > 0.60f - moist * 0.34f) {
                    features.data[i] = sm::FT_Tree;
                    ++trees;
                }
            }
        }
        auto put = [&](int x, int y, sm::FeatureType t) {
            const int wx = ((x % td.width) + td.width) % td.width;
            const int wy = ((y % td.height) + td.height) % td.height;
            const std::size_t i = std::size_t(wy) * td.width + wx;
            const bool water = td.rgba[i * 4 + 3] == 0 ||
                               float(td.rgba[i * 4 + 0]) / 255.0f < lp.seaLevel;
            if (water) return; // roads compose over any biome, including mountains
            features.data[i] = std::uint8_t(t);
        };
        auto road = [&](int x0, int y0, int x1, int y1, sm::FeatureType t) {
            int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
            for (;;) {
                put(x0, y0, t);
                if (x0 == x1 && y0 == y1) break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        };
        const int dX = int(denseX), dY = int(denseY);
        const int rX = int(riverX), rY = int(riverY);
        road(dX - 40, dY - 22, dX + 40, dY + 28, sm::FT_Road);      // cobbled, thru massif foot
        road(dX + 24, dY - 34, dX - 20, dY + 34, sm::FT_DirtRoad);  // dirt, crossing the first
        road(rX - 44, rY - 6, rX + 44, rY + 10, sm::FT_Road);       // cobbled, crossing a river
        road(rX - 10, rY - 40, rX + 14, rY + 40, sm::FT_DirtRoad);  // dirt, along the valley
        std::fprintf(stderr, "[macro_shot] synth %ld forest cells + 4 roads\n", trees);
    }

    sm::MacroRendererVk mr;
    Offscreen off;
    if (!off.create(dev, resW, resH)) {
        std::fprintf(stderr, "[macro_shot] offscreen target FAILED\n");
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 4;
    }
    if (!mr.init(dev, off.pass)) {
        std::fprintf(stderr, "[macro_shot] macro renderer init FAILED\n");
        off.destroy(dev);
        dev.destroy();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 5;
    }
    mr.upload(dev, td, features, zones);

    const Shot shots[] = {
        {"day_wide", 0.5f, 512.0f, FRAME_DENSE},
        {"day_close", 0.5f, 160.0f, FRAME_DENSE},
        {"game_close", 0.5f, 48.0f, FRAME_DENSE},   // ~game zoom: judge pixel crispness
        {"night_wide", 0.0f, 512.0f, FRAME_DENSE},
        {"cold_close", 0.5f, 160.0f, FRAME_COLD},
        {"river_mid", 0.5f, 120.0f, FRAME_RIVER},   // rivers + banks + forests + roads
        {"river_close", 0.5f, 48.0f, FRAME_RIVER},  // banks up close (blue-halo check)
    };
    int failures = 0;
    std::vector<std::uint8_t> pixels;
    for (const Shot& s : shots) {
        const float zoom = float(resW) / s.cellsAcross;
        const float camX = s.frame == FRAME_COLD ? coldX
                         : s.frame == FRAME_RIVER ? riverX : denseX;
        const float camY = s.frame == FRAME_COLD ? coldY
                         : s.frame == FRAME_RIVER ? riverY : denseY;
        if (!off.shoot(dev, mr, td, camX, camY, zoom, lp.seaLevel, s.tod, pixels)) {
            std::fprintf(stderr, "[macro_shot] render FAILED (%s)\n", s.name);
            ++failures;
            continue;
        }
        char path[1024];
        std::snprintf(path, sizeof path, "%s/%s.ppm", outDir, s.name);
        if (write_ppm(path, pixels.data(), int(resW), int(resH)))
            std::fprintf(stderr, "[macro_shot] wrote %s\n", path);
        else
            ++failures;
    }

    vkDeviceWaitIdle(dev.device);
    mr.destroy(dev);
    off.destroy(dev);
    dev.destroy();
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::fprintf(stderr, "[macro_shot] done (%d failure(s))\n", failures);
    return failures == 0 ? 0 : 6;
}
