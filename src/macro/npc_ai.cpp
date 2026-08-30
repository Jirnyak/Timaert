// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "macro/agent_memory.h"
#include "macro/chronicle.h"
#include "macro/currency.h"
#include "macro/economy.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "macro/movement_cost.h"
#include "macro/npc.h"
#include "macro/npc_spawn.h"
#include "macro/squad.h"
#include "macro/travel.h"
#include "ecs/components.h"
#include "core/torus.h"
#include "core/rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

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
    // ONE landmark roster (v62): the id alone names the place.
    const Landmark* lm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!lm) return false;
    out = {float(lm->x), float(lm->y)};
    return true;
}

// The agent's HOME STORE — where a gatherer's haul lands. The same universal
// Inventory the market sells from, resolved by the honest id.
Inventory* home_inventory(const ecs::MacroNpcRuntime& rt,
                          const TickContext& ctx) {
    Landmark* lm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    return lm ? &lm->inventory : nullptr;
}

// Empty the gatherer's own bag of `id` into his home store — the shared
// arrival half of every honest work-loop (woodcutter, farmer).
void deliver_bag_home(entt::entity self, const ecs::MacroNpcRuntime& rt,
                      const TickContext& ctx, const char* id) {
    if (!ctx.mw.world) return;
    auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(self);
    if (!bag) return;
    const int n = bag->inv.count(id);
    Inventory* store = home_inventory(rt, ctx);
    if (n > 0 && store) {
        bag->inv.remove(id, n);
        store->add(id, n);
    }
}

// The nearest FT_Field of the agent's HOME — fields are stamped within two
// cells of a village (spawners.cpp), so a small box around home covers them.
bool find_home_field(const TickContext& ctx, float px, float py,
                     const XY& home, XY& out) {
    if (!ctx.mw.features) return false;
    bool found = false;
    float best = 1e30f;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const int cx = int(home.x) + dx;
            const int cy = int(home.y) + dy;
            if (ctx.mw.features->at(cx, cy) != FT_Field) continue;
            const float d = torus_dist_sq(px, py, float(cx), float(cy),
                                          float(ctx.mapW), float(ctx.mapH));
            if (d < best) {
                best = d;
                out = {float(cx), float(cy)};
                found = true;
            }
        }
    }
    return found;
}

bool at_target(const ecs::Position& p, const ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    return torus_dist_sq(p.x, p.y, rt.targetX, rt.targetY,
                         float(ctx.mapW), float(ctx.mapH)) < 4.0f;
}

// Settle the fractional SP carry into whole points — BOTH directions (march
// costs push it negative, rest regen positive), the player's fractional-carry
// idiom. The bar clamps at maxSp above and keeps its DEBT below zero, exactly
// like the player's (movement_cost.h apply_stamina_cost).
void settle_sp_carry(ecs::MacroNpcRuntime& rt) {
    // The runtime keeps its bar in an int16, so the shared law works on an int
    // and the narrowing lives here, at the one place that owns the field.
    int sp = int(rt.sp);
    sm::settle_sp_carry(sp, int(rt.maxSp), rt.spCarry);
    rt.sp = std::int16_t(std::clamp(sp, -32768, 32767));
}

// Why a think is or is not dispatched. A corpse is skipped WHOLE; a camping
// body skips only the behaviour — it still lives through the think, and the
// rhythm below still pays it. Telling the two apart is the whole reason this
// is an enum and not a bool: as one, a resting squad "failed to prepare" and
// was `continue`d past its own recovery.
enum class ThinkGate : std::uint8_t { Dead, Rest, Think };

void settle_exhaustion(entt::entity e, const ecs::Position& p,
                       ecs::MacroNpcRuntime& rt, ecs::Health& hp,
                       bool canCamp, const TickContext& ctx);
bool cell_is_water(const TickContext& ctx, int x, int y);

// What this leader's load is costing him per cell, asked once per think: a
// bag changes at every market, so unlike the rest of the sheet cache this one
// cannot be refreshed at birth and left alone.
void refresh_overload_cost(ecs::MacroNpcRuntime& rt,
                           const ecs::NpcInventory* bag) {
    if (!bag) { rt.overloadCost = 0; return; }
    const int cost = overload_charge_from_capacity(rt.carryCap, bag->inv).cost;
    rt.overloadCost = std::int16_t(std::clamp(cost, 0, 32767));
}

ThinkGate prepare_macro_npc_tick(ecs::MacroNpcRuntime& rt,
                                 const ecs::Health& hp) {
    if (hp.hp <= 0.0f) {
        rt.visualSpeed = 0.0f;
        return ThinkGate::Dead;
    }

    // Time-in-cell advances every AI tick (both sweep drivers pass through
    // here); a try_move that changes cell resets it right after.
    rt.entryTicks = saturate_entry_ticks(rt.entryTicks);

    const auto state = static_cast<NPCState>(rt.state);
    const int maxSp = std::max<int>(1, rt.maxSp);

    // Getting up is a DECISION (owner: «до скольки отдыхать — решение
    // конечного автомата»), and half a bar is this AI's answer to it. The
    // player's answer is his own (aim_rest_until_rested runs to a full bar);
    // both drink from the one regen law below, which is where the mechanic
    // ends and the decider begins.
    if (state == NPCState::Resting) {
        if (int(rt.sp) >= maxSp / 2) {
            rt.state = std::uint8_t(NPCState::Idle);
            rt.stateTimer = 0;
        }
        rt.visualSpeed = 0.0f;
        return ThinkGate::Rest;
    }

    return ThinkGate::Think;
}

// THE rhythm of a think, settled after it, for every behaviour: a body that
// MOVED pays (and, on an empty bar, pays in flesh); a body that STOOD rests.
// One sentence, «остановился — отдыхаешь», and its exact negation.
//
// The regen used to live before the think and be gated on a STATE WHITELIST
// (Idle or Resting), which is the squads' own dialect of the same idea: a
// body doing anything else — standing at a market, waiting out a siege —
// recovered nothing, while the player recovered whenever his route was empty.
// Now both ask the one question the law actually asks: did you move?
//
// Standing where no camp is possible is not rest either — the same decision
// the bite below consults, so an ocean stays lethal without the mechanic ever
// naming water.
//
// STOPPED is not the same question as "did not change cell this think", and
// getting that wrong is how the first cut of this law paid marchers to march:
// the pace is fractional (kMacroWalkCellsPerHour against a think's slice of
// the day), so a body on the road banks part-cells and stands still on maybe a
// quarter of its thinks. Reading those as rest handed a walking squad free
// stamina and no squad ever ran out again. A body has stopped when it is where
// it meant to be, or when it has DECIDED to stop — which is exactly the
// player's own gate, «маршрут пуст», said in the squads' words.
void settle_march_rhythm(entt::entity e, const ecs::Position& p,
                         ecs::MacroNpcRuntime& rt, ecs::Health& hp,
                         bool moved, const TickContext& ctx) {
    const int maxSp = std::max<int>(1, rt.maxSp);
    const bool canCamp = !cell_is_water(ctx, int(p.x), int(p.y));
    const bool stopped = rt.state == std::uint8_t(NPCState::Resting)
                         || at_target(p, rt, ctx);

    // ...and `!moved` on top, because a think that arrived still MARCHED: you
    // do not walk two cells and take a slice of rest in the same breath. Rest
    // begins on the first think after the legs stop.
    if (stopped && !moved) {
        if (canCamp && int(rt.sp) < maxSp) {
            // THE regen law (attributes.h kSpRegenPctPerHour): a percent of
            // the bar per game hour, the leader's marathon skill speeding the
            // rate, paid out in this think's slice of the day. The old
            // 5%-per-think was ~53% of the bar per game HOUR — a rest that
            // cost nothing. Fractional carry, the player's own idiom.
            rt.spCarry += float(maxSp) * kSpRegenPctPerHour
                          * skill_bonus_mult(int(rt.marathonRank))
                          * kAiTickGameHours;
            settle_sp_carry(rt);
        }
    }

    // A body that took a step with its bar already spent pays for it, whether
    // or not it also counts as stopped — the two halves answer two questions.
    if (moved) settle_exhaustion(e, p, rt, hp, canCamp, ctx);
}

void set_visual_speed(ecs::MacroNpcRuntime& rt, float oldX, float oldY,
                      float newX, float newY) {
    float dx = newX - oldX, dy = newY - oldY;
    float dist = std::sqrt(dx * dx + dy * dy);
    rt.visualSpeed = dist > 0.0f ? dist / kAiPeriodSeconds : 0.0f;
}

