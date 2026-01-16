#pragma once

#include "game_state.h"
#include <cmath>

class MapState : public GameState
{
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            ctx.map_dragging = true;
            ctx.drag_last_x = event.button.x;
            ctx.drag_last_y = event.button.y;
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
        {
            ctx.map_dragging = false;
        }
        else if (event.type == SDL_MOUSEMOTION && ctx.map_dragging)
        {
            const int dx = event.motion.x - ctx.drag_last_x;
            const int dy = event.motion.y - ctx.drag_last_y;
            
            ctx.map_offset_x += static_cast<float>(dx);
            ctx.map_offset_y += static_cast<float>(dy);
            
            ctx.velocity_x = ctx.velocity_x * 0.5f + static_cast<float>(dx) * 0.5f;
            ctx.velocity_y = ctx.velocity_y * 0.5f + static_cast<float>(dy) * 0.5f;
            
            ctx.drag_last_x = event.motion.x;
            ctx.drag_last_y = event.motion.y;
        }
        else if (event.type == SDL_FINGERDOWN)
        {
            ctx.map_dragging = true;
            ctx.drag_last_x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            ctx.drag_last_y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            ctx.velocity_x = 0.0f;
            ctx.velocity_y = 0.0f;
        }
        else if (event.type == SDL_FINGERUP)
        {
            ctx.map_dragging = false;
        }
        else if (event.type == SDL_FINGERMOTION)
        {
            const int new_x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
            const int new_y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            const int dx = new_x - ctx.drag_last_x;
            const int dy = new_y - ctx.drag_last_y;
            
            ctx.map_offset_x += static_cast<float>(dx);
            ctx.map_offset_y += static_cast<float>(dy);
            
            ctx.velocity_x = ctx.velocity_x * 0.5f + static_cast<float>(dx) * 0.5f;
            ctx.velocity_y = ctx.velocity_y * 0.5f + static_cast<float>(dy) * 0.5f;
            
            ctx.drag_last_x = new_x;
            ctx.drag_last_y = new_y;
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    ctx.quit = true;
                    break;
                case SDLK_0:
                    ctx.fullscreen = !ctx.fullscreen;
                    if (ctx.fullscreen)
                        SDL_SetWindowFullscreen(ctx.window, SDL_WINDOW_FULLSCREEN);
                    else
                        SDL_SetWindowFullscreen(ctx.window, 0);
                    break;
                case SDLK_RETURN:
                    ctx.game_mod = GameMode::Game;
                    ctx.picked = false;
                    ctx.map_offset_x = 0.0f;
                    ctx.map_offset_y = 0.0f;
                    ctx.velocity_x = 0.0f;
                    ctx.velocity_y = 0.0f;
                    break;
                case SDLK_k:
                    ctx.screenshot = true;
                    break;
                default:
                    break;
            }
        }
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        const std::uint32_t current_time = SDL_GetTicks();
        float delta_time = static_cast<float>(current_time - ctx.last_frame_time) / 16.67f;
        ctx.last_frame_time = current_time;
        if (delta_time > 3.0f) delta_time = 3.0f;
        
        if (!ctx.map_dragging)
        {
            ctx.map_offset_x += ctx.velocity_x * delta_time;
            ctx.map_offset_y += ctx.velocity_y * delta_time;
            
            ctx.velocity_x *= std::pow(ctx.friction, delta_time);
            ctx.velocity_y *= std::pow(ctx.friction, delta_time);
            
            if (std::abs(ctx.velocity_x) < ctx.velocity_threshold) ctx.velocity_x = 0.0f;
            if (std::abs(ctx.velocity_y) < ctx.velocity_threshold) ctx.velocity_y = 0.0f;
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx.renderer);
        
        SDL_Rect ui{};
        ui.w = ctx.window_height;
        ui.h = ctx.window_height;
        ui.x = ctx.window_width / 2 - ui.w / 2 + static_cast<int>(ctx.map_offset_x);
        ui.y = ctx.window_height / 2 - ui.h / 2 + static_cast<int>(ctx.map_offset_y);
        
        SDL_RenderCopy(ctx.renderer, ctx.world_image.get(), nullptr, &ui);
    }
};
