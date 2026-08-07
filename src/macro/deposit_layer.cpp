#include "macro/deposit_layer.h"

#include "core/rng.h"
#include "macro/biomes.h"

namespace sm {

namespace {

// Densities and base amounts — po2 by house style. Stone is quasi-infinite
// (the owner's "огромный remaining"); iron is FINITE, so a mine can run dry
// and the discovery rule (W2c) has a fact to answer.
constexpr std::uint32_t kClayChanceMask  = 63;    // 1 in 64 river-adjacent cells
constexpr std::uint32_t kStoneChanceMask = 63;    // 1 in 64 mountain cells
constexpr std::uint32_t kIronChanceMask  = 255;   // 1 in 256 mountain cells
constexpr std::int32_t  kClayBase  = 4096;
constexpr std::int32_t  kIronBase  = 2048;
constexpr std::int32_t  kStoneBase = 65536;

constexpr std::uint32_t kClaySalt  = 0xC1A70000u;
constexpr std::uint32_t kIronSalt  = 0x1F0E0000u;
constexpr std::uint32_t kStoneSalt = 0x570E0000u;

bool river_adjacent(const TerrainData& t, int x, int y) {
    if (!t.has_river_storage()) return false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int xi = ((x + dx) % t.width + t.width) % t.width;
            const int yi = ((y + dy) % t.height + t.height) % t.height;
            if (t.riverData[std::size_t(yi) * std::size_t(t.width) + std::size_t(xi)]
                == 255) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

const char* deposit_commodity_id(DepositKind kind) {
    switch (kind) {
        case DepositKind::Clay:  return "clay";
        case DepositKind::Iron:  return "iron";
        case DepositKind::Stone: return "stone";
    }
    return "";
}

DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed, float seaLevel) {
    DepositLayer layer;
    layer.width = terrain.width;
    layer.height = terrain.height;
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return layer;
    }
    const std::uint8_t sea8 = std::uint8_t(seaLevel * 255.0f);
    for (int y = 0; y < terrain.height; ++y) {
        for (int x = 0; x < terrain.width; ++x) {
            if (terrain.is_water(x, y, sea8)) continue;
            const std::uint32_t idx =
                std::uint32_t(y) * std::uint32_t(terrain.width)
                + std::uint32_t(x);
            const float h01 = float(terrain.height_at(x, y)) / 255.0f;
            const bool mountain = h01 >= kMountainBiomeLevel;
            // Mountains hold the minerals; iron is the rare one, and where
            // both would land, the SCARCE kind wins the cell.
            if (mountain) {
                if ((hash3(std::uint32_t(x), std::uint32_t(y),
                           seed ^ kIronSalt) & kIronChanceMask) == 0) {
                    layer.cells.emplace(idx,
                        DepositCell{DepositKind::Iron, kIronBase});
                    continue;
                }
                if ((hash3(std::uint32_t(x), std::uint32_t(y),
                           seed ^ kStoneSalt) & kStoneChanceMask) == 0) {
                    layer.cells.emplace(idx,
                        DepositCell{DepositKind::Stone, kStoneBase});
                }
                continue;
            }
            // Clay is alluvial: lowland cells touching a river.
            if ((hash3(std::uint32_t(x), std::uint32_t(y),
                       seed ^ kClaySalt) & kClayChanceMask) == 0
                && river_adjacent(terrain, x, y)) {
                layer.cells.emplace(idx,
                    DepositCell{DepositKind::Clay, kClayBase});
            }
        }
    }
    return layer;
}

bool set_deposit_remaining(DepositLayer& layer, DepositOverrides& overrides,
                           int x, int y, std::int32_t remaining) {
    if (layer.width <= 0 || layer.height <= 0) return false;
    const std::uint32_t xi =
        std::uint32_t(((x % layer.width) + layer.width) % layer.width);
    const std::uint32_t yi =
        std::uint32_t(((y % layer.height) + layer.height) % layer.height);
    const std::uint32_t idx = yi * std::uint32_t(layer.width) + xi;
    auto it = layer.cells.find(idx);
    if (it == layer.cells.end()) return false;   // mining invents no geology
    const std::int32_t v = remaining < 0 ? 0 : remaining;
    it->second.remaining = v;
    overrides[idx] = v;
    ++layer.revision;
    return true;
}

void apply_deposit_overrides(DepositLayer& layer,
                             const DepositOverrides& overrides) {
    for (const auto& [idx, remaining] : overrides) {
        auto it = layer.cells.find(idx);
        if (it == layer.cells.end()) continue;
        it->second.remaining = remaining < 0 ? 0 : remaining;
    }
    ++layer.revision;
}

} // namespace sm
