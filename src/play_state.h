#pragma once

#include "game_state.h"
#include "input.h"
#include "ui_button.h"
#include <cmath>

class PlayState : public GameState
{
private:
    UIButtonGroup buttons_;
    bool buttons_initialized_ = false;
    
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
        
        buttons_initialized_ = true;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);
        
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            if (buttons_.handle_press(event.button.x, event.button.y)) {
                return;
            }
            ctx.map_dragging = true;
            ctx.drag_last_x = event.button.x;
            ctx.drag_last_y = event.button.y;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
        {
            buttons_.reset_pressed();
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
            ctx.map_dragging = true;
            ctx.drag_last_x = tx;
            ctx.drag_last_y = ty;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_FINGERUP)
        {
            buttons_.reset_pressed();
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
        
        row_start_idx = pos_idx;
        for (int a = 0; a < tiles_y; a++)
        {
            int pos_line_idx = row_start_idx;
            for (int b = 0; b < tiles_x; b++)
            {
                if (!ctx.pos_map[pos_line_idx].empty()) 
                {
                    draw_tile.x = b * scaled_tile_size - (tiles_x / 2) * scaled_tile_size + ctx.window_width / 2 + pixel_offset_x;
                    draw_tile.y = a * scaled_tile_size - (tiles_y / 2) * scaled_tile_size + ctx.window_height / 2 + pixel_offset_y;
                    
                    if (draw_tile.x + scaled_tile_size > 0 && draw_tile.x < ctx.window_width &&
                        draw_tile.y + scaled_tile_size > 0 && draw_tile.y < ctx.window_height)
                    {
                        draw_tile.w = scaled_tile_size;
                        draw_tile.h = scaled_tile_size;
                        const int entity_id = ctx.pos_map[pos_line_idx][0];
                        SDL_RenderCopy(ctx.renderer, textures.sprite(entities[static_cast<std::size_t>(entity_id)].type), nullptr, &draw_tile);
                    }
                }
                pos_line_idx = ctx.get_neighbor(pos_line_idx, 3);
            }
            row_start_idx = ctx.get_neighbor(row_start_idx, 2);
        }
        
        SDL_Rect tile = {0, 0, 0, 0};
        
        if (ctx.paused)
        {
            const std::string text = "paused";
            render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 0, 0, 255});
        }
        else
        {
            const std::string text = "unpaused";
            render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        }
        
        tile.y += 10;
        std::string text = "us per tick: " + std::to_string(SDL_GetTicks() - static_cast<std::uint32_t>(ctx.frame));
        render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        
        tile.y += 10;
        text = "pos: " + std::to_string(ctx.pos_cam);
        render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        
        tile.y += 10;
        text = "seed: " + std::to_string(ctx.seed);
        render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        
        tile.y += 10;
        text = "you said:";
        render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        
        tile.y += 10;
        text = std::string(ctx.input.data());
        render_text(ctx.renderer, ctx.font.get(), text, tile.x, tile.y, static_cast<int>(text.size()) * 10, 10, {255, 255, 255, 255});
        
        if (buttons_initialized_) {
            buttons_.render(ctx.renderer, ctx.font.get());
        }
    }
};
