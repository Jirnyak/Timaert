// The world's mineral deposits (macro/deposit_layer.h, W2a; carrier rows
// since R2). Pinned:
//   · CONTEXT LAW — clay only on river-adjacent lowland, iron/stone only in
//     the mountains, nothing in the water;
//   · determinism — one seed, one geology;
//   · the quantity door refuses to invent geology on a non-deposit cell,
//     clamps at zero, and a dry vein stays a VISIBLE cell (the
//     iron-discovery rule needs the fact);
//   · discovery ADDS iron INTO a stone cell — the quarry survives (owner:
//     у каждого ресурса своё поле, никто не исчезает);
//   · load path (v37) — restoring the saved cells reproduces the mutated
//     world, drained and discovered alike.
#include "check.h"

#include "macro/deposit_layer.h"

#include <cstdint>

namespace {

using namespace sm;

int total_cells(const DepositLayer& layer) {
    int n = 0;
    for (const auto& m : layer.cells) n += int(m.size());
    return n;
}

// A little world: sea on the left edge, a river column at x=8, a mountain
// band at y>=48, plain grassland elsewhere.
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
            if (y >= 48) height = 220;                   // mountains (>=0.75*255)
            td.rgba[s + 0] = height;
            td.rgba[s + 1] = 128;
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

    bool contextHolds = true;
    for (int k = 0; k < kDepositKindCount; ++k) {
        for (const auto& [idx, remaining] : layer.cells[k]) {
            const int x = int(idx % std::uint32_t(td.width));
            const int y = int(idx / std::uint32_t(td.width));
            const bool mountain = td.height_at(x, y) / 255.0f >= 0.75f;
            const bool water =
                td.is_water(x, y, std::uint8_t(seaLevel * 255.0f));
            contextHolds = contextHolds && !water && remaining > 0;
            if (DepositKind(k) == DepositKind::Clay) {
                bool nearRiver = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xi = (x + dx + td.width) % td.width;
                        const int yi = (y + dy + td.height) % td.height;
                        nearRiver = nearRiver
                            || td.riverData[yi * td.width + xi] == 255;
                    }
                contextHolds = contextHolds && nearRiver && !mountain;
            } else {
                contextHolds = contextHolds && mountain;
            }
        }
    }
    CHECK(contextHolds,
          "clay hugs rivers, minerals hug mountains, water holds nothing");
    CHECK(!layer.cells[std::size_t(DepositKind::Stone)].empty(),
          "the mountain band yields stone");
    // At DERIVATION the kinds partition the cells (iron wins where both
    // roll); only DISCOVERY may later stack a second kind onto a cell.
    bool disjoint = true;
    for (const auto& [idx, rem] : layer.cells[std::size_t(DepositKind::Iron)]) {
        (void)rem;
        disjoint = disjoint
            && !layer.cells[std::size_t(DepositKind::Stone)].count(idx)
            && !layer.cells[std::size_t(DepositKind::Clay)].count(idx);
    }
    CHECK(disjoint, "virgin geology holds one kind per cell");

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
    CHECK_OR_RETURN(!layer.cells[std::size_t(DepositKind::Stone)].empty(),
                    "fixture holds stone");

    const auto first = layer.cells[std::size_t(DepositKind::Stone)].begin();
    const int x = int(first->first % std::uint32_t(layer.width));
    const int y = int(first->first / std::uint32_t(layer.width));

    const std::uint32_t rev0 = layer.revision;
    CHECK(set_deposit_remaining(layer, DepositKind::Stone, x, y, 5),
          "the door mutates a real deposit");
    const std::int32_t* rem = layer.remaining_at(DepositKind::Stone, x, y);
    CHECK(rem != nullptr && *rem == 5, "the layer shows the drained vein");
    CHECK(layer.revision > rev0, "the mutation moved the revision");

    // Draining to (or below) zero ANNIHILATES: a worked-out vein is a vein
    // that no longer exists (owner, 2026-08-28), and the counter keeps the
    // scarcity baseline the dead cell used to carry.
    const std::uint32_t drained0 =
        layer.drainedCells[std::size_t(DepositKind::Stone)];
    CHECK(set_deposit_remaining(layer, DepositKind::Stone, x, y, -3),
          "over-draining annihilates, not refuses");
    rem = layer.remaining_at(DepositKind::Stone, x, y);
    CHECK(rem == nullptr, "a worked-out vein leaves the map entirely");
    CHECK(layer.drainedCells[std::size_t(DepositKind::Stone)] == drained0 + 1,
          "the annihilation is counted - the baseline survives the cell");

    // Mining cannot invent geology: a water cell never hosts a deposit, and
    // a stone write cannot touch another kind's map either.
    CHECK(!set_deposit_remaining(layer, DepositKind::Stone, 1, 8, 100),
          "a non-deposit cell refuses the quantity door");
    CHECK(!set_deposit_remaining(layer, DepositKind::Iron, x, y, 100)
              || layer.cells[std::size_t(DepositKind::Iron)].count(
                     layer.wrap_index(x, y)),
          "a kind the cell does not hold refuses the door");

    // The load path (v37/v55): the save carries the live cells AND the
    // annihilation counters whole; restoring onto a virgin derivation
    // reproduces the mutated world — the dead vein stays dead.
    DepositLayer loaded = build_deposit_layer(td, 777u, 0.4f);
    restore_deposit_cells(loaded, layer);
    rem = loaded.remaining_at(DepositKind::Stone, x, y);
    CHECK(rem == nullptr, "the annihilated vein stays gone through a load");
    CHECK(loaded.drainedCells[std::size_t(DepositKind::Stone)]
              == layer.drainedCells[std::size_t(DepositKind::Stone)],
          "the annihilation counter survives the load");
    CHECK(total_cells(loaded) == total_cells(layer),
          "the restore reproduces every live cell, adds none");
}

