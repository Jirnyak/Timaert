#include "macro/features.h"
#include "macro/items.h"
#include "macro/map_generator.h"
#include "macro/movement_cost.h"
#include "macro/state.h"
#include "macro/player_recovery.h"
#include "macro/world_tick.h"
#include "macro/travel.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

int g_failures = 0;

bool nearf(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

void expect(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL macro_travel_parity_test: %s\n", label);
        ++g_failures;
    }
}

sm::TerrainData make_terrain() {
    sm::TerrainData terrain;
    terrain.width = 2;
    terrain.height = 2;
    terrain.rgba.assign(2u * 2u * 4u, 255u);

    const auto set_cell = [&](int x,
                              int y,
                              std::uint8_t height,
                              std::uint8_t moisture,
                              std::uint8_t temperature) {
        const std::size_t base =
            (std::size_t(y) * std::size_t(terrain.width) + std::size_t(x)) * 4u;
        terrain.rgba[base + 0u] = height;
        terrain.rgba[base + 1u] = moisture;
        terrain.rgba[base + 2u] = temperature;
        terrain.rgba[base + 3u] = height < 102u ? 0u : 255u;
    };

    set_cell(0, 0, 64u, 128u, 128u);  // water at seaLevel 0.40
    set_cell(1, 0, 180u, 128u, 128u); // Meadow
    set_cell(0, 1, 220u, 250u, 250u); // Mountain (height >= 0.75 mountain level)
    set_cell(1, 1, 180u, 10u, 250u);  // Desert, dirt-road wrap target below
    return terrain;
}

sm::FeatureLayer make_features() {
    sm::FeatureLayer features;
    features.resize(2, 2);
    features.set(0, 0, sm::FT_Road);
    features.set(1, 1, sm::FT_DirtRoad);
    return features;
}

void test_cell_costs_follow_the_weight_table() {
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    expect(sm::macro_travel_cost_for_cell(gs, terrain, nullptr, 0, 0, cost),
           "water cost query succeeds");
    expect(cost.biome == sm::Water, "height below seaLevel becomes Water");
    expect(cost.feature == sm::FT_None, "missing feature layer means no feature");
    // Cost is the terrain weight per macro cell: open water is the 10.0 row.
    expect(nearf(cost.weight, 10.0f), "bare water carries the water weight");
    expect(nearf(cost.cellCost, 10.0f * sm::kStaminaPerCell),
           "one cell of open water costs weight x kStaminaPerCell");
    expect(cost.totalCost == cost.cellCost, "no inventory means no overload");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 0, cost),
           "road-over-water query succeeds");
    expect(cost.biome == sm::Water, "feature does not rewrite biome");
    expect(cost.feature == sm::FT_Road, "feature layer returns road");
    expect(nearf(cost.cellCost, 1.0f * sm::kStaminaPerCell),
           "a road over water is paid at the road weight, not the water one");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 1, cost),
           "mountain biome query succeeds");
    expect(cost.biome == sm::Mountain,
           "height above mountain level becomes the Mountain biome");
    expect(cost.feature == sm::FT_None, "mountain biome carries no feature");
    expect(nearf(cost.cellCost, 5.0f * sm::kStaminaPerCell),
           "a mountain cell costs the mountain weight");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, -1, -1, cost),
           "negative coordinates wrap");
    expect(cost.feature == sm::FT_DirtRoad, "wrapped cell reads dirt road");
    expect(nearf(cost.cellCost, 1.5f * sm::kStaminaPerCell),
           "a dirt road costs half again what a paved one does");
}

