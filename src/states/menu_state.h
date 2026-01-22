#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class MenuState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Menu; }

private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;
    
    void init_menu(GameContext& ctx) {
        menu_.clear();
        
        menu_.add(MenuItem{"New Game", [&ctx]() { enter_gen(ctx); }, RaIcon::Flower});
        menu_.add(MenuItem{"Settings", [&ctx]() { replace_state(ctx, GameMode::Settings); }, RaIcon::Tower});
        menu_.add(MenuItem{"Labyrinth", [&ctx]() { enter_labyrinth(ctx); }, RaIcon::Tower});
        menu_.add(MenuItem{"Load", [&ctx]() { enter_load(ctx); }, RaIcon::Load});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [&ctx]() { ctx.quit = true; }, RaIcon::Reverse});
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
                set_pick(ctx, evt.x, evt.y);
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            handle_fullscreen_key(ctx, event.key.keysym.sym);
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
