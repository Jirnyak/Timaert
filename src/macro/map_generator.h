// CPU-side macroworld terrain data. Built by CPU synthesis.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include "core/torus.h"
#include "macro/biomes.h"

namespace sm {

struct LayerParameters {
    // The world's seed IS an integer (CANON S26 «всё дискретно»); it was a
    // float here, so every consumer round-tripped through casts and any seed
    // above 2^24 would have silently lost bits. Where the synthesis feeds it
    // into float noise, the cast happens AT the use site (map_generator.cpp)
    // — every UI-facing seed is < 100000 (see main.cpp), exactly
    // representable, so the generated world is bit-identical.
    std::uint32_t seed = 1u;
    // THE macroworld synthesis defaults — this struct is the source of truth.
    float seaLevel = 0.40f;
    float heightScale = 1.0f;
    float moistureScale = 1.0f;
    float temperatureVariation = 0.30f;
    float continentScale = 0.50f;
    float continentIntensity = 0.40f;
    float ridgeIntensity = 0.15f;
    float domainWarp = 0.30f;
    float heightOctaves = 6.0f;
    float moistureOctaves = 4.0f;
};

struct TerrainData {
    int width = 0, height = 0;
    // RGBA: R=height, G=moisture, B=temperature, A=mask (255=land,0=water).
    std::vector<std::uint8_t> rgba;
    // R8 river mask generated from the terrain heightmap. 255 = river cell.
    std::vector<std::uint8_t> riverData;

    static bool cell_count_for(int w, int h, std::size_t& out) {
        out = 0;
        if (w <= 0 || h <= 0)
            return false;
        if (std::size_t(w) > std::numeric_limits<std::size_t>::max() / std::size_t(h))
            return false;
        out = std::size_t(w) * std::size_t(h);
        return true;
    }

    std::size_t cell_count() const {
        std::size_t n = 0;
        return cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_rgba_storage() const {
        const std::size_t n = cell_count();
        return n > 0u
            && n <= std::numeric_limits<std::size_t>::max() / 4u
            && rgba.size() >= n * 4u;
    }

    bool has_river_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && riverData.size() >= n;
    }

    inline std::uint8_t height_at(int x, int y) const {
        return rgba[std::size_t(y * width + x) * 4 + 0];
    }
    inline std::uint8_t moisture_at(int x, int y) const {
        return rgba[std::size_t(y * width + x) * 4 + 1];
    }
    inline std::uint8_t temperature_at(int x, int y) const {
        return rgba[std::size_t(y * width + x) * 4 + 2];
    }
    inline bool is_water(int x, int y, std::uint8_t seaLevel) const {
        return height_at(x, y) < seaLevel;
    }
};

// ── THE cell biome classifier (CANON S6, 2026-08-24) ─────────────────────
// One cascade for a WORLD CELL: the baked land mask decides Water, elevation
// decides Mountain, the climate matrix fills in the rest. `biome_at`
// (biomes.h) stays the pure-math core for callers that do not hold a cell —
// the shader mirror and world-gen scratch buffers.
//
// The mask, not the threshold: both mask writers (the climate synth and the
// river carve) derive A from the SAME quantized height the R channel stores,
// so the mask exists precisely to make "is this water" answer identically
// everywhere. Before this door the question was answered five ways — the
// subworld read the mask, travel/pathfinding/fauna re-derived it from float
// height vs a float sea level, and two UI twins disagreed with each other at
// the coast (canon-audit C5/H10) — a coastal cell could be Meadow to a boot
// and Water to a wolf. The sea level itself vanishes from the query: the mask
// already carries it.
//
// Torus-wrapped; fail-closed to Water (a world with no storage has no land to
// walk, grow or hunt — the zero contribution, never a crash).
inline Biome biome_at_cell(const TerrainData& td, int x, int y) {
    if (!td.has_rgba_storage()) return Biome::Water;
    const int xi = wrapi(x, td.width);
    const int yi = wrapi(y, td.height);
    const std::size_t s =
        (std::size_t(yi) * std::size_t(td.width) + std::size_t(xi)) * 4u;
    if (td.rgba[s + 3u] == 0u) return Biome::Water;   // the baked land mask
    const float h = float(td.rgba[s + 0u]) / 255.0f;
    if (h >= kMountainBiomeLevel) return Biome::Mountain;
    return biome_from_climate(float(td.rgba[s + 2u]) / 255.0f,
                              float(td.rgba[s + 1u]) / 255.0f);
}

// Generate the master texture on GPU and read back to CPU. Allocates `texture`.
TerrainData generate_terrain(int w, int h, const LayerParameters& params);

// Second CPU synth pass (also called by generate_terrain): trace least-cost
// rivers hugging climate-biome edges toward the nearest sea, stamp them into
// td.riverData, and carve those cells below sea level so they classify as
// Biome::Water. Exposed for the river generation test suite; call it on a
// TerrainData whose rgba height/moisture/temperature channels are populated.
void generate_river_data(TerrainData& td, const LayerParameters& params);

void destroy_terrain(TerrainData& t);

} // namespace sm
