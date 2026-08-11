// Vulkan instance + surface + device bring-up (no rendering yet).
// Phase 1 of the OpenGL -> Vulkan migration. See ARCHITECTURE.md
// §Rendering & Compute Backend.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace gpu
{
    struct QueueFamilies
    {
        std::uint32_t graphics = UINT32_MAX;
        std::uint32_t present = UINT32_MAX;
        bool complete() const
        {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
    };

    // Owns the core Vulkan objects needed before any swapchain / pipeline work:
    // instance, (optional) debug messenger, window surface, the selected
    // physical device, a logical device, and the graphics/present queues.
    // Non-copyable. Call init(); on success call destroy() to tear down.
    struct VulkanDevice
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        QueueFamilies families;
        VkPhysicalDeviceProperties props{};
        bool validation = false;

        // window must have been created with SDL_WINDOW_VULKAN.
        bool init(SDL_Window* window, bool enableValidation);
        void destroy();

        // ── Deferred destruction (the "graveyard") ──
        // A resource replaced mid-frame may still be read by the frame in
        // flight (acquire waits only its OWN slot's fence, so frame N−1 is
        // executing while N records). Destroying it immediately is a GPU
        // use-after-free; vkDeviceWaitIdle would drain the queue mid-frame.
        // Instead the handles are parked here and destroyed after
        // kMaxFramesInFlight collected frames — by then every submission that
        // could reference them has been fenced.
        //
        // collect_deferred(): call ONCE per acquired frame, AFTER the slot's
        // fence wait (VulkanRenderer::acquire_frame does this). flush_deferred():
        // destroy everything immediately — teardown only, device idle.
        // Members are mutable because the device travels as const& through
        // every upload path; the graveyard is bookkeeping, not device state.
        // Frames a parked resource must outlive. Derivation: with F frames in
        // flight, the oldest submission that can reference a handle deferred
        // during frame N is N itself, and N's fence has certainly been waited
        // once F later acquires have each waited their slot. Must be ≥ the
        // renderer's kMaxFramesInFlight — vk_renderer.cpp static_asserts the
        // two together (lockstep by mechanism, not by hope).
        static constexpr std::uint32_t kGraveyardDelayFrames = 2;

        struct DeferredResource
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            std::uint32_t framesLeft = 0;
        };
        mutable std::vector<DeferredResource> graveyard;

        void defer_destroy(VkBuffer buf, VkDeviceMemory mem) const;
        void defer_destroy_image(VkImage image, VkImageView view,
                                 VkSampler sampler, VkDeviceMemory mem) const;
        void collect_deferred() const;
        void flush_deferred() const;

        VulkanDevice() = default;
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
    };

} // namespace gpu
