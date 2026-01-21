#pragma once

#include "core/game_state.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

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
                    begin_map_drag(ctx);
                    break;
                
                case InputAction::Drag:
                    apply_map_drag(ctx, static_cast<float>(evt.dx), static_cast<float>(evt.dy));
                    break;

                case InputAction::Release:
                    end_map_drag(ctx);
                    break;
                    
                default: break;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    enter_pause(ctx);
                    break;
                case SDLK_0:
                    handle_fullscreen_key(ctx, event.key.keysym.sym);
                    break;
                case SDLK_RETURN:
                    enter_game(ctx);
                    reset_map_view(ctx);
                    break;
                case SDLK_k:
                    trigger_screenshot(ctx);
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
        const float delta_time = calc_frame_delta_time(ctx);
        
        update_map_inertia(ctx, delta_time);

        if (ctx.map_dragging || ctx.velocity_x != 0.0f || ctx.velocity_y != 0.0f) {
            ctx.redraw_requested = true;
        }
        if (ctx.map_offset_x != prev_offset_x || ctx.map_offset_y != prev_offset_y) {
            ctx.redraw_requested = true;
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear_black(ctx.renderer);
        
        const int size = ctx.window_height;
        SDL_Rect ui = centered_rect(
            ctx,
            size,
            size,
            static_cast<int>(ctx.map_offset_x),
            static_cast<int>(ctx.map_offset_y));
        
        SDL_RenderCopy(ctx.renderer, ctx.world_image.get(), nullptr, &ui);
    }
};
