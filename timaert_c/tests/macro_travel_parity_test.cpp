#include "macro/features.h"
#include "macro/items.h"
#include "macro/map_generator.h"
#include "macro/movement_cost.h"
#include "macro/state.h"
#include "macro/travel.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

int g_failures = 0;

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

void test_cell_costs_match_ts_rules() {
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    expect(sm::macro_travel_cost_for_cell(gs, terrain, nullptr, 0, 0, cost),
           "water cost query succeeds");
    expect(cost.biome == sm::Water, "height below seaLevel becomes Water");
    expect(cost.feature == sm::FT_None, "missing feature layer means no feature");
    expect(cost.cellCost == sm::kMacroBaseSP * 10, "bare water costs 100 SP");
    expect(cost.totalCost == cost.cellCost, "no inventory means no overload");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 0, cost),
           "road-over-water query succeeds");
    expect(cost.biome == sm::Water, "feature does not rewrite biome");
    expect(cost.feature == sm::FT_Road, "feature layer returns road");
    expect(cost.cellCost == sm::kMacroBaseSP, "road overrides water weight");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 1, cost),
           "mountain biome query succeeds");
    expect(cost.biome == sm::Mountain,
           "height above mountain level becomes the Mountain biome");
    expect(cost.feature == sm::FT_None, "mountain biome carries no feature");
    expect(cost.cellCost == sm::kMacroBaseSP * 5, "mountain biome costs 50 SP");

    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, -1, -1, cost),
           "negative coordinates wrap");
    expect(cost.feature == sm::FT_DirtRoad, "wrapped cell reads dirt road");
    expect(cost.cellCost == 15, "dirt road costs 15 SP");
}

void test_overload_and_drain_match_ts_shape() {
    sm::GameState gs;
    gs.mapParams.seaLevel = 0.40f;
    gs.player.inventory.add("mat_wood", 56); // 112 kg, default capacity is 110 kg.
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features();

    sm::MacroTravelCost cost;
    expect(sm::macro_travel_cost_for_cell(gs, terrain, &features, 0, 0, cost),
           "overload road query succeeds");
    expect(cost.cellCost == sm::kMacroBaseSP, "overload test uses road base cost");
    expect(std::fabs(cost.overload - 2.0f) < 0.001f,
           "overload is weight minus carry capacity");
    expect(cost.overloadCost == 2, "integer native overload cost keeps TS penalty");
    expect(cost.totalCost == 12, "total cost is cell plus overload");

    sm::GameState drainGs;
    drainGs.mapParams.seaLevel = 0.40f;
    drainGs.player.combatStats.currentSp = 5;
    drainGs.player.combatStats.currentHp = 30;
    expect(sm::drain_player_sp_for_macro_cell(drainGs, terrain, &features, 0, 0, &cost),
           "drain query succeeds");
    expect(cost.totalCost == sm::kMacroBaseSP, "drain uses road cost");
    expect(drainGs.player.combatStats.currentSp == -5,
           "SP may go negative after macro travel");
    expect(drainGs.player.combatStats.currentHp == 25,
           "negative SP remainder is applied to HP");
}

void test_invalid_terrain_fails_closed() {
    sm::GameState gs;
    sm::TerrainData terrain;
    terrain.width = 2;
    terrain.height = 2;
    terrain.rgba.assign(3u, 255u);

    sm::MacroTravelCost cost;
    cost.cellCost = 777;
    expect(!sm::macro_travel_cost_for_cell(gs, terrain, nullptr, 0, 0, cost),
           "invalid terrain storage is rejected");
    expect(cost.cellCost == 0 && cost.totalCost == 0,
           "failed query clears stale cost output");
}

} // namespace

int main() {
    test_cell_costs_match_ts_rules();
    test_overload_and_drain_match_ts_shape();
    test_invalid_terrain_fails_closed();

    if (g_failures != 0) {
        return 1;
    }

    std::puts("OK macro_travel_parity_test costs=ok overload=ok drain=ok invalid=ok");
    return 0;
}
