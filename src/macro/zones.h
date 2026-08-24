// Difficulty zones — the per-cell DANGER CONTINUUM, one byte: 0 = absolutely
// safe, 255 = where the strongest demons stand (owner, 2026-08-24; the ten
// quantised steps were a false discreteness — the labels below survive only
// as display BANDS over the continuum). Spawn composition and loot quality
// read the byte through the one matching law (macro/spawn law): a row's
// derived strength against the cell's danger, tails never zero.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/torus.h"
#include "macro/features.h"

namespace sm {

// Display bands over the continuum (UI copy only — no mechanic may branch
// on a band; mechanics read the byte).
constexpr int kZoneCount = 10;
inline constexpr std::uint8_t zone_band(std::uint8_t z) {
    return std::uint8_t((int(z) * kZoneCount) >> 8);
}
// The exit gate's "settled land" ceiling: everything the old quantiser called
// bands 0..2 — derived, not tuned: ceil(3 * 256 / kZoneCount) - 1.
inline constexpr int kSafeExitDanger = (3 * 256) / kZoneCount - 1;
inline constexpr const char* kZoneLabels[kZoneCount] = {
    "Safe Haven", "Settled", "Patrolled", "Frontier", "Wild",
    "Untamed", "Perilous", "Forsaken", "Cursed", "Hellgate",
};

struct ZoneSeed { int x, y; };

struct ZoneLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;   // quantised 0..9
    // (A parallel continuous float grid lived here until 2026-08-24 — 4 MiB
    // per world with no reader in the game, canon-audit D. The continuous
    // value exists only DURING the bake; a test that wants its precision
    // asks generate_zones for the optional capture below.)

    std::size_t cell_count() const {
        std::size_t n = 0;
        return FeatureLayer::cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }

    std::uint8_t at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return 0;
        const int wx = wrapi(x, width);
        const int wy = wrapi(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        return i < data.size() ? data[i] : std::uint8_t(0);
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
                         const TreeLayer* treeLayer = nullptr,
                         // Optional bake-time capture of the CONTINUOUS zone
                         // value per cell — for tests that verify the law at
                         // a precision the 0..9 quantisation would swallow.
                         // The game never stores it.
                         std::vector<float>* continuousOut = nullptr);

} // namespace sm
