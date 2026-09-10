// Spellbook runtime behavior. The state struct itself lives in macro/
// because PlayerState owns it; this content layer wires it to spell defs.
#pragma once

#include <cstdint>
#include <string>

#include "content/spells/casting.h"
#include "ecs/pools.h"
#include "macro/attributes.h"
#include "macro/spell_book_state.h"

namespace sm {

struct CastCheck {
    bool ok = false;
    std::string reason{};
    float recoveryRemaining = 0.0f;
};

int spell_strength(const SpellDef& spell,
                     const Attributes& attributes,
                     const Skills& skills);
// The strike's EXPECTATION — panels and macro readers; the cast rolls.
int spell_damage(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills);
// The strike itself, through THE assembly (macro/damage_types.h): dice +
// the sheet's add, the row's percent, the caster's LCK at the crit door.
StrikeRoll spell_strike(Rng& rng, const SpellDef& spell,
                        const Attributes& attributes, const Skills& skills);
int spell_heal(const SpellDef& spell,
               const Attributes& attributes,
               const Skills& skills);
int spell_radius(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills);
// Spells are addressed by their registry ORDINAL (macro/spells.h) — the one
// identity the book itself is indexed by. Strings resolve at the edges
// (console tokens, event payloads) via spell_ordinal(), never in here.
// `bodyRecoverySteps` is the caster BODY's one gate (ecs::Combat::
// recoverySteps — the field a sword swing charges): a busy body casts
// nothing, whatever the spell is (owner verdict 2026-09-09). Deliberately no
// default — a call site without a body in hand (a paused panel, the world
// map) says 0 out loud.
CastCheck spellbook_can_cast_ex(const SpellBook& sb,
                                const ecs::Pools& combat,
                                int spellOrd,
                                bool inMicro,
                                std::uint32_t bodyRecoverySteps);
// The mana-or-toggle half of a cast. Recovery is NOT charged here — the
// micro door (spellbook_cast) writes the body's gate; a world-map cast has
// no fighting body and owes none.
int spellbook_start_cast(SpellBook& sb, ecs::Pools& combat,
                         int spellOrd);
// diceRng — the stream the wound is ROLLED from at cast. nullptr = the
// strike's exact expectation, no crit: what a harness with no stream gets,
// deterministic by construction.
bool spellbook_cast(ecs::World& w, SpellBook& sb, ecs::Pools& combat,
                    const Attributes& attributes, const Skills& skills,
                    int spellOrd,
                    std::uint32_t playerId, float px, float py, float pz,
                    float nx, float ny, float nz, bool inMicro,
                    SpellRngFn rng01 = nullptr,
                    void* rngUser = nullptr,
                    Rng* diceRng = nullptr);
// Advance every timer the book owns by `steps` simulation steps (core/time.h).
// It used to take a float dt of real seconds — the same wall-clock coupling the
// tick ladder abolished everywhere else.
void spellbook_tick(SpellBook& sb, ecs::Pools& combat, std::uint32_t steps);

} // namespace sm
