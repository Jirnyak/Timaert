// The macro march pays the map by the HOUR (owner, 2026-10-06): a squad burns
// the ground under it every hour at the SAME weight rows the player burns,
// steered greedily or by the baked gait around expensive ground, and settled
// by the one door that also hands back rest (macro/recovery.h
// settle_pools_over_time). There is no price of a step and no wall anywhere.
//
// What is pinned here is the owner's design made live:
//   · water is dear by DATA (weight 10), so a squad walks AROUND a wet cell
//     when dry progress exists — and the ledger (SP spent) proves the detour
//     was taken for a REASON, by comparing it against a forced ford;
//   · when the map leaves no dry way, the squad FORDS and arrives: «ВЕСА БЫЛО
//     ЕДИНОЕ РЕШЕНИЕ», and a bridge becomes the CHEAP crossing instead of the
//     only one;
//   · an ocean the bar cannot pay KILLS the lord (he IS the squad): camp
//     cannot repay a sea hour, so the automaton does not pitch one, the debt
//     bites his HP by the one exhaustion law, and his death settles through
//     the standing dead-leader doors;
//   · on LAND a spent squad makes camp: Resting, debt kept, no blood;
//   · the calibration anchor, restated on the DEAR end of the ladder: a leg
//     lasts the camp margin over its ground's burn rate, and a full bar keeps
//     a body afloat the better part of a day — one law, two walkers.
#include "check.h"
#include <vector>

#include "macro/npc_ai.h"
#include "macro/world_row.h"
#include "macro/movement_cost.h"
#include "macro/pathfinding.h"
#include "macro/squad.h"
#include "tables/npc.h"
#include "core/torus.h"
#include "macro/store.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <utility>

namespace {

using namespace sm;

// A hand-built cost world: uniform `base` weight, cells painted wet get the
// water weight AND the water flag — the same pairing build_cost_grid bakes.
PathCostData make_grid(int w, int h, float base) {
    PathCostData g;
    g.width = w;
    g.height = h;
    g.costGrid.assign(std::size_t(w) * std::size_t(h), base);
    g.water.assign(std::size_t(w) * std::size_t(h), 0u);
    return g;
}

void paint_water(PathCostData& g, int x, int y) {
    const std::size_t i =
        std::size_t(y) * std::size_t(g.width) + std::size_t(x);
    g.costGrid[i] = biome_sp_weight(Water);
    g.water[i] = 1u;
}

// A BRIDGE, spelled the way the shipping bake spells it (pathfinding.cpp
// build_cost_grid: `costGrid[i] = cell_sp_weight(biome, feature, density)`) —
// the engineered bed goes INTO the one cost grid, and the water mask stays
// raised because the cell is still water geographically.
//
// It needs saying because until 2026-10-06 a bridge worked through a SECOND
// source of truth: `nav_can_stand` read the FeatureLayer directly, so a
// fixture could paint a bridge nowhere near the cost grid and still see a
// march cross it. That predicate is gone, the grid is the only answer, and a
// fixture that paints water over a bridge is now simply describing a world
// the generator never builds (AGENTS §8 п.8 — the фикстура-лжец).
void paint_bridge(PathCostData& g, int x, int y) {
    const std::size_t i =
        std::size_t(y) * std::size_t(g.width) + std::size_t(x);
    g.costGrid[i] = cell_sp_weight(Water, FT_Bridge);
    g.water[i] = 1u;
}

// A marching fixture: one Traveling caravan with an explicit, known sheet
// cache (bar 110 = the fresh traveller, no skills, neutral pace) so every
// number below is arithmetic, not a seed's opinion.
sm::MacroHandle make_walker(ecs::World& w, int mapW, float x, float y,
                            float tx, float ty,
                            int maxSp, int hp = 100) {
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(int(x), int(y), mapW)};
    st.visual[h.slot] = ecs::MacroVisual{x, y, 0.0f};
    // НОСИТЕЛЬ МАРША — ЖИВОЙ (2026-09-22). Прежде ходок ехал на роли
    // TaxCollector и сваливался в ai_nomad фолбэком снесённого ai_taxrun:
    // роль умерла, фолбэк вместе с ней, и девятнадцать законов марша разом
    // перестали проверяться ЧЕМ БЫ ТО НИ БЫЛО, оставаясь при этом целыми.
    // Теперь ходок — ТИП СКВАДА (первый ответ диспетчера): корован без
    // дома честно доходит до того же ai_nomad и просто идёт к цели.
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t{0}};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::Caravan);
    rt.homeSettlementId = 0;
    rt.targetSettlementId = 0;
    rt.targetX = tx;
    rt.targetY = ty;
    rt.state = std::uint8_t(NPCState::Traveling);
    rt.travelRank = 0;
    rt.marathonRank = 0;
    rt.moveMult = 1.0f;
    st.runtime[h.slot] = rt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{7u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(1)};
    // Three bars, one block (pools landing): the legs' bar is filled here
    // beside the wound, not in the march runtime.
    ecs::Pools pools{};
    pools.hp = pools.maxHp = hp;
    pools.sp = pools.maxSp = maxSp;
    st.pools[h.slot] = pools;
    return h;
}

