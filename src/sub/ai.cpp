#include "sub/ai.h"
#include "sub/map_data.h"
#include "ecs/components.h"
#include "core/rng.h"
#include "macro/npc.h"   // cruiseM — крейсерская высота рода летуна
#include <cmath>
#include <algorithm>

namespace sm::sub {

static constexpr float kFleeRadius    = 60.0f;
static constexpr float kFleeSpeedMult = 2.2f;

// This file is a BRAIN, not a body. It decides what a mind wants — a wander
// bearing, a flee vector — and writes that intent into SubworldAi.wantVx/Vy.
// It never touches Position: the ONE mover (sub/movement.cpp) is the
// one owner of every body's legs, and it executes intent with the same
// separation, solid-slide, terrain cost and world bounds every other mover
// gets. The private 30-line integrator that used to live here was a second
// mover fighting the first — steering strangled its velocity and wrote the
// remains back, so wander was a 0.1 s twitch per decision — and its
// wall-handling was a near-verbatim copy of steering's. Both are gone.

void tick_npc_ai(ecs::World& w, float px, float py,
                 std::uint32_t /*playerEnt*/, float dt,
                 PlayerThreatFn threatFn,
                 void* threatUser,
                 GroundHeightFn heightFn,
                 void* heightUser) {
    auto& reg = w.reg;

    auto view = reg.view<ecs::Position, ecs::SubworldAi>();
    for (auto e : view) {
        // A body wearing the scene flag (AvatarTag) is driven by player
        // input (its authoritative Position is written by the engine), not by
        // its own brain. Skip it entirely so Wander/Flee never fights the
        // player. No component churn on possess/vacate — when the flag leaves,
        // the body's AI resumes automatically on the very next tick.
        if (reg.any_of<ecs::AvatarTag>(e)) continue;
        auto& p = view.get<ecs::Position>(e);
        auto& a = view.get<ecs::SubworldAi>(e);
        // Deterministic per-decision seed: entity bits, the DECISION COUNTER
        // (see SubworldAi.seq — position alone froze standing minds), and a
        // coarse position bucket so a herd doesn't turn in lockstep.
        std::uint32_t salt = std::uint32_t(entt::to_integral(e)) * 2654435761u;
        Rng rng(salt ^ (a.seq * 0x9E3779B9u)
                ^ std::uint32_t(p.x * 31.7f) ^ std::uint32_t(p.y * 17.3f));

        switch (a.kind) {
        case ecs::SubworldAi::Wander: {
            a.aiTimer -= dt;
            if (a.aiTimer <= 0.0f) {
                ++a.seq;
                if (rng.next_f01() < 0.40f) {
                    a.wantVx = a.wantVy = 0.0f;
                } else {
                    float ang = rng.next_f01() * 6.2831853f;
                    a.wantVx = std::cos(ang) * a.wanderSpeed;
                    a.wantVy = std::sin(ang) * a.wanderSpeed;
                }
                a.aiTimer = 1.5f + rng.next_f01() * 3.0f;
            }
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
                a.wantVx = dx / d * fs;
                a.wantVy = dy / d * fs;
                a.aiTimer = 0.5f;
            } else {
                // Nothing to run from: amble like a wanderer.
                a.aiTimer -= dt;
                if (a.aiTimer <= 0.0f) {
                    ++a.seq;
                    if (rng.next_f01() < 0.40f) {
                        a.wantVx = a.wantVy = 0.0f;
                    } else {
                        float ang = rng.next_f01() * 6.2831853f;
                        a.wantVx = std::cos(ang) * a.wanderSpeed;
                        a.wantVy = std::sin(ang) * a.wanderSpeed;
                    }
                    a.aiTimer = 1.5f + rng.next_f01() * 3.0f;
                }
            }
            break;
        }
        case ecs::SubworldAi::Combat:
            // The Combat mind wants nothing HORIZONTAL here: its drive is the
            // influence field and the contact scan inside the battle pass.
            // Writing an x/y intent for it would put a second voice in that
            // body's head. (The VERTICAL below is not a second voice — the
            // battle pass has no vertical voice at all; a fighting flyer
            // holds its cruise and bites from altitude until the day a
            // fighting MELEE flyer needs the dive.)
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

        // ── Вертикальное намерение — ТРЕТЬЯ ось того же мозга ────────────
        // (полёт-посадка 2026-09-10, владелец: «субмир 3D — все
        // воспринимают x/y/z»; 2D-мозг был пережитком старого субмира.)
        // Пишется только летуну (ecs::Flying): ходока к земле прижимает
        // закон опоры, его wantVz мёртв по построению — один mover, одна
        // рамка, никакого «мозга для летающих». Закон прост: тянись к
        // крейсерской высоте своего рода (ПРЕДПОЧТЕНИЕ характера, не
        // закон — конверт [опора, потолок] остаётся единственным законом),
        // беглец предпочитает вдвое выше: высота — его дорога. Темп
        // подъёма = его же wanderSpeed: одно тело — один темп.
        if (heightFn && reg.any_of<ecs::Flying>(e)) {
            const auto* kind = reg.try_get<ecs::NPCKind>(e);
            const float cruise =
                kind && kind->type < std::uint16_t(NPCType::Count)
                    ? kNpcTypeDefs[kind->type].combat.cruiseM : 0.0f;
            if (cruise > 0.0f) {
                const float floorZ = heightFn(heightUser, p.x, p.y);
                const float targetZ = floorZ
                    + (a.kind == ecs::SubworldAi::Flee ? cruise * 2.0f
                                                       : cruise);
                a.wantVz = std::clamp(targetZ - p.z,
                                      -a.wanderSpeed, a.wanderSpeed);
            }
        }
    }
}

} // namespace sm::sub
