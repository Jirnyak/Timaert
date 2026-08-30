// The standing wheat is an honest macro stock (Field Inc F3). Pinned:
//   · an untouched cell reads a fertility-derived ESTIMATE (derived, no
//     storage) — and reading is not spending: ask twice, get the same;
//   · the estimate follows the owner's law — capacity is a function of the
//     cell's OWN fertility (the moisture channel), so a wetter cell reads
//     more, settlements included, no special case;
//   · the sickle thins the cell THROUGH the one receipt path
//     (settle_macro_debt), a return does NOT resurrect the wheat, the read
//     floors at zero, and the scar is capped — a runaway writer cannot wind
//     a cell into a millennium of regrowth;
//   · regrowth self-cleans: a cell healed back to whole erases its override
//     — the persisted set is "cells the sickle has scarred", never the
//     world;
//   · no context (unwired terrain) = nothing stands here and nothing moves —
//     fail closed, like every malformed receipt.
#include "check.h"

#include "macro/macro_stock.h"
#include "macro/map_generator.h"
#include "macro/state.h"

namespace {

using namespace sm;

// One 4x4 world, land everywhere, per-cell moisture set by the tests.
TerrainData flat_terrain(std::uint8_t moisture) {
    TerrainData t;
    t.width = 4;
    t.height = 4;
    t.rgba.assign(std::size_t(4 * 4) * 4u, 128);
    for (std::size_t i = 0; i < t.rgba.size(); i += 4) {
        t.rgba[i + 1] = moisture;   // G = the fertility channel
        t.rgba[i + 3] = 255;        // A = land
    }
    return t;
}

MacroStockKey cell_key(int x, int y) {
    return MacroStockKey{-1, std::int16_t(x), std::int16_t(y)};
}

void test_untouched_cell_reads_fertility_estimate() {
    GameState gs;
    const TerrainData terrain = flat_terrain(160);
    MacroWorld w{&gs, nullptr, nullptr, &terrain};

    const int est = macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1));
    CHECK(est > 0, "a fertile cell carries standing wheat - the estimate is real");
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1)) == est,
          "reading is not spending");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].empty(),
          "the derived estimate writes NOTHING - overrides are scars only");
}

void test_estimate_follows_fertility() {
    GameState gs;
    TerrainData terrain = flat_terrain(128);
    // Cell (1,1) is wetter than cell (2,2) — the wetter one must read more.
    terrain.rgba[(std::size_t(1) * 4 + 1) * 4 + 1] = 220;
    terrain.rgba[(std::size_t(2) * 4 + 2) * 4 + 1] = 100;
    MacroWorld w{&gs, nullptr, nullptr, &terrain};

    const int wet = macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1));
    const int dry = macro_stock_read(w, MacroStock::CropCount, cell_key(2, 2));
    CHECK(wet > dry,
          "capacity is the cell's own fertility - the wetter cell reads more");
    CHECK(dry > 0, "field-worthy ground still reads a positive estimate");
}

void test_harvest_thins_and_return_does_not_resurrect() {
    GameState gs;
    const TerrainData terrain = flat_terrain(160);
    MacroWorld w{&gs, nullptr, nullptr, &terrain};
    const int est = macro_stock_read(w, MacroStock::CropCount, cell_key(2, 2));
    CHECK(est >= 2, "the fixture needs at least two stands to cut");
    const int neighbourBefore =
        macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1));

    // Two stands cut, each through the SAME receipt path a kill settles.
    const ecs::MacroDebt receipt{
        std::uint8_t(MacroStock::CropCount), -1, 2, 2, 1, -1};
    settle_macro_debt(w, receipt, -1);
    settle_macro_debt(w, receipt, -1);
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(2, 2)) == est - 2,
          "each settled receipt is one stand gone");
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(2, 2)) == est - 2,
          "leave and RETURN: the harvested stays harvested");
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1)) ==
              neighbourBefore,
          "the scar is per cell - the neighbour still stands whole");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].size() == 1,
          "one scarred cell = one override entry");

    // Over-harvest: the read floors at zero, the scar stays bounded.
    macro_stock_apply(w, MacroStock::CropCount, cell_key(2, 2), -1000000);
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(2, 2)) == 0,
          "a cell can be emptied but never owes wheat");
    const auto it = gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].begin();
    CHECK(it != gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].end() && int(it->second) <= 4096,
          "the scar is capped - no millennium of regrowth from one writer");
}

void test_regrow_self_cleans_when_whole() {
    GameState gs;
    const TerrainData terrain = flat_terrain(160);
    MacroWorld w{&gs, nullptr, nullptr, &terrain};

    // Reap the parcel BARE, so the regrowth law is measured against the
    // field's own potential (the same number the code reads), never a
    // pinned literal.
    const int full = macro_stock_read(w, MacroStock::CropCount, cell_key(3, 1));
    CHECK(full > 4, "fixture: the parcel holds a measurable potential");
    macro_stock_apply(w, MacroStock::CropCount, cell_key(3, 1), -full);
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].size() == 1, "the cut leaves a scar");

    // The ONE growth law visits a cell once per epoch, on the cell's own
    // slice day (idx % kGrowthEpochDays) — the slice discipline is part of
    // the law, so a non-due day moves nothing.
    const std::uint32_t idx = 1u * 4u + 3u;   // cell (3,1) of the 4×4 world
    const int dueDay = int(idx % std::uint32_t(kGrowthEpochDays));
    resource_fields_daily_growth(w, dueDay + 1);
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(3, 1)) == 0,
          "a day that is not this cell's slice grows nothing here");
    // The field turns over its own POTENTIAL, a quarter per due visit
    // (macro_stock.cpp wheat_growth_at, owner track 2026-08-30) — a bare
    // parcel regrows whole in a year of seasons.
    const int perVisit = std::max(1, full / 4);
    resource_fields_daily_growth(w, dueDay + kGrowthEpochDays);
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(3, 1))
              == std::min(full, perVisit),
          "a due visit regrows a quarter of the parcel's own potential");
    for (int e = 2; e <= 5; ++e) {
        resource_fields_daily_growth(w, dueDay + e * kGrowthEpochDays);
    }
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(3, 1)) == full,
          "a year of seasons turns the bare parcel back over whole");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].empty(),
          "a cell healed back to whole erases its own override");
    resource_fields_daily_growth(w, dueDay + 6 * kGrowthEpochDays);
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].empty(),
          "growing an unscarred wheat field is a no-op, not a creation engine");
}

void test_no_terrain_fails_closed() {
    GameState gs;
    MacroWorld w{&gs, nullptr, nullptr, nullptr};
    CHECK(macro_stock_read(w, MacroStock::CropCount, cell_key(1, 1)) == 0,
          "no terrain wired: nothing stands here");
    macro_stock_apply(w, MacroStock::CropCount, cell_key(1, 1), -3);
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Wheat)].empty(),
          "no terrain wired: nothing moves, no scar appears");
}

} // namespace

int main() {
    test_untouched_cell_reads_fertility_estimate();
    test_estimate_follows_fertility();
    test_harvest_thins_and_return_does_not_resurrect();
    test_regrow_self_cleans_when_whole();
    test_no_terrain_fails_closed();
    return sm::test::report("crop_stock_test");
}
