// Native spell projectile/effect ticking for subworld ECS entities.
#pragma once

#include <cstdint>
#include "ecs/world.h"
#include "events/event_bus.h"

namespace sm::sub {

using SpellDamageLogFn = void (*)(void* user,
                                  std::uint32_t targetEntityId,
                                  float damage,
                                  bool lethal);
using SpellCanHitFn = bool (*)(void* user,
                               const ecs::Projectile& projectile,
                               std::uint32_t targetEntityId);

void tick_spell_projectiles(ecs::World& w,
                            EventBus* bus,
                            float dt,
                            SpellDamageLogFn logFn = nullptr,
                            void* logUser = nullptr,
                            SpellCanHitFn canHitFn = nullptr,
                            void* canHitUser = nullptr);

} // namespace sm::sub
