#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class SettingsState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Settings; }

private:
    UIButtonGroup buttons_;
    bool buttons_initialized_ = false;
    InputManager input_manager_;

    void init_buttons(GameContext& ctx) {
        buttons_.clear();
        
        // Continents decrease
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 200, ctx.window_height / 2 - 80, 40, 40},
            "-",
            [&ctx]() {
                if (ctx.num_continents > 0) ctx.num_continents--;
            }
        });
        
        // Continents increase
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 + 160, ctx.window_height / 2 - 80, 40, 40},
            "+",
            [&ctx]() {
                if (ctx.num_continents < 10) ctx.num_continents++;
            }
        });
        
        // Water decrease
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 200, ctx.window_height / 2, 40, 40},
            "-",
            [&ctx]() {
                if (ctx.water_amount > 0) ctx.water_amount--;
            }
        });
        
        // Water increase
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 + 160, ctx.window_height / 2, 40, 40},
            "+",
            [&ctx]() {
                if (ctx.water_amount < 10) ctx.water_amount++;
            }
        });
        
        // Generate button
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 70, ctx.window_height / 2 + 100, 140, 45},
            "Generate",
            [&ctx]() { enter_gen(ctx); }
        });
        
        // Back button
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 70, ctx.window_height / 2 + 160, 140, 45},
            "Back",
            [&ctx]() { enter_menu(ctx); }
        });
        
        buttons_initialized_ = true;
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);

        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                buttons_.handle_press(evt.x, evt.y);
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                enter_menu(ctx);
            }
            else {
                handle_fullscreen_key(ctx, event.key.keysym.sym);
            }
        }
    }

    void update(GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        SDL_Rect bg = textures.tile_background();
        SDL_RenderCopy(ctx.renderer, textures.bg(0), nullptr, &bg);

        // Title
        render_text(ctx, "Map Generation Settings", ctx.window_width / 2 - 200, 50, 400, 40, {255, 200, 100, 255});

        // Continents label and value
        render_text(ctx, "Continents:", ctx.window_width / 2 - 200, ctx.window_height / 2 - 95, 150, 30, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.num_continents) + " / 10", ctx.window_width / 2 - 50, ctx.window_height / 2 - 95, 150, 30, {255, 255, 100, 255});

        // Water label and value
        render_text(ctx, "Water:", ctx.window_width / 2 - 200, ctx.window_height / 2 - 15, 150, 30, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.water_amount) + " / 10", ctx.window_width / 2 - 50, ctx.window_height / 2 - 15, 150, 30, {100, 150, 255, 255});

        // Render buttons
        buttons_.render(ctx);

        // Instructions
        render_text(ctx, "Tap +/- to adjust. More continents = more landmass. More water = archipelago.", 
                    ctx.window_width / 2 - 350, ctx.window_height - 60, 700, 30, {150, 150, 150, 255});
    }
};
