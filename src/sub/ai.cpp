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

static inline void integrate_with_bounds(ecs::Position& p, ecs::SubworldAi& a,
                                         float dt, float worldMax,
                                         SolidCanStandFn canStand,
                                         void* canStandUser) {
    const float fx = p.x;
    const float fy = p.y;
    p.x += a.vx * dt;
    p.y += a.vy * dt;
    const float r = a.radius;
    if (p.x <= r)            { p.x = r;            a.vx =  std::abs(a.vx); }
    if (p.x >= worldMax - r) { p.x = worldMax - r; a.vx = -std::abs(a.vx); }
    if (p.y <= r)            { p.y = r;            a.vy =  std::abs(a.vy); }
    if (p.y >= worldMax - r) { p.y = worldMax - r; a.vy = -std::abs(a.vy); }
    // Solid structures block exactly like the world border: slide along the
    // free axis, reflect the blocked velocity so wanderers pick a new line
    // instead of grinding into masonry. Escape rule: a body already inside a
    // solid may always move (out) — nothing gets trapped.
    if (canStand && !canStand(canStandUser, p.x, p.y, r, p.z)
        && canStand(canStandUser, fx, fy, r, p.z)) {
        if (canStand(canStandUser, p.x, fy, r, p.z)) {
            p.y = fy;
            a.vy = -a.vy;
        } else if (canStand(canStandUser, fx, p.y, r, p.z)) {
            p.x = fx;
            a.vx = -a.vx;
        } else {
            p.x = fx;
            p.y = fy;
            a.vx = -a.vx;
            a.vy = -a.vy;
        }
    }
}

void tick_npc_ai(ecs::World& w, float px, float py,
                 std::uint32_t /*playerEnt*/, float dt,
                 PlayerThreatFn threatFn,
                 void* threatUser,
                 SolidCanStandFn canStand,
                 void* canStandUser) {
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
            integrate_with_bounds(p, a, dt, worldMax, canStand, canStandUser);
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
                integrate_with_bounds(p, a, dt, worldMax, canStand, canStandUser);
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
                integrate_with_bounds(p, a, dt, worldMax, canStand, canStandUser);
            }
            break;
        }
        case ecs::SubworldAi::Combat:
            // Combat bodies are steered by the mass-battle pass (sub/battle.h,
            // driven from SubworldEngine::tick_subworld_combat): it owns their
            // position, velocity and target so nothing integrates twice in one
            // frame. Deliberately inert here.
            //
            // Two paths used to live in this file and BOTH homed on the player
            // scalar: this case, and a "legacy" view over Position+Combat+NPCKind
            // without SubworldAi. They were the second and third global
            // attractors behind the collapse-into-a-point bug, and the legacy one
            // moved bodies that no other system even considered alive
            // (alive_subworld_entity requires SubworldTag, which that view never
            // checked). Both are deleted; the battle pass covers every combat
            // body through one universal path — the player is simply another
            // faction slot in the influence field, not a hardcoded destination.
            break;
        }
    }
}

} // namespace sm::sub
