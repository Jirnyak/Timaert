// Vulkan instance + surface + device bring-up (no rendering yet).
// Phase 1 of the OpenGL -> Vulkan migration. See ARCHITECTURE.md
// §Rendering & Compute Backend.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

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

        VulkanDevice() = default;
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
    };

} // namespace gpu
