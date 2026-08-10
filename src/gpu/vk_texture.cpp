#include "gpu/vk_texture.h"
#include "gpu/vk_common.h"
#include "gpu/vk_device.h"

#include <cstdio>
#include <cstring>

namespace gpu
{
    namespace
    {
        std::uint32_t find_memory_type(VkPhysicalDevice pd, std::uint32_t bits,
                                       VkMemoryPropertyFlags props)
        {
            VkPhysicalDeviceMemoryProperties mp{};
            vkGetPhysicalDeviceMemoryProperties(pd, &mp);
            for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
                if ((bits & (1u << i))
                    && (mp.memoryTypes[i].propertyFlags & props) == props)
                    return i;
            return UINT32_MAX;
        }

        bool make_buffer(const VulkanDevice& d, VkDeviceSize size,
                         VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                         VkBuffer* buf, VkDeviceMemory* mem)
        {
            VkBufferCreateInfo bci{};
            bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size = size;
            bci.usage = usage;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(d.device, &bci, nullptr, buf) != VK_SUCCESS)
                return false;
            VkMemoryRequirements mr{};
            vkGetBufferMemoryRequirements(d.device, *buf, &mr);
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = find_memory_type(d.physical, mr.memoryTypeBits,
                                                  props);
            if (ai.memoryTypeIndex == UINT32_MAX) return false;
            if (vkAllocateMemory(d.device, &ai, nullptr, mem) != VK_SUCCESS)
                return false;
            vkBindBufferMemory(d.device, *buf, *mem, 0);
            return true;
        }
    } // namespace

    // Shared device-local sampled-image upload: stage `pixels`, copy into an
    // OPTIMAL image of `format`, transition to SHADER_READ, then build a view +
    // sampler. `bpp` is the source bytes-per-pixel (must match `format`).
    static bool upload_sampled_2d(const VulkanDevice& d, std::uint32_t width,
                                  std::uint32_t height,
                                  const std::uint8_t* pixels, VkFormat format,
                                  std::uint32_t bpp, bool linearFilter,
                                  bool repeat, VulkanTexture& out)
    {
        const VkDeviceSize size =
            static_cast<VkDeviceSize>(width) * height * bpp;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!make_buffer(d, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                             | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &staging, &stagingMem)) {
            std::fprintf(stderr, "[vk] texture staging buffer failed\n");
            return false;
        }
        void* mapped = nullptr;
        vkMapMemory(d.device, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, pixels, static_cast<std::size_t>(size));
        vkUnmapMemory(d.device, stagingMem);

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = format;
        ici.extent = {width, height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_SRC lets a sampled image serve as a vkCmdCopyImage source and
        // be read back (seam ping-pong / self-check). Harmless for every other
        // sampled texture; costs nothing on a device-local OPTIMAL image.
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(d.device, &ici, nullptr, &out.image) != VK_SUCCESS) {
            vkDestroyBuffer(d.device, staging, nullptr);
            vkFreeMemory(d.device, stagingMem, nullptr);
            return false;
        }
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(d.device, out.image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = find_memory_type(d.physical, mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(d.device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
            vkDestroyBuffer(d.device, staging, nullptr);
            vkFreeMemory(d.device, stagingMem, nullptr);
            return false;
        }
        vkBindImageMemory(d.device, out.image, out.memory, 0);

        // One-time transfer: layout to TRANSFER_DST, copy, layout to SHADER_READ.
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = d.families.graphics;
        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(d.device, &pci, nullptr, &pool);

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(d.device, &cai, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = out.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(d.device, &fci, nullptr, &fence);
        vkQueueSubmit(d.graphicsQueue, 1, &si, fence);
        vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(d.device, fence, nullptr);
        vkDestroyCommandPool(d.device, pool, nullptr);
        vkDestroyBuffer(d.device, staging, nullptr);
        vkFreeMemory(d.device, stagingMem, nullptr);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = out.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(d.device, &vci, nullptr, &out.view) != VK_SUCCESS)
            return false;

        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        const VkFilter filter = linearFilter ? VK_FILTER_LINEAR
                                             : VK_FILTER_NEAREST;
        sci.magFilter = filter;
        sci.minFilter = filter;
        const VkSamplerAddressMode addr =
            repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                   : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeU = addr;
        sci.addressModeV = addr;
        sci.addressModeW = addr;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.maxLod = 0.0f;
        if (vkCreateSampler(d.device, &sci, nullptr, &out.sampler) != VK_SUCCESS)
            return false;
        out.width = width;
        out.height = height;
        out.bpp = bpp;
        return true;
    }

    bool VulkanTexture::update_region(const VulkanDevice& d, std::uint32_t x,
                                      std::uint32_t y, std::uint32_t w,
                                      std::uint32_t h, const std::uint8_t* pixels)
    {
        if (image == VK_NULL_HANDLE || bpp == 0 || w == 0 || h == 0) return false;
        if (x + w > width || y + h > height) return false;

        const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * bpp;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!make_buffer(d, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                             | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &staging, &stagingMem)) {
            return false;
        }
        void* mapped = nullptr;
        vkMapMemory(d.device, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, pixels, static_cast<std::size_t>(size));
        vkUnmapMemory(d.device, stagingMem);

        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = d.families.graphics;
        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(d.device, &pci, nullptr, &pool);

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(d.device, &cai, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // Transition the whole image SHADER_READ → TRANSFER_DST (even for a
        // partial copy — a single-subresource layout is simplest and correct).
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {static_cast<std::int32_t>(x),
                              static_cast<std::int32_t>(y), 0};
        region.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, staging, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(d.device, &fci, nullptr, &fence);
        vkQueueSubmit(d.graphicsQueue, 1, &si, fence);
        vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(d.device, fence, nullptr);
        vkDestroyCommandPool(d.device, pool, nullptr);
        vkDestroyBuffer(d.device, staging, nullptr);
        vkFreeMemory(d.device, stagingMem, nullptr);
        return true;
    }

    bool VulkanTexture::read_back(const VulkanDevice& d,
                                  std::vector<std::uint8_t>& out) const
    {
        if (image == VK_NULL_HANDLE || bpp == 0 || width == 0 || height == 0)
            return false;
        const VkDeviceSize size =
            static_cast<VkDeviceSize>(width) * height * bpp;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!make_buffer(d, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                             | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &staging, &stagingMem))
            return false;

        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = d.families.graphics;
        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(d.device, &pci, nullptr, &pool);

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(d.device, &cai, &cmd);

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
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(d.device, &fci, nullptr, &fence);
        vkQueueSubmit(d.graphicsQueue, 1, &si, fence);
        vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);

        out.resize(static_cast<std::size_t>(size));
        void* mapped = nullptr;
        vkMapMemory(d.device, stagingMem, 0, size, 0, &mapped);
        std::memcpy(out.data(), mapped, static_cast<std::size_t>(size));
        vkUnmapMemory(d.device, stagingMem);

        vkDestroyFence(d.device, fence, nullptr);
        vkDestroyCommandPool(d.device, pool, nullptr);
        vkDestroyBuffer(d.device, staging, nullptr);
        vkFreeMemory(d.device, stagingMem, nullptr);
        return true;
    }

    bool blit_shift_r8(const VulkanDevice& d, VulkanTexture& src,
                       VulkanTexture& dst, std::uint32_t srcX, std::uint32_t srcY,
                       std::uint32_t dstX, std::uint32_t dstY,
                       std::uint32_t copyW, std::uint32_t copyH,
                       const FreshRegion* fresh, std::size_t nFresh)
    {
        if (src.image == VK_NULL_HANDLE || dst.image == VK_NULL_HANDLE)
            return false;
        if (src.bpp != 1 || dst.bpp != 1) return false;  // R8 only

        // Host-visible staging for each fresh cell (one buffer per rect; all must
        // outlive the single command buffer, so they're freed after the fence).
        std::vector<VkBuffer> stg(nFresh, VK_NULL_HANDLE);
        std::vector<VkDeviceMemory> stgMem(nFresh, VK_NULL_HANDLE);
        auto cleanup_staging = [&]() {
            for (std::size_t i = 0; i < nFresh; ++i) {
                if (stg[i]) vkDestroyBuffer(d.device, stg[i], nullptr);
                if (stgMem[i]) vkFreeMemory(d.device, stgMem[i], nullptr);
            }
        };
        for (std::size_t i = 0; i < nFresh; ++i) {
            const VkDeviceSize sz =
                static_cast<VkDeviceSize>(fresh[i].w) * fresh[i].h;
            if (sz == 0 || fresh[i].pixels == nullptr) {
                cleanup_staging();
                return false;
            }
            if (!make_buffer(d, sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &stg[i], &stgMem[i])) {
                cleanup_staging();
                return false;
            }
            void* mapped = nullptr;
            vkMapMemory(d.device, stgMem[i], 0, sz, 0, &mapped);
            std::memcpy(mapped, fresh[i].pixels, static_cast<std::size_t>(sz));
            vkUnmapMemory(d.device, stgMem[i]);
        }

        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = d.families.graphics;
        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(d.device, &pci, nullptr, &pool);

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(d.device, &cai, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // src: SHADER_READ → TRANSFER_SRC. dst: UNDEFINED → TRANSFER_DST — the
        // overlap copy + fresh fills together cover every texel of dst, so its
        // prior contents are discarded (no false read-after-write dependency).
        VkImageMemoryBarrier pre[2]{};
        for (auto& p : pre) {
            p.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            p.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            p.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            p.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        pre[0].image = src.image;
        pre[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        pre[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        pre[1].image = dst.image;
        pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        pre[1].srcAccessMask = 0;
        pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 2, pre);

        if (copyW > 0 && copyH > 0) {
            VkImageCopy ic{};
            ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            ic.srcOffset = {static_cast<std::int32_t>(srcX),
                            static_cast<std::int32_t>(srcY), 0};
            ic.dstOffset = {static_cast<std::int32_t>(dstX),
                            static_cast<std::int32_t>(dstY), 0};
            ic.extent = {copyW, copyH, 1};
            vkCmdCopyImage(cmd, src.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        }
        // Fresh cells are disjoint from the overlap rect, so no barrier is needed
        // between the image copy and these buffer copies (all TRANSFER-stage
        // writes to non-overlapping regions of dst).
        for (std::size_t i = 0; i < nFresh; ++i) {
            VkBufferImageCopy r{};
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            r.imageOffset = {static_cast<std::int32_t>(fresh[i].x),
                             static_cast<std::int32_t>(fresh[i].y), 0};
            r.imageExtent = {fresh[i].w, fresh[i].h, 1};
            vkCmdCopyBufferToImage(cmd, stg[i], dst.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        }

        VkImageMemoryBarrier post[2]{};
        for (auto& p : post) {
            p.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            p.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            p.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            p.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            p.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            p.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        post[0].image = dst.image;
        post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        post[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post[1].image = src.image;
        post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        post[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 2, post);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(d.device, &fci, nullptr, &fence);
        vkQueueSubmit(d.graphicsQueue, 1, &si, fence);
        vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(d.device, fence, nullptr);
        vkDestroyCommandPool(d.device, pool, nullptr);
        cleanup_staging();
        return true;
    }

    bool VulkanTexture::create_rgba8(const VulkanDevice& d, std::uint32_t width,
                                     std::uint32_t height,
                                     const std::uint8_t* pixels,
                                     bool linearFilter, bool repeat)
    {
        return upload_sampled_2d(d, width, height, pixels,
                                 VK_FORMAT_R8G8B8A8_UNORM, 4, linearFilter,
                                 repeat, *this);
    }

    bool VulkanTexture::create_r8(const VulkanDevice& d, std::uint32_t width,
                                  std::uint32_t height,
                                  const std::uint8_t* pixels,
                                  bool linearFilter, bool repeat)
    {
        return upload_sampled_2d(d, width, height, pixels, VK_FORMAT_R8_UNORM, 1,
                                 linearFilter, repeat, *this);
    }

    bool VulkanTexture::create_r32f(const VulkanDevice& d, std::uint32_t width,
                                    std::uint32_t height, const float* texels,
                                    bool linearFilter, bool repeat)
    {
        return upload_sampled_2d(d, width, height,
                                 reinterpret_cast<const std::uint8_t*>(texels),
                                 VK_FORMAT_R32_SFLOAT, 4, linearFilter, repeat,
                                 *this);
    }

    bool VulkanTexture::create_rg32f(const VulkanDevice& d, std::uint32_t width,
                                     std::uint32_t height, const float* texels,
                                     bool linearFilter, bool repeat)
    {
        return upload_sampled_2d(d, width, height,
                                 reinterpret_cast<const std::uint8_t*>(texels),
                                 VK_FORMAT_R32G32_SFLOAT, 8, linearFilter,
                                 repeat, *this);
    }

    void VulkanTexture::destroy(const VulkanDevice& d)
    {
        if (sampler) {
            vkDestroySampler(d.device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }
        if (view) {
            vkDestroyImageView(d.device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (image) {
            vkDestroyImage(d.device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (memory) {
            vkFreeMemory(d.device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
        width = height = bpp = 0;
    }
} // namespace gpu