int drive_until(GameState& gs, ecs::World& w, MacroNpcAiRuntime& rt,
                const PathCostData* grid, ecs::MacroNpcRuntime& npc,
                NPCState stop, int capThinks) {
    int thinks = 0;
    while (npc.state != std::uint8_t(stop) && thinks < capThinks) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
        ++thinks;
    }
    return thinks;
}

// Drive until the walker stands within arrival range of (tx,ty). The ledger
// (sp + carry) must be read HERE, before an arrival state starts the Idle
// regen that would quietly refill what the march charged.
bool drive_to_arrival(GameState& gs, ecs::World& w, MacroNpcAiRuntime& rt,
                      const PathCostData* grid, sm::MacroHandle e,
                      float tx, float ty, int capThinks) {
    // Fresh decode EVERY look (never a reference across a tick — the
    // landing-4 grabla): the store is the cell, and a tick moves it.
    auto at = [&]() {
        const auto c = (*sm::body_state<ecs::MacroCell>(sm::store_of(w), e));
        return MacroPos{float(ecs::cell_x(c, gs.mapW)),
                        float(ecs::cell_y(c, gs.mapW))};
    };
    for (int i = 0; i < capThinks; ++i) {
        const MacroPos p = at();
        if (torus_dist_sq(p.x, p.y, tx, ty,
                          float(gs.mapW), float(gs.mapH)) < 4.0f) {
            return true;
        }
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const MacroPos p = at();
    return torus_dist_sq(p.x, p.y, tx, ty,
                         float(gs.mapW), float(gs.mapH)) < 4.0f;
}

// SP the whole trip charged, fractional carry included — the ledger that sees
// every sub-step a per-think position trace cannot.
float sp_spent(const ecs::Pools& pools, int maxSp) {
    return float(maxSp) - (float(pools.sp) + pools.spCarry);
}

// ── Water is walked around when dry progress exists ────────────────────────
// ...AND THE SAME LEDGER PROVES THE OTHER HALF. Avoidance alone is a weak
// claim: a greedy step that never looked at the weight grid at all would pass
// it too, because the straight line also goes around a single cell most of
// the time. So the two runs are compared — a detour that CAN be made against
// a river that must be forded — and the ford must cost strictly more. That
// comparison is the negative control, and it is immune to any recalibration
// of the knob (AGENTS §8 п.4, п.6).
void test_greedy_walks_around_a_wet_cell() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;

    const auto trip_cost = [&](bool fullRiver) {
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        sm::store_attach(w, wStore_.get());
        PathCostData grid = make_grid(32, 32, 1.0f);
        if (fullRiver) {
            for (int y = 0; y < 32; ++y) paint_water(grid, 16, y);
        } else {
            paint_water(grid, 16, 10);   // one wet cell dead on the line
        }
        auto e = make_walker(w, gs.mapW, 13.0f, 10.0f, 20.0f, 10.0f, 110);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, fullRiver ? 32u : 21u);
        const bool arrived =
            drive_to_arrival(gs, w, rt, &grid, e, 20.0f, 10.0f, 64);
        const float spent =
            sp_spent((*sm::body_state<ecs::Pools>(sm::store_of(w), e)), 110);
        return std::pair<bool, float>{arrived, spent};
    };

    const auto around = trip_cost(/*fullRiver=*/false);
    const auto through = trip_cost(/*fullRiver=*/true);
    CHECK(around.first, "the walker reaches its destination past the wet cell");
    CHECK(through.first,
          "and reaches it ACROSS a full river too — water is a PRICE, not a "
          "wall (owner 2026-10-06: «ВЕСА БЫЛО ЕДИНОЕ РЕШЕНИЕ»)");
    CHECK(through.second > around.second * 1.5f,
          "and the ford costs strictly more than the detour: the ledger is "
          "what proves the detour was taken for a REASON. If this fires with "
          "the two equal, the weight grid is not being read at all");
}

