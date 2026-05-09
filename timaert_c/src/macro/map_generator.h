// CPU-side macroworld terrain data. Built by GPU FBO + readback.
#pragma once
#include <cstdint>
#include <vector>
#include "gl/gl.h"

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
    GLuint texture = 0;   // GPU master texture (RGBA8).

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
