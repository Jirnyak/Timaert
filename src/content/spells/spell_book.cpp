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

// The scaling column as the strike assembly's whole percent — one rounding,
// one place.
static int spell_mult_pct(const SpellDef& spell) {
    return int(spell.scalingPower * 100.0f + 0.5f);
}

int spell_damage(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills) {
    if (spell.dice.n == 0) return 0;
    const float s = spell_strength(spell, attributes, skills);
    // The strike's EXPECTATION — what a panel prints and a macro reader
    // credits; the cast itself rolls (spell_strike below). Exact for the
    // mechanical Nd1 rows, honest for authored spreads later.
    return strike_mean_x2(spell.dice, int(std::floor(s)),
                          spell_mult_pct(spell)) / 2;
}

StrikeRoll spell_strike(Rng& rng, const SpellDef& spell,
                        const Attributes& attributes, const Skills& skills) {
    if (spell.dice.n == 0) return {};
    const float s = spell_strength(spell, attributes, skills);
    // THE strike assembly, the same the sword swings through: dice + the
    // sheet's add, scaled by the row's percent, LCK asking the crit door.
    return roll_strike(rng, spell.dice, int(std::floor(s)),
                       spell_mult_pct(spell),
                       attributes.of(AttributeId::Lck));
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
                                int spellOrd,
                                bool inMicro) {
    if (!spell_ordinal_ok(spellOrd)) return {false, "Unknown spell", 0.0f};
    const SpellDef* d = &kSpellDefs[spellOrd];
    if (!spellbook_has_learned(sb, spellOrd))
        return {false, "Spell not learned", 0.0f};
    if (d->sustained && spellbook_has_sustained(sb, spellOrd))
        return {true, "", 0.0f};
    if (combat.currentMp < d->manaCost) return {false, "Not enough mana", 0.0f};

    if (const std::uint32_t cd = sb.cooldownSteps[spellOrd]; cd > 0u) {
        return {false, cooldown_reason(cd), seconds_from_steps(cd)};
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

int spellbook_start_cast(SpellBook& sb, CombatStats& combat, int spellOrd) {
    if (!spell_ordinal_ok(spellOrd)) return 0;
    const SpellDef* d = &kSpellDefs[spellOrd];
    if (d->sustained) {
        spellbook_toggle_sustained(sb, spellOrd);
        return 0;
    }
    combat.currentMp -= d->manaCost;
    if (combat.currentMp < 0) combat.currentMp = 0;
    // The table authors seconds; the world counts steps.
    if (d->cooldown > 0.0f) {
        sb.cooldownSteps[spellOrd] = steps_from_seconds(d->cooldown);
    }
    return d->manaCost;
}

bool spellbook_cast(ecs::World& w, SpellBook& sb, CombatStats& combat,
                    const Attributes& attributes, const Skills& skills,
                    int spellOrd,
                    std::uint32_t pid, float px, float py, float pz,
                    float nx, float ny, float nz, bool inMicro,
                    SpellRngFn rng01,
                    void* rngUser,
                    Rng* diceRng) {
    if (!spellbook_can_cast_ex(sb, combat, spellOrd, inMicro).ok) return false;
    const SpellDef* d = &kSpellDefs[spellOrd];
    if (d->sustained || d->shape == DeliveryShape::Self) {
        spellbook_start_cast(sb, combat, spellOrd);
        return true;
    }
    if (!inMicro) {
        return false;
    }

    const float blastRadius = d->friendlyFire ? d->baseRadius : 0.0f;
    // The wound is ROLLED here, at the cast — the bolt then carries its
    // number like a loosed arrow. No stream = the expectation, no crit.
    const StrikeRoll strike = diceRng
        ? spell_strike(*diceRng, *d, attributes, skills)
        : StrikeRoll{spell_damage(*d, attributes, skills), false};
    SpellSpawnContext ctx{
        px, py,
        pz,
        kSpellCasterRadius,
        nx, ny, nz,
        float(strike.amount),
        d->speed > 0.0f ? d->speed : 300.0f,
        d->projectileRadius,
        blastRadius,
        d->friendlyFire,
        pid,
        stable_spell_id(d->id),
        rng01,
        rngUser,
    };
    ctx.dmgType = std::uint8_t(spell_damage_type(*d));
    ctx.critical = strike.critical;

    if (!cast_spell(w, *d, ctx)) return false;
    spellbook_start_cast(sb, combat, spellOrd);
    return true;
}

void spellbook_tick(SpellBook& sb, CombatStats& combat, std::uint32_t steps) {
    if (steps == 0u) return;
    const float dt = float(steps) * kStepSeconds;   // for the per-second rates
    for (int i = 0; i < kSpellCount; ++i) {
        sb.cooldownSteps[i] = sb.cooldownSteps[i] <= steps
                                  ? 0u
                                  : sb.cooldownSteps[i] - steps;
    }

    // Sustained drains: flat flags over the registry — a set flag is valid
    // by construction (toggle guards the ordinal), so the row's own
    // `sustained` column is the only sanity the old string list re-checked.
    float drainPerSecond = 0.0f;
    for (int i = 0; i < kSpellCount; ++i) {
        if (!sb.sustained[i]) continue;
        drainPerSecond += kSpellDefs[i].manaDrain;
    }
    if (drainPerSecond <= 0.0f) {
        sb.sustainedDrainCarry = 0.0f;
        return;
    }

    if (combat.currentMp <= 0) {
        // No mana left: every paying drain collapses at once.
        for (int i = 0; i < kSpellCount; ++i) {
            if (sb.sustained[i] && kSpellDefs[i].manaDrain > 0.0f) {
                sb.sustained[i] = 0;
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

    // Not enough for the full bill: newest-numbered drains pay first and the
    // ones the pool cannot cover switch off (the old list's newest-first
    // walk, said in ordinals).
    int remainingMp = combat.currentMp;
    int remainingDrain = drain;
    sb.sustainedDrainCarry = 0.0f;

    for (int i = kSpellCount; i-- > 0 && remainingDrain > 0; ) {
        if (!sb.sustained[i]) continue;
        const SpellDef& d = kSpellDefs[i];
        int spellDrain = int(std::floor(d.manaDrain * dt));
        if (spellDrain <= 0 && d.manaDrain > 0.0f) spellDrain = 1;
        if (spellDrain > remainingDrain) spellDrain = remainingDrain;
        if (spellDrain <= 0) continue;

        remainingDrain -= spellDrain;
        if (remainingMp >= spellDrain) {
            remainingMp -= spellDrain;
        } else {
            remainingMp = 0;
            sb.sustained[i] = 0;
        }
    }
    if (!spellbook_any_sustained(sb)) sb.sustainedDrainCarry = 0.0f;

    combat.currentMp = remainingMp;
}

} // namespace sm
