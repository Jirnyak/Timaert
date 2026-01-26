#include "systems/resource_generator.h"
#include <vector>
#include <algorithm>

namespace resource {

void generate_iron_map(const TerrainType* relief,
                       std::uint8_t* out_map,
                       std::size_t size,
                       rng_t& rng,
                       const ResourceConfig& cfg) noexcept {
    if (!relief || !out_map || size == 0)
        return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    std::vector<int> mount_positions;
    mount_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i) {
        if (is_mountain_terrain(relief[i]))
            mount_positions.push_back(static_cast<int>(i));
    }

    auto rand_u32 = [&](std::uint32_t m) { return random_u32_inclusive(rng, m); };

    const int mount_seed_count = (cfg.seed_count * 80) / 100;
    for (int s = 0; s < mount_seed_count && !mount_positions.empty(); ++s) {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(mount_positions.size() - 1));
        const int center_value = 240 + static_cast<int>(rand_u32(15));
        const int radius = cfg.cluster_radius + 2 + static_cast<int>(rand_u32(5));
        apply_cluster(relief, out_map, mount_positions[idx], radius, center_value, true);
    }

    for (int s = mount_seed_count; s < cfg.seed_count; ++s) {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20) {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water) {
                if (is_near_water(relief, candidate, 2) && rand_u32(99) < 40) {
                    attempts++;
                    continue;
                }
                chosen = candidate;
            }
            attempts++;
        }
        if (chosen >= 0) {
            const int center_value = 120 + static_cast<int>(rand_u32(60));
            const int radius = cfg.cluster_radius;
            apply_cluster(relief, out_map, chosen, radius, center_value, true);
        }
    }

    if (cfg.sprinkle_fraction > 0.0) {
        const std::size_t expected =
            static_cast<std::size_t>(cfg.sprinkle_fraction * 0.3 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i) {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water)
                continue;

            int tiny;
            if (is_mountain_terrain(relief[pos]))
                tiny = 15 + static_cast<int>(rand_u32(25));
            else
                tiny = static_cast<int>(rand_u32(15));

            if (tiny > static_cast<int>(out_map[pos]))
                out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }
}

void generate_clay_map(const TerrainType* relief,
                       std::uint8_t* out_map,
                       std::size_t size,
                       rng_t& rng,
                       const ResourceConfig& cfg) noexcept {
    if (!relief || !out_map || size == 0)
        return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    auto rand_u32 = [&](std::uint32_t m) { return random_u32_inclusive(rng, m); };

    std::vector<int> shore_positions;
    shore_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i) {
        if (relief[i] != TerrainType::Water) {
            int water_adjacent = count_adjacent_water(relief, static_cast<int>(i));
            if (water_adjacent > 0)
                shore_positions.push_back(static_cast<int>(i));
        }
    }

    std::vector<int> water_near_positions;
    water_near_positions.reserve(2048);
    for (std::size_t i = 0; i < size; ++i) {
        if (relief[i] != TerrainType::Water && is_near_water(relief, static_cast<int>(i), 3)) {
            if (count_adjacent_water(relief, static_cast<int>(i)) == 0)
                water_near_positions.push_back(static_cast<int>(i));
        }
    }

    const int shore_seed_count = (cfg.seed_count * 70) / 100;
    for (int s = 0; s < shore_seed_count && !shore_positions.empty(); ++s) {
        const std::uint32_t idx = rand_u32(static_cast<std::uint32_t>(shore_positions.size() - 1));
        const int center_value = 220 + static_cast<int>(rand_u32(35));
        const int radius = cfg.cluster_radius + static_cast<int>(rand_u32(2));
        apply_cluster(relief, out_map, shore_positions[idx], radius, center_value);
    }

    const int water_seed_count = shore_seed_count + (cfg.seed_count * 20) / 100;
    for (int s = shore_seed_count; s < water_seed_count && !water_near_positions.empty(); ++s) {
        const std::uint32_t idx =
            rand_u32(static_cast<std::uint32_t>(water_near_positions.size() - 1));
        const int center_value = 150 + static_cast<int>(rand_u32(60));
        const int radius = cfg.cluster_radius;
        apply_cluster(relief, out_map, water_near_positions[idx], radius, center_value);
    }

    for (int s = water_seed_count; s < cfg.seed_count; ++s) {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20) {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water)
                chosen = candidate;
            attempts++;
        }
        if (chosen >= 0) {
            const int center_value = 60 + static_cast<int>(rand_u32(80));
            const int radius = cfg.cluster_radius - 1;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    if (cfg.sprinkle_fraction > 0.0) {
        const std::size_t expected =
            static_cast<std::size_t>(cfg.sprinkle_fraction * 0.8 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i) {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water)
                continue;

            int tiny;
            if (count_adjacent_water(relief, pos) > 0)
                tiny = 20 + static_cast<int>(rand_u32(30));
            else if (is_near_water(relief, pos, 3))
                tiny = 10 + static_cast<int>(rand_u32(20));
            else
                tiny = static_cast<int>(rand_u32(10));

            if (tiny > static_cast<int>(out_map[pos]))
                out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (relief[i] == TerrainType::Water)
            out_map[i] = 0;
    }
}

