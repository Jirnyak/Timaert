#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/npc.h"
#include <unordered_map>

namespace ecs {

class SpatialHash {
public:
    void clear() { cells_.clear(); }
    
    void insert(entt::entity entity, TilePosition pos) {
        if (is_valid(pos)) {
            cells_[pos_to_key(pos)].push_back(entity);
        }
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
};

inline void build_spatial_hash(World& world, SpatialHash& hash) {
    hash.clear();
    
    auto view = world.registry.view<Position, FactionMember, Health, Active>();
    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        const auto& health = view.get<Health>(entity);
        if (health.is_alive()) {
            hash.insert(entity, pos.tile);
        }
    }
}

inline void resolve_combat(World& world, const SpatialHash& hash, rng_t& rng) {
    auto view = world.registry.view<Position, FactionMember, Health, Active>();
    
    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        const auto& faction = view.get<FactionMember>(entity);
        auto& health = view.get<Health>(entity);
        if (!health.is_alive()) continue;
        
        const auto* neighbors = hash.at(pos.tile);
        if (!neighbors || neighbors->size() <= 1) continue;
        
        for (entt::entity other : *neighbors) {
            if (other == entity) continue;
            if (!world.registry.valid(other)) continue;
            
            auto* other_faction = world.registry.try_get<FactionMember>(other);
            auto* other_health = world.registry.try_get<Health>(other);
            
            if (!other_faction || !other_health || !other_health->is_alive()) continue;
            
            bool hostile = is_hostile(faction.faction, other_faction->faction);
            if (!hostile) continue;
            
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

} // namespace ecs
