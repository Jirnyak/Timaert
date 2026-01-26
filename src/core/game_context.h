#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <vector>
#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <memory>
#include <array>
#include <cstdint>
#include <numbers>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/tile_map.h"
#include "core/types.h"
#include "rendering/text_renderer.h"
#include "rendering/sound_manager.h"
#include <entt/entt.hpp>

class WorldManager;
class GameState;

namespace ecs { class World; }

inline constexpr int WORLD_WIDTH = 1024;
inline constexpr int MAX_OBJECTS = 128;
inline constexpr int TILE_SIZE = 16;
inline constexpr double PI = std::numbers::pi;
inline constexpr std::uint64_t TICKS_PER_DAY = 24000;



struct UIHitTest
{
    std::vector<SDL_Rect> active;
    std::vector<SDL_Rect> pending;
    bool pending_dirty = false;

    void begin_frame() {
        pending.clear();
        pending_dirty = true;
    }

    void add(const SDL_Rect& rect) {
        pending.push_back(rect);
        pending_dirty = true;
    }

    void commit_if_dirty() {
        if (!pending_dirty) return;
        active.swap(pending);
        pending.clear();
        pending_dirty = false;
    }

    [[nodiscard]] bool contains(int x, int y) const noexcept
    {
        for (const auto& rect : active)
        {
            if (x >= rect.x && x < rect.x + rect.w &&
                y >= rect.y && y < rect.y + rect.h)
            {
                return true;
            }
        }
        return false;
    }
};


using rng_t = std::mt19937;

[[nodiscard]] inline std::uint32_t random_u32_inclusive(rng_t& rng, std::uint32_t range) noexcept
{
    range += 1;
    std::uint32_t x = rng();
    std::uint64_t m = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(range);
    std::uint32_t l = static_cast<std::uint32_t>(m);
    if (l < range) {
        std::uint32_t t = -range;
        if (t >= range) {
            t -= range;
            if (t >= range) 
                t %= range;
        }
        while (l < t) {
            x = rng();
            m = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(range);
            l = static_cast<std::uint32_t>(m);
        }
    }
    return m >> 32;
}

[[nodiscard]] inline SDL_Color get_ambient_color(std::uint64_t total_ticks) noexcept
{
    // Текущий тик внутри цикла суток (0 - 23999)
    const std::uint64_t day_tick = total_ticks % TICKS_PER_DAY;
    const float progress = static_cast<float>(day_tick) / static_cast<float>(TICKS_PER_DAY);

    // Ночь: Черный оверлей (альфа ~200-215 из 255)
    // День: Прозрачный (альфа 0)
    
    // Ночь (до 5 утра и после 9 вечера)
    if (progress < 0.2f || progress > 0.9f) { 
        return { 0, 0, 0, 210 }; // Просто темнота
    }
    // Рассвет (5:00 - 8:30)
    else if (progress >= 0.2f && progress < 0.35f) { 
        float f = (progress - 0.2f) / 0.15f;
        // Плавное посветление (уменьшаем альфу черного)
        return { 0, 0, 0, static_cast<std::uint8_t>(210 * (1.0f - f)) };
    }
    // День (8:30 - 18:00)
    else if (progress >= 0.35f && progress < 0.75f) { 
        return { 0, 0, 0, 0 }; // Светло
    }
    // Закат (18:00 - 21:30)
    else { 
        float f = (progress - 0.75f) / 0.15f;
        // Плавное затемнение (увеличиваем альфу черного)
        return { 0, 0, 0, static_cast<std::uint8_t>(210 * f) };
    }
}

struct MapPixel
{
    std::uint8_t R{};
    std::uint8_t G{};
    std::uint8_t B{};
};

[[nodiscard]] constexpr int wrap_coord(int x, int size = WORLD_WIDTH) noexcept
{
    if (x < 0)
        x = (size + x) % size;
    else if (x >= size)
        x = x % size;
    return x;
}

