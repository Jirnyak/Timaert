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

    bool VulkanTexture::create_rgba8(const VulkanDevice& d, std::uint32_t width,
                                     std::uint32_t height,
                                     const std::uint8_t* pixels,
                                     bool linearFilter, bool repeat)
    {
        const VkDeviceSize size =
            static_cast<VkDeviceSize>(width) * height * 4;

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
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {width, height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(d.device, &ici, nullptr, &image) != VK_SUCCESS) {
            vkDestroyBuffer(d.device, staging, nullptr);
            vkFreeMemory(d.device, stagingMem, nullptr);
            return false;
        }
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(d.device, image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = find_memory_type(d.physical, mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(d.device, &ai, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyBuffer(d.device, staging, nullptr);
            vkFreeMemory(d.device, stagingMem, nullptr);
            return false;
        }
        vkBindImageMemory(d.device, image, memory, 0);

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
        b.image = image;
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

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(d.device, &vci, nullptr, &view) != VK_SUCCESS)
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
        if (vkCreateSampler(d.device, &sci, nullptr, &sampler) != VK_SUCCESS)
            return false;
        return true;
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
    }
} // namespace gpu
