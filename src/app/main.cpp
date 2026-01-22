#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <print>
#include <string>
#include <fstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/game_context.h"
#include "rendering/texture_manager.h"
#include "systems/entity_manager.h"
#include "systems/world_manager.h"

// Include all states to ensure StateRegistrar static objects are instantiated
#include "states/menu_state.h"
#include "states/gen_state.h"
#include "states/load_state.h"
#include "states/play_state.h"
#include "states/pause_state.h"
#include "states/map_state.h"
#include "states/stat_state.h"
#include "states/settings_state.h"
#include "states/labyrinth_state.h"

class Faction
{
public:
    std::uint8_t number{};
    std::uint8_t R{};
    std::uint8_t G{};
    std::uint8_t B{};

    Faction(int num, rng_t& rng)
        : number(static_cast<std::uint8_t>(num))
        , R(static_cast<std::uint8_t>(random_u32_inclusive(rng, 255)))
        , G(static_cast<std::uint8_t>(random_u32_inclusive(rng, 255)))
        , B(static_cast<std::uint8_t>(random_u32_inclusive(rng, 255)))
    {} 
};

namespace {
constexpr double kPerfLogIntervalMs = 2000.0;
[[maybe_unused]] constexpr std::uint32_t kTargetFrameMs = 16;
[[maybe_unused]] constexpr std::uint32_t kIdleDelayMs = 10;

#ifndef __EMSCRIPTEN__
constexpr const char* kWindowPrefsFile = "persistent.dat";

struct WindowPrefs {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int display_index = 0;
};

[[nodiscard]] bool load_window_prefs(WindowPrefs& prefs, const GameContext& ctx)
{
    std::ifstream in(resolve_path(ctx, kWindowPrefsFile), std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(&prefs), static_cast<std::streamsize>(sizeof(WindowPrefs)));
    return in.gcount() == static_cast<std::streamsize>(sizeof(WindowPrefs));
}

void save_window_prefs(const WindowPrefs& prefs, const GameContext& ctx)
{
    std::ofstream out(resolve_path(ctx, kWindowPrefsFile), std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(reinterpret_cast<const char*>(&prefs), static_cast<std::streamsize>(sizeof(WindowPrefs)));
}

[[nodiscard]] bool is_window_size_valid(int width, int height, const SDL_Rect& bounds)
{
    return width > 0 && height > 0 && width <= bounds.w && height <= bounds.h;
}

[[nodiscard]] bool is_window_pos_valid(int x, int y, int width, int height, const SDL_Rect& bounds)
{
    const int margin = 40;
    return x >= bounds.x - width + margin && y >= bounds.y - height + margin &&
           x <= bounds.x + bounds.w - margin && y <= bounds.y + bounds.h - margin;
}
#endif

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

    std::println("[perf] fps={:.1f} frame_ms={:.2f} update_ms={:.2f} post_ms={:.2f} mode={} entities={} npcs={} suspects={}",
                fps, avg_frame_ms, avg_update_ms, avg_post_ms, static_cast<int>(mode), entity_count, npc_count, suspects);
}

[[nodiscard]] bool is_redraw_event(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_MULTIGESTURE:
        case SDL_WINDOWEVENT:
            return true;
        default:
            return false;
    }
}

void update_window_metrics(GameContext& ctx, TextureManager& textures)
{
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(ctx.window, &window_w, &window_h);

    int output_w = 0;
    int output_h = 0;
    SDL_GetRendererOutputSize(ctx.renderer, &output_w, &output_h);
    if (output_w <= 0 || output_h <= 0) {
        output_w = window_w;
        output_h = window_h;
    }

    ctx.window_width = output_w;
    ctx.window_height = output_h;
    ctx.input_scale_x = window_w > 0 ? static_cast<float>(output_w) / static_cast<float>(window_w) : 1.0f;
    ctx.input_scale_y = window_h > 0 ? static_cast<float>(output_h) / static_cast<float>(window_h) : 1.0f;

    SDL_RenderSetLogicalSize(ctx.renderer, ctx.window_width, ctx.window_height);
    ctx.screen_center_x = ctx.window_width / 2;
    ctx.screen_center_y = ctx.window_height / 2;
    textures.tile_background().w = ctx.window_width;
    textures.tile_background().h = ctx.window_height;
    ctx.window_dirty = true;
    ctx.redraw_requested = true;
}

