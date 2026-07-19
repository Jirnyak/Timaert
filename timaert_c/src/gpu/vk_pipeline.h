// Minimal fullscreen graphics pipeline for the Vulkan backend: no vertex input
// (the vertex shader emits a fullscreen triangle from gl_VertexIndex), dynamic
// viewport/scissor, one opaque color attachment, and an optional push-constant
// block visible to the fragment stage. The macro/subworld fragment passes are
// built on this shape.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanPipeline
    {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        // vertSpvPath / fragSpvPath are absolute paths to glslc-compiled .spv.
        // pushConstantBytes may be 0 (no push constants). descriptorSetLayout
        // may be VK_NULL_HANDLE (no descriptors) or a set-0 layout (e.g. a
        // combined image sampler).
        bool create(const VulkanDevice& dev, VkRenderPass renderPass,
                    const char* vertSpvPath, const char* fragSpvPath,
                    std::uint32_t pushConstantBytes,
                    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE);
        // 3D mesh pipeline: one interleaved vertex binding (stride + attrs),
        // optional depth test/write and alpha blend, and push constants visible
        // to both the vertex and fragment stages (e.g. MVP + lighting). Culls
        // back faces when cullBack (front face = counter-clockwise).
        bool create_mesh(const VulkanDevice& dev, VkRenderPass renderPass,
                         const char* vertSpvPath, const char* fragSpvPath,
                         std::uint32_t pushConstantBytes,
                         std::uint32_t vertexStride,
                         const VkVertexInputAttributeDescription* attrs,
                         std::uint32_t attrCount, bool instanced,
                         bool depthTest, bool depthWrite, bool blend,
                         bool cullBack,
                         VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE);
        // Depth-only shadow-caster pipeline: no colour attachment, depth test +
        // write, slope-scaled depth bias, push constants (e.g. the light MVP).
        bool create_shadow(const VulkanDevice& dev, VkRenderPass shadowPass,
                           const char* vertSpvPath, const char* fragSpvPath,
                           std::uint32_t pushConstantBytes,
                           std::uint32_t vertexStride,
                           const VkVertexInputAttributeDescription* attrs,
                           std::uint32_t attrCount, bool instanced);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
