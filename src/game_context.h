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

class WorldManager;

inline constexpr int WORLD_WIDTH = 1024;
inline constexpr int MAX_OBJECTS = 128;
inline constexpr int TILE_SIZE = 16;
inline constexpr double PI = std::numbers::pi;

enum class GameMode : std::uint8_t
{
    Gen,
    Exit,
    Game,
    Menu,
    Stat,
    Map,
    Load,
    Labyrinth,
    Event,
    Fight,
    Pause
};

enum class TerrainType : std::uint8_t
{
    Nothing,
    Sand,
    Grass,
    Dirt,
    Mount,
    Water,
    Count
};

enum class Direction : std::int8_t
{
    Up = 0,
    Left = 1,
    Down = 2,
    Right = 3
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

[[nodiscard]] inline int neighbor_from_pos(int pos, Direction direction) noexcept
{
    const int x = pos / WORLD_WIDTH;
    const int y = pos % WORLD_WIDTH;
    switch (direction)
    {
        case Direction::Up: return wrap_coord(x) * WORLD_WIDTH + wrap_coord(y - 1);
        case Direction::Left: return wrap_coord(x - 1) * WORLD_WIDTH + wrap_coord(y);
        case Direction::Down: return wrap_coord(x) * WORLD_WIDTH + wrap_coord(y + 1);
        case Direction::Right: return wrap_coord(x + 1) * WORLD_WIDTH + wrap_coord(y);
        default: return -1;
    }
}

[[nodiscard]] inline int toroidal_distance(int x1, int y1, int x2, int y2) noexcept
{
    int dx = std::abs(x1 - x2);
    if (dx > WORLD_WIDTH / 2) 
        dx = WORLD_WIDTH - dx;
    int dy = std::abs(y1 - y2);
    if (dy > WORLD_WIDTH / 2) 
        dy = WORLD_WIDTH - dy;
    return static_cast<int>(std::sqrt(dx * dx + dy * dy));
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

struct GameContext
{
    // SDL - raw pointers for compatibility with SDL_CreateWindowAndRenderer
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    TTFFontPtr font{};
    SDLTexturePtr world_image{};
    
    // Display
    int window_width = 0;
    int window_height = 0;
    int screen_center_x = 0;
    int screen_center_y = 0;
    float input_scale_x = 1.0f;
    float input_scale_y = 1.0f;
    bool window_dirty = false;
    
    // Game state flags
    bool fullscreen = false;
    bool paused = false;
    bool quit = false;
    bool freecam = true;
    bool screenshot = false;
    GameMode game_mod = GameMode::Menu;
    bool picked = false;
    int game_speed = 1;  // 1 = normal, 2+ = fast forward multiplier
    
    // Input
    int curs_x = 0;
    int curs_y = 0;
    int pick_x = 0;
    int pick_y = 0;
    std::array<char, 64> input{};
    
    // Camera
    int cam_x = WORLD_WIDTH / 2;
    int cam_y = WORLD_WIDTH / 2;
    int pos_cam = 0;
    
    // Kinetic panning
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
    
    // Zoom
    float zoom = 1.0f;
    float target_zoom = 1.0f;
    static constexpr float min_zoom = 0.25f;
    static constexpr float max_zoom = 4.0f;
    static constexpr float zoom_speed = 0.15f;
    
    // Timing
    int frame = 0;
    std::uint64_t hour = 0;
    std::uint32_t seed = 0;
    bool redraw_requested = true;
    std::uint32_t last_present_ticks = 0;
    
    // World data - using unique_ptr for automatic memory management
    std::unique_ptr<TerrainType[]> relief;
    std::unique_ptr<std::uint8_t[]> owner;
    std::unique_ptr<MapPixel[]> world_map;
    std::unique_ptr<float[]> field;
    std::unique_ptr<float[]> temp;
    
    // Objects - dense occupancy counts per tile
    std::vector<std::uint16_t> pos_map;

    // Paths
    std::string base_path;

    // Pathfinding scratch buffers
    std::vector<int> path_prev;
    std::vector<int> path_queue;
    
    // RNG
    rng_t rng;

    // World manager (for save/load access)
    WorldManager* world_manager = nullptr;
    
    GameContext() 
        : rng(std::random_device{}())
    {
        pos_cam = cam_x * WORLD_WIDTH + cam_y;
    }
    
    ~GameContext()
    {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }
    
    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;
    GameContext(GameContext&&) = default;
    GameContext& operator=(GameContext&&) = default;
    
    void init_world()
    {
        relief = std::make_unique<TerrainType[]>(WORLD_SIZE); //basic terrain tiles
        std::fill_n(relief.get(), WORLD_SIZE, TerrainType::Nothing);

        flora = std::make_unique<std::uint8_t[]>(WORLD_SIZE); //VEGETATION SURFACE e.g forests
        std::fill_n(flora.get(), WORLD_SIZE, 0);

        clouds = std::make_unique<std::uint8_t[]>(WORLD_SIZE); //clouds above world for rain
        std::fill_n(clouds.get(), WORLD_SIZE, 0);

        zone_level = std::make_unique<std::uint8_t[]>(WORLD_SIZE); //danger level of area - lower around townd/higher in wilderness + procedural pregen base lvl
        std::fill_n(zone_level.get(), WORLD_SIZE, 0);
        
        owner = std::make_unique<std::uint8_t[]>(WORLD_SIZE); //Politik map owner state of land
        std::fill_n(owner.get(), WORLD_SIZE, 0);
        
        world_map = std::make_unique<MapPixel[]>(WORLD_SIZE);
        field = std::make_unique<float[]>(WORLD_SIZE);
        temp = std::make_unique<float[]>(WORLD_SIZE);
        pos_map.assign(WORLD_SIZE, 0);
        path_prev.assign(WORLD_SIZE, -1);
        path_queue.reserve(WORLD_SIZE);
    }
    
    [[nodiscard]] int get_neighbor(int pos, Direction direction) const noexcept
    {
        return neighbor_from_pos(pos, direction);
    }
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
#endif
    return std::string(relative);
}

[[nodiscard]] inline int to_render_x(const GameContext& ctx, int x) noexcept
{
    return static_cast<int>(static_cast<float>(x) * ctx.input_scale_x);
}

[[nodiscard]] inline int to_render_y(const GameContext& ctx, int y) noexcept
{
    return static_cast<int>(static_cast<float>(y) * ctx.input_scale_y);
}

[[nodiscard]] inline SDL_Texture* update_map_texture(SDL_Renderer* renderer, SDL_Texture* texture, const MapPixel* pixels, int size)
{
    if (!texture) {
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            size, size
        );
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

inline void build_terrain_map_range(GameContext& ctx, std::size_t start, std::size_t count)
{
    if (start >= WORLD_SIZE || count == 0) return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    for (std::size_t i = start; i < end; ++i)
    {
        if (ctx.field[i] < 0.4f)
        {
            ctx.relief[i] = TerrainType::Water;
            ctx.world_map[i] = {0, 0, 255};
        }
        else if (ctx.field[i] < 0.45f)
        {
            ctx.relief[i] = TerrainType::Sand;
            ctx.world_map[i] = {255, 255, 0};
        }
        else if (ctx.field[i] < 0.8f)
        {
            const int drop = random_u32_inclusive(ctx.rng, 1);
            if (drop == 0)
            {
                ctx.relief[i] = TerrainType::Dirt;
                ctx.world_map[i] = {128, 255, 0};
            }
            else
            {
                ctx.relief[i] = TerrainType::Grass;
                ctx.world_map[i] = {0, 255, 0};
            }
        }
        else
        {
            ctx.relief[i] = TerrainType::Mount;
            ctx.world_map[i] = {128, 128, 128};
        }
    }
}

inline void build_terrain_map(GameContext& ctx)
{
    build_terrain_map_range(ctx, 0, WORLD_SIZE);
}

//FOREST SEEDS GEN
inline void seed_forests(GameContext& ctx, std::size_t start, std::size_t count)
{
    if (start >= WORLD_SIZE || count == 0) return; // Не до конца понимаю идею этого
    const std::size_t end = std::min(start + count, WORLD_SIZE); //и этого (типа обозначить энд для перебора?)
    for (std::size_t i = start; i < end; ++i)
    {
        if (ctx.relief[i] == TerrainType::Grass)
        {
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop == 0)
            {
                forest[i] = 255;
            }
        }
    }
}

//FOREST PREGORW
inline void spread_forests(GameContext& ctx, std::size_t start, std::size_t count)
{
    if (start >= WORLD_SIZE || count == 0) return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    const int steps = 0; //number of steps to grow forest 
    while (steps < 10) 
    {
        if (ctx.forest[i] > 0)
        {
            const int drop = random_u32_inclusive(ctx.rng, 3);
            if (0 == 0)
            {
             // тут я хотел чтото типа if (world[i].side(drop)->type != WATER)
             // forest[world[i].side(drop)->get_n()] = forest[i] - random_u32_inclusive(ctx.rng, 50);
            // if (forest[world[i].side(drop)->get_n()] < 0)
            // (forest[world[i].side(drop)->get_n()] = 0;
            // но не понял как это называется в новых структурах >_<!
            }
        }
    }
}

        

    
