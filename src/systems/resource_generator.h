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

// Generate a resource strength map into `out_map` (size elements).
// `relief` is an array of TerrainType with WORLD_SIZE elements.
// `rng` is used for reproducible randomness.
static inline void generate_resource_map(const TerrainType* relief,
                                        std::uint8_t* out_map,
                                        std::size_t size,
                                        rng_t& rng,
                                        const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    // collect mountain candidates
    std::vector<int> mount_positions;
    mount_positions.reserve(1024);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Mount || relief[i] == TerrainType::Snow)
            mount_positions.push_back(static_cast<int>(i));
    }

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // helper: add cluster with radial attenuation
    auto apply_cluster = [&](int center_idx, int radius, int center_value){
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
    };

    // choose seeds
    for (int s = 0; s < cfg.seed_count; ++s)
    {
        int chosen = -1;
        if (!mount_positions.empty())
        {
            const std::uint32_t pick = rand_u32(99);
            if (pick < 80)
            {
                const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(mount_positions.size() - 1));
                chosen = mount_positions[idx];
            }
        }
        if (chosen < 0)
        {
            chosen = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
        }

        const int center_value = 200 + static_cast<int>(rand_u32(55)); // 200..255
        const int radius = cfg.cluster_radius + static_cast<int>(rand_u32(3));
        apply_cluster(chosen, radius, center_value);
    }

    // sprinkle tiny deposits randomly across the world (0..10)
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            const int tiny = static_cast<int>(rand_u32(10));
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    // clear underwater tiles
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Water) out_map[i] = 0;
    }
}

} // namespace resource

