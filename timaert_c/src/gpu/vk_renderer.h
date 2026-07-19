// Minimal present path: render pass + framebuffers + per-frame command buffers
// and synchronization, plus a clear-screen draw. Later phases add pipelines and
// real geometry; this proves the swapchain/submit/present loop end-to-end.
#pragma once

#include <vulkan/vulkan.h>

#include "gpu/vk_swapchain.h"

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace gpu
{
    struct VulkanDevice;

    struct VulkanRenderer
    {
        static constexpr int kMaxFramesInFlight = 2;

        VulkanDevice* dev = nullptr;
        VulkanSwapchain swapchain;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> framebuffers;
        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkCommandBuffer cmd[kMaxFramesInFlight] = {};

        // Depth attachment (shared across swapchain images; recreated on resize).
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        // Per frame-in-flight.
        VkSemaphore imageAvailable[kMaxFramesInFlight] = {};
        VkFence inFlight[kMaxFramesInFlight] = {};
        // Per swapchain image.
        std::vector<VkSemaphore> renderFinished;
        std::vector<VkFence> imagesInFlight; // borrowed handles, not owned

        std::uint32_t currentFrame = 0;
        std::uint32_t currentImageIndex = 0;
        bool framebufferResized = false;

        bool init(VulkanDevice& d, SDL_Window* window);
        void destroy();
        // Frame API: begin_frame() waits/acquires and opens the render pass with
        // a clear. Record draw commands into current_command_buffer(), then call
        // end_frame() to close, submit and present. begin_frame() returns false
        // when the frame is skipped (resize/minimize) — record nothing and do
        // not call end_frame() that frame.
        bool begin_frame(SDL_Window* window, float r, float g, float b);
        // Split frame begin: acquire_frame() waits/acquires and opens the
        // command buffer (no render pass) so callers can record an offscreen
        // pass (e.g. shadow map) first; then begin_render_pass() opens the main
        // pass. begin_frame() is acquire_frame() + begin_render_pass().
        bool acquire_frame(SDL_Window* window);
        void begin_render_pass(float r, float g, float b);
        VkCommandBuffer current_command_buffer() const { return cmd[currentFrame]; }
        bool end_frame(SDL_Window* window);

    private:
        bool create_render_pass();
        bool create_depth();
        void destroy_depth();
        bool create_framebuffers();
        bool create_commands();
        bool create_frame_sync();
        bool create_present_semaphores();
        void destroy_present_semaphores();
        void destroy_framebuffers();
        bool recreate(SDL_Window* window);
    };

} // namespace gpu
