#include "check.h"

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

// The march prices the player's CARRIED weight, and his bag is an ordinary
// NpcInventory on his squad entity now — so a headless fixture owns one and
// hands it to the law.
sm::Inventory bag{};

bool nearf(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
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
    bag.clear();
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    CHECK(sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, nullptr, 0, 0, cost),
           "water cost query succeeds");
    CHECK(cost.biome == sm::Water, "height below seaLevel becomes Water");
    CHECK(cost.feature == sm::FT_None, "missing feature layer means no feature");
    // Cost is the terrain weight per macro cell: open water is the 10.0 row.
    CHECK(nearf(cost.weight, 10.0f), "bare water carries the water weight");
    CHECK(nearf(cost.cellCost, 10.0f * sm::kStaminaPerCell),
           "one cell of open water costs weight x kStaminaPerCell");
    CHECK(cost.totalCost == cost.cellCost, "no inventory means no overload");

    CHECK(sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, &features, 0, 0, cost),
           "road-over-water query succeeds");
    CHECK(cost.biome == sm::Water, "feature does not rewrite biome");
    CHECK(cost.feature == sm::FT_Road, "feature layer returns road");
    CHECK(nearf(cost.cellCost, 1.0f * sm::kStaminaPerCell),
           "a road over water is paid at the road weight, not the water one");

    CHECK(sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, &features, 0, 1, cost),
           "mountain biome query succeeds");
    CHECK(cost.biome == sm::Mountain,
           "height above mountain level becomes the Mountain biome");
    CHECK(cost.feature == sm::FT_None, "mountain biome carries no feature");
    CHECK(nearf(cost.cellCost, 5.0f * sm::kStaminaPerCell),
           "a mountain cell costs the mountain weight");

    CHECK(sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, &features, -1, -1, cost),
           "negative coordinates wrap");
    CHECK(cost.feature == sm::FT_DirtRoad, "wrapped cell reads dirt road");
    CHECK(nearf(cost.cellCost, 1.5f * sm::kStaminaPerCell),
           "a dirt road costs half again what a paved one does");
}

void test_overload_and_drain_charge_per_cell() {
    bag.clear();
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    bag.add("wood", 56);   // 112 kg, default capacity is 110 kg.
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    CHECK(sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, &features, 0, 0, cost),
           "overload road query succeeds");
    CHECK(nearf(cost.cellCost, 1.0f * sm::kStaminaPerCell),
           "overload case walks a road");
    CHECK(std::fabs(cost.overload - 2.0f) < 0.001f,
           "overload is weight minus carry capacity");
    CHECK(cost.overloadCost == 2, "any overload hurts: kilos over, rounded up");
    CHECK(nearf(cost.totalCost, 1.0f * sm::kStaminaPerCell + 2.0f),
           "one cell costs the ground plus the burden carried over it");

    // Crossing cells drains stamina, and the fractional remainder is CARRIED
    // rather than rounded away at each step: five 1.5-SP dirt-road cells cost
    // exactly 7 whole SP with 0.5 left pending, not 5 or 10.
    // The drain half walks a DIFFERENT walker — its own state, and therefore
    // its own (empty) pack. Sharing the overloaded bag above would fold the
    // burden surcharge into an accounting check about whole SP.
    sm::Inventory drainBag{};
    sm::GameState drainGs;
    drainGs.mapParams.seaLevel = 0.40f;
    drainGs.player.combatStats.currentSp = 100;
    drainGs.player.combatStats.currentHp = 100;
    // ONE signed carry, the shape every body on the map keeps: a march drives
    // it DOWN, so the fraction still owed reads as a negative remainder.
    float spCarry = 0.0f;
    for (int i = 0; i < 5; ++i) {
        CHECK(sm::drain_player_sp_for_macro_cell(drainGs, drainGs.player.sheet, &drainBag, terrain,
                                                  &features,
                                                  -1, -1, spCarry, &cost),
               "drain query succeeds");
    }
    // Expectations derived from the constants, never restated as numbers: a
    // recalibration must move the balance, not break the accounting.
    const float perCell = sm::travel_stamina_cost(1.5f, 1.0f);   // dirt road
    const float owed = 5.0f * perCell;
    const int charged = int(owed);
    CHECK(nearf(cost.cellCost, perCell), "the dirt-road cell costs its weight");
    CHECK(drainGs.player.combatStats.currentSp == 100 - charged,
           "whole SP are charged, and only whole ones");
    CHECK(nearf(spCarry, -(owed - float(charged))),
           "the fraction is carried to the next step, not lost or rounded up");
    CHECK(drainGs.player.combatStats.currentHp == 100,
           "a rested body pays travel in stamina alone");
}

