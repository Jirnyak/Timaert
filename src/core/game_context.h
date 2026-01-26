#pragma once

#include <vector>
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

#include "core/gfx_types.h"
#include "core/tile_map.h"
#include "core/types.h"
#include "rendering/sound_manager.h"
#include <entt/entt.hpp>

class WorldManager;
class GameState;

namespace ecs {
class World;
}

inline constexpr int WORLD_WIDTH = 1024;
inline constexpr int MAX_OBJECTS = 128;
inline constexpr int TILE_SIZE = 16;
inline constexpr double PI = std::numbers::pi;
inline constexpr std::uint64_t TICKS_PER_DAY = 24000;

struct UIHitTest {
    std::vector<Rect> active;
    std::vector<Rect> pending;
    bool pending_dirty = false;

    void begin_frame() {
        pending.clear();
        pending_dirty = true;
    }

    void add(const Rect& rect) {
        pending.push_back(rect);
        pending_dirty = true;
    }

    void commit_if_dirty() {
        if (!pending_dirty)
            return;
        active.swap(pending);
        pending.clear();
        pending_dirty = false;
    }

    [[nodiscard]] Rect get_rect_at(int x, int y) const noexcept {
        for (const auto& rect : active) {
            if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h)
                return rect;
        }
        return {0, 0, 0, 0};
    }
    
    [[nodiscard]] bool contains(int x, int y) const noexcept {
        for (const auto& rect : active) {
            if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
                return true;
            }
        }
        return false;
    }
};

using rng_t = std::mt19937;

[[nodiscard]] inline std::uint32_t random_u32_inclusive(rng_t& rng, std::uint32_t range) noexcept {
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

[[nodiscard]] Color get_ambient_color(std::uint64_t total_ticks) noexcept;

struct MapPixel {
    std::uint8_t R{};
    std::uint8_t G{};
    std::uint8_t B{};
};

[[nodiscard]] constexpr int wrap_coord(int x, int size = WORLD_WIDTH) noexcept {
    if (x < 0)
        x = (size + x) % size;
    else if (x >= size)
        x = x % size;
    return x;
}

[[nodiscard]] inline TilePosition neighbor_from_pos(TilePosition p, Direction direction) noexcept {
    const int x = static_cast<int>(p.x);
    const int y = static_cast<int>(p.y);
    switch (direction) {
        case Direction::Up:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x)),
                                static_cast<std::uint16_t>(wrap_coord(y - 1))};
        case Direction::Left:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x - 1)),
                                static_cast<std::uint16_t>(wrap_coord(y))};
        case Direction::Down:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x)),
                                static_cast<std::uint16_t>(wrap_coord(y + 1))};
        case Direction::Right:
            return TilePosition{static_cast<std::uint16_t>(wrap_coord(x + 1)),
                                static_cast<std::uint16_t>(wrap_coord(y))};
        default:
            return INVALID_POS;
    }
}

[[nodiscard]] inline double toroidal_distance(TilePosition p1, TilePosition p2) noexcept {
    int dx = std::abs(static_cast<int>(p1.x) - static_cast<int>(p2.x));
    if (dx > WORLD_WIDTH / 2)
        dx = WORLD_WIDTH - dx;
    int dy = std::abs(static_cast<int>(p1.y) - static_cast<int>(p2.y));
    if (dy > WORLD_WIDTH / 2)
        dy = WORLD_WIDTH - dy;
    return std::hypot(static_cast<double>(dx), static_cast<double>(dy));
}

// SDL types removed - using Sokol for graphics

inline constexpr std::size_t WORLD_SIZE = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
inline constexpr std::size_t ENTITY_POOL_SIZE = static_cast<std::size_t>(MAX_OBJECTS) * MAX_OBJECTS;

template <typename T>
using WorldMap = TileMap<T, WORLD_WIDTH, WORLD_WIDTH>;

struct GameContext {
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

    std::int32_t active_event_id = -1;  // ID текущего текстового события. -1 если событий нет.
    entt::entity battle_target_entity = entt::null;  // ECS entity for battle target

