#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

inline float noise2D(int x, int y, uint32_t seed)
{
    // Wrap coordinates for toroidal world (seamless wrapping at edges)
    x = ((x % 1024) + 1024) % 1024;
    y = ((y % 1024) + 1024) % 1024;
    
    uint32_t h = x * 374761393u + y * 668265263u + seed * 1442695041u;
    h ^= h >> 13;
    h *= 1274126177u;
    return (h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}

// Smooth interpolation for organic coastlines
[[nodiscard]] inline float smoothstep(float t) noexcept
{
    return t * t * (3.0f - 2.0f * t);
}

// Interpolated noise for smoother, more organic coastlines
[[nodiscard]] inline float smoothNoise2D(float x, float y, uint32_t seed) noexcept
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - xi;
    const float yf = y - yi;
    
    // Get four corner noise values
    float n00 = noise2D(xi, yi, seed);
    float n10 = noise2D(xi + 1, yi, seed);
    float n01 = noise2D(xi, yi + 1, seed);
    float n11 = noise2D(xi + 1, yi + 1, seed);
    
    // Interpolate smoothly
    float u = smoothstep(xf);
    float v = smoothstep(yf);
    
    float nx0 = n00 * (1.0f - u) + n10 * u;
    float nx1 = n01 * (1.0f - u) + n11 * u;
    float nxy = nx0 * (1.0f - v) + nx1 * v;
    
    return nxy;
}

// Toroidal distance calculation for seamless wrapping world
[[nodiscard]] inline float squared_distance(int x1, int y1, int x2, int y2) noexcept
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    
    // Wrap distances to shortest path on torus
    if (dx > 512) dx = 1024 - dx;
    if (dx < -512) dx = 1024 + dx;
    if (dy > 512) dy = 1024 - dy;
    if (dy < -512) dy = 1024 + dy;
    
    const float fdx = static_cast<float>(dx);
    const float fdy = static_cast<float>(dy);
    return fdx * fdx + fdy * fdy;
}

