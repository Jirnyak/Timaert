#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <print>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <iostream>
#else
#include <print>
#endif
#include <string>

#include "game_context.h"
#include "texture_manager.h"
#include "entity_manager.h"
#include "world_manager.h"
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

namespace {
constexpr double kPerfLogIntervalMs = 2000.0;
constexpr double kLagFrameMs = 33.33;

struct PerfStats {
    std::uint64_t frame_count = 0;
    double accum_frame_ms = 0.0;
    double accum_update_ms = 0.0;
    double accum_post_ms = 0.0;
    std::uint64_t last_log_ticks = 0;
};

[[nodiscard]] int count_active_entities(const EntityManager& entities)
{
    int count = 0;
    for (const auto& entity : entities.entities())
    {
        if (entity.active) ++count;
    }
    return count;
}

void log_perf_stats(const PerfStats& stats, GameMode mode, int entity_count, int npc_count)
{
    const double avg_frame_ms = stats.accum_frame_ms / static_cast<double>(stats.frame_count);
    const double avg_update_ms = stats.accum_update_ms / static_cast<double>(stats.frame_count);
    const double avg_post_ms = stats.accum_post_ms / static_cast<double>(stats.frame_count);
    const double fps = avg_frame_ms > 0.0 ? 1000.0 / avg_frame_ms : 0.0;

    std::string suspects;
    if (avg_update_ms > avg_frame_ms * 0.8)
    {
        suspects += "update/render-bound";
    }
    if (avg_post_ms > avg_frame_ms * 0.25)
    {
        if (!suspects.empty()) suspects += ", ";
        suspects += "present-bound";
    }
    if (entity_count > 1500)
    {
        if (!suspects.empty()) suspects += ", ";
        suspects += "entity-heavy";
    }
    if (npc_count > 800)
    {
        if (!suspects.empty()) suspects += ", ";
        suspects += "npc-heavy";
    }
    if (suspects.empty())
    {
        suspects = "unknown (profile for cache misses)";
    }

#ifdef __EMSCRIPTEN__
    std::cerr << "[perf] fps=" << fps
              << " frame_ms=" << avg_frame_ms
              << " update_ms=" << avg_update_ms
              << " post_ms=" << avg_post_ms
              << " mode=" << static_cast<int>(mode)
              << " entities=" << entity_count
              << " npcs=" << npc_count
              << " suspects=" << suspects
              << std::endl;
#else
    std::println("[perf] fps={:.1f} frame_ms={:.2f} update_ms={:.2f} post_ms={:.2f} mode={} entities={} npcs={} suspects={}",
                fps, avg_frame_ms, avg_update_ms, avg_post_ms, static_cast<int>(mode), entity_count, npc_count, suspects);
#endif
}
} // namespace

#ifdef __EMSCRIPTEN__
struct EmscriptenState {
    GameContext* ctx;
    TextureManager* textures;
    EntityManager* entities;
    WorldManager* world_manager;
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