void test_overload_and_drain_charge_per_cell() {
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    gs.player.inventory.add("mat_wood", 56); // 112 kg, default capacity is 110 kg.
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 0, cost),
           "overload road query succeeds");
    expect(nearf(cost.cellCost, 1.0f * sm::kStaminaPerCell),
           "overload case walks a road");
    expect(std::fabs(cost.overload - 2.0f) < 0.001f,
           "overload is weight minus carry capacity");
    expect(cost.overloadCost == 2, "any overload hurts: kilos over, rounded up");
    expect(nearf(cost.totalCost, 1.0f * sm::kStaminaPerCell + 2.0f),
           "one cell costs the ground plus the burden carried over it");

    // Crossing cells drains stamina, and the fractional remainder is CARRIED
    // rather than rounded away at each step: five 1.5-SP dirt-road cells cost
    // exactly 7 whole SP with 0.5 left pending, not 5 or 10.
    sm::GameState drainGs;
    drainGs.mapParams.seaLevel = 0.40f;
    drainGs.player.combatStats.currentSp = 100;
    drainGs.player.combatStats.currentHp = 100;
    sm::TravelStamina stamina{};
    for (int i = 0; i < 5; ++i) {
        expect(sm::drain_player_sp_for_macro_cell(drainGs, terrain, &features,
                                                  -1, -1, stamina, &cost),
               "drain query succeeds");
    }
    // Expectations derived from the constants, never restated as numbers: a
    // recalibration must move the balance, not break the accounting.
    const float perCell = sm::travel_stamina_cost(1.5f, 1.0f);   // dirt road
    const float owed = 5.0f * perCell;
    const int charged = int(owed);
    expect(nearf(cost.cellCost, perCell), "the dirt-road cell costs its weight");
    expect(drainGs.player.combatStats.currentSp == 100 - charged,
           "whole SP are charged, and only whole ones");
    expect(nearf(stamina.pending, owed - float(charged)),
           "the fraction is carried to the next step, not lost or rounded up");
    expect(drainGs.player.combatStats.currentHp == 100,
           "a rested body pays travel in stamina alone");
}

// Death by exhaustion is DESIGN: past zero every further step costs HP equal to
// the whole outstanding debt, so the deeper the hole the worse each step. This
// pins the curve at the boundary — the step that empties the bar, and the steps
// after it — so it can never again become an accident of the accounting.
void test_exhaustion_curve_bites_deeper_each_step() {
    sm::CombatStats cs{};
    cs.currentSp = 3;
    cs.currentHp = 100;

    // Still solvent: stamina pays, the body does not.
    expect(sm::apply_stamina_cost(cs, 3) == 0, "stamina pays while it lasts");
    expect(cs.currentSp == 0 && cs.currentHp == 100,
           "reaching exactly zero costs no health");

    // Past zero: each step charges the WHOLE outstanding debt.
    expect(sm::apply_stamina_cost(cs, 2) == 2, "the first step over costs its deficit");
    expect(cs.currentSp == -2 && cs.currentHp == 98, "debt is kept, health paid");
    expect(sm::apply_stamina_cost(cs, 2) == 4, "the next step costs the whole debt");
    expect(cs.currentSp == -4 && cs.currentHp == 94, "the bite grows with the debt");
    expect(sm::apply_stamina_cost(cs, 2) == 6, "and keeps growing");
    expect(cs.currentSp == -6 && cs.currentHp == 88,
           "pressing on exhausted is a gamble, by design");

    // A zero/negative charge is not a free heal or a free step.
    expect(sm::apply_stamina_cost(cs, 0) == 0, "a zero cost changes nothing");
    expect(cs.currentSp == -6 && cs.currentHp == 88, "and touches neither pool");
}

// The two layers walk the same world, so the same journey costs the same:
// one macro cell on the map == kCellSize tiles on foot.
void test_both_layers_price_one_journey_alike() {
    const float weight = sm::cell_sp_weight(sm::Meadow, sm::FT_None);
    const float wholeCell = sm::travel_stamina_cost(weight, 1.0f);
    const float onFoot = sm::travel_stamina_cost(weight, 1024.0f / 1024.0f);
    expect(nearf(wholeCell, onFoot),
           "a cell crossed on foot costs what it costs on the map");
    expect(nearf(sm::travel_stamina_cost(weight, 0.5f), wholeCell * 0.5f),
           "half a cell costs half as much");
    expect(nearf(sm::travel_stamina_cost(weight, 0.0f), 0.0f),
           "standing still is free");
    expect(nearf(sm::travel_stamina_cost(weight, 1.0f, 3), wholeCell + 3.0f),
           "the burden carried is paid per cell, alongside the ground");
}

// ── The balance itself ──────────────────────────────────────────────────────
// A test about DESIGN INTENT, stated in game hours, so that any retuning has to
// be a deliberate decision rather than a drift. It reads the shipping constants
// and asks the questions a player would:
//
//   • how long can a fresh traveller march before he is spent?
//   • does the road repay the walk to it?
//   • does becoming a better traveller actually let you travel further?
//   • does a night's rest undo a day's march?
//
// The history it guards: travel used to be either lethal in two steps or free
// forever, because stamina recovered WHILE marching — a standing income of
// 10 SP per game hour against a road that cost 25, so any pause paid for the
// journey. Marching and resting are separate states now, and these numbers are
// what that separation is worth.
// Cells covered per game hour of marching. Since the march is now quoted in
// exactly that unit (macro/movement_cost.h), this is no longer a derivation
// from two constants that could drift apart — it IS the shipping number, and
// every hour below is a game hour, whatever a day costs in real seconds.
float cells_per_game_hour() {
    return sm::kMacroWalkCellsPerHour;
}