// Generate continent map (large landmasses and oceans)
// Returns 1.0 for continents/islands, 0.0 for ocean
// Algorithm: Places blob seeds and uses falloff distance with smooth organic shorelines
inline float generate_continent_map(int x, int y, uint32_t seed, int num_continents, int num_islands)
{
    // Create pseudo-random continent centers based on seeds
    float max_influence = 0.0f;
    
    // Generate OCEANS first (large empty water basins)
    const uint32_t ocean_seed = seed + 3000u;
    // Randomize ocean count: 1-2
    const int num_oceans = 1 + (((seed ^ 8877u) % 1000u) % 2u);
    
    for (int i = 0; i < num_oceans; ++i)
    {
        const uint32_t h1 = (static_cast<uint32_t>(i) * 73856093u) ^ ocean_seed;
        const uint32_t h2 = (static_cast<uint32_t>(i) * 19349663u) ^ ocean_seed;
        const uint32_t h3 = (static_cast<uint32_t>(i) * 43614093u) ^ ocean_seed;
        
        // Randomize ocean size: 100-180 pixels radius
        const float ocean_radius = 100.0f + (static_cast<float>(h3 % 800u) / 10.0f);
        const float ocean_radius_sq = ocean_radius * ocean_radius;
        
        // Spread oceans across map
        const int ox = static_cast<int>((h1 % 1024u));
        const int oy = static_cast<int>((h2 % 1024u));
        
        const float dist_sq = squared_distance(x, y, ox, oy);
        
        // Ocean falloff: pushes influence DOWN (toward water)
        if (dist_sq < ocean_radius_sq)
        {
            float dist = std::sqrt(dist_sq);
            // Add noise to ocean edges for organic shorelines
            const float noise = smoothNoise2D(
                static_cast<float>(x) / 20.0f + i * 50.0f,
                static_cast<float>(y) / 20.0f + i * 50.0f,
                ocean_seed
            ) * 20.0f;
            dist = dist - noise;
            
            // Ocean reduces influence (inverted)
            const float ocean_influence = std::max(0.0f, 1.0f - (dist / ocean_radius));
            max_influence = std::max(0.0f, max_influence - ocean_influence * 0.95f);
        }
    }
    
    // Ensure we don't go negative
    max_influence = std::max(0.0f, max_influence);
    
    // Generate ISLANDS WITHIN OCEANS (sparse, small)
    const uint32_t ocean_island_seed = seed + 4000u;
    for (int i = 0; i < num_oceans; ++i)
    {
        const uint32_t h1 = (static_cast<uint32_t>(i) * 73856093u) ^ ocean_seed;
        const uint32_t h2 = (static_cast<uint32_t>(i) * 19349663u) ^ ocean_seed;
        const uint32_t h3 = (static_cast<uint32_t>(i) * 43614093u) ^ ocean_seed;
        
        const float ocean_radius = 100.0f + (static_cast<float>(h3 % 800u) / 10.0f);
        const int ox = static_cast<int>((h1 % 1024u));
        const int oy = static_cast<int>((h2 % 1024u));
        
        // Only generate islands if point is within ocean zone
        const float dist_to_ocean = std::sqrt(squared_distance(x, y, ox, oy));
        if (dist_to_ocean < ocean_radius * 0.9f)
        {
            // Random number of islands per ocean: 2-5
            const uint32_t islands_in_ocean = 2u + ((ocean_island_seed + i) % 4u);
            
            for (uint32_t j = 0; j < islands_in_ocean; ++j)
            {
                const uint32_t island_seed = ocean_island_seed + i * 100u + j * 17u;
                const uint32_t ix_offset = ((island_seed * 11u) % 1000u);
                const uint32_t iy_offset = ((island_seed * 23u) % 1000u);
                
                // Island center relative to ocean center
                const int island_offset_x = static_cast<int>(ix_offset) - 500;
                const int island_offset_y = static_cast<int>(iy_offset) - 500;
                
                const int ix = ox + island_offset_x;
                const int iy = oy + island_offset_y;
                
                // Clamp to valid map bounds
                if (ix >= 0 && ix < 1024 && iy >= 0 && iy < 1024)
                {
                    // Island size: 10-30 pixels radius
                    const float island_radius = 10.0f + (static_cast<float>((island_seed * 31u) % 200u) / 10.0f);
                    const float island_radius_sq = island_radius * island_radius;
                    
                    const float island_dist_sq = squared_distance(x, y, ix, iy);
                    
                    if (island_dist_sq < island_radius_sq)
                    {
                        float island_dist = std::sqrt(island_dist_sq);
                        
                        // Add noise to island edges
                        const float island_noise = smoothNoise2D(
                            static_cast<float>(ix) / 20.0f,
                            static_cast<float>(iy) / 20.0f,
                            island_seed
                        ) * 10.0f;
                        island_dist = island_dist - island_noise;
                        
                        // Island influence
                        const float island_influence = std::max(0.0f, 1.0f - (island_dist / island_radius));
                        max_influence = std::max(max_influence, island_influence * 0.6f);
                    }
                }
            }
        }
    }
    
    // Generate continents with RANDOM sizes (smaller)
    const uint32_t continent_seed = seed + 1000u;
    for (int i = 0; i < num_continents; ++i)
    {
        // Pseudo-random continent properties
        const uint32_t h1 = (static_cast<uint32_t>(i) * 73856093u) ^ continent_seed;
        const uint32_t h2 = (static_cast<uint32_t>(i) * 19349663u) ^ continent_seed;
        const uint32_t h3 = (static_cast<uint32_t>(i) * 43614093u) ^ continent_seed;
        
        // Randomize continent size: 75-220 pixels radius (bigger spread)
        const float base_radius = 75.0f + (static_cast<float>(h3 % 1450u) / 10.0f);
        const float continent_radius_sq = base_radius * base_radius;
        
        // Spread continents across map - more random now
        const int cx = static_cast<int>((h1 % 1024u));
        const int cy = static_cast<int>((h2 % 1024u));
        
        const float dist_sq = squared_distance(x, y, cx, cy);
        
        // Falloff: from 1.0 at center to 0 at radius
        if (dist_sq < continent_radius_sq)
        {
            float dist = std::sqrt(dist_sq);
            // Use smooth interpolated noise for organic coastlines
            const float noise_strength = 20.0f + (static_cast<float>(h3 % 10u) / 2.0f);
            const float noise = smoothNoise2D(
                static_cast<float>(x) / 20.0f + i * 50.0f,
                static_cast<float>(y) / 20.0f + i * 50.0f,
                continent_seed
            ) * noise_strength;
            dist = dist - noise; // Make shoreline organic and natural
            
            const float influence = std::max(0.0f, 1.0f - (dist / base_radius));
            max_influence = std::max(max_influence, influence * 0.98f);
        }
    }
    
    // Generate very FEW islands - clustered in certain regions
    const uint32_t island_seed = seed + 2000u;
    for (int i = 0; i < num_islands; ++i)
    {
        // Create island CLUSTERS - group islands together
        const uint32_t cluster_hash = (static_cast<uint32_t>(i) * 73856093u) ^ island_seed;
        const uint32_t island_hash = (static_cast<uint32_t>(i) * 83492791u) ^ island_seed;
        const uint32_t size_hash = (static_cast<uint32_t>(i) * 43614093u) ^ island_seed;
        
        // Randomize island size: 10-100 pixels (wide variation)
        const float island_radius = 10.0f + (static_cast<float>(size_hash % 900u) / 10.0f);
        const float island_radius_sq = island_radius * island_radius;
        
        // Determine cluster center
        const int cluster_x = static_cast<int>((cluster_hash % 1024u));
        const int cluster_y = static_cast<int>(((cluster_hash >> 16) % 1024u));
        
        // Place individual islands around cluster center
        const int ix = cluster_x + static_cast<int>((island_hash % 200u)) - 100;
        const int iy = cluster_y + static_cast<int>(((island_hash >> 16) % 200u)) - 100;
        
        // Wrap around world
        const int wrapped_ix = (ix + 1024) % 1024;
        const int wrapped_iy = (iy + 1024) % 1024;
        
        const float dist_sq = squared_distance(x, y, wrapped_ix, wrapped_iy);
        
        if (dist_sq < island_radius_sq)
        {
            float dist = std::sqrt(dist_sq);
            // Smooth noise for islands too
            const float noise = smoothNoise2D(
                static_cast<float>(x) / 15.0f,
                static_cast<float>(y) / 15.0f,
                island_seed + static_cast<uint32_t>(i)
            ) * 10.0f;
            dist = dist - noise;
            
            const float influence = std::max(0.0f, 1.0f - (dist / island_radius));
            max_influence = std::max(max_influence, influence * 0.65f);
        }
    }
    
    return std::clamp(max_influence, 0.0f, 1.0f);
}


