#pragma once

#include "game_state.h"
#include "input.h"
#include "ui_button.h"
#include "world_manager.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

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
    int pending_move_dir_ = -1;
    int drag_start_x_ = 0;
    int drag_start_y_ = 0;
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;
    int hud_gold_value_ = std::numeric_limits<int>::min();
    int hud_life_value_ = std::numeric_limits<int>::min();
    int hud_max_life_value_ = std::numeric_limits<int>::min();
    int hud_items_value_ = std::numeric_limits<int>::min();
    int hud_max_items_value_ = std::numeric_limits<int>::min();
    int hud_settlement_count_ = std::numeric_limits<int>::min();
    int hud_npc_count_ = std::numeric_limits<int>::min();
    int hud_aim_pos_ = std::numeric_limits<int>::min();
    bool hud_has_aim_ = false;
    std::string hud_settlement_name_;
    std::string hud_gold_text_;
    std::string hud_hp_text_;
    std::string hud_items_text_;
    std::string hud_at_text_;
    std::string hud_aim_text_;
    std::string hud_settlement_count_text_;
    std::string hud_npc_count_text_;
    std::vector<int> visible_epoch_;
    std::vector<SDL_Point> visible_points_;
    int visible_epoch_counter_ = 0;
    
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
                pos_idx = ctx.get_neighbor(pos_idx, 3);
        } else {
            for (int i = 0; i < -tile_offset_x; i++)
                pos_idx = ctx.get_neighbor(pos_idx, 1);
        }
        
        if (tile_offset_y > 0) {
            for (int i = 0; i < tile_offset_y; i++)
                pos_idx = ctx.get_neighbor(pos_idx, 2);
        } else {
            for (int i = 0; i < -tile_offset_y; i++)
                pos_idx = ctx.get_neighbor(pos_idx, 0);
        }
        
        return pos_idx;
    }
    
    void render_roads(GameContext& /*ctx*/, int /*scaled_tile_size*/, int /*pixel_offset_x*/, int /*pixel_offset_y*/, 
                       int /*tiles_x*/, int /*tiles_y*/, int /*top_left_pos*/)
    {
        // Roads disabled
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
            [this]() { pending_move_dir_ = 0; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x, move_start_y + move_btn_size + move_margin, move_btn_size, move_btn_size},
            "<",
            [this]() { pending_move_dir_ = 1; },
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
            [this]() { pending_move_dir_ = 3; },
            nullptr
        });
        move_buttons_.add(UIButton{
            {move_start_x + move_btn_size + move_margin, move_start_y + (move_btn_size + move_margin) * 2, move_btn_size, move_btn_size},
            "v",
            [this]() { pending_move_dir_ = 2; },
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
    
    void move_player_direction(int dir, GameContext& ctx)
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

        if (world_manager_->player_ctrl.set_path_to(target_pos, ctx.relief.get(), ctx.world))
        {
            player_destination_ = target_pos;
        }
    }
    
public:
    void set_world_manager(WorldManager* wm) { world_manager_ = wm; }
    
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);
        auto trade_panel_contains = [&ctx](int x, int y) {
            const int panel_w = 300;
            const int panel_h = 400;
            const int panel_x = ctx.window_width / 2 - panel_w / 2;
            const int panel_y = ctx.window_height / 2 - panel_h / 2;
            return x >= panel_x && x <= panel_x + panel_w && y >= panel_y && y <= panel_y + panel_h;
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
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 0);
                    break;
                case SDLK_LEFT:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 1);
                    break;
                case SDLK_DOWN:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 2);
                    break;
                case SDLK_RIGHT:
                    ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 3);
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
        
        if (pending_move_dir_ >= 0)
        {
            move_player_direction(pending_move_dir_, ctx);
            pending_move_dir_ = -1;
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
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 1);
            ctx.map_offset_x -= tile_size;
        }
        while (ctx.map_offset_x <= -tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 3);
            ctx.map_offset_x += tile_size;
        }
        while (ctx.map_offset_y <= -tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 2);
            ctx.map_offset_y += tile_size;
        }
        while (ctx.map_offset_y >= tile_size) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 0);
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

                ctx.pos_map.clear();
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
                    const std::size_t start_idx = randomer(ctx.rng, static_cast<std::uint32_t>(entity_count - 1));
                    int checked = 0;
                    for (std::size_t offset = 0; offset < entity_count && checked < kSpawnSamplesPerTick; ++offset)
                    {
                        const std::size_t idx = (start_idx + offset) % entity_count;
                        const auto& obj = entity_span[idx];
                        if (!obj.active) continue;
                        ++checked;

                        const int drop = randomer(ctx.rng, WORLD_WIDTH);
                        const int drop1 = randomer(ctx.rng, 3);
                        const int side_idx = ctx.get_neighbor(obj.pos, drop1);
                        if (drop == 0 && side_idx >= 0 &&
                            (ctx.relief[side_idx] == TerrainType::Grass ||
                             ctx.relief[side_idx] == TerrainType::Dirt) &&
                            ctx.pos_map[side_idx].empty())
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
        SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx.renderer);
        
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
            pos_idx = ctx.get_neighbor(pos_idx, 1);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 0);
        
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
                pos_line_idx = ctx.get_neighbor(pos_line_idx, 3);
                draw_x += scaled_tile_size;
            }
            row_start_idx = ctx.get_neighbor(row_start_idx, 2);
            draw_y += scaled_tile_size;
        }
        
        pos_idx = ctx.pos_cam;
        for (int i = 0; i < tiles_x / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 1);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 0);
        
        render_roads(ctx, scaled_tile_size, pixel_offset_x, pixel_offset_y, tiles_x, tiles_y, pos_idx);
        
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
            SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 40);
            SDL_RenderFillRect(ctx.renderer, &hover_rect);
            SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 140);
            SDL_RenderDrawRect(ctx.renderer, &hover_rect);
        }
        
        if (world_manager_)
        {
            const Player& p = world_manager_->player_ctrl.player();
            if (p.active)
            {
                const int gold_value = static_cast<int>(p.inventory.capital);
                if (gold_value != hud_gold_value_)
                {
                    hud_gold_value_ = gold_value;
                    hud_gold_text_ = "Gold: " + std::to_string(hud_gold_value_);
                }
                
                if (p.life != hud_life_value_ || p.max_life != hud_max_life_value_)
                {
                    hud_life_value_ = p.life;
                    hud_max_life_value_ = p.max_life;
                    hud_hp_text_ = "HP: " + std::to_string(hud_life_value_) + "/" + std::to_string(hud_max_life_value_);
                }
                
                const int items_value = p.inventory.total_items();
                const int max_items_value = p.inventory.max_capacity;
                if (items_value != hud_items_value_ || max_items_value != hud_max_items_value_)
                {
                    hud_items_value_ = items_value;
                    hud_max_items_value_ = max_items_value;
                    hud_items_text_ = "Items: " + std::to_string(hud_items_value_) + "/" + std::to_string(hud_max_items_value_);
                }
                
                const Settlement* at_settlement = world_manager_->get_settlement_at(p.pos);
                const std::string settlement_name = at_settlement ? at_settlement->name : std::string{};
                if (settlement_name != hud_settlement_name_)
                {
                    hud_settlement_name_ = settlement_name;
                    if (at_settlement)
                    {
                        hud_at_text_ = "At: " + hud_settlement_name_;
                    }
                    else
                    {
                        hud_at_text_.clear();
                    }
                }

                if (p.has_aim() != hud_has_aim_ || p.aim_pos != hud_aim_pos_)
                {
                    hud_has_aim_ = p.has_aim();
                    hud_aim_pos_ = p.aim_pos;
                    if (hud_has_aim_)
                    {
                        hud_aim_text_ = "Moving to: " + std::to_string(hud_aim_pos_);
                    }
                    else
                    {
                        hud_aim_text_.clear();
                    }
                }
            }
            
            const int settlement_count = static_cast<int>(world_manager_->landmarks.settlement_count());
            if (settlement_count != hud_settlement_count_)
            {
                hud_settlement_count_ = settlement_count;
                hud_settlement_count_text_ = "Settlements: " + std::to_string(hud_settlement_count_);
            }
            
            const int npc_count = static_cast<int>(world_manager_->npcs.active_count());
            if (npc_count != hud_npc_count_)
            {
                hud_npc_count_ = npc_count;
                hud_npc_count_text_ = "NPCs: " + std::to_string(hud_npc_count_);
            }
            
            struct HudItem {
                std::string text;
                SDL_Color color;
                int height;
            };
            auto text_width = [](const std::string& text) {
                return static_cast<int>(text.size()) * 10;
            };
            std::vector<HudItem> row_one;
            std::vector<HudItem> row_two;
            row_one.push_back({hud_gold_text_, {255, 215, 0, 255}, 14});
            row_one.push_back({hud_hp_text_, {255, 100, 100, 255}, 14});
            row_one.push_back({hud_items_text_, {200, 200, 200, 255}, 14});
            if (!hud_at_text_.empty()) {
                row_one.push_back({hud_at_text_, {100, 255, 100, 255}, 14});
            }
            if (hud_has_aim_ && !hud_aim_text_.empty()) {
                row_two.push_back({hud_aim_text_, {150, 150, 255, 255}, 12});
            }
            row_two.push_back({hud_settlement_count_text_, {180, 180, 180, 255}, 12});
            row_two.push_back({hud_npc_count_text_, {180, 180, 180, 255}, 12});
            
            const int padding = 8;
            const int gap = 12;
            const int row_gap = 4;
            const int row_one_height = 16;
            const int row_two_height = 14;
            int row_one_width = 0;
            int row_two_width = 0;
            for (const auto& item : row_one) {
                if (!item.text.empty()) {
                    row_one_width += text_width(item.text) + gap;
                }
            }
            for (const auto& item : row_two) {
                if (!item.text.empty()) {
                    row_two_width += text_width(item.text) + gap;
                }
            }
            if (row_one_width > 0) row_one_width -= gap;
            if (row_two_width > 0) row_two_width -= gap;
            const int hud_width = std::max(row_one_width, row_two_width);
            const int hud_height = row_one_height + row_two_height + row_gap + padding * 2;
            const int hud_x = 8;
            const int hud_y = 6;
            
            SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx.renderer, 18, 16, 12, 190);
            SDL_Rect hud_bg = {hud_x, hud_y, hud_width + padding * 2, hud_height};
            SDL_RenderFillRect(ctx.renderer, &hud_bg);
            SDL_SetRenderDrawColor(ctx.renderer, 80, 70, 50, 220);
            SDL_RenderDrawRect(ctx.renderer, &hud_bg);
            
            int draw_x = hud_x + padding;
            int draw_y = hud_y + padding;
            for (const auto& item : row_one) {
                if (item.text.empty()) continue;
                render_text(ctx.renderer, ctx.font.get(), item.text, draw_x, draw_y, text_width(item.text), item.height, item.color);
                draw_x += text_width(item.text) + gap;
            }
            draw_x = hud_x + padding;
            draw_y += row_one_height + row_gap;
            for (const auto& item : row_two) {
                if (item.text.empty()) continue;
                render_text(ctx.renderer, ctx.font.get(), item.text, draw_x, draw_y, text_width(item.text), item.height, item.color);
                draw_x += text_width(item.text) + gap;
            }
        }
        
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
        
        const int panel_w = 300;
        const int panel_h = 400;
        const int panel_x = ctx.window_width / 2 - panel_w / 2;
        const int panel_y = ctx.window_height / 2 - panel_h / 2;
        
        SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
        SDL_SetRenderDrawColor(ctx.renderer, 40, 40, 60, 230);
        SDL_RenderFillRect(ctx.renderer, &panel);
        SDL_SetRenderDrawColor(ctx.renderer, 100, 100, 140, 255);
        SDL_RenderDrawRect(ctx.renderer, &panel);
        
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
