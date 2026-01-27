#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/npc.h"
#include "core/game_context.h"
#include <unordered_map>

namespace ecs {

// Design: Instead of rebuilding every frame, support incremental updates via signals

class SpatialHash {
public:
    void clear() {
        cells_.clear();
        entity_positions_.clear();
    }

    void insert(entt::entity entity, TilePosition pos) {
        if (is_valid(pos)) {
            cells_[pos_to_key(pos)].push_back(entity);
            entity_positions_[entity] = pos;
        }
    }

    void remove(entt::entity entity) {
        auto it = entity_positions_.find(entity);
        if (it == entity_positions_.end())
            return;

        TilePosition const old_pos = it->second;
        auto key = pos_to_key(old_pos);
        auto cell_it = cells_.find(key);
        if (cell_it != cells_.end()) {
            auto& vec = cell_it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
            if (vec.empty()) {
                cells_.erase(cell_it);
            }
        }
        entity_positions_.erase(it);
    }

    void update(entt::entity entity, TilePosition new_pos) {
        remove(entity);
        insert(entity, new_pos);
    }

    [[nodiscard]] bool contains(entt::entity entity) const {
        return entity_positions_.contains(entity);
    }

    [[nodiscard]] const std::vector<entt::entity>* at(TilePosition pos) const {
        auto it = cells_.find(pos_to_key(pos));
        return it != cells_.end() ? &it->second : nullptr;
    }

private:
    static std::uint32_t pos_to_key(TilePosition p) {
        return (static_cast<std::uint32_t>(p.y) << 16) | p.x;
    }

    std::unordered_map<std::uint32_t, std::vector<entt::entity>> cells_;

    std::unordered_map<entt::entity, TilePosition> entity_positions_;
};

inline void build_spatial_hash(World& world, SpatialHash& hash) {
    hash.clear();

    auto group = world.registry.group<Health, FactionMember>(entt::get<Position, Active>);

    // Use explicit for-loop for clarity (group.each doesn't pass entity as first param)
    for (auto entity : group) {
        auto [health, faction, pos] = group.get<Health, FactionMember, Position>(entity);
        if (health.is_alive()) {
            hash.insert(entity, pos.tile);
        }
    }
}

inline void resolve_combat(World& world, const SpatialHash& hash, rng_t& rng) {
    auto group = world.registry.group<Health, FactionMember>(entt::get<Position, Active>);

    for (auto entity : group) {
        auto [health, faction, pos] = group.get<Health, FactionMember, Position>(entity);
        if (!health.is_alive())
            continue;

        const auto* neighbors = hash.at(pos.tile);
        if (!neighbors || neighbors->size() <= 1)
            continue;

        for (entt::entity const other : *neighbors) {
            if (other == entity)
                continue;
            if (!world.registry.valid(other))
                continue;

            auto* other_faction = world.registry.try_get<FactionMember>(other);
            auto* other_health = world.registry.try_get<Health>(other);

            if (!other_faction || !other_health || !other_health->is_alive())
                continue;

            bool const hostile = is_hostile(faction.faction, other_faction->faction);
            if (!hostile)
                continue;

            int dmg = 5 + static_cast<int>(random_u32_inclusive(rng, 9));

            if (world.registry.any_of<GuardTag, BanditTag>(entity)) {
                dmg += 10;
            }

            other_health->current -= dmg;

            if (!other_health->is_alive()) {
                world.mark_dead(other);
            }
        }
    }
}

inline void rebuild_pos_map(World& world, WorldMap<std::uint16_t>& pos_map) {
    pos_map.fill(0);

    auto view = world.registry.view<Position, Active>();
    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        if (is_valid(pos.tile) && pos_map[pos.tile] < 65535) {
            pos_map[pos.tile] += 1;
        }
    }
}

}  // namespace ecs
