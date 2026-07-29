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
#include "gpu/vk_sprite_array.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gpu { struct VulkanDevice; }

namespace sm::character {

class PaperdollAtlas {
public:
    // Create the 48x48 sprite pool (NEAREST, art-locked to kLogicalTileSize).
    // The atlas image/bin is loaded lazily on first layer_for so a missing
    // asset set does not fail engine init.
    bool init(const gpu::VulkanDevice& dev, std::uint32_t layerCount = 1024,
              std::uint32_t stagingRing = 128);
    void destroy(const gpu::VulkanDevice& dev);
    bool atlas_loaded() const { return loaded_; }

    // Memoised, deterministic descriptor generation (seed [+ appearance]).
    const CharacterDescriptor& descriptor_for_seed(
        std::uint32_t seed, AppearancePreset preset = AppearancePreset::None);

    // Resident layer index for a composited frame. On a miss, composes the
    // frame and assigns a layer (queued for upload at flush_uploads). Returns
    // -1 if the atlas failed to load, composition produced nothing, or every
    // layer is already needed this frame (pool saturated).
    int layer_for(const CharacterDescriptor& descriptor,
                  const AnimationState& animation);

    void begin_frame();
    // Record queued layer uploads onto `cmd`. MUST run before the render pass.
    void flush_uploads(VkCommandBuffer cmd);

    // Bind targets for any pipeline that samples the pool via sampler2DArray.
    VkDescriptorSetLayout set_layout() const { return sprite_.setLayout; }
    VkDescriptorSet descriptor_set() const { return sprite_.descriptorSet; }
    std::uint32_t layer_capacity() const { return sprite_.layerCount; }

    // For the UI per-portrait view adapter (step 5).
    const gpu::SpriteArray& sprite_array() const { return sprite_; }

private:
    bool load_atlas();

    gpu::SpriteArray sprite_{};
    AtlasData atlas_{};
    std::vector<std::uint8_t> atlasPixels_;
    int atlasW_ = 0;
    int atlasH_ = 0;
    bool loadAttempted_ = false;
    bool loaded_ = false;

    struct Slot {
        std::uint64_t key = 0;
        std::uint32_t lastUsed = 0;
        bool occupied = false;
    };
    std::vector<Slot> slots_;
    std::unordered_map<std::uint64_t, std::uint32_t> keyToSlot_;
    std::uint32_t frameCounter_ = 0;

    struct Pending {
        std::uint32_t layer = 0;
        std::array<std::uint8_t,
                   std::size_t(kLogicalTileSize) * kLogicalTileSize * 4u> px{};
    };
    std::vector<Pending> pending_;

    std::unordered_map<std::uint64_t, CharacterDescriptor> descriptorCache_;
};

} // namespace sm::character
