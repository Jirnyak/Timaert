#pragma once

#include <entt/entt.hpp>
#include "ecs/components/core.h"
#include "ecs/components/npc.h"

namespace ecs {

// Use patch() and replace() instead of get() + modify for signal support

// patch() - Modify component in-place, triggers on_update signal
// Use when: You want to modify a component AND trigger update signals
template <typename Component, typename Func>
void patch_component(entt::registry& registry, entt::entity entity, Func&& func) {
    registry.patch<Component>(entity, std::forward<Func>(func));
}

// replace() - Replace component entirely, triggers on_update signal
// Use when: You want to completely replace a component's data
template <typename Component>
void replace_component(entt::registry& registry, entt::entity entity, Component&& component) {
    registry.replace<Component>(entity, std::forward<Component>(component));
}

// emplace_or_replace() - Add if missing, replace if exists
// Use when: You're not sure if the component exists
template <typename Component, typename... Args>
Component& ensure_component(entt::registry& registry, entt::entity entity, Args&&... args) {
    return registry.emplace_or_replace<Component>(entity, std::forward<Args>(args)...);
}

// Use these instead of directly modifying Health component

inline void damage_entity(entt::registry& registry, entt::entity entity, int amount) {
    if (!registry.valid(entity))
        return;
    auto* health = registry.try_get<Health>(entity);
    if (!health)
        return;

    // Use patch to trigger on_update signal if connected
    registry.patch<Health>(entity,
                           [amount](Health& h) { h.current = std::max(0, h.current - amount); });
}

inline void heal_entity(entt::registry& registry, entt::entity entity, int amount) {
    if (!registry.valid(entity))
        return;
    auto* health = registry.try_get<Health>(entity);
    if (!health)
        return;

    registry.patch<Health>(entity, [amount](Health& h) {
        h.current = std::min(h.max, h.current + amount);
    });
}

// Use this for position changes that should trigger signals

inline void move_entity(entt::registry& registry, entt::entity entity, TilePosition new_pos) {
    if (!registry.valid(entity))
        return;

    // Update previous position first
    auto* prev = registry.try_get<PreviousPosition>(entity);
    auto* pos = registry.try_get<Position>(entity);

    if (prev && pos) {
        prev->tile = pos->tile;
    }

    if (pos) {
        // Use patch to trigger on_update signal if connected
        registry.patch<Position>(entity, [new_pos](Position& p) { p.tile = new_pos; });
    }
}

inline void modify_will(entt::registry& registry, entt::entity entity, int delta) {
    if (!registry.valid(entity))
        return;
    auto* stats = registry.try_get<CombatStats>(entity);
    if (!stats)
        return;

    registry.patch<CombatStats>(entity,
                                [delta](CombatStats& s) { s.will = std::max(0, s.will + delta); });
}

inline void modify_lust(entt::registry& registry, entt::entity entity, int delta) {
    if (!registry.valid(entity))
        return;
    auto* stats = registry.try_get<CombatStats>(entity);
    if (!stats)
        return;

    registry.patch<CombatStats>(entity,
                                [delta](CombatStats& s) { s.lust = std::max(0, s.lust + delta); });
}

}  // namespace ecs
