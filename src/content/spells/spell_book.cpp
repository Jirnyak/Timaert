#include "content/spells/spell_book.h"
#include "core/time.h"

#include <cstddef>
#include <cmath>
#include <cstdio>

namespace sm {

namespace {

// The one place steps turn back into seconds: a human reads seconds.
std::string cooldown_reason(std::uint32_t steps) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "Cooldown %.1fs",
                  double(seconds_from_steps(steps)));
    return std::string(buf);
}

} // namespace

float spell_strength(const SpellDef& spell,
                     const Attributes& attributes,
                     const Skills& skills) {
    const DerivedBonuses derived = calculate_derived(attributes, skills);
    const float tierMul = 1.0f + 0.08f * float(spell.tier - 1);
    return derived.rawSpellDamage * tierMul;
}

int spell_damage(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills) {
    if (spell.baseDamage <= 0.0f) return 0;
    const float s = spell_strength(spell, attributes, skills);
    return int(std::floor((spell.baseDamage + s) * spell.scalingPower));
}

int spell_heal(const SpellDef& spell,
               const Attributes& attributes,
               const Skills& skills) {
    if (spell.baseHeal <= 0.0f) return 0;
    const float s = spell_strength(spell, attributes, skills);
    return int(std::floor((spell.baseHeal + s) * spell.scalingPower));
}

int spell_radius(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills) {
    if (spell.baseRadius <= 0.0f) return 0;
    const float s = spell_strength(spell, attributes, skills);
    const float scaleFactor = s / (s + 50.0f);
    return int(std::floor(spell.baseRadius
        * (1.0f + scaleFactor * spell.scalingRadius)));
}

CastCheck spellbook_can_cast_ex(const SpellBook& sb,
                                const CombatStats& combat,
                                const std::string& id,
                                bool inMicro) {
    const SpellDef* d = spell_find(id);
    if (!d) return {false, "Unknown spell", 0.0f};
    if (!spellbook_has_learned(sb, id)) return {false, "Spell not learned", 0.0f};
    if (d->sustained && spellbook_has_sustained(sb, id)) return {true, "", 0.0f};
    if (combat.currentMp < d->manaCost) return {false, "Not enough mana", 0.0f};

    const auto it = sb.cooldowns.find(id);
    if (it != sb.cooldowns.end() && it->second > 0u) {
        return {false, cooldown_reason(it->second),
                seconds_from_steps(it->second)};
    }

    if (inMicro && !d->hasMicro) return {false, "Cannot use here", 0.0f};
    if (!inMicro && !d->hasMacro) {
        // What a spell can do on the world map is now stated by the row that
        // was always meant to state it (`hasMacro`), not inferred from whether
        // it happened to carry a macro-effect enum. The two used to disagree
        // silently, and the enum was the one nobody read.
        return {false, "Cannot use on world map", 0.0f};
    }
    return {true, "", 0.0f};
}

int spellbook_start_cast(SpellBook& sb, CombatStats& combat,
                         const std::string& id) {
    const SpellDef* d = spell_find(id);
    if (!d) return 0;
    if (d->sustained) {
        spellbook_toggle_sustained(sb, id);
        return 0;
    }
    combat.currentMp -= d->manaCost;
    if (combat.currentMp < 0) combat.currentMp = 0;
    // The table authors seconds; the world counts steps.
    if (d->cooldown > 0.0f) {
        sb.cooldowns[id] = steps_from_seconds(d->cooldown);
    }
    return d->manaCost;
}

