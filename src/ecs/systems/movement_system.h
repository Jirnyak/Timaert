#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"

namespace ecs {

inline void update_visual_interpolation(World& world, float dt) {
    auto view = world.registry.view<Position, VisualPos, Active>();

    constexpr float LERP_SPEED = 0.15f;

    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        auto& visual = view.get<VisualPos>(entity);
        if (!is_valid(pos.tile))
            continue;

        float target_x = static_cast<float>(pos.tile.x);
        float target_y = static_cast<float>(pos.tile.y);

        float dx = target_x - visual.x;
        if (dx > WORLD_WIDTH / 2.0f)
            dx -= WORLD_WIDTH;
        if (dx < -WORLD_WIDTH / 2.0f)
            dx += WORLD_WIDTH;

        float dy = target_y - visual.y;
        if (dy > WORLD_WIDTH / 2.0f)
            dy -= WORLD_WIDTH;
        if (dy < -WORLD_WIDTH / 2.0f)
            dy += WORLD_WIDTH;

        visual.x += dx * LERP_SPEED * dt;
        visual.y += dy * LERP_SPEED * dt;

        if (visual.x < 0)
            visual.x += WORLD_WIDTH;
        if (visual.x >= WORLD_WIDTH)
            visual.x -= WORLD_WIDTH;
        if (visual.y < 0)
            visual.y += WORLD_WIDTH;
        if (visual.y >= WORLD_WIDTH)
            visual.y -= WORLD_WIDTH;
    }
}

inline bool try_move(Position& pos,
                     PreviousPosition& prev,
                     Direction dir,
                     const WorldMap<TerrainType>& relief) {
    TilePosition next = neighbor_from_pos(pos.tile, dir);

    if (!is_valid(next))
        return false;

    TerrainType terrain = relief[next];
    if (terrain == TerrainType::Water || terrain == TerrainType::Mount) {
        return false;
    }

    prev.tile = pos.tile;
    pos.tile = next;
    return true;
}

}  // namespace ecs
