// THE spellbook — a BODY's knowledge of the append-only spell registry
// (macro/spells.h), indexed by ordinal. An ECS component since §41 root 3:
// it existed in ONE copy (PlayerState::spellBook) — literally the class
// combatStats was before landing 4 — so only the player could cast, only he
// paid sustained drains, only he could be taught by a spire. Now every macro
// body is born with a book (all zeros = knows nothing), it rides the macro
// snapshot like every other component, and «НПЦ-маг» is a body whose book
// says so, not a second ranged-attack system.
//
// It was three heap containers keyed by STRING ids (vector<string> learned,
// unordered_map<string,u32> cooldowns, vector<string> sustained) — exactly
// the shape CANON S26 forbids on an entity (v59 flattened it). The rows are
// BITSETS now: «выучен/не выучен» is binary by the owner's verdict
// (2026-09-10), and the envelope below is his number.
#pragma once

#include <cstdint>

#include "macro/spells.h"

namespace sm {

// THE spell-capacity envelope (owner verdict 2026-09-10: «точно не больше
// 256 спеллов»): the ceiling on DIFFERENT spells the registry may ever hold
// — not a per-body limit; a body may learn all of them. 256 bits = 32 B a
// row, 16384 bodies × 72 B of book ≈ 1.2 MB — DOD money (CANON S26). If the
// registry ever outgrows it, this assert fails LOUDLY and the arrays below
// grow by one stride each; nothing truncates silently.
inline constexpr int kSpellBookCapacity = 256;
static_assert(kSpellCount <= kSpellBookCapacity,
              "the spell registry outgrew the owner's 256-spell envelope: "
              "widen kSpellBookCapacity (32 B per row per body)");

struct SpellBook {
    std::uint64_t learned[kSpellBookCapacity / 64] = {};    // bit per row
    std::int32_t  activeSpell = -1;                         // ordinal; -1 = none
    // No per-spell timers (owner verdict 2026-09-09, v83): a cast charges the
    // BODY's one recovery gate (ecs::Combat::recoverySteps) — the same field
    // a sword swing charges — so «occupied» is one fact with one home, and a
    // book row never counts fight time on its own.
    std::uint64_t sustained[kSpellBookCapacity / 64] = {};  // bit per drain
    // Fractional mana owed by sustained spells, carried between steps: a drain
    // of 3 mana/second is 3/64 per step and the pool is an integer.
    float sustainedDrainCarry = 0.0f;
};

inline constexpr bool spell_ordinal_ok(int ord) noexcept {
    return ord >= 0 && ord < kSpellCount;
}

// The two bit planes speak ONLY through these (no consumer indexes a raw
// array): one bit-math site, and the planes cannot be confused.
inline bool spellbook_bit(const std::uint64_t* plane, int ord) noexcept {
    return (plane[ord >> 6] >> (ord & 63)) & 1u;
}
inline void spellbook_bit_set(std::uint64_t* plane, int ord, bool on) noexcept {
    const std::uint64_t m = std::uint64_t(1) << (ord & 63);
    if (on) plane[ord >> 6] |= m;
    else    plane[ord >> 6] &= ~m;
}

inline bool spellbook_has_learned(const SpellBook& book, int ord) noexcept {
    return spell_ordinal_ok(ord) && spellbook_bit(book.learned, ord);
}

inline bool spellbook_learn(SpellBook& book, int ord) {
    if (!spell_ordinal_ok(ord) || spellbook_bit(book.learned, ord))
        return false;
    spellbook_bit_set(book.learned, ord, true);
    if (book.activeSpell < 0) book.activeSpell = ord;
    return true;
}

inline bool spellbook_set_active(SpellBook& book, int ord) {
    if (!spellbook_has_learned(book, ord)) return false;
    book.activeSpell = ord;
    return true;
}

inline bool spellbook_has_sustained(const SpellBook& book, int ord) noexcept {
    return spell_ordinal_ok(ord) && spellbook_bit(book.sustained, ord);
}

// Is a RULE of the world switched on in this book? A rule has no magnitude,
// so this is a scan of the sustained rows against the registry's rule column
// — a SECOND spell that grants Flight is one registry row away (CANON S16),
// and every consumer of the rule sees it without naming a spell.
inline bool spellbook_rule_active(const SpellBook& book,
                                  SpellRuleId rule) noexcept {
    if (rule == SpellRuleId::None) return false;
    for (int ord = 0; ord < kSpellCount; ++ord) {
        if (!spellbook_bit(book.sustained, ord)) continue;
        if (kSpellDefs[ord].rule == rule) return true;
    }
    return false;
}

inline bool spellbook_any_sustained(const SpellBook& book) noexcept {
    for (int i = 0; i < kSpellBookCapacity / 64; ++i) {
        if (book.sustained[i]) return true;
    }
    return false;
}

inline int spellbook_learned_count(const SpellBook& book) noexcept {
    int n = 0;
    for (int ord = 0; ord < kSpellCount; ++ord)
        n += spellbook_bit(book.learned, ord) ? 1 : 0;
    return n;
}

// Returns the NEW state: true = now active.
inline bool spellbook_toggle_sustained(SpellBook& book, int ord) {
    if (!spell_ordinal_ok(ord)) return false;
    if (spellbook_bit(book.sustained, ord)) {
        spellbook_bit_set(book.sustained, ord, false);
        if (!spellbook_any_sustained(book)) book.sustainedDrainCarry = 0.0f;
        return false;
    }
    spellbook_bit_set(book.sustained, ord, true);
    return true;
}

} // namespace sm