// ── No dry way: the river is FORDED, and a bridge is the cheaper crossing ──
// THE headline of M-236, and the exact reversal of what stood here until
// 2026-10-06 («the river is a WALL for a walker; a bridge is the door»). The
// owner retired the wall by name — «да уничтодить вторую стену она портит всё
// (ВЕСА БЫЛО ЕДИНОЕ РЕШЕНИЕ» — because a veto living beside a price meant two
// answers to one question about the world, and because it held 4328 land
// cells of every measured world outside all navigation. A bridge does not stop
// being worth building: it stops being the ONLY way across.
void test_river_is_forded_and_a_bridge_is_cheaper() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;

    const auto cross = [&](bool bridged, unsigned seed) {
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        sm::store_attach(w, wStore_.get());
        PathCostData grid = make_grid(32, 32, 1.0f);
        for (int y = 0; y < 32; ++y) paint_water(grid, 16, y);   // full river
        if (bridged) paint_bridge(grid, 16, 10);   // dead on the straight line
        auto e = make_walker(w, gs.mapW, 13.0f, 10.0f, 20.0f, 10.0f, 110);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, seed);
        bool crossed = false;
        for (int i = 0; i < 96 && !crossed; ++i) {
            MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
            crossed = float(ecs::cell_x(
                          (*sm::body_state<ecs::MacroCell>(sm::store_of(w), e)),
                          gs.mapW)) >= 18.0f;
        }
        const auto& pools = (*sm::body_state<ecs::Pools>(sm::store_of(w), e));
        return std::tuple<bool, float, int>{crossed, sp_spent(pools, 110),
                                            pools.hp};
    };

    const auto bare = cross(/*bridged=*/false, 22u);
    const auto bridged = cross(/*bridged=*/true, 45u);

    CHECK(std::get<0>(bare),
          "A RIVER WITH NO BRIDGE IS CROSSED. The wall is gone: a ford is "
          "expensive, and expensive is not forbidden");
    CHECK(std::get<2>(bare) >= 1,
          "and a narrow river is survived — 8 SP of water out of a 110-SP "
          "bar, so the ford costs sweat, not life (CANON S7)");
    CHECK(std::get<0>(bridged),
          "the bridge carries the same march");
    CHECK(std::get<1>(bridged) < std::get<1>(bare),
          "AND IT IS CHEAPER, which is the honest reason to build one: a bed "
          "of 1.0 over water is a cheap crossing instead of the only one");
}

