// The world's mineral deposits (macro/deposit_layer.h, W2a). Pinned:
//   · CONTEXT LAW — clay only on river-adjacent lowland, iron/stone only in
//     the mountains, nothing in the water;
//   · determinism — one seed, one geology;
//   · the ONE mutation door records overrides, refuses to invent geology on
//     a non-deposit cell, clamps at zero, and a dry vein stays a VISIBLE
//     cell (the iron-discovery rule needs the fact);
//   · load path — a virgin derivation plus the overrides equals the saved
//     world.
#include "check.h"

#include "macro/deposit_layer.h"

#include <cstdint>

namespace {

using namespace sm;

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

    CHECK_OR_RETURN(!layer.cells.empty(), "the little world holds deposits");

    int clay = 0, iron = 0, stone = 0;
    bool contextHolds = true;
    for (const auto& [idx, cell] : layer.cells) {
        const int x = int(idx % std::uint32_t(td.width));
        const int y = int(idx / std::uint32_t(td.width));
        const bool mountain = td.height_at(x, y) / 255.0f >= 0.75f;
        const bool water = td.is_water(x, y, std::uint8_t(seaLevel * 255.0f));
        contextHolds = contextHolds && !water;
        switch (cell.kind) {
            case DepositKind::Clay: {
                ++clay;
                bool nearRiver = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xi = (x + dx + td.width) % td.width;
                        const int yi = (y + dy + td.height) % td.height;
                        nearRiver = nearRiver
                            || td.riverData[yi * td.width + xi] == 255;
                    }
                contextHolds = contextHolds && nearRiver && !mountain;
                break;
            }
            case DepositKind::Iron:  ++iron;  contextHolds = contextHolds && mountain; break;
            case DepositKind::Stone: ++stone; contextHolds = contextHolds && mountain; break;
        }
        contextHolds = contextHolds && cell.remaining > 0;
    }
    CHECK(contextHolds, "clay hugs rivers, minerals hug mountains, water holds nothing");
    CHECK(stone > 0, "the mountain band yields stone");
    // Iron is 1-in-256 of ~1024 mountain cells and clay 1-in-64 of ~96
    // river-adjacent cells — the law is context, not census, so no count
    // pins beyond stone; the kinds must merely partition the cells.
    CHECK(clay + iron + stone == int(layer.cells.size()),
          "every deposit cell is exactly one of the three kinds");

    // One seed, one geology.
    const DepositLayer again = build_deposit_layer(td, 777u, seaLevel);
    CHECK(again.cells.size() == layer.cells.size(),
          "the same seed derives the same geology");
    // A different seed shuffles the sites.
    const DepositLayer other = build_deposit_layer(td, 778u, seaLevel);
    bool identical = other.cells.size() == layer.cells.size();
    if (identical) {
        for (const auto& [idx, cell] : layer.cells) {
            identical = identical && other.cells.count(idx) != 0;
        }
    }
    CHECK(!identical, "a different seed is a different geology");
}

void test_the_mutation_door_and_the_load_path() {
    const TerrainData td = make_world();
    DepositLayer layer = build_deposit_layer(td, 777u, 0.4f);
    CHECK_OR_RETURN(!layer.cells.empty(), "fixture holds deposits");

    const auto first = layer.cells.begin();
    const int x = int(first->first % std::uint32_t(layer.width));
    const int y = int(first->first / std::uint32_t(layer.width));

    DepositOverrides overrides;
    CHECK(set_deposit_remaining(layer, overrides, x, y, 5),
          "the door mutates a real deposit");
    CHECK(layer.at(x, y) != nullptr && layer.at(x, y)->remaining == 5,
          "the layer shows the drained vein");
    CHECK(overrides.size() == 1, "the mutation left its override");

    // Draining below zero clamps; the DRY vein keeps its cell.
    CHECK(set_deposit_remaining(layer, overrides, x, y, -3),
          "over-draining clamps, not refuses");
    CHECK(layer.at(x, y) != nullptr && layer.at(x, y)->remaining == 0,
          "a dry vein stays a visible cell at zero");

    // Mining cannot invent geology: a plain grass cell (x=32,y=8 is neither
    // river-adjacent nor mountain in the fixture... it may still be a clay
    // roll; use a WATER cell, which never hosts a deposit).
    CHECK(!set_deposit_remaining(layer, overrides, 1, 8, 100),
          "a non-deposit cell refuses the door");

    // The load path: virgin derivation + overrides == the saved world.
    DepositLayer loaded = build_deposit_layer(td, 777u, 0.4f);
    apply_deposit_overrides(loaded, overrides);
    CHECK(loaded.at(x, y) != nullptr && loaded.at(x, y)->remaining == 0,
          "the drained vein survives a load");
    CHECK(loaded.cells.size() == layer.cells.size(),
          "overrides drain veins, never add or remove cells");
}

void test_iron_discovery() {
    const TerrainData td = make_world();
    DepositLayer layer = build_deposit_layer(td, 777u, 0.4f);

    // An untouched world never strikes anything.
    CHECK(iron_depletion(layer) == 0.0f
              && iron_discovery_chance_per_day(0.0f) == 0.0f,
          "full veins = zero discovery chance");

    // Mine the world's iron OUT through the door.
    DepositOverrides overrides;
    int ironBefore = 0;
    for (const auto& [idx, cell] : layer.cells) {
        if (cell.kind != DepositKind::Iron) continue;
        ++ironBefore;
        const int x = int(idx % std::uint32_t(layer.width));
        const int y = int(idx / std::uint32_t(layer.width));
        CHECK(set_deposit_remaining(layer, overrides, x, y, 0),
              "the vein drains through the one door");
    }
    CHECK_OR_RETURN(ironBefore > 0, "the fixture holds iron to exhaust");
    CHECK(iron_depletion(layer) == 1.0f,
          "a mined-out world reads fully depleted");
    CHECK(iron_discovery_chance_per_day(1.0f) > 0.0f,
          "an empty world prospects hopefully");

    // The strike: a stone quarry turns out to hold iron.
    CHECK_OR_RETURN(discover_iron_vein(layer, overrides, 5u),
                    "the strike lands on a stone cell");
    int ironFresh = 0;
    for (const auto& [idx, cell] : layer.cells) {
        if (cell.kind == DepositKind::Iron && cell.remaining > 0) ++ironFresh;
    }
    CHECK(ironFresh == 1, "exactly one fresh vein opened");
    CHECK(iron_depletion(layer) < 1.0f, "the strike relieves the depletion");

    // The discovered vein SURVIVES a load: virgin derivation + overrides.
    DepositLayer loaded = build_deposit_layer(td, 777u, 0.4f);
    apply_deposit_overrides(loaded, overrides);
    int loadedFresh = 0;
    for (const auto& [idx, cell] : loaded.cells) {
        if (cell.kind == DepositKind::Iron && cell.remaining > 0) ++loadedFresh;
    }
    CHECK(loadedFresh == 1, "the discovered vein is reborn on load");
    CHECK(loaded.cells.size() == layer.cells.size(),
          "discovery converts a cell, it does not invent geology");
}

} // namespace

int main() {
    test_deposits_obey_the_world();
    test_the_mutation_door_and_the_load_path();
    test_iron_discovery();
    return sm::test::report("deposit_layer_test");
}
