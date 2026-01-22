#pragma once

#include <cstdint>
#include <cstddef>
#include "core/game_context.h"

// Header-only resource generator (keeps project style).
#include <vector>
#include <algorithm>
#include <cmath>

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

// Helper: add cluster with radial attenuation
static inline void apply_cluster(const TerrainType* relief,
                                std::uint8_t* out_map,
                                int center_idx,
                                int radius,
                                int center_value) noexcept
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

// Generate iron map: high on mountains, none in water.
static inline void generate_iron_map(const TerrainType* relief,
                                    std::uint8_t* out_map,
                                    std::size_t size,
                                    rng_t& rng,
                                    const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    // collect mountain candidates (high probability for iron)
    std::vector<int> mount_positions;
    mount_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Mount || relief[i] == TerrainType::Snow)
            mount_positions.push_back(static_cast<int>(i));
    }

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // 90% of seeds from mountains
    const int mount_seed_count = (cfg.seed_count * 9) / 10;
    for (int s = 0; s < mount_seed_count && !mount_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(mount_positions.size() - 1));
        const int center_value = 220 + static_cast<int>(rand_u32(35)); // 220..255 (higher than generic)
        const int radius = cfg.cluster_radius + 1 + static_cast<int>(rand_u32(5));
        apply_cluster(relief, out_map, mount_positions[idx], radius, center_value);
    }

    // 10% of seeds from anywhere (rarer)
    for (int s = mount_seed_count; s < cfg.seed_count; ++s)
    {
        const int chosen = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
        const int center_value = 150 + static_cast<int>(rand_u32(50));
        const int radius = cfg.cluster_radius;
        apply_cluster(relief, out_map, chosen, radius, center_value);
    }

    // sparse random tiny deposits
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * 0.3 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            const int tiny = static_cast<int>(rand_u32(15));
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    // clear underwater tiles
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Water) out_map[i] = 0;
    }
}

// Generate clay map: high near water, none in deep water.
static inline void generate_clay_map(const TerrainType* relief,
                                    std::uint8_t* out_map,
                                    std::size_t size,
                                    rng_t& rng,
                                    const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // collect positions near water
    std::vector<int> water_near_positions;
    water_near_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] != TerrainType::Water && is_near_water(relief, static_cast<int>(i), 3))
            water_near_positions.push_back(static_cast<int>(i));
    }

    // 85% of seeds from water-adjacent tiles
    const int water_seed_count = (cfg.seed_count * 85) / 100;
    for (int s = 0; s < water_seed_count && !water_near_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(water_near_positions.size() - 1));
        const int center_value = 210 + static_cast<int>(rand_u32(40)); // 210..250
        const int radius = cfg.cluster_radius + static_cast<int>(rand_u32(3));
        apply_cluster(relief, out_map, water_near_positions[idx], radius, center_value);
    }

    // 15% of seeds from anywhere
    for (int s = water_seed_count; s < cfg.seed_count; ++s)
    {
        const int chosen = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
        const int center_value = 100 + static_cast<int>(rand_u32(60));
        const int radius = cfg.cluster_radius;
        apply_cluster(relief, out_map, chosen, radius, center_value);
    }

    // moderate random tiny deposits
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * 0.7 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            const int tiny = static_cast<int>(rand_u32(20));
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    // clear water tiles
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Water) out_map[i] = 0;
    }
}

} // namespace resource

