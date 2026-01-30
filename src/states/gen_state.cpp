#include "states/gen_state.h"

#include <cstdio>
#include <print>

#include "states/play_state.h"
#include "core/tergen.h"
#include "core/tile_map.h"
#include "systems/world_manager.h"
#include "systems/landmark.h"
#include "systems/resource_generator.h"
#include "systems/politics.h"
#include "ecs/systems/spawn_system.h"

void GenState::begin_generation(GameContext& ctx) {
    phase_ = Phase::GenerateContinents;
    continent_index_ = 0;
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
    const std::size_t units_per_map =
        WORLD_SIZE + (static_cast<std::size_t>(kOctaves) * WORLD_SIZE)
        + (static_cast<std::size_t>(kOctaves) * kDiffusionSteps * WORLD_SIZE) + WORLD_SIZE
        + WORLD_SIZE;

    total_units_ = WORLD_SIZE + (3 * units_per_map) + WORLD_SIZE
                   + kTextureUnits + WORLD_SIZE
                   + (static_cast<std::size_t>(WORLD_SIZE) * kFloraSpreadSteps) + kPostUnits;

    if (total_units_ == 0)
        total_units_ = 1;
    ctx.seed = random_u32_inclusive(ctx.rng, 10000);
    status_text_ = "Generating terrain...";
    std::println("[GEN] Started. Seed: {}, Total Units: {}", ctx.seed, total_units_);
}

void GenState::step_generation(GameContext& ctx) {
    switch (phase_) {
        case Phase::GenerateContinents:
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
        case Phase::Done:
        case Phase::Idle:
        default:
            break;
    }
}

void GenState::step_init_field(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - init_index_;
    const std::size_t count = std::min(kChunkSize, remaining);
    std::fill_n(current_field(ctx) + init_index_, count, 0.0f);

    init_index_ += count;
    completed_units_ += count;

    if (init_index_ >= WORLD_SIZE) {
        phase_ = Phase::NoiseInject;
        noise_index_ = 0;
        status_text_ = "Adding noise...";
    }
}

void GenState::step_generate_continents(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - continent_index_;
    const std::size_t count = std::min(kChunkSize, remaining);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = continent_index_ + i;
        const int x = static_cast<int>(idx % WORLD_WIDTH);
        const int y = static_cast<int>(idx / WORLD_WIDTH);

        const float elevation = generate_continent_map(x, y, ctx.seed);
        
        const TilePosition tile_pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
        // Map [-inf, inf] to [0, 1] with land at > 0.5
        ctx.continent_map[tile_pos] = (elevation > 0.0f) ? 0.7f : 0.3f;
    }

    continent_index_ += count;
    completed_units_ += count;

    if (continent_index_ >= WORLD_SIZE) {
        phase_ = Phase::InitField;
        init_index_ = 0;
        status_text_ = "Preparing terrain...";
    }
}

void GenState::step_noise(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - noise_index_;
    const std::size_t count = std::min(kChunkSize, remaining);
    float* field = current_field(ctx);
    const std::uint32_t seed = ctx.seed + static_cast<std::uint32_t>(octave_ * 1013u);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = noise_index_ + i;
        const int x = static_cast<int>(idx % WORLD_WIDTH);
        const int y = static_cast<int>(idx / WORLD_WIDTH);
        field[idx] += noise2D(x, y, seed) * noise_amp_;
    }

    noise_index_ += count;
    completed_units_ += count;

    if (noise_index_ >= WORLD_SIZE) {
        phase_ = Phase::Diffuse;
        diffuse_index_ = 0;
        diffusion_step_ = 0;
        status_text_ = "Smoothing terrain...";
    }
}