[[nodiscard]] inline TilePosition neighbor_from_pos(TilePosition p, Direction direction) noexcept
{
    const int x = static_cast<int>(p.x);
    const int y = static_cast<int>(p.y);
    switch (direction)
    {
        case Direction::Up:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x)), static_cast<std::uint16_t>(wrap_coord(y - 1))};
        case Direction::Left:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x - 1)), static_cast<std::uint16_t>(wrap_coord(y))};
        case Direction::Down:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x)), static_cast<std::uint16_t>(wrap_coord(y + 1))};
        case Direction::Right:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x + 1)), static_cast<std::uint16_t>(wrap_coord(y))};
        default:
            return INVALID_POS;
    }
}

[[nodiscard]] inline double toroidal_distance(TilePosition p1, TilePosition p2) noexcept
{
    int dx = std::abs(static_cast<int>(p1.x) - static_cast<int>(p2.x));
    if (dx > WORLD_WIDTH / 2)
        dx = WORLD_WIDTH - dx;
    int dy = std::abs(static_cast<int>(p1.y) - static_cast<int>(p2.y));
    if (dy > WORLD_WIDTH / 2)
        dy = WORLD_WIDTH - dy;
    return std::hypot(static_cast<double>(dx), static_cast<double>(dy));
}

struct SDLDeleter
{
    void operator()(SDL_Window* w) const noexcept { if (w) SDL_DestroyWindow(w); }
    void operator()(SDL_Renderer* r) const noexcept { if (r) SDL_DestroyRenderer(r); }
    void operator()(SDL_Texture* t) const noexcept { if (t) SDL_DestroyTexture(t); }
    void operator()(SDL_Surface* s) const noexcept { if (s) SDL_FreeSurface(s); }
};

struct TTFDeleter
{
    void operator()(TTF_Font* f) const noexcept { if (f) TTF_CloseFont(f); }
};

using SDLWindowPtr = std::unique_ptr<SDL_Window, SDLDeleter>;
using SDLRendererPtr = std::unique_ptr<SDL_Renderer, SDLDeleter>;
using SDLTexturePtr = std::unique_ptr<SDL_Texture, SDLDeleter>;
using SDLSurfacePtr = std::unique_ptr<SDL_Surface, SDLDeleter>;
using TTFFontPtr = std::unique_ptr<TTF_Font, TTFDeleter>;

struct SDLSubsystem
{
    bool sdl_initialized = false;
    bool ttf_initialized = false;
    bool img_initialized = false;
    
    SDLSubsystem() = default;
    
    ~SDLSubsystem()
    {
        if (ttf_initialized) TTF_Quit();
        if (img_initialized) IMG_Quit();
        if (sdl_initialized) SDL_Quit();
    }
    
    SDLSubsystem(const SDLSubsystem&) = delete;
    SDLSubsystem& operator=(const SDLSubsystem&) = delete;
    SDLSubsystem(SDLSubsystem&&) = delete;
    SDLSubsystem& operator=(SDLSubsystem&&) = delete;
    
    [[nodiscard]] bool init_sdl()
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
        sdl_initialized = true;
        return true;
    }
    
    [[nodiscard]] bool init_ttf()
    {
        if (TTF_Init() < 0) return false;
        ttf_initialized = true;
        return true;
    }
    
    [[nodiscard]] bool init_img(int flags)
    {
        if (IMG_Init(flags) == 0) return false;
        img_initialized = true;
        return true;
    }
};

inline constexpr std::size_t WORLD_SIZE = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
inline constexpr std::size_t ENTITY_POOL_SIZE = static_cast<std::size_t>(MAX_OBJECTS) * MAX_OBJECTS;

template <typename T>
using WorldMap = TileMap<T, WORLD_WIDTH, WORLD_WIDTH>;

struct GameContext
{
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    TTFFontPtr font{};
    SDLTexturePtr world_image{};
    TextRenderer text_renderer{};
    
    int window_width = 0;
    int window_height = 0;
    int screen_center_x = 0;
    int screen_center_y = 0;
    float input_scale_x = 1.0f;
    float input_scale_y = 1.0f;
    bool window_dirty = false;
    
