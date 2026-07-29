// ECS systems — pure functions over the registry. Mirrors movement +
// projectile + visual interpolation systems. No engine state held here.
#pragma once
#include "ecs/world.h"

namespace sm::ecs::sys {

// Advance projectiles, decrement life, destroy expired.
void tick_projectiles(World& w, float dt);

// Smoothly lerp VisualPos toward Position.
void tick_visual_interp(World& w, float dt);

// Decrement combat cooldowns.
void tick_combat_cooldowns(World& w, float dt);

} // namespace sm::ecs::sys
