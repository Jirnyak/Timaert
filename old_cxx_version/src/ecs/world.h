#pragma once

#include <entt/entt.hpp>
#include "ecs/components/core.h"
#include "ecs/components/singletons.h"
#include "ecs/components/entity.h"
#include "ecs/components/npc.h"

namespace ecs {

class World {
public:
    entt::registry registry;

    void init(std::size_t expected_npcs = 5000, std::size_t expected_objects = 15000) {
        // Singletons
        registry.ctx().emplace<TimeOfDay>();
        registry.ctx().emplace<Camera>();
        registry.ctx().emplace<InputState>();
        registry.ctx().emplace<BattleContext>();

        // Reserve storage for expected entity counts
        registry.storage<Position>().reserve(expected_npcs + expected_objects);
        registry.storage<Active>().reserve(expected_npcs + expected_objects);
        registry.storage<PreviousPosition>().reserve(expected_npcs);
        registry.storage<VisualPos>().reserve(expected_npcs);
        registry.storage<NPCTag>().reserve(expected_npcs);
        registry.storage<AIBehavior>().reserve(expected_npcs);
        registry.storage<Speed>().reserve(expected_npcs);
        registry.storage<Health>().reserve(expected_npcs);
        registry.storage<FactionMember>().reserve(expected_npcs);
        registry.storage<SettlementLink>().reserve(expected_npcs * 3 / 5);
        registry.storage<CharacterInfo>().reserve(3000);
        registry.storage<CombatStats>().reserve(3000);
        registry.storage<SkillSet>().reserve(3000);
        registry.storage<InventoryComponent>().reserve(2000);
        registry.storage<WoodcutterWork>().reserve(500);
        registry.storage<PeasantTag>().reserve(1000);
        registry.storage<WoodcutterTag>().reserve(500);
        registry.storage<BanditTag>().reserve(500);
        registry.storage<CaravanTag>().reserve(500);
        registry.storage<MerchantTag>().reserve(500);
        registry.storage<GuardTag>().reserve(500);
        registry.storage<WitchTag>().reserve(200);
        registry.storage<ObjectSprite>().reserve(expected_objects);
        registry.storage<Dead>().reserve(1000);

        // Groups for cache-efficient iteration (owned components listed first)
        (void)registry.group<Position>(entt::get<Speed>);
        (void)registry.group<Health, FactionMember>(entt::get<Position, Active>);

        // Death signal: adding Dead automatically removes Active
        registry.on_construct<Dead>().connect<&World::on_entity_death>();
    }

    [[nodiscard]] TimeOfDay& time() {
        return registry.ctx().get<TimeOfDay>();
    }
    [[nodiscard]] const TimeOfDay& time() const {
        return registry.ctx().get<TimeOfDay>();
    }
    [[nodiscard]] Camera& camera() {
        return registry.ctx().get<Camera>();
    }
    [[nodiscard]] const Camera& camera() const {
        return registry.ctx().get<Camera>();
    }
    [[nodiscard]] InputState& input() {
        return registry.ctx().get<InputState>();
    }
    [[nodiscard]] const InputState& input() const {
        return registry.ctx().get<InputState>();
    }
    [[nodiscard]] BattleContext& battle() {
        return registry.ctx().get<BattleContext>();
    }
    [[nodiscard]] const BattleContext& battle() const {
        return registry.ctx().get<BattleContext>();
    }

    static void on_entity_death(entt::registry& reg, entt::entity entity) {
        reg.remove<Active>(entity);
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
        }
    }

    void cleanup_dead() {
        auto view = registry.view<Dead>();
        registry.destroy(view.begin(), view.end());
    }

    template <typename... Components>
    [[nodiscard]] auto view() const {
        return registry.view<Components...>();
    }

    [[nodiscard]] std::size_t active_count() const {
        return registry.view<Active>().size();
    }

    void clear_all_entities() {
        registry.clear();
        // Re-initialize singletons after clear
        registry.ctx().emplace<TimeOfDay>();
        registry.ctx().emplace<Camera>();
        registry.ctx().emplace<InputState>();
        registry.ctx().emplace<BattleContext>();
    }
};

}  // namespace ecs
