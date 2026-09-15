// The world's mineral deposits (macro/deposit_layer.h; the FIELD law of
// geology since v72 — owner 2026-08-31: «диффузионная полевая генерация,
// горы дают ВЕСА, не гейт»). Pinned:
//   · the AFFINITY LAW — nothing in the water; metal/stone cells stand on
//     HIGHER ground than the land's average (weight = height⁴), clay on
//     river-wetted lowland (on this fixture the off-river weight cannot
//     crest, so every clay cell is river-adjacent — a pinned consequence);
//   · NESTS — the field law clusters: somewhere a kind holds two adjacent
//     cells (the connectivity the mine's consolidation folds — the hash law
//     this replaced scattered lone veins that clustered with nothing);
//   · determinism — one seed, one geology;
//   · the quantity door refuses to invent geology on a non-deposit cell, and
//     a write down to zero ANNIHILATES the cell (owner, 2026-08-28) while the
//     derived virginUnits baseline keeps what the world was born with;
//   · load path (v37) — restoring the saved cells reproduces the mutated
//     world, the dead vein staying dead.
#include "check.h"

#include "macro/deposit_layer.h"

#include <algorithm>
#include <cstdint>

namespace {

using namespace sm;

int total_cells(const DepositLayer& layer) {
    int n = 0;
    for (const auto& g : layer.cells) n += int(g.liveCells);
    return n;
}

// A little world: sea on the left edge, a river column at x=8 wetting the
// land, a TALL mountain band at y>=48 (the field law concentrates metal
// where the land is HIGH — height⁴ — so the fixture holds honest peaks,
// not foothills), plain grassland elsewhere.
TerrainData make_world() {
    const int w = 64, h = 64;
    TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4u, 0);
    td.riverData.assign(std::size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t s = std::size_t(y * w + x) * 4u;
            std::uint8_t height = 140;                   // land
            if (x < 4) height = 40;                      // sea
            if (y >= 48) height = 250;                   // peaks (0.98)
            td.rgba[s + 0] = height;
            td.rgba[s + 1] = 200;                        // wet enough for clay
            td.rgba[s + 2] = 128;
            td.rgba[s + 3] = height < 102 ? 0 : 255;
            if (x == 8 && y < 48) td.riverData[y * w + x] = 255;
        }
    }
    return td;
}

void test_deposits_obey_the_world() {
    const TerrainData td = make_world();
    const float seaLevel = 0.4f;
    const DepositLayer layer = build_deposit_layer(td, 777u, seaLevel);

    CHECK_OR_RETURN(total_cells(layer) > 0, "the little world holds deposits");

    // The AFFINITY law (weights, never gates): water holds nothing; a
    // mineral cell stands on HIGHER ground than the land's average; clay on
    // this fixture cannot crest off-river (moisture/8), so every clay cell
    // is river-adjacent — the old hard-gate assertion, now a consequence.
    long long landHeightSum = 0, landCells = 0;
    for (int y = 0; y < td.height; ++y)
        for (int x = 0; x < td.width; ++x)
            if (!td.is_water(x, y, std::uint8_t(seaLevel * 255.0f))) {
                landHeightSum += td.height_at(x, y);
                ++landCells;
            }
    const long long landMean = landHeightSum / std::max(1LL, landCells);
    bool affinityHolds = true;
    long long mineralHeightSum = 0, mineralCells = 0;
    for (int k = 0; k < kDepositKindCount; ++k) {
        layer.cells[k].for_each_live(
                [&](std::uint32_t idx, std::int32_t remaining) {
            const int x = int(idx % std::uint32_t(td.width));
            const int y = int(idx / std::uint32_t(td.width));
            const bool water =
                td.is_water(x, y, std::uint8_t(seaLevel * 255.0f));
            affinityHolds = affinityHolds && !water && remaining > 0;
            if (DepositKind(k) == DepositKind::Clay) {
                bool nearRiver = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xi = (x + dx + td.width) % td.width;
                        const int yi = (y + dy + td.height) % td.height;
                        nearRiver = nearRiver
                            || td.riverData[yi * td.width + xi] == 255;
                    }
                affinityHolds = affinityHolds && nearRiver;
            } else {
                mineralHeightSum += td.height_at(x, y);
                ++mineralCells;
            }
        });
    }
    CHECK(affinityHolds,
          "water holds nothing; clay crests only by the river here");
    CHECK_OR_RETURN(mineralCells > 0, "the peaks yield minerals");
    CHECK(mineralHeightSum / mineralCells > landMean,
          "minerals stand on higher ground than the land's average — the "
          "height-weight affinity, never a hard gate");
    CHECK(layer.grid(DepositKind::Stone).liveCells > 0,
          "the mountain band yields stone");
    // NESTS: the field law clusters — somewhere a kind holds two adjacent
    // cells. This is the meat the mine's consolidation folds; the hash law
    // this replaced scattered lone veins and this check is red against it.
    bool nested = false;
    for (int k = 0; k < kDepositKindCount && !nested; ++k) {
        layer.cells[std::size_t(k)].for_each_live(
                [&](std::uint32_t idx, std::int32_t rem) {
            (void)rem;
            if (nested) return;
            const int x = int(idx % std::uint32_t(td.width));
            const int y = int(idx / std::uint32_t(td.width));
            for (int dy = -1; dy <= 1 && !nested; ++dy)
                for (int dx = -1; dx <= 1 && !nested; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (layer.cells[std::size_t(k)].at(x + dx, y + dy) != 0)
                        nested = true;
                }
        });
    }
    CHECK(nested, "the field law grows NESTS — adjacent same-kind veins");

    // One seed, one geology.
    const DepositLayer again = build_deposit_layer(td, 777u, seaLevel);
    bool same = true;
    for (int k = 0; k < kDepositKindCount; ++k)
        same = same && again.cells[k] == layer.cells[k];
    CHECK(same, "the same seed derives the same geology");
    // A different seed shuffles the sites.
    const DepositLayer other = build_deposit_layer(td, 778u, seaLevel);
    bool identical = true;
    for (int k = 0; k < kDepositKindCount; ++k)
        identical = identical && other.cells[k] == layer.cells[k];
    CHECK(!identical, "a different seed is a different geology");
}

