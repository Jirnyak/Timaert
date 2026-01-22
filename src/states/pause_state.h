#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "systems/save_game.h"
#include "ui/ui_events.h"

class PauseState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Pause; }
    [[nodiscard]] bool is_overlay() const noexcept override { return true; }

private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;

    void init_menu(GameContext& ctx, EntityManager& entities) {
        menu_.clear();
        
        menu_.add(MenuItem{"Resume", [&ctx]() { pop_state(ctx, false); }, RaIcon::Forward});
        menu_.add(MenuItem{"Save", [&ctx, &entities]() {
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, entities, *ctx.world_manager);
            }
        }, RaIcon::Save});
        menu_.add(MenuItem{"Load", [&ctx]() { enter_load(ctx); }, RaIcon::Load});
        menu_.add(MenuItem{"To main menu", [&ctx]() {
            enter_menu(ctx);
        }, RaIcon::CastleEmblem});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [&ctx, &entities]() { 
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, entities, *ctx.world_manager);
            }
            ctx.quit = true; 
        }, RaIcon::Reverse});
#endif
        
        menu_initialized_ = true;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        if (!menu_initialized_) init_menu(ctx, entities);
        
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                set_pick(ctx, evt.x, evt.y);
            }
        }
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        {
            pop_state(ctx);
        }
    }
    
    void update(GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, ui_color("#000000B4"));

        const int title_h = ctx.window_height / 12;
        const std::string title = "PAUSED";
        render_text(ctx, title, 
                    ctx.window_width / 2 - static_cast<int>(title.size()) * title_h / 4, 
                    ctx.window_height / 6, 
                    static_cast<int>(title.size()) * title_h / 2, title_h, 
                    {255, 255, 255, 255});

        menu_.render_and_handle(
            ctx,
            ctx.window_width / 2, ctx.window_height / 3,
            ctx.window_width / 3, ctx.window_height / 12, 15,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};