// Death by exhaustion is DESIGN: past zero every further step costs HP equal to
// the whole outstanding debt, so the deeper the hole the worse each step. This
// pins the curve at the boundary — the step that empties the bar, and the steps
// after it — so it can never again become an accident of the accounting.
void test_exhaustion_curve_bites_deeper_each_step() {
    bag.clear();
    sm::CombatStats cs{};
    cs.currentSp = 3;
    cs.currentHp = 100;

    // Still solvent: stamina pays, the body does not.
    CHECK(sm::apply_stamina_cost(cs, 3) == 0, "stamina pays while it lasts");
    CHECK(cs.currentSp == 0 && cs.currentHp == 100,
           "reaching exactly zero costs no health");

    // Past zero: each step charges the WHOLE outstanding debt.
    CHECK(sm::apply_stamina_cost(cs, 2) == 2, "the first step over costs its deficit");
    CHECK(cs.currentSp == -2 && cs.currentHp == 98, "debt is kept, health paid");
    CHECK(sm::apply_stamina_cost(cs, 2) == 4, "the next step costs the whole debt");
    CHECK(cs.currentSp == -4 && cs.currentHp == 94, "the bite grows with the debt");
    CHECK(sm::apply_stamina_cost(cs, 2) == 6, "and keeps growing");
    CHECK(cs.currentSp == -6 && cs.currentHp == 88,
           "pressing on exhausted is a gamble, by design");

    // A zero/negative charge is not a free heal or a free step.
    CHECK(sm::apply_stamina_cost(cs, 0) == 0, "a zero cost changes nothing");
    CHECK(cs.currentSp == -6 && cs.currentHp == 88, "and touches neither pool");
}

