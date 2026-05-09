// Per-player spell book — known spells, cooldown timers, mana, current
// active. Ticks decrement cooldowns. Mirrors the player-facing portion of
// spell-casting.ts.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "content/spells/spell_types.h"

namespace sm {

struct SpellBook {
    std::vector<std::string>            known;
    std::string                         active;
    std::unordered_map<std::string, float> cooldowns;
    int   mana    = 100;
    int   maxMana = 100;
    float manaRegenPerSec = 1.5f;
};

void spellbook_learn(SpellBook& sb, const std::string& id);
bool spellbook_set_active(SpellBook& sb, const std::string& id);
bool spellbook_can_cast(const SpellBook& sb, const std::string& id);
bool spellbook_cast(ecs::World& w, SpellBook& sb, const std::string& id,
                    std::uint32_t playerId, float px, float py, float nx, float ny);
void spellbook_tick(SpellBook& sb, float dt);

} // namespace sm
