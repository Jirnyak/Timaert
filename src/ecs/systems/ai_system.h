#pragma once

#include "core/game_context.h"
#include "core/tile_map.h"
#include <cstdint>

class LandmarkSystem;
namespace ecs { class World; }

namespace ecs {

// View patterns used in this codebase (21 total across 10 files):
//
// HOT PATHS (converted to groups):
// - Position, Speed -> movement group (ai_system.h, all AI functions)
// - Health, FactionMember, Position, Active -> combat group (combat_system.h)
//
// REMAINING VIEWS (adequate performance, not worth converting):
// - Position, ObjectSprite, Active -> tree finding (find_nearest_tree, is_tree_at)
// - Position, Active -> general position queries (world.h, render_system.h)
// - Position, PlayerTag, Active -> player position (get_player_position)
// - Position, VisualPos, Active -> rendering interpolation (render_system.h)
// - Dead -> cleanup (world.h cleanup_dead)
// - WoodcutterWork, WoodcutterTag, Active -> woodcutter work state
//
// Views are cheap to create (~nanoseconds) per EnTT docs, so caching is not needed.

// Helper: get direction toward target position
[[nodiscard]] inline Direction get_direction_toward(TilePosition from, TilePosition to) {
    int dx = static_cast<int>(to.x) - static_cast<int>(from.x);
    int dy = static_cast<int>(to.y) - static_cast<int>(from.y);
    
    if (dx > WORLD_WIDTH / 2) dx -= WORLD_WIDTH;
    if (dx < -WORLD_WIDTH / 2) dx += WORLD_WIDTH;
    if (dy > WORLD_WIDTH / 2) dy -= WORLD_WIDTH;
    if (dy < -WORLD_WIDTH / 2) dy += WORLD_WIDTH;
    
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? Direction::Right : Direction::Left;
    } else {
        return dy > 0 ? Direction::Down : Direction::Up;
    }
}

void update_peasant_ai(World& world, const WorldMap<TerrainType>& relief, rng_t& rng);

[[nodiscard]] TilePosition find_nearest_tree(World& world, TilePosition from, entt::entity exclude_woodcutter);
[[nodiscard]] bool is_tree_at(World& world, TilePosition pos);
void remove_tree_at(World& world, TilePosition pos, WorldMap<std::uint8_t>& flora);
void update_woodcutter_ai(World& world, const WorldMap<TerrainType>& relief,
                          WorldMap<std::uint8_t>& flora,
                          LandmarkSystem& landmarks, rng_t& rng);
void update_bandit_ai(World& world, const WorldMap<TerrainType>& relief, 
                      TilePosition player_pos, rng_t& rng);
void update_guard_ai(World& world, const WorldMap<TerrainType>& relief,
                     LandmarkSystem& landmarks, rng_t& rng);
void update_caravan_ai(World& world, const WorldMap<TerrainType>& relief,
                       LandmarkSystem& landmarks, rng_t& rng);
void update_merchant_ai(World& world, const WorldMap<TerrainType>& relief,
                        LandmarkSystem& landmarks, rng_t& rng);
TilePosition get_player_position(World& world);
void update_witch_ai(World& world, const WorldMap<TerrainType>& relief, rng_t& rng);
void update_all_npc_ai(World& world, const WorldMap<TerrainType>& relief,
                       WorldMap<std::uint8_t>& flora,
                       LandmarkSystem& landmarks, rng_t& rng);

} // namespace ecs
