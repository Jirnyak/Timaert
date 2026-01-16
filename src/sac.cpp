#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <print>

#include "game_context.h"
#include "texture_manager.h"
#include "entity_manager.h"
#include "menu_state.h"
#include "gen_state.h"
#include "load_state.h"
#include "play_state.h"
#include "map_state.h"

class Faction
{
public:
    std::uint8_t number{};
    std::uint8_t R{};
    std::uint8_t G{};
    std::uint8_t B{};

    Faction(int num, rng_t& rng)
        : number(static_cast<std::uint8_t>(num))
        , R(static_cast<std::uint8_t>(randomer(rng, 255)))
        , G(static_cast<std::uint8_t>(randomer(rng, 255)))
        , B(static_cast<std::uint8_t>(randomer(rng, 255)))
    {} 
};

int main(int /*argc*/, char** /*argv*/) 
{
    SDLSubsystem subsystem;
    
    if (!subsystem.init_sdl()) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }
    
    if (!subsystem.init_ttf()) {
        std::println(stderr, "TTF_Init failed: {}", TTF_GetError());
        return EXIT_FAILURE;
    }
    
    if (!subsystem.init_img(IMG_INIT_PNG)) {
        std::println(stderr, "IMG_Init failed: {}", IMG_GetError());
        return EXIT_FAILURE;
    }

    SDL_DisplayMode current{};
    for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i)
    {
        SDL_GetDesktopDisplayMode(i, &current);
    }

    GameContext ctx;
    ctx.window_width = current.w;
    ctx.window_height = current.h;
    ctx.screen_center_x = ctx.window_width / 2;
    ctx.screen_center_y = ctx.window_height / 2;

    SDL_CreateWindowAndRenderer(ctx.window_width, ctx.window_height, 0, &ctx.window, &ctx.renderer);
    SDL_SetWindowFullscreen(ctx.window, 0);

    ctx.font.reset(TTF_OpenFont("Roboto-Black.ttf", 20));
    if (!ctx.font) {
        std::println(stderr, "Failed to load font: {}", TTF_GetError());
    }

    ctx.init_world();
    ctx.last_frame_time = SDL_GetTicks();

    TextureManager textures;
    textures.init(ctx.renderer, ctx.window_width, ctx.window_height);

    EntityManager entities;

    MenuState menu_state;
    GenState gen_state;
    LoadState load_state;
    PlayState play_state;
    MapState map_state;

    SDL_Event event{};

    while (!ctx.quit) 
    {
        ctx.frame = static_cast<int>(SDL_GetTicks());
        SDL_GetMouseState(&ctx.curs_x, &ctx.curs_y);

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                ctx.quit = true;
                break;
            }

            switch(ctx.game_mod)
            {
                case GameMode::Menu:
                    menu_state.handle_event(event, ctx, textures, entities);
                    break;
                case GameMode::Game:
                    play_state.handle_event(event, ctx, textures, entities);
                    break;
                case GameMode::Map:
                    map_state.handle_event(event, ctx, textures, entities);
                    break;
                case GameMode::Gen:
                    gen_state.handle_event(event, ctx, textures, entities);
                    break;
                case GameMode::Load:
                    load_state.handle_event(event, ctx, textures, entities);
                    break;
                default:
                    break;
            }
        }

        switch(ctx.game_mod)
        {
            case GameMode::Menu:
                menu_state.update(ctx, textures, entities);
                menu_state.render(ctx, textures, entities);
                break;
            case GameMode::Gen:
                gen_state.update(ctx, textures, entities);
                gen_state.render(ctx, textures, entities);
                break;
            case GameMode::Load:
                load_state.update(ctx, textures, entities);
                load_state.render(ctx, textures, entities);
                break;
            case GameMode::Game:
                play_state.update(ctx, textures, entities);
                play_state.render(ctx, textures, entities);
                break;
            case GameMode::Map:
                map_state.update(ctx, textures, entities);
                map_state.render(ctx, textures, entities);
                break;
            default:
                break;
        }

        SDL_RenderPresent(ctx.renderer);

        entities.rebuild_pos_map(ctx.pos_map);

        if (ctx.screenshot)
        {
            SDLSurfacePtr shot{SDL_CreateRGBSurface(0, ctx.window_width, ctx.window_height, 32, 0, 0, 0, 0)};
            if (shot) {
                SDL_RenderReadPixels(ctx.renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
                SDL_SaveBMP(shot.get(), "save.png");
            }
            ctx.screenshot = false;
        }
    }

    for (const auto& [pos, ids] : ctx.pos_map)
    {
        std::print("Position {} has entities: ", pos);
        for (const int eid : ids)
            std::print("{} ", eid);
        std::println("");
    }

    return EXIT_SUCCESS;
}