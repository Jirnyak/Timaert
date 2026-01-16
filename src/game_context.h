#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <vector>
#include <unordered_map>
#include <random>
#include <string>
#include <memory>
#include <array>
#include <cstdint>
#include <numbers>
#include <cmath>

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
    Event,
    Fight
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

using rng_t = std::mt19937;

[[nodiscard]] inline std::uint32_t randomer(rng_t& rng, std::uint32_t range) noexcept
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

class Cell 
{
private:
    std::int16_t x_{};  
    std::int16_t y_{};  
    std::int32_t neighbor_up_{-1};   
    std::int32_t neighbor_left_{-1}; 
    std::int32_t neighbor_down_{-1}; 
    std::int32_t neighbor_right_{-1}; 

public:  
    constexpr Cell() = default;

    constexpr Cell(int x, int y) noexcept 
        : x_(static_cast<std::int16_t>(x))
        , y_(static_cast<std::int16_t>(y)) {}

    constexpr void set_up(std::int32_t idx) noexcept { neighbor_up_ = idx; }
    constexpr void set_down(std::int32_t idx) noexcept { neighbor_down_ = idx; }
    constexpr void set_left(std::int32_t idx) noexcept { neighbor_left_ = idx; }
    constexpr void set_right(std::int32_t idx) noexcept { neighbor_right_ = idx; }

    [[nodiscard]] constexpr std::int32_t side(int d) const noexcept
    {
        switch(d) {
            case 0: return neighbor_up_;
            case 1: return neighbor_left_;
            case 2: return neighbor_down_;
            case 3: return neighbor_right_;
            default: return -1;
        }
    }

    [[nodiscard]] constexpr int get_x() const noexcept { return x_; }
    [[nodiscard]] constexpr int get_y() const noexcept { return y_; }
    [[nodiscard]] constexpr int get_n(int razmer = WORLD_WIDTH) const noexcept { return x_ * razmer + y_; }
};

[[nodiscard]] constexpr int tor_cord(int x, int razmer = WORLD_WIDTH) noexcept
{
    if (x < 0)
        x = (razmer + x) % razmer;
    else if (x >= razmer)
        x = x % razmer;
    return x;
}

[[nodiscard]] inline int rasstoyanie(int x1, int y1, int x2, int y2) noexcept
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
    
    // Game state flags
    bool fullscreen = true;
    bool paused = true;
    bool quit = false;
    bool freecam = true;
    bool screenshot = false;
    GameMode game_mod = GameMode::Menu;
    bool picked = false;
    
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
    
    // World data - using unique_ptr for automatic memory management
    std::vector<Cell> world;
    std::unique_ptr<TerrainType[]> relief;
    std::unique_ptr<std::uint8_t[]> owner;
    std::unique_ptr<MapPixel[]> world_map;
    std::unique_ptr<float[]> field;
    std::unique_ptr<float[]> temp;
    
    // Objects - flat_map would be better but using unordered_map for compatibility
    std::unordered_map<int, std::vector<int>> pos_map;
    
    // RNG
    rng_t rng;
    
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
        world.reserve(WORLD_SIZE);
        for (int i = 0; i < WORLD_WIDTH; ++i)
        {
            for (int j = 0; j < WORLD_WIDTH; ++j)
            {       
                world.emplace_back(i, j);
            }
        }
        
        for (auto& cell : world)
        {
            const int x = cell.get_x();
            const int y = cell.get_y();
            cell.set_up(tor_cord(x) * WORLD_WIDTH + tor_cord(y - 1));
            cell.set_down(tor_cord(x) * WORLD_WIDTH + tor_cord(y + 1));
            cell.set_left(tor_cord(x - 1) * WORLD_WIDTH + tor_cord(y));
            cell.set_right(tor_cord(x + 1) * WORLD_WIDTH + tor_cord(y));
        }
        
        relief = std::make_unique<TerrainType[]>(WORLD_SIZE);
        std::fill_n(relief.get(), WORLD_SIZE, TerrainType::Nothing);
        
        owner = std::make_unique<std::uint8_t[]>(WORLD_SIZE);
        std::fill_n(owner.get(), WORLD_SIZE, 0);
        
        world_map = std::make_unique<MapPixel[]>(WORLD_SIZE);
        field = std::make_unique<float[]>(WORLD_SIZE);
        temp = std::make_unique<float[]>(WORLD_SIZE);
    }
    
    [[nodiscard]] Cell& cell_at(int idx) noexcept { return world[static_cast<std::size_t>(idx)]; }
    [[nodiscard]] const Cell& cell_at(int idx) const noexcept { return world[static_cast<std::size_t>(idx)]; }
    
    [[nodiscard]] int get_neighbor(int pos, int direction) const noexcept
    {
        return world[static_cast<std::size_t>(pos)].side(direction);
    }
};

inline void render_text(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, 
                        int x, int y, int width, int height, const SDL_Color& color) 
{
    if (!renderer || !font || text.empty()) return;
    
    SDLSurfacePtr surface{TTF_RenderText_Solid(font, text.c_str(), color)};
    if (!surface) return;
    
    SDLTexturePtr texture{SDL_CreateTextureFromSurface(renderer, surface.get())};
    if (!texture) return;
    
    SDL_Rect rect = { x, y, width, height };
    SDL_RenderCopy(renderer, texture.get(), nullptr, &rect);
}

[[nodiscard]] inline SDL_Texture* img_mapo(SDL_Renderer* renderer, SDL_Texture* texture, const MapPixel* pixels, int N)
{
    if (!texture) {
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            N, N
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

    for (int y = 0; y < N; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
        const MapPixel* src = pixels + y * N;

        for (int x = 0; x < N; ++x) {
            row[x] = SDL_MapRGB(fmt, src[x].R, src[x].G, src[x].B);
        }
    }

    SDL_FreeFormat(fmt);
    SDL_UnlockTexture(texture);
    return texture;
}
