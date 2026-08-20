// Player spellbook runtime state. Mirrors TS SpellBook from
// src/game/spells/spell-types.ts without depending on content/ or UI.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sm {

struct SpellBook {
    std::vector<std::string> learned;
    std::string activeSpellId;
    // Steps remaining, not seconds left (core/time.h): a fight is measured in
    // the simulation's own integer quantum, so a spell comes back at the same
    // pace whether the world clock above is racing or crawling.
    std::unordered_map<std::string, std::uint32_t> cooldowns;
    std::vector<std::string> sustainedActive;
    // Fractional mana owed by sustained spells, carried between steps: a drain
    // of 3 mana/second is 3/64 per step and the pool is an integer.
    float sustainedDrainCarry = 0.0f;
};

inline bool spellbook_has_learned(const SpellBook& book,
                                  std::string_view id) noexcept {
    for (const auto& known : book.learned) {
        if (known == id) return true;
    }
    return false;
}

inline bool spellbook_learn(SpellBook& book, const std::string& id) {
    if (spellbook_has_learned(book, id)) return false;
    book.learned.push_back(id);
    if (book.activeSpellId.empty()) book.activeSpellId = id;
    return true;
}

inline bool spellbook_set_active(SpellBook& book, std::string_view id) {
    if (!spellbook_has_learned(book, id)) return false;
    book.activeSpellId.assign(id.data(), id.size());
    return true;
}

inline bool spellbook_has_sustained(const SpellBook& book,
                                    std::string_view id) noexcept {
    for (const auto& active : book.sustainedActive) {
        if (active == id) return true;
    }
    return false;
}

inline bool spellbook_toggle_sustained(SpellBook& book,
                                       const std::string& id) {
    for (auto it = book.sustainedActive.begin();
         it != book.sustainedActive.end(); ++it) {
        if (*it == id) {
            book.sustainedActive.erase(it);
            if (book.sustainedActive.empty()) book.sustainedDrainCarry = 0.0f;
            return false;
        }
    }
    book.sustainedActive.push_back(id);
    return true;
}

} // namespace sm
