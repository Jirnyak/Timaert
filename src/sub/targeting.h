// Pure subworld targeting primitive (Increment 5 possession groundwork).
//
// aim_target() selects the entity a shooter standing at (px,py) and facing
// `yaw` would strike within a forward cone. It generalises the shipped
// player-melee selection (SubworldEngine::tick_player_melee, engine.cpp): the
// candidate set is the live subworld hostiles —
//   view<Position, Health, NPCKind, SubworldTag> minus Dead
// — excluding the player's own side (PlayerTag / PlayerSoldierTag, i.e. the
// is_player_side predicate at engine.cpp) and the shooter entity itself. Among
// those it returns the NEAREST candidate that is both within `maxRange` and
// inside the aim cone (cos(angle to target) >= cosHalfAngle).
//
// With cosHalfAngle == -1 the cone is the full circle, so the result matches
// melee's omnidirectional nearest-in-range; a narrower cone (e.g. cos 15° for a
// first-person reticle) restricts to what the shooter is looking at.
//
// The function is deliberately pure and free of SubworldEngine state so it is
// unit-testable in isolation and can back both the first-person reticle and the
// macro `control <id>` debug aim without duplication.
#pragma once
#include "ecs/world.h" // entt::registry/entity + ecs components

namespace sm::sub {

// Forward direction is (cos yaw, sin yaw) in the tile-XY plane, matching the
// camera / movement convention (engine.cpp:1425-1428, camera.h). `shooter` is
// excluded from the candidate set (pass entt::null if there is no self entity).
// Returns entt::null when no live enemy lies within both range and cone.
// The player-melee auto-aim (owner ruling 2026-08-05): HOSTILES FIRST — the
// nearest body the `isHostile` oracle confirms wins; only when no hostile is
// in reach does the swing fall back to the nearest body of any stripe, so
// striking a neutral stays possible (and keeps its reputation price) but a
// bystander no longer catches the blow meant for the bandit behind him.
// `isHostile` is a callback because hostility needs GameState (reputation),
// which this pure module deliberately does not know.
using HostileFn = bool (*)(void* user, entt::entity e);

// Broad phase for the swing — the battle pick grid, reached exactly like the
// spell contact reaches it (same shape as SpellNeighborsFn, same promises: a
// SUPERSET of everything within `r` of (x, y), -1 when completeness cannot be
// promised this tick, null = no grid at all — both mean the full scan).
using MeleeNeighborsFn = int (*)(void* user, float x, float y, float r,
                                 std::uint32_t* out, int maxOut);
inline constexpr int kMaxMeleeNeighbors = 16384; // the battle snapshot ceiling

// `range` is the attacker's reach — ONE number (owner ruling 2026-08-27: a
// mob's row states its own attack radius; a man's will come from his spear
// through the equipment increment — but it is always just this number). The
// blow lands on a target's SURFACE: a candidate is in reach when
// dist − body_radius(target) ≤ range, the same law the NPC strike walks
// (sub/movement.cpp reach + radius[target]) — a troll is struck from farther
// away than a frog because a troll is wider, not because anyone branched.
entt::entity melee_pick_target(entt::registry& reg,
                               float px, float py, float pz,
                               float range,
                               HostileFn isHostile, void* user,
                               MeleeNeighborsFn neighborsFn = nullptr,
                               void* neighborsUser = nullptr);

entt::entity aim_target(entt::registry& reg,
                        float px, float py, float yaw,
                        float maxRange, float cosHalfAngle,
                        entt::entity shooter = entt::null);

} // namespace sm::sub
