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
            // both would land at derivation, the SCARCE kind takes the cell
            // (discovery may later add iron INTO a stone cell — that is the
            // one way a cell comes to carry two kinds).
            if (mountain) {
                if ((hash3(std::uint32_t(x), std::uint32_t(y),
                           seed ^ kIronSalt) & kIronChanceMask) == 0) {
                    layer.cells[std::size_t(DepositKind::Iron)]
                        .emplace(idx, kIronBase);
                    continue;
                }
                if ((hash3(std::uint32_t(x), std::uint32_t(y),
                           seed ^ kStoneSalt) & kStoneChanceMask) == 0) {
                    layer.cells[std::size_t(DepositKind::Stone)]
                        .emplace(idx, kStoneBase);
                }
                continue;
            }
            // Clay is alluvial: lowland cells touching a river.
            if ((hash3(std::uint32_t(x), std::uint32_t(y),
                       seed ^ kClaySalt) & kClayChanceMask) == 0
                && river_adjacent(terrain, x, y)) {
                layer.cells[std::size_t(DepositKind::Clay)]
                    .emplace(idx, kClayBase);
            }
        }
    }
    return layer;
}

bool set_deposit_remaining(DepositLayer& layer, DepositKind kind,
                           int x, int y, std::int32_t remaining) {
    if (layer.width <= 0 || layer.height <= 0) return false;
    auto& m = layer.cells[std::size_t(kind)];
    auto it = m.find(layer.wrap_index(x, y));
    if (it == m.end()) return false;   // mining invents no geology
    if (remaining <= 0) {
        // ANNIHILATION (owner, 2026-08-28): a worked-out vein is a vein that
        // no longer exists. The counter keeps the scarcity baseline the dead
        // cell used to carry.
        m.erase(it);
        ++layer.drainedCells[std::size_t(kind)];
    } else {
        it->second = remaining;
    }
    ++layer.revision;
    return true;
}

void create_deposit(DepositLayer& layer, DepositKind kind,
                    int x, int y, std::int32_t amount) {
    if (layer.width <= 0 || layer.height <= 0) return;
    // Every entry is ALIVE (annihilation law): genesis of an empty vein
    // would mint the "dry cell" state back into existence.
    if (amount <= 0) return;
    layer.cells[std::size_t(kind)][layer.wrap_index(x, y)] = amount;
    ++layer.revision;
}

void restore_deposit_cells(DepositLayer& layer, const DepositLayer& loaded) {
    if (layer.width <= 0 || layer.height <= 0) return;
    const std::uint32_t n =
        std::uint32_t(layer.width) * std::uint32_t(layer.height);
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        layer.cells[k].clear();
        for (const auto& [idx, remaining] : loaded.cells[k]) {
            if (idx >= n) continue;   // stale index vs a corrupt file: drop
            if (remaining <= 0) continue;   // pre-annihilation dry cell: gone
            layer.cells[k].emplace(idx, remaining);
        }
        // The scarcity baseline travels with the cells it stands in for.
        layer.drainedCells[k] = loaded.drainedCells[k];
    }
    ++layer.revision;
}

int iron_vein_lump() { return kIronBase; }

float iron_depletion(const DepositLayer& layer) {
    std::int64_t remaining = 0;
    std::int64_t cells =
        std::int64_t(layer.drainedCells[std::size_t(DepositKind::Iron)]);
    for (const auto& [idx, rem] : layer.cells[std::size_t(DepositKind::Iron)]) {
        (void)idx;
        ++cells;
        remaining += rem;
    }
    // cells counts live AND annihilated veins: a fully worked-out world has
    // an empty map but a full counter, and must read depleted, not virgin.
    if (cells == 0) return 0.0f;
    const std::int64_t virgin = cells * std::int64_t(kIronBase);
    const float d = 1.0f - float(remaining) / float(virgin);
    return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
}

bool discover_iron_vein(DepositLayer& layer, std::uint32_t roll) {
    // Candidates: stone cells NOT yet holding iron (striking the same cell
    // twice would just refill it — that is a refill, not a discovery). Two
    // passes over a sparse map, once a day at most.
    const auto& stone = layer.cells[std::size_t(DepositKind::Stone)];
    const auto& iron  = layer.cells[std::size_t(DepositKind::Iron)];
    std::uint32_t candidates = 0;
    for (const auto& [idx, rem] : stone) {
        (void)rem;
        if (!iron.count(idx)) ++candidates;
    }
    if (candidates == 0) return false;
    std::uint32_t target = roll % candidates;
    for (const auto& [idx, rem] : stone) {
        (void)rem;
        if (iron.count(idx)) continue;
        if (target-- != 0) continue;
        // The quarry STAYS — iron is found IN the mountain, nothing vanishes.
        const int x = int(idx % std::uint32_t(layer.width));
        const int y = int(idx / std::uint32_t(layer.width));
        create_deposit(layer, DepositKind::Iron, x, y, kIronBase);
        return true;
    }
    return false;
}

} // namespace sm