void sync_window_metrics(GameContext& ctx, TextureManager& textures)
{
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(ctx.window, &window_w, &window_h);

    int output_w = 0;
    int output_h = 0;
    SDL_GetRendererOutputSize(ctx.renderer, &output_w, &output_h);
    if (output_w <= 0 || output_h <= 0) {
        output_w = window_w;
        output_h = window_h;
    }

    const float scale_x = window_w > 0 ? static_cast<float>(output_w) / static_cast<float>(window_w) : 1.0f;
    const float scale_y = window_h > 0 ? static_cast<float>(output_h) / static_cast<float>(window_h) : 1.0f;
    if (output_w != ctx.window_width || output_h != ctx.window_height ||
        scale_x != ctx.input_scale_x || scale_y != ctx.input_scale_y) {
        ctx.window_width = output_w;
        ctx.window_height = output_h;
        ctx.input_scale_x = scale_x;
        ctx.input_scale_y = scale_y;
        SDL_RenderSetLogicalSize(ctx.renderer, ctx.window_width, ctx.window_height);
        ctx.screen_center_x = ctx.window_width / 2;
        ctx.screen_center_y = ctx.window_height / 2;
        textures.tile_background().w = ctx.window_width;
        textures.tile_background().h = ctx.window_height;
        ctx.window_dirty = true;
        ctx.redraw_requested = true;
    }
}

struct LoopState {
    GameContext& ctx;
    TextureManager& textures;
    EntityManager& entities;
    WorldManager& world_manager;
};

void update_cursor_position(GameContext& ctx)
{
    int raw_x = 0;
    int raw_y = 0;
    SDL_GetMouseState(&raw_x, &raw_y);
    ctx.curs_x = static_cast<int>(static_cast<float>(raw_x) * ctx.input_scale_x);
    ctx.curs_y = static_cast<int>(static_cast<float>(raw_y) * ctx.input_scale_y);
}

[[nodiscard]] GameState* state_at_depth(const GameContext& ctx, std::size_t depth)
{
    if (ctx.state_stack.size() > depth) {
        return ctx.state_stack[ctx.state_stack.size() - 1 - depth].get();
    }
    return nullptr;
}

void handle_game_event(LoopState& state, SDL_Event& event)
{
    GameState* current = current_state(state.ctx);
    if (current) {
        current->handle_event(event, state.ctx, state.textures, state.entities);
    }
}

bool process_events(LoopState& state)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state.ctx.quit = true;
            return true;
        }
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             event.window.event == SDL_WINDOWEVENT_RESIZED)) {
            update_window_metrics(state.ctx, state.textures);
        }

        handle_game_event(state, event);

        if (is_redraw_event(event)) {
            state.ctx.redraw_requested = true;
        }
    }
    return false;
}

void update_and_render(LoopState& state)
{
    state.ctx.ui_hit_test.begin_frame();
    state.ctx.ui_input_enabled = true;

    GameState* current = current_state(state.ctx);
    if (!current) {
        state.ctx.ui_hit_test.commit_if_dirty();
        return;
    }

    const GameMode mode_before_update = current->mode();

    // For overlay states, render the underlying state first
    if (current->is_overlay() && state.ctx.redraw_requested) {
        const bool was_picked = state.ctx.picked;
        const int pick_x = state.ctx.pick_x;
        const int pick_y = state.ctx.pick_y;
        state.ctx.ui_input_enabled = false;
        
        GameState* underlying = state_at_depth(state.ctx, 1);
        if (underlying) {
            underlying->render(state.ctx, state.textures, state.entities);
        }
        
        state.ctx.ui_input_enabled = true;
        state.ctx.picked = was_picked;
        state.ctx.pick_x = pick_x;
        state.ctx.pick_y = pick_y;
    }

    current->update(state.ctx, state.textures, state.entities);

    // Re-fetch current state in case it changed during update
    current = current_state(state.ctx);
    if (!current) {
        state.ctx.ui_hit_test.commit_if_dirty();
        return;
    }

    // If state changed during update (e.g., Game -> Fight), request redraw
    if (current->mode() != mode_before_update) {
        state.ctx.redraw_requested = true;
    }

    if (state.ctx.redraw_requested) {
        current->render(state.ctx, state.textures, state.entities);
    }

    state.ctx.ui_hit_test.commit_if_dirty();
}

void present_frame(GameContext& ctx)
{
    if (ctx.redraw_requested) {
        SDL_RenderPresent(ctx.renderer);
        ctx.redraw_requested = false;
        ctx.last_present_ticks = SDL_GetTicks();
    }
}

void update_perf_stats_if_ready(PerfStats& perf_stats,
                               std::uint64_t perf_freq,
                               std::uint64_t frame_start,
                               std::uint64_t update_start,
                               std::uint64_t update_end,
                               std::uint64_t frame_end,
                               GameContext& ctx,
                               EntityManager& entities,
                               WorldManager& world_manager)
{
    const double frame_ms = (static_cast<double>(frame_end - frame_start) * 1000.0) / static_cast<double>(perf_freq);
    const double update_ms = (static_cast<double>(update_end - update_start) * 1000.0) / static_cast<double>(perf_freq);
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
    if (elapsed_ms >= kPerfLogIntervalMs) {
        const int entity_count = count_active_entities(entities);
        const int npc_count = world_manager.npcs.active_count();
        log_perf_stats(perf_stats, current_game_mode(ctx), entity_count, npc_count);
        perf_stats = {};
        perf_stats.last_log_ticks = now_ticks;
    }
}
} // namespace

