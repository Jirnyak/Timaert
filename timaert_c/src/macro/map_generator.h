// CPU-side macroworld terrain data. Built by GPU FBO + readback.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sm {

struct LayerParameters {
    float seed = 1.0f;
    // Match TS `defaultParameters` in src/webgl/shaders.ts.
    float seaLevel = 0.40f;
    float heightScale = 1.0f;
    float moistureScale = 1.0f;
    float temperatureVariation = 0.30f;
    float tempMin = 0.0f, tempMax = 1.0f;          // unused now (TS packs to 0..1)
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
    std::uint32_t texture = 0;      // GPU master texture id (RGBA8).
    std::uint32_t riverTexture = 0; // GPU river mask texture id (R8).

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

// Generate the master texture on GPU and read back to CPU. Allocates `texture`.
TerrainData generate_terrain(int w, int h, const LayerParameters& params);

void destroy_terrain(TerrainData& t);

} // namespace sm
