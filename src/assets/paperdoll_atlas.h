// GPU-resident paper-doll sprite pool. Composes 48x48 frames on the CPU (via
// the shared compose_paperdoll_rgba8) and stores each UNIQUE composited frame
// as one layer of a gpu::SpriteArray, keyed by paperdoll_frame_key with LRU
// eviction. The result: ONE image, ONE sampler, ONE descriptor set for every
// character / monster / NPC frame on screen — and ONE texture fetch per
// fragment in npc.frag / shadow_npc.frag.
//
// This is the "bank" half of the subworld's universal sprite law: a sprite
// whose pixels are expensive to derive (a 37-layer palette composite, drawn
// art) is resolved to texels ONCE per unique frame and drawn many times;
// a sprite whose pixels are cheap math (trees, procedural creatures) stays
// inline in its fragment stage. The per-fragment compositing experiment
// (7cd71e2) paid the full 37-layer loop per PIXEL per FRAME, so one close-up
// NPC covering the screen cost millions of composites — the pool pays per
// unique frame KEY, a working set of dozens.
//
// Per-frame contract:
//   begin_frame();                        // once, at frame start
//   ... layer_for(cmd, desc, anim) ...    // resolve every visible frame
//                                         // (records layer uploads on `cmd`;
//                                         //  MUST be before begin_render_pass)
//   ... draw, binding descriptor_set() ...
#pragma once

#include "assets/character_paperdoll.h"
#include "gpu/vk_sprite_array.h"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace gpu {
struct VulkanDevice;
} // namespace gpu

namespace sm::character {

class PaperdollAtlas {
public:
    // layer_for() when the pool cannot serve this frame (staging ring out, or
    // every layer already used this frame): caller skips the sprite this frame
    // and it resolves on the next one.
    static constexpr std::uint32_t kNoLayer = 0xFFFFFFFFu;

    bool init(const gpu::VulkanDevice& dev);
    void destroy(const gpu::VulkanDevice& dev);

    const CharacterDescriptor& descriptor_for_seed(
        std::uint32_t seed, AppearancePreset preset = AppearancePreset::None);

    // Once per frame, BEFORE any layer_for(): advances the LRU clock and
    // resets the upload staging ring.
    void begin_frame();

    // Resolve one composited frame to its resident pool layer. On a cache miss
    // the frame is composed on the CPU and its upload is recorded on `cmd`
    // (before the render pass opens — a layout transition is illegal inside
    // one). Returns kNoLayer if the pool cannot take the frame right now.
    std::uint32_t layer_for(VkCommandBuffer cmd,
                            const CharacterDescriptor& descriptor,
                            const AnimationState& animation);

    // Load-time blocking resolve (own submit + fence per upload): for
    // preloading a batch before the first frame — the smoke-harness path.
    // Never call per frame. A fresh pool assigns layers 0, 1, 2, … in call
    // order, which is what lets a static scene bake layer indices into its
    // instance buffer up front.
    std::uint32_t layer_for_now(const gpu::VulkanDevice& dev,
                                const CharacterDescriptor& descriptor,
                                const AnimationState& animation);

    VkDescriptorSetLayout set_layout() const { return pool_.setLayout; }
    VkDescriptorSet descriptor_set() const { return pool_.descriptorSet; }

private:
    bool load_atlas();
    // Claim a pool layer for `key` (fresh first, else LRU-evict) and compose
    // the frame into composeScratch_. kNoLayer if every layer is in use this
    // frame. The caller uploads the scratch and, on success, commit_layer()s.
    std::uint32_t claim_and_compose(std::uint64_t key,
                                    const CharacterDescriptor& descriptor,
                                    const AnimationState& animation);
    void commit_layer(std::uint64_t key, std::uint32_t layer);
    void release_layer(std::uint32_t layer);

    // Resident working set: 1024 composited 48x48 frames ≈ 9.4 MB VRAM.
    // A frame holds every VISIBLE (descriptor, animation, direction, frame)
    // combination — dozens to a few hundred; eviction touches only sprites
    // that left the screen long ago.
    static constexpr std::uint32_t kPoolLayers = 1024;
    // Uploads recordable per frame: a whole town walking into view lands in
    // one frame (128 × 9 KiB staging = ~1.2 MiB host memory).
    static constexpr std::uint32_t kStagingRing = 128;

    gpu::SpriteArray pool_{};

    AtlasData atlas_{};
    // Kept resident for the lifetime of the atlas: the CPU compositor reads
    // straight from the decoded PNG on every cache miss.
    std::vector<std::uint8_t> atlasPixels_;
    int atlasW_ = 0;
    int atlasH_ = 0;
    bool loadAttempted_ = false;
    bool loaded_ = false;

    std::unordered_map<std::uint64_t, CharacterDescriptor> descriptorCache_;

    // LRU state: frame key -> layer, plus per-layer back-pointers so eviction
    // can unmap its victim. stamp_ is the frame clock; a layer stamped this
    // frame is in use and never evicted.
    std::unordered_map<std::uint64_t, std::uint32_t> frameToLayer_;
    std::vector<std::uint64_t> layerKey_;
    std::vector<std::uint32_t> layerStamp_;
    std::uint32_t stamp_ = 0;
    std::uint32_t nextFreshLayer_ = 0;

    std::vector<std::uint8_t> composeScratch_;
};

} // namespace sm::character
