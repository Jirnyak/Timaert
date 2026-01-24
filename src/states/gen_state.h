#pragma once

#include "core/game_state.h"
#include "core/tergen.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/resource_generator.h"
#include "systems/politics.h"
#include "systems/save_game.h"
#include "states/play_state.h"
#include "ecs/systems/spawn_system.h"
#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <memory>

class GenState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Gen; }
    
    // Generation cannot be saved mid-process
    [[nodiscard]] bool can_save() const noexcept override { return false; }
    [[nodiscard]] GameMode fallback_mode() const noexcept override { return GameMode::Menu; }
    
    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/) override
    {
        if (current_game_mode(ctx) != GameMode::Gen) return;

        if (phase_ == Phase::Idle || phase_ == Phase::Done) {
            begin_generation(ctx);
        }

        const std::uint64_t start = SDL_GetPerformanceCounter();
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        std::uint64_t now = start;
        int iterations = 0;
        do {
            step_generation(ctx);
            now = SDL_GetPerformanceCounter();
            iterations++;
        } while (phase_ != Phase::Done &&
                 ((static_cast<double>(now - start) * 1000.0) / freq) < kGenerationBudgetMs &&
                 iterations < kMaxStepsPerFrame);
        ctx.redraw_requested = true;
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/) override
    {
        ui_clear_black(ctx.renderer);

        const float progress = generation_progress();
        const int percent = static_cast<int>(progress * 100.0f + 0.5f);
        const std::string title = status_text_.empty() ? "Generating world..." : status_text_;
        const std::string percent_text = std::to_string(percent) + "%";

        const int bar_width = ctx.window_width / 2;
        const int bar_height = 28;
        const int bar_x = ctx.window_width / 2 - bar_width / 2;
        const int bar_y = ctx.window_height / 2 + 20;

        render_text(ctx, title,
                    ctx.window_width / 2 - 170, ctx.window_height / 2 - 20, 340, 28,
                    {255, 255, 255, 255});

        SDL_Rect bar_bg = {bar_x, bar_y, bar_width, bar_height};
        ui_draw_panel(ctx.renderer, bar_bg, ui_color("#0B1D2A"), ui_color("#16C79A"));

        const int fill_width = static_cast<int>(static_cast<float>(bar_width - 4) * std::clamp(progress, 0.0f, 1.0f));
        SDL_Rect bar_fill = {bar_x + 2, bar_y + 2, fill_width, bar_height - 4};
        ui_fill_rect(ctx.renderer, bar_fill, ui_color("#16C79A"));

        render_text(ctx, percent_text,
                    ctx.window_width / 2 - 30, bar_y + 2, 60, bar_height - 4,
                    {0, 0, 0, 255});
    }
    
