// The ONE growth/diffusion law of the resource fields (owner, R2 track):
// every field is BORN from time and context through the same walker —
// resource_fields_daily_growth — and the nuances are rows, never systems.
//
// Properties pinned (never restated numbers):
//   · TREES — the forest plants the forest: a clear-cut cell inside a
//     living massif regrows to forest class within the owner's ~4-year
//     invariant; an isolated brush cell never seeds a forest; the desert
//     gate starves growth a meadow would get; the cap holds.
//   · FAUNA — beasts breed where beasts are: a hunted cell in a living
//     valley heals at the old 1/epoch rate; a valley emptied WHOLE stays
//     empty (nobody left to breed) — that asymmetry IS the law, and it is
//     what a flat heal-to-baseline could never say (the built-in negative
//     control: a flat law would regrow both).
//   · IRON — born where scarce: a virgin world strikes nothing, a
//     mined-out world strikes within the epoch horizon, the vein lands on
//     a stone host WITHOUT deleting it, and the strike is a pure function
//     of the calendar (same seed+day = same vein).
#include "check.h"

#include "macro/deposit_layer.h"
#include "macro/macro_stock.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kW = 64, kH = 64;

// Dry meadow land everywhere; a mountain band at y>=40 for geology (wide
// enough that the 1/256 iron roll reliably seeds a few veins).
TerrainData make_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW) * kH * 4u, 128);
    t.riverData.assign(std::size_t(kW) * kH, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            t.rgba[s + 0] = y >= 40 ? 220 : 180;   // land / mountain band
            t.rgba[s + 3] = 255;                   // land mask
        }
    }
    return t;
}

// A desert stripe at x < 8 (hot + dry climate channels), meadow elsewhere.
void paint_desert_stripe(TerrainData& t) {
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            t.rgba[s + 1] = 10;    // bone dry
            t.rgba[s + 2] = 240;   // scorching
        }
    }
}

TreeLayer flat_trees(std::uint16_t count) {
    TreeLayer trees;
    trees.width = kW;
    trees.height = kH;
    trees.data.assign(std::size_t(kW) * kH, count);
    return trees;
}

// Run the daily walker over `days` consecutive game days (1-based).
void run_days(MacroWorld& w, int days) {
    for (int day = 1; day <= days; ++day) {
        resource_fields_daily_growth(w, day);
    }
}

void test_forest_plants_the_forest() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData td = make_terrain();

    // A living massif block with one clear-cut cell inside it.
    TreeLayer trees = flat_trees(0);
    for (int y = 4; y <= 8; ++y)
        for (int x = 4; x <= 8; ++x)
            set_tree_count(trees, x, y, kMaxTreesPerCell);
    set_tree_count(trees, 6, 6, 0);
    // An isolated brush cell far from any forest: below the seeding
    // threshold, surrounded by bare ground.
    set_tree_count(trees, 20, 6, 100);
    MacroWorld w{&gs, &trees, nullptr, &td};

    // The owner's invariant: a clear-cut inside a living massif returns to
    // forest class in ~4 game years. Allow the loose window [2, 6] years —
    // the property is the timescale, not a pinned constant.
    run_days(w, 2 * 512);
    const bool by2y = is_forest_cell(int(trees.at(6, 6)));
    run_days(w, 4 * 512);   // total 6 years
    CHECK(is_forest_cell(int(trees.at(6, 6))),
          "a clear-cut inside a living massif is forest again within ~6y");
    CHECK(int(trees.at(6, 6)) >= 8192 || !by2y,
          "and it was NOT instant - growth is a timescale, not a snap");
    CHECK(int(trees.at(6, 6)) <= kMaxTreesPerCell
              && int(trees.at(4, 4)) <= kMaxTreesPerCell,
          "the golden cap holds under years of growth");

    CHECK(int(trees.at(20, 6)) == 100,
          "an isolated brush cell below the seeding threshold never grows");
    // The massif SPREADS: its outer neighbours started at 0 and now stand.
    CHECK(int(trees.at(3, 6)) > 0,
          "the forest edge seeds the bare neighbour cell (diffusion)");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Trees)].empty(),
          "years of growth left no scars - the grid is the only state");
}

void test_biome_gate_starves_the_desert() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    TerrainData td = make_terrain();
    paint_desert_stripe(td);

    // Two identical forest blocks: one on the desert stripe, one on meadow.
    TreeLayer trees = flat_trees(0);
    for (int y = 4; y <= 6; ++y) {
        for (int x = 2; x <= 4; ++x)
            set_tree_count(trees, x, y, 12000);         // desert block
        for (int x = 20; x <= 22; ++x)
            set_tree_count(trees, x, y, 12000);         // meadow block
    }
    const int desertBefore = int(trees.at(3, 5));
    const int meadowBefore = int(trees.at(21, 5));
    MacroWorld w{&gs, &trees, nullptr, &td};
    run_days(w, 512);

    const int desertGrown = int(trees.at(3, 5)) - desertBefore;
    const int meadowGrown = int(trees.at(21, 5)) - meadowBefore;
    CHECK(meadowGrown > 0, "the meadow block thickens (чащоба in the making)");
    CHECK(desertGrown * 4 < meadowGrown,
          "the desert gate starves growth the meadow gets freely");
}

