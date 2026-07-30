// GPU-resident paper-doll sprite pool. Composes 48x48 frames on the CPU (via
// the shared compose_paperdoll_rgba8) and stores each UNIQUE composited frame
// as one layer of a gpu::SpriteArray, keyed by paperdoll_frame_key with LRU
// eviction. The result: ONE image, ONE sampler, ONE descriptor set for every
// character / monster / NPC frame on screen — replacing the per-frame-texture
// path that exhausted the descriptor pool and stalled the queue (problems.md).
//
// The same pool feeds the subworld 3D renderer, the macro map, and (via a
// per-portrait 2D view) the UI — one unified paper-doll system for macro and
// micro worlds, extensible to an unknown number of new monster sprites by the
// same human model (just more layers, no engine change).
//
// Per-frame contract:
//   begin_frame();                        // once, at frame start
//   ... layer_for(desc, anim) ...         // resolve every visible frame
//   flush_uploads(cmd);                   // BEFORE begin_render_pass()
//   ... draw, binding descriptor_set() ...
#pragma once

#include "assets/character_paperdoll.h"
#include "gpu/vk_buffer.h"
#include "gpu/vk_texture.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

struct GpuAtlasEntry {
    std::uint32_t uv_wh;
    std::uint32_t ox_oy;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

struct GpuCharacterDescriptor {
    std::uint32_t headAndBody;
    std::uint32_t hairAndEyes;
    std::uint32_t bottomAndTop;
    std::uint32_t armsAndLegs;
    std::uint32_t feetAndBack;
    std::uint32_t beardAndPaletteHead;
    std::uint32_t paletteHairEyesBottomTop;
    std::uint32_t paletteArmsLegsFeetBack;
};

namespace gpu {
struct VulkanDevice;
} // namespace gpu

namespace sm::character {

class PaperdollAtlas {
public:
    bool init(const gpu::VulkanDevice& dev);
    void destroy(const gpu::VulkanDevice& dev);

    // Register a descriptor and get its stable SSBO index. This index is valid
    // as long as the atlas exists. Safe to call per-frame.
    std::uint32_t register_descriptor(const CharacterDescriptor& descriptor);

    const CharacterDescriptor& descriptor_for_seed(
        std::uint32_t seed, AppearancePreset preset = AppearancePreset::None);

    const std::vector<GpuAtlasEntry>& gpu_entries() const { return gpuEntries_; }
    const std::vector<std::int32_t>& gpu_sheet_ordinals() const { return gpuSheetOrdinals_; }
    const std::vector<GpuCharacterDescriptor>& gpu_descriptors() const { return gpuDescriptors_; }

    VkDescriptorSetLayout set_layout() const { return setLayout_; }
    VkDescriptorSet descriptor_set() const { return descSet_; }

private:
    bool load_atlas();

    gpu::VulkanTexture atlasTex_{};
    gpu::VulkanBuffer  entriesSsbo_{};
    gpu::VulkanBuffer  ordinalsSsbo_{};
    gpu::VulkanBuffer  descriptorsSsbo_{};

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_  = VK_NULL_HANDLE;
    VkDescriptorSet       descSet_   = VK_NULL_HANDLE;

    AtlasData atlas_{};
    std::vector<std::uint8_t> atlasPixels_;
    int atlasW_ = 0;
    int atlasH_ = 0;
    bool loadAttempted_ = false;
    bool loaded_ = false;

    std::vector<GpuAtlasEntry> gpuEntries_;
    std::vector<std::int32_t> gpuSheetOrdinals_;
    std::vector<GpuCharacterDescriptor> gpuDescriptors_;
    std::unordered_map<std::uint64_t, CharacterDescriptor> descriptorCache_;
    std::unordered_map<std::uint64_t, std::uint32_t> gpuDescriptorCache_;
};

} // namespace sm::character
