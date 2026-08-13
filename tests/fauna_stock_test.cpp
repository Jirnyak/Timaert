// The wild headcount is an honest macro stock (Session 16). Pinned:
//   · an untouched cell reads its own spawn-table CAPACITY (derived, no
//     storage) — and reading is not spending: ask twice, get the same;
//   · the hunt thins the cell THROUGH the one receipt path
//     (settle_macro_debt), a return does NOT resurrect the culled, and the
//     count can be emptied but never owes heads;
//   · regrowth self-cleans: a cell written back to its baseline erases its
//     override — the persisted set is "cells play has scarred", never the
//     whole world;
//   · no context (unwired terrain) = nothing stands here and nothing moves —
//     fail closed, like every malformed receipt.
#include "check.h"

#include "macro/fauna.h"
#include "macro/macro_stock.h"
#include "macro/map_generator.h"
#include "macro/state.h"
#include "macro/world_tick.h"

namespace {

using namespace sm;

// One 4x4 all-Meadow world: height 128 (above the 0.40 sea, below the 0.75
// mountain line), temperature/moisture 128 -> the climate matrix centre.
TerrainData meadow_terrain() {
    TerrainData t;
    t.width = 4;
    t.height = 4;
    t.rgba.assign(std::size_t(4 * 4) * 4u, 128);
    for (std::size_t i = 3; i < t.rgba.size(); i += 4) t.rgba[i] = 255;
    return t;
}

MacroStockKey cell_key(int x, int y) {
    return MacroStockKey{-1, std::int16_t(x), std::int16_t(y)};
}

void test_untouched_cell_reads_capacity() {
    GameState gs;
    const TerrainData terrain = meadow_terrain();
    MacroWorld w{&gs, nullptr, nullptr, &terrain};

    const int cap = fauna_cell_capacity_at(&gs, &terrain, nullptr, 1, 1);
    CHECK(cap > 0, "a meadow carries wildlife - the baseline is real");
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(1, 1)) == cap,
          "an untouched cell reads its own table capacity");
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(1, 1)) == cap,
          "reading is not spending");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Fauna)].empty(),
          "the derived baseline writes NOTHING - overrides are scars only");
}

void test_hunt_thins_and_return_does_not_resurrect() {
    GameState gs;
    const TerrainData terrain = meadow_terrain();
    MacroWorld w{&gs, nullptr, nullptr, &terrain};
    const int cap = macro_stock_read(w, MacroStock::FaunaCount, cell_key(2, 2));
    CHECK(cap >= 2, "the fixture needs at least two heads to hunt");

    // Two kills, each through the SAME receipt the death reaper settles.
    const ecs::MacroDebt receipt{
        std::uint8_t(MacroStock::FaunaCount), -1, 2, 2, 1, -1};
    settle_macro_debt(w, receipt, -1);
    settle_macro_debt(w, receipt, -1);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(2, 2)) == cap - 2,
          "each settled receipt is one head gone");
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(2, 2)) == cap - 2,
          "leave and RETURN: the culled stay culled");
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(1, 1)) ==
              fauna_cell_capacity_at(&gs, &terrain, nullptr, 1, 1),
          "the scar is per cell - the neighbour still stands whole");

    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(2, 2), -999);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(2, 2)) == 0,
          "a cell can be emptied but never owes heads");
}

void test_regrow_self_cleans_at_baseline() {
    GameState gs;
    const TerrainData terrain = meadow_terrain();
    MacroWorld w{&gs, nullptr, nullptr, &terrain};
    const int cap = macro_stock_read(w, MacroStock::FaunaCount, cell_key(0, 3));

    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(0, 3), -2);
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Fauna)].size() == 1, "the hunt scars exactly one cell");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(0, 3), +1);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(0, 3)) == cap - 1,
          "one head regrown");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(0, 3), +999);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(0, 3)) == cap,
          "regrowth caps at the cell's own baseline - never beyond");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Fauna)].empty(),
          "a cell back at baseline erases its override - the map self-cleans");
}

void test_regrow_runs_on_game_days() {
    GameState gs;
    const TerrainData terrain = meadow_terrain();
    MacroWorld w{&gs, nullptr, nullptr, &terrain};
    const int cap = macro_stock_read(w, MacroStock::FaunaCount, cell_key(3, 1));
    CHECK(cap >= 3, "the fixture needs three heads to cull");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(3, 1), -3);

    // Two full growth epochs of queued GAME days, processed in one call:
    // the cadence is the calendar's, not the frame's. The cell's slice day
    // occurs exactly twice in 64 consecutive days, and its 3×3 valley is
    // alive (only 3 heads of 9 cells' capacity gone), so the breeding law
    // grants one head per visit.
    const int period = kGrowthEpochDays;
    WorldTickRuntime rt{};
    rt.pendingDailyTicks = 2 * period;
    rt.nextDailyTickDay = 1;
    const int processed = process_world_daily_ticks(
        gs, rt, /*max_daily_ticks*/ 2 * period, &w);
    CHECK(processed == 2 * period, "every queued day was simulated");
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(3, 1))
              == cap - 3 + 2,
          "two epochs passed = exactly two heads bred");

    // Without the macro context the wilds sleep — growth needs its world.
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(3, 1), -1);
    const int before = macro_stock_read(w, MacroStock::FaunaCount, cell_key(3, 1));
    rt.pendingDailyTicks = 2 * period;
    rt.nextDailyTickDay = 1;
    process_world_daily_ticks(gs, rt, 2 * period, nullptr);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(3, 1)) == before,
          "no macro context wired = no growth (fail closed)");
}

void test_no_context_fails_closed() {
    GameState gs;
    MacroWorld w{&gs, nullptr, nullptr, nullptr};
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(1, 1)) == 0,
          "no terrain wired = nothing stands here");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(1, 1), -3);
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Fauna)].empty(), "and nothing moves");
    CHECK(fauna_cell_capacity_at(nullptr, nullptr, nullptr, 0, 0) == 0,
          "null context capacity is zero, not a crash");
}

} // namespace

int main() {
    test_untouched_cell_reads_capacity();
    test_hunt_thins_and_return_does_not_resurrect();
    test_regrow_self_cleans_at_baseline();
    test_regrow_runs_on_game_days();
    test_no_context_fails_closed();
    return sm::test::report("fauna_stock_test");
}
