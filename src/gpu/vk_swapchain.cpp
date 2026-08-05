#include "gpu/vk_swapchain.h"
#include "gpu/vk_common.h"
#include "gpu/vk_device.h"

#include <algorithm>
#include <vector>

namespace gpu
{
    namespace
    {
        VkSurfaceFormatKHR choose_format(VkPhysicalDevice pd, VkSurfaceKHR s)
        {
            std::uint32_t n = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(pd, s, &n, nullptr);
            std::vector<VkSurfaceFormatKHR> f(n);
            vkGetPhysicalDeviceSurfaceFormatsKHR(pd, s, &n, f.data());
            for (const auto& x : f)
                if (x.format == VK_FORMAT_B8G8R8A8_UNORM
                    && x.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    return x;
            if (!f.empty()) return f[0];
            return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }
        // Present mode: prefer one that does NOT block the loop.
        //
        // This is a TIME decision, not a graphics one. The world advances one
        // tick per turn of the main loop (core/time.h), so anything that gates
        // how fast the loop may turn also gates how fast the world lives. FIFO
        // blocks until the display's refresh, which on a 60 Hz screen holds the
        // loop to 60 turns a second — the world would run 6% slow because of
        // the monitor, not because the game was busy. That is the real world
        // reaching into the simulation, which is exactly what the tick model
        // exists to prevent.
        //
        // MAILBOX returns immediately and shows the newest finished frame, so
        // the pace is ours: the loop's own wait holds it at the tick rate on
        // any display. FIFO stays the fallback because it is the only mode
        // guaranteed to exist — and where it is all we get, the world's rate
        // follows the display and that is a known, documented limit.
        VkPresentModeKHR choose_present_mode(VkPhysicalDevice p,
                                             VkSurfaceKHR s)
        {
            std::uint32_t n = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(p, s, &n, nullptr);
            std::vector<VkPresentModeKHR> modes(n);
            if (n) {
                vkGetPhysicalDeviceSurfacePresentModesKHR(p, s, &n,
                                                          modes.data());
            }
            for (auto m : modes)
                if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
            return VK_PRESENT_MODE_FIFO_KHR;  // always available
        }
    } // namespace

    bool VulkanSwapchain::create(const VulkanDevice& d, int fbWidth, int fbHeight)
    {
        VkSurfaceCapabilitiesKHR caps{};
        VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical, d.surface,
                                                         &caps));

        VkSurfaceFormatKHR sf = choose_format(d.physical, d.surface);
        format = sf.format;

        if (caps.currentExtent.width != UINT32_MAX) {
            extent = caps.currentExtent;
        } else {
            extent.width = std::clamp(static_cast<std::uint32_t>(fbWidth),
                                      caps.minImageExtent.width,
                                      caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<std::uint32_t>(fbHeight),
                                       caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) return false;

        std::uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
            imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = d.surface;
        ci.minImageCount = imageCount;
        ci.imageFormat = sf.format;
        ci.imageColorSpace = sf.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        // Screenshot readback: also request TRANSFER_SRC so a capture path can
        // copy the presented image into a host-visible buffer. Presentable
        // images are not guaranteed to allow it, so gate on surface support and
        // record the outcome — the capture path degrades to a no-op if absent.
        transferSrc =
            (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (transferSrc)
            ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        std::uint32_t qf[2] = {d.families.graphics, d.families.present};
        if (d.families.graphics != d.families.present) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = qf;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        presentMode = choose_present_mode(d.physical, d.surface);
        ci.presentMode = presentMode;
        // MAILBOX needs a spare image to hold the newest frame while one is
        // being scanned out; two is not enough for it to be non-blocking.
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR && imageCount < 3) {
            imageCount = (caps.maxImageCount > 0)
                ? std::min<std::uint32_t>(3, caps.maxImageCount) : 3;
            ci.minImageCount = imageCount;
        }
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = VK_NULL_HANDLE;
        VK_TRY(vkCreateSwapchainKHR(d.device, &ci, nullptr, &handle));

        std::uint32_t n = 0;
        vkGetSwapchainImagesKHR(d.device, handle, &n, nullptr);
        images.resize(n);
        vkGetSwapchainImagesKHR(d.device, handle, &n, images.data());

        views.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = images[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = format;
            vci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            vci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.baseMipLevel = 0;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.baseArrayLayer = 0;
            vci.subresourceRange.layerCount = 1;
            VK_TRY(vkCreateImageView(d.device, &vci, nullptr, &views[i]));
        }
        return true;
    }

    void VulkanSwapchain::destroy(const VulkanDevice& d)
    {
        for (auto v : views)
            if (v) vkDestroyImageView(d.device, v, nullptr);
        views.clear();
        if (handle) {
            vkDestroySwapchainKHR(d.device, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
        images.clear();
    }
} // namespace gpu
