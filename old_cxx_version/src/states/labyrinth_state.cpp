#include "states/labyrinth_state.h"

#include <print>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <utility>

#include "sokol_time.h"
#include "rendering/texture_manager.h"
#include "core/gfx_types.h"
#include "rendering/renderer.h"

void LabyrinthState::handle_click_move(GameContext& ctx, int screen_x, int screen_y) {
    const TileView view = make_tile_view(ctx, TILE_SIZE, 0, 0);
    const TilePosition target_tile = screen_to_world_pos(ctx, screen_x, screen_y, view);
    if (!is_valid(target_tile))
        return;
    if (is_wall(target_tile))
        return;
    if (target_tile == player_pos_)
        return;

    path_prev_.fill(INVALID_POS);
    path_queue_.clear();
    path_queue_.reserve(WORLD_SIZE);

    path_prev_[player_pos_] = player_pos_;
    path_queue_.push_back(player_pos_);
    std::size_t head = 0;
    bool found = false;

    while (head < path_queue_.size()) {
        const TilePosition pos = path_queue_[head++];
        if (pos == target_tile) {
            found = true;
            break;
        }
        for (int d = 0; d < 4; ++d) {
            const TilePosition n_tile = neighbor_from_pos(pos, static_cast<Direction>(d));
            if (!is_valid(n_tile))
                continue;
            if (is_wall(n_tile))
                continue;
            if (is_valid(path_prev_[n_tile]))
                continue;
            path_prev_[n_tile] = pos;
            path_queue_.push_back(n_tile);
        }
    }

    if (!found)
        return;

    path_.clear();
    TilePosition pos = target_tile;
    while (!(pos == player_pos_)) {
        path_.push_back(pos);
        pos = path_prev_[pos];
    }
    std::reverse(path_.begin(), path_.end());
    path_index_ = 0;
    last_move_ticks_ = static_cast<std::uint32_t>(stm_ms(stm_now()));
    ctx.redraw_requested = true;
}

void LabyrinthState::reveal_from_player() noexcept {
    if (!is_valid(player_pos_))
        return;
    const int px = static_cast<int>(player_pos_.x);
    const int py = static_cast<int>(player_pos_.y);
    for (int dy = -kRevealRadius; dy <= kRevealRadius; ++dy) {
        for (int dx = -kRevealRadius; dx <= kRevealRadius; ++dx) {
            if (dx * dx + dy * dy > kRevealRadius * kRevealRadius)
                continue;
            const TilePosition tile_pos{static_cast<std::uint16_t>(wrap_coord(px + dx)),
                                        static_cast<std::uint16_t>(wrap_coord(py + dy))};
            seen_[tile_pos] = 1;
        }
    }
}

