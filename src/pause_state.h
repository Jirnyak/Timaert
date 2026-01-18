#pragma once

#include "game_state.h"
#include "ui.h"
#include "save_game.h"

class PauseState : public GameState
{
private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;

    void init_menu(GameContext& ctx, EntityManager& entities) {
        menu_.clear();
        
        menu_.add(MenuItem{"Resume", [&ctx]() { ctx.game_mod = GameMode::Game; }});
        menu_.add(MenuItem{"Save", [&ctx, &entities]() {
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, entities, *ctx.world_manager);
            }
        }});
        menu_.add(MenuItem{"Load", [&ctx]() {
            ctx.game_mod = GameMode::Load;
            ctx.picked = false;
        }});
        menu_.add(MenuItem{"To main menu", [&ctx]() {
            ctx.game_mod = GameMode::Menu;
            ctx.picked = false;
        }});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [&ctx, &entities]() { 
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, entities, *ctx.world_manager);
            }
            ctx.quit = true; 
        }});
#endif
        
        menu_initialized_ = true;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        if (!menu_initialized_) init_menu(ctx, entities);
        
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_FINGERDOWN)
        {
            if (event.type == SDL_FINGERDOWN)
            {
                ctx.pick_x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
                ctx.pick_y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
            }
            else
            {
                ctx.pick_x = ctx.curs_x;
                ctx.pick_y = ctx.curs_y;
            }
            ctx.picked = true;  
        }
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        {
            ctx.game_mod = GameMode::Game;
            ctx.picked = false;
        }
    }
    
    void update(GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, ui_color(0, 0, 0, 180));

        const int title_h = ctx.window_height / 12;
        const std::string title = "PAUSED";
        render_text(ctx.renderer, ctx.font.get(), title, 
                    ctx.window_width / 2 - static_cast<int>(title.size()) * title_h / 4, 
                    ctx.window_height / 6, 
                    static_cast<int>(title.size()) * title_h / 2, title_h, 
                    {255, 255, 255, 255});

        menu_.render_and_handle(
            ctx.renderer, ctx.font.get(),
            ctx.window_width / 2, ctx.window_height / 3,
            ctx.window_width / 3, ctx.window_height / 12, 15,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};
