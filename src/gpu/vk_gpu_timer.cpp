#include "gpu/vk_gpu_timer.h"
#include "gpu/vk_device.h"

namespace gpu
{
    bool GpuTimer::init(const VulkanDevice& dev, std::uint32_t framesInFlight)
    {
        slots = framesInFlight < kMaxSlots ? framesInFlight : kMaxSlots;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev.physical, &props);
        if (props.limits.timestampPeriod <= 0.0f
            || !props.limits.timestampComputeAndGraphics) {
            slots = 0;
            return false;
        }
        periodNs = props.limits.timestampPeriod;

        VkQueryPoolCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qci.queryCount = kMaxStamps;
        for (std::uint32_t i = 0; i < slots; ++i) {
            if (vkCreateQueryPool(dev.device, &qci, nullptr, &pools[i])
                != VK_SUCCESS) {
                destroy(dev);
                return false;
            }
        }
        return true;
    }

    void GpuTimer::destroy(const VulkanDevice& dev)
    {
        for (std::uint32_t i = 0; i < kMaxSlots; ++i) {
            if (pools[i]) vkDestroyQueryPool(dev.device, pools[i], nullptr);
            pools[i] = VK_NULL_HANDLE;
            written[i] = 0;
        }
        slots = 0;
    }

    std::uint32_t GpuTimer::collect(const VulkanDevice& dev, std::uint32_t slot,
                                    double* outMs, std::uint32_t outCap)
    {
        if (slot >= slots || written[slot] < 2) return 0;
        std::uint64_t ticks[kMaxStamps] = {};
        // The slot's fence was waited in acquire_frame, so its queries are
        // done; WAIT is a no-op safety net rather than a stall.
        if (vkGetQueryPoolResults(dev.device, pools[slot], 0, written[slot],
                                  sizeof(ticks), ticks, sizeof(std::uint64_t),
                                  VK_QUERY_RESULT_64_BIT
                                      | VK_QUERY_RESULT_WAIT_BIT)
            != VK_SUCCESS) {
            return 0;
        }
        std::uint32_t n = 0;
        for (std::uint32_t i = 1; i < written[slot] && n < outCap; ++i) {
            outMs[n++] = double(ticks[i] - ticks[i - 1]) * double(periodNs)
                         * 1.0e-6;
        }
        return n;
    }

    void GpuTimer::begin(VkCommandBuffer cmd, std::uint32_t slot)
    {
        if (slot >= slots) return;
        vkCmdResetQueryPool(cmd, pools[slot], 0, kMaxStamps);
        cursor = 0;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            pools[slot], cursor++);
        written[slot] = cursor;
    }

    void GpuTimer::stamp(VkCommandBuffer cmd, std::uint32_t slot)
    {
        if (slot >= slots || cursor >= kMaxStamps) return;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            pools[slot], cursor++);
        written[slot] = cursor;
    }

} // namespace gpu
