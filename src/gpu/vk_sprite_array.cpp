#include "gpu/vk_sprite_array.h"
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

        // Barrier a single array layer between SHADER_READ and TRANSFER_DST.
        void layer_barrier(VkCommandBuffer cmd, VkImage image,
                           std::uint32_t layer, VkImageLayout oldLayout,
                           VkImageLayout newLayout, VkAccessFlags srcAccess,
                           VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.oldLayout = oldLayout;
            b.newLayout = newLayout;
            b.srcAccessMask = srcAccess;
            b.dstAccessMask = dstAccess;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0,
                                 nullptr, 1, &b);
        }

        // Record: barrier layer -> TRANSFER_DST, copy staging -> the slot's
        // sub-rect of its layer, barrier layer -> SHADER_READ. Shared by the
        // per-frame and blocking paths. The barrier covers the whole layer
        // (the finest subresource granularity Vulkan has) — correct, merely
        // wider than the written rect.
        void record_slot_copy(VkCommandBuffer cmd, VkImage image,
                              VkBuffer staging, std::uint32_t layer,
                              std::int32_t offX, std::int32_t offY,
                              std::uint32_t w, std::uint32_t h)
        {
            layer_barrier(cmd, image, layer,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
            region.imageOffset = {offX, offY, 0};
            region.imageExtent = {w, h, 1};
            vkCmdCopyBufferToImage(cmd, staging, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &region);

            layer_barrier(cmd, image, layer,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    } // namespace

    bool SpriteArray::init(const VulkanDevice& d, std::uint32_t w,
                           std::uint32_t h, std::uint32_t layers,
                           bool linearFilter, std::uint32_t stagingRing,
                           std::uint32_t framesInFlight,
                           std::uint32_t grid)
    {
        if (w == 0 || h == 0 || layers == 0 || stagingRing == 0
            || framesInFlight == 0 || stagingRing < framesInFlight
            || grid == 0)
            return false;
        tileW = w;
        tileH = h;
        layerCount = layers;
        subTiles = grid;
        framesInFlight_ = framesInFlight;
        sliceSize_ = stagingRing / framesInFlight;
        frameParity_ = 0;
        stagingCursor_ = 0;
        sliceEnd_ = sliceSize_;

        // ── 2D-array image, all layers ──
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {w * grid, h * grid, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = layers;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_TRY(vkCreateImage(d.device, &ici, nullptr, &image));

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(d.device, image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = find_memory_type(d.physical, mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        VK_TRY(vkAllocateMemory(d.device, &ai, nullptr, &memory));
        vkBindImageMemory(d.device, image, memory, 0);

        // ── One-time: clear ALL layers to transparent, land in SHADER_READ ──
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = d.families.graphics;
        VkCommandPool pool = VK_NULL_HANDLE;
        VK_TRY(vkCreateCommandPool(d.device, &pci, nullptr, &pool));
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

        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = image;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toDst);

        VkClearColorValue clear{};
        clear.float32[0] = clear.float32[1] = clear.float32[2] = 0.0f;
        clear.float32[3] = 0.0f;
        VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &all);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toRead);
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

        // ── 2D_ARRAY view + shared sampler ──
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        VK_TRY(vkCreateImageView(d.device, &vci, nullptr, &view));

        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        const VkFilter filter =
            linearFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sci.magFilter = filter;
        sci.minFilter = filter;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.maxLod = 0.0f;
        VK_TRY(vkCreateSampler(d.device, &sci, nullptr, &sampler));

        // ── Bind-once descriptor (one combined image sampler) ──
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 1;
        dlci.pBindings = &b;
        VK_TRY(vkCreateDescriptorSetLayout(d.device, &dlci, nullptr,
                                           &setLayout));

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        VK_TRY(vkCreateDescriptorPool(d.device, &dpci, nullptr,
                                      &descriptorPool));

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descriptorPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &setLayout;
        VK_TRY(vkAllocateDescriptorSets(d.device, &dsai, &descriptorSet));

        VkDescriptorImageInfo dii{};
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii.imageView = view;
        dii.sampler = sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &dii;
        vkUpdateDescriptorSets(d.device, 1, &write, 0, nullptr);

        // ── Persistent host-visible staging ring for per-frame uploads ──
        const VkDeviceSize tileBytes =
            static_cast<VkDeviceSize>(w) * h * 4;
        staging_.resize(stagingRing);
        for (Staging& s : staging_) {
            if (!make_buffer(d, tileBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &s.buffer, &s.memory)) {
                std::fprintf(stderr, "[SpriteArray] staging buffer failed\n");
                return false;
            }
            vkMapMemory(d.device, s.memory, 0, tileBytes, 0, &s.mapped);
        }
        return true;
    }

    void SpriteArray::begin_frame()
    {
        // Rotate to the next slice: slot lifetimes now match frame-slot
        // lifetimes, so a slot is only rewritten after its frame's fence was
        // waited on — never while its previous copy may still be pending.
        frameParity_ = (frameParity_ + 1u) % framesInFlight_;
        stagingCursor_ = frameParity_ * sliceSize_;
        sliceEnd_ = stagingCursor_ + sliceSize_;
    }

    bool SpriteArray::upload_slot(VkCommandBuffer cmd, std::uint32_t slot,
                                  const std::uint8_t* rgba)
    {
        if (slot >= slot_count() || !rgba) return false;
        if (stagingCursor_ >= sliceEnd_) return false; // slice exhausted
        Staging& s = staging_[stagingCursor_++];
        const std::size_t bytes = std::size_t(tileW) * tileH * 4u;
        std::memcpy(s.mapped, rgba, bytes);
        const std::uint32_t perLayer = subTiles * subTiles;
        record_slot_copy(cmd, image, s.buffer, slot / perLayer,
                         std::int32_t(slot % subTiles * tileW),
                         std::int32_t(slot % perLayer / subTiles * tileH),
                         tileW, tileH);
        return true;
    }

    bool SpriteArray::upload_slot_now(const VulkanDevice& d,
                                      std::uint32_t slot,
                                      const std::uint8_t* rgba)
    {
        if (slot >= slot_count() || !rgba) return false;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(tileW) * tileH * 4;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!make_buffer(d, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                             | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &staging, &stagingMem))
            return false;
        void* mapped = nullptr;
        vkMapMemory(d.device, stagingMem, 0, bytes, 0, &mapped);
        std::memcpy(mapped, rgba, static_cast<std::size_t>(bytes));
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
        const std::uint32_t perLayer = subTiles * subTiles;
        record_slot_copy(cmd, image, staging, slot / perLayer,
                         std::int32_t(slot % subTiles * tileW),
                         std::int32_t(slot % perLayer / subTiles * tileH),
                         tileW, tileH);
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

    void SpriteArray::destroy(const VulkanDevice& d)
    {
        for (Staging& s : staging_) {
            if (s.buffer) {
                vkUnmapMemory(d.device, s.memory);
                vkDestroyBuffer(d.device, s.buffer, nullptr);
                vkFreeMemory(d.device, s.memory, nullptr);
            }
            s = Staging{};
        }
        staging_.clear();
        stagingCursor_ = 0;
        if (descriptorPool) {
            vkDestroyDescriptorPool(d.device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            descriptorSet = VK_NULL_HANDLE;
        }
        if (setLayout) {
            vkDestroyDescriptorSetLayout(d.device, setLayout, nullptr);
            setLayout = VK_NULL_HANDLE;
        }
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