void LabyrinthState::generate_labyrinth(GameContext& ctx) {
    const std::uint64_t start_ticks = stm_now();
    cells_.fill(CellType::Nothing);

    std::vector<TilePosition> sources;
    sources.reserve(WORLD_SIZE / 4);
    for (int y = 0; y < WORLD_WIDTH; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            if (x % kWallSpacing == 0 && y % kWallSpacing == 0) {
                const TilePosition pos{static_cast<std::uint16_t>(x),
                                       static_cast<std::uint16_t>(y)};
                cells_[pos] = CellType::Source;
                sources.push_back(pos);
            }
        }
    }

    std::vector<TilePosition> active_sources = sources;
    while (!active_sources.empty()) {
        std::vector<TilePosition> next_sources;
        next_sources.reserve(active_sources.size());
        for (const TilePosition pos : active_sources) {
            int wall_sum = 0;
            for (int d = 0; d < 4; ++d) {
                const TilePosition n = neighbor_from_pos(pos, static_cast<Direction>(d));
                if (cells_[n] == CellType::Wall)
                    ++wall_sum;
            }

            if (wall_sum < 2) {
                int drop = static_cast<int>(random_u32_inclusive(ctx.rng, 3));
                const TilePosition next = neighbor_from_pos(pos, static_cast<Direction>(drop));
                if (cells_[next] == CellType::Wall)
                    drop = (drop + 1) & 3;

                TilePosition cella = pos;
                for (int j = 0; j < kWallSpacing - 1; ++j) {
                    const TilePosition n = neighbor_from_pos(cella, static_cast<Direction>(drop));
                    cells_[n] = (j + 1 == kWallSpacing / 2) ? CellType::Door : CellType::Wall;
                    cella = n;
                }
                next_sources.push_back(pos);
            }
        }
        active_sources = std::move(next_sources);
    }

    for (const TilePosition pos : sources) {
        if (cells_[pos] == CellType::Source)
            cells_[pos] = CellType::Wall;
    }

    TilePosition drop_pos = INVALID_POS;
    while (cells_[drop_pos] != CellType::Nothing) {
        const int drop_x = static_cast<int>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const int drop_y = static_cast<int>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        drop_pos =
            TilePosition{static_cast<std::uint16_t>(drop_x), static_cast<std::uint16_t>(drop_y)};
    }
    cells_[drop_pos] = CellType::Test;

    std::vector<TilePosition> queue;
    queue.reserve(WORLD_SIZE);
    std::size_t head = 0;
    queue.push_back(drop_pos);

    std::vector<TilePosition> door_candidates;
    door_candidates.reserve(WORLD_SIZE / 16);

    while (true) {
        int fil = 0;
        door_candidates.clear();

        while (head < queue.size()) {
            const TilePosition pos = queue[head++];
            for (int d = 0; d < 4; ++d) {
                const Direction dir = static_cast<Direction>(d);
                const TilePosition n = neighbor_from_pos(pos, dir);
                if (cells_[n] == CellType::Nothing) {
                    cells_[n] = CellType::Test;
                    queue.push_back(n);
                    ++fil;
                }

                if (cells_[n] == CellType::Door) {
                    const TilePosition beyond = neighbor_from_pos(n, dir);
                    if (cells_[beyond] == CellType::Nothing) {
                        door_candidates.push_back(n);
                    }
                }
            }
        }

        if (fil > 0)
            continue;

        if (door_candidates.empty()) {
            for (int y = 0; y < WORLD_WIDTH; ++y) {
                for (int x = 0; x < WORLD_WIDTH; ++x) {
                    const TilePosition pos{static_cast<std::uint16_t>(x),
                                           static_cast<std::uint16_t>(y)};
                    if (cells_[pos] != CellType::Test)
                        continue;
                    for (int d = 0; d < 4; ++d) {
                        const Direction dir = static_cast<Direction>(d);
                        const TilePosition n = neighbor_from_pos(pos, dir);
                        if (cells_[n] != CellType::Door)
                            continue;
                        const TilePosition beyond = neighbor_from_pos(n, dir);
                        if (cells_[beyond] == CellType::Nothing) {
                            door_candidates.push_back(n);
                        }
                    }
                }
            }
        }

        if (door_candidates.empty())
            break;

        bool opened = false;
        for (const TilePosition door : door_candidates) {
            if (random_u32_inclusive(ctx.rng, 10) == 0) {
                cells_[door] = CellType::Test;
                queue.push_back(door);
                opened = true;
            }
        }
        if (!opened) {
            const TilePosition door = door_candidates.front();
            cells_[door] = CellType::Test;
            queue.push_back(door);
        }
    }

    for (int y = 0; y < WORLD_WIDTH; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            const TilePosition pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            if (cells_[pos] == CellType::Test)
                cells_[pos] = CellType::Nothing;
            else if (cells_[pos] == CellType::Door)
                cells_[pos] = CellType::Wall;
        }
    }

    for (int y = 0; y < WORLD_WIDTH; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            const TilePosition pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            if (cells_[pos] != CellType::Nothing)
                continue;

            const TilePosition up = neighbor_from_pos(pos, Direction::Up);
            const TilePosition down = neighbor_from_pos(pos, Direction::Down);
            const TilePosition left = neighbor_from_pos(pos, Direction::Left);
            const TilePosition right = neighbor_from_pos(pos, Direction::Right);

            const bool vertical = cells_[up] == CellType::Wall && cells_[down] == CellType::Wall;
            const bool horizontal =
                cells_[left] == CellType::Wall && cells_[right] == CellType::Wall;

            if (vertical || horizontal)
                cells_[pos] = CellType::Door;
        }
    }

    const std::uint64_t end_ticks = stm_now();
    const double freq = static_cast<double>(1000000000ULL);
    const double elapsed_ms = (static_cast<double>(end_ticks - start_ticks) * 1000.0) / freq;
    std::println("[labyrinth] generation time: {:.2f} ms", elapsed_ms);
}