    bool fullscreen = false;
    bool paused = false;
    bool quit = false;
    bool freecam = true;
    bool screenshot = false;
    std::vector<std::unique_ptr<GameState>> state_stack;
    bool picked = false;
    int game_speed = 1;

    std::int32_t active_event_id = -1; // ID текущего текстового события. -1 если событий нет.
    entt::entity battle_target_entity = entt::null;  // ECS entity for battle target
    
    int curs_x = 0;
    int curs_y = 0;
    int pick_x = 0;
    int pick_y = 0;
    std::array<char, 64> input{};
    UIHitTest ui_hit_test{};
    bool ui_input_enabled = true;
    
    int cam_x = WORLD_WIDTH / 2;
    int cam_y = WORLD_WIDTH / 2;
    TilePosition pos_cam{0, 0};
    
    bool map_dragging = false;
    int drag_last_x = 0;
    int drag_last_y = 0;
    float map_offset_x = 0.0f;
    float map_offset_y = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    static constexpr float friction = 0.92f;
    static constexpr float velocity_threshold = 0.5f;
    std::uint32_t last_frame_time = 0;
    
    float zoom = 1.0f;
    float target_zoom = 1.0f;
    static constexpr float min_zoom = 0.25f;
    static constexpr float max_zoom = 4.0f;
    static constexpr float zoom_speed = 0.15f;
    
    int frame = 0;
    std::uint64_t hour = 0;
    std::uint32_t seed = 0;
    bool redraw_requested = true;
    std::uint32_t last_present_ticks = 0;
    
    // Map generation settings
    std::string seed_input;
    int num_continents = 5;
    int water_amount = 5;
    
    WorldMap<TerrainType> relief;
    WorldMap<std::uint8_t> flora;
    WorldMap<std::uint8_t> clouds;
    WorldMap<std::uint8_t> zone_level;
    WorldMap<std::uint8_t> owner;
    WorldMap<std::uint8_t> resource_iron;
    WorldMap<std::uint8_t> resource_clay;
    WorldMap<std::uint8_t> resource_fertility;
    
    WorldMap<MapPixel> world_map;
    WorldMap<float> field;              // Elevation heightmap
    WorldMap<float> heightmap;          // Stored heightmap for reference
    WorldMap<float> continent_map;      // Continent/island map (0=ocean, 1=land)
    WorldMap<float> temperature;
    WorldMap<float> humidity;
    WorldMap<float> temp;
    
    WorldMap<std::uint16_t> pos_map;
    std::string base_path;

    WorldMap<TilePosition> path_prev;
    std::vector<TilePosition> path_queue;
    
    rng_t rng;
    WorldManager* world_manager = nullptr;
    SoundManager sound_manager{};
    
    // ECS World (Phase 4 migration)
    std::unique_ptr<ecs::World> ecs_world;
    
    GameContext();
    
    ~GameContext();
    
    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;
    GameContext(GameContext&&) = delete;
    GameContext& operator=(GameContext&&) = delete;
    
    void init_world()
    {
        relief.fill(TerrainType::Nothing);

        flora.fill(0);

        clouds.fill(0);

        zone_level.fill(0);
        
        owner.fill(0);
        resource_iron.fill(0);
        resource_clay.fill(0);
        resource_fertility.fill(0);

        pos_map.fill(0);
        path_prev.fill(INVALID_POS);
        path_queue.reserve(WORLD_SIZE);
    }
    
    [[nodiscard]] TilePosition get_neighbor(TilePosition pos, Direction direction) const noexcept
    {
        return neighbor_from_pos(pos, direction);
    }
    
    // ECS singleton accessors (implementations in game_context.cpp)
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    void set_ticks(std::uint64_t t) noexcept;
    [[nodiscard]] int speed() const noexcept;
    void set_speed(int s) noexcept;
    [[nodiscard]] bool is_paused() const noexcept;
    void set_paused(bool p) noexcept;
};

