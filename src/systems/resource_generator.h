#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include "core/game_context.h"

namespace resource
{
struct ResourceConfig
{
    int seed_count = 60;        // number of cluster centers
    int cluster_radius = 8;     // radius of diffusion (tiles)
    double sprinkle_fraction = 0.005; // fraction of random tiny deposits
};

static inline int coord_x(int idx) noexcept { return static_cast<int>(idx / WORLD_WIDTH); }
static inline int coord_y(int idx) noexcept { return static_cast<int>(idx % WORLD_WIDTH); }

// Helper: check if tile is near water (within distance d)
static inline bool is_near_water(const TerrainType* relief, int idx, int distance) noexcept
{
    if (!relief) return false;
    const int cx = coord_x(idx);
    const int cy = coord_y(idx);
    const int d2 = distance * distance;
    for (int dx = -distance; dx <= distance; ++dx)
    {
        for (int dy = -distance; dy <= distance; ++dy)
        {
            if (dx*dx + dy*dy > d2) continue;
            const int wx = wrap_coord(cx + dx, WORLD_WIDTH);
            const int wy = wrap_coord(cy + dy, WORLD_WIDTH);
            const int check_idx = wx * WORLD_WIDTH + wy;
            if (relief[check_idx] == TerrainType::Water) return true;
        }
    }
    return false;
}

// Helper: count adjacent water tiles for shore detection
static inline int count_adjacent_water(const TerrainType* relief, int idx) noexcept
{
    if (!relief) return 0;
    const int cx = coord_x(idx);
    const int cy = coord_y(idx);
    int water_count = 0;
    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            if (dx == 0 && dy == 0) continue;
            const int wx = wrap_coord(cx + dx, WORLD_WIDTH);
            const int wy = wrap_coord(cy + dy, WORLD_WIDTH);
            const int check_idx = wx * WORLD_WIDTH + wy;
            if (relief[check_idx] == TerrainType::Water) water_count++;
        }
    }
    return water_count;
}

// Helper: check if terrain is mountain-like (good for iron)
static inline bool is_mountain_terrain(TerrainType t) noexcept
{
    return t == TerrainType::Mount || t == TerrainType::Snow;
}

// Helper: add cluster with radial attenuation
static inline void apply_cluster(const TerrainType* relief,
                                std::uint8_t* out_map,
                                int center_idx,
                                int radius,
                                int center_value,
                                bool skip_water = true) noexcept
{
    if (radius <= 0) return;
    const int cx = coord_x(center_idx);
    const int cy = coord_y(center_idx);
    const int r2 = radius * radius;
    const int x0 = cx - radius;
    const int x1 = cx + radius;
    const int y0 = cy - radius;
    const int y1 = cy + radius;

    for (int x = x0; x <= x1; ++x)
    {
        for (int y = y0; y <= y1; ++y)
        {
            const int wx = wrap_coord(x, WORLD_WIDTH);
            const int wy = wrap_coord(y, WORLD_WIDTH);
            const int idx = wx * WORLD_WIDTH + wy;
            if (skip_water && relief[idx] == TerrainType::Water) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int dist2 = dx*dx + dy*dy;
            if (dist2 > r2) continue;
            const double dist = std::sqrt(static_cast<double>(dist2));
            double attenuation = 1.0 - (dist / static_cast<double>(radius));
            if (attenuation < 0.0) attenuation = 0.0;
            int val = static_cast<int>(std::round(center_value * attenuation));
            if (val < 0) val = 0;
            if (val > static_cast<int>(out_map[idx])) out_map[idx] = static_cast<std::uint8_t>(std::min(255, val));
        }
    }
}

void generate_iron_map(const TerrainType* relief,
                       std::uint8_t* out_map,
                       std::size_t size,
                       rng_t& rng,
                       const ResourceConfig& cfg) noexcept;

void generate_clay_map(const TerrainType* relief,
                       std::uint8_t* out_map,
                       std::size_t size,
                       rng_t& rng,
                       const ResourceConfig& cfg) noexcept;

void generate_fertility_map(const TerrainType* relief,
                            std::uint8_t* out_map,
                            std::size_t size,
                            rng_t& rng,
                            const ResourceConfig& cfg) noexcept;

} // namespace resource

