#pragma once

#include "core/game_state.h"
#include "core/game_context.h"
#include "ui/ui.h"
#include <string>
#include <vector>
#include <cstdint>

class GenState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Gen;
    }

    // Generation cannot be saved mid-process
    [[nodiscard]] bool can_save() const noexcept override {
        return false;
    }
    [[nodiscard]] GameMode fallback_mode() const noexcept override {
        return GameMode::Menu;
    }

    void handle_event(SDL_Event& /*event*/,
                      GameContext& /*ctx*/,
                      TextureManager& /*textures*/) override {}

    void update(GameContext& ctx, TextureManager& /*textures*/) override {
        if (current_game_mode(ctx) != GameMode::Gen)
            return;

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
        } while (phase_ != Phase::Done
                 && ((static_cast<double>(now - start) * 1000.0) / freq) < kGenerationBudgetMs
                 && iterations < kMaxStepsPerFrame);
        ctx.redraw_requested = true;
    }

    void render(GameContext& ctx, TextureManager& /*textures*/) override {
        ui_clear_black(ctx.renderer);

        const float progress = generation_progress();
        const int percent = static_cast<int>(progress * 100.0f + 0.5f);
        const std::string title = status_text_.empty() ? "Generating world..." : status_text_;
        const std::string percent_text = std::to_string(percent) + "%";

        const int bar_width = ctx.window_width / 2;
        const int bar_height = 28;
        const int bar_x = ctx.window_width / 2 - bar_width / 2;
        const int bar_y = ctx.window_height / 2 + 20;

        render_text(ctx,
                    title,
                    ctx.window_width / 2 - 170,
                    ctx.window_height / 2 - 20,
                    340,
                    28,
                    {255, 255, 255, 255});

        SDL_Rect bar_bg = {bar_x, bar_y, bar_width, bar_height};
        ui_draw_panel(ctx.renderer, bar_bg, ui_color("#0B1D2A"), ui_color("#16C79A"));

        const int fill_width =
            static_cast<int>(static_cast<float>(bar_width - 4) * std::clamp(progress, 0.0f, 1.0f));
        SDL_Rect bar_fill = {bar_x + 2, bar_y + 2, fill_width, bar_height - 4};
        ui_fill_rect(ctx.renderer, bar_fill, ui_color("#16C79A"));

        render_text(ctx,
                    percent_text,
                    ctx.window_width / 2 - 30,
                    bar_y + 2,
                    60,
                    bar_height - 4,
                    {0, 0, 0, 255});
    }

private:
    enum class MapTarget : std::uint8_t { Elevation, Temperature, Humidity, Count };

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
        InitPolitics,      // Initialize politics system
        PlaceCapitals,     // Place faction capitals (BEFORE filling politics map)
        FillPoliticsMap,   // BFS fill politics map from capitals
        InitWorldManager,  // Place secondary settlements
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
        float influence_sign;    // -1.0 (океан) или 1.0 (суша)
        float influence_factor;  // Сила влияния (0.95, 0.98 и т.д.)
        float noise_offset;      // Смещение шума (i * 50.0f)
    };
    std::vector<MapShape> shapes_;

    void begin_generation(GameContext& ctx);

    [[nodiscard]] float* current_field(GameContext& ctx) const {
        float* target = ctx.field.data();
        if (target_map_ == MapTarget::Temperature)
            target = ctx.temperature.data();
        else if (target_map_ == MapTarget::Humidity)
            target = ctx.humidity.data();

        return field_primary_ ? target : ctx.temp.data();
    }

    void step_generation(GameContext& ctx);

    void step_init_field(GameContext& ctx);
    void step_generate_continents(GameContext& ctx);
    void step_noise(GameContext& ctx);
    void step_diffuse(GameContext& ctx);
    void step_normalize_minmax(GameContext& ctx);
    void step_normalize_apply(GameContext& ctx);
    void step_terrain_map(GameContext& ctx);
    void step_update_texture(GameContext& ctx);
    void step_generate_flora(GameContext& ctx);
    void step_spread_flora(GameContext& ctx);
    void step_init_entities();
    void step_spawn_trees(GameContext& ctx);
    void step_init_politics(GameContext& ctx);
    void step_place_capitals(GameContext& ctx);
    void step_fill_politics_map(GameContext& ctx);
    void step_init_world_manager(GameContext& ctx);
    void step_save(GameContext& ctx);

    [[nodiscard]] float generation_progress() const {
        const float total = static_cast<float>(total_units_);
        if (total <= 0.0f)
            return 0.0f;
        const float progress = static_cast<float>(completed_units_) / total;
        return std::clamp(progress, 0.0f, 1.0f);
    }
};

inline StateRegistrar<GenState> register_gen_state_{GameMode::Gen};
