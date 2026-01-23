#pragma once

#include <entt/entt.hpp>
#include "ecs/components/core.h"
#include "ecs/components/singletons.h"

namespace ecs {

class World {
public:
    entt::registry registry;
    
    void init() {
        // Register singleton components with EnTT ctx<>
        registry.ctx().emplace<TimeOfDay>();
        registry.ctx().emplace<Camera>();
        registry.ctx().emplace<InputState>();
        registry.ctx().emplace<BattleContext>();
        
        // Pre-reserve component pools for expected entity counts
        registry.storage<Position>().reserve(20000);
        registry.storage<Active>().reserve(20000);
        registry.storage<Health>().reserve(5000);
        
        // Set up component groups for cache-efficient iteration (Phase 5)
        // These ensure commonly accessed components are stored contiguously
        (void)registry.group<Position>(entt::get<Speed>);
        (void)registry.group<Health, FactionMember>(entt::get<Position>);
    }
    
    [[nodiscard]] entt::entity create_entity(TilePosition pos) {
        auto entity = registry.create();
        registry.emplace<Position>(entity, pos);
        registry.emplace<Active>(entity);
        return entity;
    }
    
    void destroy_entity(entt::entity entity) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
    
    void mark_dead(entt::entity entity) {
        if (registry.valid(entity) && !registry.all_of<Dead>(entity)) {
            registry.emplace<Dead>(entity);
            registry.remove<Active>(entity);
        }
    }
    
    void cleanup_dead() {
        auto view = registry.view<Dead>();
        registry.destroy(view.begin(), view.end());
    }
    
    template<typename... Components>
    [[nodiscard]] auto view() {
        return registry.view<Components...>();
    }
    
    template<typename... Components>
    [[nodiscard]] auto view() const {
        return registry.view<Components...>();
    }
    
    [[nodiscard]] std::size_t active_count() const {
        return registry.view<Active>().size();
    }
};

} // namespace ecs