bool spellbook_cast(ecs::World& w, SpellBook& sb, CombatStats& combat,
                    const Attributes& attributes, const Skills& skills,
                    const std::string& id,
                    std::uint32_t pid, float px, float py, float pz,
                    float nx, float ny, float nz, bool inMicro,
                    SpellRngFn rng01,
                    void* rngUser) {
    if (!spellbook_can_cast_ex(sb, combat, id, inMicro).ok) return false;
    const SpellDef* d = spell_find(id);
    if (!d) return false;
    if (d->sustained || d->shape == DeliveryShape::Self) {
        spellbook_start_cast(sb, combat, id);
        return true;
    }
    if (!inMicro) {
        return false;
    }

    const float blastRadius = d->friendlyFire ? d->baseRadius : 0.0f;
    SpellSpawnContext ctx{
        px, py,
        pz,
        kSpellCasterRadius,
        nx, ny, nz,
        float(spell_damage(*d, attributes, skills)),
        d->speed > 0.0f ? d->speed : 300.0f,
        d->projectileRadius,
        blastRadius,
        d->friendlyFire,
        pid,
        stable_spell_id(d->id),
        rng01,
        rngUser,
    };

    if (!cast_spell(w, *d, ctx)) return false;
    spellbook_start_cast(sb, combat, id);
    return true;
}

void spellbook_tick(SpellBook& sb, CombatStats& combat, std::uint32_t steps) {
    if (steps == 0u) return;
    const float dt = float(steps) * kStepSeconds;   // for the per-second rates
    for (auto it = sb.cooldowns.begin(); it != sb.cooldowns.end(); ) {
        if (it->second <= steps) {
            it = sb.cooldowns.erase(it);
        } else {
            it->second -= steps;
            ++it;
        }
    }

    if (sb.sustainedActive.empty()) {
        sb.sustainedDrainCarry = 0.0f;
        return;
    }

    float drainPerSecond = 0.0f;
    for (auto it = sb.sustainedActive.begin(); it != sb.sustainedActive.end(); ) {
        const SpellDef* d = spell_find(*it);
        if (!d || !d->sustained) {
            it = sb.sustainedActive.erase(it);
            continue;
        }
        drainPerSecond += d->manaDrain;
        ++it;
    }

    if (sb.sustainedActive.empty() || drainPerSecond <= 0.0f) {
        sb.sustainedDrainCarry = 0.0f;
        return;
    }

    if (combat.currentMp <= 0) {
        for (std::size_t i = sb.sustainedActive.size(); i > 0; --i) {
            const std::size_t idx = i - 1;
            const SpellDef* d = spell_find(sb.sustainedActive[idx]);
            if (!d || !d->sustained || d->manaDrain * dt > 0.0f) {
                sb.sustainedActive.erase(sb.sustainedActive.begin()
                                         + std::ptrdiff_t(idx));
            }
        }
        sb.sustainedDrainCarry = 0.0f;
        return;
    }

    sb.sustainedDrainCarry += drainPerSecond * dt;
    const int drain = int(std::floor(sb.sustainedDrainCarry));
    if (drain <= 0) return;
    sb.sustainedDrainCarry -= float(drain);

    if (combat.currentMp >= drain) {
        combat.currentMp -= drain;
        return;
    }

    int remainingMp = combat.currentMp;
    int remainingDrain = drain;
    sb.sustainedDrainCarry = 0.0f;

    for (std::size_t i = sb.sustainedActive.size();
         i > 0 && remainingDrain > 0; --i) {
        const std::size_t idx = i - 1;
        const SpellDef* d = spell_find(sb.sustainedActive[idx]);
        if (!d || !d->sustained) {
            sb.sustainedActive.erase(sb.sustainedActive.begin()
                                     + std::ptrdiff_t(idx));
            continue;
        }

        int spellDrain = int(std::floor(d->manaDrain * dt));
        if (spellDrain <= 0 && d->manaDrain > 0.0f) {
            spellDrain = 1;
        }
        if (spellDrain > remainingDrain) spellDrain = remainingDrain;
        if (spellDrain <= 0) continue;

        remainingDrain -= spellDrain;
        if (remainingMp >= spellDrain) {
            remainingMp -= spellDrain;
        } else {
            remainingMp = 0;
            sb.sustainedActive.erase(sb.sustainedActive.begin()
                                     + std::ptrdiff_t(idx));
        }
    }

    combat.currentMp = remainingMp;
}

} // namespace sm
