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
        // Non-null only for a create_host_mapped() buffer: a persistent CPU
        // pointer to the whole allocation (HOST_VISIBLE | HOST_COHERENT). Write
        // straight into it each frame — see create_host_mapped for the contract.
        void* mapped = nullptr;

        // Uploads `bytes` from `data` into a DEVICE_LOCAL buffer of the given
        // usage (e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT). Adds TRANSFER_DST.
        bool create_device_local(const VulkanDevice& dev, const void* data,
                                 VkDeviceSize bytes, VkBufferUsageFlags usage);
        // Allocation ONLY — no staging copy, no submit, no wait. For buffers
        // whose content is recorded into a frame command buffer right after
        // creation (grown instance buffers replaced mid-frame). Contents are
        // undefined until written. Adds TRANSFER_DST like create_device_local.
        bool create_device_local_uninit(const VulkanDevice& dev,
                                        VkDeviceSize bytes,
                                        VkBufferUsageFlags usage);
        // Allocates a HOST_VISIBLE | HOST_COHERENT buffer of `bytes` and leaves
        // it PERSISTENTLY MAPPED at `mapped`. For per-frame data that changes
        // every frame (e.g. a light SSBO in a per-frame-in-flight ring): unlike
        // update()/create_device_local, there is NO staging copy and NO
        // vkQueueWaitIdle, so it never stalls the queue. Coherent memory means a
        // plain memcpy into `mapped` is visible to the GPU with no explicit
        // flush. The CALLER owns the hazard: only write the copy whose in-flight
        // fence has been waited on (write cmd[currentFrame]'s buffer AFTER
        // acquire_frame() has reset that frame's fence). Contents are undefined
        // until first written. Returns false on a Vulkan error.
        bool create_host_mapped(const VulkanDevice& dev, VkDeviceSize bytes,
                                VkBufferUsageFlags usage);
        // (`update` СНЕСЕНА 2026-11-08, M-436: in-place перезапись через
        //  staging-копию, у которой НОЛЬ звонящих в `src/` и в `tests/`. Живут
        //  две соседки, и обе заняты: `create_device_local` грузит один раз
        //  (4 сайта), `create_host_mapped` держит покадровые данные без
        //  staging и без простоя очереди. Третьего пути — «перезаписать
        //  device-local в кадре через transient pool и vkQueueWaitIdle» — в
        //  дереве не просил никто, а он самый дорогой из трёх.)
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