[[nodiscard]] inline std::string resolve_path(const GameContext& ctx, std::string_view relative)
{
#ifndef __EMSCRIPTEN__
    if (!ctx.base_path.empty()) {
        if (ctx.base_path.back() == '/' || ctx.base_path.back() == '\\') {
            return ctx.base_path + std::string(relative);
        }
        return ctx.base_path + "/" + std::string(relative);
    }
#else
    (void)ctx;
    if (relative == "save.dat" || relative == "save.png") {
        return "/persist/" + std::string(relative);
    }
#endif
    return std::string(relative);
}

#ifdef __EMSCRIPTEN__
inline void em_init_persistent_fs()
{
    EM_ASM(
        FS.mkdir('/persist');
        FS.mount(IDBFS, {}, '/persist');
        FS.syncfs(true, function(err) {
            if (err) {
                console.error('IDBFS load error:', err);
            } else {
                console.log('IDBFS: Loaded persistent storage');
            }
        });
    );
}

inline void em_sync_persistent_fs()
{
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) {
                console.error('IDBFS sync error:', err);
            } else {
                console.log('IDBFS: Saved to persistent storage');
            }
        });
    );
}
#endif

[[nodiscard]] inline int to_render_x(const GameContext& ctx, int x) noexcept
{
    return static_cast<int>(static_cast<float>(x) * ctx.input_scale_x);
}

[[nodiscard]] inline int to_render_y(const GameContext& ctx, int y) noexcept
{
    return static_cast<int>(static_cast<float>(y) * ctx.input_scale_y);
}

inline void toggle_fullscreen(GameContext& ctx)
{
    ctx.fullscreen = !ctx.fullscreen;
    SDL_SetWindowFullscreen(ctx.window, ctx.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

inline bool handle_fullscreen_key(GameContext& ctx, SDL_Keycode key)
{
    if (key != SDLK_0) return false;
    toggle_fullscreen(ctx);
    return true;
}

inline void update_map_inertia(GameContext& ctx, float delta_time)
{
    if (ctx.map_dragging) return;

    ctx.map_offset_x += ctx.velocity_x * delta_time;
    ctx.map_offset_y += ctx.velocity_y * delta_time;

    ctx.velocity_x *= std::pow(ctx.friction, delta_time);
    ctx.velocity_y *= std::pow(ctx.friction, delta_time);

    if (std::abs(ctx.velocity_x) < ctx.velocity_threshold) ctx.velocity_x = 0.0f;
    if (std::abs(ctx.velocity_y) < ctx.velocity_threshold) ctx.velocity_y = 0.0f;
}

inline void begin_map_drag(GameContext& ctx)
{
    ctx.map_dragging = true;
    ctx.velocity_x = 0.0f;
    ctx.velocity_y = 0.0f;
}

inline void apply_map_drag(GameContext& ctx, float dx, float dy, float scale = 1.0f)
{
    if (!ctx.map_dragging) return;

    const float scaled_dx = dx * scale;
    const float scaled_dy = dy * scale;

    ctx.map_offset_x += scaled_dx;
    ctx.map_offset_y += scaled_dy;

    ctx.velocity_x = ctx.velocity_x * 0.5f + scaled_dx * 0.5f;
    ctx.velocity_y = ctx.velocity_y * 0.5f + scaled_dy * 0.5f;
}

inline void end_map_drag(GameContext& ctx)
{
    ctx.map_dragging = false;
}

[[nodiscard]] GameState* current_state(const GameContext& ctx) noexcept;

[[nodiscard]] GameMode current_game_mode(const GameContext& ctx) noexcept;

using StateCreatorFn = std::unique_ptr<GameState>(*)();

struct StateRegistry {
    static constexpr std::size_t kMaxModes = 16;
    StateCreatorFn creators[kMaxModes] = {};
    
    static StateRegistry& instance() {
        static StateRegistry reg;
        return reg;
    }
    
    void register_state(GameMode mode, StateCreatorFn fn) {
        creators[static_cast<std::size_t>(mode)] = fn;
    }

    [[nodiscard]] std::unique_ptr<GameState> create(GameMode mode) const;
};

template<typename T>
struct StateRegistrar {
    StateRegistrar(GameMode mode) {
        StateRegistry::instance().register_state(mode, []() -> std::unique_ptr<GameState> {
            return std::make_unique<T>();
        });
    }
};

void push_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick = true);

void replace_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick = true);

