// Spellbook runtime behavior. The state struct itself lives in macro/
// because PlayerState owns it; this content layer wires it to spell defs.
#pragma once

#include <cstdint>
#include <string>

#include "content/spells/spell_types.h"
#include "macro/attributes.h"
#include "macro/spell_book_state.h"

namespace sm {

struct CastCheck {
    bool ok = false;
    std::string reason{};
    float cooldownRemaining = 0.0f;
};

float spell_strength(const SpellDef& spell,
                     const Attributes& attributes,
                     const Skills& skills);
int spell_damage(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills);
int spell_heal(const SpellDef& spell,
               const Attributes& attributes,
               const Skills& skills);
int spell_radius(const SpellDef& spell,
                 const Attributes& attributes,
                 const Skills& skills);
float spell_duration(const SpellDef& spell,
                     const Attributes& attributes,
                     const Skills& skills);

CastCheck spellbook_can_cast_ex(const SpellBook& sb,
                                const CombatStats& combat,
                                const std::string& id,
                                bool inMicro);
bool spellbook_can_cast(const SpellBook& sb, const CombatStats& combat,
                        const std::string& id);
int spellbook_start_cast(SpellBook& sb, CombatStats& combat,
                         const std::string& id);
bool spellbook_cast(ecs::World& w, SpellBook& sb, CombatStats& combat,
                    const Attributes& attributes, const Skills& skills,
                    const std::string& id,
                    std::uint32_t playerId, float px, float py, float pz,
                    float nx, float ny, float nz, bool inMicro,
                    SpellRngFn rng01 = nullptr,
                    void* rngUser = nullptr);
void spellbook_tick(SpellBook& sb, CombatStats& combat, float dt);

} // namespace sm
