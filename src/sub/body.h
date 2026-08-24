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
//   2. The body's `kNpcTypeDefs` row (macro/npc.h) — ONE table for wolf and
//      spearman alike (CANON S16): its authored `radius` first, then what its
//      combat line implies for a man-shaped thing (`combat.bodyRadius`).
//   3. `ecs::SubworldAi.radius` — the mover's own idea of its footprint.
//   4. `ecs::Sprite.scale` — a billboard's VISUAL size standing in for a
//      physical one. Kept because bare test and console entities rely on it, but
//      it is a conflation: see the note at the bottom.
//   5. `kBodyRadiusFallback` — a bare entity that declares nothing at all.
#pragma once

#include "ecs/components.h"
#include "macro/npc.h"
#include "macro/fauna.h"

namespace sm::sub {

// Last-resort body radius for a body carrying neither an explicit
// ecs::BodyRadius nor a table row (a bare test/console entity). Roughly a
// human's half-width: 1 world unit ≈ 1 metre.
inline constexpr float kBodyRadiusFallback = 0.55f;

// THE row behind a body, whatever it is. There were two of these — one that
// looked a kind up in the monster catalog and one that looked it up in the NPC
// registry — for as long as there were two tables. There is one table, so
// there is one lookup, and a caller no longer has to ask which sort of thing
// it is holding before it can ask what that thing is (CANON.md S16).
inline const NpcTypeDef* row_for(const ecs::NPCKind* kind) {
    if (!kind || !valid_npc_kind(kind->type)) return nullptr;
    return &npc_def(static_cast<NPCType>(kind->type));
}

// THE combat half-width of a body, in world units (≈ metres).
inline float body_radius(const entt::registry& reg, entt::entity e) {
    if (const auto* br = reg.try_get<ecs::BodyRadius>(e)) return br->radius;
    const auto* kind = reg.try_get<ecs::NPCKind>(e);
    if (const NpcTypeDef* row = row_for(kind)) {
        // The row's own authored width first (a rabbit says how wide a rabbit
        // is), then what its combat line implies for a man-shaped thing.
        if (row->radius > 0.0f) return row->radius;
        if (row->combat.bodyRadius > 0.0f) return row->combat.bodyRadius;
    }
    if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) return ai->radius;
    if (const auto* sp = reg.try_get<ecs::Sprite>(e)) return sp->scale;
    return kBodyRadiusFallback;
}

// ── How TALL is this body ─────────────────────────────────────────────────
//
// The same question as above, on the other axis, and it had a worse answer: no
// authority at all. The renderer drew every humanoid exactly 2 metres
// (`inst.size = 2.0f`) and every creature at 1.5 × its width, so a body's size
// was authored data for physics and a literal for the eye. Height is now a
// column of the SAME row that states the width (CombatTemplate::bodyHeight),
// which means one table entry decides what a thing is, how much room it takes
// and how big it looks.
//
// A row that says nothing gets the honest default for its kind — a humanoid is
// a person, a creature keeps the proportion the renderer used to hardcode, so
// nothing that has not opted in changes appearance.
inline constexpr float kHumanHeightM = 1.8f;
inline constexpr float kCreatureHeightPerRadius = 1.5f;

// Individual variation, from the byte the face already rolls
// (`ecs::NpcCharacter.bodyShape`, 0..3 — small / medium / large / huge). It was
// rolled at every birth and read by nobody. Now one lord stands taller than
// another without a second field, a second roll, or a second system.
inline constexpr float kBodyShapeHeightScale[4] = {0.92f, 1.00f, 1.08f, 1.16f};

inline float body_shape_height_scale(std::uint8_t bodyShape) {
    return kBodyShapeHeightScale[bodyShape & 0x3u];
}

// Drawn height of ANY row, before individual variation. There were two of
// these — one for men, one for beasts — until the tables merged; they were
// never in disagreement, only in different files. The answer is the row's own
// authored height, then what its body radius implies, then the human default:
// three sentences that a wolf and a guard both read top to bottom.
inline float body_height_m(const NpcTypeDef& row) {
    if (row.combat.bodyHeight > 0.0f) return row.combat.bodyHeight;
    if (row.radius > 0.0f)            return row.radius * kCreatureHeightPerRadius;
    return kHumanHeightM;
}

// KNOWN CONFLATION, left deliberately. Step 5 reads `Sprite.scale`, which is a
// billboard's drawing size, as a physical width — one field wearing two
// meanings. It survives because bare fixtures lean on it, and removing it would
// silently shrink those bodies to the fallback rather than fail loudly. The
// honest cure is to give every spawner a real radius (a table row or an explicit
// BodyRadius) and then delete the step; until then it lives in ONE place, where
// changing it changes every weapon at once.

} // namespace sm::sub