void generate_fertility_map(const TerrainType* relief,
                            std::uint8_t* out_map,
                            std::size_t size,
                            rng_t& rng,
                            const ResourceConfig& cfg) noexcept {
    if (!relief || !out_map || size == 0)
        return;
    std::fill_n(out_map, size, static_cast<std::uint8_t>(0));

    auto rand_u32 = [&](std::uint32_t m) { return random_u32_inclusive(rng, m); };

    std::vector<int> fertile_positions;
    fertile_positions.reserve(4096);
    for (std::size_t i = 0; i < size; ++i) {
        if (relief[i] == TerrainType::Grass || relief[i] == TerrainType::Dirt)
            fertile_positions.push_back(static_cast<int>(i));
    }

    const int fertile_seed_count = (cfg.seed_count * 60) / 100;
    for (int s = 0; s < fertile_seed_count && !fertile_positions.empty(); ++s) {
        const std::uint32_t idx =
            rand_u32(static_cast<std::uint32_t>(fertile_positions.size() - 1));
        const int center_value = 200 + static_cast<int>(rand_u32(55));
        const int radius = cfg.cluster_radius + 1 + static_cast<int>(rand_u32(3));
        apply_cluster(relief, out_map, fertile_positions[idx], radius, center_value);
    }

    for (int s = fertile_seed_count; s < fertile_seed_count + (cfg.seed_count * 30) / 100; ++s) {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20) {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water && !is_mountain_terrain(relief[candidate])
                && relief[candidate] != TerrainType::Grass
                && relief[candidate] != TerrainType::Dirt) {
                chosen = candidate;
            }
            attempts++;
        }
        if (chosen >= 0) {
            const int center_value = 120 + static_cast<int>(rand_u32(60));
            const int radius = cfg.cluster_radius;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    for (int s = fertile_seed_count + (cfg.seed_count * 30) / 100; s < cfg.seed_count; ++s) {
        int chosen = -1;
        int attempts = 0;
        while (chosen < 0 && attempts < 20) {
            const int candidate = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[candidate] != TerrainType::Water && !is_mountain_terrain(relief[candidate]))
                chosen = candidate;
            attempts++;
        }
        if (chosen >= 0) {
            const int center_value = 70 + static_cast<int>(rand_u32(70));
            const int radius = cfg.cluster_radius - 1;
            apply_cluster(relief, out_map, chosen, radius, center_value);
        }
    }

    if (cfg.sprinkle_fraction > 0.0) {
        const std::size_t expected =
            static_cast<std::size_t>(cfg.sprinkle_fraction * 0.6 * static_cast<double>(size));
        for (std::size_t i = 0; i < expected; ++i) {
            const int pos = static_cast<int>(rand_u32(static_cast<std::uint32_t>(size - 1)));
            if (relief[pos] == TerrainType::Water || is_mountain_terrain(relief[pos]))
                continue;

            int tiny;
            if (relief[pos] == TerrainType::Grass || relief[pos] == TerrainType::Dirt)
                tiny = 20 + static_cast<int>(rand_u32(30));
            else
                tiny = 10 + static_cast<int>(rand_u32(20));

            if (tiny > static_cast<int>(out_map[pos]))
                out_map[pos] = static_cast<std::uint8_t>(tiny);
        }
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (relief[i] == TerrainType::Water || is_mountain_terrain(relief[i]))
            out_map[i] = 0;
    }
}

}  // namespace resource
