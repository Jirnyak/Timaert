// Difficulty zones — per-cell danger heightmap (0..9). Mirrors zones.ts.
#pragma once
#include <cstdint>
#include <vector>
#include "macro/features.h"

namespace sm {

constexpr int kZoneCount = 10;
inline constexpr const char* kZoneLabels[kZoneCount] = {
    "Safe Haven", "Settled", "Patrolled", "Frontier", "Wild",
    "Untamed", "Perilous", "Forsaken", "Cursed", "Hellgate",
};

struct ZoneSeed { int x, y; };

struct ZoneLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;   // quantised 0..9
    std::vector<float>        field;  // continuous [0,1]
    std::uint8_t at(int x, int y) const {
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        return data[std::size_t(wy) * width + wx];
    }
    float field_at(int x, int y) const {
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        return field[std::size_t(wy) * width + wx];
    }
};

// Build zones from cities, villages, features. Heightmap parameters mirror zones.ts.
// `waterMaskA` (optional) — RGBA terrain bytes; cells with alpha < 128 add WATER_BOOST.
ZoneLayer generate_zones(int width, int height, std::uint32_t seed,
                         const std::vector<ZoneSeed>& cities,
                         const std::vector<ZoneSeed>& villages,
                         const FeatureLayer& features,
                         const std::uint8_t* waterMaskA = nullptr);

} // namespace sm
