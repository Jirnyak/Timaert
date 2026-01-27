#pragma once

#include <string>
#include <optional>

#include "core/game_state.h"
#include "core/game_context.h"
#include "rendering/hud.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include "core/gfx_types.h"
#include "core/tile_map.h"
#include "core/types.h"

class TextureManager;

class PlayState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Game;
    }

private:
    UIButtonGroup buttons_;
    UIButtonGroup move_buttons_;
    UIButtonGroup action_buttons_;
    bool buttons_initialized_ = false;
    TilePosition player_destination_ = INVALID_POS;
    bool show_trade_ui_ = false;
    std::optional<Direction> pending_move_dir_;
    bool center_pending_ = false;
    bool pause_pending_ = false;
    bool stat_pending_ = false;

    void request_center() {
        center_pending_ = true;
    }
    void request_pause() {
        pause_pending_ = true;
    }
    void request_stat() {
        stat_pending_ = true;
    }
    // drag_start_x_ и drag_start_y_ удалены, так как InputManager обрабатывает дистанцию
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;
    HudState hud_;
    InputManager input_manager_;
    WorldMap<int> visible_epoch_;
    WorldMap<Point> visible_points_;
    int visible_epoch_counter_ = 0;
    std::string hovered_npc_text_;

    [[nodiscard]] static Rect trade_panel_rect(const GameContext& ctx) noexcept {
        return ui_centered_rect(ctx.window_width, ctx.window_height, 300, 400);
    }

    [[nodiscard]] static TilePosition screen_to_world_pos(const GameContext& ctx,
                                                   int screen_x,
                                                   int screen_y,
                                                   const TileView& view) {
        auto neighbor = [&ctx](TilePosition pos, Direction dir) {
            return ctx.get_neighbor(pos, dir);
        };
        return ::screen_to_world_pos(ctx, screen_x, screen_y, ctx.pos_cam, view, neighbor);
    }

    static void render_all_npcs(GameContext& ctx,
                         TextureManager& textures,
                         int scaled_tile_size,
                         int visible_epoch);
    void render_entities(GameContext& ctx,
                         TextureManager& textures,
                         int scaled_tile_size,
                         int visible_epoch);
    void render_settlements(GameContext& ctx,
                            TextureManager& textures,
                            int scaled_tile_size,
                            int visible_epoch);
    static void render_player(GameContext& ctx,
                       TextureManager& textures,
                       int scaled_tile_size,
                       int visible_epoch);

    void init_buttons(GameContext& ctx);
    void move_player_direction(Direction dir, GameContext& ctx);
    static void center_on_player(GameContext& ctx);
    void handle_tap_to_move(GameContext& ctx, int screen_x, int screen_y);

public:
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;

    static void render_trade_ui(GameContext& ctx);
};

inline StateRegistrar<PlayState> register_play_state_{GameMode::Game};