// Weight of a macro cell from the baked grid the player's A* walks
// (ctx.mw.pathCost); a missing grid reads as a featureless free-road world.
float cell_weight(const TickContext& ctx, int x, int y) {
    const PathCostData* pc = ctx.mw.pathCost;
    if (!pc || pc->width <= 0 || pc->height <= 0
        || pc->costGrid.size()
               != std::size_t(pc->width) * std::size_t(pc->height)) {
        return 1.0f;
    }
    const int wx = wrapi(x, pc->width);
    const int wy = wrapi(y, pc->height);
    return pc->costGrid[std::size_t(wy) * std::size_t(pc->width)
                        + std::size_t(wx)];
}

// The EDGE weight of one greedy step — the cell half from the baked grid
// plus the uphill climb half (movement_cost.h: the law's two halves; the
// squad walks the same slopes the player and both A*s pay for).
float edge_weight(const TickContext& ctx, int fx, int fy, int tx, int ty) {
    const PathCostData* pc = ctx.mw.pathCost;
    float w = cell_weight(ctx, tx, ty);
    if (pc && pc->width > 0 && pc->height > 0
        && pc->height8.size() == pc->costGrid.size()) {
        const std::size_t fi =
            std::size_t(wrapi(fy, pc->height)) * std::size_t(pc->width)
            + std::size_t(wrapi(fx, pc->width));
        const std::size_t ti =
            std::size_t(wrapi(ty, pc->height)) * std::size_t(pc->width)
            + std::size_t(wrapi(tx, pc->width));
        w += pc->climb(fi, ti);
    }
    return w;
}

void try_move(ecs::Position& p, ecs::MacroNpcRuntime& rt,
              float tx, float ty, const TickContext& ctx) {
    int ix = int(p.x), iy = int(p.y);
    const int itx = int(tx), ity = int(ty);
    const float oldX = p.x, oldY = p.y;
    const float mapWf = float(ctx.mapW), mapHf = float(ctx.mapH);

    // Cells this think may cover — the SAME march the player walks
    // (kMacroWalkCellsPerHour per game hour), paced by the leader's own sheet
    // (moveMult: spd × athletics) and by the ground underfoot
    // (terrain_speed_mult of the CURRENT cell — one sample per think, the
    // same approximation the player's per-frame walk makes). Base numbers:
    // 3 cells per think on a road, ~1 in open water.
    const float perThink = kMacroWalkCellsPerHour * kAiTickGameHours
                           * std::max(0.0f, rt.moveMult)
                           * terrain_speed_mult(cell_weight(ctx, ix, iy));
    rt.moveBudget += perThink;
    // Banking bound, not a speed limit: at most one whole spare cell rides
    // across thinks on top of this think's own production.
    if (rt.moveBudget > perThink + 1.0f) rt.moveBudget = perThink + 1.0f;

    // What the leader's own training says a cell costs him (travel skill).
    const float efficiency = skill_cost_mult(int(rt.travelRank));
    const int playerCellX = wrapi(int(ctx.playerX), ctx.mapW);
    const int playerCellY = wrapi(int(ctx.playerY), ctx.mapH);

    while (rt.moveBudget >= 1.0f && (ix != itx || iy != ity)) {
        // Greedy steering (Session 21, owner's choice over per-squad A*):
        // among the eight neighbours, keep those strictly CLOSER to the
        // target and step onto the cheapest by the one weight grid. The
        // straight torus step wins ties, so a featureless world walks
        // exactly the line the old flat step walked — and a coast is walked
        // AROUND (land is 2-5×, water 10×), while a river with no cheap way
        // through is finally forded at its honest price. O(8) per cell:
        // 16384 squads can afford it where a pathfind each would starve
        // the frame.
        const Step straight =
            torus_step_toward(ix, iy, itx, ity, ctx.mapW, ctx.mapH);
        int bx = straight.nx, by = straight.ny;
        float bw = edge_weight(ctx, ix, iy, bx, by);
        const float dHere =
            torus_dist_sq(float(ix), float(iy), float(itx), float(ity),
                          mapWf, mapHf);
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) continue;
                const int nx = wrapi(ix + ox, ctx.mapW);
                const int ny = wrapi(iy + oy, ctx.mapH);
                if (nx == bx && ny == by) continue;
                const float d =
                    torus_dist_sq(float(nx), float(ny), float(itx), float(ity),
                                  mapWf, mapHf);
                if (d >= dHere) continue;   // only steps that make progress
                const float w = edge_weight(ctx, ix, iy, nx, ny);
                if (w < bw) { bx = nx; by = ny; bw = w; }
            }
        }

        // Entry-side stamp: the signed step of THIS cell change, torus-folded
        // (stepping east off the map's edge is still +1, not -(w-1)).
        int dx = bx - ix;
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = by - iy;
        if (dy > 1) dy = -1; else if (dy < -1) dy = 1;
        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;

        ix = bx; iy = by;
        rt.moveBudget -= 1.0f;

        // The step pays THE cell price — the same rows and formula the
        // player's march is charged (travel_stamina_cost), through the
        // fractional carry. The flat `sp -= 10` dialect dies here.
        // The step pays the load too (owner: перегруз универсальный) —
        // `overloadCost` is this think's surcharge, refreshed from the bag by
        // the sweep before the behaviour ran, so a caravan hauling more than
        // its leader's back can hold buys the trip at the honest price.
        rt.spCarry -= travel_stamina_cost(bw, 1.0f, int(rt.overloadCost),
                                          efficiency);
        settle_sp_carry(rt);
        if (int(rt.sp) < 0) break;   // spent: the think's march ends

        // Never hop OVER the player's cell in a multi-cell think: the forced
        // encounter (Inc 6) is geometric, so the squad stops ON the meeting
        // cell where the door can see it.
        if (ix == playerCellX && iy == playerCellY) break;
    }

    p.x = float(ix);
    p.y = float(iy);
    set_visual_speed(rt, oldX, oldY, p.x, p.y);

    // Exhaustion is settled by the DISPATCHER after the think (it owns the
    // entity + Health this function never sees): rest on land, the debt's
    // bite on water — one door for every behaviour that marched.
}

