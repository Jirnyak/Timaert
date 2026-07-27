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

        bool create_rgba8(const VulkanDevice& dev, std::uint32_t width,
                          std::uint32_t height, const std::uint8_t* pixels,
                          bool linearFilter, bool repeat);
        // Single-channel R8_UNORM variant — one byte per texel (e.g. a
        // full-resolution tile/material id grid sampled per-fragment). 4× less
        // memory/bandwidth than packing a scalar into create_rgba8's red channel.
        bool create_r8(const VulkanDevice& dev, std::uint32_t width,
                       std::uint32_t height, const std::uint8_t* pixels,
                       bool linearFilter, bool repeat);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
