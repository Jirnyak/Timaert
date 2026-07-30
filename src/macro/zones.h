// Difficulty zones — per-cell danger heightmap (0..9). Mirrors zones.ts.
#pragma once
#include <cstddef>
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

    static std::uint8_t decode(std::uint8_t value) {
        return value < std::uint8_t(kZoneCount) ? value : 0u;
    }

    std::size_t cell_count() const {
        std::size_t n = 0;
        return FeatureLayer::cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n && field.size() >= n;
    }

    bool has_data_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }

    bool has_field_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && field.size() >= n;
    }

    std::uint8_t at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return 0;
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        return i < data.size() ? decode(data[i]) : 0;
    }
    float field_at(int x, int y) const {
        if (width <= 0 || height <= 0 || field.empty()) return 0.0f;
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        return i < field.size() ? field[i] : 0.0f;
    }
};

struct TreeLayer;

// Build zones from cities, villages, features. Heightmap parameters mirror zones.ts.
// `waterMaskA` (optional) - RGBA terrain bytes; cells with alpha < 128 add WATER_BOOST.
// If provided, `waterMaskByteCount` must cover width*height*4 or the mask is ignored.
// `treeLayer` (optional): forest danger scales continuously with the cell's
// tree count (deep massifs get the full old FT_Tree boost, ambience little).
ZoneLayer generate_zones(int width, int height, std::uint32_t seed,
                         const std::vector<ZoneSeed>& cities,
                         const std::vector<ZoneSeed>& villages,
                         const FeatureLayer& features,
                         const std::uint8_t* waterMaskA = nullptr,
                         std::size_t waterMaskByteCount = 0u,
                         const TreeLayer* treeLayer = nullptr);

} // namespace sm
