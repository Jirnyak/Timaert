#pragma once

#include "game_state.h"
#include <array>

class MenuState : public GameState
{
private:
    static constexpr std::array<const char*, 3> menu_items = {
        "New Game",
        "Load",
        "Exit"
    };
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (event.type == SDL_MOUSEBUTTONDOWN)
        {
            ctx.pick_x = ctx.curs_x;
            ctx.pick_y = ctx.curs_y;
            ctx.picked = true;  
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    break;
                case SDLK_0:
                    ctx.fullscreen = !ctx.fullscreen;
                    if (ctx.fullscreen)
                        SDL_SetWindowFullscreen(ctx.window, SDL_WINDOW_FULLSCREEN);
                    else
                        SDL_SetWindowFullscreen(ctx.window, 0);
                    break;
                default:
                    break;
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

        int box_y = ctx.window_height / 3;
        SDL_Rect ui{};

        for (std::size_t i = 0; i < menu_items.size(); ++i) 
        {
            ui.w = ctx.window_width / 3;
            ui.h = ctx.window_height / 10;
            const int box_x = ctx.window_width / 2 - ui.w / 2;

            ui.x = box_x;
            ui.y = box_y;

            const std::string text = menu_items[i];

            if (ctx.curs_x > ui.x && ctx.curs_x < ui.x + ui.w && 
                ctx.curs_y > ui.y && ctx.curs_y < ui.y + ui.h) 
            {
                SDL_SetRenderDrawColor(ctx.renderer, 0, 255, 0, 255);

                if (ctx.picked) 
                {
                    ctx.picked = false;
                    switch(i)
                    {
                        case 0:
                            ctx.game_mod = GameMode::Gen;
                            break;
                        case 1:
                            ctx.game_mod = GameMode::Load;
                            break;
                        case 2:
                            ctx.quit = true;
                            break;
                    }
                }
            } 
            else 
            {
                SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 255);
            }

            SDL_RenderFillRect(ctx.renderer, &ui);
            render_text(ctx.renderer, ctx.font.get(), text, ui.x + ui.w / 4, ui.y + ui.h / 4, ui.w / 2, ui.h / 2, {255, 255, 255, 255});

            box_y += ui.h + 20;
        }
    }
};
