#include "core/game_context.h"
#include "core/game_state.h"
#include "ecs/world.h"

GameContext::GameContext()
    : rng(std::random_device{}()), ecs_world(std::make_unique<ecs::World>()) {
    pos_cam = TilePosition{static_cast<std::uint16_t>(cam_x), static_cast<std::uint16_t>(cam_y)};
    ecs_world->init();
}

GameContext::~GameContext() = default;

GameState* current_state(const GameContext& ctx) noexcept {
    return ctx.state_stack.empty() ? nullptr : ctx.state_stack.back().get();
}

std::unique_ptr<GameState> StateRegistry::create(GameMode mode) const {
    auto fn = creators[static_cast<std::size_t>(mode)];
    return fn ? fn() : nullptr;
}

void push_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick) {
    if (state)
        ctx.state_stack.push_back(std::move(state));
    if (reset_pick)
        ctx.picked = false;
}

void replace_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick) {
    if (!state)
        return;
    if (ctx.state_stack.empty()) {
        ctx.state_stack.push_back(std::move(state));
    } else {
        ctx.state_stack.back() = std::move(state);
    }
    if (reset_pick)
        ctx.picked = false;
}

bool pop_state(GameContext& ctx, bool reset_pick) {
    if (!ctx.state_stack.empty()) {
        ctx.state_stack.pop_back();
        if (reset_pick)
            ctx.picked = false;
        return true;
    }
    return false;
}

void clear_states(GameContext& ctx, bool reset_pick) {
    ctx.state_stack.clear();
    if (reset_pick)
        ctx.picked = false;
}

// ECS singleton accessor implementations

std::uint64_t GameContext::ticks() const noexcept {
    if (ecs_world)
        return ecs_world->time().ticks;
    return hour;
}

void GameContext::set_ticks(std::uint64_t t) noexcept {
    hour = t;
    if (ecs_world)
        ecs_world->time().ticks = t;
}

int GameContext::speed() const noexcept {
    if (ecs_world)
        return ecs_world->time().game_speed;
    return game_speed;
}

void GameContext::set_speed(int s) noexcept {
    game_speed = s;
    if (ecs_world)
        ecs_world->time().game_speed = s;
}

bool GameContext::is_paused() const noexcept {
    if (ecs_world)
        return ecs_world->time().paused;
    return paused;
}

void GameContext::set_paused(bool p) noexcept {
    paused = p;
    if (ecs_world)
        ecs_world->time().paused = p;
}

SDL_Color get_ambient_color(std::uint64_t total_ticks) noexcept {
    const std::uint64_t day_tick = total_ticks % TICKS_PER_DAY;
    const float progress = static_cast<float>(day_tick) / static_cast<float>(TICKS_PER_DAY);

    if (progress < 0.2f || progress > 0.9f) {
        return {0, 0, 0, 210};
    } else if (progress >= 0.2f && progress < 0.35f) {
        float f = (progress - 0.2f) / 0.15f;
        return {0, 0, 0, static_cast<std::uint8_t>(210 * (1.0f - f))};
    } else if (progress >= 0.35f && progress < 0.75f) {
        return {0, 0, 0, 0};
    } else {
        float f = (progress - 0.75f) / 0.15f;
        return {0, 0, 0, static_cast<std::uint8_t>(210 * f)};
    }
}