bool pop_state(GameContext& ctx, bool reset_pick = true);

void clear_states(GameContext& ctx, bool reset_pick = true);

inline void trigger_screenshot(GameContext& ctx)
{
    ctx.screenshot = true;
}

inline void set_pick(GameContext& ctx, int x, int y)
{
    ctx.pick_x = x;
    ctx.pick_y = y;
    ctx.picked = true;
}


[[nodiscard]] inline float calc_frame_delta_time(GameContext& ctx,
                                                 float frame_ms = 16.67f,
                                                 float max_delta = 3.0f)
{
    const std::uint32_t current_time = SDL_GetTicks();
    float delta_time = static_cast<float>(current_time - ctx.last_frame_time) / frame_ms;
    ctx.last_frame_time = current_time;
    if (delta_time > max_delta) delta_time = max_delta;
    return delta_time;
}

inline void reset_map_view(GameContext& ctx)
{
    ctx.map_offset_x = 0.0f;
    ctx.map_offset_y = 0.0f;
    ctx.velocity_x = 0.0f;
    ctx.velocity_y = 0.0f;
}

[[nodiscard]] inline SDL_Texture* update_map_texture(SDL_Renderer* renderer, SDL_Texture* texture, const MapPixel* pixels, int size)
{
    if (!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, size, size);
        if (!texture) return nullptr;
    }

    void* texPixels = nullptr;
    int pitch = 0;

    if (SDL_LockTexture(texture, nullptr, &texPixels, &pitch) != 0)
        return texture;

    SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    if (!fmt) {
        SDL_UnlockTexture(texture);
        return texture;
    }

    for (int y = 0; y < size; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
        const MapPixel* src = pixels + y * size;
        for (int x = 0; x < size; ++x) {
            row[x] = SDL_MapRGB(fmt, src[x].R, src[x].G, src[x].B);
        }
    }

    SDL_FreeFormat(fmt);
    SDL_UnlockTexture(texture);
    return texture;
}

