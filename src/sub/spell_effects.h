// Native spell projectile/effect ticking for subworld ECS entities.
#pragma once

#include <cstdint>
#include <limits>
#include "ecs/world.h"
#include "events/event_bus.h"

namespace sm::sub {

// One duration for the on-hit flash, whatever weapon landed it (melee in
// engine.cpp, projectile/blast/beam/chain here). Was two file-local copies.
inline constexpr float kHitFlashDuration = 0.15f;

using SpellDamageLogFn = void (*)(void* user,
                                  std::uint32_t targetEntityId,
                                  float damage,
                                  bool lethal);
using SpellCanHitFn = bool (*)(void* user,
                               const ecs::Projectile& projectile,
                               std::uint32_t targetEntityId);

// Broad-phase candidate enumeration — the battle bucket grids, reached through
// the engine the same way height and solidity are. Fills `out` with up to
// `maxOut` entity ids whose CENTRES may lie within `r` of (x, y), and returns
// how many. The promise is a SUPERSET: extras are harmless (the exact 3D
// checks in this TU are the only filter that decides hits), a MISS is a bug —
// so the provider pads the query with everything the caller cannot know: the
// fattest body radius in the crowd and the largest per-tick step (the grid is
// built before steering moves bodies). The caller asks in terms of its own
// geometry alone.
//
// Returns -1 when completeness cannot be promised this tick (the battle
// gather hit its ceiling, or the candidates outnumber maxOut): the caller
// then falls back to the full registry scan, so a projectile is never
// blinded by an overloaded broad phase — a miss from exhaustion would be a
// bug, not an approximation (unlike the AI's budgeted contact scan, which is
// a legitimate heuristic). Null = always the full scan (headless tests).
using SpellNeighborsFn = int (*)(void* user, float x, float y, float r,
                                 std::uint32_t* out, int maxOut);

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
                            void* fxUser = nullptr,
                            float (*heightFn)(void*, float, float) = nullptr,
                            void* heightUser = nullptr,
                            // Solid-structure test (sub/collide.h via the
                            // engine): bolts detonate on walls/houses exactly
                            // like they detonate on terrain. Optional — null
                            // keeps structures transparent (headless tests).
                            bool (*solidFn)(void*, float, float, float) = nullptr,
                            void* solidUser = nullptr,
                            // Broad phase (see SpellNeighborsFn above). Null
                            // keeps the O(N·M) full scan — correct, just slow.
                            SpellNeighborsFn neighborsFn = nullptr,
                            void* neighborsUser = nullptr,
                            // THE LID of the 3×3 box. The window is a closed
                            // volume: four walls in XY and a floor of terrain
                            // and masonry were always checked, but the top was
                            // open, so a bolt aimed at the sky flew straight
                            // (projectiles carry no gravity) until its life ran
                            // out. That made the world ANISOTROPIC — bounded
                            // sideways, endless upward — and left the lifetime
                            // as the only thing standing in for geometry.
                            // Same surface flying bodies are clamped to, so the
                            // window has ONE ceiling. Infinity = open sky, which
                            // is the old behaviour and what headless tests with
                            // no renderer get.
                            float ceilingM
                                = std::numeric_limits<float>::infinity());

} // namespace sm::sub