void test_iron_discovery() {
    const TerrainData td = make_world();
    DepositLayer layer = build_deposit_layer(td, 777u, 0.4f);

    // An untouched world never strikes anything.
    CHECK(iron_depletion(layer) == 0.0f
              && iron_discovery_chance_per_day(0.0f) == 0.0f,
          "full veins = zero discovery chance");

    // Mine the world's iron OUT through the door.
    int ironBefore = 0;
    {
        // Collect first: the door mutates the map we would be walking.
        std::vector<std::uint32_t> veins;
        for (const auto& [idx, rem] :
             layer.cells[std::size_t(DepositKind::Iron)]) {
            (void)rem;
            veins.push_back(idx);
        }
        for (const std::uint32_t idx : veins) {
            ++ironBefore;
            const int x = int(idx % std::uint32_t(layer.width));
            const int y = int(idx / std::uint32_t(layer.width));
            CHECK(set_deposit_remaining(layer, DepositKind::Iron, x, y, 0),
                  "the vein drains through the one door");
        }
    }
    CHECK_OR_RETURN(ironBefore > 0, "the fixture holds iron to exhaust");
    // Annihilation emptied the MAP — the counter alone must carry the
    // baseline, or a worked-out world would read virgin and never prospect.
    CHECK(layer.cells[std::size_t(DepositKind::Iron)].empty(),
          "every worked-out vein left the map");
    CHECK(layer.drainedCells[std::size_t(DepositKind::Iron)]
              == std::uint32_t(ironBefore),
          "the counter remembers every annihilated vein");
    CHECK(iron_depletion(layer) == 1.0f,
          "a mined-out world reads fully depleted");
    CHECK(iron_discovery_chance_per_day(1.0f) > 0.0f,
          "an empty world prospects hopefully");

    // The strike: a stone quarry turns out to ALSO hold iron.
    const int stoneBefore =
        int(layer.cells[std::size_t(DepositKind::Stone)].size());
    CHECK_OR_RETURN(discover_iron_vein(layer, 5u),
                    "the strike lands on a stone cell");
    int ironFresh = 0;
    std::uint32_t freshIdx = 0;
    for (const auto& [idx, rem] :
         layer.cells[std::size_t(DepositKind::Iron)]) {
        if (rem > 0) { ++ironFresh; freshIdx = idx; }
    }
    CHECK(ironFresh == 1, "exactly one fresh vein opened");
    CHECK(iron_depletion(layer) < 1.0f, "the strike relieves the depletion");
    // NOTHING VANISHES: the host quarry keeps its stone cell — the vein was
    // found IN the mountain, and the old kind-swap that deleted it is dead.
    CHECK(layer.cells[std::size_t(DepositKind::Stone)].count(freshIdx) == 1,
          "the host quarry survives the discovery");
    CHECK(int(layer.cells[std::size_t(DepositKind::Stone)].size())
              == stoneBefore,
          "discovery adds iron, it deletes no stone");

    // The discovered vein SURVIVES a load (v37: cells ride the save whole).
    DepositLayer loaded = build_deposit_layer(td, 777u, 0.4f);
    restore_deposit_cells(loaded, layer);
    const std::int32_t* rem = loaded.remaining_at(
        DepositKind::Iron, int(freshIdx % std::uint32_t(loaded.width)),
        int(freshIdx / std::uint32_t(loaded.width)));
    CHECK(rem != nullptr && *rem > 0, "the discovered vein is reborn on load");
    CHECK(total_cells(loaded) == total_cells(layer),
          "the restore carries the discovery, invents nothing further");
}

} // namespace

int main() {
    test_deposits_obey_the_world();
    test_the_quantity_door_and_the_load_path();
    test_iron_discovery();
    return sm::test::report("deposit_layer_test");
}