// The two layers walk the same world, so the same journey costs the same:
// one macro cell on the map == kCellSize tiles on foot.
void test_both_layers_price_one_journey_alike() {
    bag.clear();
    const float weight = sm::cell_sp_weight(sm::Meadow, sm::FT_None);
    const float wholeCell = sm::travel_stamina_cost(weight, 1.0f);
    const float onFoot = sm::travel_stamina_cost(weight, 1024.0f / 1024.0f);
    CHECK(nearf(wholeCell, onFoot),
           "a cell crossed on foot costs what it costs on the map");
    CHECK(nearf(sm::travel_stamina_cost(weight, 0.5f), wholeCell * 0.5f),
           "half a cell costs half as much");
    CHECK(nearf(sm::travel_stamina_cost(weight, 0.0f), 0.0f),
           "standing still is free");
    CHECK(nearf(sm::travel_stamina_cost(weight, 1.0f, 3), wholeCell + 3.0f),
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
    // Heavy ground also slows the legs (terrain_speed_mult, Session 21), so
    // the HOURS a bar buys shrink by √weight, not by weight: part of the
    // terrain's price arrives as time instead of stamina.
    return cells / (cells_per_game_hour() * sm::terrain_speed_mult(weight));
}

void test_travel_balance_holds_its_intent() {
    bag.clear();
    const sm::Attributes attrs = sm::default_attributes();
    const sm::Skills skills = sm::default_skills();
    const sm::CombatStats fresh = sm::calculate_combat_stats(attrs, skills);

    // THE anchor (owner, 2026-08-24 recalibration): a fresh traveller burns
    // his whole bar in ROUGHLY A DAY'S WALKING of open country — out at dawn,
    // spent with the light, camp. The bar
    // has to run out inside a day's walking, or camping is a thing the player
    // never has to think about (at the old 0.2/cell it never did: 17+ hours
    // of road, and any pause repaid the walk).
    const float meadow = march_hours(fresh, skills,
                                      sm::cell_sp_weight(sm::Meadow, sm::FT_None));
    CHECK(meadow > 8.0f && meadow < 12.0f,
           "a day's march over open country spends the fresh bar");
    // The road stretches the same bar further than open country by exactly
    // the law's own ratio — weight halves, pace gains √2, endurance gains
    // weight × 1/√weight = √2 (that is WHY roads are worth building).
    const float road = march_hours(fresh, skills,
                                   sm::cell_sp_weight(sm::Meadow, sm::FT_Road));
    CHECK(road > meadow * 1.35f && road < meadow * 1.5f,
           "the road stretches the bar by the law's own sqrt-2");

    // Terrain has to MATTER, in the order the weight table declares. The gaps
    // are √weight, not weight: heavy ground pays part of its price in HOURS
    // (terrain_speed_mult) and the rest in stamina.
    const float mountain = march_hours(fresh, skills,
                                       sm::cell_sp_weight(sm::Mountain, sm::FT_None));
    const float water = march_hours(fresh, skills,
                                    sm::cell_sp_weight(sm::Water, sm::FT_None));
    CHECK(road > meadow * 1.3f, "a road is worth walking to");
    CHECK(mountain < meadow * 0.7f, "mountains are a real obstacle");
    CHECK(water < mountain * 0.8f, "swimming is the most expensive way to travel");

    // Progression: an RPG must reward the character sheet. A veteran carries a
    // bigger pool (END and WILL by half each — the canon eight, 2026-09-03)
    // AND spends less on the same ground (travel), so his day of marching
    // becomes several.
    sm::Attributes vetAttrs = attrs;
    vetAttrs[sm::AttributeId::End] = 20;
    vetAttrs[sm::AttributeId::Wil] = 20;
    sm::Skills vetSkills = skills;
    vetSkills[sm::SkillId::Travel] = 10;
    const sm::CombatStats veteran = sm::calculate_combat_stats(vetAttrs, vetSkills);
    const float vetMeadow = march_hours(veteran, vetSkills,
                                        sm::cell_sp_weight(sm::Meadow, sm::FT_None));
    CHECK(vetMeadow > meadow * 3.0f,
           "training triples the distance a traveller covers");

    // The Session 21 lever split, pinned. The bar belongs to the ATTRIBUTES
    // alone (END and WILL by half each); `marathon` buys the RATE of recovery
    // and never the bar. Full rest is therefore the same 8 hours for every
    // sheet in the world (regen is a PERCENT of the bar), and only marathon
    // shortens it.
    sm::Skills marathoner = skills;
    marathoner[sm::SkillId::Marathon] = 20;
    CHECK(sm::calculate_combat_stats(attrs, marathoner).maxSp == fresh.maxSp,
           "marathon does not grow the bar");
    CHECK(sm::calculate_combat_stats(attrs, marathoner).spRegen
               > fresh.spRegen,
           "marathon does speed the recovery");
    const float freshRestH = float(fresh.maxSp) / fresh.spRegen;
    const float vetRestH = float(veteran.maxSp) / veteran.spRegen;
    CHECK(nearf(freshRestH, 1.0f / sm::kRestRegenPctPerHour),
           "a full rest is the designed 8 hours");
    CHECK(nearf(vetRestH, freshRestH),
           "a bigger bar rests no longer — regen is a percent of it");
    CHECK(sm::travel_skill_efficiency(vetSkills) < 1.0f
               && sm::travel_skill_efficiency(vetSkills) > 0.0f,
           "the travel skill discounts terrain without ever making it free");
    CHECK(nearf(sm::travel_skill_efficiency(skills), 1.0f),
           "an untrained traveller gets no discount");

    // One skill, one meaning. `athletics` moves you FASTER; `travel` moves you
    // FURTHER on the same bar. Neither may quietly become the other, or the
    // sheet stops telling the player what his choices buy.
    sm::Skills sprinter = skills;
    sprinter[sm::SkillId::Athletics] = 20;
    CHECK(nearf(sm::travel_skill_efficiency(sprinter), 1.0f),
           "athletics does not make ground cheaper");
    CHECK(sm::calculate_derived(attrs, sprinter).moveSpeedMult
               > sm::calculate_derived(attrs, skills).moveSpeedMult,
           "athletics does make the traveller faster");
    sm::Skills pathfinder = skills;
    pathfinder[sm::SkillId::Travel] = 20;
    CHECK(nearf(sm::calculate_derived(attrs, pathfinder).moveSpeedMult,
                 sm::calculate_derived(attrs, skills).moveSpeedMult),
           "the travel skill does not make the traveller faster");
    CHECK(sm::travel_skill_efficiency(pathfinder) < 1.0f,
           "the travel skill does make ground cheaper");
    // And speed costs no stamina: pricing per CELL means a sprinter and a
    // plodder pay the same for the same road, they just arrive at different
    // hours. That orthogonality is why both stats are worth having.
    CHECK(nearf(march_hours(fresh, sprinter,
                             sm::cell_sp_weight(sm::Meadow, sm::FT_None)),
                 meadow),
           "running does not burn a bigger share of the bar per cell");

    // A night undoes a day: the bar refills in the hours the design promises,
    // and NOT while the legs are moving.
    sm::PlayerState resting{};
    resting.combatStats = fresh;
    resting.combatStats.currentSp = 0;
    sm::PlayerRecoveryAccumulator acc{};
    float restCarry = 0.0f;
    sm::apply_minute_recovery(resting, 8 * 60, acc, restCarry);
    CHECK(resting.combatStats.currentSp >= fresh.maxSp - 1,
           "eight hours of rest refill the whole bar (the 1/8-per-hour law)");

    sm::PlayerState marching{};
    marching.combatStats = fresh;
    marching.combatStats.currentSp = 0;
    sm::PlayerRecoveryAccumulator marchAcc{};
    float marchCarry = 0.0f;
    sm::apply_minute_recovery(marching, 8 * 60, marchAcc, marchCarry,
                                    sm::kMarchRecoveryPct);
    CHECK(marching.combatStats.currentSp == 0,
           "marching returns no stamina — a journey is paid for, not subsidised");
    CHECK(marching.combatStats.currentHp >= fresh.currentHp,
           "and suppressing stamina recovery does not stop the body healing");

    // THE SKILL LAW, pinned through the one door (skill_mult_of): a rank is
    // the row's percent, and the cap is the ceiling. The generic helpers that
    // once answered without knowing their row died in the 2026-09-03 sweep.
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, 0), 1.0f),
           "rank 0 grants nothing");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, 37), 1.37f),
           "rank reads as the row's percent");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, 37), 0.63f),
           "and as percent off a cost");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, sm::kMaxSkillRank), 0.0f),
           "mastery of a cost skill removes that cost entirely");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, sm::kMaxSkillRank + 500),
                 sm::skill_mult_of(sm::SkillId::Marathon, sm::kMaxSkillRank)),
           "nothing above the cap counts, however it got there");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, -5), 1.0f),
           "and nothing below zero does");

    // Mastery earns free ground — but only the GROUND. An overloaded master
    // still pays for what he carries, and the exhaustion curve is untouched.
    sm::Skills master = skills;
    master[sm::SkillId::Travel] = sm::kMaxSkillRank;
    CHECK(nearf(sm::travel_stamina_cost(10.0f, 1.0f, 0,
                                         sm::travel_skill_efficiency(master)),
                 0.0f),
           "at mastery the world stops resisting the traveller");
    CHECK(sm::travel_stamina_cost(10.0f, 1.0f, 3,
                                   sm::travel_skill_efficiency(master)) > 2.9f,
           "but the pack on his back still weighs what it weighs");

    // The cap is enforced at the one door into a rank, so no path can exceed
    // it. Learning comes first (THE learn law): rank 0 refuses a spend, so
    // mastery is 1 (learned) + 99 spends.
    sm::LevelData ld{};
    sm::Skills capped{};
    ld.skillPoints = sm::kMaxSkillRank + 10;
    CHECK(!sm::spend_skill_point(ld, capped, sm::SkillId::Travel),
           "an unknown skill refuses the point: learn first");
    CHECK(sm::learn_skill(capped, sm::SkillId::Travel),
           "the world teaches, and rank 1 is the knowing");
    int spent = 0;
    while (sm::spend_skill_point(ld, capped, sm::SkillId::Travel)) ++spent;
    CHECK(spent == sm::kMaxSkillRank - 1
              && capped.of(sm::SkillId::Travel) == sm::kMaxSkillRank,
           "a rank stops at mastery");
    CHECK(ld.skillPoints == 11,
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
    bag.clear();
    sm::GameState gs;
    sm::TerrainData terrain;
    terrain.width = 2;
    terrain.height = 2;
    terrain.rgba.assign(3u, 255u);

    sm::MacroTravelCost cost;
    cost.cellCost = 777.0f;
    CHECK(!sm::macro_travel_cost_for_cell(gs.player.sheet, &bag, terrain, nullptr, 0, 0, cost),
           "invalid terrain storage is rejected");
    CHECK(nearf(cost.cellCost, 0.0f) && nearf(cost.totalCost, 0.0f),
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
    return sm::test::report("macro_travel_parity_test");
}
