// Sampled 2D texture upload for the Vulkan backend: RGBA8 CPU pixels -> a
// device-local sampled image via a staging buffer and a one-time transfer.
// The upload blocks (load-time only) — never call this per frame.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanTexture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        // Recorded at creation so in-place updates can size staging + validate
        // bounds without the caller re-passing the format.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t bpp = 0;

        bool create_rgba8(const VulkanDevice& dev, std::uint32_t width,
                          std::uint32_t height, const std::uint8_t* pixels,
                          bool linearFilter, bool repeat);
        // Single-channel R8_UNORM variant — one byte per texel (e.g. a
        // full-resolution tile/material id grid sampled per-fragment). 4× less
        // memory/bandwidth than packing a scalar into create_rgba8's red channel.
        bool create_r8(const VulkanDevice& dev, std::uint32_t width,
                       std::uint32_t height, const std::uint8_t* pixels,
                       bool linearFilter, bool repeat);
        // Overwrite a tightly-packed [x,x+w)×[y,y+h) sub-rectangle IN PLACE,
        // reusing the existing image/view/sampler (no realloc, no descriptor
        // rewrite). `pixels` is w*h*bpp bytes, row stride = w*bpp. The whole
        // image is transitioned SHADER_READ→TRANSFER_DST→SHADER_READ around the
        // copy; the caller must guarantee no in-flight frame samples the image
        // (same fence contract as create_*). Pass (0,0,width,height,…) for a
        // full refresh. Returns false on out-of-bounds or a Vulkan failure.
        bool update_region(const VulkanDevice& dev, std::uint32_t x,
                           std::uint32_t y, std::uint32_t w, std::uint32_t h,
                           const std::uint8_t* pixels);
        // Read the whole image back into `out` (resized to width*height*bpp) via
        // an image→buffer copy. Transitions SHADER_READ→TRANSFER_SRC→SHADER_READ
        // and blocks on a fence. Diagnostics/verification only (never per frame);
        // requires the image to have been created with TRANSFER_SRC usage.
        bool read_back(const VulkanDevice& dev,
                       std::vector<std::uint8_t>& out) const;
        void destroy(const VulkanDevice& dev);
    };

    // A freshly exposed cell rectangle to be filled into the destination image
    // of a seam-crossing shift (see blit_shift_r8). `pixels` is w*h bytes
    // (R8, tightly packed, row stride = w).
    struct FreshRegion
    {
        std::uint32_t x = 0, y = 0, w = 0, h = 0;
        const std::uint8_t* pixels = nullptr;
    };

    // One-shot on-GPU seam relocation for an R8 image (material ping-pong).
    // Copies the [srcX,srcY]+(copyW×copyH) overlap of `src` into `dst` at
    // (dstX,dstY) with vkCmdCopyImage — no host round-trip — then fills the
    // `nFresh` newly exposed cell rects into `dst` from host pixels. The overlap
    // and the fresh rects are disjoint and together cover all of `dst`, so `dst`
    // is discarded (UNDEFINED) on entry. Both images end in SHADER_READ; the
    // caller then swaps src/dst and rewrites the sampler descriptor to `dst`.
    // Both images must be the same R8 size and created with
    // TRANSFER_SRC|TRANSFER_DST usage. Blocks on a fence (same contract as the
    // create/update paths). Returns false on a Vulkan failure.
    bool blit_shift_r8(const VulkanDevice& dev, VulkanTexture& src,
                       VulkanTexture& dst, std::uint32_t srcX, std::uint32_t srcY,
                       std::uint32_t dstX, std::uint32_t dstY,
                       std::uint32_t copyW, std::uint32_t copyH,
                       const FreshRegion* fresh, std::size_t nFresh);

} // namespace gpu