void LabyrinthState::ensure_generated(GameContext& ctx) {
    if (initialized_)
        return;
    generate_labyrinth(ctx);
    seen_.fill(0);

    while (!is_valid(player_pos_)) {
        const auto drop_x = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const auto drop_y = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const TilePosition drop_pos{drop_x, drop_y};
        const CellType t = cells_[drop_pos];
        if (t == CellType::Nothing || t == CellType::Door) {
            player_pos_ = drop_pos;
        }
    }

    ensure_player_exit(ctx);
    cam_pos_ = player_pos_;
    freecam_ = false;
    reveal_from_player();
    initialized_ = true;
    ctx.redraw_requested = true;
}

void LabyrinthState::ensure_player_exit(GameContext& ctx) {
    if (!is_valid(player_pos_))
        return;

    const std::array<Direction, 4> dirs = {Direction::Up,
                                           Direction::Left,
                                           Direction::Down,
                                           Direction::Right};
    bool has_exit = false;
    for (const Direction dir : dirs) {
        const TilePosition next_pos = neighbor_from_pos(player_pos_, dir);
        if (!is_wall(next_pos)) {
            has_exit = true;
            break;
        }
    }
    if (has_exit)
        return;

    int best_len = std::numeric_limits<int>::max();
    Direction best_dir = Direction::Up;
    for (const Direction dir : dirs) {
        TilePosition pos = player_pos_;
        int steps = 0;
        while (steps < kWallSpacing * 3) {
            pos = neighbor_from_pos(pos, dir);
            ++steps;
            if (!is_wall(pos)) {
                if (steps < best_len) {
                    best_len = steps;
                    best_dir = dir;
                }
                break;
            }
        }
    }

    const int carve_steps = best_len == std::numeric_limits<int>::max() ? 1 : best_len;
    TilePosition pos = player_pos_;
    for (int i = 0; i < carve_steps; ++i) {
        pos = neighbor_from_pos(pos, best_dir);
        cells_[pos] = CellType::Nothing;
    }

    ctx.redraw_requested = true;
}

void LabyrinthState::move_player(Direction dir, GameContext& ctx) {
    const TilePosition next_tile = neighbor_from_pos(player_pos_, dir);
    if (is_valid(next_tile) && !is_wall(next_tile)) {
        player_pos_ = next_tile;
        reveal_from_player();
        if (!freecam_)
            cam_pos_ = player_pos_;
        ctx.redraw_requested = true;
    }
}

void LabyrinthState::init_buttons(GameContext& ctx) {
    ui_init_speed_buttons(speed_buttons_, ctx, 2);
    ui_init_move_buttons(
        move_buttons_,
        ctx,
        [this]() { request_move(Direction::Up); },
        [this]() { request_move(Direction::Left); },
        [this]() { request_center(); },
        [this]() { request_move(Direction::Right); },
        [this]() { request_move(Direction::Down); });

    buttons_initialized_ = true;
}