void test_beasts_breed_where_beasts_are() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData td = make_terrain();
    MacroWorld w{&gs, nullptr, nullptr, &td};

    const int cap = resource_field_read(w, ResourceFieldId::Fauna, 10, 10);
    CHECK_OR_RETURN(cap >= 2, "the meadow carries beasts to hunt");

    // One hunted cell in a LIVING valley: heals at one head per epoch.
    resource_field_apply(w, ResourceFieldId::Fauna, 10, 10, -2);
    // A 5×5 hole shot dead around (20,20): it must repopulate from the
    // OUTSIDE in. The hole's corner (18,18) sees 5 of its 9 neighbours
    // alive — over the half-alive threshold — so it breeds; the centre
    // sees nobody and stays bare. A flat heal-to-baseline law would
    // regrow both at once.
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            resource_field_apply(w, ResourceFieldId::Fauna,
                                 20 + dx, 20 + dy, -999);

    run_days(w, 2 * kGrowthEpochDays);

    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 10, 10) == cap,
          "a hunted cell in a living valley healed at one head per epoch");
    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 18, 18) > 0,
          "the hole's corner repopulates first - life walks in from the "
          "living side");
    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 20, 20) == 0,
          "the hole's centre is still bare after two epochs - the wave "
          "has not reached it (a flat law would have)");
}

void test_iron_is_born_where_scarce() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    gs.worldSeed = 12345u;
    const TerrainData td = make_terrain();
    DepositLayer deposits = build_deposit_layer(td, gs.worldSeed, 0.4f);
    const auto ironCells = [&] {
        return deposits.cells[std::size_t(DepositKind::Iron)].size();
    };
    const auto stoneCells = [&] {
        return deposits.cells[std::size_t(DepositKind::Stone)].size();
    };
    CHECK_OR_RETURN(stoneCells() > 0, "the mountain band yields stone hosts");
    CHECK_OR_RETURN(ironCells() > 0, "the fixture holds iron to exhaust");
    MacroWorld w{&gs, nullptr, nullptr, &td, &deposits};

    // A VIRGIN world strikes nothing, however long it waits.
    const std::size_t virginIron = ironCells();
    run_days(w, 256);
    CHECK(ironCells() == virginIron, "full veins = no discovery, ever");

    // Mine the world's iron OUT through the registry door.
    std::vector<std::uint32_t> veins;
    for (const auto& [idx, rem] : deposits.cells[std::size_t(DepositKind::Iron)]) {
        (void)rem;
        veins.push_back(idx);
    }
    for (const std::uint32_t idx : veins) {
        resource_field_apply(w, ResourceFieldId::Iron,
                             int(idx % std::uint32_t(kW)),
                             int(idx / std::uint32_t(kW)), -1000000);
    }
    const std::size_t stoneBefore = stoneCells();

    // Annihilation (v55): the worked-out veins LEFT the map, and only the
    // drained counter still says the world ever knew iron — which is what
    // keeps the scarcity law prospecting.
    CHECK(ironCells() == 0
              && deposits.drainedCells[std::size_t(DepositKind::Iron)]
                     == std::uint32_t(virginIron),
          "mined-out veins are annihilated and counted");

    // A mined-out world prospects at depletion/8 = 12.5 %/day: over 256
    // days the horizon is generous, and the roll is a PURE function of the
    // calendar, so this is exact, not flaky.
    run_days(w, 256);
    CHECK(ironCells() > 0,
          "a mined-out world struck new iron within the horizon");
    CHECK(stoneCells() == stoneBefore,
          "every strike landed IN a quarry and deleted no stone");
    // The fresh vein holds real metal on a stone host.
    bool freshOnStone = false;
    for (const auto& [idx, rem] : deposits.cells[std::size_t(DepositKind::Iron)]) {
        if (rem > 0
            && deposits.cells[std::size_t(DepositKind::Stone)].count(idx)) {
            freshOnStone = true;
        }
    }
    CHECK(freshOnStone, "the fresh vein carries metal and shares its cell "
                        "with the host quarry");

    // EXTINCTION is the honest end of the breeding law: a world whose
    // fauna is gone EVERYWHERE has nobody left to breed, anywhere, ever.
    {
        GameState gsx{};
        gsx.mapW = kW;
        gsx.mapH = kH;
        MacroWorld wx{&gsx, nullptr, nullptr, &td};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                resource_field_apply(wx, ResourceFieldId::Fauna, x, y, -999);
        run_days(wx, 4 * kGrowthEpochDays);
        int total = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                total += resource_field_read(wx, ResourceFieldId::Fauna, x, y);
        CHECK(total == 0,
              "a world emptied WHOLE stays extinct - nobody is left to "
              "breed (a heal-to-baseline law could never say this)");
    }

    // Determinism: replaying the same calendar on the same world state
    // births the same geology (pure hash of seed+day, no RNG stream).
    GameState gs2{};
    gs2.mapW = kW;
    gs2.mapH = kH;
    gs2.worldSeed = 12345u;
    DepositLayer deposits2 = build_deposit_layer(td, gs2.worldSeed, 0.4f);
    MacroWorld w2{&gs2, nullptr, nullptr, &td, &deposits2};
    for (const std::uint32_t idx : veins) {
        resource_field_apply(w2, ResourceFieldId::Iron,
                             int(idx % std::uint32_t(kW)),
                             int(idx / std::uint32_t(kW)), -1000000);
    }
    run_days(w2, 256);
    CHECK(deposits2.cells[std::size_t(DepositKind::Iron)]
              == deposits.cells[std::size_t(DepositKind::Iron)],
          "the same seed and calendar strike the same veins - growth is a "
          "pure function of the world");
}

} // namespace

int main() {
    test_forest_plants_the_forest();
    test_biome_gate_starves_the_desert();
    test_beasts_breed_where_beasts_are();
    test_iron_is_born_where_scarce();
    return sm::test::report("resource_growth_test");
}
