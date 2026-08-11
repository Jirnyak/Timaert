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
        // Single-channel R32_SFLOAT variant — raw float texels (e.g. the
        // window heightfield in metres for the terrain-occlusion march). No
        // quantisation, no scale convention for the shader to keep in sync.
        bool create_r32f(const VulkanDevice& dev, std::uint32_t width,
                         std::uint32_t height, const float* texels,
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
        // RECORDED twin of update_region: the copy source is `stagingOff` bytes
        // into a caller-owned host-visible buffer (already filled), and the
        // layout barriers + copy are recorded into `cmd` instead of submitted
        // and fenced. Queue-scope FRAGMENT_SHADER→TRANSFER ordering makes it
        // safe against the frame in flight with NO stall — this is the per-frame
        // path; the blocking twin remains for load-time. `discard` marks a
        // whole-image overwrite: prior contents enter as UNDEFINED (required on
        // the first fill of an empty-created image, free bandwidth otherwise).
        bool update_region_recorded(VkCommandBuffer cmd, VkBuffer staging,
                                    VkDeviceSize stagingOff, std::uint32_t x,
                                    std::uint32_t y, std::uint32_t w,
                                    std::uint32_t h, bool discard);
        // Image + view + sampler with NO pixel upload and NO submit: layout is
        // UNDEFINED until the first update_region_recorded(..., discard=true)
        // fills it inside a frame. For images whose content is born mid-frame
        // (the material grid at first subworld entry).
        bool create_r8_empty(const VulkanDevice& dev, std::uint32_t width,
                             std::uint32_t height, bool linearFilter,
                             bool repeat);
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

    // Fresh rect whose pixels already sit `stagingOff` bytes into the caller's
    // staging buffer — the recorded twin's counterpart of FreshRegion.
    struct FreshRegionStaged
    {
        std::uint32_t x = 0, y = 0, w = 0, h = 0;
        VkDeviceSize stagingOff = 0;
    };

    // RECORDED twin of blit_shift_r8: same relocation (overlap image copy +
    // fresh-cell fills, dst discarded), but recorded into `cmd` with fresh
    // pixels sourced from one host-visible staging buffer at per-rect offsets.
    // No submit, no fence — queue-scope FRAGMENT_SHADER→TRANSFER barriers
    // order it after the in-flight frame's sampling of BOTH images.
    bool blit_shift_r8_recorded(VkCommandBuffer cmd, VulkanTexture& src,
                                VulkanTexture& dst, std::uint32_t srcX,
                                std::uint32_t srcY, std::uint32_t dstX,
                                std::uint32_t dstY, std::uint32_t copyW,
                                std::uint32_t copyH, VkBuffer staging,
                                const FreshRegionStaged* fresh,
                                std::size_t nFresh);

} // namespace gpu