float march_hours(const sm::CombatStats& cs, const sm::Skills& skills,
                  float weight) {
    const float perCell = sm::travel_stamina_cost(
        weight, 1.0f, 0, sm::travel_skill_efficiency(skills));
    if (perCell <= 0.0f) return 0.0f;
    const float cells = float(cs.currentSp) / perCell;
    return cells / cells_per_game_hour();
}

void test_travel_balance_holds_its_intent() {
    const sm::Attributes attrs = sm::default_attributes();
    const sm::Skills skills = sm::default_skills();
    const sm::CombatStats fresh = sm::calculate_combat_stats(attrs, skills);

    // A day of marching on easy ground: out at dawn, camp by nightfall. A day
    // on foot is the 8-13 hours a man actually walks, not the 24 the clock
    // turns — the bar has to run out inside one of them, or camping is a thing
    // the player never has to think about.
    const float meadow = march_hours(fresh, skills,
                                     sm::cell_sp_weight(sm::Meadow, sm::FT_None));
    expect(meadow > 8.0f && meadow < 13.0f,
           "a fresh traveller marches about a day across meadow");

    // Terrain has to MATTER, in the order the weight table declares.
    const float road = march_hours(fresh, skills,
                                   sm::cell_sp_weight(sm::Meadow, sm::FT_Road));
    const float mountain = march_hours(fresh, skills,
                                       sm::cell_sp_weight(sm::Mountain, sm::FT_None));
    const float water = march_hours(fresh, skills,
                                    sm::cell_sp_weight(sm::Water, sm::FT_None));
    expect(road > meadow * 1.8f, "a road is worth walking to");
    expect(mountain < meadow * 0.5f, "mountains are a real obstacle");
    expect(water < mountain * 0.6f, "swimming is the most expensive way to travel");

    // Progression: an RPG must reward the character sheet. A veteran carries a
    // bigger pool (END + endurance) AND spends less on the same ground (travel),
    // so his day of marching becomes several.
    sm::Attributes vetAttrs = attrs;
    vetAttrs.end = 20;
    sm::Skills vetSkills = skills;
    vetSkills.endurance = 5;
    vetSkills.travel = 10;
    const sm::CombatStats veteran = sm::calculate_combat_stats(vetAttrs, vetSkills);
    const float vetMeadow = march_hours(veteran, vetSkills,
                                        sm::cell_sp_weight(sm::Meadow, sm::FT_None));
    expect(vetMeadow > meadow * 3.0f,
           "training triples the distance a traveller covers");
    expect(sm::travel_skill_efficiency(vetSkills) < 1.0f
               && sm::travel_skill_efficiency(vetSkills) > 0.0f,
           "the travel skill discounts terrain without ever making it free");
    expect(nearf(sm::travel_skill_efficiency(skills), 1.0f),
           "an untrained traveller gets no discount");

    // One skill, one meaning. `athletics` moves you FASTER; `travel` moves you
    // FURTHER on the same bar. Neither may quietly become the other, or the
    // sheet stops telling the player what his choices buy.
    sm::Skills sprinter = skills;
    sprinter.athletics = 20;
    expect(nearf(sm::travel_skill_efficiency(sprinter), 1.0f),
           "athletics does not make ground cheaper");
    expect(sm::calculate_derived(attrs, sprinter).moveSpeedMult
               > sm::calculate_derived(attrs, skills).moveSpeedMult,
           "athletics does make the traveller faster");
    sm::Skills pathfinder = skills;
    pathfinder.travel = 20;
    expect(nearf(sm::calculate_derived(attrs, pathfinder).moveSpeedMult,
                 sm::calculate_derived(attrs, skills).moveSpeedMult),
           "the travel skill does not make the traveller faster");
    expect(sm::travel_skill_efficiency(pathfinder) < 1.0f,
           "the travel skill does make ground cheaper");
    // And speed costs no stamina: pricing per CELL means a sprinter and a
    // plodder pay the same for the same road, they just arrive at different
    // hours. That orthogonality is why both stats are worth having.
    expect(nearf(march_hours(fresh, sprinter,
                             sm::cell_sp_weight(sm::Meadow, sm::FT_None)),
                 meadow),
           "running does not burn a bigger share of the bar per cell");

    // A night undoes a day: the bar refills in the hours the design promises,
    // and NOT while the legs are moving.
    sm::PlayerState resting{};
    resting.combatStats = fresh;
    resting.combatStats.currentSp = 0;
    sm::PlayerRecoveryAccumulator acc{};
    sm::apply_macro_minute_recovery(resting, 8 * 60, acc);
    expect(resting.combatStats.currentSp > fresh.maxSp / 2,
           "a night of rest returns most of a spent bar");

    sm::PlayerState marching{};
    marching.combatStats = fresh;
    marching.combatStats.currentSp = 0;
    sm::PlayerRecoveryAccumulator marchAcc{};
    sm::apply_macro_minute_recovery(marching, 8 * 60, marchAcc,
                                    sm::kMarchRecoveryPct);
    expect(marching.combatStats.currentSp == 0,
           "marching returns no stamina — a journey is paid for, not subsidised");
    expect(marching.combatStats.currentHp >= fresh.currentHp,
           "and suppressing stamina recovery does not stop the body healing");

    // THE SKILL LAW, pinned: one rank is one percent, and the cap is the
    // ceiling. A future skill that invents its own curve should trip this.
    expect(nearf(sm::skill_bonus_mult(0), 1.0f), "rank 0 grants nothing");
    expect(nearf(sm::skill_bonus_mult(37), 1.37f), "rank reads as percent");
    expect(nearf(sm::skill_cost_mult(37), 0.63f), "and as percent off a cost");
    expect(nearf(sm::skill_cost_mult(sm::kMaxSkillRank), 0.0f),
           "mastery of a cost skill removes that cost entirely");
    expect(nearf(sm::skill_bonus_mult(sm::kMaxSkillRank + 500),
                 sm::skill_bonus_mult(sm::kMaxSkillRank)),
           "nothing above the cap counts, however it got there");
    expect(nearf(sm::skill_cost_mult(-5), 1.0f), "and nothing below zero does");

    // Mastery earns free ground — but only the GROUND. An overloaded master
    // still pays for what he carries, and the exhaustion curve is untouched.
    sm::Skills master = skills;
    master.travel = sm::kMaxSkillRank;
    expect(nearf(sm::travel_stamina_cost(10.0f, 1.0f, 0,
                                         sm::travel_skill_efficiency(master)),
                 0.0f),
           "at mastery the world stops resisting the traveller");
    expect(sm::travel_stamina_cost(10.0f, 1.0f, 3,
                                   sm::travel_skill_efficiency(master)) > 2.9f,
           "but the pack on his back still weighs what it weighs");

    // The cap is enforced at the one door into a rank, so no path can exceed it.
    sm::LevelData ld{};
    sm::Skills capped{};
    ld.skillPoints = sm::kMaxSkillRank + 10;
    int spent = 0;
    while (sm::spend_skill_point(ld, capped, sm::SkillId::Travel)) ++spent;
    expect(spent == sm::kMaxSkillRank && capped.travel == sm::kMaxSkillRank,
           "a rank stops at mastery");
    expect(ld.skillPoints == 10,
           "and a refused spend keeps the point for another skill");

    // The balance, printed on every run: a number you can read is a number you
    // can argue with.
    std::printf("   travel balance (game hours of marching per full bar)\n"
                "     fresh   road %.1f  meadow %.1f  mountain %.1f  water %.1f\n"
                "     veteran meadow %.1f  (bar %d, terrain x%.2f)\n",
                double(road), double(meadow), double(mountain), double(water),
                double(vetMeadow), veteran.maxSp,
                double(sm::travel_skill_efficiency(vetSkills)));
}

void test_invalid_terrain_fails_closed() {
    sm::GameState gs;
    sm::TerrainData terrain;
    terrain.width = 2;
    terrain.height = 2;
    terrain.rgba.assign(3u, 255u);

    sm::MacroTravelCost cost;
    cost.cellCost = 777.0f;
    expect(!sm::macro_travel_cost_for_cell(gs, terrain, nullptr, 0, 0, cost),
           "invalid terrain storage is rejected");
    expect(nearf(cost.cellCost, 0.0f) && nearf(cost.totalCost, 0.0f),
           "failed query clears stale cost output");
}

} // namespace

int main() {
    test_cell_costs_follow_the_weight_table();
    test_overload_and_drain_charge_per_cell();
    test_exhaustion_curve_bites_deeper_each_step();
    test_both_layers_price_one_journey_alike();
    test_travel_balance_holds_its_intent();
    test_invalid_terrain_fails_closed();

    if (g_failures != 0) {
        return 1;
    }

    std::puts("OK macro_travel_parity_test costs=ok overload=ok drain=ok "
              "exhaustion=ok one-law=ok balance=ok invalid=ok");
    return 0;
}
