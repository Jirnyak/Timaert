// Subworld AI tick — universal combat. NPCs see player within
// kDetectionRadius, move toward, attack on cooldown. Mirrors engine.ts
// combat AI plus npc-ai wandering.
#pragma once
#include <cstdint>
#include "ecs/world.h"

namespace sm::sub {

constexpr float kDetectionRadius  = 200.0f;
constexpr int   kHostileThreshold = -50;
constexpr int   kHitRepPenalty    = -1;
constexpr float kCrowdPenalty     = 40.0f;

void tick_npc_ai(ecs::World& w, float playerX, float playerY,
                 std::uint32_t playerEntityId, float dt);

} // namespace sm::sub
