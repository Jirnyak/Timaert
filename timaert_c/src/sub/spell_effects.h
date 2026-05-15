// Native spell projectile/effect ticking for subworld ECS entities.
#pragma once

#include "ecs/world.h"
#include "events/event_bus.h"

namespace sm::sub {

void tick_spell_projectiles(ecs::World& w, EventBus* bus, float dt);

} // namespace sm::sub
