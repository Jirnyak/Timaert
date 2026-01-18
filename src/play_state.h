#pragma once

#include "game_state.h"
#include "hud.h"
#include "ui.h"
#include "world_manager.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>

class PlayState : public GameState
{
private:
    UIButtonGroup buttons_;
    UIButtonGroup move_buttons_;
    UIButtonGroup action_buttons_;
    bool buttons_initialized_ = false;
    WorldManager* world_manager_ = nullptr;
    int player_destination_ = -1;
    bool show_trade_ui_ = false;
    bool center_requested_ = false;
    std::optional<Direction> pending_move_dir_;
    int drag_start_x_ = 0;
    int drag_start_y_ = 0;
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;
    HudState hud_;
    std::vector<int> visible_epoch_;
    std::vector<SDL_Point> visible_points_;
    int visible_epoch_counter_ = 0;

    [[nodiscard]] SDL_Rect trade_panel_rect(const GameContext& ctx) const noexcept
    {
        return ui_centered_rect(ctx.window_width, ctx.window_height, 300, 400);
    }
    
    int screen_to_world_pos(GameContext& ctx, int screen_x, int screen_y)
    {
        const int scaled_tile_size = static_cast<int>(static_cast<float>(TILE_SIZE) * ctx.zoom);
        if (scaled_tile_size < 1) return -1;
        
        const int pixel_offset_x = static_cast<int>(ctx.map_offset_x * ctx.zoom);
        const int pixel_offset_y = static_cast<int>(ctx.map_offset_y * ctx.zoom);
        
        const int center_screen_x = ctx.window_width / 2 + pixel_offset_x;
        const int center_screen_y = ctx.window_height / 2 + pixel_offset_y;
        
        int diff_x = screen_x - center_screen_x;
        int diff_y = screen_y - center_screen_y;
        
        int tile_offset_x, tile_offset_y;
        if (diff_x >= 0) {
            tile_offset_x = diff_x / scaled_tile_size;
        } else {
            tile_offset_x = -(((-diff_x - 1) / scaled_tile_size) + 1);
        }
        if (diff_y >= 0) {
            tile_offset_y = diff_y / scaled_tile_size;
        } else {
            tile_offset_y = -(((-diff_y - 1) / scaled_tile_size) + 1);
        }
        
        int pos_idx = ctx.pos_cam;
        
        if (tile_offset_x > 0) {
            for (int i = 0; i < tile_offset_x; i++)
                pos_idx = ctx.get_neighbor(pos_idx, Direction::Right);
        } else {
            for (int i = 0; i < -tile_offset_x; i++)
                pos_idx = ctx.get_neighbor(pos_idx, Direction::Left);
        }
        
        if (tile_offset_y > 0) {
            for (int i = 0; i < tile_offset_y; i++)
                pos_idx = ctx.get_neighbor(pos_idx, Direction::Down);
        } else {
            for (int i = 0; i < -tile_offset_y; i++)
                pos_idx = ctx.get_neighbor(pos_idx, Direction::Up);
        }
        
        return pos_idx;
    }
    
