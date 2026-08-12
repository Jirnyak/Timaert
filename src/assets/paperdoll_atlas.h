// GPU-resident paper-doll sprite pool. Composes 48x48 frames on the CPU (via
// the shared compose_paperdoll_rgba8) and stores each UNIQUE composited frame
// as one SLOT of a gpu::SpriteArray (2×2 slots per 96×96 array layer), keyed
// by paperdoll_frame_key with LRU eviction. The result: ONE image, ONE
// sampler, ONE descriptor set for every character / monster / NPC frame on
// screen — and ONE texture fetch per fragment in npc.frag / shadow_npc.frag.
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
//   ... slot_for(cmd, desc, anim) ...     // resolve every visible frame
//                                         // (records slot uploads on `cmd`;
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
    // slot_for() when the pool cannot serve this frame (staging ring out, or
    // every slot already used this frame): caller skips the sprite this frame
    // and it resolves on the next one.
    static constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;

    // Part of the pool's contract with its renderer: the staging ring is
    // sliced into this many per-frame windows (gpu::SpriteArray), and the
    // value must equal the renderer's frames-in-flight — pinned by a
    // static_assert at the init call site in vk_renderer_3d.cpp.
    static constexpr std::uint32_t kPoolFramesInFlight = 2;

    bool init(const gpu::VulkanDevice& dev);
    void destroy(const gpu::VulkanDevice& dev);

    const CharacterDescriptor& descriptor_for_seed(
        std::uint32_t seed, AppearancePreset preset = AppearancePreset::None);

    // Once per frame, BEFORE any slot_for(): advances the LRU clock and
    // resets the upload staging ring.
    void begin_frame();

    // Resolve one composited frame to its resident pool SLOT — the index the
    // renderer writes into BbInstance.kind and shaders/doll_pool.glsl decodes
    // (slot = layer·4 + quadrant). On a cache miss the frame is composed on
    // the CPU and its upload is recorded on `cmd` (before the render pass
    // opens — a layout transition is illegal inside one). Returns kNoSlot if
    // the pool cannot take the frame right now.
    std::uint32_t slot_for(VkCommandBuffer cmd,
                           const CharacterDescriptor& descriptor,
                           const AnimationState& animation);

    // Same resolve with a CALLER-SUPPLIED cache key. For callers whose
    // descriptor is a pure function of a small id (the renderer's 256
    // quantized seeds), a bijective bit-packing of (id, animation state) is a
    // perfect key — computing it is free, while the default key hashes the
    // whole 70-byte descriptor per call, which a city of thousands paid every
    // frame (the `rec=9..25ms` column of 2026-08-10). The caller owns key
    // uniqueness: equal keys MUST mean identical composited pixels.
    std::uint32_t slot_for_keyed(VkCommandBuffer cmd, std::uint64_t key,
                                 const CharacterDescriptor& descriptor,
                                 const AnimationState& animation);

    // Load-time blocking resolve (own submit + fence per upload): for
    // preloading a batch before the first frame — the smoke-harness path.
    // Never call per frame. A fresh pool assigns slots 0, 1, 2, … in call
    // order, which is what lets a static scene bake slot indices into its
    // instance buffer up front.
    std::uint32_t slot_for_now(const gpu::VulkanDevice& dev,
                               const CharacterDescriptor& descriptor,
                               const AnimationState& animation);

    VkDescriptorSetLayout set_layout() const { return pool_.setLayout; }
    VkDescriptorSet descriptor_set() const { return pool_.descriptorSet; }

private:
    bool load_atlas();
    // Claim a pool slot for `key` (fresh first, else LRU-evict) and compose
    // the frame into composeScratch_. kNoSlot if every slot is in use this
    // frame. The caller uploads the scratch and, on success, commit_slot()s.
    std::uint32_t claim_and_compose(std::uint64_t key,
                                    const CharacterDescriptor& descriptor,
                                    const AnimationState& animation);
    void commit_slot(std::uint64_t key, std::uint32_t slot);
    void release_slot(std::uint32_t slot);

    // Resident working set. The demand is bounded by the key space the
    // renderer can ask for — 256 quantized seeds × (Idle 4 dir × 4 frames +
    // Walk 4 dir × 6 frames) = 10 240 unique crowd frames — and a 5k-body
    // settlement MEASURES ~4.3k of them active per frame (2026-08-13). The
    // old 2048-frame pool sat BELOW its own steady-state demand, so the LRU
    // thrashed forever: 128 recomposes every warm frame (the staging cap) and
    // ~1.1k bodies/frame degraded to the canonical fallback pose.
    // MoltenVK caps maxImageArrayLayers at 2048, so capacity grows INSIDE the
    // layer: kSubTiles×kSubTiles frames per 96×96 layer, slot = layer·4+quad
    // (decoded by shaders/doll_pool.glsl — shared-shader contract).
    // 2048 layers × 4 = 8192 slots ≈ 75.5 MB VRAM, ~2× the measured demand.
    static constexpr std::uint32_t kPoolLayers = 2048;
    static constexpr std::uint32_t kSubTiles = 2;
    static constexpr std::uint32_t kPoolSlots =
        kPoolLayers * kSubTiles * kSubTiles;
    // Staging ring is SLICED per frame in flight (gpu::SpriteArray): 256
    // slots / 2 frames = 128 uploads recordable per frame — a whole town
    // walking into view lands in one frame (256 × 9 KiB = ~2.3 MiB host).
    static constexpr std::uint32_t kStagingRing = 256;

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

    // LRU state: frame key -> slot, plus per-slot back-pointers so eviction
    // can unmap its victim. stamp_ is the frame clock; a slot stamped this
    // frame is in use and never evicted.
    std::unordered_map<std::uint64_t, std::uint32_t> keyToSlot_;
    std::vector<std::uint64_t> slotKey_;
    std::vector<std::uint32_t> slotStamp_;
    std::uint32_t stamp_ = 0;
    std::uint32_t nextFreshSlot_ = 0;

    // TIMAERT_DOLL_STATS telemetry (begin_frame prints + resets).
    std::uint32_t statHits_ = 0;
    std::uint32_t statMisses_ = 0;
    std::uint32_t statNoSlot_ = 0;

    std::vector<std::uint8_t> composeScratch_;
};

} // namespace sm::character
