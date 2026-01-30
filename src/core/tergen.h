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
    // Very low frequency = massive connected basins (wavelength ~900-1000px)
    return fbm(x * 0.0008f, y * 0.0008f, seed + 9000u, 1);
}

// Large-scale anisotropic field - creates continental/oceanic structure
// Target: large continents 256-512px, large ocean basins
[[nodiscard]] inline float anisotropic_field(float x, float y, uint32_t seed) noexcept {
    // Very large-scale directional flows (wavelength ~400-600px) - more octaves for complex anisotropy
    const float angle = fbm(x * 0.001f, y * 0.001f, seed + 5000u, 3) * 6.28318f;
    const float strength = fbm(x * 0.0012f, y * 0.0012f, seed + 6000u, 3);
    
    // Much stronger anisotropic stretching for pronounced elongated continents/oceans
    const float stretch_x = std::cos(angle) * strength;
    const float stretch_y = std::sin(angle) * strength;
    
    const float warped_x = x + stretch_x * 480.0f;
    const float warped_y = y + stretch_y * 480.0f;
    
    // Continental scale base structure (wavelength ~350px) - more octaves for detailed anisotropy
    return fbm(warped_x * 0.003f, warped_y * 0.003f, seed + 7000u, 3);
}

// Curl noise for large-scale rotational flow patterns (tectonic-like)
[[nodiscard]] inline float curl_noise(float x, float y, uint32_t seed) noexcept {
    constexpr float eps = 1.5f;
    
    // Large-scale curl at continental wavelengths (~400px) - single octave for cleaner patterns
    const float n_x0 = fbm(x - eps, y, seed + 8000u, 1);
    const float n_x1 = fbm(x + eps, y, seed + 8000u, 1);
    const float n_y0 = fbm(x, y - eps, seed + 8000u, 1);
    const float n_y1 = fbm(x, y + eps, seed + 8000u, 1);
    
    const float curl = (n_y1 - n_y0) - (n_x1 - n_x0);
    
    return curl;
}

// Physics-inspired terrain with large-scale anisotropy
// Large continents (256-512px), large ocean basins, fewer but bigger features
inline float generate_continent_map(int x, int y, uint32_t seed) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    
    // Ultra-large ocean basin potential (wavelength ~700px) - creates few large oceans
    const float basin = ocean_basin_field(fx, fy, seed);
    
    // Large-scale anisotropic field (wavelength ~400px) - large continents
    const float anisotropic = anisotropic_field(fx, fy, seed);
    
    // Compose: basin and anisotropic only - cleanest possible large continents
    float elevation = basin * 0.8f + anisotropic * 0.8f;
    
    
    return elevation;
}
