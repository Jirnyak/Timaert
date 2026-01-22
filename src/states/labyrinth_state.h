
#pragma once

#include "core/game_state.h"
#include "rendering/texture_manager.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

class LabyrinthState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Labyrinth; }

private:
    enum class CellType : std::uint8_t {
        Nothing = 0,
        Wall,
        Door,
        Source,
        Test
    };

    std::vector<CellType> cells_;
    std::vector<std::uint8_t> seen_;
    std::vector<int> neighbors_;
    bool neighbors_ready_ = false;
    int player_pos_ = -1;
    int cam_pos_ = -1;
    bool initialized_ = false;
    bool freecam_ = false;
    std::vector<int> path_;
    std::vector<int> path_prev_;
    std::vector<int> path_queue_;
    std::size_t path_index_ = 0;
    std::uint32_t last_move_ticks_ = 0;
    UIButtonGroup speed_buttons_;
    UIButtonGroup move_buttons_;
    bool buttons_initialized_ = false;
    bool click_blocked_ = false;
    Direction pending_move_dir_ = Direction::Up;
    bool move_requested_ = false;
    bool center_requested_ = false;
    int hover_pos_ = -1;
    InputManager input_manager_;


    static constexpr std::uint32_t kMoveDelayMs = 80;

    static constexpr int kWallSpacing = 4;
    static constexpr int kRevealRadius = 6;

    [[nodiscard]] int to_pos(int x, int y) const noexcept
    {
        x = wrap_coord(x);
        y = wrap_coord(y);
        return x * WORLD_WIDTH + y;
    }

    void handle_click_move(GameContext& ctx, int screen_x, int screen_y)
    {
        const TileView view = make_tile_view(ctx, TILE_SIZE, 0, 0);
        const int target = screen_to_world_pos(ctx, screen_x, screen_y, view);
        if (target < 0 || target >= static_cast<int>(WORLD_SIZE)) return;
        if (is_wall(target)) return;
        if (target == player_pos_) return;

        if (path_prev_.empty())
        {
            path_prev_.assign(WORLD_SIZE, -1);
            path_queue_.reserve(WORLD_SIZE);
        }
        std::fill(path_prev_.begin(), path_prev_.end(), -1);
        path_queue_.clear();

        path_prev_[static_cast<std::size_t>(player_pos_)] = player_pos_;
        path_queue_.push_back(player_pos_);
        std::size_t head = 0;
        bool found = false;

        while (head < path_queue_.size())
        {
            const int pos = path_queue_[head++];
            if (pos == target) { found = true; break; }
            for (int s = 0; s < 4; ++s)
            {
                const int n = neighbors_[static_cast<std::size_t>(pos) * 4u + static_cast<std::size_t>(s)];
                if (is_wall(n)) continue;
                if (path_prev_[static_cast<std::size_t>(n)] != -1) continue;
                path_prev_[static_cast<std::size_t>(n)] = pos;
                path_queue_.push_back(n);
            }
        }

        if (!found) return;

        path_.clear();
        int pos = target;
        while (pos != player_pos_)
        {
            path_.push_back(pos);
            pos = path_prev_[static_cast<std::size_t>(pos)];
        }
        std::reverse(path_.begin(), path_.end());
        path_index_ = 0;
        last_move_ticks_ = SDL_GetTicks();
        ctx.redraw_requested = true;
    }

    void ensure_neighbors()
    {
        if (neighbors_ready_) return;
        neighbors_.resize(WORLD_SIZE * 4u);
        for (int x = 0; x < WORLD_WIDTH; ++x)
        {
            for (int y = 0; y < WORLD_WIDTH; ++y)
            {
                const int pos = x * WORLD_WIDTH + y;
                const int up = x * WORLD_WIDTH + wrap_coord(y - 1);
                const int down = x * WORLD_WIDTH + wrap_coord(y + 1);
                const int left = wrap_coord(x - 1) * WORLD_WIDTH + y;
                const int right = wrap_coord(x + 1) * WORLD_WIDTH + y;
                const std::size_t base = static_cast<std::size_t>(pos) * 4u;
                neighbors_[base + static_cast<std::size_t>(Direction::Up)] = up;
                neighbors_[base + static_cast<std::size_t>(Direction::Left)] = left;
                neighbors_[base + static_cast<std::size_t>(Direction::Down)] = down;
                neighbors_[base + static_cast<std::size_t>(Direction::Right)] = right;
            }
        }
        neighbors_ready_ = true;
    }

    [[nodiscard]] std::pair<int, int> to_xy(int pos) const noexcept
    {
        const int x = pos / WORLD_WIDTH;
        const int y = pos % WORLD_WIDTH;
        return {x, y};
    }

    [[nodiscard]] bool is_wall(int pos) const noexcept
    {
        const CellType t = cells_[static_cast<std::size_t>(pos)];
        return t == CellType::Wall;
    }

    [[nodiscard]] int screen_to_world_pos(const GameContext& ctx,
                                          int screen_x,
                                          int screen_y,
                                          const TileView& view) const
    {
        auto neighbor = [this](int pos, Direction dir) {
            return neighbors_[static_cast<std::size_t>(pos) * 4u + static_cast<std::size_t>(dir)];
        };
        return ::screen_to_world_pos(ctx, screen_x, screen_y, cam_pos_, view, neighbor);
    }

    void reveal_from_player() noexcept
    {
        if (player_pos_ < 0) return;
        const auto [px, py] = to_xy(player_pos_);
        for (int dy = -kRevealRadius; dy <= kRevealRadius; ++dy)
        {
            for (int dx = -kRevealRadius; dx <= kRevealRadius; ++dx)
            {
                if (dx * dx + dy * dy > kRevealRadius * kRevealRadius) continue;
                const int pos = to_pos(px + dx, py + dy);
                seen_[static_cast<std::size_t>(pos)] = 1;
            }
        }
    }

    void generate_labyrinth(GameContext& ctx)
    {
        const std::uint64_t start_ticks = SDL_GetPerformanceCounter();
        ensure_neighbors();
        cells_.assign(WORLD_SIZE, CellType::Nothing);
        CellType* cells = cells_.data();
        const auto neighbor = [this](int pos, Direction dir) {
            return neighbors_[static_cast<std::size_t>(pos) * 4u + static_cast<std::size_t>(dir)];
        };

        std::vector<int> sources;
        sources.reserve(WORLD_SIZE / 4);
        for (int y = 0; y < WORLD_WIDTH; ++y)
        {
            for (int x = 0; x < WORLD_WIDTH; ++x)
            {
                if (x % kWallSpacing == 0 && y % kWallSpacing == 0)
                {
                    const int pos = to_pos(x, y);
                    cells[static_cast<std::size_t>(pos)] = CellType::Source;
                    sources.push_back(pos);
                }
            }
        }

        std::vector<int> active_sources = sources;
        while (!active_sources.empty())
        {
            std::vector<int> next_sources;
            next_sources.reserve(active_sources.size());
            for (int idx : active_sources)
            {
                int wall_sum = 0;
                for (int s = 0; s < 4; ++s)
                {
                    const int n = neighbor(idx, static_cast<Direction>(s));
                    if (cells[static_cast<std::size_t>(n)] == CellType::Wall) ++wall_sum;
                }

                if (wall_sum < 2)
                {
                    int drop = static_cast<int>(random_u32_inclusive(ctx.rng, 3));
                    int next = neighbor(idx, static_cast<Direction>(drop));
                    if (cells[static_cast<std::size_t>(next)] == CellType::Wall)
                        drop = (drop + 1) & 3;

                    int cella = idx;
                    for (int j = 0; j < kWallSpacing - 1; ++j)
                    {
                        const int n = neighbor(cella, static_cast<Direction>(drop));
                        cells[static_cast<std::size_t>(n)] =
                            (j + 1 == kWallSpacing / 2) ? CellType::Door : CellType::Wall;
                        cella = n;
                    }
                    next_sources.push_back(idx);
                }
            }
            active_sources = std::move(next_sources);
        }

        for (int idx : sources)
        {
            if (cells[static_cast<std::size_t>(idx)] == CellType::Source)
                cells[static_cast<std::size_t>(idx)] = CellType::Wall;
        }

        int drop = 0;
        do {
            drop = static_cast<int>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_SIZE - 1)));
        } while (cells[static_cast<std::size_t>(drop)] != CellType::Nothing);
        cells[static_cast<std::size_t>(drop)] = CellType::Test;

        std::vector<int> queue;
        queue.reserve(WORLD_SIZE);
        std::size_t head = 0;
        queue.push_back(drop);
        cells[static_cast<std::size_t>(drop)] = CellType::Test;

        std::vector<int> door_candidates;
        door_candidates.reserve(WORLD_SIZE / 16);

        while (true)
        {
            int fil = 0;
            door_candidates.clear();

            while (head < queue.size())
            {
                const int idx = queue[head++];
                for (int s = 0; s < 4; ++s)
                {
                    const int n = neighbor(idx, static_cast<Direction>(s));
                    if (cells[static_cast<std::size_t>(n)] == CellType::Nothing)
                    {
                        cells[static_cast<std::size_t>(n)] = CellType::Test;
                        queue.push_back(n);
                        ++fil;
                    }

                    if (cells[static_cast<std::size_t>(n)] == CellType::Door)
                    {
                        const int beyond = neighbor(n, static_cast<Direction>(s));
                        if (cells[static_cast<std::size_t>(beyond)] == CellType::Nothing)
                        {
                            door_candidates.push_back(n);
                        }
                    }
                }
            }

            if (fil > 0)
                continue;

            if (door_candidates.empty())
            {
                for (std::size_t i = 0; i < cells_.size(); ++i)
                {
                    if (cells[i] != CellType::Test) continue;
                    for (int s = 0; s < 4; ++s)
                    {
                        const int n = neighbor(static_cast<int>(i), static_cast<Direction>(s));
                        if (cells[static_cast<std::size_t>(n)] != CellType::Door) continue;
                        const int beyond = neighbor(n, static_cast<Direction>(s));
                        if (cells[static_cast<std::size_t>(beyond)] == CellType::Nothing)
                        {
                            door_candidates.push_back(n);
                        }
                    }
                }
            }

            if (door_candidates.empty())
                break;

            bool opened = false;
            for (int door : door_candidates)
            {
                if (random_u32_inclusive(ctx.rng, 10) == 0)
                {
                    cells[static_cast<std::size_t>(door)] = CellType::Test;
                    queue.push_back(door);
                    opened = true;
                }
            }
            if (!opened)
            {
                const int door = door_candidates.front();
                cells[static_cast<std::size_t>(door)] = CellType::Test;
                queue.push_back(door);
            }
        }

        for (std::size_t i = 0; i < cells_.size(); ++i)
        {
            if (cells[i] == CellType::Test)
                cells[i] = CellType::Nothing;
            else if (cells[i] == CellType::Door)
                cells[i] = CellType::Wall;
        }

        for (int i = 0; i < WORLD_WIDTH * WORLD_WIDTH; ++i)
        {
            if (cells[static_cast<std::size_t>(i)] != CellType::Nothing) continue;

            const int up = neighbor(i, Direction::Up);
            const int down = neighbor(i, Direction::Down);
            const int left = neighbor(i, Direction::Left);
            const int right = neighbor(i, Direction::Right);

            const bool vertical =
                cells[static_cast<std::size_t>(up)] == CellType::Wall &&
                cells[static_cast<std::size_t>(down)] == CellType::Wall;
            const bool horizontal =
                cells[static_cast<std::size_t>(left)] == CellType::Wall &&
                cells[static_cast<std::size_t>(right)] == CellType::Wall;

            if (vertical || horizontal)
                cells[static_cast<std::size_t>(i)] = CellType::Door;
        }

        const std::uint64_t end_ticks = SDL_GetPerformanceCounter();
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        const double elapsed_ms = (static_cast<double>(end_ticks - start_ticks) * 1000.0) / freq;
        SDL_Log("[labyrinth] generation time: %.2f ms", elapsed_ms);
    }

    void ensure_generated(GameContext& ctx)
    {
        if (initialized_) return;
        generate_labyrinth(ctx);
        seen_.assign(WORLD_SIZE, 0);

        while (player_pos_ < 0)
        {
            const int drop = static_cast<int>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_SIZE - 1)));
            const CellType t = cells_[static_cast<std::size_t>(drop)];
            if (t == CellType::Nothing || t == CellType::Door)
            {
                player_pos_ = drop;
            }
        }

        ensure_player_exit(ctx);
        cam_pos_ = player_pos_;
        freecam_ = false;
        reveal_from_player();
        initialized_ = true;
        ctx.redraw_requested = true;
    }

    void ensure_player_exit(GameContext& ctx)
    {
        if (player_pos_ < 0) return;

        const std::array<Direction, 4> dirs = {
            Direction::Up, Direction::Left, Direction::Down, Direction::Right
        };
        bool has_exit = false;
        for (Direction dir : dirs)
        {
            const int next_pos = neighbor_from_pos(player_pos_, dir);
            if (!is_wall(next_pos)) {
                has_exit = true;
                break;
            }
        }
        if (has_exit) return;

        int best_len = std::numeric_limits<int>::max();
        Direction best_dir = Direction::Up;
        for (Direction dir : dirs)
        {
            int pos = player_pos_;
            int steps = 0;
            while (steps < kWallSpacing * 3)
            {
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

        int carve_steps = best_len == std::numeric_limits<int>::max()
            ? 1
            : best_len;
        int pos = player_pos_;
        for (int i = 0; i < carve_steps; ++i)
        {
            pos = neighbor_from_pos(pos, best_dir);
            cells_[static_cast<std::size_t>(pos)] = CellType::Nothing;
        }

        ctx.redraw_requested = true;
    }

    void move_player(Direction dir, GameContext& ctx)
    {
        const int next_pos = neighbor_from_pos(player_pos_, dir);
        if (next_pos >= 0 && !is_wall(next_pos))
        {
            player_pos_ = next_pos;
            reveal_from_player();
            if (!freecam_) cam_pos_ = player_pos_;
            ctx.redraw_requested = true;
        }
    }

    void init_buttons(GameContext& ctx)
    {
        ui_init_speed_buttons(speed_buttons_, ctx, 2);
        ui_init_move_buttons(
            move_buttons_,
            ctx,
            [this]() { pending_move_dir_ = Direction::Up; move_requested_ = true; },
            [this]() { pending_move_dir_ = Direction::Left; move_requested_ = true; },
            [this]() { center_requested_ = true; },
            [this]() { pending_move_dir_ = Direction::Right; move_requested_ = true; },
            [this]() { pending_move_dir_ = Direction::Down; move_requested_ = true; });

        buttons_initialized_ = true;
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ensure_generated(ctx);
        if (!buttons_initialized_) init_buttons(ctx);

        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            switch (evt.action)
            {
                case InputAction::Press:
                    if (speed_buttons_.handle_press(evt.x, evt.y)) {
                        click_blocked_ = true;
                    } else {
                        click_blocked_ = move_buttons_.handle_press(evt.x, evt.y);
                    }
                    break;
                
                case InputAction::Click:
                    speed_buttons_.reset_pressed();
                    move_buttons_.reset_pressed();
                    if (!click_blocked_) {
                        handle_click_move(ctx, evt.x, evt.y);
                    }
                    click_blocked_ = false;
                    break;

                case InputAction::Release:
                    speed_buttons_.reset_pressed();
                    move_buttons_.reset_pressed();
                    click_blocked_ = false;
                    break;
                    
                default: break;
            }
        }
        else if (event.type == SDL_MOUSEMOTION)
        {
            // Сохраняем обработку пассивного ховера мыши (когда нет нажатия), 
            // так как InputManager обрабатывает движения только с нажатием (Drag).
            const int mx = to_render_x(ctx, event.motion.x);
            const int my = to_render_y(ctx, event.motion.y);
            const TileView view = make_tile_view(ctx, TILE_SIZE, 0, 0);
            hover_pos_ = screen_to_world_pos(ctx, mx, my, view);
            ctx.redraw_requested = true;
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch (event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    clear_states(ctx);
                    break;
                case SDLK_0:
                    handle_fullscreen_key(ctx, event.key.keysym.sym);
                    break;
                case SDLK_p:
                    freecam_ = !freecam_;
                    if (!freecam_) cam_pos_ = player_pos_;
                    ctx.redraw_requested = true;
                    break;
                case SDLK_UP:
                    move_player(Direction::Up, ctx);
                    break;
                case SDLK_LEFT:
                    move_player(Direction::Left, ctx);
                    break;
                case SDLK_DOWN:
                    move_player(Direction::Down, ctx);
                    break;
                case SDLK_RIGHT:
                    move_player(Direction::Right, ctx);
                    break;
                default:
                    break;
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ensure_generated(ctx);
        if (!buttons_initialized_) init_buttons(ctx);
        const int prev_cam = cam_pos_;
        cam_pos_ = player_pos_;

        if (center_requested_)
        {
            cam_pos_ = player_pos_;
            center_requested_ = false;
            ctx.redraw_requested = true;
        }

        if (!ctx.paused)
        {
            if (move_requested_)
            {
                move_player(pending_move_dir_, ctx);
                move_requested_ = false;
            }

            const int speed = std::max(1, ctx.game_speed);
            const std::uint32_t now_ticks = SDL_GetTicks();
            const std::uint32_t step_delay = std::max(1u, kMoveDelayMs / static_cast<std::uint32_t>(speed));
            if (path_index_ < path_.size() && now_ticks - last_move_ticks_ >= step_delay)
            {
                const std::size_t steps = static_cast<std::size_t>(speed);
                for (std::size_t step = 0; step < steps && path_index_ < path_.size(); ++step)
                {
                    player_pos_ = path_[path_index_++];
                }
                last_move_ticks_ = now_ticks;
                reveal_from_player();
                if (!freecam_) cam_pos_ = player_pos_;
                if (path_index_ >= path_.size())
                {
                    path_.clear();
                    path_index_ = 0;
                }
                ctx.redraw_requested = true;
            }
        }

        if (cam_pos_ != prev_cam) ctx.redraw_requested = true;
    }

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        ensure_generated(ctx);
        ui_clear_black(ctx.renderer);

        const int tile_size = TILE_SIZE;
        const TileView view = make_tile_view(ctx, tile_size, 0, 0);
        auto neighbor = [this](int pos, Direction dir) {
            return neighbors_[static_cast<std::size_t>(pos) * 4u + static_cast<std::size_t>(dir)];
        };

        for_each_visible_tile(cam_pos_, view, neighbor, [&](int pos_line_idx, const SDL_Rect& draw_tile) {
            if (draw_tile.x + tile_size > 0 && draw_tile.x < ctx.window_width &&
                draw_tile.y + tile_size > 0 && draw_tile.y < ctx.window_height)
            {
                if (seen_[static_cast<std::size_t>(pos_line_idx)] != 0)
                {
                    const CellType cell = cells_[static_cast<std::size_t>(pos_line_idx)];
                    const TerrainType base_tile = (cell == CellType::Wall) ? TerrainType::Mount : TerrainType::Dirt;
                    SDL_RenderCopy(ctx.renderer, textures.tile(base_tile), nullptr, &draw_tile);
                    if (cell == CellType::Door)
                    {
                        SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(ObjectType::Door)), nullptr, &draw_tile);
                    }
                }
                else
                {
                    ui_fill_rect(ctx.renderer, draw_tile, ui_color("#000000"));
                }
            }
        });

        if (!ctx.ui_hit_test.contains(ctx.curs_x, ctx.curs_y)) {
            if (hover_pos_ >= 0 && hover_pos_ < static_cast<int>(WORLD_SIZE) &&
                seen_[static_cast<std::size_t>(hover_pos_)] != 0)
            {
                const auto [cam_x, cam_y] = to_xy(cam_pos_);
                const auto [hover_x, hover_y] = to_xy(hover_pos_);
                const int offset_x = (hover_x - cam_x) * tile_size;
                const int offset_y = (hover_y - cam_y) * tile_size;
                const int center_x = ctx.window_width / 2 - tile_size / 2;
                const int center_y = ctx.window_height / 2 - tile_size / 2;
                SDL_Rect hover_rect{center_x + offset_x, center_y + offset_y, tile_size, tile_size};
                ui_fill_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF28"));
                ui_draw_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF8C"));
            }
        }

        if (player_pos_ >= 0 && seen_[static_cast<std::size_t>(player_pos_)] != 0)
        {
            const auto [cam_x, cam_y] = to_xy(cam_pos_);
            const auto [player_x, player_y] = to_xy(player_pos_);
            const int offset_x = (player_x - cam_x) * tile_size;
            const int offset_y = (player_y - cam_y) * tile_size;
            const int center_x = ctx.window_width / 2 - tile_size / 2;
            const int center_y = ctx.window_height / 2 - tile_size / 2;
            SDL_Rect player_rect{center_x + offset_x, center_y + offset_y, tile_size, tile_size};
            SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(ObjectType::Player)), nullptr, &player_rect);
        }

        if (buttons_initialized_)
        {
            speed_buttons_.render(ctx);
            move_buttons_.render(ctx);
        }
    }
};
