// Player spellbook runtime state — FLAT rows over the append-only spell
// registry (macro/spells.h), indexed by ordinal.
//
// It was three heap containers keyed by STRING ids (vector<string> learned,
// unordered_map<string,u32> cooldowns, vector<string> sustained) — exactly
// the shape CANON S26 forbids on an entity: allocating, cache-hostile, and a
// string is not an identity (S20.1: the ordinal of an append-only registry
// is). Now every row of the registry has its cell here, zero allocations,
// and the save writes the block ordinal-for-ordinal (v59).
#pragma once

#include <cstdint>

#include "macro/spells.h"

namespace sm {

struct SpellBook {
    std::uint8_t  learned[kSpellCount] = {};        // 0/1 per registry row
    std::int32_t  activeSpell = -1;                 // ordinal; -1 = none
    // Steps remaining, not seconds left (core/time.h): a fight is measured in
    // the simulation's own integer quantum, so a spell comes back at the same
    // pace whether the world clock above is racing or crawling.
    std::uint32_t cooldownSteps[kSpellCount] = {};
    std::uint8_t  sustained[kSpellCount] = {};      // 0/1 active drains
    // Fractional mana owed by sustained spells, carried between steps: a drain
    // of 3 mana/second is 3/64 per step and the pool is an integer.
    float sustainedDrainCarry = 0.0f;
};

inline constexpr bool spell_ordinal_ok(int ord) noexcept {
    return ord >= 0 && ord < kSpellCount;
}

inline bool spellbook_has_learned(const SpellBook& book, int ord) noexcept {
    return spell_ordinal_ok(ord) && book.learned[ord] != 0;
}

inline bool spellbook_learn(SpellBook& book, int ord) {
    if (!spell_ordinal_ok(ord) || book.learned[ord]) return false;
    book.learned[ord] = 1;
    if (book.activeSpell < 0) book.activeSpell = ord;
    return true;
}

inline bool spellbook_set_active(SpellBook& book, int ord) {
    if (!spellbook_has_learned(book, ord)) return false;
    book.activeSpell = ord;
    return true;
}

inline bool spellbook_has_sustained(const SpellBook& book, int ord) noexcept {
    return spell_ordinal_ok(ord) && book.sustained[ord] != 0;
}

inline bool spellbook_any_sustained(const SpellBook& book) noexcept {
    for (int i = 0; i < kSpellCount; ++i) {
        if (book.sustained[i]) return true;
    }
    return false;
}

inline int spellbook_learned_count(const SpellBook& book) noexcept {
    int n = 0;
    for (int i = 0; i < kSpellCount; ++i) n += book.learned[i] ? 1 : 0;
    return n;
}

// Returns the NEW state: true = now active.
inline bool spellbook_toggle_sustained(SpellBook& book, int ord) {
    if (!spell_ordinal_ok(ord)) return false;
    if (book.sustained[ord]) {
        book.sustained[ord] = 0;
        if (!spellbook_any_sustained(book)) book.sustainedDrainCarry = 0.0f;
        return false;
    }
    book.sustained[ord] = 1;
    return true;
}

} // namespace sm
