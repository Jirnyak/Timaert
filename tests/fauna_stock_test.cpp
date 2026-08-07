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
    CHECK(gs.faunaOverrides.empty(),
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
    CHECK(gs.faunaOverrides.size() == 1, "the hunt scars exactly one cell");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(0, 3), +1);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(0, 3)) == cap - 1,
          "one head regrown");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(0, 3), +999);
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(0, 3)) == cap,
          "regrowth caps at the cell's own baseline - never beyond");
    CHECK(gs.faunaOverrides.empty(),
          "a cell back at baseline erases its override - the map self-cleans");
}

void test_no_context_fails_closed() {
    GameState gs;
    MacroWorld w{&gs, nullptr, nullptr, nullptr};
    CHECK(macro_stock_read(w, MacroStock::FaunaCount, cell_key(1, 1)) == 0,
          "no terrain wired = nothing stands here");
    macro_stock_apply(w, MacroStock::FaunaCount, cell_key(1, 1), -3);
    CHECK(gs.faunaOverrides.empty(), "and nothing moves");
    CHECK(fauna_cell_capacity_at(nullptr, nullptr, nullptr, 0, 0) == 0,
          "null context capacity is zero, not a crash");
}

} // namespace

int main() {
    test_untouched_cell_reads_capacity();
    test_hunt_thins_and_return_does_not_resurrect();
    test_regrow_self_cleans_at_baseline();
    test_no_context_fails_closed();
    return sm::test::report("fauna_stock_test");
}
