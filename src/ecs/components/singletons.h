#pragma once

#include "core/tile_map.h"
#include <cstdint>

namespace ecs {

struct TimeOfDay {
    std::uint64_t ticks = 0;
    int game_speed = 1;
    bool paused = false;
};

struct Camera {
    TilePosition center{0, 0};
    float zoom = 1.0f;
    float target_zoom = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    bool dragging = false;
};

struct InputState {
    int cursor_x = 0;
    int cursor_y = 0;
    int pick_x = 0;
    int pick_y = 0;
    bool picked = false;
};

struct BattleContext {
    std::int32_t target_id = -1;
    std::int32_t active_battle_id = -1;
    std::int32_t active_event_id = -1;
};

// Debug-only state that can be accessed by name for editor/debug tools
// Usage: registry.ctx().emplace_as<DebugState>("debug"_hs);
//        auto* debug = registry.ctx().find<DebugState>("debug"_hs);
struct DebugState {
    bool show_entity_ids = false;
    bool show_tile_coords = false;
    bool show_faction_colors = false;
    bool show_ai_state = false;
    bool show_spatial_hash = false;
    bool pause_ai = false;
    int selected_entity_idx = -1;
};

} // namespace ecs
