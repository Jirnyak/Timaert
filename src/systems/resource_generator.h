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

// Generate iron map: high on mountains, less in water, random scattered
static inline void generate_iron_map(const TerrainType* relief,
                                    std::uint8_t* out_map,
                                    std::size_t size,
                                    rng_t& rng,
                                    const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    // collect mountain candidates (strong preference for iron)
    std::vector<int> mount_positions;
    mount_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (is_mountain_terrain(relief[i]))
            mount_positions.push_back(static_cast<int>(i));
    }

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // 80% of seeds from mountains (high concentration)
    const int mount_seed_count = (cfg.seed_count * 80) / 100;
    for (int s = 0; s < mount_seed_count && !mount_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(mount_positions.size() - 1));
        const int center_value = 240 + static_cast<int>(rand_u32(15)); // 240..255 (very high on mountains)
        const int radius = cfg.cluster_radius + 2 + static_cast<int>(rand_u32(5)); // larger clusters
        apply_cluster(relief, out_map, mount_positions[idx], radius, center_value, true);
    }

    // 20% of seeds from non-water, non-mountain terrain (scattered deposits)
    for (int s = mount_seed_count; s < cfg.seed_count; ++s)
    {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20)
        {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            // Avoid water, but allow all other terrain types
            if (relief[candidate] != TerrainType::Water)
            {
                // Penalty chance if near water (reduce iron near shores)
                if (is_near_water(relief, candidate, 2) && rand_u32(99) < 40)
                {
                    attempts++;
                    continue; // 40% chance to skip water-adjacent tiles
                }
                chosen = candidate;
            }
            attempts++;
        }
        if (chosen >= 0)
        {
            const int center_value = 120 + static_cast<int>(rand_u32(60)); // 120..180 (moderate elsewhere)
            const int radius = cfg.cluster_radius;
            apply_cluster(relief, out_map, chosen, radius, center_value, true);
        }
    }

    // Random tiny iron deposits scattered across non-water terrain
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * 0.3 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water) continue;
            
            // Higher concentration on mountains even in sprinkles
            int tiny;
            if (is_mountain_terrain(relief[pos]))
                tiny = 15 + static_cast<int>(rand_u32(25)); // 15..40 on mountains
            else
                tiny = static_cast<int>(rand_u32(15)); // 0..15 elsewhere
            
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }
}

// Generate clay map: high on water shores, distributed near water.
static inline void generate_clay_map(const TerrainType* relief,
                                    std::uint8_t* out_map,
                                    std::size_t size,
                                    rng_t& rng,
                                    const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // Collect shore positions (tiles directly adjacent to water)
    std::vector<int> shore_positions;
    shore_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] != TerrainType::Water)
        {
            // Check if this is a shore tile (adjacent to water)
            int water_adjacent = count_adjacent_water(relief, static_cast<int>(i));
            if (water_adjacent > 0)
                shore_positions.push_back(static_cast<int>(i));
        }
    }

    // Also collect positions near water (within 3 tiles) for wider distribution
    std::vector<int> water_near_positions;
    water_near_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] != TerrainType::Water && is_near_water(relief, static_cast<int>(i), 3))
        {
            // Only add if not already a direct shore
            if (count_adjacent_water(relief, static_cast<int>(i)) == 0)
                water_near_positions.push_back(static_cast<int>(i));
        }
    }

    // 70% of seeds from direct shore positions (highest concentration)
    const int shore_seed_count = (cfg.seed_count * 70) / 100;
    for (int s = 0; s < shore_seed_count && !shore_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(shore_positions.size() - 1));
        const int center_value = 220 + static_cast<int>(rand_u32(35)); // 220..255 (very high on shores)
        const int radius = cfg.cluster_radius + static_cast<int>(rand_u32(2));
        apply_cluster(relief, out_map, shore_positions[idx], radius, center_value);
    }

    // 20% of seeds from near-water positions (secondary deposits)
    const int water_seed_count = shore_seed_count + (cfg.seed_count * 20) / 100;
    for (int s = shore_seed_count; s < water_seed_count && !water_near_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(water_near_positions.size() - 1));
        const int center_value = 150 + static_cast<int>(rand_u32(60)); // 150..210 (moderate near water)
        const int radius = cfg.cluster_radius;
        apply_cluster(relief, out_map, water_near_positions[idx], radius, center_value);
    }

    // 10% of seeds from anywhere (scattered clay inland)
    for (int s = water_seed_count; s < cfg.seed_count; ++s)
    {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20)
        {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water)
                chosen = candidate;
            attempts++;
        }
        if (chosen >= 0)
        {
            const int center_value = 60 + static_cast<int>(rand_u32(80)); // 60..140 (low inland)
            const int radius = cfg.cluster_radius - 1;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    // Random tiny clay deposits (more common on shores, less inland)
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * 0.8 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water) continue;
            
            // Higher concentration on shores
            int tiny;
            if (count_adjacent_water(relief, pos) > 0)
                tiny = 20 + static_cast<int>(rand_u32(30)); // 20..50 on shores
            else if (is_near_water(relief, pos, 3))
                tiny = 10 + static_cast<int>(rand_u32(20)); // 10..30 near water
            else
                tiny = static_cast<int>(rand_u32(10)); // 0..10 inland
            
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    // Ensure water tiles have no clay
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Water) out_map[i] = 0;
    }
}