    const std::uint64_t perf_freq = SDL_GetPerformanceFrequency();
    static PerfStats perf_stats{};
    const std::uint64_t frame_start = SDL_GetPerformanceCounter();

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            ctx.quit = true;
            emscripten_cancel_main_loop();
            return;
        }
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             event.window.event == SDL_WINDOWEVENT_RESIZED)) {
            ctx.window_width = event.window.data1;
            ctx.window_height = event.window.data2;
            ctx.screen_center_x = ctx.window_width / 2;
            ctx.screen_center_y = ctx.window_height / 2;
            textures.tile_background().w = ctx.window_width;
            textures.tile_background().h = ctx.window_height;
            ctx.window_dirty = true;
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

    const std::uint64_t update_end = SDL_GetPerformanceCounter();

    SDL_RenderPresent(ctx.renderer);

    if (ctx.game_mod != GameMode::Game) {
        entities.rebuild_pos_map(ctx.pos_map, true);
    }

    const std::uint64_t frame_end = SDL_GetPerformanceCounter();
    const std::uint64_t post_end = frame_end;

    const double frame_ms = (static_cast<double>(frame_end - frame_start) * 1000.0) / static_cast<double>(perf_freq);
    const double update_ms = (static_cast<double>(update_end - frame_start) * 1000.0) / static_cast<double>(perf_freq);
    const double post_ms = (static_cast<double>(post_end - update_end) * 1000.0) / static_cast<double>(perf_freq);

    perf_stats.frame_count += 1;
    perf_stats.accum_frame_ms += frame_ms;
    perf_stats.accum_update_ms += update_ms;
    perf_stats.accum_post_ms += post_ms;

    const std::uint32_t now_ticks = SDL_GetTicks();
    if (perf_stats.last_log_ticks == 0) {
        perf_stats.last_log_ticks = now_ticks;
    }
    const double elapsed_ms = static_cast<double>(now_ticks - perf_stats.last_log_ticks);
    if (elapsed_ms >= kPerfLogIntervalMs || frame_ms >= kLagFrameMs) {
        const int entity_count = count_active_entities(entities);
        const int npc_count = g_state->world_manager->npcs.active_count();
        log_perf_stats(perf_stats, ctx.game_mod, entity_count, npc_count);
        perf_stats = {};
        perf_stats.last_log_ticks = now_ticks;
    }

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
    ctx.window_width = static_cast<int>(current.w * 0.75f);
    ctx.window_height = static_cast<int>(current.h * 0.75f);
    ctx.screen_center_x = ctx.window_width / 2;
    ctx.screen_center_y = ctx.window_height / 2;

    SDL_CreateWindowAndRenderer(ctx.window_width, ctx.window_height, SDL_WINDOW_RESIZABLE, &ctx.window, &ctx.renderer);
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

    WorldManager world_manager;
    
    MenuState menu_state;
    GenState gen_state;
    LoadState load_state;
    PlayState play_state;
    MapState map_state;
    PauseState pause_state;
    
    gen_state.set_world_manager(&world_manager);
    play_state.set_world_manager(&world_manager);

#ifdef __EMSCRIPTEN__
    EmscriptenState state{
        &ctx, &textures, &entities, &world_manager,
        &menu_state, &gen_state, &load_state, &play_state, &map_state, &pause_state
    };
    g_state = &state;
    
    emscripten_set_main_loop(emscripten_main_loop, 0, 1);
#else
    SDL_Event event{};
    const std::uint64_t perf_freq = SDL_GetPerformanceFrequency();
    PerfStats perf_stats{};

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
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                ctx.window_width = event.window.data1;
                ctx.window_height = event.window.data2;
                ctx.screen_center_x = ctx.window_width / 2;
                ctx.screen_center_y = ctx.window_height / 2;
                textures.tile_background().w = ctx.window_width;
                textures.tile_background().h = ctx.window_height;
                ctx.window_dirty = true;
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

        const std::uint64_t frame_start = SDL_GetPerformanceCounter();
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

        const std::uint64_t update_end = SDL_GetPerformanceCounter();

        SDL_RenderPresent(ctx.renderer);

        if (ctx.game_mod != GameMode::Game) {
            entities.rebuild_pos_map(ctx.pos_map, true);
        }

        const std::uint64_t frame_end = SDL_GetPerformanceCounter();
        const double frame_ms = (static_cast<double>(frame_end - frame_start) * 1000.0) / static_cast<double>(perf_freq);
        const double update_ms = (static_cast<double>(update_end - frame_start) * 1000.0) / static_cast<double>(perf_freq);
        const double post_ms = (static_cast<double>(frame_end - update_end) * 1000.0) / static_cast<double>(perf_freq);

        perf_stats.frame_count += 1;
        perf_stats.accum_frame_ms += frame_ms;
        perf_stats.accum_update_ms += update_ms;
        perf_stats.accum_post_ms += post_ms;

        const std::uint32_t now_ticks = SDL_GetTicks();
        if (perf_stats.last_log_ticks == 0) {
            perf_stats.last_log_ticks = now_ticks;
        }
        const double elapsed_ms = static_cast<double>(now_ticks - perf_stats.last_log_ticks);
        if (elapsed_ms >= kPerfLogIntervalMs || frame_ms >= kLagFrameMs) {
            const int entity_count = count_active_entities(entities);
            const int npc_count = world_manager.npcs.active_count();
            log_perf_stats(perf_stats, ctx.game_mod, entity_count, npc_count);
            perf_stats = {};
            perf_stats.last_log_ticks = now_ticks;
        }

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
#endif

    return EXIT_SUCCESS;
}