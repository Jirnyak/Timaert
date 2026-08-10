// Coarse per-frame GPU pass timing via timestamp queries — the measuring half
// of "optimise by fact, not by hypothesis" (TIMAERT_GPU_STATS). A ring of
// query pools (one per frame in flight, same ring discipline as the light
// SSBO) is stamped at pass boundaries while recording; each slot's results
// are read back on the NEXT use of that slot, after acquire_frame has waited
// its fence — no stalls, one frame of latency.
//
// Caveat honestly stated: on MoltenVK/Apple, timestamps inside a render pass
// may snap toward pass boundaries (tile-based execution). Stamps at true
// pass/command boundaries (what we use) are the reliable ones.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{
    struct VulkanDevice;

    struct GpuTimer
    {
        static constexpr std::uint32_t kMaxSlots = 4;
        static constexpr std::uint32_t kMaxStamps = 8;

        VkQueryPool pools[kMaxSlots] = {};
        std::uint32_t written[kMaxSlots] = {};   // stamps recorded last use
        std::uint32_t cursor = 0;                // stamps recorded this frame
        float periodNs = 0.0f;
        std::uint32_t slots = 0;

        bool init(const VulkanDevice& dev, std::uint32_t framesInFlight);
        void destroy(const VulkanDevice& dev);

        // Read the slot's PREVIOUS frame durations (milliseconds between
        // consecutive stamps) into outMs; returns the span count (0 on the
        // slot's first use). Call after acquire_frame's fence wait, before
        // begin().
        std::uint32_t collect(const VulkanDevice& dev, std::uint32_t slot,
                              double* outMs, std::uint32_t outCap);

        // Reset the slot's pool and record stamp 0. Must be OUTSIDE a render
        // pass (top of the command buffer).
        void begin(VkCommandBuffer cmd, std::uint32_t slot);

        // Record the next boundary stamp.
        void stamp(VkCommandBuffer cmd, std::uint32_t slot);
    };

} // namespace gpu
