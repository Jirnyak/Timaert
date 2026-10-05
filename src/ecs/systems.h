// ECS systems — pure functions over the registry. Mirrors movement +
// projectile + visual interpolation systems. No engine state held here.
#pragma once
#include "ecs/world.h"

namespace sm::ecs::sys {

// Advance projectiles, decrement life, destroy expired.
// NOTE: no tick_projectiles here. Projectiles are ticked by
// sub/spell_effects.cpp tick_spell_projectiles — the 3D one that checks the
// box, the ground, structures and hits. See the note in systems.cpp.

// Smoothly lerp VisualPos toward Position.
void tick_visual_interp(World& w, float dt);

// (tick_combat_recovery переехал в sub/ability.h — tick_body_recovery:
// боевой лист — колонка арены SubObjects (M-150 кусок 2), слой sub выше.)

} // namespace sm::ecs::sys
