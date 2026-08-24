// The macro march pays the map (Session 21): every squad step is priced by
// the SAME weight rows the player's travel is (movement_cost.h via the baked
// PathCostData), steered greedily around expensive ground, and settled by the
// one exhaustion door — camp on land, the debt's bite on water.
//
// What is pinned here is the owner's design made live:
//   · water is dear by DATA (weight 10), so a squad walks AROUND a wet cell
//     when dry progress exists — and the ledger (SP spent) proves it never
//     swam, sub-steps included;
//   · when the map leaves no dry way, the squad FORDS and pays the water
//     price — the negative control: the same ledger that proves avoidance
//     fires when crossing is forced;
//   · an ocean the bar cannot pay KILLS the lord (he IS the squad): Resting
//     is refused at sea, the debt bites his HP by the player's exhaustion
//     law, and his death settles through the standing dead-leader doors;
//   · on LAND a spent squad makes camp: Resting, debt kept, no blood;
//   · the calibration anchor: a fresh 110-SP bar buys ~8 game hours of road
//     for a squad exactly as it does for the player — one law, two walkers.
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/movement_cost.h"
#include "macro/pathfinding.h"
#include "macro/squad.h"
#include "macro/npc.h"
#include "core/torus.h"

#include <cmath>
#include <cstdint>

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

// A marching fixture: one Traveling caravan with an explicit, known sheet
// cache (bar 110 = the fresh traveller, no skills, neutral pace) so every
// number below is arithmetic, not a seed's opinion.
entt::entity make_walker(ecs::World& w, float x, float y,
                         float tx, float ty,
                         int maxSp, float hp = 100.0f) {
    auto e = w.reg.create();
    w.reg.emplace<ecs::Position>(e, x, y, 0.0f);
    w.reg.emplace<ecs::VisualPos>(e, x, y, 0.0f);
    w.reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Caravan),
                                std::uint16_t{0});
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = -1;
    rt.targetSettlementId = -1;
    rt.targetX = tx;
    rt.targetY = ty;
    rt.state = std::uint8_t(NPCState::Traveling);
    rt.maxSp = std::int16_t(maxSp);
    rt.sp = std::int16_t(maxSp);
    rt.travelRank = 0;
    rt.marathonRank = 0;
    rt.moveMult = 1.0f;
    w.reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    w.reg.emplace<ecs::MacroSpawnId>(e, 7u);
    w.reg.emplace<ecs::NpcLevel>(e, std::int16_t(1));
    w.reg.emplace<ecs::Health>(e, hp, hp);
    w.reg.emplace<ecs::SquadRoster>(e);
    return e;
}

int drive_until(GameState& gs, ecs::World& w, MacroNpcAiRuntime& rt,
                const PathCostData* grid, ecs::MacroNpcRuntime& npc,
                NPCState stop, int capThinks) {
    int thinks = 0;
    while (npc.state != std::uint8_t(stop) && thinks < capThinks) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle*/false);
        ++thinks;
    }
    return thinks;
}

// Drive until the walker stands within arrival range of (tx,ty). The ledger
// (sp + carry) must be read HERE, before an arrival state starts the Idle
// regen that would quietly refill what the march charged.
bool drive_to_arrival(GameState& gs, ecs::World& w, MacroNpcAiRuntime& rt,
                      const PathCostData* grid, entt::entity e,
                      float tx, float ty, int capThinks) {
    const auto& p = w.reg.get<ecs::Position>(e);
    for (int i = 0; i < capThinks; ++i) {
        if (torus_dist_sq(p.x, p.y, tx, ty,
                          float(gs.mapW), float(gs.mapH)) < 4.0f) {
            return true;
        }
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle*/false);
    }
    return torus_dist_sq(p.x, p.y, tx, ty,
                         float(gs.mapW), float(gs.mapH)) < 4.0f;
}

// SP the whole trip charged, fractional carry included — the ledger that sees
// every sub-step a per-think position trace cannot.
float sp_spent(const ecs::MacroNpcRuntime& npc, int maxSp) {
    return float(maxSp) - (float(npc.sp) + npc.spCarry);
}

// ── Water is walked around when dry progress exists ────────────────────────
void test_greedy_walks_around_a_wet_cell() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;
    ecs::World w;
    PathCostData grid = make_grid(32, 32, 1.0f);
    paint_water(grid, 16, 10);   // one wet cell dead on the straight line

    auto e = make_walker(w, 13.0f, 10.0f, 20.0f, 10.0f, 110);
    auto& npc = w.reg.get<ecs::MacroNpcRuntime>(e);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 21u);
    CHECK(drive_to_arrival(gs, w, rt, &grid, e, 20.0f, 10.0f, 8),
          "the walker reaches its destination past the wet cell");
    // 7 cells of weight-1 ground cost 7 × 7/16 ≈ 3.06 SP; ONE swum cell
    // would add ~3.9 more. The ledger says it stayed dry, sub-steps included.
    CHECK(sp_spent(npc, 110) < 4.0f,
          "the trip was paid at dry prices: the greedy step went around");
}