// ── Deep water: the crossing is paid in blood, and an unpayable one kills ─
// A walker DOES enter water since 2026-10-06 (the ford test above), so this
// is no longer about «whoever is already floating» — it is the ordinary price
// of a wide crossing. Near the shore a body wades out bleeding; a shore its
// flesh cannot reach kills it, and the dead squad leaves the map. What makes
// the sea lethal is arithmetic, not a veto: 20 SP of burn an hour against
// 13.75 of rest, so camp cannot repay it and the automaton does not pitch one
// (movement_cost.h camp_repays_its_hour) — it keeps wading and pays the debt.
void test_ocean_drowns_who_cannot_reach_the_shore() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    PathCostData grid = make_grid(64, 64, 1.0f);
    // A wide strait: land only at x ≥ 26.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 26; ++x) paint_water(grid, x, y);

    {   // Near the shore: out in debt, bled but alive.
        // The bar is what makes a shore NEAR — not the cell count. Water
        // burns 20 SP a game HOUR since 2026-10-06, and a water cell takes
        // √10/8 ≈ 0.4 h to wade, so three cells of strait cost ≈ 24 SP. A bar
        // of 14 therefore reaches the beach owing ≈ 10, and the QUADRATIC
        // bite (CANON S14.1, «как в Elin») takes that debt point by point —
        // deep enough to bleed, shallow enough to live.
        auto e = make_walker(w, gs.mapW, 23.0f, 10.0f, 30.0f, 10.0f, /*maxSp*/14,
                             /*hp*/30.0f);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 23u);
        // THE LOW-WATER MARK, not the final reading. Since the one recovery
        // law reached the NPC camp (2026-09-09, CANON S14), a body that
        // reaches its target CAMPS THERE and mends — so by think 60 this
        // survivor is whole again and a probe of his final HP measures the
        // rest, not the bite. The promise here is that the sea BIT him; the
        // honest place to read that is while it is happening.
        int lowest = (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp;
        for (int i = 0; i < 60 && sm::store_of(w).valid(e); ++i) {
            MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
            if (sm::store_of(w).valid(e))
                lowest = std::min(lowest, (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp);
        }
        CHECK(sm::store_of(w).valid(e) && float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), e)), gs.mapW)) >= 26.0f,
              "a floating body wades OUT: water is exited, never entered");
        CHECK(lowest < 30,
              "and the unpayable steps out were paid in blood — the sea "
              "bite lives");
        CHECK((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp > lowest,
              "and ashore the wound MENDS: the one recovery law reaches an "
              "NPC in camp, not only the player");
    }
    {   // Far from shore: the crossing is unpayable by design and kills.
        auto e = make_walker(w, gs.mapW, 2.0f, 40.0f, 30.0f, 40.0f, /*maxSp*/8,
                             /*hp*/30.0f);
        sm::MacroStore& std_ = sm::store_of(w);
        const sm::MacroHandle eh =
            e;
        std_.spawnId[eh.slot] = ecs::MacroSpawnId{55u};
        auto& bag = std_.inventory[eh.slot];
        creatures_push(bag.inv, make_soldier(
            std::uint8_t(NPCType::Peasant), 1, 101u));
        creatures_push(bag.inv, make_soldier(
            std::uint8_t(NPCType::Peasant), 1, 102u));
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 24u);
        int thinks = 0;
        // 6.3: смерть сквада — смерть СЛОТА store (свип конца тика), суд по
        // хэндлу; entt-тела у макро-сквада больше нет.
        while (std_.valid(eh) && std_.dead[eh.slot] == 0 && thinks < 400) {
            MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
            ++thinks;
        }
        CHECK(!std_.valid(eh),
              "an ocean the bar cannot pay kills, and the dead squad leaves "
              "the map: there is no Resting at sea and no corpse-row after");
        CHECK(creature_count(gs.deserterPool) == 0,
              "утонувший лорд утопил и своих людей: павшие ГИБНУТ, а в пул "
              "дезертиров идёт только неоплата сезона (M-228)");
    }
}

