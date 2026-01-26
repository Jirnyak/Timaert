#pragma once

#include "core/game_context.h"
#include "core/tile_map.h"
#include "systems/landmark.h"
#include "systems/player.h"
#include "systems/politics.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/movement_system.h"
#include <cstdint>
#include <istream>
#include <memory>
#include <string>

class WorldManager {
public:
    LandmarkSystem landmarks;

    PlayerController player_ctrl;
    politics::PoliticsSystem politics;  // <--- Добавлено: система теперь часть мира

    // ECS spatial hash for combat resolution
    ecs::SpatialHash ecs_spatial_hash;

    static constexpr int NUM_CITIES = 5;
    static constexpr int NUM_TOWNS = 15;
    static constexpr int NUM_VILLAGES = 30;
    static constexpr int MIN_SETTLEMENT_DISTANCE = 50;

    WorldManager() = default;

    void save(std::ostream& out) const;
    void load(std::istream& in, GameContext& ctx);
    void init();
    void generate_settlements(GameContext& ctx, politics::PoliticsSystem* politics_sys = nullptr);

    void place_faction_capitals(GameContext& ctx, politics::PoliticsSystem* politics_sys) noexcept;
    void place_faction_settlements(GameContext& ctx, SettlementType type, int count);
    [[nodiscard]] static MapPixel get_settlement_color(SettlementType type) noexcept;
    [[nodiscard]] static std::string generate_settlement_name(rng_t& rng, SettlementType type);

    void spawn_initial_npcs(GameContext& ctx);
    void init_player(GameContext& ctx);
    void update(GameContext& ctx);

    void update_visual_interpolation(GameContext& ctx, float delta_time) {
        if (ctx.ecs_world) {
            ecs::update_visual_interpolation(*ctx.ecs_world, delta_time);
        }
    }

    void spawn_from_settlements(GameContext& ctx);
    void rebuild_pos_map(WorldMap<std::uint16_t>& pos_map);

    [[nodiscard]] bool is_settlement_at(TilePosition pos) const {
        return landmarks.find_settlement_at(pos) != nullptr;
    }

    [[nodiscard]] const Settlement* get_settlement_at(TilePosition pos) const {
        return landmarks.find_settlement_at(pos);
    }

    [[nodiscard]] Settlement* get_settlement_at(TilePosition pos) {
        return landmarks.find_settlement_at(pos);
    }
};
