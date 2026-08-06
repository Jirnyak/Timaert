// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/squad.h"
#include "ecs/components.h"
#include "core/torus.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sm {

namespace {

// TS uses Math.random() inside behaviours. The C++ port uses App-owned
// MacroNpcAiRuntime RNG so world resets do not inherit hidden module state.
inline int rand_int(const TickContext& ctx, int range) {
    if (range <= 0) return 0;
    return int(ctx.rng->next_u32() % std::uint32_t(range));
}

inline float rand_f01(const TickContext& ctx) { return ctx.rng->next_f01(); }

// ── Helpers shared by all behaviours ──────────────────────────

struct XY { float x, y; };

XY pick_random_nearby(float cx, float cy, int range, const TickContext& ctx) {
    float nx = wrapf(cx + float(rand_int(ctx, range * 2) - range),
                     float(ctx.mapW));
    float ny = wrapf(cy + float(rand_int(ctx, range * 2) - range),
                     float(ctx.mapH));
    return {nx, ny};
}

bool home_pos(const ecs::MacroNpcRuntime& rt, const TickContext& ctx, XY& out) {
    if (rt.homeSettlementId < 0) return false;
    for (auto& s : ctx.gs->settlements) {
        if (s.id == rt.homeSettlementId) {
            out = {float(s.x), float(s.y)};
            return true;
        }
    }
    return false;
}

bool at_target(const ecs::Position& p, const ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    return torus_dist_sq(p.x, p.y, rt.targetX, rt.targetY,
                         float(ctx.mapW), float(ctx.mapH)) < 4.0f;
}

// macro_npc_max_sp moved to macro/squad.h: the auto-battle fatigue reads the
// same ceiling this file's regen fills — one law, one home.

bool prepare_macro_npc_tick(ecs::MacroNpcRuntime& rt,
                            const ecs::Health& hp) {
    if (hp.hp <= 0.0f) {
        rt.visualSpeed = 0.0f;
        return false;
    }

    // Time-in-cell advances every AI tick (both sweep drivers pass through
    // here); a try_move that changes cell resets it right after.
    rt.entryTicks = saturate_entry_ticks(rt.entryTicks);

    const auto state = static_cast<NPCState>(rt.state);
    const int maxSp = macro_npc_max_sp(hp);
    if ((state == NPCState::Idle || state == NPCState::Resting)
        && int(rt.sp) < maxSp) {
        const int regen = std::max(1, int(std::ceil(float(maxSp) * 0.05f)));
        rt.sp = std::int16_t(std::min(maxSp, int(rt.sp) + regen));
    }

    if (state == NPCState::Resting) {
        if (int(rt.sp) >= maxSp / 2) {
            rt.state = std::uint8_t(NPCState::Idle);
            rt.stateTimer = 0;
        }
        rt.visualSpeed = 0.0f;
        return false;
    }

    return true;
}

void set_visual_speed(ecs::MacroNpcRuntime& rt, float oldX, float oldY,
                      float newX, float newY) {
    float dx = newX - oldX, dy = newY - oldY;
    float dist = std::sqrt(dx * dx + dy * dy);
    rt.visualSpeed = dist > 0.0f ? dist / kAiPeriodSeconds : 0.0f;
}

void try_move(ecs::Position& p, ecs::MacroNpcRuntime& rt,
              float tx, float ty, const TickContext& ctx) {
    int ix = int(p.x), iy = int(p.y);
    int itx = int(tx), ity = int(ty);
    Step s = torus_step_toward(ix, iy, itx, ity, ctx.mapW, ctx.mapH);
    float oldX = p.x, oldY = p.y;
    p.x = float(s.nx);
    p.y = float(s.ny);
    if (s.nx != ix || s.ny != iy) {
        // Entry-side stamp: the signed step of THIS cell change, torus-folded
        // (stepping east off the map's edge is still +1, not -(w-1)).
        int dx = s.nx - ix;
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = s.ny - iy;
        if (dy > 1) dy = -1; else if (dy < -1) dy = 1;
        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;
    }
    set_visual_speed(rt, oldX, oldY, p.x, p.y);
    rt.sp -= 10;
    if (rt.sp < 0) {
        rt.state = std::uint8_t(NPCState::Resting);
        rt.stateTimer = 0;
        rt.sp = 0;
    }
}

bool find_nearest_tree_grid(const TreeGrid& g, float px, float py,
                            int mw, int mh, XY& out) {
    if (!g.trees || g.trees->empty()) return false;
    float halfW = float(mw) * 0.5f;
    float halfH = float(mh) * 0.5f;
    int cx0 = int(std::floor(px / float(g.cellSize)));
    int cy0 = int(std::floor(py / float(g.cellSize)));
    float best = 901.0f;        // 30² + 1
    bool found = false;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int gx = wrapi(cx0 + ox, g.cols);
            int gy = wrapi(cy0 + oy, g.rows);
            const auto& bucket = g.buckets[std::size_t(gy * g.cols + gx)];
            for (std::uint32_t idx : bucket) {
                const auto& t = (*g.trees)[idx];
                float dx = std::fabs(px - float(t.x));
                float dy = std::fabs(py - float(t.y));
                if (dx > halfW) dx = float(mw) - dx;
                if (dy > halfH) dy = float(mh) - dy;
                if (dx > 30.0f || dy > 30.0f) continue;
                float d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    out = {float(t.x), float(t.y)};
                    found = true;
                }
            }
        }
    }
    return found;
}