// ── One exhaustion law: the step in debt bleeds, the camp does not ────────
// Owner, 2026-08-27: «истощение — это когда SP кончилось, и тогда отнимается
// HP от ДВИЖЕНИЯ по миру; остановился — отдыхаешь». On land a spent squad
// still makes camp — that is the AI's DECISION — but the step that emptied
// the bar is paid for in flesh, exactly as the player's identical step is.
// The old shape had the bite belong to water alone, so a squad could march
// itself into the ground on dry meadow for free while the player bled.
void test_land_exhaustion_makes_camp_without_blood() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    PathCostData grid = make_grid(64, 64, 2.0f);   // meadow everywhere

    // A REAL bar (110 — the minimum any row in the creature table carries,
    // measured) drawn down to just above the camp margin. The old fixture used
    // a bar of FOUR to reach the margin in a handful of thinks, and since
    // 2026-10-06 that body is not a body: the regen is a PERCENT of the bar
    // while the burn is ABSOLUTE, so four points of bar earn 0.5 SP/h against
    // the meadow's 4 and camp repays nowhere — it would march until the debt
    // killed it. Measured over all 46 creature rows: the smallest real maxSp
    // is 110 and a road repays from 16 up, so no body in the game is in that
    // class. The fixture had to stop being one.
    auto e = make_walker(w, gs.mapW, 10.0f, 10.0f, 60.0f, 10.0f, /*maxSp*/110);
    sm::store_of(w).pools[e.slot].sp = 20;   // a leg's worth above the margin
    auto& npc = (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), e));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 24u);
    const int thinks = drive_until(gs, w, rt, &grid, npc,
                                   NPCState::Resting, 32);

    CHECK(thinks < 32 && npc.state == std::uint8_t(NPCState::Resting),
          "a bar spent on land is a camp, not a catastrophe");
    const auto& campPools = (*sm::body_state<ecs::Pools>(sm::store_of(w), e));
    CHECK(campPools.sp <= campPools.maxSp / kCampBarDivisor,
          "the legs stopped at the camp margin, the automaton's own answer");
    const float bled = 100.0f - (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp;
    // The camp decision lands BEFORE debt (npc_ai.h kCampBarDivisor): on
    // campable ground nobody bleeds — the bite stays a LAW for whoever
    // cannot stop (the ocean section above drowns a lord through it) or
    // chooses to keep walking (the player).
    CHECK(bled == 0.0f,
          "camp is pitched before the debt: no blood on campable ground");

    // The camp itself is free — «остановился, значит отдыхаешь». Thinks spent
    // Resting must cost nothing, or a tired squad would bleed out standing
    // still. This is the control that separates "moving in debt" from
    // "being in debt".
    const float campedAt = (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp;
    // The LEDGER, not the bar: regen is fractional (kRestRegenPctPerHour of a
    // 4-point bar per game hour), so eight thinks may not add a WHOLE point.
    // The file's own convention — sp + carry — is what actually moved.
    const float ledgerAt = float((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).sp) + (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).spCarry;
    for (int i = 0; i < 8; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp == campedAt,
          "eight thinks in camp cost no blood at all");
    CHECK(float((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).sp) + (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).spCarry > ledgerAt,
          "negative control: those thinks DID pass — the bar was refilling");
}

// ── The world does not bleed out (the new law's blast radius) ─────────────
// Making the exhaustion bite universal means every squad on the map now pays
// flesh for marching in debt, where before only a drowning one did. That is a
// change to a law thousands of bodies live under, so it is not enough to
// prove one walker behaves — the question is whether a MAP of them survives
// an ordinary long haul. A hundred caravans, a season of thinks, ordinary
// ground: they may bleed, they must not die.
void test_a_map_of_marchers_survives_the_new_law() {
    GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    // MOUNTAIN ground, not meadow, and that is what forces the case: with a
    // real bar (110) the burn has to be heavy enough that 116 cells of haul
    // cannot be walked in one leg. Mountain burns 10 SP/h, so a leg lasts
    // ~9.6 game hours and the haul takes three of them — run out, camp,
    // refill, run out again. The old fixture got there with a bar of 20 on
    // meadow, and a 20-point bar is not a body any more (see the camp test
    // above): its 2.5 SP/h of regen cannot repay a meadow, so a hundred of
    // them would march until the debt killed every one.
    PathCostData grid = make_grid(128, 128, biome_sp_weight(Mountain));

    // Every one of these WILL run out, camp, refill and run out again. That is
    // the worst honest case the law can be put to, and it is the case the old
    // water-only bite never charged at all.
    constexpr int kWalkers = 100;
    std::vector<sm::MacroHandle> walkers;
    walkers.reserve(kWalkers);
    for (int i = 0; i < kWalkers; ++i) {
        const float y = float(i % 100) + 8.0f;
        const sm::MacroHandle e =
            make_walker(w, gs.mapW, 4.0f, y, 120.0f, y, /*maxSp*/110);
        // Each one hauls right across the map on its own line.
        sm::store_of(w).spawnId[e.slot] =
            ecs::MacroSpawnId{std::uint32_t(100 + i)};
        walkers.push_back(e);
    }

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 77u);
    for (int think = 0; think < 600; ++think) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    int alive = 0, bled = 0, moved = 0;
    for (const sm::MacroHandle e : walkers) {
        // A dead squad LEAVES the map (S4): a destroyed walker counts as
        // neither alive nor moved, so a regression fails the checks below
        // loudly instead of dereferencing a gone entity.
        if (!sm::store_of(w).valid(e)) continue;
        if (sm::store_of(w).dead[e.slot] == 0
            && (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp > 0.0f) ++alive;
        if ((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).hp < 100.0f) ++bled;
        if (float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), e)), gs.mapW)) != 4.0f) ++moved;
    }
    CHECK(alive == kWalkers,
          "a season of honest marching kills nobody: the bite is a cost, "
          "not a cull");
    CHECK(moved == kWalkers,
          "negative control: they all actually WALKED — the survival above "
          "is not the survival of a hundred bodies standing still");
    // The camp decision (kCampBarDivisor) spares every one of them: camp
    // is pitched before the debt, so dry-land marching draws no blood at
    // all — 447 caravans died of the chronic-exhaustion shape this pins
    // against (measured 2026-08-30). The bite's own negative control is
    // the ocean section above, where stopping is impossible.
    CHECK(bled == 0,
          "the camp decision spares them all: no blood where camp is "
          "possible");
}

