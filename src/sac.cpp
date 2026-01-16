#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <iostream>
#else
#include <print>
#endif

#include "game_context.h"
#include "texture_manager.h"
#include "entity_manager.h"
#include "menu_state.h"
#include "gen_state.h"
#include "load_state.h"
#include "play_state.h"
#include "map_state.h"
#include "pause_state.h"

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

#ifdef __EMSCRIPTEN__
struct EmscriptenState {
    GameContext* ctx;
    TextureManager* textures;
    EntityManager* entities;
    MenuState* menu_state;
    GenState* gen_state;
    LoadState* load_state;
    PlayState* play_state;
    MapState* map_state;
    PauseState* pause_state;
};

static EmscriptenState* g_state = nullptr;

void emscripten_main_loop() {
    auto& ctx = *g_state->ctx;
    auto& textures = *g_state->textures;
    auto& entities = *g_state->entities;
    auto& menu_state = *g_state->menu_state;
    auto& gen_state = *g_state->gen_state;
    auto& load_state = *g_state->load_state;
    auto& play_state = *g_state->play_state;
    auto& map_state = *g_state->map_state;
    auto& pause_state = *g_state->pause_state;

    ctx.frame = static_cast<int>(SDL_GetTicks());
    SDL_GetMouseState(&ctx.curs_x, &ctx.curs_y);

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            ctx.quit = true;
            emscripten_cancel_main_loop();
            return;
        }

        switch(ctx.game_mod) {
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
            case GameMode::Pause:
                pause_state.handle_event(event, ctx, textures, entities);
                break;
            default:
                break;
        }
    }

    switch(ctx.game_mod) {
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
        case GameMode::Pause:
            play_state.render(ctx, textures, entities);
            pause_state.update(ctx, textures, entities);
            pause_state.render(ctx, textures, entities);
            break;
        default:
            break;
    }

    SDL_RenderPresent(ctx.renderer);
    entities.rebuild_pos_map(ctx.pos_map);

    if (ctx.screenshot) {
        ctx.screenshot = false;
    }

    if (ctx.quit) {
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int /*argc*/, char** /*argv*/) 
{
    SDLSubsystem subsystem;
    
    if (!subsystem.init_sdl()) {
#ifdef __EMSCRIPTEN__
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
#else
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
#endif
        return EXIT_FAILURE;
    }
    
    if (!subsystem.init_ttf()) {
#ifdef __EMSCRIPTEN__
        std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
#else
        std::println(stderr, "TTF_Init failed: {}", TTF_GetError());
#endif
        return EXIT_FAILURE;
    }
    
    if (!subsystem.init_img(IMG_INIT_PNG)) {
#ifdef __EMSCRIPTEN__
        std::cerr << "IMG_Init failed: " << IMG_GetError() << std::endl;
#else
        std::println(stderr, "IMG_Init failed: {}", IMG_GetError());
#endif
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
#ifdef __EMSCRIPTEN__
        std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
#else
        std::println(stderr, "Failed to load font: {}", TTF_GetError());
#endif
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
    PauseState pause_state;

#ifdef __EMSCRIPTEN__
    EmscriptenState state{
        &ctx, &textures, &entities,
        &menu_state, &gen_state, &load_state, &play_state, &map_state, &pause_state
    };
    g_state = &state;
    
    emscripten_set_main_loop(emscripten_main_loop, 0, 1);
#else
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
                case GameMode::Pause:
                    pause_state.handle_event(event, ctx, textures, entities);
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
            case GameMode::Pause:
                play_state.render(ctx, textures, entities);
                pause_state.update(ctx, textures, entities);
                pause_state.render(ctx, textures, entities);
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
#endif

    return EXIT_SUCCESS;
}