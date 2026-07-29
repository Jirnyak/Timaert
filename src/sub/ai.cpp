#include "sub/ai.h"
#include "sub/map_data.h"
#include "ecs/components.h"
#include "core/rng.h"
#include <cmath>
#include <algorithm>

namespace sm::sub {

// Mirrors `subworld/ai.ts` constants.
static constexpr float kFleeRadius    = 60.0f;
static constexpr float kFleeSpeedMult = 2.2f;

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline void integrate_with_bounds(ecs::Position& p, ecs::SubworldAi& a,
                                         float dt, float worldMax) {
    p.x += a.vx * dt;
    p.y += a.vy * dt;
    const float r = a.radius;
    if (p.x <= r)            { p.x = r;            a.vx =  std::abs(a.vx); }
    if (p.x >= worldMax - r) { p.x = worldMax - r; a.vx = -std::abs(a.vx); }
    if (p.y <= r)            { p.y = r;            a.vy =  std::abs(a.vy); }
    if (p.y >= worldMax - r) { p.y = worldMax - r; a.vy = -std::abs(a.vy); }
}

void tick_npc_ai(ecs::World& w, float px, float py,
                 std::uint32_t /*playerEnt*/, float dt,
                 PlayerThreatFn threatFn,
                 void* threatUser) {
    auto& reg = w.reg;
    const float worldMax = float(kFullSize);

    // ── Modern path: SubworldAi-equipped entities (Wander / Flee / Combat) ──
    auto modernView = reg.view<ecs::Position, ecs::SubworldAi>();
    // Per-entity RNG salt — pure function of entity id, deterministic.
    for (auto e : modernView) {
        // A POSSESSED body carries PlayerTag (Inc 5c): it is driven by player
        // input (its authoritative Position is written by the engine), not by
        // its own brain. Skip it entirely so Wander/Flee/Combat never fights the
        // player. No component churn on possess/vacate — when the flag leaves,
        // the body's AI resumes automatically on the very next tick.
        if (reg.any_of<ecs::PlayerTag>(e)) continue;
        auto& p = modernView.get<ecs::Position>(e);
        auto& a = modernView.get<ecs::SubworldAi>(e);
        // Cheap deterministic per-step jitter; combines entity bits with
        // a coarse time bucket so all NPCs don't change direction in lockstep.
        std::uint32_t salt = std::uint32_t(entt::to_integral(e)) * 2654435761u;
        Rng rng(salt ^ std::uint32_t(p.x * 31.7f) ^ std::uint32_t(p.y * 17.3f));

        switch (a.kind) {
        case ecs::SubworldAi::Wander: {
            a.aiTimer -= dt;
            if (a.aiTimer <= 0.0f) {
                if (rng.next_f01() < 0.40f) {
                    a.vx = a.vy = 0.0f;
                } else {
                    float ang = rng.next_f01() * 6.2831853f;
                    a.vx = std::cos(ang) * a.wanderSpeed;
                    a.vy = std::sin(ang) * a.wanderSpeed;
                }
                a.aiTimer = 1.5f + rng.next_f01() * 3.0f;
            }
            integrate_with_bounds(p, a, dt, worldMax);
            break;
        }
        case ecs::SubworldAi::Flee: {
            float dx = p.x - px, dy = p.y - py;
            float d2 = dx * dx + dy * dy;
            const bool playerIsThreat = threatFn
                ? threatFn(threatUser, std::uint32_t(entt::to_integral(e)))
                : true;
            if (playerIsThreat && d2 < kFleeRadius * kFleeRadius) {
                float d = std::sqrt(d2) + 1e-4f;
                float fs = a.wanderSpeed * kFleeSpeedMult;
                a.vx = dx / d * fs;
                a.vy = dy / d * fs;
                a.aiTimer = 0.5f;
                integrate_with_bounds(p, a, dt, worldMax);
            } else {
                // Fall through to wander branch.
                a.aiTimer -= dt;
                if (a.aiTimer <= 0.0f) {
                    if (rng.next_f01() < 0.40f) {
                        a.vx = a.vy = 0.0f;
                    } else {
                        float ang = rng.next_f01() * 6.2831853f;
                        a.vx = std::cos(ang) * a.wanderSpeed;
                        a.vy = std::sin(ang) * a.wanderSpeed;
                    }
                    a.aiTimer = 1.5f + rng.next_f01() * 3.0f;
                }
                integrate_with_bounds(p, a, dt, worldMax);
            }
            break;
        }
        case ecs::SubworldAi::Combat: {
            if (reg.any_of<ecs::PlayerSoldierTag>(e)
                || reg.any_of<ecs::Combat>(e)) {
                a.vx = a.vy = 0.0f;
                break;
            }
            // No Combat component: degrade to chase-only movement.
            // Real combat actors are moved/attacked by SubworldEngine so
            // they do not integrate twice in one frame.
            float dx = px - p.x, dy = py - p.y;
            float d2 = dx * dx + dy * dy;
            if (d2 > kDetectionRadius * kDetectionRadius) {
                a.vx = a.vy = 0.0f; break;
            }
            float d = std::sqrt(d2) + 1e-4f;
            float speed = a.wanderSpeed / 0.40f;  // recover combat.speed
            float range = a.radius * 1.5f;
            if (d > range) {
                a.vx = dx / d * speed;
                a.vy = dy / d * speed;
                p.x = clampf(p.x + a.vx * dt, 1.0f, worldMax - 1.0f);
                p.y = clampf(p.y + a.vy * dt, 1.0f, worldMax - 1.0f);
            } else {
                a.vx = a.vy = 0.0f;
            }
            break;
        }
        }
    }

    // ── Legacy path: any Position + Combat + NPCKind without SubworldAi ──
    // (keeps macro NPCs that wander into a subworld still functional).
    auto legacyView = reg.view<ecs::Position, ecs::Combat, ecs::NPCKind>(
        entt::exclude<ecs::SubworldAi>);
    for (auto e : legacyView) {
        // Skip player-side bodies: projected soldiers (PlayerSoldierTag) and a
        // possessed body under player control (PlayerTag, Inc 5c).
        if (reg.any_of<ecs::PlayerSoldierTag, ecs::PlayerTag>(e)) continue;
        auto& p = legacyView.get<ecs::Position>(e);
        auto& c = legacyView.get<ecs::Combat>(e);
        float dx = px - p.x, dy = py - p.y;
        float d2 = dx * dx + dy * dy;
        if (d2 > kDetectionRadius * kDetectionRadius) continue;
        float d = std::sqrt(d2);
        if (d > c.attackRange) {
            float nx = dx / (d + 1e-4f), ny = dy / (d + 1e-4f);
            p.x += nx * c.speed * dt;
            p.y += ny * c.speed * dt;
        } else if (c.cooldownTimer <= 0.0f) {
            c.cooldownTimer = c.cooldown;
        }
    }
}

} // namespace sm::sub