// ── THE anchor, RESTATED ON THE DEAR END (2026-10-06) ─────────────────────
// It used to read «a fresh bar buys ~8 game hours of road, squad or player».
// That sentence cannot survive the hour quantum: the road burns 2 SP an hour
// against 13.75 of rest, so a body ON a road gains by standing there and its
// marching hours are not what the economy balances against. What the economy
// balances now is the dear end — how long a bar keeps a body going where rest
// cannot repay the burn — and movement_cost.h asserts that at compile time
// (kWaterDriftHoursPerFreshBar). The owner ruled the shift ACCEPTED, not
// tuned: «если сдвинет мир не важно не надо даже подгонять».
//
// So what the SQUAD is asked here is the other half, the one no constant can
// state: that the body walking on those constants agrees with them — its leg
// ends in a camp, after the hours the bar's burn rate says, over the cells the
// pace says. The design literals stay below, on the ladder rather than on the
// road.
void test_road_bar_lasts_a_days_march() {
    GameState gs{};
    // ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки (было 1024x8).
    // Полуширина тора не изменилась (512 > 300 целевых клеток), а
    // адресация только пришла в согласие с собой: ecs::cell_index и так
    // маскировал ОБЕ оси по mapW, то есть мир уже был квадратным для
    // адреса и тонким только для аллокации.
    gs.mapW = 1024;
    gs.mapH = 1024;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    // MEADOW, not road: the leg has to END, and on a road it never would —
    // standing still there repays more than walking burns, so the automaton
    // keeps its legs forever. That is not a defect to work around but the law
    // this fixture has to respect, and it is why the bed moved to open
    // country: 4 SP/h of burn against 13.75 of rest means a MARCHING body
    // still runs out (the regen is off for legs in motion) while a camped one
    // profits.
    const float bed = biome_sp_weight(Meadow);
    PathCostData grid = make_grid(1024, 1024, bed);

    // Target 300 cells EAST — well beyond what the bar can pay, and well
    // under the torus half-width so the straight step never discovers a
    // short way west around the seam.
    auto e = make_walker(w, gs.mapW, 10.0f, 4.0f, 310.0f, 4.0f, /*maxSp*/110);
    auto& npc = (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), e));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 25u);
    const int thinks = drive_until(gs, w, rt, &grid, npc,
                                   NPCState::Resting, 400);

    const auto& pcell = (*sm::body_state<ecs::MacroCell>(sm::store_of(w), e));
    const MacroPos p{float(ecs::cell_x(pcell, gs.mapW)),
                     float(ecs::cell_y(pcell, gs.mapW))};
    const float cells = p.x - 10.0f;
    const float hours = float(thinks) * kAiTickGameHours;
    // The leg ends at the CAMP MARGIN, not at zero (npc_ai.h
    // kCampBarDivisor — the same number the automaton reads), and the hours
    // it lasts are the margin over the BURN RATE. The pace does not appear:
    // that is the hour quantum, stated by its absence.
    const float expectHours =
        (110.0f - 110.0f / float(kCampBarDivisor))
        / burn_stamina_per_hour(bed);
    // ...and the CELLS are those hours walked at the ground's own pace, which
    // is the only place terrain_speed_mult enters the economy now.
    const float expectCells =
        expectHours * kMacroWalkCellsPerHour * terrain_speed_mult(bed);
    CHECK(npc.state == std::uint8_t(NPCState::Resting),
          "the march ends in a camp, not in infinity");
    CHECK(hours > expectHours * 0.9f && hours < expectHours * 1.1f,
          "a leg lasts the camp margin over its ground's BURN RATE — hours, "
          "not cells");
    CHECK(cells > expectCells * 0.85f && cells < expectCells * 1.15f,
          "and covers those hours at the ground's own pace");

    // ── THE DESIGN, in literals ──────────────────────────────────────────
    // The ladder, said twice on purpose: movement_cost.h asserts it over the
    // constants at compile time, and THIS guards the squad that walks on them.
    CHECK(kWaterDriftHoursPerFreshBar > 8.0f
              && kWaterDriftHoursPerFreshBar < 32.0f,
          "a full bar keeps a body afloat the better part of a day — the one "
          "number the economy is anchored on now that the road is cheap");
    CHECK(burn_stamina_per_hour(biome_sp_weight(Water))
              > kFreshBarSp * kRestRegenPctPerHour,
          "open water out-burns any rest: the ocean is lethal by PRICE");
    CHECK(burn_stamina_per_hour(feature_bed_weight(FT_Road))
              < kFreshBarSp * kRestRegenPctPerHour,
          "...and a road does not, or there would be nowhere in the world to "
          "rest at all");

    // The bar the ladder is stated against is the bar the SHEET hands a fresh
    // level-1 body — the literal in movement_cost.h cannot drift away from
    // attributes.h without this line saying so.
    CHECK(bar_ceilings(Attributes{}, Skills{}, 100, 100, 100).maxSp
              == int(kFreshBarSp),
          "kFreshBarSp is the bare level-1 bar the sheet actually builds");

    // THE CAMP DECISION IS ARITHMETIC, NOT PLACE (movement_cost.h
    // camp_repays_its_hour) — the door that replaced the standing wall in the
    // automaton. Without it the demolition would have defeated itself: a body
    // low on legs in open water would pitch camp there, never reach the
    // half-bar wake-up, and drown on the spot.
    CHECK(camp_repays_its_hour(
              burn_stamina_per_hour(feature_bed_weight(FT_Road)), 110, 0),
          "a road is worth camping on");
    CHECK(!camp_repays_its_hour(
              burn_stamina_per_hour(biome_sp_weight(Water)), 110, 0),
          "open water is not, and the automaton must not pitch one there");
}

