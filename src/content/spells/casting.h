// The spell BINDING layer (ARCHITECTURE.md Rule 13): where a pure-data row
// (macro/spells.h kSpellDefs) meets its subworld effect. The spawn functions
// live in effects.cpp, bound to the data rows by ordinal (kSpellEffects, one
// static_assert per row) — the same shape as creature row → archetype
// silhouette and material row → struct.frag branch.
#pragma once

#include <cstdint>
#include <string_view>

#include "macro/spells.h"

namespace sm::ecs { struct World; }

namespace sm {

using SpellRngFn = float (*)(void*);

struct SpellSpawnContext {
    float px, py;
    float pz;              // caster altitude (metres above ground)
    // The CASTER's shell, not "the player's": every shooter has one, and the
    // muzzle clears whichever body is doing the casting.
    float casterRadius;
    float nx, ny, nz;      // 3D aim direction (normalised)
    std::int32_t damage;   // ROLLED at cast (spell_strike) — the bolt carries
                           // its wound like an arrow does, integer like every
                           // combat quantity
    float speed;
    float projectileRadius;
    float effectRadius;
    bool  friendlyFire;
    std::uint32_t playerId;
    std::uint32_t spellId;
    SpellRngFn rng01 = nullptr;
    void* rngUser = nullptr;
    // The blow's armour column (DamageType ordinal — the tag's row) and the
    // crit door's verdict at cast; both ride the projectile to the damage
    // door. Appended with defaults so positional builders stay whole.
    std::uint8_t dmgType = 2;  // DamageType::Blunt
    bool critical = false;
};

using SpellSpawnFn = void (*)(ecs::World&, const SpellSpawnContext&);

// (`kSpellCasterRadius` lived here and is gone. It was introduced as the ONE
// home of a caster's shell so the muzzle could clear it — but the subworld
// then armed the PLAYER'S ecs::BodyRadius from it, and a number chosen for
// muzzle geometry became his physical size. A caster's shell is a property of
// the caster, so it is now read from the body: sub/body.h body_radius, the
// same door the muzzle-hit guard in spell_effects.cpp already used. The two
// sides of that guard used to disagree for every NPC caster, because only one
// of them asked the body.)

bool cast_spell(ecs::World& w, std::string_view id,
                std::uint32_t playerId, float px, float py, float nx, float ny);
bool cast_spell(ecs::World& w, const SpellDef& spell,
                const SpellSpawnContext& ctx);

} // namespace sm
