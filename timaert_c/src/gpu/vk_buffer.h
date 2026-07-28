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
        // Overwrite `bytes` at `dstOffset` IN PLACE via a staging copy, reusing
        // the existing device-local allocation (no realloc). Requires the buffer
        // to have been created with create_device_local (which adds TRANSFER_DST)
        // and [dstOffset, dstOffset+bytes) to lie within `size`. Same in-flight
        // fence contract as create_device_local. Returns false on a Vulkan error
        // or an out-of-range write.
        bool update(const VulkanDevice& dev, const void* data, VkDeviceSize bytes,
                    VkDeviceSize dstOffset = 0);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