private:
    enum class MapTarget : std::uint8_t {
        Elevation,
        Temperature,
        Humidity,
        Count
    };

    enum class Phase : std::uint8_t {
        Idle,
        GenerateContinents,  // Generate continent/island map
        InitField,
        NoiseInject,
        Diffuse,
        NormalizeMinMax,
        NormalizeApply,
        TerrainMap,
        UpdateTexture,
        GenerateFlora,
        SpreadFlora,
        InitEntities,
        SpawnTrees,
        InitPolitics,        // Initialize politics system
        PlaceCapitals,       // Place faction capitals (BEFORE filling politics map)
        FillPoliticsMap,     // BFS fill politics map from capitals
        InitWorldManager,    // Place secondary settlements
        SaveGame,
        Done
    };

    static constexpr int kOctaves = 6;
    static constexpr int kDiffusionSteps = 64;
    static constexpr float kBaseDiffusion = 0.25f;
    static constexpr float kBaseNoise = 0.1f;
    static constexpr std::size_t kChunkSize = 120000;
    static constexpr std::size_t kTextureUnits = WORLD_SIZE / 4;
    static constexpr std::size_t kPostUnits = 20000;
    static constexpr double kGenerationBudgetMs = 20.0;
    static constexpr int kMaxStepsPerFrame = 512;
    static constexpr int kFloraSpreadSteps = 10;

    Phase phase_ = Phase::Idle;
    MapTarget target_map_ = MapTarget::Elevation;
    std::size_t continent_index_ = 0;  // NEW: For continent map generation
    std::size_t init_index_ = 0;
    std::size_t noise_index_ = 0;
    std::size_t diffuse_index_ = 0;
    std::size_t normalize_index_ = 0;
    std::size_t terrain_index_ = 0;
    std::size_t flora_index_ = 0;
    int flora_spread_step_ = 0;
    std::size_t interior_count_ = 0;
    int octave_ = 0;
    int diffusion_step_ = 0;
    float noise_amp_ = 0.0f;
    float diffusion_ = 0.0f;
    float min_value_ = 0.0f;
    float max_value_ = 0.0f;
    float inv_range_ = 1.0f;
    bool field_primary_ = true;
    std::size_t completed_units_ = 0;
    std::size_t total_units_ = 1;
    std::string status_text_ = "Generating world...";
    int num_continents_ = 3;  // Random 3-6 continents
    int num_islands_ = 3;     // Random islands
    
    // politics_sys_ удален, используем ctx.world_manager->politics

    struct MapShape {
        int x, y;
        float radius;
        float radius_sq;
        float noise_strength;
        uint32_t seed;
        float influence_sign;   // -1.0 (океан) или 1.0 (суша)
        float influence_factor; // Сила влияния (0.95, 0.98 и т.д.)
        float noise_offset;     // Смещение шума (i * 50.0f)
    };
    std::vector<MapShape> shapes_;

    void begin_generation(GameContext& ctx)
    {
        num_continents_ = ctx.num_continents;
        const uint32_t island_count_seed = (static_cast<uint32_t>(ctx.seed) * 83492791u) ^ 6666u;
        num_islands_ = 1 + (island_count_seed % 3);

        // --- PRE-CALCULATE SHAPES (OPTIMIZATION) ---
        shapes_.clear();
        if (num_continents_ < 10) {
            // 1. OCEANS
            const uint32_t ocean_seed = ctx.seed + 3000u;
            const int oceans = (ctx.water_amount > 0) ? ctx.water_amount : (1 + (((ctx.seed ^ 8877u) % 1000u) % 2u));
            
            for (int i = 0; i < oceans; ++i) {
                const uint32_t h1 = (static_cast<uint32_t>(i) * 73856093u) ^ ocean_seed;
                const uint32_t h2 = (static_cast<uint32_t>(i) * 19349663u) ^ ocean_seed;
                const uint32_t h3 = (static_cast<uint32_t>(i) * 43614093u) ^ ocean_seed;
                
                const float r = 100.0f + (static_cast<float>(h3 % 800u) / 10.0f);
                const int ox = static_cast<int>(h1 % 1024u);
                const int oy = static_cast<int>(h2 % 1024u);
                
                shapes_.push_back({ox, oy, r, r*r, 20.0f, ocean_seed, -1.0f, 0.95f, i * 50.0f});

                // 2. ISLANDS IN OCEANS
                const uint32_t ocean_island_seed = ctx.seed + 4000u;
                const uint32_t islands_in_ocean = 2u + ((ocean_island_seed + i) % 4u);
                for (uint32_t j = 0; j < islands_in_ocean; ++j) {
                    const uint32_t iseed = ocean_island_seed + i * 100u + j * 17u;
                    const int ix = ox + (static_cast<int>((iseed * 11u) % 1000u) - 500);
                    const int iy = oy + (static_cast<int>((iseed * 23u) % 1000u) - 500);
                    if (ix >= 0 && ix < 1024 && iy >= 0 && iy < 1024) {
                        const float dist_sq = squared_distance(ox, oy, ix, iy); // Используем функцию из tergen.h
                        if (dist_sq < r * r * 0.81f) { // 0.9 * 0.9 approx
                            const float ir = 10.0f + (static_cast<float>((iseed * 31u) % 200u) / 10.0f);
                            shapes_.push_back({ix, iy, ir, ir*ir, 10.0f, iseed, 1.0f, 0.6f, 0.0f});
                        }
                    }
                }
            }

            // 3. CONTINENTS
            const uint32_t continent_seed = ctx.seed + 1000u;
            for (int i = 0; i < num_continents_; ++i) {
                const uint32_t h1 = (static_cast<uint32_t>(i) * 73856093u) ^ continent_seed;
                const uint32_t h2 = (static_cast<uint32_t>(i) * 19349663u) ^ continent_seed;
                const uint32_t h3 = (static_cast<uint32_t>(i) * 43614093u) ^ continent_seed;
                
                const float r = 75.0f + (static_cast<float>(h3 % 1450u) / 10.0f);
                const int cx = static_cast<int>(h1 % 1024u);
                const int cy = static_cast<int>(h2 % 1024u);
                const float n_str = 20.0f + (static_cast<float>(h3 % 10u) / 2.0f);
                
                shapes_.push_back({cx, cy, r, r*r, n_str, continent_seed, 1.0f, 0.98f, i * 50.0f});
            }

            // 4. ISLAND CLUSTERS
            const uint32_t island_seed = ctx.seed + 2000u;
            for (int i = 0; i < num_islands_; ++i) {
                const uint32_t cluster_hash = (static_cast<uint32_t>(i) * 73856093u) ^ island_seed;
                const uint32_t island_hash = (static_cast<uint32_t>(i) * 83492791u) ^ island_seed;
                const uint32_t size_hash = (static_cast<uint32_t>(i) * 43614093u) ^ island_seed;
                
                const float r = 10.0f + (static_cast<float>(size_hash % 900u) / 10.0f);
                const int cl_x = static_cast<int>(cluster_hash % 1024u);
                const int cl_y = static_cast<int>((cluster_hash >> 16) % 1024u);
                
                const int ix = (cl_x + static_cast<int>(island_hash % 200u) - 100 + 1024) % 1024;
                const int iy = (cl_y + static_cast<int>((island_hash >> 16) % 200u) - 100 + 1024) % 1024;
                
                shapes_.push_back({ix, iy, r, r*r, 10.0f, island_seed + static_cast<uint32_t>(i), 1.0f, 0.65f, 0.0f});
            }
            
            phase_ = Phase::GenerateContinents;
            continent_index_ = 0;
        } else {
            // Pure noise mode
            for (int y = 0; y < WORLD_WIDTH; ++y) {
                for (int x = 0; x < WORLD_WIDTH; ++x) {
                    const TilePosition tile_pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
                    ctx.continent_map[tile_pos] = 0.5f;
                }
            }
            phase_ = Phase::InitField;
            init_index_ = 0;
        }
        // ----------------------------------------------------
        
        target_map_ = MapTarget::Elevation;
        noise_index_ = 0;
        diffuse_index_ = 0;
        normalize_index_ = 0;
        terrain_index_ = 0;
        flora_index_ = 0;
        flora_spread_step_ = 0;
        interior_count_ = WORLD_SIZE;
        octave_ = 0;
        diffusion_step_ = 0;
        noise_amp_ = kBaseNoise;
        diffusion_ = kBaseDiffusion;
        min_value_ = std::numeric_limits<float>::max();
        max_value_ = std::numeric_limits<float>::lowest();
        inv_range_ = 1.0f;
        field_primary_ = true;
        completed_units_ = 0;
        const std::size_t units_per_map = WORLD_SIZE + 
                                         (static_cast<std::size_t>(kOctaves) * WORLD_SIZE) + 
                                         (static_cast<std::size_t>(kOctaves) * kDiffusionSteps * WORLD_SIZE) + 
                                         WORLD_SIZE + WORLD_SIZE;
        
        total_units_ = (num_continents_ < 10 ? WORLD_SIZE : 0) + (3 * units_per_map) + WORLD_SIZE + kTextureUnits + WORLD_SIZE + (static_cast<std::size_t>(WORLD_SIZE) * kFloraSpreadSteps) + kPostUnits;

        if (total_units_ == 0) total_units_ = 1;
        ctx.seed = random_u32_inclusive(ctx.rng, 10000);
        status_text_ = "Preparing terrain...";
        SDL_Log("GEN: Started. Seed: %u, Continents: %d, Total Units: %zu", ctx.seed, num_continents_, total_units_);
    }

    [[nodiscard]] float* current_field(GameContext& ctx) const
    {
        float* target = ctx.field.data();
        if (target_map_ == MapTarget::Temperature) target = ctx.temperature.data();
        else if (target_map_ == MapTarget::Humidity) target = ctx.humidity.data();

        return field_primary_ ? target : ctx.temp.data();
    }

    void step_generation(GameContext& ctx)
    {
        switch (phase_)
        {
            case Phase::GenerateContinents:  // NEW
                step_generate_continents(ctx);
                break;
            case Phase::InitField:
                step_init_field(ctx);
                break;
            case Phase::NoiseInject:
                step_noise(ctx);
                break;
            case Phase::Diffuse:
                step_diffuse(ctx);
                break;
            case Phase::NormalizeMinMax:
                step_normalize_minmax(ctx);
                break;
            case Phase::NormalizeApply:
                step_normalize_apply(ctx);
                break;
            case Phase::TerrainMap:
                step_terrain_map(ctx);
                break;
            case Phase::UpdateTexture:
                step_update_texture(ctx);
                break;
            case Phase::GenerateFlora:
                step_generate_flora(ctx);
                break;
             case Phase::SpreadFlora: 
                step_spread_flora(ctx);
                break;
            case Phase::InitEntities:
                step_init_entities();
                break;
            case Phase::SpawnTrees:
                step_spawn_trees(ctx);
                break;
            case Phase::InitPolitics:
                step_init_politics(ctx);
                break;
            case Phase::PlaceCapitals:
                step_place_capitals(ctx);
                break;
            case Phase::FillPoliticsMap:
                step_fill_politics_map(ctx);
                break;
            case Phase::InitWorldManager:
                step_init_world_manager(ctx);
                break;
            case Phase::SaveGame:
                step_save(ctx);
                break;
            case Phase::Done:
            case Phase::Idle:
            default:
                break;
        }
    }

    void step_init_field(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - init_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        std::fill_n(current_field(ctx) + init_index_, count, 0.0f);
        
        init_index_ += count;
        completed_units_ += count;

        if (init_index_ >= WORLD_SIZE)
        {
            phase_ = Phase::NoiseInject;
            noise_index_ = 0;
            status_text_ = "Adding noise...";
        }
    }

    void step_generate_continents(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - continent_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t idx = continent_index_ + i;
            const int x = static_cast<int>(idx % WORLD_WIDTH);
            const int y = static_cast<int>(idx / WORLD_WIDTH);
            
            float max_influence = 0.0f;

            // Iterate over PRE-CALCULATED shapes instead of regenerating them
            for (const auto& shape : shapes_) {
                const float dist_sq = squared_distance(x, y, shape.x, shape.y);
                if (dist_sq < shape.radius_sq) {
                    float dist = std::sqrt(dist_sq);
                    // Add noise for organic edges
                    const float noise = smoothNoise2D(
                        static_cast<float>(x) / 20.0f + shape.noise_offset,
                        static_cast<float>(y) / 20.0f + shape.noise_offset,
                        shape.seed
                    ) * shape.noise_strength;
                    
                    dist -= noise;
                    
                    const float influence = std::max(0.0f, 1.0f - (dist / shape.radius));
                    
                    if (shape.influence_sign < 0.0f) {
                        // Ocean: reduces influence
                        max_influence = std::max(0.0f, max_influence - influence * shape.influence_factor);
                    } else {
                        // Land: increases influence
                        max_influence = std::max(max_influence, influence * shape.influence_factor);
                    }
                }
            }
            
            const TilePosition tile_pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            ctx.continent_map[tile_pos] = std::clamp(max_influence, 0.0f, 1.0f);
        }
        
        continent_index_ += count;
        completed_units_ += count;
        
        if (continent_index_ >= WORLD_SIZE)
        {
            phase_ = Phase::InitField;
            init_index_ = 0;
            status_text_ = "Preparing terrain...";
        }
    }

    void step_noise(GameContext& ctx)
    {
        // Apply noise to ALL tiles, including edges, for proper toroidal wrapping
        const std::size_t remaining = WORLD_SIZE - noise_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        float* field = current_field(ctx);
        const std::uint32_t seed = ctx.seed + static_cast<std::uint32_t>(octave_ * 1013u);

        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t idx = noise_index_ + i;
            const int x = static_cast<int>(idx % WORLD_WIDTH);
            const int y = static_cast<int>(idx / WORLD_WIDTH);
            field[idx] += noise2D(x, y, seed) * noise_amp_;
        }

        noise_index_ += count;
        completed_units_ += count;

        if (noise_index_ >= WORLD_SIZE)
        {
            phase_ = Phase::Diffuse;
            diffuse_index_ = 0;
            diffusion_step_ = 0;
            status_text_ = "Smoothing terrain...";
        }
    }

    void step_diffuse(GameContext& ctx)
    {
        float* target_buf = ctx.field.data();
        if (target_map_ == MapTarget::Temperature) target_buf = ctx.temperature.data();
        else if (target_map_ == MapTarget::Humidity) target_buf = ctx.humidity.data();
        float* in = field_primary_ ? target_buf : ctx.temp.data();
        float* out = field_primary_ ? ctx.temp.data() : target_buf;
        
        const std::size_t remaining = WORLD_SIZE - diffuse_index_;
        const std::size_t count = std::min(kChunkSize, remaining);

        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t idx = diffuse_index_ + i;
            const int x = static_cast<int>(idx % WORLD_WIDTH);
            const int y = static_cast<int>(idx / WORLD_WIDTH);
            
            // Toroidal wrapping for edges
            const int left = ((x - 1 + WORLD_WIDTH) % WORLD_WIDTH) + y * WORLD_WIDTH;
            const int right = ((x + 1) % WORLD_WIDTH) + y * WORLD_WIDTH;
            const int up = x + (((y - 1 + WORLD_WIDTH) % WORLD_WIDTH) * WORLD_WIDTH);
            const int down = x + (((y + 1) % WORLD_WIDTH) * WORLD_WIDTH);
            
            out[idx] = in[idx] + diffusion_ * (
                in[left] + in[right] +
                in[up] + in[down] -
                4.0f * in[idx]
            );
        }

        diffuse_index_ += count;
        completed_units_ += count;

        if (diffuse_index_ >= WORLD_SIZE)
        {
            field_primary_ = !field_primary_;
            diffusion_step_ += 1;
            diffuse_index_ = 0;

            if (diffusion_step_ >= kDiffusionSteps)
            {
                noise_amp_ *= 0.5f;
                diffusion_ *= 0.5f;
                octave_ += 1;
                diffusion_step_ = 0;

                if (octave_ >= kOctaves)
                {
                    if (!field_primary_)
                    {
                        if (target_map_ == MapTarget::Elevation) std::swap(ctx.field, ctx.temp);
                        else if (target_map_ == MapTarget::Temperature) std::swap(ctx.temperature, ctx.temp);
                        else if (target_map_ == MapTarget::Humidity) std::swap(ctx.humidity, ctx.temp);
                        
                        field_primary_ = true;
                    }
                    phase_ = Phase::NormalizeMinMax;
                    normalize_index_ = 0;
                    min_value_ = std::numeric_limits<float>::max();
                    max_value_ = std::numeric_limits<float>::lowest();
                    status_text_ = "Normalizing heightmap...";
                }
                else
                {
                    phase_ = Phase::NoiseInject;
                    noise_index_ = 0;
                    status_text_ = "Adding noise...";
                }
            }
        }
    }

    void step_normalize_minmax(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - normalize_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        float* field = current_field(ctx); 

        for (std::size_t i = 0; i < count; ++i)
        {
            const float value = field[normalize_index_ + i];
            min_value_ = std::min(min_value_, value);
            max_value_ = std::max(max_value_, value);
        }

        normalize_index_ += count;
        completed_units_ += count;

        if (normalize_index_ >= WORLD_SIZE)
        {
            inv_range_ = 1.0f / (max_value_ - min_value_ + 1e-6f);
            phase_ = Phase::NormalizeApply;
            normalize_index_ = 0;
            status_text_ = "Applying normalization...";
        }
    }

    void step_normalize_apply(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - normalize_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        
        // ИСПРАВЛЕНИЕ: Аналогично, используем правильный буфер
        float* field = current_field(ctx);

        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t idx = normalize_index_ + i;
            field[idx] = (field[idx] - min_value_) * inv_range_;
        }

        normalize_index_ += count;
        completed_units_ += count;

        if (normalize_index_ >= WORLD_SIZE)
        {
            int next_target = static_cast<int>(target_map_) + 1;
            if (next_target < static_cast<int>(MapTarget::Count))
            {
                target_map_ = static_cast<MapTarget>(next_target);
                phase_ = Phase::InitField;
                init_index_ = 0;
                noise_amp_ = kBaseNoise;
                diffusion_ = kBaseDiffusion;
                octave_ = 0;
                status_text_ = (target_map_ == MapTarget::Temperature) ? "Generating climate..." : "Calculating humidity...";
            }
            else
            {
                phase_ = Phase::TerrainMap;
                terrain_index_ = 0;
                status_text_ = "Building terrain map...";
            }
        }
    }

    void step_terrain_map(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - terrain_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        
        // Calculate water threshold: 
        // water 0 -> 0.0 (minimal water)
        // water 5 -> 0.35 (default, original value)
        // water 10 -> 0.7 (lots of water/archipelago)
        // Formula: threshold = 0.35 + (water_amount - 5) * 0.07
        float water_threshold = 0.35f + (static_cast<float>(ctx.water_amount) - 5.0f) * 0.07f;
        water_threshold = std::clamp(water_threshold, 0.0f, 1.0f);
        
        build_terrain_map_range(ctx, terrain_index_, count, water_threshold);

        terrain_index_ += count;
        completed_units_ += count;

        if (terrain_index_ >= WORLD_SIZE)
        {
            phase_ = Phase::UpdateTexture;
            status_text_ = "Updating textures...";
        }
    }

    void step_update_texture(GameContext& ctx)
    {
        ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.data(), WORLD_WIDTH));
        completed_units_ += kTextureUnits;
        // Generate resource maps (iron and clay) right after terrain is ready
        {
            resource::ResourceConfig rcfg;
            rcfg.seed_count = 60;
            rcfg.cluster_radius = 8;
            rcfg.sprinkle_fraction = 0.005;
            resource::generate_iron_map(ctx.relief.data(), ctx.resource_iron.data(), WORLD_SIZE, ctx.rng, rcfg);
            resource::generate_clay_map(ctx.relief.data(), ctx.resource_clay.data(), WORLD_SIZE, ctx.rng, rcfg);
        }
        // Переходим к генерации флоры
        phase_ = Phase::GenerateFlora;
        status_text_ = "Growing forests...";
    }

    void step_generate_flora(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - flora_index_;
        const std::size_t count = std::min(kChunkSize, remaining);
        
        seed_forests(ctx, flora_index_, count);
        
        flora_index_ += count;
        completed_units_ += count;
        
        if (flora_index_ >= WORLD_SIZE) {
            phase_ = Phase::SpreadFlora;
            flora_index_ = 0; 
            flora_spread_step_ = 0;
            status_text_ = "Spreading forests...";
        }
    }
    void step_spread_flora(GameContext& ctx)
    {
        const std::size_t remaining = WORLD_SIZE - flora_index_;
        const std::size_t safe_chunk = 10000; 
        const std::size_t count = std::min(safe_chunk, remaining);

        spread_forests_step(ctx, flora_index_, count);

        flora_index_ += count;
        completed_units_ += count;

        if (flora_index_ >= WORLD_SIZE) {
            flora_spread_step_++;
            flora_index_ = 0; 
            
            SDL_Log("GEN: Spread flora pass %d/%d done", flora_spread_step_, kFloraSpreadSteps);

            if (flora_spread_step_ >= kFloraSpreadSteps) {
                phase_ = Phase::InitEntities;
                status_text_ = "Spawning entities...";
            }
        }
    }
    void step_init_entities()
    {
        // Entities now managed by ECS - no legacy init needed
        completed_units_ += kPostUnits / 4;
        phase_ = Phase::SpawnTrees;
    }

    void step_spawn_trees(GameContext& ctx)
    {
        if (!ctx.ecs_world) {
            phase_ = Phase::InitPolitics;
            return;
        }
        
        int checker = 0;
        int attempts = 0; 
        const int max_attempts = MAX_OBJECTS * 100; 

        while (checker < MAX_OBJECTS && attempts < max_attempts)
        {
            attempts++;
            const auto drop_x = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const auto drop_y = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const TilePosition drop_tile{drop_x, drop_y};
            if (ctx.relief[drop_tile] == TerrainType::Grass || ctx.relief[drop_tile] == TerrainType::Dirt)
            {
                if (ctx.flora[drop_tile] > 50 || random_u32_inclusive(ctx.rng, 10) == 0) {
                    // Spawn tree directly to ECS
                    ecs::spawn_tree(*ctx.ecs_world, drop_tile);
                    checker++;
                }
            }
        }
        
        SDL_Log("GEN: Spawned %d trees to ECS after %d attempts", checker, attempts);

        completed_units_ += kPostUnits / 4;
        phase_ = Phase::InitPolitics;
        status_text_ = "Setting up factions...";
    }
    
    void step_init_politics(GameContext& ctx)
    {
        // Create and initialize the politics system
        politics_sys_ = std::make_unique<politics::PoliticsSystem>();
        politics_sys_->init(ctx.rng);
        
        completed_units_ += kPostUnits / 4;
        phase_ = Phase::PlaceCapitals;
        status_text_ = "Placing faction capitals...";
    }
    
    void step_place_capitals(GameContext& ctx)
    {
        // Place faction capitals and set their positions in politics system
        if (ctx.world_manager) {
            ctx.world_manager->place_faction_capitals(ctx, politics_sys_.get());
        }
        
        completed_units_ += kPostUnits / 4;
        phase_ = Phase::FillPoliticsMap;
        status_text_ = "Claiming territory...";
    }
    
    void step_fill_politics_map(GameContext& ctx)
    {
        // Fill politics map via BFS from faction capitals
        if (politics_sys_) {
            politics_sys_->fill_politics_map(ctx);
        }
        
        completed_units_ += kPostUnits / 4;
        phase_ = Phase::InitWorldManager;
        status_text_ = "Building settlements...";
    }

    void step_init_world_manager(GameContext& ctx)
    {
        if (ctx.world_manager)
        {
            ctx.world_manager->init();
            // Only place secondary settlements (capitals already placed)
            ctx.world_manager->place_faction_settlements(ctx, SettlementType::City, WorldManager::NUM_CITIES - 8);
            ctx.world_manager->place_faction_settlements(ctx, SettlementType::Town, WorldManager::NUM_TOWNS);
            ctx.world_manager->place_faction_settlements(ctx, SettlementType::Village, WorldManager::NUM_VILLAGES);
            ctx.world_manager->landmarks.propagate_all_fields(ctx.relief);
            
            ctx.world_manager->spawn_initial_npcs(ctx);
            ctx.world_manager->init_player(ctx);
            ctx.world_manager->rebuild_pos_map(ctx.pos_map);
        }

        completed_units_ += kPostUnits / 4;
        phase_ = Phase::SaveGame;
        status_text_ = "Saving...";
    }

    void step_save(GameContext& ctx)
    {
        if (ctx.world_manager)
        {
            (void)save_game::write_save(ctx, *ctx.world_manager);
        }
        
        const int start_time = 10000 + static_cast<int>(random_u32_inclusive(ctx.rng, 6000)) - 3000;
        ctx.hour = static_cast<std::uint64_t>(std::max(0, start_time));

        completed_units_ += kPostUnits / 4;
        phase_ = Phase::Done;
        status_text_ = "Starting...";
        clear_states(ctx, false);
        push_state(ctx, std::make_unique<PlayState>(), false);
    }

    [[nodiscard]] float generation_progress() const
    {
        const float total = static_cast<float>(total_units_);
        if (total <= 0.0f) return 0.0f;
        const float progress = static_cast<float>(completed_units_) / total;
        return std::clamp(progress, 0.0f, 1.0f);
    }
};

inline StateRegistrar<GenState> register_gen_state_{GameMode::Gen};