bool find_nearest_tree_grid(const TreeGrid& g, float px, float py,
                            int mw, int mh, XY& out) {
    if (!g.trees || g.trees->empty()) return false;
    float halfW = float(mw) * 0.5f;
    float halfH = float(mh) * 0.5f;
    const CellBuckets& b = g.grid;
    int cx0 = int(std::floor(px / float(b.cellSize)));
    int cy0 = int(std::floor(py / float(b.cellSize)));
    float best = 901.0f;        // 30² + 1
    bool found = false;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int gx = wrapi(cx0 + ox, b.cols);
            int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const auto& t = (*g.trees)[*it];
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

// ── The ONE gatherer loop — a profession is a ROW, never a branch ─────────
// (owner: «профессия на каждый ресурс, дата-контекст, универсально»;
// resources.md). Idle → find the worksite the row names → travel → WORK
// (take from the resource-field registry into the OWN bag — kill the man on
// the road and the haul is loot, not bookkeeping) → return (deliver into
// the HOME store). kGatherPerWorkerDay is the same anchor the economy
// day-loop gathers by: one law of labour. No worksite or no wired layer =
// the home wander, fail closed — nothing is conjured.

// How a profession finds its worksite. Three shapes, priced by the world:
enum class Worksite : std::uint8_t {
    ForestCell,   // nearest forest-class cell (the TreeGrid index)
    HomeField,    // the home's nearest FT_Field parcel
    Deposit,      // the home's nearest deposit cell of the row's kind
};

struct GathererDef {
    NPCType         type;
    ResourceFieldId row;        // what leaves the world
    const char*     commodity;  // what rides the bag and lands in the store
    Worksite        worksite;
};

constexpr GathererDef kGathererDefs[] = {
    {NPCType::Peasant,    ResourceFieldId::Wheat, "grain", Worksite::HomeField},
    {NPCType::Woodcutter, ResourceFieldId::Trees, "wood",  Worksite::ForestCell},
    {NPCType::Miner,      ResourceFieldId::Iron,  "iron",  Worksite::Deposit},
    {NPCType::Quarryman,  ResourceFieldId::Stone, "stone", Worksite::Deposit},
    {NPCType::ClayDigger, ResourceFieldId::Clay,  "clay",  Worksite::Deposit},
};

const GathererDef* gatherer_def(NPCType t) {
    for (const GathererDef& d : kGathererDefs)
        if (d.type == t) return &d;
    return nullptr;
}

// The home's nearest live cell of the row's deposit kind, torus-metric,
// capped at kGathererReach. The deposit maps are sparse, so this is a walk
// over a handful of cells, not the map.
bool find_home_deposit(const TickContext& ctx, ResourceFieldId row,
                       const XY& home, XY& out) {
    if (!ctx.mw.deposits || ctx.mapW <= 0) return false;
    const DepositKind kind =
        DepositKind(std::uint8_t(row) - std::uint8_t(ResourceFieldId::Clay));
    const auto& cells = ctx.mw.deposits->cells[std::size_t(kind)];
    float bestSq = float(kGathererReach) * float(kGathererReach);
    bool found = false;
    for (const auto& [idx, remaining] : cells) {
        (void)remaining;   // every entry is ALIVE (annihilation law, v55)
        const float x = float(int(idx % std::uint32_t(ctx.mapW)));
        const float y = float(int(idx / std::uint32_t(ctx.mapW)));
        const float dsq = torus_dist_sq(home.x, home.y, x, y,
                                        float(ctx.mapW), float(ctx.mapH));
        if (dsq <= bestSq) {
            bestSq = dsq;
            out = XY{x, y};
            found = true;
        }
    }
    return found;
}

bool find_worksite(const GathererDef& def, const TickContext& ctx,
                   const ecs::Position& p, const XY& home, XY& out) {
    switch (def.worksite) {
        case Worksite::ForestCell:
            return ctx.mw.treeGrid
                && find_nearest_tree_grid(*ctx.mw.treeGrid, p.x, p.y,
                                          ctx.mapW, ctx.mapH, out);
        case Worksite::HomeField:
            return find_home_field(ctx, p.x, p.y, home, out);
        case Worksite::Deposit:
            return find_home_deposit(ctx, def.row, home, out);
    }
    return false;
}

void ai_gatherer(entt::entity self, ecs::Position& p,
                 const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                 const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    const GathererDef* def = gatherer_def(NPCType(kind.type));
    if (!def) { ai_home_wanderer(p, rt, ctx); return; }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY site;
            if (find_worksite(*def, ctx, p, home, site)) {
                rt.targetX = site.x; rt.targetY = site.y;
                rt.state = std::uint8_t(NS::Traveling);
            } else if (torus_dist_sq(p.x, p.y, home.x, home.y,
                                     float(ctx.mapW), float(ctx.mapH))
                       > 400.0f) {
                // No work and far afield: come home first — the exact
                // HomeWanderer rule every gatherer degrades to.
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
            // WORK COSTS SP through the one stamina law (owner 2026-08-30;
            // econ_day.h kWorkCyclesPerBar carries the derivation). A squad
            // too spent for a cycle goes home to rest instead of working on
            // an empty bar — the same sentence the march pays.
            const int cycleCost =
                std::max(1, int(rt.maxSp) / kWorkCyclesPerBar);
            if (int(rt.sp) < cycleCost) {
                rt.targetX = home.x;
                rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
                return;
            }
            // The take is REAL: the trip's yield leaves the world through
            // the profession's registry row and rides home in the OWN bag.
            // A row whose layers are not wired reads 0 and takes nothing —
            // the fail-closed rule every gatherer shares.
            if (ctx.mw.world) {
                const int tx = int(rt.targetX);
                const int ty = int(rt.targetY);
                MacroWorld mw = ctx.mw;  // the envelope, whole — never a
                                         // partial re-pick of its layers
                const int have = resource_field_read(mw, def->row, tx, ty);
                // Every soul in the squad works: headcount multiplies the
                // cycle's yield at the squad's one fixed SP price (owner:
                // «SP тратится столько же, добывают кратно больше»).
                const auto* roster =
                    ctx.mw.world->reg.try_get<ecs::SquadRoster>(self);
                const int workers =
                    1 + (roster ? roster->squad.size() : 0);
                const int take =
                    std::min(kGatherPerCycle * workers, have);
                auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(self);
                // Credit BEFORE debit (CANON S5): the field pays only what
                // the OWN bag actually took — a bagless walker, or a bag
                // with no room, drains nothing and writes no Drained fact.
                if (take > 0 && bag && bag->inv.add(def->commodity, take)) {
                    resource_field_apply(mw, def->row, tx, ty, -take);
                    // The cycle is PAID the moment it produced — through
                    // the same fractional carry the march charges
                    // (settle_sp_carry above): work and walking drain one
                    // purse, which is the whole law.
                    rt.spCarry -= float(cycleCost);
                    settle_sp_carry(rt);
                    // A DEPOSIT worked down to nothing is a fact of the world
                    // (FactKind::Drained: "a vein worked out") — and by the
                    // annihilation law the cell itself leaves the map, so
                    // "why did the town grow poor" is answered by THIS record
                    // and nothing else. The daily haul is weather, not
                    // history (S20.1: the TRANSITION is the story); forest
                    // and wheat regrow by their own law, so their emptied
                    // cells write nothing. amount = resource registry row +1:
                    // the land has no ordinal to ride as the object (a spire
                    // does, and its drain points at the place instead), and
                    // after annihilation the world no longer holds the
                    // "what" — the fact is its only carrier.
                    if (def->worksite == Worksite::Deposit && take == have) {
                        record_landmark_fact(*ctx.mw.gs, FactKind::Drained,
                                             rt.homeSettlementId, tx, ty,
                                             int(def->row) + 1);
                    }
                }
            }
            rt.targetX = home.x; rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            // Home with the haul: everything gathered lands in the HOME
            // store — the same universal inventory the market sells from.
            if (rt.state == std::uint8_t(NS::Returning)) {
                deliver_bag_home(self, rt, ctx, def->commodity);
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(6 + rand_int(ctx, 12));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

// ── The city's trading agent (W2b) ───────────────────────────────────────
// An honest caravan: no TradeRoute abstraction settles anything — the goods
// ride in the caravan's OWN bag between real inventories, so a robbery on
// the road takes REAL cargo. What to haul is decided by MEMORY, not
// omniscience: at departure the caravan snapshots the home market
// (AgentMemory MarketSnapshot, the owner's design) and at the village loads
// what that snapshot says the city LACKS — it can be wrong by the time it
// returns, and that is a trader's life.
//
// How much it hauls is its OWN carry law and nothing else: rt.carryCap =
// get_carry_capacity(sheet) × the row's haulMult (squad.h
// refresh_leader_travel_stats — a caravan is wagons and mules, ×32). A flat
// `kCaravanCapacityKg = 256` lived here until 2026-08-29: a SECOND capacity
// beside the legal one, so the hold neither grew with the leader's back nor
// answered to the overload law that priced the very same cargo's march.

void ai_nomad(ecs::Position& p, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx);

// One roster (v62): the KIND check is explicit where a caller means "only a
// village" / "only a city", instead of being implied by which vector it
// searched.
Inventory* landmark_inventory_of_kind(const TickContext& ctx, int id,
                                      LandmarkType kind) {
    Landmark* lm = landmark_by_id(*ctx.mw.gs, id);
    return lm && lm->type == kind ? &lm->inventory : nullptr;
}

// Move up to `maxUnits` of `id` between inventories, bounded by the cargo
// hold's remaining weight. Returns units moved.
// The city's purchase PRIORITY: the needs ladder unrolled to its recipe
// INPUTS (bread ← grain first, then cloth's, bricks'…), then every other
// commodity in table order. Derived once from kNeeds × kRecipes — no
// commodity is named in code. Shared by the deal (what to buy first) and
// the route choice (which village is worth the ride): without the shared
// weight the caravans twice ran for whatever was merely PLENTIFUL — wood —
// while the granary held one unit (measured, balance_run 2026-08-30).
const std::array<int, std::size_t(kCommodityCount)>& caravan_buy_order() {
    static const auto kOrder = [] {
        std::array<int, std::size_t(kCommodityCount)> order{};
        bool seen[std::size_t(kCommodityCount)] = {};
        int n = 0;
        const auto push = [&](int idx) {
            if (idx >= 0 && idx < kCommodityCount && !seen[idx]) {
                seen[idx] = true;
                order[std::size_t(n++)] = idx;
            }
        };
        for (int i = 0; i < kNeedCount; ++i) {
            for (const RecipeDef& r : kRecipes) {
                if (std::strcmp(r.output, kNeeds[i].commodity) != 0) continue;
                for (const RecipeInput& in : r.inputs) {
                    if (in.id) push(commodity_index(in.id));
                }
            }
        }
        for (int i = 0; i < kCommodityCount; ++i) push(i);
        return order;
    }();
    return kOrder;
}

int haul_between(Inventory& from, Inventory& to, const char* id,
                 int maxUnits, float capacityLeftKg) {
    if (maxUnits <= 0 || capacityLeftKg <= 0.0f) return 0;
    const ItemDef* def = item_def(id);
    const float unitKg = def && def->weight > 0.0f ? def->weight : 1.0f;
    const int byWeight = int(capacityLeftKg / unitKg);
    const int n = std::min({maxUnits, byWeight, from.count(id)});
    if (n <= 0) return 0;
    // Credit before debit (CANON S5): a hold with no free slot refuses, the
    // cargo stays where it was, and "units moved" is never said of goods
    // that evaporated between two bags.
    if (!to.add(id, n)) return 0;
    from.remove(id, n);
    return n;
}

// Pick the caravan's next STATION: the nearest other city, never the one it
// just left; a coin-flip between the two nearest keeps one pair of towns
// from monopolising the leg. Pure geometry over the landmark list — the
// road graph prices the march anyway (pathCost), so "nearest" IS "next
// down the road" in practice.
int pick_next_station_(const TickContext& ctx, const ecs::Position& p,
                       int currentId, int prevId, float& outX, float& outY) {
    int bestId = -1, secondId = -1;
    float bestD = 1e30f, secondD = 1e30f;
    float bX = 0, bY = 0, sX = 0, sY = 0;
    for (auto& c : ctx.mw.gs->landmarks) {
        if (c.type != LandmarkType::City) continue;
        if (c.id == currentId || c.id == prevId) continue;
        const float d = torus_dist_sq(p.x, p.y, float(c.x), float(c.y),
                                      float(ctx.mapW), float(ctx.mapH));
        if (d < bestD) {
            secondId = bestId; secondD = bestD; sX = bX; sY = bY;
            bestId = c.id; bestD = d; bX = float(c.x); bY = float(c.y);
        } else if (d < secondD) {
            secondId = c.id; secondD = d; sX = float(c.x); sY = float(c.y);
        }
    }
    if (bestId < 0) return -1;
    // The coin-flip diversifies COMPARABLY near stations only (second within
    // twice the best's distance — ×4 on the squared metric): a road fork is
    // a choice, a town across the map is not.
    if (secondId >= 0 && secondD <= bestD * 4.0f && rand_int(ctx, 2) == 0) {
        outX = sX; outY = sY;
        return secondId;
    }
    outX = bX; outY = bY;
    return bestId;
}

void ai_caravan(entt::entity self, ecs::Position& p,
                ecs::MacroNpcRuntime& rt, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        // No honest home — degrade to the old nomad wander.
        ai_nomad(p, rt, ctx);
        return;
    }
    auto& reg = ctx.mw.world->reg;
    auto* bag = reg.try_get<ecs::NpcInventory>(self);
    // A caravan's home must be a CITY: the kind check is the gate (v62 —
    // one roster, so a village home simply resolves to the wrong kind here
    // and the caravan degrades below).
    Landmark* homeLm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!bag || !homeLm || homeLm->type != LandmarkType::City) {
        ai_nomad(p, rt, ctx);
        return;
    }
    Inventory* homeStore = &homeLm->inventory;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // DEPARTURE (owner 2026-08-30: caravans walk CITY to CITY, station
        // by station). Load the HOME surplus first — the stock above the
        // town's own daily demand. No deal here: the hold IS the city's
        // property (the loan law), and a town does not sell to itself.
        const EconSite homeSite =
            EconSite(landmark_def(homeLm->type).econSite);
        for (int i = 0; i < kCommodityCount
                        && inventory_weight(bag->inv) < rt.carryCap / 2;
             ++i) {
            const char* id = kCommodities[i].id;
            const int surplus =
                homeStore->count(id)
                - daily_demand_for(id, homeLm->population, homeSite);
            if (surplus <= 0) continue;
            haul_between(*homeStore, bag->inv, id, surplus,
                         rt.carryCap / 2 - inventory_weight(bag->inv));
        }
        // The LOAN (CANON S5: имущество NPC — заём со склада родного
        // ландмарка, не минт): purchasing power for the run — enough coin
        // to fill the hold's free half with grain at BASE price (grain is
        // what a city starves without), capped at HALF the town's wallet so
        // k simultaneous departures shrink the treasury geometrically and
        // never to zero. Repaid whole, with proceeds, at Returning.
        {
            const ItemDef* grain = item_def("grain");
            const float kg =
                grain && grain->weight > 0.0f ? grain->weight : 1.0f;
            const int unitsFit =
                int((rt.carryCap - inventory_weight(bag->inv)) / kg);
            const int need = unitsFit * (grain ? grain->value : 0);
            const int cap = wallet_value(*homeStore) / 2;
            transfer_value(*homeStore, bag->inv, std::min(need, cap));
        }
        // The run: a few stops down the road, then home. «Несколько
        // остановок» (owner) — two or three legs keeps a run inside a
        // handful of days; the count is a balance-run tunable.
        float nx = 0, ny = 0;
        const int next = pick_next_station_(ctx, p, rt.homeSettlementId,
                                            -1, nx, ny);
        if (next < 0) {
            ai_nomad(p, rt, ctx);   // a one-city world: wander on
            return;
        }
        rt.stationsLeft = std::uint8_t(2 + rand_int(ctx, 2));
        rt.prevStationId = rt.homeSettlementId;
        rt.targetSettlementId = next;
        rt.targetX = nx;
        rt.targetY = ny;
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(4 + rand_int(ctx, 4));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        if (Landmark* market = landmark_by_id(*ctx.mw.gs,
                                              rt.targetSettlementId);
            market && market->type == LandmarkType::City) {
            // THE station stop (npc_ai.h trade_caravan_at_station): sell
            // into this market\'s shortage, buy its surplus — every number
            // read off the market the caravan STANDS ON. Arbitrage with no
            // knowledge of anywhere else.
            const CaravanDeal deal = trade_caravan_at_station(
                bag->inv, rt.carryCap, *market);
            // The exchange is DONE — that one moment is the fact (S20.1: a
            // deal is a transition by nature; the ride on is the same
            // cargo, not a second deal). Subject = the home city whose
            // caravan this is, object = the station it traded WITH.
            if (deal.movedTableValue > 0) {
                record_landmark_fact(*ctx.mw.gs, FactKind::Traded,
                                     rt.homeSettlementId,
                                     int(rt.targetX), int(rt.targetY),
                                     deal.movedTableValue,
                                     rt.targetSettlementId);
            }
        }
        // Next leg, or home when the stops are spent.
        if (rt.stationsLeft > 0) --rt.stationsLeft;
        if (rt.stationsLeft > 0) {
            float nx = 0, ny = 0;
            const int next = pick_next_station_(
                ctx, p, rt.targetSettlementId, rt.prevStationId, nx, ny);
            if (next >= 0) {
                rt.prevStationId = rt.targetSettlementId;
                rt.targetSettlementId = next;
                rt.targetX = nx;
                rt.targetY = ny;
                rt.state = std::uint8_t(NS::Traveling);
                return;
            }
        }
        rt.targetX = home.x;
        rt.targetY = home.y;
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            if (rt.state == std::uint8_t(NS::Returning)) {
                // Home: the whole hold lands on the market — and the whole
                // purse repays the loan plus proceeds (the caravan is the
                // city\'s agent; its wealth IS the city\'s). A caravan killed
                // mid-run drops the coin with its cargo instead — the loan
                // law\'s honest downside.
                for (int i = 0; i < kCommodityCount; ++i) {
                    haul_between(bag->inv, *homeStore, kCommodities[i].id,
                                 1 << 30, 1e9f);
                }
                transfer_value(bag->inv, *homeStore,
                               wallet_value(bag->inv));
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

// The village vendor (owner 2026-08-30: the village ALWAYS sells at its
// nearest city's market). One run a day: load the home surplus, walk to the
// city the world already assigned this village (nearestCityId — known by
// construction, not by rumour), sell everything, buy the home's lacks,
// walk back. The labour rotation raises and dissolves the crew like any
// working squad.
void ai_vendor(entt::entity self, ecs::Position& p,
               ecs::MacroNpcRuntime& rt, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        ai_nomad(p, rt, ctx);
        return;
    }
    auto& reg = ctx.mw.world->reg;
    auto* bag = reg.try_get<ecs::NpcInventory>(self);
    auto* mem = reg.try_get<AgentMemory>(self);
    Landmark* homeLm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!bag || !mem || !homeLm) {
        ai_home_wanderer(p, rt, ctx);
        return;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        Landmark* market = landmark_by_id(*ctx.mw.gs, homeLm->nearestCityId);
        if (!market || market->type != LandmarkType::City) {
            ai_home_wanderer(p, rt, ctx);
            return;
        }
        // The crew's memory of ITS OWN home at departure — what the buy
        // half of the market deal shops against.
        remember(*mem, pack_market_snapshot(
                           homeLm->inventory,
                           std::uint16_t(rt.homeSettlementId),
                           ctx.mw.gs->worldTime.day()));
        // Load the surplus above the home's own daily demand — never its
        // living stock.
        const EconSite homeSite =
            EconSite(landmark_def(homeLm->type).econSite);
        for (int i = 0; i < kCommodityCount
                        && inventory_weight(bag->inv) < rt.carryCap;
             ++i) {
            const char* id = kCommodities[i].id;
            const int surplus =
                homeLm->inventory.count(id)
                - daily_demand_for(id, homeLm->population, homeSite);
            if (surplus <= 0) continue;
            haul_between(homeLm->inventory, bag->inv, id, surplus,
                         rt.carryCap - inventory_weight(bag->inv));
        }
        if (inventory_weight(bag->inv) <= 0.0f) {
            // Nothing to sell today: wait out the morning at home.
            rt.stateTimer = std::int16_t(40 + rand_int(ctx, 40));
            return;
        }
        rt.targetSettlementId = market->id;
        rt.targetX = float(market->x);
        rt.targetY = float(market->y);
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(4 + rand_int(ctx, 4));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        if (Landmark* market = landmark_by_id(*ctx.mw.gs,
                                              rt.targetSettlementId);
            market && market->type == LandmarkType::City) {
            const MemoryEntry* snap = recall(
                *mem, AgentMemoryKind::MarketSnapshot,
                std::uint16_t(rt.homeSettlementId));
            const CaravanDeal deal = trade_vendor_at_market(
                bag->inv, rt.carryCap, *market, snap);
            if (deal.movedTableValue > 0) {
                record_landmark_fact(*ctx.mw.gs, FactKind::Traded,
                                     rt.homeSettlementId,
                                     int(rt.targetX), int(rt.targetY),
                                     deal.movedTableValue,
                                     rt.targetSettlementId);
            }
        }
        rt.targetX = home.x;
        rt.targetY = home.y;
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            if (rt.state == std::uint8_t(NS::Returning)) {
                // Home: purchases and earnings land on the home store; the
                // rotation dissolves the crew at dawn.
                for (int i = 0; i < kCommodityCount; ++i) {
                    haul_between(bag->inv, homeLm->inventory,
                                 kCommodities[i].id, 1 << 30, 1e9f);
                }
                transfer_value(bag->inv, homeLm->inventory,
                               wallet_value(bag->inv));
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

void ai_trader(ecs::Position& p, ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    auto& settles = ctx.mw.gs->landmarks;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // Pick another city (id != home).
            int candidates = 0;
            for (auto& s : settles) {
                if (s.type != LandmarkType::City) continue;
                if (s.id != rt.homeSettlementId) ++candidates;
            }
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.type != LandmarkType::City) continue;
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
    auto& settles = ctx.mw.gs->landmarks;
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            int candidates = 0;
            for (auto& s : settles) {
                if (s.type != LandmarkType::City) continue;
                if (s.id != rt.targetSettlementId) ++candidates;
            }
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.type != LandmarkType::City) continue;
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
    // No private player-channel here any more (owner, 2026-08-29: «игрок
    // ничем не особенен»). Perception and pursuit are squad_threat_step's —
    // the player's squad sits in the SAME SquadIndex at the SAME
    // kSquadSightCells as every other squad, so an aggressive row that can
    // see the player chases him through the one law it chases anyone by.
    // The old channel saw the player at 10 cells against everyone else's 6.
    // What is left below is the row's untroubled day: wander.
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
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return entt::null;
    auto& reg = ctx.mw.world->reg;
    const char* myFaction = faction_id_for_index(kind.factionIdx);
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    float best = kSquadSightCells * kSquadSightCells + 1.0f;
    entt::entity found = entt::null;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const entt::entity e = entt::entity(*it);
                if (e == self || !reg.valid(e)) continue;
                const auto* op = reg.try_get<ecs::Position>(e);
                const auto* ok = reg.try_get<ecs::NPCKind>(e);
                if (!op || !ok) continue;
                const float d = torus_dist_sq(p.x, p.y, op->x, op->y,
                                              float(ctx.mapW),
                                              float(ctx.mapH));
                if (d >= best) continue;
                if (!factions_hostile(ctx.mw.gs, myFaction,
                                      faction_id_for_index(ok->factionIdx))) {
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
    if (!ctx.mw.world || !ctx.squads || !ctx.mw.gs) return false;

    const entt::entity enemy = nearest_hostile_squad(self, p, kind, ctx);
    if (enemy == entt::null) {
        // Threat gone: a fleeing squad calms down and resumes its life.
        if (rt.state == std::uint8_t(NS::Fleeing)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = 0;
        }
        return false;
    }

    auto& reg = ctx.mw.world->reg;
    const auto& ep = reg.get<ecs::Position>(enemy);
    const float myPower = squad_power(auto_battle_side_of(*ctx.mw.world, self));
    const float theirPower =
        squad_power(auto_battle_side_of(*ctx.mw.world, enemy));

    // The geometric meeting: same macro cell = the fight happens, resolved
    // by the ONE law and settled through the ONE ledger. An ambush is a
    // pursuer catching a squad that never saw it coming.
    if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
        // A player-controlled squad's meetings belong to the forced-encounter
        // door (Inc 6, main.cpp detect_forced_encounter): the squad stands ON
        // the meeting cell and the door opens the pre-battle screen — never
        // the silent auto-resolve. Both flags, because possession moves
        // PlayerTag while PlayerSquadTag stays home (components.h).
        if (reg.any_of<ecs::PlayerTag, ecs::PlayerSquadTag>(enemy)) {
            rt.visualSpeed = 0.0f;
            return true;
        }
        if (!ctx.allowAutoBattle) return false;
        auto* ert = reg.try_get<ecs::MacroNpcRuntime>(enemy);
        const bool ambush =
            rt.state == std::uint8_t(NS::Chasing) && ert
            && ert->state != std::uint8_t(NS::Chasing)
            && ert->state != std::uint8_t(NS::Fleeing);
        const AutoBattleOutcome o = resolve_auto_battle(
            auto_battle_side_of(*ctx.mw.world, self),
            auto_battle_side_of(*ctx.mw.world, enemy),
            ambush ? Ambush::SideA : Ambush::None, *ctx.rng);
        settle_auto_battle(ctx.mw, self, enemy, o);
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

// Follow the waypoint route in the squad's orders (Session 15, Inc 7): walk
// to the current waypoint, arrive, take the next, loop. A squad ordered onto
// a route with no route wanders — a degraded order is a visible NPC, not a
// frozen one.
void ai_waypoints(entt::entity e, ecs::Position& p, ecs::MacroNpcRuntime& rt,
                  const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.mw.world
        ? ctx.mw.world->reg.try_get<ecs::SquadOrders>(e) : nullptr;
    if (!orders || orders->waypointCount == 0) {
        ai_wanderer(p, rt, ctx);
        return;
    }
    const int i = orders->currentWaypoint % orders->waypointCount;
    rt.targetX = wrapf(float(orders->waypoints[std::size_t(i * 2)]),
                       float(ctx.mapW));
    rt.targetY = wrapf(float(orders->waypoints[std::size_t(i * 2 + 1)]),
                       float(ctx.mapH));
    if (at_target(p, rt, ctx)) {
        orders->currentWaypoint =
            std::uint8_t((i + 1) % orders->waypointCount);
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = std::int16_t(2 + rand_int(ctx, 4));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle) && rt.stateTimer > 0) {
        --rt.stateTimer;
        return;
    }
    rt.state = std::uint8_t(NS::Traveling);
    try_move(p, rt, rt.targetX, rt.targetY, ctx);
}

// The behaviour a squad ACTUALLY lives by: its type row's ai column, unless
// it CARRIES a waypoint route — the route's presence is the order (owner's
// ruling: one knob, not two). New kinds of squad AI are rows, never fields.
AIBehaviour effective_behaviour(entt::registry& reg, entt::entity e,
                                const ecs::NPCKind& kind) {
    if (const auto* orders = reg.try_get<ecs::SquadOrders>(e)) {
        if (orders->waypointCount > 0) return AIBehaviour::Waypoints;
    }
    return kNpcTypeDefs[kind.type].ai;
}

// Exhaustion, settled once per think AFTER the behaviour marched — the one
// door for every behaviour, where the dispatcher holds the entity and its
// Health (try_move deliberately sees neither).
//
// ONE LAW, both scales (owner's ruling, 2026-08-27): «истощение — это когда
// SP кончилось, и тогда отнимается HP от ДВИЖЕНИЯ по миру; остановился —
// отдыхаешь». So the bite is owed by a body that MOVED this think with its
// bar already spent, wherever it stands — the same `exhaustion_bite` the
// player's every step pays (movement_cost.h). It used to be gated on WATER:
// a squad that marched itself into the ground on dry meadow simply made camp
// and paid nothing, while the player bled for the identical step. That gate
// is gone; drowning is no longer a special case, it is the general case
// happening on the most expensive ground there is.
//
// The bite lands on the LORD's HP because the lord IS the squad — the roster
// is a row inside him, macro damage lands on the avatar. A march the bar
// cannot pay therefore kills, through the same tracked-death door an
// auto-battle uses; the dead lord's men settle by the standing rule.
//
// MAKING CAMP is a DECISION, not a mechanic (owner: «до скольки отдыхать —
// решение конечного автомата, а не механики»), so it stays here as what the
// AI chooses when its legs are gone, and the player keeps his own aim. What
// is one law is the PRICE; what is two is who decides to stop paying it.
bool cell_is_water(const TickContext& ctx, int x, int y) {
    const PathCostData* pc = ctx.mw.pathCost;
    if (!pc || pc->width <= 0 || pc->height <= 0
        || pc->water.size()
               != std::size_t(pc->width) * std::size_t(pc->height)) {
        return false;
    }
    const int wx = wrapi(x, pc->width);
    const int wy = wrapi(y, pc->height);
    return pc->water[std::size_t(wy) * std::size_t(pc->width)
                     + std::size_t(wx)] != 0u;
}

void settle_exhaustion(entt::entity e, const ecs::Position& p,
                       ecs::MacroNpcRuntime& rt, ecs::Health& hp,
                       bool canCamp, const TickContext& ctx) {
    (void)p;
    if (int(rt.sp) >= 0) return;

    // The AI's DECISION: legs gone, make camp — wherever a camp is possible.
    // Standing still costs nothing; that is the same sentence as «остановился
    // — отдыхаешь». Open water offers no camp, so a squad caught mid-ocean
    // does not get to stop, and the one mechanic below bills it for every
    // further step until it makes a shore or drowns. That is the SAME outcome
    // the old water-only bite produced, arrived at by the right layer: the
    // price is a law, the choice of where to stop is a decision.
    if (canCamp && rt.state != std::uint8_t(NPCState::Resting)) {
        rt.state = std::uint8_t(NPCState::Resting);
        rt.stateTimer = 0;
    }

    const int bite = exhaustion_bite(int(rt.sp));
    if (bite <= 0) return;
    hp.hp -= float(bite);
    if (hp.hp <= 0.0f && ctx.mw.world && ctx.mw.gs) {
        settle_leader_fraction(*ctx.mw.world, e, 0.0f);
        drain_dead_leader_squads(*ctx.mw.world, ctx.mw.gs->deserterPool);
    }
}

void dispatch(AIBehaviour b, entt::entity e, ecs::Position& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx) {
    if (squad_threat_step(e, p, kind, rt, ctx)) return;
    switch (b) {
        case AIBehaviour::Gatherer:     ai_gatherer(e, p, kind, rt, ctx); break;
        case AIBehaviour::CaravanTrade: ai_caravan   (e, p, rt, ctx); break;
        case AIBehaviour::VendorTrade:  ai_vendor    (e, p, rt, ctx); break;
        case AIBehaviour::Trader:       ai_trader       (p, rt, ctx); break;
        case AIBehaviour::Nomad:        ai_nomad        (p, rt, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive   (p, rt, ctx); break;
        case AIBehaviour::Patrol:       ai_patrol       (p, rt, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter   (p, rt, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer     (p, rt, ctx); break;
        // Prey. Running is not its own errand: the threat step above already
        // makes ANY row run from what it cannot beat (squad_power), so what
        // this column says about an untroubled day is "it roams" — the same
        // walk a wanderer takes. The difference between a fox and a rabbit is
        // what happens when something appears, and that is decided above.
        case AIBehaviour::Flee:         ai_wanderer     (p, rt, ctx); break;
        case AIBehaviour::Waypoints:    ai_waypoints (e, p, rt, ctx); break;
        case AIBehaviour::Count:        break;
    }
}

} // namespace

// ── Trading at a market, two strategies over ONE price law ───────────────
// (owner 2026-08-30: «караван приходит в город и НА МЕСТЕ смотрит, что
// выгодно продать и купить» — locality, no omniscience, no rumours.)
// Both functions price through economy.h stock_price at POST-TRADE supply
// (every lot pays its own slippage), and coin travels by transfer_value:
// conservation is by construction, not by audit.

// The STATION deal — a caravan standing on a CITY market. Symmetric and
// purely local: a market short of a good (daily demand above supply) BUYS
// from the hold up to its demand; a market glutted (supply above demand)
// SELLS the surplus into the hold. The bounds fall straight out of the
// price law — price > base ⇔ demand > supply — so «продай что здесь
// дорого, купи что дёшево» needs no threshold constants at all, and the
// trade drives every market it touches TOWARD its own demand. Arbitrage is
// emergent: the surplus bought cheap here is exactly what the next hungry
// station pays above base for.
CaravanDeal trade_caravan_at_station(Inventory& hold, float capacityKg,
                                     Landmark& market) {
    CaravanDeal out{};
    Inventory& ms = market.inventory;
    const EconSite site = EconSite(landmark_def(market.type).econSite);
    for (int i = 0; i < kCommodityCount; ++i) {
        const char* id = kCommodities[i].id;
        const ItemDef* def = item_def(id);
        const int base = def ? def->value : 0;
        if (base <= 0) continue;
        const int demand = daily_demand_for(id, market.population, site);
        const int have = ms.count(id);
        if (demand > have) {
            // SELL into the shortage, up to the market's own demand. The
            // affordability bound refines once: the ceiling price yields a
            // small lot, the lot's own (cheaper) post-trade price affords a
            // bigger one — and the bigger lot is still affordable because
            // more supply only cheapens the unit further.
            int n = std::min(hold.count(id), demand - have);
            if (n <= 0) continue;
            const int wallet = wallet_value(ms);
            const int priceCeil = stock_price(base, have, demand);
            int afford = wallet / std::max(1, priceCeil);
            if (afford < n) {
                const int p1 = stock_price(base, have + afford, demand);
                afford = wallet / std::max(1, p1);
            }
            n = std::min(n, afford);
            if (n <= 0) continue;
            const int moved = haul_between(hold, ms, id, n, 1e9f);
            if (moved <= 0) continue;
            const int price = stock_price(base, have + moved, demand);
            out.soldValue += transfer_value(ms, hold, moved * price);
            out.movedTableValue += base * moved;
        } else if (have > demand) {
            // BUY the surplus above the market's own demand — never its
            // living stock.
            const float kg = def->weight > 0.0f ? def->weight : 1.0f;
            int n = std::min(have - demand,
                             int((capacityKg - inventory_weight(hold)) / kg));
            if (n <= 0) continue;
            // A short purse shrinks the lot ONCE: shrinking n leaves more
            // supply behind, which only CHEAPENS the unit, so the shrunk
            // lot is affordable by monotonicity.
            int price = stock_price(base, have - n, demand);
            const int purse = wallet_value(hold);
            if (n * price > purse) {
                n = purse / std::max(1, price);
                price = stock_price(base, have - n, demand);
            }
            if (n <= 0) continue;
            const int moved =
                haul_between(ms, hold, id, n,
                             capacityKg - inventory_weight(hold));
            if (moved <= 0) continue;
            const int cost = moved * stock_price(base, have - moved, demand);
            out.boughtValue += transfer_value(hold, ms, cost);
            out.movedTableValue += base * moved;
        }
    }
    return out;
}

// The VENDOR deal — a village crew at its NEAREST city's market (владелец:
// «крестьяне просто всегда идут продавать на рынок ближайшего города»).
// Not an arbitrageur: sells EVERYTHING it carried (the village's surplus,
// at whatever the local law prices it), then spends the earnings down the
// home's needs ladder — what the home snapshot says the village lacks
// (class ≤ 1). The snapshot is the crew's memory of ITS OWN home at
// departure, never a rumour.
CaravanDeal trade_vendor_at_market(Inventory& bag, float capacityKg,
                                   Landmark& market,
                                   const MemoryEntry* homeSnapshot) {
    CaravanDeal out{};
    Inventory& ms = market.inventory;
    const EconSite site = EconSite(landmark_def(market.type).econSite);
    const auto base_value = [](const char* id) {
        const ItemDef* d = item_def(id);
        return d ? d->value : 0;
    };
    // SELL the whole load first — the coin below buys the home's lacks.
    for (int i = 0; i < kCommodityCount; ++i) {
        const char* id = kCommodities[i].id;
        const int base = base_value(id);
        if (base <= 0) continue;
        int n = bag.count(id);
        if (n <= 0) continue;
        const int demand = daily_demand_for(id, market.population, site);
        const int have = ms.count(id);
        // Affordability refined once, exactly as at the station: a glut lot
        // prices far below the ceiling, so the ceiling alone under-sells.
        const int wallet = wallet_value(ms);
        const int priceCeil = stock_price(base, have, demand);
        int afford = wallet / std::max(1, priceCeil);
        if (afford < n) {
            const int p1 = stock_price(base, have + afford, demand);
            afford = wallet / std::max(1, p1);
        }
        n = std::min(n, afford);
        if (n <= 0) continue;
        const int moved = haul_between(bag, ms, id, n, 1e9f);
        if (moved <= 0) continue;
        const int price = stock_price(base, have + moved, demand);
        out.soldValue += transfer_value(ms, bag, moved * price);
        out.movedTableValue += base * moved;
    }
    // BUY the home's lacks, needs-ladder inputs first (caravan_buy_order).
    if (homeSnapshot) {
        for (int cls = 0; cls <= 1; ++cls) {
            for (int oi = 0; oi < kCommodityCount; ++oi) {
                const int i = caravan_buy_order()[std::size_t(oi)];
                if (market_stock_class(*homeSnapshot, i) != cls) continue;
                const char* id = kCommodities[i].id;
                const ItemDef* def = item_def(id);
                const int base = def ? def->value : 0;
                if (base <= 0) continue;
                const int demand =
                    daily_demand_for(id, market.population, site);
                const int have = ms.count(id);
                const float kg = def->weight > 0.0f ? def->weight : 1.0f;
                int n = std::min(
                    have, int((capacityKg - inventory_weight(bag)) / kg));
                if (n <= 0) continue;
                int price = stock_price(base, have - n, demand);
                const int purse = wallet_value(bag);
                if (n * price > purse) {
                    n = purse / std::max(1, price);
                    price = stock_price(base, have - n, demand);
                }
                if (n <= 0) continue;
                const int moved =
                    haul_between(ms, bag, id, n,
                                 capacityKg - inventory_weight(bag));
                if (moved <= 0) continue;
                const int cost =
                    moved * stock_price(base, have - moved, demand);
                out.boughtValue += transfer_value(bag, ms, cost);
                out.movedTableValue += base * moved;
            }
        }
    }
    return out;
}

// ── The daily labour rotation (contract in npc_ai.h) ─────────────────────
int rotate_worker_squads(MacroWorld& mw, int day) {
    (void)day;
    if (!mw.gs || !mw.world || !mw.terrain) return 0;
    GameState& gs = *mw.gs;
    auto& reg = mw.world->reg;
    TickContext ctx{};
    ctx.mw = mw;
    ctx.mapW = gs.mapW;
    ctx.mapH = gs.mapH;

    const auto row_of = [&](int id) -> int {
        for (std::size_t i = 0; i < gs.landmarks.size(); ++i)
            if (gs.landmarks[i].id == id) return int(i);
        return -1;
    };
    const auto def_index = [&](std::uint16_t type) -> int {
        for (int gi = 0; gi < int(std::size(kGathererDefs)); ++gi)
            if (std::uint16_t(kGathererDefs[gi].type) == type) return gi;
        return -1;
    };
    // The vendor crew rotates with the working crews — one law of labour.
    const auto is_crew = [&](std::uint16_t type) {
        return def_index(type) >= 0
               || type == std::uint16_t(NPCType::Vendor);
    };

    // 1) DISSOLVE yesterday's crews that made it home: souls and leftovers
    //    return to the landmark. Collect first, destroy after — the
    //    registry is never mutated under its own view.
    std::vector<entt::entity> done;
    for (auto [e, kind, rt, p]
         : reg.view<ecs::NPCKind, ecs::MacroNpcRuntime,
                    ecs::Position>().each()) {
        if (!is_crew(kind.type)) continue;
        if (rt.state != std::uint8_t(NS::Idle)) continue;
        const int row = row_of(rt.homeSettlementId);
        if (row < 0) continue;
        const Landmark& lm = gs.landmarks[std::size_t(row)];
        if (int(std::lround(p.x)) != lm.x || int(std::lround(p.y)) != lm.y)
            continue;
        done.push_back(e);
    }
    for (const entt::entity e : done) {
        const auto& rt = reg.get<ecs::MacroNpcRuntime>(e);
        const int row = row_of(rt.homeSettlementId);
        if (row < 0) continue;
        Landmark& lm = gs.landmarks[std::size_t(row)];
        int souls = 1;
        if (const auto* roster = reg.try_get<ecs::SquadRoster>(e))
            souls += roster->squad.size();
        if (auto* bag = reg.try_get<ecs::NpcInventory>(e)) {
            // Leftovers home: cargo by the haul door, coin by the wallet
            // door — a dissolved crew owns nothing (CANON S5, the loan law).
            for (int c = 0; c < kCommodityCount; ++c)
                haul_between(bag->inv, lm.inventory, kCommodities[c].id,
                             1 << 30, 1e9f);
            transfer_value(bag->inv, lm.inventory,
                           wallet_value(bag->inv));
        }
        lm.population += souls;
        reg.destroy(e);
    }

    // Which professions still have a crew OUT (on the road, at the field —
    // anyone not dissolved above): those are not re-raised today.
    std::vector<std::uint8_t> outMask(gs.landmarks.size(), 0);
    for (auto [e, kind, rt]
         : reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
        (void)e;
        int gi = def_index(kind.type);
        if (gi < 0 && kind.type == std::uint16_t(NPCType::Vendor))
            gi = 7;   // the vendor's own bit, above the profession bits
        if (gi < 0) continue;
        const int row = row_of(rt.homeSettlementId);
        if (row >= 0) outMask[std::size_t(row)] |= std::uint8_t(1u << gi);
    }

    // 2) RAISE today's crews: pop/kHeadsPerCityWorker souls across the
    //    professions whose worksite is LIVE (the same find_worksite the
    //    working AI walks by — ore near home IS the presence of miners).
    int raised = 0;
    for (std::size_t row = 0; row < gs.landmarks.size(); ++row) {
        Landmark& s = gs.landmarks[row];
        if (s.type != LandmarkType::City && s.type != LandmarkType::Village)
            continue;
        if (s.population < kHeadsPerCityWorker) continue;
        const XY home{float(s.x), float(s.y)};
        const ecs::Position homePos{home.x, home.y, 0.0f};
        int live[int(std::size(kGathererDefs))];
        int liveCount = 0;
        for (int gi = 0; gi < int(std::size(kGathererDefs)); ++gi) {
            if (outMask[row] & (1u << gi)) continue;
            XY site;
            if (find_worksite(kGathererDefs[gi], ctx, homePos, home, site))
                live[liveCount++] = gi;
        }
        if (liveCount <= 0) continue;
        // The same share of hands the city's benches already draw
        // (kHeadsPerCityWorker) — ONE labour quota, split across today's
        // live professions.
        const int workers = s.population / kHeadsPerCityWorker;
        const int perCrew = std::max(1, workers / liveCount);
        for (int li = 0; li < liveCount; ++li) {
            if (s.population < perCrew) break;
            SquadSpec spec{};
            spec.leaderType = kGathererDefs[live[li]].type;
            spec.x = s.x;
            spec.y = s.y;
            spec.homeSettlementId = s.id;
            for (int m = 1; m < perCrew; ++m) {
                SoldierRecord rec{};
                // Identity through the ONE persistent ordinal stream
                // (CANON S20.1) — never a hash, never a second counter.
                rec.entityId = ++gs.nextMacroSpawnOrdinal;
                rec.kind = std::uint16_t(spec.leaderType);
                rec.level = 1;
                if (!spec.members.push(rec)) break;
            }
            if (spawn_squad(gs, *mw.world, *mw.terrain, spec)
                != entt::null) {
                s.population -= 1 + spec.members.size();
                ++raised;
            }
        }
        // The vendor run (owner 2026-08-30): a village with a home city
        // sends its surplus to market — a crew of the same labour quota as
        // any profession, because the surplus scales with the same
        // population that made it.
        if (s.type == LandmarkType::Village && !(outMask[row] & 0x80u)
            && s.population >= perCrew
            && landmark_by_id(gs, s.nearestCityId)) {
            SquadSpec spec{};
            spec.leaderType = NPCType::Vendor;
            spec.x = s.x;
            spec.y = s.y;
            spec.homeSettlementId = s.id;
            for (int m = 1; m < perCrew; ++m) {
                SoldierRecord rec{};
                rec.entityId = ++gs.nextMacroSpawnOrdinal;
                rec.kind = std::uint16_t(NPCType::Vendor);
                rec.level = 1;
                if (!spec.members.push(rec)) break;
            }
            if (spawn_squad(gs, *mw.world, *mw.terrain, spec)
                != entt::null) {
                s.population -= 1 + spec.members.size();
                ++raised;
            }
        }
    }
    return raised;
}

void bucket_reset(CellBuckets& g, int mapW, int mapH, int cellSize) {
    g.cellSize = std::max(1, cellSize);
    g.cols = std::max(1, (mapW + g.cellSize - 1) / g.cellSize);
    g.rows = std::max(1, (mapH + g.cellSize - 1) / g.cellSize);
    const std::size_t n = std::size_t(g.cols) * std::size_t(g.rows);
    // assign() over the SAME size keeps the capacity, so a grid that is not
    // resized never allocates again after its first build.
    g.begin.assign(n + 1, 0u);
    g.cursor.assign(n, 0u);
}

void bucket_count(CellBuckets& g, int gx, int gy) {
    // Counts land at begin[cell + 1] so the prefix pass can sum in place.
    ++g.begin[g.cell_of(gx, gy) + 1];
}

void bucket_prefix(CellBuckets& g, std::size_t itemCount) {
    for (std::size_t i = 1; i < g.begin.size(); ++i) g.begin[i] += g.begin[i - 1];
    g.items.resize(itemCount);
    for (std::size_t i = 0; i < g.cursor.size(); ++i) g.cursor[i] = g.begin[i];
}

void bucket_scatter(CellBuckets& g, int gx, int gy, std::uint32_t item) {
    const std::size_t c = g.cell_of(gx, gy);
    g.items[g.cursor[c]++] = item;
}

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize) {
    g.trees = &trees;
    CellBuckets& b = g.grid;
    bucket_reset(b, mapW, mapH, cellSize);
    for (const TreePoint& t : trees) {
        bucket_count(b, wrapi(t.x / b.cellSize, b.cols),
                     wrapi(t.y / b.cellSize, b.rows));
    }
    bucket_prefix(b, trees.size());
    for (std::uint32_t i = 0; i < trees.size(); ++i) {
        bucket_scatter(b, wrapi(trees[i].x / b.cellSize, b.cols),
                       wrapi(trees[i].y / b.cellSize, b.rows), i);
    }
}

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime,
                                std::uint32_t seed) {
    runtime = MacroNpcAiRuntime{};
    runtime.jitter = Rng{seed ^ 0xA1F0u};
}

void build_squad_index(SquadIndex& g, ecs::World& w, int mapW, int mapH,
                       int cellSize) {
    CellBuckets& b = g.grid;
    bucket_reset(b, mapW, mapH, cellSize);

    // Every live macro squad — INCLUDING the player's (owner, 2026-08-29:
    // «игрок ничем не особенен», one law of sight for all). His squad is
    // perceived through this index at the same kSquadSightCells as anyone;
    // what stays special is only the MEETING, which belongs to Inc 6's
    // forced-encounter door (squad_threat_step stops short of auto-battling
    // a player-controlled squad). The Dead are no squads at all.
    auto view = w.reg.view<ecs::Position, ecs::NPCKind,
                           ecs::MacroNpcRuntime>(
        entt::exclude<ecs::Dead, ecs::SubworldTag>);
    // Two passes over the view — count, then scatter — which is what buys the
    // allocation-free rebuild. The view is cheap to walk twice; sixteen
    // thousand vector headers were not cheap to rebuild once.
    std::size_t total = 0;
    for (auto e : view) {
        const auto& p = view.get<ecs::Position>(e);
        bucket_count(b, wrapi(int(p.x) / b.cellSize, b.cols),
                     wrapi(int(p.y) / b.cellSize, b.rows));
        ++total;
    }
    bucket_prefix(b, total);
    for (auto e : view) {
        const auto& p = view.get<ecs::Position>(e);
        bucket_scatter(b, wrapi(int(p.x) / b.cellSize, b.cols),
                       wrapi(int(p.y) / b.cellSize, b.rows),
                       std::uint32_t(entt::to_integral(e)));
    }
}

// ONE assembly of the AI think's view (canon-audit H2: this block used to
// exist twice, line for line, one copy per driver, and the copies drifted —
// the budgeted twin once shipped without `deposits`, so every miner in the
// world stopped digging while the player was underground). The layer envelope
// arrives assembled by its owner; this adds only the drive-state.
static TickContext make_tick_context(MacroWorld& mw,
                                     MacroNpcAiRuntime& runtime,
                                     bool allowAutoBattle) {
    TickContext ctx{};
    ctx.mw      = mw;
    ctx.mapW    = mw.gs->mapW;
    ctx.mapH    = mw.gs->mapH;
    ctx.rng     = &runtime.jitter;
    ctx.playerX = mw.gs->player.x;
    ctx.playerY = mw.gs->player.y;
    ctx.squads  = &runtime.squadIndex;
    ctx.allowAutoBattle = allowAutoBattle;
    return ctx;
}

void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle) {
    if (!mw.gs || !mw.world) return;   // no world, no thinking (fail-closed)
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    auto& reg = w.reg;
    auto view = reg.view<ecs::Position, ecs::NPCKind,
                         ecs::MacroNpcRuntime, ecs::Health>(
        entt::exclude<ecs::Dead, ecs::PlayerTag, ecs::PlayerSquadTag>);  // never AI-drive the player: the flag OR his own squad

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);

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
        const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
        if (gate == ThinkGate::Dead) continue;
        refresh_overload_cost(rt, reg.try_get<ecs::NpcInventory>(e));
        const float x0 = p.x, y0 = p.y;
        if (gate == ThinkGate::Think)
            dispatch(effective_behaviour(reg, e, kind), e, p, kind, rt, ctx);
        settle_march_rhythm(e, p, rt, hp, p.x != x0 || p.y != y0, ctx);
    }

    // The END of every dead squad's story, once per tick (CANON S4): any
    // stragglers' survivors to the pool (idempotent for the already-drained;
    // it also catches a dead=1 roster a save carried across the sweep
    // window), then the drained corpse-rows leave the map. Deferred to HERE
    // because the settle doors' callers still hold the entities mid-tick.
    drain_dead_leader_squads(w, gs.deserterPool);
    destroy_dead_macro_squads(w);
}

void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt) {
    if (mapW <= 0 || mapH <= 0 || dt <= 0.0f) return;

    auto view = w.reg.view<ecs::Position, ecs::VisualPos,
                           ecs::MacroNpcRuntime, ecs::Health>(
        entt::exclude<ecs::Dead, ecs::SubworldTag, ecs::PlayerTag,
                      ecs::PlayerSquadTag>);  // player drawn by its own marker (Inc 5e-2)
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
        // Snap bound covers one full think of honest marching (up to ~4 cells
        // diagonal ≈ 5.7): a road-pace squad GLIDES; only true jumps
        // (teleporter, seam remaps) snap. Was 3 cells, sized to the old
        // one-cell step.
        if (dSq > 36.0f) {
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
    MacroWorld& mw,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle) {
    MacroNpcAiSliceResult result{};
    if (max_npc_ticks <= 0) return result;
    if (!mw.gs || !mw.world) return result;  // no world, no thinking
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;

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
        entt::exclude<ecs::Dead, ecs::PlayerTag, ecs::PlayerSquadTag>);  // never AI-drive the player: the flag OR his own squad

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    // The same ONE assembly as the map-view driver. This used to be a paste
    // that had drifted (the deposits epitaph now lives on TickContext itself,
    // npc_ai.h); the two drivers may differ in HOW they walk the entities —
    // never in what world the entities think about (CANON.md S2).
    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);

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
                const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
                if (gate != ThinkGate::Dead) {
                    refresh_overload_cost(rt,
                                          reg.try_get<ecs::NpcInventory>(e));
                    const float x0 = p.x, y0 = p.y;
                    if (gate == ThinkGate::Think) {
                        dispatch(effective_behaviour(reg, e, kind), e, p, kind,
                                 rt, ctx);
                    }
                    settle_march_rhythm(e, p, rt, hp,
                                        p.x != x0 || p.y != y0, ctx);
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
    // Same end-of-tick settlement as the map-view driver above (S4): the
    // macro clock ticks underground too, and a lord felled down there must
    // leave the map by the same law. (The positional sweep cursor already
    // tolerates the view shrinking — every death mid-sweep shrinks it.)
    drain_dead_leader_squads(w, gs.deserterPool);
    destroy_dead_macro_squads(w);
    result.backlog = result.backlog || runtime.pendingSweeps > 0;
    return result;
}

} // namespace sm