std::string resolve_path(const GameContext& ctx, std::string_view relative) {
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

void toggle_fullscreen(GameContext& ctx) {
    ctx.fullscreen = !ctx.fullscreen;
    SDL_SetWindowFullscreen(ctx.window, ctx.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

bool handle_fullscreen_key(GameContext& ctx, SDL_Keycode key) {
    if (key != SDLK_0)
        return false;
    toggle_fullscreen(ctx);
    return true;
}

void update_map_inertia(GameContext& ctx, float delta_time) {
    if (ctx.map_dragging)
        return;

    ctx.map_offset_x += ctx.velocity_x * delta_time;
    ctx.map_offset_y += ctx.velocity_y * delta_time;

    ctx.velocity_x *= std::pow(ctx.friction, delta_time);
    ctx.velocity_y *= std::pow(ctx.friction, delta_time);

    if (std::abs(ctx.velocity_x) < ctx.velocity_threshold)
        ctx.velocity_x = 0.0f;
    if (std::abs(ctx.velocity_y) < ctx.velocity_threshold)
        ctx.velocity_y = 0.0f;
}

void begin_map_drag(GameContext& ctx) {
    ctx.map_dragging = true;
    ctx.velocity_x = 0.0f;
    ctx.velocity_y = 0.0f;
}

void apply_map_drag(GameContext& ctx, float dx, float dy, float scale) {
    if (!ctx.map_dragging)
        return;

    const float scaled_dx = dx * scale;
    const float scaled_dy = dy * scale;

    ctx.map_offset_x += scaled_dx;
    ctx.map_offset_y += scaled_dy;

    ctx.velocity_x = ctx.velocity_x * 0.5f + scaled_dx * 0.5f;
    ctx.velocity_y = ctx.velocity_y * 0.5f + scaled_dy * 0.5f;
}

void end_map_drag(GameContext& ctx) {
    ctx.map_dragging = false;
}

void trigger_screenshot(GameContext& ctx) {
    ctx.screenshot = true;
}

void set_pick(GameContext& ctx, int x, int y) {
    ctx.pick_x = x;
    ctx.pick_y = y;
    ctx.picked = true;
}

float calc_frame_delta_time(GameContext& ctx, float frame_ms, float max_delta) {
    const std::uint32_t current_time = SDL_GetTicks();
    float delta_time = static_cast<float>(current_time - ctx.last_frame_time) / frame_ms;
    ctx.last_frame_time = current_time;
    if (delta_time > max_delta)
        delta_time = max_delta;
    return delta_time;
}

void reset_map_view(GameContext& ctx) {
    ctx.map_offset_x = 0.0f;
    ctx.map_offset_y = 0.0f;
    ctx.velocity_x = 0.0f;
    ctx.velocity_y = 0.0f;
}

SDL_Texture*
update_map_texture(SDL_Renderer* renderer, SDL_Texture* texture, const MapPixel* pixels, int size) {
    if (!texture) {
        texture = SDL_CreateTexture(renderer,
                                    SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    size,
                                    size);
        if (!texture)
            return nullptr;
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
        auto* row =
            reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
        const MapPixel* src = pixels + y * size;
        for (int x = 0; x < size; ++x) {
            row[x] = SDL_MapRGB(fmt, src[x].R, src[x].G, src[x].B);
        }
    }

    SDL_FreeFormat(fmt);
    SDL_UnlockTexture(texture);
    return texture;
}

void build_terrain_map_range(GameContext& ctx,
                             std::size_t start,
                             std::size_t count,
                             float water_threshold) {
    if (start >= WORLD_SIZE || count == 0)
        return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    for (std::size_t i = start; i < end; ++i) {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH),
                               static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        float h = ctx.field[pos];
        const float t = ctx.temperature[pos];
        const float w = ctx.humidity[pos];
        const float cont = ctx.continent_map[pos];

        ctx.heightmap[pos] = h;

        if (cont < 0.15f) {
            h = h * 0.35f + 0.1f;
        } else if (cont < 0.3f) {
            h = h * 0.5f + 0.15f;
        } else if (cont > 0.6f) {
            h = std::min(1.0f, h * 1.15f + 0.08f);
        } else if (cont > 0.45f) {
            h = std::min(1.0f, h * 1.05f + 0.03f);
        }

        if (h < water_threshold) {
            ctx.relief[pos] = TerrainType::Water;
            ctx.world_map[pos] = {25, 75, 155};
        } else if (h > 0.85f) {
            if (t < 0.35f) {
                ctx.relief[pos] = TerrainType::Snow;
                ctx.world_map[pos] = {245, 245, 255};
            } else {
                ctx.relief[pos] = TerrainType::Mount;
                ctx.world_map[pos] = {105, 105, 105};
            }
        } else {
            if (t < 0.27f) {
                if (w < 0.4f) {
                    ctx.relief[pos] = TerrainType::Tundra;
                    ctx.world_map[pos] = {160, 180, 180};
                } else {
                    ctx.relief[pos] = TerrainType::Snow;
                    ctx.world_map[pos] = {220, 230, 255};
                }
            } else if (t > 0.66f) {
                if (w < 0.5f) {
                    ctx.relief[pos] = TerrainType::Sand;
                    ctx.world_map[pos] = {230, 210, 150};
                } else if (w > 0.75f) {
                    ctx.relief[pos] = TerrainType::Jungle;
                    ctx.world_map[pos] = {34, 139, 34};
                } else {
                    ctx.relief[pos] = TerrainType::Grass;
                    ctx.world_map[pos] = {160, 200, 100};
                }
            } else {
                if (w > 0.7f) {
                    ctx.relief[pos] = TerrainType::Swamp;
                    ctx.world_map[pos] = {85, 107, 47};
                } else if (w < 0.25f) {
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

void build_terrain_map(GameContext& ctx) {
    build_terrain_map_range(ctx, 0, WORLD_SIZE);
}

void seed_forests(GameContext& ctx, std::size_t start, std::size_t count) {
    if (start >= WORLD_SIZE || count == 0)
        return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);
    for (std::size_t i = start; i < end; ++i) {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH),
                               static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        if (ctx.relief[pos] == TerrainType::Grass) {
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 30) {
                ctx.flora[pos] = 120 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 30));
            }
        } else if (ctx.relief[pos] == TerrainType::Dirt) {
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 5) {
                ctx.flora[pos] = 100 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 20));
            }
        } else if (ctx.relief[pos] == TerrainType::Jungle) {
            const int drop = random_u32_inclusive(ctx.rng, 1000);
            if (drop < 200) {
                ctx.flora[pos] = 150 + static_cast<std::uint8_t>(random_u32_inclusive(ctx.rng, 35));
            }
        }
    }
}

