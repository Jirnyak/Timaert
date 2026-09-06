#include "gpu/vk_canvas.h"
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

    bool VulkanStainCanvas::init(const VulkanDevice& dev, std::uint32_t sz)
    {
        size = sz;

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = {size, size, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // Colour target (stamp pass) + sampled (terrain pass) + transfer dst
        // (the one-time birth clear below).
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                   | VK_IMAGE_USAGE_SAMPLED_BIT
                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_TRY(vkCreateImageView(dev.device, &vi, nullptr, &view));

        // NEAREST + REPEAT: crisp pixel-art marks, and the REPEAT does the
        // toroidal wrap in hardware — world position mod the canvas span IS
        // the texel address, no shader arithmetic.
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.maxLod = 1.0f;
        VK_TRY(vkCreateSampler(dev.device, &si, nullptr, &sampler));

        // Colour pass that PRESERVES prior marks (loadOp LOAD) and hands the
        // image back to the sampling passes (finalLayout SHADER_READ_ONLY).
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R8G8B8A8_UNORM;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        // External deps mirror the shadow map's: the previous frame's sampling
        // finishes before this pass writes; this pass's writes finish before
        // the frame's terrain fragments sample the fresh marks.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
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

        // One-time birth clear: UNDEFINED → TRANSFER_DST, clear to transparent
        // black (no marks), → SHADER_READ_ONLY so the very first frame can
        // sample it before any stamp pass runs. Fenced once, never again.
        {
            VkCommandPoolCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pci.queueFamilyIndex = dev.families.graphics;
            VkCommandPool pool = VK_NULL_HANDLE;
            VK_TRY(vkCreateCommandPool(dev.device, &pci, nullptr, &pool));

            VkCommandBufferAllocateInfo cai{};
            cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cai.commandPool = pool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(dev.device, &cai, &cmd);

            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &bi);

            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &b);

            VkClearColorValue zero{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &zero, 1, &range);

            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &b);

            vkEndCommandBuffer(cmd);

            VkSubmitInfo si2{};
            si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si2.commandBufferCount = 1;
            si2.pCommandBuffers = &cmd;
            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            vkCreateFence(dev.device, &fci, nullptr, &fence);
            vkQueueSubmit(dev.graphicsQueue, 1, &si2, fence);
            vkWaitForFences(dev.device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(dev.device, fence, nullptr);
            vkDestroyCommandPool(dev.device, pool, nullptr);
        }
        return true;
    }

    void VulkanStainCanvas::begin(VkCommandBuffer cmd) const
    {
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffer;
        rp.renderArea.extent = {size, size};
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

    void VulkanStainCanvas::end(VkCommandBuffer cmd) const
    {
        vkCmdEndRenderPass(cmd);
    }

    void VulkanStainCanvas::clear_all(VkCommandBuffer cmd) const
    {
        clear_rect(cmd, 0, 0, size, size);
    }

    void VulkanStainCanvas::clear_rect(VkCommandBuffer cmd, std::int32_t x,
                                       std::int32_t y, std::uint32_t w,
                                       std::uint32_t h) const
    {
        if (w == 0 || h == 0) return;
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca.colorAttachment = 0;
        ca.clearValue.color = {};
        VkClearRect cr{};
        cr.rect.offset = {x, y};
        cr.rect.extent = {w, h};
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
    }

    void VulkanStainCanvas::destroy(const VulkanDevice& dev)
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