// ── Negative control: no dry way → the ford is paid at the water price ─────
void test_forced_ford_pays_the_water_price() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;
    ecs::World w;
    PathCostData grid = make_grid(32, 32, 1.0f);
    for (int y = 0; y < 32; ++y) paint_water(grid, 16, y);   // a full river

    auto e = make_walker(w, 13.0f, 10.0f, 20.0f, 10.0f, 110);
    auto& npc = w.reg.get<ecs::MacroNpcRuntime>(e);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 22u);
    CHECK(drive_to_arrival(gs, w, rt, &grid, e, 20.0f, 10.0f, 12),
          "a one-cell river is forded, not a wall");
    // Same 7-cell trip, but one cell now costs 10 × 7/16: the detector the
    // avoidance test trusts FIRES when the crossing is real.
    CHECK(sp_spent(npc, 110) > 6.0f,
          "the ford was charged at the water weight, by data");
}

// ── The ocean kills the lord: no camp at sea, the debt bites HP ────────────
void test_ocean_drowns_the_lord_and_settles_his_squad() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    ecs::World w;
    // Open ocean shore to shore — the torus offers no dry detour anywhere.
    PathCostData grid = make_grid(64, 64, 1.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) paint_water(grid, x, y);

    // A tiny bar and a mortal body: the crossing is unpayable by design.
    auto e = make_walker(w, 10.0f, 10.0f, 60.0f, 10.0f, /*maxSp*/8,
                         /*hp*/30.0f);
    auto& roster = w.reg.get<ecs::SquadRoster>(e);
    roster.members.push_back(make_soldier(
        std::uint8_t(NPCType::Peasant), 1, 101u));
    roster.members.push_back(make_soldier(
        std::uint8_t(NPCType::Peasant), 1, 102u));

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 23u);
    int thinks = 0;
    while (!w.reg.all_of<ecs::Dead>(e) && thinks < 400) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, false);
        ++thinks;
    }

    CHECK(w.reg.all_of<ecs::Dead>(e),
          "an ocean the bar cannot pay kills: there is no Resting at sea");
    CHECK(w.reg.get<ecs::Health>(e).hp <= 0.0f,
          "the death is the tracked shape every other death has (hp=0+Dead)");
    CHECK(w.reg.get<ecs::SquadRoster>(e).members.empty()
              && total_soldiers(gs.deserterPool) == 2,
          "the drowned lord's men settle by the standing dead-leader rule");
}

// ── On land a spent squad camps: Resting, debt kept, no blood ──────────────
void test_land_exhaustion_makes_camp_without_blood() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    ecs::World w;
    PathCostData grid = make_grid(64, 64, 2.0f);   // meadow everywhere

    auto e = make_walker(w, 10.0f, 10.0f, 60.0f, 10.0f, /*maxSp*/4);
    auto& npc = w.reg.get<ecs::MacroNpcRuntime>(e);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 24u);
    const int thinks = drive_until(gs, w, rt, &grid, npc,
                                   NPCState::Resting, 32);

    CHECK(thinks < 32 && npc.state == std::uint8_t(NPCState::Resting),
          "a bar spent on land is a camp, not a catastrophe");
    CHECK(int(npc.sp) <= 0,
          "the debt is kept on the bar, the player's own shape");
    CHECK(w.reg.get<ecs::Health>(e).hp == 100.0f,
          "no blood: the exhaustion bite belongs to water alone");
}

// ── THE anchor: a fresh bar buys ~8 game hours of road, squad or player ────
void test_road_bar_lasts_a_days_march() {
    GameState gs{};
    gs.mapW = 1024;
    gs.mapH = 8;
    ecs::World w;
    PathCostData grid = make_grid(1024, 8, 1.0f);   // one long road

    // Target 300 cells EAST — beyond the ~251 the bar can pay, and well
    // under the torus half-width so the straight step never discovers a
    // short way west around the seam.
    auto e = make_walker(w, 10.0f, 4.0f, 310.0f, 4.0f, /*maxSp*/110);
    auto& npc = w.reg.get<ecs::MacroNpcRuntime>(e);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 25u);
    const int thinks = drive_until(gs, w, rt, &grid, npc,
                                   NPCState::Resting, 200);

    const auto& p = w.reg.get<ecs::Position>(e);
    const float cells = p.x - 10.0f;
    const float hours = float(thinks) * kAiTickGameHours;
    // 110 SP / (7/16 per road cell) = 251 cells; at 32 cells/game hour that
    // is 7.9 h — the owner's day-of-marching anchor, for squads exactly as
    // the parity test pins it for the player.
    CHECK(npc.state == std::uint8_t(NPCState::Resting),
          "the road march ends in a camp, not in infinity");
    CHECK(cells > 240.0f && cells < 262.0f,
          "a fresh bar buys ~251 road cells");
    CHECK(hours > 7.0f && hours < 9.0f,
          "which is a day's march: ~8 game hours of road");
}

} // namespace

int main() {
    test_greedy_walks_around_a_wet_cell();
    test_forced_ford_pays_the_water_price();
    test_ocean_drowns_the_lord_and_settles_his_squad();
    test_land_exhaustion_makes_camp_without_blood();
    test_road_bar_lasts_a_days_march();
    return sm::test::report("squad_travel_test");
}
