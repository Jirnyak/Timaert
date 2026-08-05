// ONE body-size authority for the subworld.
// ----------------------------------------------------------------
// How wide is this body? Every consumer asks HERE — melee reach, projectile
// contact, AoE blast, crowd separation, the battle unit descriptor. There is no
// second opinion, because there used to be one and it drifted.
//
// WHAT WENT WRONG (fixed 2026-08-05). `target_radius` existed twice: once in
// sub/engine.cpp for melee and once in sub/spell_effects.cpp for projectiles.
// Both carried a comment promising they were twins kept in lockstep. They were
// not:
//   * melee consulted the authored radius in the monster and NPC tables;
//     projectiles never did, so a creature's own size was visible to a sword and
//     invisible to an arrow;
//   * the last-resort values were 0.55 and 6.0 — an eleven-fold disagreement
//     about a body that declared nothing.
// A body was literally a different size depending on which weapon was pointed at
// it. Owner's ruling: one function, and EVERYTHING reads the tables.
//
// THE ORDER, most specific first. Each step answers "who has the best claim to
// know this body's width":
//   1. `ecs::BodyRadius` — an explicit per-entity override. The player carries
//      one; so does anything a spawner sized deliberately.
//   2. The MONSTER table row (`sub/fauna.h`). A wolf's width is authored data.
//   3. The NPC table row (`macro/npc.h`). Same, for humanoids.
//   4. `ecs::SubworldAi.radius` — the mover's own idea of its footprint.
//   5. `ecs::Sprite.scale` — a billboard's VISUAL size standing in for a
//      physical one. Kept because bare test and console entities rely on it, but
//      it is a conflation: see the note at the bottom.
//   6. `kBodyRadiusFallback` — a bare entity that declares nothing at all.
#pragma once

#include "ecs/components.h"
#include "macro/npc.h"
#include "sub/fauna.h"

namespace sm::sub {

// Last-resort body radius for a body carrying neither an explicit
// ecs::BodyRadius nor a table row (a bare test/console entity). Roughly a
// human's half-width: 1 world unit ≈ 1 metre.
inline constexpr float kBodyRadiusFallback = 0.55f;

inline const FaunaEntry* creature_row_for(const ecs::NPCKind* kind) {
    if (!kind || kind->type < std::uint16_t{0x100}) return nullptr;
    return creature_def_from_kind(kind->type);
}

inline const NpcTypeDef* humanoid_row_for(const ecs::NPCKind* kind) {
    if (!kind || kind->type >= std::uint16_t(NPCType::Count)) return nullptr;
    return &npc_def(static_cast<NPCType>(std::uint8_t(kind->type)));
}

// THE combat half-width of a body, in world units (≈ metres).
inline float body_radius(const entt::registry& reg, entt::entity e) {
    if (const auto* br = reg.try_get<ecs::BodyRadius>(e)) return br->radius;
    const auto* kind = reg.try_get<ecs::NPCKind>(e);
    if (const FaunaEntry* cr = creature_row_for(kind)) {
        if (cr->radius > 0.0f) return cr->radius;
    }
    if (const NpcTypeDef* nd = humanoid_row_for(kind)) {
        if (nd->combat.bodyRadius > 0.0f) return nd->combat.bodyRadius;
    }
    if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) return ai->radius;
    if (const auto* sp = reg.try_get<ecs::Sprite>(e)) return sp->scale;
    return kBodyRadiusFallback;
}

// KNOWN CONFLATION, left deliberately. Step 5 reads `Sprite.scale`, which is a
// billboard's drawing size, as a physical width — one field wearing two
// meanings. It survives because bare fixtures lean on it, and removing it would
// silently shrink those bodies to the fallback rather than fail loudly. The
// honest cure is to give every spawner a real radius (a table row or an explicit
// BodyRadius) and then delete the step; until then it lives in ONE place, where
// changing it changes every weapon at once.

} // namespace sm::sub
