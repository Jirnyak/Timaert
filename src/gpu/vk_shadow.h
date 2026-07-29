// Sun-view shadow map for the subworld 3D pass (Phase 5). A depth-only image
// rendered from the light's orthographic viewpoint, then sampled (with PCF) by
// the terrain / tree fragment shaders so shadows fall on the actual surfaces —
// no floating blobs. Extensible: any caster drawn during begin()/end() casts.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanShadowMap
    {
        std::uint32_t size = 0;
        VkFormat format = VK_FORMAT_D32_SFLOAT;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;   // sampled in the main pass
        VkSampler sampler = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;

        bool init(const VulkanDevice& dev, std::uint32_t size);
        // Record: open the depth-only pass (clear to 1.0) + set viewport.
        void begin(VkCommandBuffer cmd) const;
        void end(VkCommandBuffer cmd) const;
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
