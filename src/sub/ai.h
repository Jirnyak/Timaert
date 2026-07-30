// Subworld AI tick. Wander/Flee movement lives here; real combat actors
// with ecs::Combat are positioned and attacked by SubworldEngine so they
// do not integrate twice in one frame.
#pragma once
#include <cstdint>
#include "ecs/world.h"

namespace sm::sub {

constexpr float kDetectionRadius  = 200.0f;
constexpr int   kHostileThreshold = -50;
constexpr int   kHitRepPenalty    = -1;
constexpr float kCrowdPenalty     = 40.0f;

using PlayerThreatFn = bool (*)(void* user, std::uint32_t entityId);

// Solid-structure gate (sub/collide.h, wired by the engine): may a body of
// radius r with feet at z occupy (x, y)? Same contract as BattleTerrain's
// canStand — one rule for every mover in the subworld.
using SolidCanStandFn = bool (*)(void* user, float x, float y,
                                 float r, float z);

void tick_npc_ai(ecs::World& w, float playerX, float playerY,
                 std::uint32_t playerEntityId, float dt,
                 PlayerThreatFn threatFn = nullptr,
                 void* threatUser = nullptr,
                 SolidCanStandFn canStand = nullptr,
                 void* canStandUser = nullptr);

} // namespace sm::sub
