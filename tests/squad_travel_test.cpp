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
#include <vector>

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
    // Seven-to-eight weight-1 cells cost that many × kStaminaPerCell; ONE
    // swum cell would add ten more. Derived, never pinned: the ledger says
    // the trip stayed dry, sub-steps included.
    CHECK(sp_spent(npc, 110) < 9.0f * kStaminaPerCell,
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
    roster.squad.push(make_soldier(
        std::uint8_t(NPCType::Peasant), 1, 101u));
    roster.squad.push(make_soldier(
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
    CHECK(w.reg.get<ecs::SquadRoster>(e).squad.empty()
              && total_soldiers(gs.deserterPool) == 2,
          "the drowned lord's men settle by the standing dead-leader rule");
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
    const float bled = 100.0f - w.reg.get<ecs::Health>(e).hp;
    CHECK(bled > 0.0f,
          "the step that emptied the bar cost flesh, on meadow as at sea");
    CHECK(bled < 100.0f,
          "and it is a bite, not a death: a spent squad camps, it does not "
          "march itself to pieces");

    // The camp itself is free — «остановился, значит отдыхаешь». Thinks spent
    // Resting must cost nothing, or a tired squad would bleed out standing
    // still. This is the control that separates "moving in debt" from
    // "being in debt".
    const float campedAt = w.reg.get<ecs::Health>(e).hp;
    // The LEDGER, not the bar: regen is fractional (kSpRegenPctPerHour of a
    // 4-point bar per game hour), so eight thinks may not add a WHOLE point.
    // The file's own convention — sp + carry — is what actually moved.
    const float ledgerAt = float(npc.sp) + npc.spCarry;
    for (int i = 0; i < 8; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, false);
    }
    CHECK(w.reg.get<ecs::Health>(e).hp == campedAt,
          "eight thinks in camp cost no blood at all");
    CHECK(float(npc.sp) + npc.spCarry > ledgerAt,
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
    PathCostData grid = make_grid(128, 128, 2.0f);   // ordinary land

    // A bar deliberately far too small for the haul (20 points against ~230
    // of ground): every one of these WILL run out, camp, refill and run out
    // again, over and over. That is the worst honest case the new law can be
    // put to, and it is the case the old water-only bite never charged at all.
    constexpr int kWalkers = 100;
    std::vector<entt::entity> walkers;
    walkers.reserve(kWalkers);
    for (int i = 0; i < kWalkers; ++i) {
        const float y = float(i % 100) + 8.0f;
        entt::entity e = make_walker(w, 4.0f, y, 120.0f, y, /*maxSp*/20);
        // Each one hauls right across the map on its own line.
        w.reg.replace<ecs::MacroSpawnId>(e, std::uint32_t(100 + i));
        walkers.push_back(e);
    }

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 77u);
    for (int think = 0; think < 600; ++think) {
        MacroWorld mw{.gs = &gs, .world = &w, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle*/false);
    }

    int alive = 0, bled = 0, moved = 0;
    for (entt::entity e : walkers) {
        if (!w.reg.all_of<ecs::Dead>(e)
            && w.reg.get<ecs::Health>(e).hp > 0.0f) ++alive;
        if (w.reg.get<ecs::Health>(e).hp < 100.0f) ++bled;
        if (w.reg.get<ecs::Position>(e).x != 4.0f) ++moved;
    }
    CHECK(alive == kWalkers,
          "a season of honest marching kills nobody: the bite is a cost, "
          "not a cull");
    CHECK(moved == kWalkers,
          "negative control: they all actually WALKED — the survival above "
          "is not the survival of a hundred bodies standing still");
    CHECK(bled == kWalkers,
          "and the new law is in force for ALL of them: emptying the bar on "
          "dry land draws blood, which the water-only bite never did");
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
    // DERIVED, never pinned: a fresh bar buys maxSp / (roadBed ×
    // kStaminaPerCell) road cells, and the clock is cells / pace. Under the
    // 2026-08-24 recalibration (base 1 SP × weight, 8 cells/h) that is ~110
    // cells over ~13¾ h — the road stretches a walker past the open-country
    // day, which is why roads are worth building. The bands are ±10% so the
    // owner can retune the data without touching this file.
    const float expectCells =
        110.0f / (feature_bed_weight(FT_Road) * kStaminaPerCell);
    const float expectHours = expectCells / kMacroWalkCellsPerHour;
    CHECK(npc.state == std::uint8_t(NPCState::Resting),
          "the road march ends in a camp, not in infinity");
    CHECK(cells > expectCells * 0.9f && cells < expectCells * 1.1f,
          "a fresh bar buys maxSp/(bed x kStaminaPerCell) road cells");
    CHECK(hours > expectHours * 0.9f && hours < expectHours * 1.1f,
          "and the march clock is cells over the derived pace");
}

} // namespace

int main() {
    test_greedy_walks_around_a_wet_cell();
    test_forced_ford_pays_the_water_price();
    test_ocean_drowns_the_lord_and_settles_his_squad();
    test_land_exhaustion_makes_camp_without_blood();
    test_a_map_of_marchers_survives_the_new_law();
    test_road_bar_lasts_a_days_march();
    return sm::test::report("squad_travel_test");
}
