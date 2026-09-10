// Subworld AI tick — the BRAIN pass. Wander/Flee decide what a body wants
// and write SubworldAi.wantVx/Vy; the ONE mover (driven from
// SubworldEngine) is the ONE owner of every body's position and executes
// that intent with the same separation, solids and terrain as any fighter.
// Nothing here ever writes Position, so nothing can integrate twice in one
// frame.
#pragma once
#include <cstdint>
#include "ecs/world.h"
#include "macro/army.h"
#include "macro/faction.h"

namespace sm::sub {

// The ambient "did anything notice me" radius — the SAME quantity as a
// fighter's default sight, read from its one home (macro/army.h
// kNpcSightDefaultM) instead of a second 200 kept here by hand.
constexpr float kDetectionRadius  = kNpcSightDefaultM;
// kHostileThreshold moved to macro/faction.h (Session 15): the macro squad
// threat step needs the same line the battle masks draw, and one number
// cannot live in two headers. Unqualified uses in sm::sub still resolve.
constexpr int   kHitRepPenalty    = -1;

using PlayerThreatFn = bool (*)(void* user, std::uint32_t entityId);
// Рельеф под точкой, метры — восприятие ЗЕМЛИ ПОД СОБОЙ для вертикального
// намерения летуна (полёт-посадка 2026-09-10: «все воспринимают x/y/z»).
// nullptr = мозг слеп по вертикали и wantVz не пишет (headless-тесты).
using GroundHeightFn = float (*)(void* user, float x, float y);

// A brain needs no collision gate: walls, bounds and the crowd are the
// steering pass's business. The SolidCanStandFn parameter died with the
// private integrator.
void tick_npc_ai(ecs::World& w, float playerX, float playerY,
                 std::uint32_t playerEntityId, float dt,
                 PlayerThreatFn threatFn = nullptr,
                 void* threatUser = nullptr,
                 GroundHeightFn heightFn = nullptr,
                 void* heightUser = nullptr);

} // namespace sm::sub
