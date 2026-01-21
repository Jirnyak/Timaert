#pragma once

#include "core/game_context.h"

#include <algorithm>

struct TileView
{
    int tile_size = 0;
    int tiles_x = 0;
    int tiles_y = 0;
    int base_x = 0;
    int base_y = 0;
    int pixel_offset_x = 0;
    int pixel_offset_y = 0;
};

[[nodiscard]] inline int scaled_tile_size(int base_tile_size, float zoom) noexcept
{
    const int size = static_cast<int>(static_cast<float>(base_tile_size) * zoom);
    return std::max(1, size);
}

[[nodiscard]] inline TileView make_tile_view(const GameContext& ctx,
                                             int tile_size,
                                             int pixel_offset_x,
                                             int pixel_offset_y) noexcept
{
    TileView view{};
    view.tile_size = tile_size;
    view.tiles_x = (ctx.window_width / tile_size) + 3;
    view.tiles_y = (ctx.window_height / tile_size) + 3;
    view.base_x = ctx.window_width / 2 + pixel_offset_x - (view.tiles_x / 2) * tile_size - tile_size / 2;
    view.base_y = ctx.window_height / 2 + pixel_offset_y - (view.tiles_y / 2) * tile_size - tile_size / 2;
    view.pixel_offset_x = pixel_offset_x;
    view.pixel_offset_y = pixel_offset_y;
    return view;
}

[[nodiscard]] inline int tile_offset_from_diff(int diff, int tile_size) noexcept
{
    if (diff >= 0) return diff / tile_size;
    return -(((-diff - 1) / tile_size) + 1);
}

[[nodiscard]] inline SDL_Rect centered_rect(const GameContext& ctx,
                                            int width,
                                            int height,
                                            int pixel_offset_x,
                                            int pixel_offset_y) noexcept
{
    return SDL_Rect{
        ctx.window_width / 2 - width / 2 + pixel_offset_x,
        ctx.window_height / 2 - height / 2 + pixel_offset_y,
        width,
        height
    };
}

template <typename NeighborFn>
[[nodiscard]] inline int move_pos(int pos,
                                  int offset_x,
                                  int offset_y,
                                  NeighborFn&& neighbor)
{
    if (offset_x > 0) {
        for (int i = 0; i < offset_x; ++i)
            pos = neighbor(pos, Direction::Right);
    } else {
        for (int i = 0; i < -offset_x; ++i)
            pos = neighbor(pos, Direction::Left);
    }

    if (offset_y > 0) {
        for (int i = 0; i < offset_y; ++i)
            pos = neighbor(pos, Direction::Down);
    } else {
        for (int i = 0; i < -offset_y; ++i)
            pos = neighbor(pos, Direction::Up);
    }

    return pos;
}

template <typename NeighborFn>
[[nodiscard]] inline int screen_to_world_pos(const GameContext& ctx,
                                             int screen_x,
                                             int screen_y,
                                             int cam_pos,
                                             const TileView& view,
                                             NeighborFn&& neighbor)
{
    const int center_screen_x = ctx.window_width / 2 + view.pixel_offset_x - view.tile_size / 2;
    const int center_screen_y = ctx.window_height / 2 + view.pixel_offset_y - view.tile_size / 2;

    const int diff_x = screen_x - center_screen_x;
    const int diff_y = screen_y - center_screen_y;

    const int tile_offset_x = tile_offset_from_diff(diff_x, view.tile_size);
    const int tile_offset_y = tile_offset_from_diff(diff_y, view.tile_size);

    return move_pos(cam_pos, tile_offset_x, tile_offset_y, std::forward<NeighborFn>(neighbor));
}

template <typename NeighborFn, typename Fn>
inline void for_each_visible_tile(int cam_pos,
                                  const TileView& view,
                                  NeighborFn&& neighbor,
                                  Fn&& fn)
{
    int row_start_idx = move_pos(cam_pos,
                                 -(view.tiles_x / 2),
                                 -(view.tiles_y / 2),
                                 std::forward<NeighborFn>(neighbor));
    int draw_y = view.base_y;

    for (int a = 0; a < view.tiles_y; ++a)
    {
        int pos_line_idx = row_start_idx;
        int draw_x = view.base_x;
        for (int b = 0; b < view.tiles_x; ++b)
        {
            SDL_Rect draw_tile{draw_x, draw_y, view.tile_size, view.tile_size};
            fn(pos_line_idx, draw_tile);
            pos_line_idx = neighbor(pos_line_idx, Direction::Right);
            draw_x += view.tile_size;
        }
        row_start_idx = neighbor(row_start_idx, Direction::Down);
        draw_y += view.tile_size;
    }
}
