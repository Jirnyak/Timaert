#pragma once

#include "core/game_context.h"
#include "core/types.h"
#include "tile_view.h"
#include <unordered_map>
#include <cstdint>

// Hybrid LOD System:
// - At high zoom (scaled_size >= 8): Render individual tiles
// - At medium zoom (scaled_size 4-8): Render 2x2 grouped tiles with dominant sampling
// - At low zoom (scaled_size < 4): Render 4x4+ grouped tiles with dominant sampling

struct LODConfig {
    int tile_size;            // Scaled tile size from current zoom
    int group_size;           // How many tiles to group together (1, 2, 4, 8, etc.)
    bool use_grouped_render;  // Whether to use grouping or single-texture fallback
};

[[nodiscard]] inline LODConfig calculate_lod_config(int scaled_tile_size) noexcept {
    LODConfig cfg;
    cfg.tile_size = scaled_tile_size;

    // Determine grouping based on scaled tile size
    if (scaled_tile_size >= 8) {
        cfg.group_size = 1;
        cfg.use_grouped_render = false;  // Render individual tiles
    } else if (scaled_tile_size >= 4) {
        cfg.group_size = 2;
        cfg.use_grouped_render = true;  // 2x2 grouping
    } else if (scaled_tile_size >= 2) {
        cfg.group_size = 4;
        cfg.use_grouped_render = true;  // 4x4 grouping
    } else {
        cfg.group_size = 8;
        cfg.use_grouped_render = true;  // 8x8 grouping or fallback to whole map
    }

    return cfg;
}

// Find the most dominant terrain in a group of tiles
template <typename NeighborFn>
[[nodiscard]] inline TerrainType get_dominant_terrain(TilePosition start_pos,
                                                      int group_size,
                                                      const WorldMap<TerrainType>& relief,
                                                      NeighborFn&& neighbor) noexcept {
    // Count terrain types in the group
    int terrain_counts[static_cast<int>(TerrainType::Count)] = {0};

    TilePosition row_start = start_pos;
    for (int y = 0; y < group_size; ++y) {
        TilePosition pos = row_start;
        for (int x = 0; x < group_size; ++x) {
            if (is_valid(pos)) {
                int terrain_idx = static_cast<int>(relief[pos]);
                if (terrain_idx >= 0 && terrain_idx < static_cast<int>(TerrainType::Count)) {
                    terrain_counts[terrain_idx]++;
                }
            }
            pos = neighbor(pos, Direction::Right);
        }
        row_start = neighbor(row_start, Direction::Down);
    }

    // Find dominant terrain
    int dominant_idx = 0;
    int dominant_count = 0;
    for (int i = 0; i < static_cast<int>(TerrainType::Count); ++i) {
        if (terrain_counts[i] > dominant_count) {
            dominant_count = terrain_counts[i];
            dominant_idx = i;
        }
    }

    return static_cast<TerrainType>(dominant_idx);
}

// Check if a group has significant flora (trees)
template <typename NeighborFn>
[[nodiscard]] inline bool group_has_flora(TilePosition start_pos,
                                          int group_size,
                                          const WorldMap<std::uint8_t>& flora,
                                          int flora_threshold,
                                          NeighborFn&& neighbor) noexcept {
    TilePosition row_start = start_pos;
    for (int y = 0; y < group_size; ++y) {
        TilePosition pos = row_start;
        for (int x = 0; x < group_size; ++x) {
            if (is_valid(pos) && flora[pos] > flora_threshold) {
                return true;
            }
            pos = neighbor(pos, Direction::Right);
        }
        row_start = neighbor(row_start, Direction::Down);
    }
    return false;
}

// Render grouped tiles with dominant sampling
template <typename NeighborFn, typename Fn>
inline void for_each_grouped_tile(TilePosition cam_pos,
                                  int group_size,
                                  const TileView& view,
                                  NeighborFn&& neighbor,
                                  Fn&& fn) {
    // Calculate how many groups fit on screen
    int groups_x = (view.tiles_x + group_size - 1) / group_size;
    int groups_y = (view.tiles_y + group_size - 1) / group_size;

    // Start position: offset by (groups / 2) groups, not tiles
    TilePosition row_start = move_pos(cam_pos,
                                      -(groups_x / 2) * group_size,
                                      -(groups_y / 2) * group_size,
                                      std::forward<NeighborFn>(neighbor));

    int draw_y = view.base_y;

    for (int gy = 0; gy < groups_y; ++gy) {
        TilePosition tile_pos = row_start;
        int draw_x = view.base_x;

        for (int gx = 0; gx < groups_x; ++gx) {
            SDL_Rect draw_tile{draw_x, draw_y, view.tile_size * 2, view.tile_size * 2};
            fn(tile_pos, draw_tile, group_size);

            // Move to next group horizontally
            for (int i = 0; i < group_size; ++i) {
                tile_pos = neighbor(tile_pos, Direction::Right);
            }
            draw_x += view.tile_size * 2;
        }

        // Move to next group row vertically
        TilePosition advance_pos = row_start;
        for (int i = 0; i < group_size; ++i) {
            advance_pos = neighbor(advance_pos, Direction::Down);
        }
        row_start = advance_pos;
        draw_y += view.tile_size * 2;
    }
}
