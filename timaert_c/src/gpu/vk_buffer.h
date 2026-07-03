// Device-local GPU buffer (vertex / index) uploaded once from CPU data via a
// staging copy. Load-time blocking upload — never call per frame.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;

        // Uploads `bytes` from `data` into a DEVICE_LOCAL buffer of the given
        // usage (e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT). Adds TRANSFER_DST.
        bool create_device_local(const VulkanDevice& dev, const void* data,
                                 VkDeviceSize bytes, VkBufferUsageFlags usage);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
