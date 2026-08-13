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
    float playerRadius;
    float nx, ny, nz;      // 3D aim direction (normalised)
    float damage;
    float speed;
    float projectileRadius;
    float effectRadius;
    bool  friendlyFire;
    std::uint32_t playerId;
    std::uint32_t spellId;
    SpellRngFn rng01 = nullptr;
    void* rngUser = nullptr;
};

using SpellSpawnFn = void (*)(ecs::World&, const SpellSpawnContext&);

// The player entity's body radius; spell visuals spawn offset past it so a
// bolt never detonates on its own caster at the muzzle.
static constexpr float kSpellCasterRadius = 1.5f;

bool cast_spell(ecs::World& w, std::string_view id,
                std::uint32_t playerId, float px, float py, float nx, float ny);
bool cast_spell(ecs::World& w, const SpellDef& spell,
                const SpellSpawnContext& ctx);

} // namespace sm
