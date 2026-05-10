#include "content/spells/spell_book.h"

namespace sm {

bool spellbook_can_cast(const SpellBook& sb, const CombatStats& combat,
                        const std::string& id) {
    const SpellDef* d = spell_registry().find(id);
    if (!d) return false;
    if (!spellbook_has_learned(sb, id)) return false;
    if (d->sustained && spellbook_has_sustained(sb, id)) return true;
    if (combat.currentMp < d->manaCost) return false;
    auto it = sb.cooldowns.find(id);
    if (it != sb.cooldowns.end() && it->second > 0.0f) return false;
    return true;
}

int spellbook_start_cast(SpellBook& sb, CombatStats& combat,
                         const std::string& id) {
    const SpellDef* d = spell_registry().find(id);
    if (!d) return 0;
    if (d->sustained) {
        spellbook_toggle_sustained(sb, id);
        return 0;
    }
    combat.currentMp -= d->manaCost;
    if (combat.currentMp < 0) combat.currentMp = 0;
    if (d->cooldown > 0.0f) {
        sb.cooldowns[id] = d->cooldown;
    }
    return d->manaCost;
}

bool spellbook_cast(ecs::World& w, SpellBook& sb, CombatStats& combat,
                    const std::string& id,
                    std::uint32_t pid, float px, float py, float nx, float ny) {
    if (!spellbook_can_cast(sb, combat, id)) return false;
    const SpellDef* d = spell_registry().find(id);
    if (!d) return false;
    if (d->sustained) {
        spellbook_start_cast(sb, combat, id);
        return true;
    }
    if (!cast_spell(w, id, pid, px, py, nx, ny)) return false;
    spellbook_start_cast(sb, combat, id);
    return true;
}

void spellbook_tick(SpellBook& sb, CombatStats& combat, float dt) {
    for (auto it = sb.cooldowns.begin(); it != sb.cooldowns.end(); ) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = sb.cooldowns.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = sb.sustainedActive.begin();
         it != sb.sustainedActive.end(); ) {
        const SpellDef* d = spell_registry().find(*it);
        if (!d || !d->sustained) {
            it = sb.sustainedActive.erase(it);
            continue;
        }
        const int drain = static_cast<int>(d->manaDrain * dt);
        if (drain <= 0) {
            ++it;
            continue;
        }
        if (combat.currentMp >= drain) {
            combat.currentMp -= drain;
            ++it;
        } else {
            combat.currentMp = 0;
            it = sb.sustainedActive.erase(it);
        }
    }
}

} // namespace sm