void GenState::step_diffuse(GameContext& ctx) {
    float* target_buf = ctx.field.data();
    if (target_map_ == MapTarget::Temperature)
        target_buf = ctx.temperature.data();
    else if (target_map_ == MapTarget::Humidity)
        target_buf = ctx.humidity.data();
    const float* const in = field_primary_ ? target_buf : ctx.temp.data();
    float* out = field_primary_ ? ctx.temp.data() : target_buf;

    const std::size_t remaining = WORLD_SIZE - diffuse_index_;
    const std::size_t count = std::min(kChunkSize, remaining);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = diffuse_index_ + i;
        const int x = static_cast<int>(idx % WORLD_WIDTH);
        const int y = static_cast<int>(idx / WORLD_WIDTH);

        const int left = ((x - 1 + WORLD_WIDTH) % WORLD_WIDTH) + y * WORLD_WIDTH;
        const int right = ((x + 1) % WORLD_WIDTH) + y * WORLD_WIDTH;
        const int up = x + (((y - 1 + WORLD_WIDTH) % WORLD_WIDTH) * WORLD_WIDTH);
        const int down = x + (((y + 1) % WORLD_WIDTH) * WORLD_WIDTH);

        out[idx] =
            in[idx] + diffusion_ * (in[left] + in[right] + in[up] + in[down] - 4.0f * in[idx]);
    }

    diffuse_index_ += count;
    completed_units_ += count;

    if (diffuse_index_ >= WORLD_SIZE) {
        field_primary_ = !field_primary_;
        diffusion_step_ += 1;
        diffuse_index_ = 0;

        if (diffusion_step_ >= kDiffusionSteps) {
            noise_amp_ *= 0.5f;
            diffusion_ *= 0.5f;
            octave_ += 1;
            diffusion_step_ = 0;

            if (octave_ >= kOctaves) {
                if (!field_primary_) {
                    if (target_map_ == MapTarget::Elevation)
                        std::swap(ctx.field, ctx.temp);
                    else if (target_map_ == MapTarget::Temperature)
                        std::swap(ctx.temperature, ctx.temp);
                    else if (target_map_ == MapTarget::Humidity)
                        std::swap(ctx.humidity, ctx.temp);

                    field_primary_ = true;
                }
                phase_ = Phase::NormalizeMinMax;
                normalize_index_ = 0;
                min_value_ = std::numeric_limits<float>::max();
                max_value_ = std::numeric_limits<float>::lowest();
                status_text_ = "Normalizing heightmap...";
            } else {
                phase_ = Phase::NoiseInject;
                noise_index_ = 0;
                status_text_ = "Adding noise...";
            }
        }
    }
}

void GenState::step_normalize_minmax(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - normalize_index_;
    const std::size_t count = std::min(kChunkSize, remaining);
    const float* const field = current_field(ctx);

    for (std::size_t i = 0; i < count; ++i) {
        const float value = field[normalize_index_ + i];
        min_value_ = std::min(min_value_, value);
        max_value_ = std::max(max_value_, value);
    }

    normalize_index_ += count;
    completed_units_ += count;

    if (normalize_index_ >= WORLD_SIZE) {
        inv_range_ = 1.0f / (max_value_ - min_value_ + 1e-6f);
        phase_ = Phase::NormalizeApply;
        normalize_index_ = 0;
        status_text_ = "Applying normalization...";
    }
}

void GenState::step_normalize_apply(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - normalize_index_;
    const std::size_t count = std::min(kChunkSize, remaining);

    float* field = current_field(ctx);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = normalize_index_ + i;
        field[idx] = (field[idx] - min_value_) * inv_range_;
    }

    normalize_index_ += count;
    completed_units_ += count;

    if (normalize_index_ >= WORLD_SIZE) {
        const int next_target = static_cast<int>(target_map_) + 1;
        if (next_target < static_cast<int>(MapTarget::Count)) {
            target_map_ = static_cast<MapTarget>(next_target);
            phase_ = Phase::InitField;
            init_index_ = 0;
            noise_amp_ = kBaseNoise;
            diffusion_ = kBaseDiffusion;
            octave_ = 0;
            status_text_ = (target_map_ == MapTarget::Temperature) ? "Generating climate..."
                                                                   : "Calculating humidity...";
        } else {
            phase_ = Phase::TerrainMap;
            terrain_index_ = 0;
            status_text_ = "Building terrain map...";
        }
    }
}

void GenState::step_terrain_map(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - terrain_index_;
    const std::size_t count = std::min(kChunkSize, remaining);

    // Water threshold - higher value = more connected ocean basins
    constexpr float water_threshold = 0.42f;

    build_terrain_map_range(ctx, terrain_index_, count, water_threshold);

    terrain_index_ += count;
    completed_units_ += count;

    if (terrain_index_ >= WORLD_SIZE) {
        phase_ = Phase::UpdateTexture;
        status_text_ = "Updating textures...";
    }
}

void GenState::step_update_texture(GameContext& ctx) {
    // World texture update - TODO: implement with Sokol
    (void)ctx;
    completed_units_ += kTextureUnits;
    {
        resource::ResourceConfig rcfg;
        rcfg.seed_count = 60;
        rcfg.cluster_radius = 8;
        rcfg.sprinkle_fraction = 0.005;
        resource::generate_iron_map(ctx.relief.data(),
                                    ctx.resource_iron.data(),
                                    WORLD_SIZE,
                                    ctx.rng,
                                    rcfg);
        resource::generate_clay_map(ctx.relief.data(),
                                    ctx.resource_clay.data(),
                                    WORLD_SIZE,
                                    ctx.rng,
                                    rcfg);
    }
    phase_ = Phase::GenerateFlora;
    status_text_ = "Growing forests...";
}

