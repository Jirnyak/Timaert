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

    // (Re)upload the world data textures (master + feature + zone + river) plus
    // the optional per-cell RGB night-light field (macro_lighting bake). The
    // field is width*height RGBA8; pass nullptr / 0,0 for no lights (a 1x1 black
    // field is bound so binding 4 is always valid). Load-time / on-world-change
    // only — never per frame.
    void upload(const gpu::VulkanDevice& dev, const TerrainData& td,
                const FeatureLayer& features, const ZoneLayer& zones,
                const std::uint8_t* lightFieldRgba = nullptr,
                std::uint32_t lightFieldW = 0, std::uint32_t lightFieldH = 0);

    // Surgically re-upload ONLY the per-cell night-light field (binding 4),
    // leaving the master/feature/zone/river textures untouched. Refreshes night
    // glow when the world state that drives it changes mid-session (settlements
    // loaded from a save, populations drifting across the daily economy tick)
    // without the cost of a full world re-upload. No-op until upload() has run
    // (the descriptor set must already be bound). Pass nullptr / 0,0 to reset to
    // the 1x1 black field.
    void upload_light_field(const gpu::VulkanDevice& dev,
                            const std::uint8_t* lightFieldRgba,
                            std::uint32_t lightFieldW, std::uint32_t lightFieldH);

    // Record the fullscreen map draw for the current framebuffer.
    void record(VkCommandBuffer cmd, VkExtent2D ext, const TerrainData& td,
                float camX, float camY, float zoom, float seaLevel,
                float timeOfDay);

    bool ready() const { return uploaded_; }

private:
    void free_textures(const gpu::VulkanDevice& dev);

    gpu::VulkanTexture master_{}, feature_{}, zone_{}, river_{};
    gpu::VulkanTexture lightField_{};  // per-cell RGB night glow (binding 4)
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    gpu::VulkanPipeline pipeline_{};
    std::vector<std::uint8_t> scratch_;  // FeatureLayer sanitize scratch
    bool uploaded_ = false;
};

} // namespace sm
