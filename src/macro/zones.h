// Difficulty zones — the per-cell DANGER CONTINUUM, one byte: 0 = absolutely
// safe, 255 = where the strongest demons stand (owner, 2026-08-24; the ten
// quantised steps were a false discreteness — only the display BANDS over
// the continuum survive). Spawn composition and loot quality
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
// bands 0..2 — derived, not tuned: 3 * 256 / kZoneCount - 1 = 75.
inline constexpr int kSafeExitDanger = (3 * 256) / kZoneCount - 1;

struct ZoneSeed { int x, y; };

struct ZoneLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;   // the danger byte, 0..255
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
        if (!world_shape_ok(width, height) || data.empty()) return 0;
        const std::size_t i = cell_of(x, y, width);
        return i < data.size() ? data[i] : std::uint8_t(0);
    }
};

struct TreeLayer;
struct TerrainData;

// Build zones from cities, villages, features. Heightmap parameters mirror zones.ts.
// `terrain` (optional) — КАРТА, а не её байты: вода добавляет WATER_BOOST, а
// высота классифицирует гору. Здесь стояли `const std::uint8_t* waterMaskA` +
// длина буфера, то есть терраин, разобранный на сырые байты у звонящего, —
// вопрос «вода ли» приходилось писать заново на месте (M-109).
// `treeLayer` (optional): forest danger scales continuously with the cell's
// tree count (deep massifs get the full old FT_Tree boost, ambience little).
ZoneLayer generate_zones(int width, int height, std::uint32_t seed,
                         const std::vector<ZoneSeed>& cities,
                         const std::vector<ZoneSeed>& villages,
                         const FeatureLayer& features,
                         const TerrainData* terrain = nullptr,
                         const TreeLayer* treeLayer = nullptr,
                         // Optional bake-time capture of the CONTINUOUS zone
                         // value per cell — for tests that verify the law at
                         // a precision the 0..9 quantisation would swallow.
                         // The game never stores it.
                         std::vector<float>* continuousOut = nullptr);

} // namespace sm
