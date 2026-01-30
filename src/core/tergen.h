#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

// Hash-based noise for toroidal world
inline float noise2D(int x, int y, uint32_t seed) {
    x = ((x % 1024) + 1024) % 1024;
    y = ((y % 1024) + 1024) % 1024;
    uint32_t h = x * 374761393u + y * 668265263u + seed * 1442695041u;
    h ^= h >> 13;
    h *= 1274126177u;
    return (h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}

[[nodiscard]] inline float smoothstep(float t) noexcept {
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline float smoothNoise2D(float x, float y, uint32_t seed) noexcept {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - xi;
    const float yf = y - yi;

    const float n00 = noise2D(xi, yi, seed);
    const float n10 = noise2D(xi + 1, yi, seed);
    const float n01 = noise2D(xi, yi + 1, seed);
    const float n11 = noise2D(xi + 1, yi + 1, seed);

    const float u = smoothstep(xf);
    const float v = smoothstep(yf);

    const float nx0 = n00 * (1.0f - u) + n10 * u;
    const float nx1 = n01 * (1.0f - u) + n11 * u;
    return nx0 * (1.0f - v) + nx1 * v;
}

// Multi-octave fractal Brownian motion
[[nodiscard]] inline float fbm(float x, float y, uint32_t seed, int octaves = 6) noexcept {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float max_value = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += smoothNoise2D(x * frequency, y * frequency, seed + i * 100u) * amplitude;
        max_value += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value / max_value;
}

// Ultra-large-scale potential field - creates ocean basins (connected low regions)
[[nodiscard]] inline float ocean_basin_field(float x, float y, uint32_t seed) noexcept {
    // Very low frequency = massive connected basins (wavelength ~1200-1500px)
    // Single octave for clean, large-scale structure
    return fbm(x * 0.0006f, y * 0.0006f, seed + 9000u, 1);
}

// Large-scale anisotropic field - creates continental/oceanic structure
// Target: large continents 400-700px with realistic directional features
[[nodiscard]] inline float anisotropic_field(float x, float y, uint32_t seed) noexcept {
    // Large-scale directional flows (wavelength ~500-800px) - 2 octaves for smooth continental drift patterns
    const float angle = fbm(x * 0.0008f, y * 0.0008f, seed + 5000u, 2) * 6.28318f;
    const float strength = fbm(x * 0.0009f, y * 0.0009f, seed + 6000u, 2);
    
    // Strong anisotropic stretching for elongated continents (tectonic-like)
    const float stretch_x = std::cos(angle) * strength;
    const float stretch_y = std::sin(angle) * strength;
    
    const float warped_x = x + stretch_x * 600.0f;
    const float warped_y = y + stretch_y * 600.0f;
    
    // Continental scale base structure (wavelength ~450px) - 2 octaves for large smooth continents
    return fbm(warped_x * 0.0022f, warped_y * 0.0022f, seed + 7000u, 2);
}

// Medium-scale detail for islands and coastal complexity
[[nodiscard]] inline float island_detail_field(float x, float y, uint32_t seed) noexcept {
    // Medium frequency (wavelength ~120-180px) - creates island chains and coastal features
    // 3 octaves for natural island size variation
    return fbm(x * 0.008f, y * 0.008f, seed + 8000u, 3);
}

// Fine-scale detail for lakes and small features
[[nodiscard]] inline float lake_detail_field(float x, float y, uint32_t seed) noexcept {
    // Higher frequency (wavelength ~40-80px) - creates lakes and local variation
    // 2 octaves for natural lake distribution
    return fbm(x * 0.018f, y * 0.018f, seed + 10000u, 2);
}

// Multi-scale terrain generation with proper frequency separation
// Large continents (400-700px), sparse medium islands (80-150px), local lakes
inline float generate_continent_map(int x, int y, uint32_t seed) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    
    // Layer 1: Ultra-large ocean basin potential (wavelength ~1200-1500px)
    // Defines major ocean vs land distribution
    const float basin = ocean_basin_field(fx, fy, seed);
    
    // Layer 2: Large-scale anisotropic continents (wavelength ~450-800px)
    // Creates realistic elongated continental shapes
    const float continents = anisotropic_field(fx, fy, seed);
    
    // Layer 3: Medium-scale island chains (wavelength ~120-180px)
    // Adds sparse islands and coastal complexity
    const float islands = island_detail_field(fx, fy, seed);
    
    // Layer 4: Fine-scale lakes and local features (wavelength ~40-80px)
    // Creates inland lakes and small-scale variation
    const float lakes = lake_detail_field(fx, fy, seed);
    
    // Hierarchical composition with decreasing weights (natural frequency cascade)
    // Basin (50%) + Continents (35%) + Islands (10%) + Lakes (5%)
    float elevation = basin * 0.50f + continents * 0.35f + islands * 0.10f + lakes * 0.05f;
    
    return elevation;
}
