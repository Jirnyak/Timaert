#pragma once

#include "game_state.h"
#include "input.h"
#include "ui_button.h"
#include "world_manager.h"
#include <cmath>

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
                          int pixel_offset_x, int pixel_offset_y, int tiles_x, int tiles_y, int top_left_pos)
    {
        if (!world_manager_) return;
        
        world_manager_->npcs.for_each_active([&](const NPC& npc) {
            if (npc.state == NPCState::Dead) return;
            
            int tile_x = -1, tile_y = -1;
            int search_pos = top_left_pos;
            for (int a = 0; a < tiles_y && tile_y < 0; ++a)
            {
                int row_pos = search_pos;
                for (int b = 0; b < tiles_x; ++b)
                {
                    if (row_pos == npc.pos)
                    {
                        tile_x = b;
                        tile_y = a;
                        break;
                    }
                    row_pos = ctx.get_neighbor(row_pos, 3);
                }
                search_pos = ctx.get_neighbor(search_pos, 2);
            }
            
            if (tile_x < 0 || tile_y < 0) return;
            
            SDL_Rect draw_tile;
            draw_tile.x = tile_x * scaled_tile_size - (tiles_x / 2) * scaled_tile_size + ctx.window_width / 2 + pixel_offset_x;
            draw_tile.y = tile_y * scaled_tile_size - (tiles_y / 2) * scaled_tile_size + ctx.window_height / 2 + pixel_offset_y;
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
            p.clear_aim();
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
    
    void handle_tap_to_move(GameContext& /*ctx*/, int /*screen_x*/, int /*screen_y*/)
    {
        // Tap-to-move disabled - use arrow buttons instead
    }
    
public:
    void set_world_manager(WorldManager* wm) { world_manager_ = wm; }
    
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);
        
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            if (buttons_.handle_press(event.button.x, event.button.y)) {
                return;
            }
            if (move_buttons_.handle_press(event.button.x, event.button.y)) {
                return;
            }
            if (action_buttons_.handle_press(event.button.x, event.button.y)) {
                return;
            }
            ctx.map_dragging = true;
            ctx.drag_last_x = event.button.x;
            ctx.drag_last_y = event.button.y;
            drag_start_x_ = event.button.x;
            drag_start_y_ = event.button.y;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
        {
            buttons_.reset_pressed();
            move_buttons_.reset_pressed();
            action_buttons_.reset_pressed();
            
            const int drag_dist = std::abs(event.button.x - drag_start_x_) + std::abs(event.button.y - drag_start_y_);
            if (drag_dist < 10)
            {
                handle_tap_to_move(ctx, event.button.x, event.button.y);
            }
            ctx.map_dragging = false;
        }
        else if (event.type == SDL_MOUSEMOTION && ctx.map_dragging)
        {
            const int dx = event.motion.x - ctx.drag_last_x;
            const int dy = event.motion.y - ctx.drag_last_y;
            
            ctx.map_offset_x += static_cast<float>(dx) / ctx.zoom;
            ctx.map_offset_y += static_cast<float>(dy) / ctx.zoom;
            
            ctx.velocity_x = ctx.velocity_x * 0.5f + (static_cast<float>(dx) / ctx.zoom) * 0.5f;
            ctx.velocity_y = ctx.velocity_y * 0.5f + (static_cast<float>(dy) / ctx.zoom) * 0.5f;
            
            ctx.drag_last_x = event.motion.x;
            ctx.drag_last_y = event.motion.y;
        }
        else if (event.type == SDL_FINGERDOWN)
        {
            const int tx = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            const int ty = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
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
        const std::uint32_t current_time = SDL_GetTicks();
        float delta_time = static_cast<float>(current_time - ctx.last_frame_time) / 16.67f;
        ctx.last_frame_time = current_time;
        if (delta_time > 3.0f) delta_time = 3.0f;
        
        if (pending_move_dir_ >= 0)
        {
            move_player_direction(pending_move_dir_, ctx);
            pending_move_dir_ = -1;
        }
        
        if (center_requested_)
        {
            center_on_player(ctx);
            center_requested_ = false;
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
        
        constexpr float scaled_tile = static_cast<float>(TILE_SIZE);
        while (ctx.map_offset_x <= -scaled_tile) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 3);
            ctx.map_offset_x += scaled_tile;
        }
        while (ctx.map_offset_x >= scaled_tile) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 1);
            ctx.map_offset_x -= scaled_tile;
        }
        while (ctx.map_offset_y <= -scaled_tile) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 2);
            ctx.map_offset_y += scaled_tile;
        }
        while (ctx.map_offset_y >= scaled_tile) {
            ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, 0);
            ctx.map_offset_y -= scaled_tile;
        }
        
        if (!ctx.paused)
        {
            for (int tick = 0; tick < ctx.game_speed; ++tick)
            {
                ctx.hour += 1;
                
                if (world_manager_)
                {
                    world_manager_->update(ctx);
                    world_manager_->rebuild_pos_map(ctx.pos_map);
                }
                
                for (auto& obj : entities.entities())
                {
                    if (!obj.active) continue;
                    
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
        
        int pos_idx = ctx.pos_cam;
        for (int i = 0; i < tiles_x / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 1);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 0);
        
        SDL_Rect draw_tile{};
        draw_tile.w = scaled_tile_size;
        draw_tile.h = scaled_tile_size;
        
        int row_start_idx = pos_idx;
        for (int a = 0; a < tiles_y; a++)
        {
            int pos_line_idx = row_start_idx;
            for (int b = 0; b < tiles_x; b++)
            {
                draw_tile.x = b * scaled_tile_size - (tiles_x / 2) * scaled_tile_size + ctx.window_width / 2 + pixel_offset_x;
                draw_tile.y = a * scaled_tile_size - (tiles_y / 2) * scaled_tile_size + ctx.window_height / 2 + pixel_offset_y;
                
                if (draw_tile.x + scaled_tile_size > 0 && draw_tile.x < ctx.window_width &&
                    draw_tile.y + scaled_tile_size > 0 && draw_tile.y < ctx.window_height)
                {
                    SDL_RenderCopy(ctx.renderer, textures.tile(ctx.relief[pos_line_idx]), nullptr, &draw_tile);
                }
                pos_line_idx = ctx.get_neighbor(pos_line_idx, 3);
            }
            row_start_idx = ctx.get_neighbor(row_start_idx, 2);
        }
        
        pos_idx = ctx.pos_cam;
        for (int i = 0; i < tiles_x / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 1);
        for (int i = 0; i < tiles_y / 2; i++)
            pos_idx = ctx.get_neighbor(pos_idx, 0);
        
        render_roads(ctx, scaled_tile_size, pixel_offset_x, pixel_offset_y, tiles_x, tiles_y, pos_idx);
        
        row_start_idx = pos_idx;
        for (int a = 0; a < tiles_y; a++)
        {
            int pos_line_idx = row_start_idx;
            for (int b = 0; b < tiles_x; b++)
            {
                draw_tile.x = b * scaled_tile_size - (tiles_x / 2) * scaled_tile_size + ctx.window_width / 2 + pixel_offset_x;
                draw_tile.y = a * scaled_tile_size - (tiles_y / 2) * scaled_tile_size + ctx.window_height / 2 + pixel_offset_y;
                
                if (draw_tile.x + scaled_tile_size > 0 && draw_tile.x < ctx.window_width &&
                    draw_tile.y + scaled_tile_size > 0 && draw_tile.y < ctx.window_height)
                {
                    draw_tile.w = scaled_tile_size;
                    draw_tile.h = scaled_tile_size;
                    
                    for (auto& obj : entities.entities())
                    {
                        if (!obj.active || obj.pos != pos_line_idx) continue;
                        SDL_RenderCopy(ctx.renderer, textures.sprite(obj.type), nullptr, &draw_tile);
                    }
                    
                    if (world_manager_)
                    {
                        const Settlement* s = world_manager_->get_settlement_at(pos_line_idx);
                        if (s)
                        {
                            ObjectType obj_type = ObjectType::Village;
                            if (s->type == SettlementType::City) obj_type = ObjectType::City;
                            else if (s->type == SettlementType::Town) obj_type = ObjectType::Town;
                            SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(obj_type)), nullptr, &draw_tile);
                        }
                        
                        
                        const Player& p = world_manager_->player_ctrl.player();
                        if (p.active && p.pos == pos_line_idx)
                        {
                            SDL_RenderCopy(ctx.renderer, textures.sprite(static_cast<std::size_t>(ObjectType::Player)), nullptr, &draw_tile);
                        }
                    }
                }
                pos_line_idx = ctx.get_neighbor(pos_line_idx, 3);
            }
            row_start_idx = ctx.get_neighbor(row_start_idx, 2);
        }
        
        render_all_npcs(ctx, textures, scaled_tile_size, pixel_offset_x, pixel_offset_y, tiles_x, tiles_y, pos_idx);
        
        SDL_Rect tile = {0, 0, 0, 0};
        
        if (world_manager_)
        {
            const Player& p = world_manager_->player_ctrl.player();
            if (p.active)
            {
                std::string text = "Gold: " + std::to_string(static_cast<int>(p.inventory.capital));
                render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 14, {255, 215, 0, 255});
                tile.y += 16;
                
                text = "HP: " + std::to_string(p.life) + "/" + std::to_string(p.max_life);
                render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 14, {255, 100, 100, 255});
                tile.y += 16;
                
                text = "Items: " + std::to_string(p.inventory.total_items()) + "/" + std::to_string(p.inventory.max_capacity);
                render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 14, {200, 200, 200, 255});
                tile.y += 16;
                
                const Settlement* at_settlement = world_manager_->get_settlement_at(p.pos);
                if (at_settlement)
                {
                    text = "At: " + at_settlement->name;
                    render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 14, {100, 255, 100, 255});
                    tile.y += 16;
                }
                
                if (p.has_aim())
                {
                    text = "Moving to: " + std::to_string(p.aim_pos);
                    render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 14, {150, 150, 255, 255});
                    tile.y += 16;
                }
            }
            
            tile.y += 8;
            std::string text = "Settlements: " + std::to_string(world_manager_->landmarks.settlement_count());
            render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 12, {180, 180, 180, 255});
            tile.y += 14;
            
            text = "NPCs: " + std::to_string(world_manager_->npcs.active_count());
            render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 12, {180, 180, 180, 255});
            tile.y += 14;
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