// ── The regen gate is «остановился», not «не сдвинулся в этот раз» ────────
// The pace is fractional, so a marching squad banks part-cells and stands on
// a quarter of its thinks. If those counted as rest, the road would pay for
// itself and no squad would ever run out of legs — which is exactly what the
// first cut of this law did. Pinned here with its negative control.
void test_banking_a_part_cell_is_not_resting() {
    GameState gs{};
    // Wide enough that the target is far inside the torus half-width: at 512
    // the greedy straight step found the SEAM a shorter way and walked west,
    // which is correct behaviour and a useless fixture.
    // ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки (было 1024x8).
    // Полуширина тора не изменилась (512 > 300 целевых клеток), а
    // адресация только пришла в согласие с собой: ecs::cell_index и так
    // маскировал ОБЕ оси по mapW, то есть мир уже был квадратным для
    // адреса и тонким только для аллокации.
    gs.mapW = 1024;
    gs.mapH = 1024;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    PathCostData grid = make_grid(1024, 1024, 1.0f);   // one long road

    auto e = make_walker(w, gs.mapW, 10.0f, 4.0f, 400.0f, 4.0f, /*maxSp*/110);
    auto& npc = (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), e));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 91u);

    // Forty thinks of unbroken marching, far short of the ~110 cells the bar
    // can pay, so nothing here is confused by exhaustion or camp.
    for (int i = 0; i < 40; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const float cells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), e)), gs.mapW)) - 10.0f;
    const float ledgerSpent = 110.0f - (float((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).sp) + (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).spCarry);

    CHECK(cells > 0.0f, "the walker is on the road");
    // THE gate, measured in the quantum it is paid in: forty thinks of
    // marching cost forty thinks of BURN, to the point — not one slice less.
    // A think quietly settled as rest would show up as a shortfall here, and
    // that is exactly the defect this test was built for (the first cut of the
    // law let the road pay for itself, because a marcher banking a part-cell
    // stands still on roughly a quarter of its thinks).
    const float expectBurn =
        burn_stamina_per_hour(1.0f) * kAiTickGameHours * 40.0f;
    CHECK(std::fabs(ledgerSpent - expectBurn) < 0.01f,
          "forty thinks of marching cost exactly forty thinks of burn — not "
          "one slice less, so no think on the road was quietly paid as rest");

    // The control: the SAME body, standing at its target, DOES recover.
    npc.targetX = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), e)), gs.mapW));
    npc.targetY = 4.0f;
    const float restingFrom = float((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).sp) + (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).spCarry;
    for (int i = 0; i < 8; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(float((*sm::body_state<ecs::Pools>(sm::store_of(w), e)).sp) + (*sm::body_state<ecs::Pools>(sm::store_of(w), e)).spCarry > restingFrom,
          "negative control: standing where it meant to be, it recovers — "
          "the gate is «остановился», and it is open");
}

