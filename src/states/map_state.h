#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include <cmath>

class MapState : public GameState
{
private:
    InputManager input_manager_;

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            switch (evt.action)
            {
                case InputAction::Press:
                    ctx.map_dragging = true;
                    ctx.velocity_x = 0.0f;
                    ctx.velocity_y = 0.0f;
                    break;
                
                case InputAction::Drag:
                    if (ctx.map_dragging) {
                        ctx.map_offset_x += static_cast<float>(evt.dx);
                        ctx.map_offset_y += static_cast<float>(evt.dy);
                        
                        ctx.velocity_x = ctx.velocity_x * 0.5f + static_cast<float>(evt.dx) * 0.5f;
                        ctx.velocity_y = ctx.velocity_y * 0.5f + static_cast<float>(evt.dy) * 0.5f;
                    }
                    break;

                case InputAction::Release:
                    ctx.map_dragging = false;
                    break;
                    
                default: break;
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
        const float prev_offset_x = ctx.map_offset_x;
        const float prev_offset_y = ctx.map_offset_y;
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

        if (ctx.map_dragging || ctx.velocity_x != 0.0f || ctx.velocity_y != 0.0f) {
            ctx.redraw_requested = true;
        }
        if (ctx.map_offset_x != prev_offset_x || ctx.map_offset_y != prev_offset_y) {
            ctx.redraw_requested = true;
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear(ctx.renderer, ui_color("#000000"));
        
        SDL_Rect ui{};
        ui.w = ctx.window_height;
        ui.h = ctx.window_height;
        ui.x = ctx.window_width / 2 - ui.w / 2 + static_cast<int>(ctx.map_offset_x);
        ui.y = ctx.window_height / 2 - ui.h / 2 + static_cast<int>(ctx.map_offset_y);
        
        SDL_RenderCopy(ctx.renderer, ctx.world_image.get(), nullptr, &ui);
    }
};
