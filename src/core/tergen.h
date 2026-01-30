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

// Domain warping for organic deformation
[[nodiscard]] inline float domain_warp(float x, float y, uint32_t seed) noexcept {
    const float warp_scale = 0.03f;
    const float warp_strength = 30.0f;
    
    const float offset_x = fbm(x * warp_scale, y * warp_scale, seed + 1000u, 4) * warp_strength;
    const float offset_y = fbm(x * warp_scale, y * warp_scale, seed + 2000u, 4) * warp_strength;
    
    return fbm((x + offset_x) * 0.01f, (y + offset_y) * 0.01f, seed, 6);
}

// Ultra-large-scale potential field - creates ocean basins (connected low regions)
// Physics: like gravitational potential creating valleys that connect
[[nodiscard]] inline float ocean_basin_field(float x, float y, uint32_t seed) noexcept {
    // Very low frequency = massive connected basins (wavelength ~900-1000px)
    return fbm(x * 0.0009f, y * 0.0009f, seed + 9000u, 2);
}

// Large-scale anisotropic field - creates continental/oceanic structure
// Target: large continents 256-512px, large ocean basins
[[nodiscard]] inline float anisotropic_field(float x, float y, uint32_t seed) noexcept {
    // Very large-scale directional flows (wavelength ~400-600px)
    const float angle = fbm(x * 0.001f, y * 0.001f, seed + 5000u, 2) * 6.28318f;
    const float strength = fbm(x * 0.0012f, y * 0.0012f, seed + 6000u, 2);
    
    // Stronger anisotropic stretching for pronounced directional features
    const float stretch_x = std::cos(angle) * strength;
    const float stretch_y = std::sin(angle) * strength;
    
    const float warped_x = x + stretch_x * 320.0f;
    const float warped_y = y + stretch_y * 320.0f;
    
    // Continental scale base structure (wavelength ~350px)
    return fbm(warped_x * 0.003f, warped_y * 0.003f, seed + 7000u, 3);
}

// Curl noise for large-scale rotational flow patterns (tectonic-like)
[[nodiscard]] inline float curl_noise(float x, float y, uint32_t seed) noexcept {
    constexpr float eps = 1.5f;
    
    // Large-scale curl at continental wavelengths (~400px)
    const float n_x0 = fbm(x - eps, y, seed + 8000u, 2);
    const float n_x1 = fbm(x + eps, y, seed + 8000u, 2);
    const float n_y0 = fbm(x, y - eps, seed + 8000u, 2);
    const float n_y1 = fbm(x, y + eps, seed + 8000u, 2);
    
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
    
    // Large-scale curl patterns (wavelength ~400px) - tectonic organization
    const float curl = curl_noise(fx * 0.002f, fy * 0.002f, seed) * 0.2f;
    
    // Medium-scale islands (wavelength ~80px) - minimal to avoid fragmenting
    const float warp_scale = 0.008f;
    const float warp_strength = 25.0f;
    const float offset_x = fbm(fx * warp_scale, fy * warp_scale, seed + 1000u, 2) * warp_strength;
    const float offset_y = fbm(fx * warp_scale, fy * warp_scale, seed + 2000u, 2) * warp_strength;
    const float warped = fbm((fx + offset_x) * 0.012f, (fy + offset_y) * 0.012f, seed, 3);
    
    // Small-scale coastline detail - very minimal to preserve large-scale structure
    const float detail = fbm(fx * 0.025f, fy * 0.025f, seed + 3000u, 2) * 0.05f;
    
    // Compose: basin and anisotropic dominate for large features
    float elevation = basin * 0.5f + anisotropic * 0.55f + curl + warped * 0.12f + detail;
    
    // Bias for fewer, larger ocean basins
    elevation -= 0.18f;
    
    return elevation;
}