// ── Перегруз универсальный всем: the pack is part of the price ────────────
// Owner's ruling, 2026-08-27. The overload surcharge was the PLAYER's alone —
// a squad hauling a ton of iron marched exactly as briskly as an empty scout,
// so the weight in its bag was a number in a panel and not a cost. Pinned
// here with the control that makes it a claim: the SAME body, the SAME road,
// the SAME distance, one of them laden.
void test_a_laden_squad_pays_for_its_load() {
    GameState gs{};
    gs.mapW = 256;
    gs.mapH = 256;   // ЗАКОН АДРЕСА: квадрат, po2
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    PathCostData grid = make_grid(256, 256, 1.0f);   // one long road

    // Bodies that SURVIVE the measurement: an overloaded march in debt bites
    // HP every moving think, and this fixture used to let the laden walker
    // march itself to death and then read the corpse-row's ledger. A dead
    // squad LEAVES the map at tick end now (CANON S4, 2026-08-29), so the
    // fixture gives both walkers the health to outlive the 30 thinks — what
    // is measured here is the PRICE of the load, not the death it can buy.
    auto light = make_walker(w, gs.mapW, 10.0f, 4.0f, 60.0f, 4.0f, /*maxSp*/110,
                             /*hp*/1e6f);
    auto heavy = make_walker(w, gs.mapW, 10.0f, 6.0f, 60.0f, 6.0f, /*maxSp*/110,
                             /*hp*/1e6f);
    sm::store_of(w).spawnId[heavy.slot] =
        ecs::MacroSpawnId{8u};
    (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), light)).targetY = 4.0f;
    (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), heavy)).targetY = 6.0f;

    // A back a person actually has, and a load well past it.
    const float cap = 40.0f;
    (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), light)).carryCap = cap;
    (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), heavy)).carryCap = cap;
    auto& load =
        sm::store_of(w).inventory[heavy.slot].inv;
    // A BEARABLE overload: past the back, but a price the bar can pay per
    // step. (An unbearable pack now honestly refuses to march at all — the
    // legs decline a step they cannot pay for, by the same pre-priced law.)
    load.add("iron", 15);
    CHECK(inventory_weight(load) > cap,
          "the fixture is honest: this load IS over the back carrying it");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 93u);
    for (int i = 0; i < 30; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const auto& lrt = (*sm::body_state<ecs::Pools>(sm::store_of(w), light));
    const auto& hrt = (*sm::body_state<ecs::Pools>(sm::store_of(w), heavy));
    const float lightSpent = 110.0f - (float(lrt.sp) + lrt.spCarry);
    const float heavySpent = 110.0f - (float(hrt.sp) + hrt.spCarry);
    const float lightCells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), light)), gs.mapW)) - 10.0f;
    const float heavyCells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(sm::store_of(w), heavy)), gs.mapW)) - 10.0f;

    CHECK(heavySpent > lightSpent,
          "the laden squad paid more for the same road — the pack is a cost");
    const auto& lrtRun = (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), light));
    const auto& hrtRun = (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), heavy));
    CHECK(hrtRun.overloadCost > 0 && lrtRun.overloadCost == 0,
          "and the surcharge is on the laden one alone");
    CHECK(lightCells > 0.0f && heavyCells > 0.0f,
          "negative control: BOTH of them actually walked, so the gap above "
          "is a price and not a body standing still");
}

} // namespace

int main() {
    test_greedy_walks_around_a_wet_cell();
    test_river_is_forded_and_a_bridge_is_cheaper();
    test_ocean_drowns_who_cannot_reach_the_shore();
    test_land_exhaustion_makes_camp_without_blood();
    test_banking_a_part_cell_is_not_resting();
    test_a_laden_squad_pays_for_its_load();
    test_a_map_of_marchers_survives_the_new_law();
    test_road_bar_lasts_a_days_march();
    return sm::test::report("squad_travel_test");
}