void test_the_quantity_door_and_the_load_path() {
    const TerrainData td = make_world();
    DepositLayer layer = build_deposit_layer(td, 777u, 0.4f);
    CHECK_OR_RETURN(layer.grid(DepositKind::Stone).liveCells > 0,
                    "fixture holds stone");

    const ResourceGrid& stone = layer.grid(DepositKind::Stone);
    const std::uint32_t firstIdx = stone.first_live();
    const int x = stone.x_of(firstIdx);
    const int y = stone.y_of(firstIdx);

    const std::uint32_t rev0 = layer.revision;
    CHECK(set_deposit_remaining(layer, DepositKind::Stone, x, y, 5),
          "the door mutates a real deposit");
    CHECK(layer.remaining_at(DepositKind::Stone, x, y) == 5,
          "the layer shows the drained vein");
    CHECK(layer.revision > rev0, "the mutation moved the revision");

    // Draining to (or below) zero ANNIHILATES: a worked-out vein is a vein
    // that no longer exists (owner, 2026-08-28). The scarcity baseline is
    // the DERIVED virginUnits, so the death moves no stored state.
    const std::int64_t virgin0 =
        layer.virginUnits[std::size_t(DepositKind::Stone)];
    CHECK(set_deposit_remaining(layer, DepositKind::Stone, x, y, -3),
          "over-draining annihilates, not refuses");
    CHECK(layer.remaining_at(DepositKind::Stone, x, y) == 0,
          "a worked-out vein leaves the map entirely - in a field, that is 0");
    CHECK(layer.virginUnits[std::size_t(DepositKind::Stone)] == virgin0
              && virgin0 > 0,
          "the born-with baseline is untouched by the death - it is derived, "
          "not kept");

    // Mining cannot invent geology: a water cell never hosts a deposit, and
    // a stone write cannot touch another kind's map either.
    CHECK(!set_deposit_remaining(layer, DepositKind::Stone, 1, 8, 100),
          "a non-deposit cell refuses the quantity door");
    CHECK(!set_deposit_remaining(layer, DepositKind::Iron, x, y, 100)
              || layer.grid(DepositKind::Iron).at(x, y) != 0,
          "a kind the cell does not hold refuses the door");

    // The load path (v37/v55): the save carries the live cells AND the
    // annihilation counters whole; restoring onto a virgin derivation
    // reproduces the mutated world — the dead vein stays dead.
    DepositLayer loaded = build_deposit_layer(td, 777u, 0.4f);
    restore_deposit_cells(loaded, layer);
    CHECK(loaded.remaining_at(DepositKind::Stone, x, y) == 0,
          "the annihilated vein stays gone through a load");
    CHECK(loaded.virginUnits[std::size_t(DepositKind::Stone)] == virgin0,
          "the re-derived layer carries the same born-with baseline");
    CHECK(total_cells(loaded) == total_cells(layer),
          "the restore reproduces every live cell, adds none");
}

} // namespace

int main() {
    test_deposits_obey_the_world();
    test_the_quantity_door_and_the_load_path();
    return sm::test::report("deposit_layer_test");
}
