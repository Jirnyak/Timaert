#include "content/spells/spell_book.h"

namespace sm {

void spellbook_learn(SpellBook& sb, const std::string& id) {
    for (auto& k : sb.known) if (k == id) return;
    sb.known.push_back(id);
    if (sb.active.empty()) sb.active = id;
}

bool spellbook_set_active(SpellBook& sb, const std::string& id) {
    for (auto& k : sb.known) if (k == id) { sb.active = id; return true; }
    return false;
}

bool spellbook_can_cast(const SpellBook& sb, const std::string& id) {
    const SpellDef* d = spell_registry().find(id);
    if (!d) return false;
    if (sb.mana < d->manaCost) return false;
    auto it = sb.cooldowns.find(id);
    if (it != sb.cooldowns.end() && it->second > 0.0f) return false;
    return true;
}

bool spellbook_cast(ecs::World& w, SpellBook& sb, const std::string& id,
                    std::uint32_t pid, float px, float py, float nx, float ny) {
    if (!spellbook_can_cast(sb, id)) return false;
    const SpellDef* d = spell_registry().find(id);
    if (!cast_spell(w, id, pid, px, py, nx, ny)) return false;
    sb.mana -= d->manaCost;
    sb.cooldowns[id] = d->cooldown;
    return true;
}

void spellbook_tick(SpellBook& sb, float dt) {
    for (auto& kv : sb.cooldowns) if (kv.second > 0.0f) kv.second -= dt;
    if (sb.mana < sb.maxMana) {
        float v = float(sb.mana) + sb.manaRegenPerSec * dt;
        if (v > float(sb.maxMana)) v = float(sb.maxMana);
        sb.mana = int(v);
    }
}

} // namespace sm
