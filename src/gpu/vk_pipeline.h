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
        //
        // When `additive` (only meaningful with blend=true): the destination
        // colour factor is VK_BLEND_FACTOR_ONE instead of ONE_MINUS_SRC_ALPHA,
        // so fragments ACCUMULATE onto the framebuffer (src·srcAlpha + dst).
        // This is order-independent — no back-to-front sort needed — and is the
        // glow/emissive look used by the particle/FX pass. Left false everywhere
        // else, preserving the straight-alpha (SRC_ALPHA / ONE_MINUS_SRC_ALPHA)
        // behaviour of water and NPC billboards.
        bool create_mesh(const VulkanDevice& dev, VkRenderPass renderPass,
                         const char* vertSpvPath, const char* fragSpvPath,
                         std::uint32_t pushConstantBytes,
                         std::uint32_t vertexStride,
                         const VkVertexInputAttributeDescription* attrs,
                         std::uint32_t attrCount, bool instanced,
                         bool depthTest, bool depthWrite, bool blend,
                         bool cullBack,
                         VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE,
                         bool additive = false);
        // Multi-set overload: accepts N descriptor set layouts (set=0..N-1).
        bool create_mesh(const VulkanDevice& dev, VkRenderPass renderPass,
                         const char* vertSpvPath, const char* fragSpvPath,
                         std::uint32_t pushConstantBytes,
                         std::uint32_t vertexStride,
                         const VkVertexInputAttributeDescription* attrs,
                         std::uint32_t attrCount, bool instanced,
                         bool depthTest, bool depthWrite, bool blend,
                         bool cullBack,
                         const VkDescriptorSetLayout* setLayouts,
                         std::uint32_t setLayoutCount,
                         bool additive = false);
        // Depth-only shadow-caster pipeline: no colour attachment, depth test +
        // write, slope-scaled depth bias, push constants (e.g. the light MVP).
        bool create_shadow(const VulkanDevice& dev, VkRenderPass shadowPass,
                           const char* vertSpvPath, const char* fragSpvPath,
                           std::uint32_t pushConstantBytes,
                           std::uint32_t vertexStride,
                           const VkVertexInputAttributeDescription* attrs,
                           std::uint32_t attrCount, bool instanced,
                           VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE);
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu
