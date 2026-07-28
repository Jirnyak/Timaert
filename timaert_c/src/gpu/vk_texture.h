// Sampled 2D texture upload for the Vulkan backend: RGBA8 CPU pixels -> a
// device-local sampled image via a staging buffer and a one-time transfer.
// The upload blocks (load-time only) — never call this per frame.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

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
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
