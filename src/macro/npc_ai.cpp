// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "macro/agent_memory.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "macro/movement_cost.h"
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
    if (rt.homeIsVillage) {
        for (auto& v : ctx.gs->villages) {
            if (v.id == rt.homeSettlementId) {
                out = {float(v.x), float(v.y)};
                return true;
            }
        }
        return false;
    }
    for (auto& s : ctx.gs->settlements) {
        if (s.id == rt.homeSettlementId) {
            out = {float(s.x), float(s.y)};
            return true;
        }
    }
    return false;
}

// The agent's HOME STORE — where a gatherer's haul lands. The same universal
// Inventory the market sells from, resolved by the honest {id-space, id} pair.
Inventory* home_inventory(const ecs::MacroNpcRuntime& rt,
                          const TickContext& ctx) {
    if (rt.homeSettlementId < 0) return nullptr;
    if (rt.homeIsVillage) {
        for (auto& v : ctx.gs->villages) {
            if (v.id == rt.homeSettlementId) return &v.inventory;
        }
        return nullptr;
    }
    for (auto& s : ctx.gs->settlements) {
        if (s.id == rt.homeSettlementId) return &s.inventory;
    }
    return nullptr;
}

// Empty the gatherer's own bag of `id` into his home store — the shared
// arrival half of every honest work-loop (woodcutter, farmer).
void deliver_bag_home(entt::entity self, const ecs::MacroNpcRuntime& rt,
                      const TickContext& ctx, const char* id) {
    if (!ctx.world) return;
    auto* bag = ctx.world->reg.try_get<ecs::NpcInventory>(self);
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
    if (!ctx.features) return false;
    bool found = false;
    float best = 1e30f;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const int cx = int(home.x) + dx;
            const int cy = int(home.y) + dy;
            if (ctx.features->at(cx, cy) != FT_Field) continue;
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
    const int whole = int(rt.spCarry);
    if (whole == 0) return;
    rt.spCarry -= float(whole);
    const int maxSp = std::max<int>(1, rt.maxSp);
    rt.sp = std::int16_t(std::min(maxSp, int(rt.sp) + whole));
}

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
    const int maxSp = std::max<int>(1, rt.maxSp);
    if ((state == NPCState::Idle || state == NPCState::Resting)
        && int(rt.sp) < maxSp) {
        // THE regen law (attributes.h kSpRegenPctPerHour): a percent of the
        // bar per game hour, the leader's marathon skill speeding the rate,
        // paid out in this think's slice of the day. The old 5%-per-think was
        // the squads' own dialect — ~53% of the bar per game HOUR, a rest
        // that cost nothing. Fractional carry, same idiom as the player's.
        rt.spCarry += float(maxSp) * kSpRegenPctPerHour
                      * skill_bonus_mult(int(rt.marathonRank))
                      * kAiTickGameHours;
        settle_sp_carry(rt);
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

// Weight of a macro cell from the baked grid the player's A* walks
// (ctx.pathCost); a missing grid reads as a featureless free-road world.
float cell_weight(const TickContext& ctx, int x, int y) {
    const PathCostData* pc = ctx.pathCost;
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

bool cell_is_water(const TickContext& ctx, int x, int y) {
    const PathCostData* pc = ctx.pathCost;
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
        float bw = cell_weight(ctx, bx, by);
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
                const float w = cell_weight(ctx, nx, ny);
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
        rt.spCarry -= travel_stamina_cost(bw, 1.0f, 0, efficiency);
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

void ai_woodcutter(entt::entity self, ecs::Position& p,
                   ecs::MacroNpcRuntime& rt, const TickContext& ctx) {
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
            // The chop is REAL (W2b): the trip's yield leaves the world
            // through the registry's Trees row and rides home in the
            // woodcutter's OWN bag — kill him on the road and the wood is
            // loot, not bookkeeping. kGatherPerWorkerDay is the same anchor
            // the economy day-loop gathers by: one law of labour.
            if (ctx.trees && ctx.world) {
                const int tx = int(rt.targetX);
                const int ty = int(rt.targetY);
                MacroWorld mw{};
                mw.gs    = ctx.gs;
                mw.trees = ctx.trees;
                const int have = resource_field_read(
                    mw, ResourceFieldId::Trees, tx, ty);
                const int take = std::min(kGatherPerWorkerDay, have);
                if (take > 0) {
                    resource_field_apply(mw, ResourceFieldId::Trees,
                                         tx, ty, -take);
                    if (auto* bag = ctx.world->reg.try_get<ecs::NpcInventory>(
                            self)) {
                        bag->inv.add("wood", take);
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
                deliver_bag_home(self, rt, ctx, "wood");
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(6 + rand_int(ctx, 12));
            return;
        }
        try_move(p, rt, rt.targetX, rt.targetY, ctx);
    }
}

// The farmer: the woodcutter's loop with a FIELD for a forest (W2b). The
// field itself is the renewable source — v1 draws at labour pace with no
// depletion; the seasonal harvest pulse and field exhaustion are the layer's
// own later increment, not this behaviour's. No field / no feature layer =
// the plain home wander, so a CITY peasant keeps his old day.
void ai_farmer(entt::entity self, ecs::Position& p,
               ecs::MacroNpcRuntime& rt, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY field;
            if (find_home_field(ctx, p.x, p.y, home, field)) {
                rt.targetX = field.x;
                rt.targetY = field.y;
                rt.state = std::uint8_t(NS::Traveling);
            } else if (torus_dist_sq(p.x, p.y, home.x, home.y,
                                     float(ctx.mapW), float(ctx.mapH))
                       > 400.0f) {
                // No field and far afield: come home first — the exact
                // HomeWanderer rule this behaviour degrades to.
                rt.targetX = home.x;
                rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 12, ctx);
                rt.targetX = t.x;
                rt.targetY = t.y;
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
            // The reap is REAL (Field Inc F4): the trip's yield leaves the
            // world through the one crop_count row — the same ledger the
            // player's sickle settles against — so the field the farmer
            // works really thins and regrows on the world clock. Same law
            // of labour as the woodcutter's chop above; nullptr terrain =
            // no honest reaping, nothing conjured (his fail-closed rule).
            if (ctx.features && ctx.world && ctx.terrain) {
                MacroWorld mw{ctx.gs, ctx.trees, ctx.world, ctx.terrain};
                const MacroStockKey key{-1, std::int16_t(rt.targetX),
                                        std::int16_t(rt.targetY)};
                const int have =
                    macro_stock_read(mw, MacroStock::CropCount, key);
                const int take = std::min(kGatherPerWorkerDay, have);
                if (take > 0) {
                    macro_stock_apply(mw, MacroStock::CropCount, key, -take);
                    if (auto* bag = ctx.world->reg.try_get<ecs::NpcInventory>(
                            self)) {
                        bag->inv.add("grain", take);
                    }
                }
            }
            rt.targetX = home.x;
            rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            if (rt.state == std::uint8_t(NS::Returning)) {
                deliver_bag_home(self, rt, ctx, "grain");
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

constexpr float kCaravanCapacityKg = 256.0f;   // po2 cargo hold

void ai_nomad(ecs::Position& p, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx);

Inventory* village_inventory_by_id(const TickContext& ctx, int id) {
    for (auto& v : ctx.gs->villages) {
        if (v.id == id) return &v.inventory;
    }
    return nullptr;
}

Inventory* settlement_inventory_by_id(const TickContext& ctx, int id) {
    for (auto& s : ctx.gs->settlements) {
        if (s.id == id) return &s.inventory;
    }
    return nullptr;
}

// Move up to `maxUnits` of `id` between inventories, bounded by the cargo
// hold's remaining weight. Returns units moved.
int haul_between(Inventory& from, Inventory& to, const char* id,
                 int maxUnits, float capacityLeftKg) {
    if (maxUnits <= 0 || capacityLeftKg <= 0.0f) return 0;
    const ItemDef* def = item_def(id);
    const float unitKg = def && def->weight > 0.0f ? def->weight : 1.0f;
    const int byWeight = int(capacityLeftKg / unitKg);
    const int n = std::min({maxUnits, byWeight, from.count(id)});
    if (n <= 0) return 0;
    from.remove(id, n);
    to.add(id, n);
    return n;
}

void ai_caravan(entt::entity self, ecs::Position& p,
                ecs::MacroNpcRuntime& rt, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || rt.homeIsVillage || !ctx.world) {
        // No honest home city — degrade to the old nomad wander.
        ai_nomad(p, rt, ctx);
        return;
    }
    auto& reg = ctx.world->reg;
    auto* bag = reg.try_get<ecs::NpcInventory>(self);
    auto* mem = reg.try_get<AgentMemory>(self);
    Inventory* homeStore = settlement_inventory_by_id(ctx, rt.homeSettlementId);
    if (!bag || !mem || !homeStore) {
        ai_nomad(p, rt, ctx);
        return;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // Pick one of the HOME city's villages — nearest first.
        int villageId = -1;
        float best = 1e30f;
        for (auto& v : ctx.gs->villages) {
            if (v.nearestCityId != rt.homeSettlementId) continue;
            const float d = torus_dist_sq(p.x, p.y, float(v.x), float(v.y),
                                          float(ctx.mapW), float(ctx.mapH));
            if (d < best) {
                best = d;
                villageId = v.id;
                rt.targetX = float(v.x);
                rt.targetY = float(v.y);
            }
        }
        if (villageId < 0) {
            ai_nomad(p, rt, ctx);   // a world without villages: wander on
            return;
        }
        // DEPARTURE: remember the home market as it stands — the belief the
        // whole trip trades on.
        remember(*mem, pack_market_snapshot(
                           *homeStore,
                           std::uint16_t(rt.homeSettlementId),
                           ctx.gs->worldTime.day()));
        // Load EXPORTS: what home has plenty of and a village lives on —
        // crafted needs first (bread before jewelry), half the hold.
        const MemoryEntry* snap = recall(
            *mem, AgentMemoryKind::MarketSnapshot,
            std::uint16_t(rt.homeSettlementId));
        for (int i = 0; i < kNeedCount
                        && inventory_weight(bag->inv) < kCaravanCapacityKg / 2;
             ++i) {
            const int idx = commodity_index(kNeeds[i].commodity);
            if (idx < 0) continue;
            if (snap && market_stock_class(*snap, idx) < 3) continue;
            haul_between(*homeStore, bag->inv, kNeeds[i].commodity,
                         1 << 30,
                         kCaravanCapacityKg / 2 - inventory_weight(bag->inv));
        }
        rt.targetSettlementId = villageId;
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
        if (Inventory* vs = village_inventory_by_id(ctx,
                                                    rt.targetSettlementId)) {
            // Unload the exports…
            for (int i = 0; i < kCommodityCount; ++i) {
                haul_between(bag->inv, *vs, kCommodities[i].id, 1 << 30,
                             1e9f);
            }
            // …and load what the SNAPSHOT says the city lacks, scarcest
            // class first, raw before crafted within a class (a city's
            // business is to make things from them).
            const MemoryEntry* snap = recall(
                *mem, AgentMemoryKind::MarketSnapshot,
                std::uint16_t(rt.homeSettlementId));
            if (snap) {
                for (int cls = 0; cls <= 1; ++cls) {
                    for (int i = 0; i < kCommodityCount; ++i) {
                        if (market_stock_class(*snap, i) != cls) continue;
                        haul_between(*vs, bag->inv, kCommodities[i].id,
                                     1 << 30,
                                     kCaravanCapacityKg
                                         - inventory_weight(bag->inv));
                    }
                }
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
                // Home: the whole hold lands on the market.
                for (int i = 0; i < kCommodityCount; ++i) {
                    haul_between(bag->inv, *homeStore, kCommodities[i].id,
                                 1 << 30, 1e9f);
                }
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

// Follow the waypoint route in the squad's orders (Session 15, Inc 7): walk
// to the current waypoint, arrive, take the next, loop. A squad ordered onto
// a route with no route wanders — a degraded order is a visible NPC, not a
// frozen one.
void ai_waypoints(entt::entity e, ecs::Position& p, ecs::MacroNpcRuntime& rt,
                  const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.world
        ? ctx.world->reg.try_get<ecs::SquadOrders>(e) : nullptr;
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
// Health (try_move deliberately sees neither). On LAND a spent squad makes
// camp: Resting, debt kept (regen pays it off — the deeper the hole, the
// longer the rest, the player's own shape). On WATER there is no camp
// (owner ruling, Session 21): the outstanding debt bites the LORD's HP by
// the player's exhaustion law (kExhaustionBite), because the lord IS the
// squad — the roster is a row inside him, macro damage lands on the avatar.
// An ocean crossing the bar cannot pay therefore kills, through the same
// tracked-death door an auto-battle uses; the drowned lord's men settle by
// the standing dead-leader rule.
void settle_exhaustion(entt::entity e, const ecs::Position& p,
                       ecs::MacroNpcRuntime& rt, ecs::Health& hp,
                       const TickContext& ctx) {
    if (int(rt.sp) >= 0) return;
    if (!cell_is_water(ctx, int(p.x), int(p.y))) {
        if (rt.state != std::uint8_t(NPCState::Resting)) {
            rt.state = std::uint8_t(NPCState::Resting);
            rt.stateTimer = 0;
        }
        return;
    }
    const int bite =
        int(std::lround(float(-int(rt.sp)) * kExhaustionBite));
    if (bite <= 0) return;
    hp.hp -= float(bite);
    if (hp.hp <= 0.0f && ctx.world && ctx.gs) {
        settle_leader_fraction(*ctx.world, e, 0.0f);
        drain_dead_leader_squads(*ctx.world, ctx.gs->deserterPool);
    }
}

void dispatch(AIBehaviour b, entt::entity e, ecs::Position& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              const TickContext& ctx) {
    if (squad_threat_step(e, p, kind, rt, ctx)) return;
    switch (b) {
        case AIBehaviour::HomeWanderer: ai_home_wanderer(p, rt, ctx); break;
        case AIBehaviour::Woodcutter:   ai_woodcutter(e, p, rt, ctx); break;
        case AIBehaviour::Farmer:       ai_farmer    (e, p, rt, ctx); break;
        case AIBehaviour::CaravanTrade: ai_caravan   (e, p, rt, ctx); break;
        case AIBehaviour::Trader:       ai_trader       (p, rt, ctx); break;
        case AIBehaviour::Nomad:        ai_nomad        (p, rt, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive   (p, rt, ctx); break;
        case AIBehaviour::Patrol:       ai_patrol       (p, rt, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter   (p, rt, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer     (p, rt, ctx); break;
        case AIBehaviour::Waypoints:    ai_waypoints (e, p, rt, ctx); break;
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
                       bool allowAutoBattle,
                       const PathCostData* pathCost,
                       TreeLayer* trees,
                       const FeatureLayer* features,
                       const TerrainData* terrain) {
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
    ctx.pathCost = pathCost;
    ctx.trees    = trees;
    ctx.features = features;
    ctx.terrain  = terrain;

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
        dispatch(effective_behaviour(reg, e, kind), e, p, kind, rt, ctx);
        settle_exhaustion(e, p, rt, hp, ctx);
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
    GameState& gs, ecs::World& w, const TreeGrid* treeGrid,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle, const PathCostData* pathCost, TreeLayer* trees,
    const FeatureLayer* features, const TerrainData* terrain) {
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
    ctx.pathCost = pathCost;
    ctx.trees    = trees;
    ctx.features = features;
    ctx.terrain  = terrain;

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
                    dispatch(effective_behaviour(reg, e, kind), e, p, kind,
                             rt, ctx);
                    settle_exhaustion(e, p, rt, hp, ctx);
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
