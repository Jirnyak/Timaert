// THE drawn-sprite bank: every body picture the artist drew, resident on the
// GPU as one image with one sampler and ONE descriptor set (gpu::SpriteArray).
// A billboard names a slot; the fragment stage samples it. That is the whole
// mechanism.
//
// It replaced the paper-doll pool, and the difference is the point of the
// sprite law (sprites.md). The pool composed 37 layers per (seed, animation,
// direction, frame) and needed 8192 slots for one city because a face was
// generated per SOUL. A drawn kind is one picture for every member of that
// kind, so the working set is not a crowd size — it is the number of PICTURES
// in the table. Today: five.
//
// Which rows get a slot is DERIVED, never chosen: a row that can stand in the
// world as a body (`archetype != kNoBody`) and names drawn art. Places and
// interface marks are drawn flat by ImGui out of assets/sprite_atlas and never
// enter the 3D world, so they cost nothing here.
//
// Every slot is filled ONCE at load, before the first frame, through the
// blocking upload path that exists for exactly this. There is no LRU, no
// eviction, no per-frame staging and no cold start: the set is complete from
// boot and never changes, which is what made the old pool's whole ring, cursor
// and fallback-to-canonical-frame machinery necessary.
#pragma once

#include "gpu/vk_sprite_array.h"
#include "macro/sprite_rows.h"

#include <cstdint>

namespace gpu { struct VulkanDevice; }

namespace sm {

// Slot side in texels. The artist's body sheets are authored at this size
// (assets/sprites/*_256.png), and the bank stores art at its authored
// resolution: rescaling on load would throw away pixels the artist drew, and
// there is no VRAM argument against it at this count — five slots is 1.3 MB.
inline constexpr std::uint32_t kSpriteBankTile = 256;

// How many rows can stand in the world with drawn art. Derived from the table
// itself, so adding a drawn body grows the bank by exactly one slot and no
// constant anywhere needs to be revisited.
inline constexpr std::uint32_t sprite_bank_slot_count() {
    std::uint32_t n = 0;
    for (const SpriteDef& row : kSpriteRows)
        if (row.asset != nullptr && row.archetype != kNoBody) ++n;
    return n;
}

class SpriteBank {
public:
    // No slot for this row — it has no drawn body. The caller draws the
    // procedural silhouette instead, which is the law, not a failure.
    static constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;

    // Decode every drawn body once and upload it. Call before the first frame.
    bool init(const gpu::VulkanDevice& dev);
    void destroy(const gpu::VulkanDevice& dev);

    // The row's resident slot, or kNoSlot.
    std::uint32_t slot_for(SpriteId id) const;

    bool ready() const { return ready_; }
    VkDescriptorSetLayout set_layout() const { return pool_.setLayout; }
    VkDescriptorSet descriptor_set() const { return pool_.descriptorSet; }

private:
    gpu::SpriteArray pool_{};
    // Row → slot, dense over the drawn-body rows. kNoSlot for every other row.
    std::uint32_t slotOf_[std::size_t(SpriteId::Count_)]{};
    bool ready_ = false;
};

} // namespace sm