void GenState::step_generate_flora(GameContext& ctx) {
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

void GenState::step_spread_flora(GameContext& ctx) {
    const std::size_t remaining = WORLD_SIZE - flora_index_;
    const std::size_t safe_chunk = 10000;
    const std::size_t count = std::min(safe_chunk, remaining);

    spread_forests_step(ctx, flora_index_, count);

    flora_index_ += count;
    completed_units_ += count;

    if (flora_index_ >= WORLD_SIZE) {
        flora_spread_step_++;
        flora_index_ = 0;

        std::println(stderr, "GEN: Spread flora pass {}/{} done", flora_spread_step_, kFloraSpreadSteps);

        if (flora_spread_step_ >= kFloraSpreadSteps) {
            phase_ = Phase::InitEntities;
            status_text_ = "Spawning entities...";
        }
    }
}

void GenState::step_init_entities() {
    completed_units_ += kPostUnits / 4;
    phase_ = Phase::SpawnTrees;
}

void GenState::step_spawn_trees(GameContext& ctx) {
    if (!ctx.ecs_world) {
        phase_ = Phase::InitPolitics;
        return;
    }

    int checker = 0;
    int attempts = 0;
    const int max_attempts = MAX_OBJECTS * 100;

    while (checker < MAX_OBJECTS && attempts < max_attempts) {
        attempts++;
        const auto drop_x = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const auto drop_y = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const TilePosition drop_tile{drop_x, drop_y};
        if (ctx.relief[drop_tile] == TerrainType::Grass
            || ctx.relief[drop_tile] == TerrainType::Dirt) {
            if (ctx.flora[drop_tile] > 50 || random_u32_inclusive(ctx.rng, 10) == 0) {
                ecs::spawn_tree(*ctx.ecs_world, drop_tile);
                checker++;
            }
        }
    }

    std::println(stderr, "GEN: Spawned {} trees to ECS after {} attempts", checker, attempts);

    completed_units_ += kPostUnits / 4;
    phase_ = Phase::InitPolitics;
    status_text_ = "Setting up factions...";
}

void GenState::step_init_politics(GameContext& ctx) {
    if (ctx.world_manager) {
        ctx.world_manager->politics.init(ctx.rng);
    }

    completed_units_ += kPostUnits / 4;
    phase_ = Phase::PlaceCapitals;
    status_text_ = "Placing faction capitals...";
}

void GenState::step_place_capitals(GameContext& ctx) {
    if (ctx.world_manager) {
        ctx.world_manager->place_faction_capitals(ctx, &ctx.world_manager->politics);
    }

    completed_units_ += kPostUnits / 4;
    phase_ = Phase::FillPoliticsMap;
    status_text_ = "Claiming territory...";
}

void GenState::step_fill_politics_map(GameContext& ctx) {
    if (ctx.world_manager) {
        ctx.world_manager->politics.fill_politics_map(ctx);
    }

    completed_units_ += kPostUnits / 4;
    phase_ = Phase::InitWorldManager;
    status_text_ = "Building settlements...";
}

void GenState::step_init_world_manager(GameContext& ctx) {
    if (ctx.world_manager) {
        ctx.world_manager->init();
        ctx.world_manager->place_faction_settlements(ctx,
                                                     SettlementType::City,
                                                     WorldManager::NUM_CITIES - 8);
        ctx.world_manager->place_faction_settlements(ctx,
                                                     SettlementType::Town,
                                                     WorldManager::NUM_TOWNS);
        ctx.world_manager->place_faction_settlements(ctx,
                                                     SettlementType::Village,
                                                     WorldManager::NUM_VILLAGES);
        ctx.world_manager->landmarks.propagate_all_fields(ctx.relief);

        ctx.world_manager->spawn_initial_npcs(ctx);
        ctx.world_manager->init_player(ctx);
        ctx.world_manager->rebuild_pos_map(ctx.pos_map);
    }

    completed_units_ += kPostUnits / 4;

    // Set random starting time and transition to play
    const int start_time = 10000 + static_cast<int>(random_u32_inclusive(ctx.rng, 6000)) - 3000;
    ctx.set_ticks(static_cast<std::uint64_t>(std::max(0, start_time)));

    phase_ = Phase::Done;
    status_text_ = "Starting...";
    replace_state(ctx, std::make_unique<PlayState>(), false);
}