    void render_all_npcs(GameContext& ctx, TextureManager& textures, int scaled_tile_size,
                          int visible_epoch)
    {
        if (!world_manager_) return;
        
        world_manager_->npcs.for_each_active([&](const NPC& npc) {
            if (npc.state == NPCState::Dead) return;

            if (npc.pos < 0 || npc.pos >= static_cast<int>(WORLD_SIZE)) return;
            if (visible_epoch_[static_cast<std::size_t>(npc.pos)] != visible_epoch) return;

            SDL_Rect draw_tile;
            const SDL_Point& pt = visible_points_[static_cast<std::size_t>(npc.pos)];
            draw_tile.x = pt.x;
            draw_tile.y = pt.y;
            draw_tile.w = scaled_tile_size;
            draw_tile.h = scaled_tile_size;
            
            if (draw_tile.x + scaled_tile_size <= 0 || draw_tile.x >= ctx.window_width ||
                draw_tile.y + scaled_tile_size <= 0 || draw_tile.y >= ctx.window_height) return;
            
            ObjectType obj_type = ObjectType::Peasant;
            switch (npc.type)
            {
                case NPCType::Peasant: obj_type = ObjectType::Peasant; break;
                case NPCType::Merchant: obj_type = ObjectType::Merchant; break;
                case NPCType::Caravan: obj_type = ObjectType::Caravan; break;
                case NPCType::Bandit: obj_type = ObjectType::Bandit; break;
                case NPCType::Guard: obj_type = ObjectType::Guard; break;
                default: break;
            }
            SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(obj_type)), nullptr, &draw_tile);
        });
    }

    void render_entities(GameContext& ctx, TextureManager& textures, int scaled_tile_size,
                         int visible_epoch,
                         const EntityManager& entities)
    {
        for (const auto& obj : entities.entities())
        {
            if (!obj.active) continue;
            if (obj.pos < 0 || obj.pos >= static_cast<int>(WORLD_SIZE)) continue;
            if (visible_epoch_[static_cast<std::size_t>(obj.pos)] != visible_epoch) continue;

            SDL_Rect draw_tile;
            const SDL_Point& pt = visible_points_[static_cast<std::size_t>(obj.pos)];
            draw_tile.x = pt.x;
            draw_tile.y = pt.y;
            draw_tile.w = scaled_tile_size;
            draw_tile.h = scaled_tile_size;
            SDL_RenderCopy(ctx.renderer, textures.sprite(obj.type), nullptr, &draw_tile);
        }
    }

    void render_settlements(GameContext& ctx, TextureManager& textures, int scaled_tile_size,
                            int visible_epoch)
    {
        if (!world_manager_) return;

        for (const auto& settlement : world_manager_->landmarks.settlements())
        {
            if (settlement.pos < 0 || settlement.pos >= static_cast<int>(WORLD_SIZE)) continue;
            if (visible_epoch_[static_cast<std::size_t>(settlement.pos)] != visible_epoch) continue;

            ObjectType obj_type = ObjectType::Village;
            if (settlement.type == SettlementType::City) obj_type = ObjectType::City;
            else if (settlement.type == SettlementType::Town) obj_type = ObjectType::Town;

            SDL_Rect draw_tile;
            const SDL_Point& pt = visible_points_[static_cast<std::size_t>(settlement.pos)];
            draw_tile.x = pt.x;
            draw_tile.y = pt.y;
            draw_tile.w = scaled_tile_size;
            draw_tile.h = scaled_tile_size;
            SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(obj_type)), nullptr, &draw_tile);
        }
    }

    void render_player(GameContext& ctx, TextureManager& textures, int scaled_tile_size,
                       int visible_epoch)
    {
        if (!world_manager_) return;

        const Player& p = world_manager_->player_ctrl.player();
        if (!p.active) return;

        if (p.pos < 0 || p.pos >= static_cast<int>(WORLD_SIZE)) return;
        if (visible_epoch_[static_cast<std::size_t>(p.pos)] != visible_epoch) return;

        SDL_Rect draw_tile;
        const SDL_Point& pt = visible_points_[static_cast<std::size_t>(p.pos)];
        draw_tile.x = pt.x;
        draw_tile.y = pt.y;
        draw_tile.w = scaled_tile_size;
        draw_tile.h = scaled_tile_size;
        SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(ObjectType::Player)), nullptr, &draw_tile);
    }
    
    void init_buttons(GameContext& ctx) {
        buttons_.clear();
        
        const int btn_size = std::min(ctx.window_width, ctx.window_height) / 10;
        const int margin = btn_size / 4;
        const int start_x = ctx.window_width - (btn_size + margin) * 3;
        const int y = ctx.window_height - btn_size - margin;
        
        buttons_.add(UIButton{
            {start_x, y, btn_size, btn_size},
            "||",
            [&ctx]() { ctx.paused = true; ctx.game_speed = 1; },
            [&ctx]() { return ctx.paused; }
        });
        
        buttons_.add(UIButton{
            {start_x + btn_size + margin, y, btn_size, btn_size},
            ">",
            [&ctx]() { ctx.paused = false; ctx.game_speed = 1; },
            [&ctx]() { return !ctx.paused && ctx.game_speed == 1; }
        });
        
        buttons_.add(UIButton{
            {start_x + (btn_size + margin) * 2, y, btn_size, btn_size},
            ">>",
            [&ctx]() { ctx.paused = false; ctx.game_speed = 4; },
            [&ctx]() { return !ctx.paused && ctx.game_speed > 1; }
        });
        
        buttons_.add(UIButton{
            {margin, y, btn_size, btn_size},
            "=",
            [&ctx]() { ctx.game_mod = GameMode::Pause; ctx.picked = false; },
            nullptr
        });
        
        const int move_btn_size = btn_size;
        const int move_margin = margin;
        const int move_start_x = margin;
        const int move_start_y = ctx.window_height - move_btn_size * 3 - move_margin * 4;
        
        move_buttons_.clear();
        move_buttons_.add(UIButton{
            {move_start_x + move_btn_size + move_margin, move_start_y, move_btn_size, move_btn_size},
            "^",
            [this]() { pending_move_dir_ = Direction::Up; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x, move_start_y + move_btn_size + move_margin, move_btn_size, move_btn_size},
            "<",
            [this]() { pending_move_dir_ = Direction::Left; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x + move_btn_size + move_margin, move_start_y + move_btn_size + move_margin, move_btn_size, move_btn_size},
            "O",
            [this]() { center_requested_ = true; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x + (move_btn_size + move_margin) * 2, move_start_y + move_btn_size + move_margin, move_btn_size, move_btn_size},
            ">",
            [this]() { pending_move_dir_ = Direction::Right; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x + move_btn_size + move_margin, move_start_y + (move_btn_size + move_margin) * 2, move_btn_size, move_btn_size},
            "v",
            [this]() { pending_move_dir_ = Direction::Down; },
            nullptr
        });
        
        action_buttons_.clear();
        const int action_x = ctx.window_width - btn_size - margin;
        action_buttons_.add(UIButton{
            {action_x, move_start_y, btn_size, btn_size},
            "$",
            [this]() { show_trade_ui_ = !show_trade_ui_; },
            [this]() { return show_trade_ui_; }
        });
        
        buttons_initialized_ = true;
    }
    
    void move_player_direction(Direction dir, GameContext& ctx)
    {
        if (!world_manager_) return;
        Player& p = world_manager_->player_ctrl.player();
        if (!p.active) return;
        
        const int next_pos = ctx.get_neighbor(p.pos, dir);
        if (next_pos >= 0 && 
            ctx.relief[next_pos] != TerrainType::Water && 
            ctx.relief[next_pos] != TerrainType::Mount)
        {
            p.prev_pos = p.pos;
            p.pos = next_pos;
            world_manager_->player_ctrl.clear_aim();
            player_destination_ = -1;
        }
    }
    
    void center_on_player(GameContext& ctx)
    {
        if (!world_manager_) return;
        const Player& p = world_manager_->player_ctrl.player();
        if (p.active)
        {
            ctx.pos_cam = p.pos;
            ctx.map_offset_x = 0;
            ctx.map_offset_y = 0;
        }
    }
    
    void handle_tap_to_move(GameContext& ctx, int screen_x, int screen_y)
    {
        if (!world_manager_) return;
        Player& p = world_manager_->player_ctrl.player();
        if (!p.active) return;

        const int target_pos = screen_to_world_pos(ctx, screen_x, screen_y);
        if (target_pos < 0 || target_pos >= static_cast<int>(WORLD_SIZE)) return;

        if (world_manager_->player_ctrl.set_path_to(ctx, target_pos, ctx.relief.get()))
        {
            player_destination_ = target_pos;
        }
    }
    
