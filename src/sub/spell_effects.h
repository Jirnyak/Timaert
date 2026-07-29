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

// Transient-VFX seam for the spell tick. `event` selects what happened; the
// engine turns it into a particle burst (this TU stays renderer/particle-free,
// exactly like SpellDamageLogFn keeps it log-free). `entity` is ALWAYS the bolt
// (still valid at both call sites, before it is reaped) so the engine can read
// its Sprite tint and colour the effect. `ax,ay`→`bx,by` is the tile-space
// segment the bolt travelled this tick (Trail); for Impact `bx,by` is the
// detonation point and `ax,ay` == `bx,by`. `blastRadius` is the bolt's (0 for a
// point hit) — the engine picks the burst archetype from it.
enum class SpellFxEvent : std::uint8_t { Trail = 0, Impact = 1 };
using SpellFxEmitFn = void (*)(void* user,
                               SpellFxEvent event,
                               std::uint32_t entity,
                               float ax, float ay, float az,
                               float bx, float by, float bz,
                               float blastRadius);

void tick_spell_projectiles(ecs::World& w,
                            EventBus* bus,
                            float dt,
                            SpellDamageLogFn logFn = nullptr,
                            void* logUser = nullptr,
                            SpellCanHitFn canHitFn = nullptr,
                            void* canHitUser = nullptr,
                            SpellFxEmitFn fxFn = nullptr,
                            void* fxUser = nullptr);

} // namespace sm::sub