// ── Behaviours ────────────────────────────────────────────────

using NS = NPCState;

void ai_home_wanderer(ecs::Position& p, ecs::MacroNpcRuntime& rt,
                      const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            float dsq = torus_dist_sq(p.x, p.y, home.x, home.y,
                                      float(ctx.mapW), float(ctx.mapH));
            if (dsq > 400.0f) {
                rt.targetX = home.x; rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 12, ctx);
                rt.targetX = t.x; rt.targetY = t.y;
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_woodcutter(ecs::Position& p, ecs::MacroNpcRuntime& rt,
                   const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY tree;
            bool got = ctx.treeGrid
                ? find_nearest_tree_grid(*ctx.treeGrid, p.x, p.y,
                                         ctx.mapW, ctx.mapH, tree)
                : false;
            if (got) {
                rt.targetX = tree.x; rt.targetY = tree.y;
                rt.state = std::uint8_t(NS::Traveling);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 10, ctx);
                rt.targetX = t.x; rt.targetY = t.y;
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 8));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            rt.targetX = home.x; rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(6 + rand_int(ctx, 12));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_trader(ecs::Position& p, ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    auto& settles = ctx.gs->settlements;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // Pick another settlement (id != home).
            int candidates = 0;
            for (auto& s : settles) if (s.id != rt.homeSettlementId) ++candidates;
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.id == rt.homeSettlementId) continue;
                    if (pick-- == 0) {
                        rt.targetSettlementId = s.id;
                        rt.targetX = float(s.x); rt.targetY = float(s.y);
                        rt.state  = std::uint8_t(NS::Traveling);
                        break;
                    }
                }
            } else {
                rt.stateTimer = 20;
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(15 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            rt.targetX = home.x; rt.targetY = home.y;
            rt.targetSettlementId = -1;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(20 + rand_int(ctx, 30));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_nomad(ecs::Position& p, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx) {
    auto& settles = ctx.gs->settlements;
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            int candidates = 0;
            for (auto& s : settles) if (s.id != rt.targetSettlementId) ++candidates;
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.id == rt.targetSettlementId) continue;
                    if (pick-- == 0) {
                        rt.targetSettlementId = s.id;
                        rt.targetX = float(s.x); rt.targetY = float(s.y);
                        rt.state  = std::uint8_t(NS::Traveling);
                        break;
                    }
                }
            } else {
                rt.stateTimer = 10;
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_aggressive(ecs::Position& p, ecs::MacroNpcRuntime& rt,
                   const TickContext& ctx) {
    float dsq = torus_dist_sq(p.x, p.y, ctx.playerX, ctx.playerY,
                              float(ctx.mapW), float(ctx.mapH));
    if (dsq < 100.0f) {
        rt.state = std::uint8_t(NS::Chasing);
        rt.targetX = ctx.playerX;
        rt.targetY = ctx.playerY;
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Chasing)) {
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = std::int16_t(5 + rand_int(ctx, 10));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 20, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_patrol(ecs::Position& p, ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    float dh = torus_dist_sq(p.x, p.y, home.x, home.y,
                             float(ctx.mapW), float(ctx.mapH));
    if (dh > 144.0f) {
        rt.targetX = home.x; rt.targetY = home.y;
        rt.state = std::uint8_t(NS::Returning);
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(home.x, home.y, 8, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Patrolling);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Patrolling)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(6 + rand_int(ctx, 10));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_teleporter(ecs::Position& p, ecs::MacroNpcRuntime& rt,
                   const TickContext& ctx) {
    if (rt.teleportCooldown > 0) --rt.teleportCooldown;
    if (rt.teleportCooldown <= 0 && rand_f01(ctx) < 0.005f) {
        XY t = pick_random_nearby(p.x, p.y, 40, ctx);
        p.x = t.x; p.y = t.y;
        // A jump has no entry edge — the sentinel degrades placement to the
        // whole-cell scatter instead of inventing a side.
        rt.entryDir = kEntryDirNone;
        rt.entryTicks = 0;
        rt.teleportCooldown = 50;
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = 10;
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 15, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(12 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_wanderer(ecs::Position& p, ecs::MacroNpcRuntime& rt,
                 const TickContext& ctx) {
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 25, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

// ── Squad↔squad perception and war (Session 15) ───────────────────────────
//
// ONE universal step, run before every role behaviour: does a hostile squad
// stand near me, and what do I do about it? Before this, macro NPCs were
// ghosts to each other — the only "other" any behaviour ever saw was the
// player. The rules are the owner's design (macrosim.md):
//   · perception through the transient SquadIndex, hostility through the ONE
//     relation matrix at the ONE line (faction.h kHostileThreshold) — the
//     same numbers the subworld battle masks read;
//   · flee or fight decided by squad_power — the SAME strength law the
//     resolver uses, so a squad never runs from a fight the law says it
//     wins. Traits modulate courage (Cowardly breaks early, Brave stands);
//   · fighters pursue (the same data column subworld hostility reads:
//     subworld_ai_for(row.ai) — bandits raid, patrols hunt), civilians run;
//   · a geometric meeting (same macro cell) IS the fight: resolved by the
//     one auto-battle law, settled through the one ledger.

constexpr float kSquadSightCells = 6.0f;
// Flee when the enemy is this many times stronger; trait-scaled below.
constexpr float kBraveryBase   = 1.5f;
constexpr float kBraveryCoward = 0.6f;   // Cowardly: breaks far earlier
constexpr float kBraveryBrave  = 1.8f;   // Brave: stands into worse odds
// Pursue only fights the law says we win with margin.
constexpr float kPursueMargin  = 1.1f;

float bravery_of(const ecs::NpcTraits* traits) {
    float b = kBraveryBase;
    if (!traits) return b;
    for (std::uint8_t i = 0; i < traits->count; ++i) {
        if (traits->traits[i] == std::uint8_t(NPCTrait::Cowardly))
            b *= kBraveryCoward;
        if (traits->traits[i] == std::uint8_t(NPCTrait::Brave))
            b *= kBraveryBrave;
    }
    return b;
}

entt::entity nearest_hostile_squad(entt::entity self, const ecs::Position& p,
                                   const ecs::NPCKind& kind,
                                   const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    if (g.cols <= 0 || g.rows <= 0) return entt::null;
    auto& reg = ctx.world->reg;
    const char* myFaction = faction_id_for_index(kind.factionIdx);
    const int cx0 = int(p.x) / g.cellSize;
    const int cy0 = int(p.y) / g.cellSize;
    float best = kSquadSightCells * kSquadSightCells + 1.0f;
    entt::entity found = entt::null;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = wrapi(cx0 + ox, g.cols);
            const int gy = wrapi(cy0 + oy, g.rows);
            for (entt::entity e : g.buckets[std::size_t(gy * g.cols + gx)]) {
                if (e == self || !reg.valid(e)) continue;
                const auto* op = reg.try_get<ecs::Position>(e);
                const auto* ok = reg.try_get<ecs::NPCKind>(e);
                if (!op || !ok) continue;
                const float d = torus_dist_sq(p.x, p.y, op->x, op->y,
                                              float(ctx.mapW),
                                              float(ctx.mapH));
                if (d >= best) continue;
                if (faction_relation(ctx.gs, myFaction,
                                     faction_id_for_index(ok->factionIdx))
                        >= kHostileThreshold) {
                    continue;
                }
                best = d;
                found = e;
            }
        }
    }
    return found;
}

// Returns true when the threat consumed this think (fled, pursued or
// fought); the role behaviour then waits for a calmer half hour.
bool squad_threat_step(entt::entity self, ecs::Position& p,
                       const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                       const TickContext& ctx) {
    if (!ctx.world || !ctx.squads || !ctx.gs) return false;

    const entt::entity enemy = nearest_hostile_squad(self, p, kind, ctx);
    if (enemy == entt::null) {
        // Threat gone: a fleeing squad calms down and resumes its life.
        if (rt.state == std::uint8_t(NS::Fleeing)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = 0;
        }
        return false;
    }

    auto& reg = ctx.world->reg;
    const auto& ep = reg.get<ecs::Position>(enemy);
    const float myPower = squad_power(auto_battle_side_of(*ctx.world, self));
    const float theirPower =
        squad_power(auto_battle_side_of(*ctx.world, enemy));

    // The geometric meeting: same macro cell = the fight happens, resolved
    // by the ONE law and settled through the ONE ledger. An ambush is a
    // pursuer catching a squad that never saw it coming.
    if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
        if (!ctx.allowAutoBattle) return false;
        auto* ert = reg.try_get<ecs::MacroNpcRuntime>(enemy);
        const bool ambush =
            rt.state == std::uint8_t(NS::Chasing) && ert
            && ert->state != std::uint8_t(NS::Chasing)
            && ert->state != std::uint8_t(NS::Fleeing);
        const AutoBattleOutcome o = resolve_auto_battle(
            auto_battle_side_of(*ctx.world, self),
            auto_battle_side_of(*ctx.world, enemy),
            ambush ? Ambush::SideA : Ambush::None, *ctx.rng);
        settle_auto_battle(*ctx.gs, *ctx.world, self, enemy, o);
        rt.visualSpeed = 0.0f;
        if (!reg.all_of<ecs::Dead>(self)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(3 + rand_int(ctx, 5));
        }
        // A beaten-but-alive enemy runs; distance is what prevents an
        // immediate rematch, and the winner's next think re-evaluates.
        if (ert && reg.valid(enemy) && !reg.all_of<ecs::Dead>(enemy)) {
            ert->state = std::uint8_t(NS::Fleeing);
        }
        return true;
    }

    const float bravery =
        bravery_of(reg.try_get<ecs::NpcTraits>(self));
    if (theirPower > myPower * bravery) {
        // Run directly away, torus-folded, a screen's worth of cells out.
        float dx = p.x - ep.x, dy = p.y - ep.y;
        if (dx > float(ctx.mapW) * 0.5f) dx -= float(ctx.mapW);
        if (dx < -float(ctx.mapW) * 0.5f) dx += float(ctx.mapW);
        if (dy > float(ctx.mapH) * 0.5f) dy -= float(ctx.mapH);
        if (dy < -float(ctx.mapH) * 0.5f) dy += float(ctx.mapH);
        const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        rt.targetX = wrapf(p.x + dx / len * 8.0f, float(ctx.mapW));
        rt.targetY = wrapf(p.y + dy / len * 8.0f, float(ctx.mapH));
        rt.state = std::uint8_t(NS::Fleeing);
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return true;
    }

    // Fighters close in when the law says they win: the same data column
    // that decides subworld combat stance decides who is a fighter at all.
    const bool fighter = combatant_behaviour(kNpcTypeDefs[kind.type].ai);
    if (fighter && myPower > theirPower * kPursueMargin) {
        rt.state = std::uint8_t(NS::Chasing);
        rt.targetX = ep.x;
        rt.targetY = ep.y;
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return true;
    }

    if (rt.state == std::uint8_t(NS::Fleeing)) {
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = 0;
    }
    return false;
}

void dispatch(AIBehaviour b, entt::entity e, ecs::Position& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx) {
    if (squad_threat_step(e, p, kind, rt, ctx)) return;
    switch (b) {
        case AIBehaviour::HomeWanderer: ai_home_wanderer(p, rt, ctx); break;
        case AIBehaviour::Woodcutter:   ai_woodcutter   (p, rt, ctx); break;
        case AIBehaviour::Trader:       ai_trader       (p, rt, ctx); break;
        case AIBehaviour::Nomad:        ai_nomad        (p, rt, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive   (p, rt, ctx); break;
        case AIBehaviour::Patrol:       ai_patrol       (p, rt, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter   (p, rt, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer     (p, rt, ctx); break;
        case AIBehaviour::Count:        break;
    }
}

} // namespace

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize) {
    g.cellSize = cellSize;
    g.cols     = (mapW + cellSize - 1) / cellSize;
    g.rows     = (mapH + cellSize - 1) / cellSize;
    g.buckets.assign(std::size_t(g.cols) * g.rows, {});
    g.trees    = &trees;
    for (std::uint32_t i = 0; i < trees.size(); ++i) {
        int gx = wrapi(trees[i].x / cellSize, g.cols);
        int gy = wrapi(trees[i].y / cellSize, g.rows);
        g.buckets[std::size_t(gy * g.cols + gx)].push_back(i);
    }
}

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime,
                                std::uint32_t seed) {
    runtime = MacroNpcAiRuntime{};
    runtime.jitter = Rng{seed ^ 0xA1F0u};
}

void build_squad_index(SquadIndex& g, ecs::World& w, int mapW, int mapH,
                       int cellSize) {
    g.cellSize = std::max(1, cellSize);
    g.cols = std::max(1, (mapW + g.cellSize - 1) / g.cellSize);
    g.rows = std::max(1, (mapH + g.cellSize - 1) / g.cellSize);
    const std::size_t n = std::size_t(g.cols) * std::size_t(g.rows);
    if (g.buckets.size() != n) g.buckets.assign(n, {});
    else for (auto& b : g.buckets) b.clear();   // reuse capacity every drive

    // Every live macro squad, and nothing else: the player's flagged body is
    // not prey for the threat step (meeting the player is Inc 6's forced
    // encounter, a different door), and the Dead are no squads at all.
    auto view = w.reg.view<ecs::Position, ecs::NPCKind,
                           ecs::MacroNpcRuntime>(
        entt::exclude<ecs::Dead, ecs::PlayerTag, ecs::SubworldTag>);
    for (auto e : view) {
        const auto& p = view.get<ecs::Position>(e);
        const int gx = wrapi(int(p.x) / g.cellSize, g.cols);
        const int gy = wrapi(int(p.y) / g.cellSize, g.rows);
        g.buckets[std::size_t(gy * g.cols + gx)].push_back(e);
    }
}

void tick_macro_npc_ai(GameState& gs, ecs::World& w,
                       const TreeGrid* treeGrid,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle) {
    auto& reg = w.reg;
    auto view = reg.view<ecs::Position, ecs::NPCKind,
                         ecs::MacroNpcRuntime, ecs::Health>(
        entt::exclude<ecs::Dead, ecs::PlayerTag>);  // never AI-drive a possessed body (Inc 5e-2)

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    TickContext ctx{};
    ctx.mapW     = gs.mapW;
    ctx.mapH     = gs.mapH;
    ctx.gs       = &gs;
    ctx.treeGrid = treeGrid;
    ctx.rng      = &runtime.jitter;
    ctx.playerX  = gs.player.x;
    ctx.playerY  = gs.player.y;
    ctx.world    = &w;
    ctx.squads   = &runtime.squadIndex;
    ctx.allowAutoBattle = allowAutoBattle;

    for (auto e : view) {
        auto& p    = view.get<ecs::Position>(e);
        auto& kind = view.get<ecs::NPCKind>(e);
        auto& rt   = view.get<ecs::MacroNpcRuntime>(e);
        auto& hp   = view.get<ecs::Health>(e);

        // One think per call at most, as before: a caller that hands over a
        // huge jump does not get a burst of catch-up thinking, it gets one.
        rt.tickAccum += std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks));
        if (rt.tickAccum < kAiTicks) continue;
        rt.tickAccum -= kAiTicks;

        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        // A battle earlier in this very sweep may have killed this squad —
        // the view's Dead exclusion was evaluated at entry, so re-check.
        if (reg.all_of<ecs::Dead>(e)) continue;
        if (!prepare_macro_npc_tick(rt, hp)) continue;
        AIBehaviour b = kNpcTypeDefs[kind.type].ai;
        dispatch(b, e, p, kind, rt, ctx);
    }
}

void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt) {
    if (mapW <= 0 || mapH <= 0 || dt <= 0.0f) return;

    auto view = w.reg.view<ecs::Position, ecs::VisualPos,
                           ecs::MacroNpcRuntime, ecs::Health>(
        entt::exclude<ecs::Dead, ecs::SubworldTag, ecs::PlayerTag>);  // player drawn by its own marker (Inc 5e-2)
    for (auto e : view) {
        const auto& p = view.get<ecs::Position>(e);
        auto& v = view.get<ecs::VisualPos>(e);
        const auto& rt = view.get<ecs::MacroNpcRuntime>(e);
        const auto& hp = view.get<ecs::Health>(e);
        if (hp.hp <= 0.0f || !std::isfinite(v.vx) || !std::isfinite(v.vy)) {
            v.vx = p.x;
            v.vy = p.y;
            v.speed = 0.0f;
            continue;
        }

        const float dx = p.x - v.vx;
        const float dy = p.y - v.vy;
        const float dSq = dx * dx + dy * dy;
        if (dSq > 9.0f) {
            v.vx = p.x;
            v.vy = p.y;
            v.speed = 0.0f;
            continue;
        }

        const float speed = rt.visualSpeed > 0.0f ? rt.visualSpeed : 2.0f;
        v.speed = speed;
        const float step = speed * dt;
        if (step <= 0.0f || dSq <= 0.000001f) continue;

        const float d = std::sqrt(dSq);
        if (d <= step) {
            v.vx = p.x;
            v.vy = p.y;
        } else {
            const float ratio = step / d;
            v.vx += dx * ratio;
            v.vy += dy * ratio;
        }
    }
}

MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    GameState& gs, ecs::World& w, const TreeGrid* treeGrid,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle) {
    MacroNpcAiSliceResult result{};
    if (max_npc_ticks <= 0) return result;

    constexpr int kMaxQueuedSweeps = 4;
    if (ticks > 0) {
        runtime.sweepAccum +=
            std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks * kMaxQueuedSweeps));
        while (runtime.sweepAccum >= kAiTicks) {
            runtime.sweepAccum -= kAiTicks;
            if (runtime.pendingSweeps < kMaxQueuedSweeps) {
                ++runtime.pendingSweeps;
            } else {
                result.backlog = true;
            }
        }
    }
    if (runtime.pendingSweeps <= 0) return result;

    auto& reg = w.reg;
    auto view = reg.view<ecs::Position, ecs::NPCKind,
                         ecs::MacroNpcRuntime, ecs::Health>(
        entt::exclude<ecs::Dead, ecs::PlayerTag>);  // never AI-drive a possessed body (Inc 5e-2)

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    TickContext ctx{};
    ctx.mapW     = gs.mapW;
    ctx.mapH     = gs.mapH;
    ctx.gs       = &gs;
    ctx.treeGrid = treeGrid;
    ctx.rng      = &runtime.jitter;
    ctx.playerX  = gs.player.x;
    ctx.playerY  = gs.player.y;
    ctx.world    = &w;
    ctx.squads   = &runtime.squadIndex;
    ctx.allowAutoBattle = allowAutoBattle;

    while (runtime.pendingSweeps > 0
           && result.npcsProcessed < max_npc_ticks) {
        std::size_t index = 0;
        bool sawEntity = false;
        bool reachedEnd = true;

        for (auto e : view) {
            sawEntity = true;
            if (index++ < runtime.sweepCursor) continue;

            auto& p    = view.get<ecs::Position>(e);
            auto& kind = view.get<ecs::NPCKind>(e);
            auto& rt   = view.get<ecs::MacroNpcRuntime>(e);
            auto& hp   = view.get<ecs::Health>(e);
            if (kind.type < std::uint16_t(NPCType::Count)
                && !reg.all_of<ecs::Dead>(e)) {   // may have died this sweep
                if (prepare_macro_npc_tick(rt, hp)) {
                    dispatch(kNpcTypeDefs[kind.type].ai, e, p, kind, rt, ctx);
                }
                ++result.npcsProcessed;
            }
            ++runtime.sweepCursor;

            if (result.npcsProcessed >= max_npc_ticks) {
                reachedEnd = false;
                break;
            }
        }

        if (!sawEntity) {
            runtime.pendingSweeps = 0;
            runtime.sweepCursor = 0;
            break;
        }
        if (!reachedEnd) break;

        --runtime.pendingSweeps;
        ++result.sweepsCompleted;
        runtime.sweepCursor = 0;
    }
    result.backlog = result.backlog || runtime.pendingSweeps > 0;
    return result;
}

} // namespace sm