// Generate fertility map: high on grass, low on mountains, uniform distribution
static inline void generate_fertility_map(const TerrainType* relief,
                                         std::uint8_t* out_map,
                                         std::size_t size,
                                         rng_t& rng,
                                         const ResourceConfig& cfg) noexcept
{
    if (!relief || !out_map || size == 0) return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    auto rand_u32 = [&](std::uint32_t m){ return random_u32_inclusive(rng, m); };

    // Collect grass/dirt positions (best for fertility)
    std::vector<int> fertile_positions;
    fertile_positions.reserve(4096);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Grass || relief[i] == TerrainType::Dirt)
            fertile_positions.push_back(static_cast<int>(i));
    }

    // 60% of seeds from grass/dirt (high fertility areas)
    const int fertile_seed_count = (cfg.seed_count * 60) / 100;
    for (int s = 0; s < fertile_seed_count && !fertile_positions.empty(); ++s)
    {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(fertile_positions.size() - 1));
        const int center_value = 200 + static_cast<int>(rand_u32(55)); // 200..255 (high on grass/dirt)
        const int radius = cfg.cluster_radius + 1 + static_cast<int>(rand_u32(3));
        apply_cluster(relief, out_map, fertile_positions[idx], radius, center_value);
    }

    // 30% of seeds from other non-mountain, non-water terrain (jungle, swamp, sand)
    for (int s = fertile_seed_count; s < fertile_seed_count + (cfg.seed_count * 30) / 100; ++s)
    {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20)
        {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water && 
                !is_mountain_terrain(relief[candidate]) &&
                relief[candidate] != TerrainType::Grass &&
                relief[candidate] != TerrainType::Dirt)
            {
                chosen = candidate;
            }
            attempts++;
        }
        if (chosen >= 0)
        {
            const int center_value = 120 + static_cast<int>(rand_u32(60)); // 120..180 (moderate elsewhere)
            const int radius = cfg.cluster_radius;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    // 10% of seeds scattered anywhere non-mountain
    for (int s = fertile_seed_count + (cfg.seed_count * 30) / 100; s < cfg.seed_count; ++s)
    {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20)
        {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water && !is_mountain_terrain(relief[candidate]))
                chosen = candidate;
            attempts++;
        }
        if (chosen >= 0)
        {
            const int center_value = 70 + static_cast<int>(rand_u32(70)); // 70..140 (low scattered)
            const int radius = cfg.cluster_radius - 1;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    // Moderate random tiny deposits (uniform distribution)
    if (cfg.sprinkle_fraction > 0.0)
    {
        const std::size_t expected = static_cast<std::size_t>(cfg.sprinkle_fraction * 0.6 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i)
        {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water || is_mountain_terrain(relief[pos])) continue;
            
            // Grass/Dirt get best fertility sprinkles
            int tiny;
            if (relief[pos] == TerrainType::Grass || relief[pos] == TerrainType::Dirt)
                tiny = 20 + static_cast<int>(rand_u32(30)); // 20..50 on grass/dirt
            else
                tiny = 10 + static_cast<int>(rand_u32(20)); // 10..30 elsewhere
            
            if (tiny > static_cast<int>(out_map[pos])) out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    // Zero out water and mountains
    for (std::size_t i = 0; i < size; ++i)
    {
        if (relief[i] == TerrainType::Water || is_mountain_terrain(relief[i])) 
            out_map[i] = 0;
    }
}

} // namespace resource

