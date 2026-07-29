#include "gpu/vk_renderer.h"
#include "gpu/vk_common.h"
#include "gpu/vk_device.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstring>

namespace gpu
{
    namespace
    {
        std::uint32_t find_mem_type(const VulkanDevice& d, std::uint32_t typeBits,
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

    bool VulkanRenderer::init(VulkanDevice& d, SDL_Window* window)
    {
        dev = &d;
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        if (!swapchain.create(d, w, h)) return false;
        if (!create_depth()) return false;
        if (!create_render_pass()) return false;
        if (!create_framebuffers()) return false;
        if (!create_commands()) return false;
        if (!create_frame_sync()) return false;
        if (!create_present_semaphores()) return false;
        return true;
    }

    bool VulkanRenderer::create_render_pass()
    {
        VkAttachmentDescription color{};
        color.format = swapchain.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkAttachmentDescription depth{};
        depth.format = depthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                           | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                           | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription atts[2] = {color, depth};
        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2;
        ci.pAttachments = atts;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;
        VK_TRY(vkCreateRenderPass(dev->device, &ci, nullptr, &renderPass));
        return true;
    }

    bool VulkanRenderer::create_depth()
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = {swapchain.extent.width, swapchain.extent.height, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = depthFormat;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_TRY(vkCreateImage(dev->device, &ii, nullptr, &depthImage));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev->device, depthImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_mem_type(*dev, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_TRY(vkAllocateMemory(dev->device, &ai, nullptr, &depthMemory));
        VK_TRY(vkBindImageMemory(dev->device, depthImage, depthMemory, 0));

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = depthFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VK_TRY(vkCreateImageView(dev->device, &vi, nullptr, &depthView));
        return true;
    }

    void VulkanRenderer::destroy_depth()
    {
        if (depthView) {
            vkDestroyImageView(dev->device, depthView, nullptr);
            depthView = VK_NULL_HANDLE;
        }
        if (depthImage) {
            vkDestroyImage(dev->device, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }
        if (depthMemory) {
            vkFreeMemory(dev->device, depthMemory, nullptr);
            depthMemory = VK_NULL_HANDLE;
        }
    }

    bool VulkanRenderer::create_framebuffers()
    {
        framebuffers.resize(swapchain.views.size());
        for (std::size_t i = 0; i < swapchain.views.size(); ++i) {
            VkImageView att[2] = {swapchain.views[i], depthView};
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = renderPass;
            ci.attachmentCount = 2;
            ci.pAttachments = att;
            ci.width = swapchain.extent.width;
            ci.height = swapchain.extent.height;
            ci.layers = 1;
            VK_TRY(vkCreateFramebuffer(dev->device, &ci, nullptr,
                                       &framebuffers[i]));
        }
        return true;
    }

    bool VulkanRenderer::create_commands()
    {
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = dev->families.graphics;
        VK_TRY(vkCreateCommandPool(dev->device, &pci, nullptr, &cmdPool));

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = kMaxFramesInFlight;
        VK_TRY(vkAllocateCommandBuffers(dev->device, &ai, cmd));
        return true;
    }

    bool VulkanRenderer::create_frame_sync()
    {
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            VK_TRY(vkCreateSemaphore(dev->device, &sci, nullptr,
                                     &imageAvailable[i]));
            VK_TRY(vkCreateFence(dev->device, &fci, nullptr, &inFlight[i]));
        }
        return true;
    }

    bool VulkanRenderer::create_present_semaphores()
    {
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        renderFinished.resize(swapchain.images.size());
        for (auto& s : renderFinished)
            VK_TRY(vkCreateSemaphore(dev->device, &sci, nullptr, &s));
        imagesInFlight.assign(swapchain.images.size(), VK_NULL_HANDLE);
        return true;
    }

    void VulkanRenderer::destroy_present_semaphores()
    {
        for (auto s : renderFinished)
            if (s) vkDestroySemaphore(dev->device, s, nullptr);
        renderFinished.clear();
        imagesInFlight.clear();
    }

    void VulkanRenderer::destroy_framebuffers()
    {
        for (auto fb : framebuffers)
            if (fb) vkDestroyFramebuffer(dev->device, fb, nullptr);
        framebuffers.clear();
    }

    bool VulkanRenderer::acquire_frame(SDL_Window* window)
    {
        vkWaitForFences(dev->device, 1, &inFlight[currentFrame], VK_TRUE,
                        UINT64_MAX);

        VkResult acq = vkAcquireNextImageKHR(
            dev->device, swapchain.handle, UINT64_MAX,
            imageAvailable[currentFrame], VK_NULL_HANDLE, &currentImageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate(window);
            return false;
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "[vk] acquire failed: %s\n", vk_result_str(acq));
            return false;
        }

        // Guard against using a swapchain image still referenced by an
        // in-flight frame (image count may exceed frames-in-flight).
        if (imagesInFlight[currentImageIndex] != VK_NULL_HANDLE)
            vkWaitForFences(dev->device, 1, &imagesInFlight[currentImageIndex],
                            VK_TRUE, UINT64_MAX);
        imagesInFlight[currentImageIndex] = inFlight[currentFrame];

        vkResetFences(dev->device, 1, &inFlight[currentFrame]);

        VkCommandBuffer c = cmd[currentFrame];
        vkResetCommandBuffer(c, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        return vkBeginCommandBuffer(c, &bi) == VK_SUCCESS;
    }

    void VulkanRenderer::begin_render_pass(float r, float g, float b)
    {
        VkCommandBuffer c = cmd[currentFrame];
        VkClearValue clears[2]{};
        clears[0].color.float32[0] = r;
        clears[0].color.float32[1] = g;
        clears[0].color.float32[2] = b;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffers[currentImageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = swapchain.extent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(c, &rp, VK_SUBPASS_CONTENTS_INLINE);
    }

    bool VulkanRenderer::begin_frame(SDL_Window* window, float r, float g, float b)
    {
        if (!acquire_frame(window)) return false;
        begin_render_pass(r, g, b);
        return true;
    }

    bool VulkanRenderer::end_frame(SDL_Window* window)
    {
        VkCommandBuffer c = cmd[currentFrame];
        vkCmdEndRenderPass(c);

        // Screenshot: copy the just-rendered swapchain image (now in
        // PRESENT_SRC_KHR per the render pass finalLayout) into the persistent
        // capture buffer, still inside THIS acquired frame's command buffer so
        // the image is only ever touched between acquire and present — the copy
        // is part of the same submit. Restore PRESENT_SRC afterward so the
        // present below is still valid.
        const bool capturing = captureArmed && swapchain.transferSrc
                               && currentImageIndex < swapchain.images.size()
                               && swapchain.extent.width != 0
                               && swapchain.extent.height != 0;
        VkDeviceSize captureBytes = 0;
        bool captureRecorded = false;
        if (capturing) {
            const std::uint32_t w = swapchain.extent.width;
            const std::uint32_t h = swapchain.extent.height;
            captureBytes = VkDeviceSize(w) * VkDeviceSize(h) * 4u;
            if (ensure_capture_buffer(captureBytes)) {
                const VkImage image = swapchain.images[currentImageIndex];
                auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                                   VkAccessFlags srcA, VkAccessFlags dstA,
                                   VkPipelineStageFlags srcS,
                                   VkPipelineStageFlags dstS) {
                    VkImageMemoryBarrier b{};
                    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    b.oldLayout = oldL;
                    b.newLayout = newL;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.image = image;
                    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    b.srcAccessMask = srcA;
                    b.dstAccessMask = dstA;
                    vkCmdPipelineBarrier(c, srcS, dstS, 0, 0, nullptr, 0, nullptr,
                                         1, &b);
                };
                barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;    // tightly packed: w*4 per row
                region.bufferImageHeight = 0;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {w, h, 1};
                vkCmdCopyImageToBuffer(c, image,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       captureBuf, 1, &region);
                barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_ACCESS_TRANSFER_READ_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
                captureW = int(w);
                captureH = int(h);
                captureFmt = swapchain.format;
                captureRecorded = true;
            }
        }

        if (vkEndCommandBuffer(c) != VK_SUCCESS) return false;

        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &imageAvailable[currentFrame];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &c;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderFinished[currentImageIndex];
        VK_TRY(vkQueueSubmit(dev->graphicsQueue, 1, &si, inFlight[currentFrame]));

        // The fence just handed to this submit is the one to wait on for the
        // capture readback; grab its index before currentFrame advances.
        const std::uint32_t submitFrame = currentFrame;

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderFinished[currentImageIndex];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain.handle;
        pi.pImageIndices = &currentImageIndex;
        VkResult pr = vkQueuePresentKHR(dev->presentQueue, &pi);

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;

        // Drain the capture: the copy above finished when submitFrame's fence
        // signals. Blocking here is fine — the capture path is test/tooling only.
        if (captureRecorded) {
            vkWaitForFences(dev->device, 1, &inFlight[submitFrame], VK_TRUE,
                            UINT64_MAX);
            void* mapped = nullptr;
            if (vkMapMemory(dev->device, captureMem, 0, captureBytes, 0, &mapped)
                == VK_SUCCESS) {
                capturePixels.resize(static_cast<std::size_t>(captureBytes));
                std::memcpy(capturePixels.data(), mapped,
                            static_cast<std::size_t>(captureBytes));
                vkUnmapMemory(dev->device, captureMem);
                captureReady = true;
            }
        }
        captureArmed = false;

        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR
            || framebufferResized) {
            framebufferResized = false;
            recreate(window);
        } else if (pr != VK_SUCCESS) {
            std::fprintf(stderr, "[vk] present failed: %s\n", vk_result_str(pr));
            return false;
        }
        return true;
    }

    void VulkanRenderer::request_capture()
    {
        // Only meaningful if the swapchain images can be copied from; otherwise
        // stay disarmed so end_frame does no extra work and take_capture fails.
        if (swapchain.transferSrc) captureArmed = true;
    }

    bool VulkanRenderer::ensure_capture_buffer(VkDeviceSize bytes)
    {
        if (captureBuf != VK_NULL_HANDLE && captureCapacity >= bytes) return true;
        destroy_capture();

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev->device, &bi, nullptr, &captureBuf) != VK_SUCCESS) {
            captureBuf = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev->device, captureBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            find_mem_type(*dev, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(dev->device, &ai, nullptr, &captureMem)
            != VK_SUCCESS) {
            vkDestroyBuffer(dev->device, captureBuf, nullptr);
            captureBuf = VK_NULL_HANDLE;
            captureMem = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(dev->device, captureBuf, captureMem, 0);
        captureCapacity = req.size;
        return true;
    }

    void VulkanRenderer::destroy_capture()
    {
        if (captureMem) {
            vkFreeMemory(dev->device, captureMem, nullptr);
            captureMem = VK_NULL_HANDLE;
        }
        if (captureBuf) {
            vkDestroyBuffer(dev->device, captureBuf, nullptr);
            captureBuf = VK_NULL_HANDLE;
        }
        captureCapacity = 0;
        captureReady = false;
        captureArmed = false;
    }

    bool VulkanRenderer::take_capture(std::vector<std::uint8_t>& out, int& outW,
                                      int& outH, VkFormat& outFmt)
    {
        if (!captureReady) return false;
        out = std::move(capturePixels);
        capturePixels.clear();
        outW = captureW;
        outH = captureH;
        outFmt = captureFmt;
        captureReady = false;
        return true;
    }

    bool VulkanRenderer::recreate(SDL_Window* window)
    {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        if (w == 0 || h == 0) return true; // minimized: skip, retry next frame

        vkDeviceWaitIdle(dev->device);
        destroy_present_semaphores();
        destroy_framebuffers();
        destroy_depth();
        swapchain.destroy(*dev);
        if (!swapchain.create(*dev, w, h)) return true;
        if (!create_depth()) return false;
        if (!create_framebuffers()) return false;
        if (!create_present_semaphores()) return false;
        currentFrame = 0;
        return true;
    }

    void VulkanRenderer::destroy()
    {
        if (!dev) return;
        vkDeviceWaitIdle(dev->device);
        destroy_capture();
        destroy_present_semaphores();
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            if (imageAvailable[i])
                vkDestroySemaphore(dev->device, imageAvailable[i], nullptr);
            if (inFlight[i]) vkDestroyFence(dev->device, inFlight[i], nullptr);
            imageAvailable[i] = VK_NULL_HANDLE;
            inFlight[i] = VK_NULL_HANDLE;
        }
        if (cmdPool) {
            vkDestroyCommandPool(dev->device, cmdPool, nullptr);
            cmdPool = VK_NULL_HANDLE;
        }
        destroy_framebuffers();
        destroy_depth();
        if (renderPass) {
            vkDestroyRenderPass(dev->device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        swapchain.destroy(*dev);
    }
} // namespace gpu