#ifdef __EMSCRIPTEN__
struct EmscriptenState {
    GameContext* ctx;
    TextureManager* textures;
    EntityManager* entities;
    WorldManager* world_manager;
};

static EmscriptenState* g_state = nullptr;

void emscripten_main_loop() {
    LoopState state{
        *g_state->ctx,
        *g_state->textures,
        *g_state->entities,
        *g_state->world_manager
    };

    sync_window_metrics(state.ctx, state.textures);

    state.ctx.frame = static_cast<int>(SDL_GetTicks());
    update_cursor_position(state.ctx);

    const std::uint64_t perf_freq = SDL_GetPerformanceFrequency();
    static PerfStats perf_stats{};
    static std::uint64_t last_frame_end = 0;

    if (process_events(state)) {
        emscripten_cancel_main_loop();
        return;
    }

    if (state.ctx.screenshot) {
        state.ctx.redraw_requested = true;
    }

    const std::uint64_t update_start = SDL_GetPerformanceCounter();
    const std::uint64_t perf_frame_start = last_frame_end != 0 ? last_frame_end : update_start;

    update_and_render(state);

    const std::uint64_t update_end = SDL_GetPerformanceCounter();

    present_frame(state.ctx);

    if (current_game_mode(state.ctx) != GameMode::Game && state.ctx.last_present_ticks != 0) {
        state.entities.rebuild_pos_map(state.ctx.pos_map, true);
    }

    if (state.ctx.last_present_ticks != 0) {
        const std::uint64_t frame_end = SDL_GetPerformanceCounter();
        update_perf_stats_if_ready(perf_stats, perf_freq, perf_frame_start, update_start, update_end, frame_end,
                                   state.ctx, state.entities, state.world_manager);
        last_frame_end = frame_end;
    }

    if (state.ctx.screenshot && state.ctx.last_present_ticks != 0) {
        state.ctx.screenshot = false;
    }

    if (state.ctx.quit) {
        emscripten_cancel_main_loop();
    }
}
#endif

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
    const int default_display = 0;
    SDL_GetDesktopDisplayMode(default_display, &current);

    GameContext ctx;
#ifndef __EMSCRIPTEN__
    if (char* base_path = SDL_GetBasePath()) {
        ctx.base_path = base_path;
        SDL_free(base_path);
    }

    WindowPrefs window_prefs{};
    bool has_prefs = load_window_prefs(window_prefs, ctx);
    if (has_prefs) {
        const int display_count = SDL_GetNumVideoDisplays();
        if (window_prefs.display_index < 0 || window_prefs.display_index >= display_count) {
            has_prefs = false;
        }
    }
#endif
    ctx.window_width = static_cast<int>(current.w * 0.75f);
    ctx.window_height = static_cast<int>(current.h * 0.75f);
#ifndef __EMSCRIPTEN__
    if (has_prefs) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(window_prefs.display_index, &bounds) == 0 &&
            is_window_size_valid(window_prefs.width, window_prefs.height, bounds)) {
            ctx.window_width = window_prefs.width;
            ctx.window_height = window_prefs.height;
        }
    }
#endif
    ctx.screen_center_x = ctx.window_width / 2;
    ctx.screen_center_y = ctx.window_height / 2;

    SDL_CreateWindowAndRenderer(ctx.window_width, ctx.window_height, SDL_WINDOW_RESIZABLE, &ctx.window, &ctx.renderer);
    SDL_SetWindowFullscreen(ctx.window, 0);
#ifndef __EMSCRIPTEN__
    if (has_prefs) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(window_prefs.display_index, &bounds) == 0) {
            SDL_SetWindowSize(ctx.window, ctx.window_width, ctx.window_height);
            if (is_window_pos_valid(window_prefs.x, window_prefs.y,
                                    ctx.window_width, ctx.window_height, bounds)) {
                SDL_SetWindowPosition(ctx.window, window_prefs.x, window_prefs.y);
            } else {
                SDL_SetWindowPosition(ctx.window,
                                      SDL_WINDOWPOS_CENTERED_DISPLAY(window_prefs.display_index),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY(window_prefs.display_index));
            }
        }
    }
