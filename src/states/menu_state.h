#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class MenuState : public GameState
{
private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;
    
    void init_menu(GameContext& ctx) {
        menu_.clear();
        
        menu_.add(MenuItem{"New Game", [&ctx]() { ctx.game_mod = GameMode::Gen; }});
        menu_.add(MenuItem{"Labyrinth", [&ctx]() { ctx.game_mod = GameMode::Labyrinth; }});
        menu_.add(MenuItem{"Load", [&ctx]() { ctx.game_mod = GameMode::Load; }});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [&ctx]() { ctx.quit = true; }});
#endif
        
        menu_initialized_ = true;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!menu_initialized_) init_menu(ctx);
        
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                ctx.pick_x = evt.x;
                ctx.pick_y = evt.y;
                ctx.picked = true;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_0)
            {
                ctx.fullscreen = !ctx.fullscreen;
                SDL_SetWindowFullscreen(ctx.window, ctx.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
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

        menu_.render_and_handle(
            ctx,
            ctx.window_width / 2, ctx.window_height / 3,
            ctx.window_width / 3, ctx.window_height / 10, 20,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};