inline void build_terrain_map_range(GameContext& ctx, std::size_t start, std::size_t count, float water_threshold = 0.35f)
{
    if (start >= WORLD_SIZE || count == 0) return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    for (std::size_t i = start; i < end; ++i)
    {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH), static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        float h = ctx.field[pos];
        const float t = ctx.temperature[pos];
        const float w = ctx.humidity[pos];
        const float cont = ctx.continent_map[pos];  // Use continent map
        
        // Store original heightmap for reference
        ctx.heightmap[pos] = h;
        
        // Adjust heightmap based on continent map:
        // Ocean circles strongly push height toward water
        // Continents push height toward land
        // This makes ocean basins visible even with noisy terrain
        if (cont < 0.15f) {
            // Strong ocean region - most becomes water
            h = h * 0.35f + 0.1f;  // Heavy water bias
        } else if (cont < 0.3f) {
            // Weak ocean region - water bias
            h = h * 0.5f + 0.15f;  // Moderate water bias
        } else if (cont > 0.6f) {
            // Strong continent region - land bias
            h = std::min(1.0f, h * 1.15f + 0.08f);  // Strong land formation
        } else if (cont > 0.45f) {
            // Weak continent region - mild land bias
            h = std::min(1.0f, h * 1.05f + 0.03f);  // Mild land formation
        }
        // else: transition zone (0.3-0.45), use more natural height

        if (h < water_threshold) { // Water based on threshold
            ctx.relief[pos] = TerrainType::Water;
            ctx.world_map[pos] = {25, 75, 155};
        }
        else if (h > 0.85f) { // Горы (slightly lower threshold for more mountains)
            if (t < 0.35f) {
                ctx.relief[pos] = TerrainType::Snow;
                ctx.world_map[pos] = {245, 245, 255};
            } else {
                ctx.relief[pos] = TerrainType::Mount;
                ctx.world_map[pos] = {105, 105, 105};
            }
        }
        else { // Суша
            // Холод
            if (t < 0.27f) { 
                if (w < 0.4f) {
                    ctx.relief[pos] = TerrainType::Tundra;
                    ctx.world_map[pos] = {160, 180, 180};
                } else {
                    ctx.relief[pos] = TerrainType::Snow;
                    ctx.world_map[pos] = {220, 230, 255};
                }
            }
            // Жара
            else if (t > 0.66f) { 
                // Пустыня
                if (w < 0.5f) {
                    ctx.relief[pos] = TerrainType::Sand;
                    ctx.world_map[pos] = {230, 210, 150};
                } else if (w > 0.75f) {
                    ctx.relief[pos] = TerrainType::Jungle;
                    ctx.world_map[pos] = {34, 139, 34}; 
                } else {
                    ctx.relief[pos] = TerrainType::Grass; // Саванна
                    ctx.world_map[pos] = {160, 200, 100};
                }
            }
            // Умеренный климат
            else { 
                // Болота
                if (w > 0.7f) {
                    ctx.relief[pos] = TerrainType::Swamp;
                    ctx.world_map[pos] = {85, 107, 47};
                } else if (w < 0.25f) { // Грязь/Пустошь
                    ctx.relief[pos] = TerrainType::Dirt;
                    ctx.world_map[pos] = {140, 120, 90};
                } else {
                    ctx.relief[pos] = TerrainType::Grass;
                    ctx.world_map[pos] = {80, 160, 60};
                }
            }
        }
    }
}
inline void build_terrain_map(GameContext& ctx)
{
    build_terrain_map_range(ctx, 0, WORLD_SIZE);
}

inline void seed_forests(GameContext& ctx, std::size_t start, std::size_t count)
{
    if (start >= WORLD_SIZE || count == 0) return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    for (std::size_t i = start; i < end; ++i)
    {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH), static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        // Seed forests on suitable terrain - VERY CONSERVATIVE
        if (ctx.relief[pos] == TerrainType::Grass)
        {
            // Only seed forests with low probability on grass
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 30)  // 3% chance for grass
            {
                ctx.flora[pos] = 120 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 30));
            }
        }
        else if (ctx.relief[pos] == TerrainType::Dirt)
        {
            // Very rare on dirt
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 5)  // 0.5% chance for dirt
            {
                ctx.flora[pos] = 100 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 20));
            }
        }
        else if (ctx.relief[pos] == TerrainType::Jungle)
        {
            // High chance in jungles
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 200)  // 20% chance
            {
                ctx.flora[pos] = 150 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 35));
            }
        }
    }
}

inline void spread_forests_step(GameContext& ctx, std::size_t start, std::size_t count)
{
    if (start >= WORLD_SIZE || count == 0) return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);

    for (std::size_t i = start; i < end; ++i) {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH), static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        if (ctx.flora[pos] > 30) {  // Only spread if strong enough
            const std::uint32_t drop = random_u32_inclusive(ctx.rng, 3);
            const TilePosition neighbor = neighbor_from_pos(pos, static_cast<Direction>(drop));
            
            if (is_valid(neighbor)) {
                TerrainType type = ctx.relief[neighbor];
                // Only spread to suitable terrain
                if (type == TerrainType::Grass || type == TerrainType::Jungle || type == TerrainType::Swamp) {
                    // Decay as it spreads
                    int spread_amount = static_cast<int>(ctx.flora[pos]) - static_cast<int>(random_u32_inclusive(ctx.rng, 60));
                    if (spread_amount > 20) {
                        ctx.flora[neighbor] = static_cast<std::uint8_t>(std::max(static_cast<int>(ctx.flora[neighbor]), spread_amount));
                    }
                }
            }
        }
    }
}