    int curs_x = 0;
    int curs_y = 0;
    int pick_x = 0;
    int pick_y = 0;
    bool mouse_pressed = false;  // True while mouse button is held
    bool click_event = false;    // True for one frame when click detected
    std::array<char, 64> input{};
    UIHitTest ui_hit_test{};
    bool ui_input_enabled = true;

    int cam_x = WORLD_WIDTH / 2;
    int cam_y = WORLD_WIDTH / 2;
    TilePosition pos_cam{0, 0};

    bool map_dragging = false;
    int drag_start_x = 0;
    int drag_start_y = 0;
    int drag_last_x = 0;
    int drag_last_y = 0;
    Rect pressed_button_rect{0, 0, 0, 0};  // Exact button rect that was pressed
    float map_offset_x = 0.0f;
    float map_offset_y = 0.0f;
    
    // Pinch zoom state
    bool pinch_active = false;
    float pinch_start_dist = 0.0f;
    float pinch_start_zoom = 1.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    static constexpr float friction = 0.92f;
    static constexpr float velocity_threshold = 0.5f;
    std::uint32_t last_frame_time = 0;

    float zoom = 1.0f;
    float target_zoom = 1.0f;
    static constexpr float min_zoom = 0.5f;  // Limit zoom out to reduce draw calls
    static constexpr float max_zoom = 4.0f;
    static constexpr float zoom_speed = 0.15f;

    int frame = 0;
    std::uint64_t hour = 0;
    std::uint32_t seed = 0;
    bool redraw_requested = true;
    std::uint32_t last_present_ticks = 0;

    // Keyboard state for movement
    bool key_up = false;
    bool key_down = false;
    bool key_left = false;
    bool key_right = false;
    
    // Optimization flags
    bool pos_map_dirty = true;  // True when pos_map needs rebuild

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
    WorldMap<float> field;          // Elevation heightmap
    WorldMap<float> heightmap;      // Stored heightmap for reference
    WorldMap<float> continent_map;  // Continent/island map (0=ocean, 1=land)
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

    void init_world() {
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

    [[nodiscard]] TilePosition get_neighbor(TilePosition pos, Direction direction) const noexcept {
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

[[nodiscard]] std::string resolve_path(const GameContext& ctx, std::string_view relative);

#ifdef __EMSCRIPTEN__
void em_init_persistent_fs();
void em_sync_persistent_fs();
#endif

[[nodiscard]] inline int to_render_x(const GameContext& ctx, int x) noexcept {
    return static_cast<int>(static_cast<float>(x) * ctx.input_scale_x);
}

[[nodiscard]] inline int to_render_y(const GameContext& ctx, int y) noexcept {
    return static_cast<int>(static_cast<float>(y) * ctx.input_scale_y);
}

void toggle_fullscreen(GameContext& ctx);
bool handle_fullscreen_key(GameContext& ctx, KeyCode key);
void update_map_inertia(GameContext& ctx, float delta_time);
void begin_map_drag(GameContext& ctx);
void apply_map_drag(GameContext& ctx, float dx, float dy, float scale = 1.0f);
void end_map_drag(GameContext& ctx);

[[nodiscard]] GameState* current_state(const GameContext& ctx) noexcept;

[[nodiscard]] GameMode current_game_mode(const GameContext& ctx) noexcept;

using StateCreatorFn = std::unique_ptr<GameState> (*)();

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

template <typename T>
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

void trigger_screenshot(GameContext& ctx);
void set_pick(GameContext& ctx, int x, int y);
[[nodiscard]] float
calc_frame_delta_time(GameContext& ctx, float frame_ms = 16.67f, float max_delta = 3.0f);
void reset_map_view(GameContext& ctx);

// update_map_texture removed - using Sokol textures
void build_terrain_map_range(GameContext& ctx,
                             std::size_t start,
                             std::size_t count,
                             float water_threshold = 0.35f);
void build_terrain_map(GameContext& ctx);
void seed_forests(GameContext& ctx, std::size_t start, std::size_t count);
void spread_forests_step(GameContext& ctx, std::size_t start, std::size_t count);