void spread_forests_step(GameContext& ctx, std::size_t start, std::size_t count) {
    if (start >= WORLD_SIZE || count == 0)
        return;
    const std::size_t end = std::min(start + count, WORLD_SIZE);

    for (std::size_t i = start; i < end; ++i) {
        const TilePosition pos{static_cast<std::uint16_t>(i % WORLD_WIDTH),
                               static_cast<std::uint16_t>(i / WORLD_WIDTH)};
        if (ctx.flora[pos] > 30) {
            const std::uint32_t drop = random_u32_inclusive(ctx.rng, 3);
            const TilePosition neighbor = neighbor_from_pos(pos, static_cast<Direction>(drop));

            if (is_valid(neighbor)) {
                TerrainType type = ctx.relief[neighbor];
                if (type == TerrainType::Grass || type == TerrainType::Jungle
                    || type == TerrainType::Swamp) {
                    int spread_amount = static_cast<int>(ctx.flora[pos])
                                        - static_cast<int>(random_u32_inclusive(ctx.rng, 60));
                    if (spread_amount > 20) {
                        ctx.flora[neighbor] = static_cast<std::uint8_t>(
                            std::max(static_cast<int>(ctx.flora[neighbor]), spread_amount));
                    }
                }
            }
        }
    }
}

#ifdef __EMSCRIPTEN__
void em_init_persistent_fs() {
    EM_ASM(FS.mkdir('/persist'); FS.mount(IDBFS, {}, '/persist'); FS.syncfs(
        true,
        function(err) {
            if (err) {
                console.error('IDBFS load error:', err);
            } else {
                console.log('IDBFS: Loaded persistent storage');
            }
        }););
}

void em_sync_persistent_fs() {
    EM_ASM(FS.syncfs(
        false,
        function(err) {
            if (err) {
                console.error('IDBFS sync error:', err);
            } else {
                console.log('IDBFS: Saved to persistent storage');
            }
        }););
}
#endif
