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

        // Screenshot capture (test/tooling): a persistent host-visible buffer the
        // rendered image is copied into inside end_frame when a capture is armed.
        // request_capture() sets captureArmed; end_frame() fills capturePixels and
        // sets captureReady; take_capture() drains it.
        bool captureArmed = false;
        bool captureReady = false;
        VkBuffer captureBuf = VK_NULL_HANDLE;
        VkDeviceMemory captureMem = VK_NULL_HANDLE;
        VkDeviceSize captureCapacity = 0;
        std::vector<std::uint8_t> capturePixels;
        int captureW = 0;
        int captureH = 0;
        VkFormat captureFmt = VK_FORMAT_UNDEFINED;

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

        // Screenshot (test/tooling). Arm a one-shot copy of the frame currently
        // being built with request_capture() BEFORE end_frame(); end_frame()
        // then records a copy of the rendered swapchain image into an internal
        // host-visible buffer, still inside the acquired frame's command buffer
        // (so it is spec-valid — the image is used only between acquire and
        // present), submits, presents, and blocks on the frame fence to stage
        // the pixels. Retrieve them after end_frame() with take_capture(): it
        // moves out tightly-packed 32-bit pixels in the swapchain's native
        // format (BGRA on this platform) plus dimensions, and returns false if
        // nothing was captured. request_capture() is a no-op when the swapchain
        // lacks TRANSFER_SRC (see VulkanSwapchain::transferSrc).
        void request_capture();
        bool take_capture(std::vector<std::uint8_t>& out, int& outW, int& outH,
                          VkFormat& outFmt);

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
        // Lazily (re)allocate the capture staging buffer to hold `bytes`.
        bool ensure_capture_buffer(VkDeviceSize bytes);
        void destroy_capture();
    };

} // namespace gpu