void LabyrinthState::update(GameContext& ctx, TextureManager& /*textures*/) {
    ensure_generated(ctx);
    if (!buttons_initialized_)
        init_buttons(ctx);
    
    // Handle click events
    if (ctx.picked) {
        const int x = ctx.pick_x;
        const int y = ctx.pick_y;
        
        // Check button clicks first
        bool handled = speed_buttons_.handle_press(x, y);
        if (!handled) handled = move_buttons_.handle_press(x, y);
        if (!handled) handled = ctx.ui_hit_test.contains(x, y);

        // Click on map - handle click to move
        if (!handled) {
            handle_click_move(ctx, x, y);
        }
    }
    const TilePosition prev_cam = cam_pos_;
    cam_pos_ = player_pos_;

    if (center_pending_) {
        cam_pos_ = player_pos_;
        center_pending_ = false;
        ctx.redraw_requested = true;
    }

    if (!ctx.paused) {
        // Handle arrow key movement
        if (ctx.key_up) {
            pending_move_dir_ = Direction::Up;
            move_pending_ = true;
            ctx.key_up = false;
        } else if (ctx.key_down) {
            pending_move_dir_ = Direction::Down;
            move_pending_ = true;
            ctx.key_down = false;
        } else if (ctx.key_left) {
            pending_move_dir_ = Direction::Left;
            move_pending_ = true;
            ctx.key_left = false;
        } else if (ctx.key_right) {
            pending_move_dir_ = Direction::Right;
            move_pending_ = true;
            ctx.key_right = false;
        }

        if (move_pending_) {
            move_player(pending_move_dir_, ctx);
            move_pending_ = false;
        }

        const int speed = std::max(1, ctx.speed());
        const std::uint32_t now_ticks = static_cast<std::uint32_t>(stm_ms(stm_now()));
        const std::uint32_t step_delay =
            std::max(1u, kMoveDelayMs / static_cast<std::uint32_t>(speed));
        if (path_index_ < path_.size() && now_ticks - last_move_ticks_ >= step_delay) {
            const std::size_t steps = static_cast<std::size_t>(speed);
            for (std::size_t step = 0; step < steps && path_index_ < path_.size(); ++step) {
                player_pos_ = path_[path_index_++];
            }
            last_move_ticks_ = now_ticks;
            reveal_from_player();
            if (!freecam_)
                cam_pos_ = player_pos_;
            if (path_index_ >= path_.size()) {
                path_.clear();
                path_index_ = 0;
            }
            ctx.redraw_requested = true;
        }
    }

    if (!(cam_pos_ == prev_cam))
        ctx.redraw_requested = true;
}

void LabyrinthState::render(GameContext& ctx, TextureManager& textures) {
    ensure_generated(ctx);
    ui_clear_black();

    const int tile_size = TILE_SIZE;
    const TileView view = make_tile_view(ctx, tile_size, 0, 0);
    auto neighbor = [](TilePosition pos, Direction dir) { return neighbor_from_pos(pos, dir); };

    for_each_visible_tile(
        cam_pos_,
        view,
        neighbor,
        [&](TilePosition tile_pos, const Rect& draw_tile) {
            if (draw_tile.x + tile_size > 0 && draw_tile.x < ctx.window_width
                && draw_tile.y + tile_size > 0 && draw_tile.y < ctx.window_height) {
                if (seen_[tile_pos] != 0) {
                    const CellType cell = cells_[tile_pos];
                    const TerrainType base_tile =
                        (cell == CellType::Wall) ? TerrainType::Mount : TerrainType::Dirt;
                    render_texture(textures.tile(base_tile), draw_tile);
                    if (cell == CellType::Door) {
                        render_texture(textures.sprite(static_cast<std::size_t>(ObjectType::Door)), draw_tile);
                    }
                } else {
                    render_fill_rect( draw_tile, ui_color("#000000"));
                }
            }
        });

    if (!ctx.ui_hit_test.contains(ctx.curs_x, ctx.curs_y)) {
        if (is_valid(hover_pos_) && seen_[hover_pos_] != 0) {
            const int offset_x =
                (static_cast<int>(hover_pos_.x) - static_cast<int>(cam_pos_.x)) * tile_size;
            const int offset_y =
                (static_cast<int>(hover_pos_.y) - static_cast<int>(cam_pos_.y)) * tile_size;
            const int center_x = ctx.window_width / 2 - tile_size / 2;
            const int center_y = ctx.window_height / 2 - tile_size / 2;
            const Rect hover_rect{center_x + offset_x, center_y + offset_y, tile_size, tile_size};
            render_fill_rect( hover_rect, ui_color("#FFFFFF28"));
            render_draw_rect( hover_rect, ui_color("#FFFFFF8C"));
        }
    }

    if (is_valid(player_pos_) && seen_[player_pos_] != 0) {
        const int offset_x =
            (static_cast<int>(player_pos_.x) - static_cast<int>(cam_pos_.x)) * tile_size;
        const int offset_y =
            (static_cast<int>(player_pos_.y) - static_cast<int>(cam_pos_.y)) * tile_size;
        const int center_x = ctx.window_width / 2 - tile_size / 2;
        const int center_y = ctx.window_height / 2 - tile_size / 2;
        const Rect player_rect{center_x + offset_x, center_y + offset_y, tile_size, tile_size};
        render_texture(textures.sprite(static_cast<std::size_t>(ObjectType::Player)), player_rect);
    }

    if (buttons_initialized_) {
        speed_buttons_.render(ctx);
        move_buttons_.render(ctx);
    }
}
