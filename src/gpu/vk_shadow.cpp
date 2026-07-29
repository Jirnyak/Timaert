#include "gpu/vk_shadow.h"
#include "gpu/vk_common.h"
#include "gpu/vk_device.h"

namespace gpu
{
    namespace
    {
        std::uint32_t find_mem(const VulkanDevice& d, std::uint32_t typeBits,
                               VkMemoryPropertyFlags props)
        {
            VkPhysicalDeviceMemoryProperties mp{};
            vkGetPhysicalDeviceMemoryProperties(d.physical, &mp);
            for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
                if ((typeBits & (1u << i))
                    && (mp.memoryTypes[i].propertyFlags & props) == props)
                    return i;
            return 0;
        }
    } // namespace

    bool VulkanShadowMap::init(const VulkanDevice& dev, std::uint32_t sz)
    {
        size = sz;

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = {size, size, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = format;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                   | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_TRY(vkCreateImage(dev.device, &ii, nullptr, &image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev.device, image, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            find_mem(dev, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_TRY(vkAllocateMemory(dev.device, &ai, nullptr, &memory));
        VK_TRY(vkBindImageMemory(dev.device, image, memory, 0));

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_TRY(vkCreateImageView(dev.device, &vi, nullptr, &view));

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 1.0f;
        VK_TRY(vkCreateSampler(dev.device, &si, nullptr, &sampler));

        // Depth-only render pass; store + transition to READ_ONLY so the main
        // pass can sample it.
        VkAttachmentDescription depth{};
        depth.format = format;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.pDepthStencilAttachment = &ref;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &depth;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 2;
        rp.pDependencies = deps;
        VK_TRY(vkCreateRenderPass(dev.device, &rp, nullptr, &renderPass));

        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass;
        fbi.attachmentCount = 1;
        fbi.pAttachments = &view;
        fbi.width = size;
        fbi.height = size;
        fbi.layers = 1;
        VK_TRY(vkCreateFramebuffer(dev.device, &fbi, nullptr, &framebuffer));
        return true;
    }

    void VulkanShadowMap::begin(VkCommandBuffer cmd) const
    {
        VkClearValue clear{};
        clear.depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffer;
        rp.renderArea.extent = {size, size};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width = static_cast<float>(size);
        vp.height = static_cast<float>(size);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{};
        sc.extent = {size, size};
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    void VulkanShadowMap::end(VkCommandBuffer cmd) const
    {
        vkCmdEndRenderPass(cmd);
    }

    void VulkanShadowMap::destroy(const VulkanDevice& dev)
    {
        if (framebuffer) vkDestroyFramebuffer(dev.device, framebuffer, nullptr);
        if (renderPass) vkDestroyRenderPass(dev.device, renderPass, nullptr);
        if (sampler) vkDestroySampler(dev.device, sampler, nullptr);
        if (view) vkDestroyImageView(dev.device, view, nullptr);
        if (image) vkDestroyImage(dev.device, image, nullptr);
        if (memory) vkFreeMemory(dev.device, memory, nullptr);
        *this = {};
    }
} // namespace gpu