#endif

    const std::string font_path = resolve_path(ctx, "assets/fonts/Roboto-Black.ttf");
    const std::string icon_font_path = resolve_path(ctx, "assets/fonts/rpgawesome-webfont.ttf");
    ctx.font.reset(TTF_OpenFont(font_path.c_str(), 20));
    if (!ctx.font) {
        std::println(stderr, "Failed to load font: {}", TTF_GetError());
    }
    ctx.text_renderer.initialize(ctx.renderer, font_path, 20, icon_font_path);
    (void)ctx.text_renderer.preload(20);
    if (!ctx.text_renderer.preload_icon(20)) {
        std::println(stderr, "Failed to load icon font: {}", TTF_GetError());
    }

    ctx.init_world();
    ctx.last_frame_time = SDL_GetTicks();

    TextureManager textures;
    {
        int window_w = 0;
        int window_h = 0;
        SDL_GetWindowSize(ctx.window, &window_w, &window_h);
        int output_w = 0;
        int output_h = 0;
        SDL_GetRendererOutputSize(ctx.renderer, &output_w, &output_h);
        if (output_w <= 0 || output_h <= 0) {
            output_w = window_w;
            output_h = window_h;
        }
        ctx.window_width = output_w;
        ctx.window_height = output_h;
        ctx.input_scale_x = window_w > 0 ? static_cast<float>(output_w) / static_cast<float>(window_w) : 1.0f;
        ctx.input_scale_y = window_h > 0 ? static_cast<float>(output_h) / static_cast<float>(window_h) : 1.0f;
        SDL_RenderSetLogicalSize(ctx.renderer, ctx.window_width, ctx.window_height);
        ctx.screen_center_x = ctx.window_width / 2;
        ctx.screen_center_y = ctx.window_height / 2;
    }
    textures.init(ctx.renderer, ctx.window_width, ctx.window_height, ctx);

    EntityManager entities;

    WorldManager world_manager;
    
    ctx.world_manager = &world_manager;

    // Initialize state stack with Menu state
    push_state(ctx, std::make_unique<MenuState>());

#ifdef __EMSCRIPTEN__
    EmscriptenState state{
        &ctx, &textures, &entities, &world_manager
    };
    g_state = &state;
    
    emscripten_set_main_loop(emscripten_main_loop, 60, 1);
#else
    const std::uint64_t perf_freq = SDL_GetPerformanceFrequency();
    PerfStats perf_stats{};
    LoopState state{ctx, textures, entities, world_manager};

    while (!ctx.quit) 
    {
        ctx.frame = static_cast<int>(SDL_GetTicks());
        update_cursor_position(ctx);

        sync_window_metrics(ctx, textures);

        const std::uint64_t loop_start = SDL_GetPerformanceCounter();

        if (process_events(state)) {
            break;
        }

        if (ctx.screenshot) {
            ctx.redraw_requested = true;
        }

        const std::uint64_t frame_start = SDL_GetPerformanceCounter();
        update_and_render(state);

        const std::uint64_t update_end = SDL_GetPerformanceCounter();

        present_frame(ctx);

        if (current_game_mode(ctx) != GameMode::Game && ctx.last_present_ticks != 0) {
            entities.rebuild_pos_map(ctx.pos_map, true);
        }

        if (ctx.screenshot)
        {
            SDLSurfacePtr shot{SDL_CreateRGBSurface(0, ctx.window_width, ctx.window_height, 32, 0, 0, 0, 0)};
            if (shot) {
                SDL_RenderReadPixels(ctx.renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
                SDL_SaveBMP(shot.get(), resolve_path(ctx, "save.png").c_str());
            }
            ctx.screenshot = false;
        }

        const std::uint64_t loop_end = SDL_GetPerformanceCounter();
        const double loop_ms = (static_cast<double>(loop_end - loop_start) * 1000.0) / static_cast<double>(perf_freq);
        if (ctx.last_present_ticks != 0) {
            if (loop_ms < static_cast<double>(kTargetFrameMs)) {
                SDL_Delay(static_cast<std::uint32_t>(kTargetFrameMs - loop_ms));
            }
        } else {
            SDL_Delay(kIdleDelayMs);
        }

        if (ctx.last_present_ticks != 0) {
            const std::uint64_t frame_end = SDL_GetPerformanceCounter();
            update_perf_stats_if_ready(perf_stats, perf_freq, frame_start, frame_start, update_end, frame_end,
                                       ctx, entities, world_manager);
        }
    }
#ifndef __EMSCRIPTEN__
    WindowPrefs save_prefs{};
    if (ctx.window) {
        SDL_GetWindowPosition(ctx.window, &save_prefs.x, &save_prefs.y);
        SDL_GetWindowSize(ctx.window, &save_prefs.width, &save_prefs.height);
        save_prefs.display_index = SDL_GetWindowDisplayIndex(ctx.window);
        save_window_prefs(save_prefs, ctx);
    }
#endif
#endif

    return EXIT_SUCCESS;
}
