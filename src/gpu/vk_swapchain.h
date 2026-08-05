// Swapchain + image views for the Vulkan backend. Recreatable on resize.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanSwapchain
    {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        // Which present mode the swapchain actually got. MAILBOX when the
        // surface offers it (non-blocking: the loop's pace stays its own), else
        // FIFO — where the display's refresh also becomes the world's tick rate.
        // A TIME property as much as a graphics one; see the note in create().
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        // True when the presentable images were created with TRANSFER_SRC usage
        // (surface-support permitting), so a screenshot path may copy them to a
        // host buffer. Set fresh on every create(); false when unsupported.
        bool transferSrc = false;

        // fbWidth/fbHeight = drawable size, used only when the surface reports
        // no fixed extent. Returns false if the drawable is zero-sized.
        bool create(const VulkanDevice& dev, int fbWidth, int fbHeight);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
