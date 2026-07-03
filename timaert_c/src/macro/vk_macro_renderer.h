// Vulkan macro-map renderer — draws shaders/macro.frag (the procedural 2D world
// map fragment synth) from the CPU climate master + feature/zone/river byte
// grids. Replaces the GL MacroRenderer at the Vulkan cutover; compiles
// alongside it beforehand. Backend = gpu/ (Vulkan); this is an L1 macro concept.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "gpu/vk_pipeline.h"
#include "gpu/vk_texture.h"

namespace gpu { struct VulkanDevice; }

namespace sm {

struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;

class MacroRendererVk {
public:
    bool init(const gpu::VulkanDevice& dev, VkRenderPass pass);
    void destroy(const gpu::VulkanDevice& dev);

    // (Re)upload the four world data textures (master + feature + zone + river).
    // Load-time / on-world-change only — never per frame.
    void upload(const gpu::VulkanDevice& dev, const TerrainData& td,
                const FeatureLayer& features, const ZoneLayer& zones);

    // Record the fullscreen map draw for the current framebuffer.
    void record(VkCommandBuffer cmd, VkExtent2D ext, const TerrainData& td,
                float camX, float camY, float zoom, float seaLevel,
                float timeOfDay);

    bool ready() const { return uploaded_; }

private:
    void free_textures(const gpu::VulkanDevice& dev);

    gpu::VulkanTexture master_{}, feature_{}, zone_{}, river_{};
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    gpu::VulkanPipeline pipeline_{};
    std::vector<std::uint8_t> scratch_;  // FeatureLayer sanitize scratch
    bool uploaded_ = false;
};

} // namespace sm