public:
    void set_world_manager(WorldManager* wm) { world_manager_ = wm; }
    
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);
        auto trade_panel_contains = [this, &ctx](int x, int y) {
            return ui_point_in_rect(x, y, trade_panel_rect(ctx));
        };
        
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            const int mx = to_render_x(ctx, event.button.x);
            const int my = to_render_y(ctx, event.button.y);
            if (show_trade_ui_) {
                if (!trade_panel_contains(mx, my)) {
                    show_trade_ui_ = false;
                }
                ctx.map_dragging = false;
                return;
            }
            if (buttons_.handle_press(mx, my)) {
                return;
            }
            if (move_buttons_.handle_press(mx, my)) {
                return;
            }
            if (action_buttons_.handle_press(mx, my)) {
                return;
            }
            ctx.map_dragging = true;
            ctx.drag_last_x = mx;
            ctx.drag_last_y = my;
            drag_start_x_ = mx;
            drag_start_y_ = my;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
        {
            const int mx = to_render_x(ctx, event.button.x);
            const int my = to_render_y(ctx, event.button.y);
            buttons_.reset_pressed();
            move_buttons_.reset_pressed();
            action_buttons_.reset_pressed();
            
            const int drag_dist = std::abs(mx - drag_start_x_) + std::abs(my - drag_start_y_);
            if (drag_dist < 10)
            {
                handle_tap_to_move(ctx, mx, my);
            }
            ctx.map_dragging = false;
        }
        else if (event.type == SDL_MOUSEMOTION && ctx.map_dragging)
        {
            const int mx = to_render_x(ctx, event.motion.x);
            const int my = to_render_y(ctx, event.motion.y);
            const int dx = mx - ctx.drag_last_x;
            const int dy = my - ctx.drag_last_y;
            
            ctx.map_offset_x += static_cast<float>(dx) / ctx.zoom;
            ctx.map_offset_y += static_cast<float>(dy) / ctx.zoom;
            
            ctx.velocity_x = ctx.velocity_x * 0.5f + (static_cast<float>(dx) / ctx.zoom) * 0.5f;
            ctx.velocity_y = ctx.velocity_y * 0.5f + (static_cast<float>(dy) / ctx.zoom) * 0.5f;
            
            ctx.drag_last_x = mx;
            ctx.drag_last_y = my;
        }
        else if (event.type == SDL_FINGERDOWN)
        {
            const int tx = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            const int ty = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            if (show_trade_ui_) {
                if (!trade_panel_contains(tx, ty)) {
                    show_trade_ui_ = false;
                }
                ctx.map_dragging = false;
                return;
            }
            if (buttons_.handle_press(tx, ty)) {
                return;
            }
            if (move_buttons_.handle_press(tx, ty)) {
                return;
            }
            if (action_buttons_.handle_press(tx, ty)) {
                return;
            }
            ctx.map_dragging = true;
            ctx.drag_last_x = tx;
            ctx.drag_last_y = ty;
            drag_start_x_ = tx;
            drag_start_y_ = ty;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_FINGERUP)
        {
            const int tx = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            const int ty = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            buttons_.reset_pressed();
            move_buttons_.reset_pressed();
            action_buttons_.reset_pressed();
            
            const int drag_dist = std::abs(tx - drag_start_x_) + std::abs(ty - drag_start_y_);
            if (drag_dist < 20)
            {
                handle_tap_to_move(ctx, tx, ty);
            }
            ctx.map_dragging = false;
        }
        else if (event.type == SDL_FINGERMOTION)
        {
            const int new_x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            const int new_y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            const int dx = new_x - ctx.drag_last_x;
            const int dy = new_y - ctx.drag_last_y;
            
            ctx.map_offset_x += static_cast<float>(dx) / ctx.zoom;
            ctx.map_offset_y += static_cast<float>(dy) / ctx.zoom;
            
            ctx.velocity_x = ctx.velocity_x * 0.5f + (static_cast<float>(dx) / ctx.zoom) * 0.5f;
            ctx.velocity_y = ctx.velocity_y * 0.5f + (static_cast<float>(dy) / ctx.zoom) * 0.5f;
            
            ctx.drag_last_x = new_x;
            ctx.drag_last_y = new_y;
        }
        else if (event.type == SDL_MOUSEWHEEL)
        {
            if (event.wheel.y > 0)
                ctx.target_zoom *= 1.2f;
            else if (event.wheel.y < 0)
                ctx.target_zoom /= 1.2f;
            
            if (ctx.target_zoom < ctx.min_zoom) ctx.target_zoom = ctx.min_zoom;
            if (ctx.target_zoom > ctx.max_zoom) ctx.target_zoom = ctx.max_zoom;
        }
        else if (event.type == SDL_MULTIGESTURE)
        {
            if (std::abs(event.mgesture.dDist) > 0.002f)
            {
                ctx.target_zoom *= (1.0f + event.mgesture.dDist * 5.0f);
                if (ctx.target_zoom < ctx.min_zoom) ctx.target_zoom = ctx.min_zoom;
                if (ctx.target_zoom > ctx.max_zoom) ctx.target_zoom = ctx.max_zoom;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    ctx.game_mod = GameMode::Pause;
                    ctx.picked = false;
                    break;
                case SDLK_0:
                    ctx.fullscreen = !ctx.fullscreen;
                    if (ctx.fullscreen)
                        SDL_SetWindowFullscreen(ctx.window, SDL_WINDOW_FULLSCREEN);
                    else
                        SDL_SetWindowFullscreen(ctx.window, 0);
                    break;
                case SDLK_SPACE:
                    ctx.paused = !ctx.paused;
                    break;
                case SDLK_k:
                    ctx.screenshot = true;
                    break;
                case SDLK_c: {
                    [[maybe_unused]] const bool input_result = inputbox(ctx.renderer, ctx.font.get(), ctx.window_width/2, ctx.window_height/2, 200, 100, ctx.input, 0);
                    break;
                }
                case SDLK_p:
                    ctx.freecam = !ctx.freecam;
                    break;
                case SDLK_m:
                    ctx.game_mod = GameMode::Map;
                    break;
                case SDLK_UP:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Up);
                    break;
                case SDLK_LEFT:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Left);
                    break;
                case SDLK_DOWN:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Down);
                    break;
                case SDLK_RIGHT:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Right);
                    break;
                default:
                    break;
            }
        }
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        bool needs_redraw = false;
        if (ctx.window_width != last_buttons_width_ || ctx.window_height != last_buttons_height_) {
            buttons_initialized_ = false;
            init_buttons(ctx);
            last_buttons_width_ = ctx.window_width;
            last_buttons_height_ = ctx.window_height;
            ctx.window_dirty = false;
            needs_redraw = true;
        }
        const float prev_zoom = ctx.zoom;
        const float prev_offset_x = ctx.map_offset_x;
        const float prev_offset_y = ctx.map_offset_y;
        const int prev_cam = ctx.pos_cam;
        const std::uint32_t current_time = SDL_GetTicks();
        float delta_time = static_cast<float>(current_time - ctx.last_frame_time) / 16.67f;
        ctx.last_frame_time = current_time;
        if (delta_time > 3.0f) delta_time = 3.0f;
        
        if (pending_move_dir_)
        {
            move_player_direction(*pending_move_dir_, ctx);
            pending_move_dir_.reset();
            needs_redraw = true;
        }
        
        if (center_requested_)
        {
            center_on_player(ctx);
            center_requested_ = false;
            needs_redraw = true;
        }
        
        ctx.zoom += (ctx.target_zoom - ctx.zoom) * ctx.zoom_speed * delta_time;
        
        if (!ctx.map_dragging)
        {
            ctx.map_offset_x += ctx.velocity_x * delta_time;
            ctx.map_offset_y += ctx.velocity_y * delta_time;
            
            ctx.velocity_x *= std::pow(ctx.friction, delta_time);
            ctx.velocity_y *= std::pow(ctx.friction, delta_time);
            
            if (std::abs(ctx.velocity_x) < ctx.velocity_threshold) ctx.velocity_x = 0.0f;
            if (std::abs(ctx.velocity_y) < ctx.velocity_threshold) ctx.velocity_y = 0.0f;
        }
        const int tile_size = TILE_SIZE;
        while (ctx.map_offset_x >= tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Left);
            ctx.map_offset_x -= tile_size;
        }
        while (ctx.map_offset_x <= -tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Right);
            ctx.map_offset_x += tile_size;
        }
        while (ctx.map_offset_y <= -tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Down);
            ctx.map_offset_y += tile_size;
        }
        while (ctx.map_offset_y >= tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Up);
            ctx.map_offset_y -= tile_size;
        }

        if (!ctx.paused) {
            needs_redraw = true;
        }
        if (ctx.map_dragging || ctx.velocity_x != 0.0f || ctx.velocity_y != 0.0f) {
            needs_redraw = true;
        }
        if (std::abs(ctx.zoom - prev_zoom) > 0.0001f) {
            needs_redraw = true;
        }
        if (ctx.map_offset_x != prev_offset_x || ctx.map_offset_y != prev_offset_y) {
            needs_redraw = true;
        }
        if (ctx.pos_cam != prev_cam) {
            needs_redraw = true;
        }
        ctx.redraw_requested = ctx.redraw_requested || needs_redraw;
        
        if (!ctx.paused)
        {
            for (int tick = 0; tick < ctx.game_speed; ++tick)
            {
                ctx.hour += 1;
                
                if (world_manager_)
                {
                    world_manager_->update(ctx);
                }

                std::fill(ctx.pos_map.begin(), ctx.pos_map.end(), 0);
                if (world_manager_)
                {
                    world_manager_->rebuild_pos_map(ctx.pos_map);
                }
                entities.rebuild_pos_map(ctx.pos_map, false);
                
                constexpr int kSpawnSamplesPerTick = 64;
                const auto entity_span = entities.entities();
                const std::size_t entity_count = entity_span.size();
                if (entity_count > 0)
                {
                    const std::size_t start_idx = random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(entity_count - 1));
                    int checked = 0;
                    for (std::size_t offset = 0; offset < entity_count && checked < kSpawnSamplesPerTick; ++offset)
                    {
                        const std::size_t idx = (start_idx + offset) % entity_count;
                        const auto& obj = entity_span[idx];
                        if (!obj.active) continue;
                        ++checked;

                        const int drop = random_u32_inclusive(ctx.rng, WORLD_WIDTH);
                        const Direction drop_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
                        const int side_idx = ctx.get_neighbor(obj.pos, drop_dir);
                        if (drop == 0 && side_idx >= 0 &&
                            (ctx.relief[side_idx] == TerrainType::Grass ||
                             ctx.relief[side_idx] == TerrainType::Dirt) &&
                            ctx.pos_map[side_idx] == 0)
                        {
                            [[maybe_unused]] auto* e = entities.new_entity(static_cast<int>(ObjectType::Tree), side_idx);
                        }
                    }
                }
            }
        }
    }
    
    void render(GameContext& ctx, TextureManager& textures, EntityManager& entities) override
    {
        ui_clear(ctx.renderer, ui_color("#000000"));
        
        int scaled_tile_size = static_cast<int>(static_cast<float>(TILE_SIZE) * ctx.zoom);
        if (scaled_tile_size < 1) scaled_tile_size = 1;
        
        const int pixel_offset_x = static_cast<int>(ctx.map_offset_x * ctx.zoom);
        const int pixel_offset_y = static_cast<int>(ctx.map_offset_y * ctx.zoom);
        
        const int tiles_x = (ctx.window_width / scaled_tile_size) + 3;
        const int tiles_y = (ctx.window_height / scaled_tile_size) + 3;
        const int base_x = ctx.window_width / 2 + pixel_offset_x - (tiles_x / 2) * scaled_tile_size;
        const int base_y = ctx.window_height / 2 + pixel_offset_y - (tiles_y / 2) * scaled_tile_size;
        
        int pos_idx = ctx.pos_cam;
        for (int i = 0; i < tiles_x / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, Direction::Left);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, Direction::Up);
        
        SDL_Rect draw_tile{};
        draw_tile.w = scaled_tile_size;
        draw_tile.h = scaled_tile_size;

        if (visible_epoch_.empty())
        {
            visible_epoch_.assign(WORLD_SIZE, 0);
            visible_points_.assign(WORLD_SIZE, SDL_Point{0, 0});
        }
        if (++visible_epoch_counter_ == std::numeric_limits<int>::max())
        {
            std::fill(visible_epoch_.begin(), visible_epoch_.end(), 0);
            visible_epoch_counter_ = 1;
        }
        const int visible_epoch = visible_epoch_counter_;
        
        int row_start_idx = pos_idx;
        int draw_y = base_y;
        for (int a = 0; a < tiles_y; a++)
        {
            int pos_line_idx = row_start_idx;
            int draw_x = base_x;
            for (int b = 0; b < tiles_x; b++)
            {
                draw_tile.x = draw_x;
                draw_tile.y = draw_y;
                
                if (draw_tile.x + scaled_tile_size > 0 && draw_tile.x < ctx.window_width &&
                    draw_tile.y + scaled_tile_size > 0 && draw_tile.y < ctx.window_height)
                {
                    SDL_RenderCopy(ctx.renderer, textures.tile(ctx.relief[pos_line_idx]), nullptr, &draw_tile);
                    const std::size_t idx = static_cast<std::size_t>(pos_line_idx);
                    visible_epoch_[idx] = visible_epoch;
                    visible_points_[idx] = SDL_Point{draw_tile.x, draw_tile.y};
                }
                pos_line_idx = ctx.get_neighbor(pos_line_idx, Direction::Right);
                draw_x += scaled_tile_size;
            }
            row_start_idx = ctx.get_neighbor(row_start_idx, Direction::Down);
            draw_y += scaled_tile_size;
        }
        
        pos_idx = ctx.pos_cam;
        for (int i = 0; i < tiles_x / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, Direction::Left);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, Direction::Up);

        render_entities(ctx, textures, scaled_tile_size, visible_epoch, entities);
        render_settlements(ctx, textures, scaled_tile_size, visible_epoch);
        render_player(ctx, textures, scaled_tile_size, visible_epoch);
        
        render_all_npcs(ctx, textures, scaled_tile_size, visible_epoch);
        
        const int hover_pos = screen_to_world_pos(ctx, ctx.curs_x, ctx.curs_y);
        if (hover_pos >= 0 && hover_pos < static_cast<int>(WORLD_SIZE) &&
            visible_epoch_[static_cast<std::size_t>(hover_pos)] == visible_epoch)
        {
            const SDL_Point& hover_pt = visible_points_[static_cast<std::size_t>(hover_pos)];
            SDL_Rect hover_rect{hover_pt.x, hover_pt.y, scaled_tile_size, scaled_tile_size};
            ui_fill_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF28"));
            ui_draw_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF8C"));
        }
        
        hud_.render(ctx, world_manager_);
        
        if (ctx.paused)
        {
            const std::string text = "PAUSED";
            render_text(ctx.renderer, ctx.font.get(), text, ctx.window_width / 2 - 50, 10, 100, 20, {255, 0, 0, 255});
        }
        
        if (buttons_initialized_) {
            buttons_.render(ctx.renderer, ctx.font.get());
            move_buttons_.render(ctx.renderer, ctx.font.get());
            action_buttons_.render(ctx.renderer, ctx.font.get());
        }
        
        if (show_trade_ui_ && world_manager_)
        {
            render_trade_ui(ctx);
        }
    }
    
    void render_trade_ui(GameContext& ctx)
    {
        if (!world_manager_) return;
        const Player& p = world_manager_->player_ctrl.player();
        if (!p.active) return;
        
        const Settlement* at_settlement = world_manager_->get_settlement_at(p.pos);
        
        const SDL_Rect panel = trade_panel_rect(ctx);
        const int panel_x = panel.x;
        const int panel_y = panel.y;
        const int panel_w = panel.w;
        const int panel_h = panel.h;
        ui_draw_panel(ctx.renderer, panel, ui_color("#28283CE6"), ui_color("#64648C"));

        int y = panel_y + 10;
        
        if (at_settlement)
        {
            std::string title = "Trade at " + at_settlement->name;
            render_text(ctx.renderer, ctx.font.get(), title, panel_x + 10, y, panel_w - 20, 20, {255, 255, 255, 255});
            y += 30;
            
            render_text(ctx.renderer, ctx.font.get(), "Your Inventory:", panel_x + 10, y, 150, 16, {200, 200, 200, 255});
            y += 20;
            
            for (std::size_t i = 1; i < RESOURCE_COUNT; ++i)
            {
                const auto res = static_cast<ResourceType>(i);
                const std::int32_t amount = p.inventory.get(res);
                if (amount > 0)
                {
                    std::string line = std::string(RESOURCE_DATA[i].name) + ": " + std::to_string(amount);
                    render_text(ctx.renderer, ctx.font.get(), line, panel_x + 20, y, 200, 14, {180, 180, 180, 255});
                    y += 16;
                }
            }
        }
        else
        {
            render_text(ctx.renderer, ctx.font.get(), "Not at a settlement", panel_x + 10, y, panel_w - 20, 20, {255, 100, 100, 255});
            y += 30;
            render_text(ctx.renderer, ctx.font.get(), "Travel to a city, town,", panel_x + 10, y, panel_w - 20, 16, {180, 180, 180, 255});
            y += 20;
            render_text(ctx.renderer, ctx.font.get(), "or village to trade.", panel_x + 10, y, panel_w - 20, 16, {180, 180, 180, 255});
        }
        
        render_text(ctx.renderer, ctx.font.get(), "Tap outside to close", panel_x + 10, panel_y + panel_h - 25, panel_w - 20, 14, {150, 150, 150, 255});
    }
};
