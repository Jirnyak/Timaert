#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/entity.h"
#include "ecs/components/player.h"
#include "systems/character_templates.h"

namespace ecs {

inline entt::entity spawn_tree(World& world, TilePosition pos) {
    auto entity = world.create_entity(pos);
    world.registry.emplace<ObjectSprite>(entity, ObjectType::Tree);
    return entity;
}

inline entt::entity
spawn_npc(World& world, NPCType type, TilePosition pos, std::int32_t home_idx, rng_t& rng) {
    auto entity = world.create_entity(pos);
    auto& registry = world.registry;

    registry.emplace<NPCTag>(entity, type);
    registry.emplace<PreviousPosition>(entity, pos);
    registry.emplace<VisualPos>(entity, static_cast<float>(pos.x), static_cast<float>(pos.y));
    registry.emplace<AIBehavior>(entity);
    registry.emplace<SettlementLink>(entity, home_idx, -1);

    // ========================================================================
    // RPG MECHANICS: Get character template and generate attributes
    // ========================================================================
    const CharacterTemplate* tmpl = get_character_template(type);
    
    Attributes attrs{};
    LevelData level_data{};
    
    if (tmpl) {
        // Use template to generate level-based attributes
        distribute_attributes_from_template(attrs, level_data, *tmpl, rng);
        
        // Set faction from template
        registry.emplace<FactionMember>(entity, tmpl->faction);
        
        // Calculate movement speed from SPD attribute (0.5 base + 0.05 per SPD point)
        const double movement_speed = 0.5 + (attrs.spd * 0.05);
        registry.emplace<Speed>(entity, std::max(0.1, movement_speed));
        
        // Calculate HP from attributes and base values
        const std::int32_t max_hp = CombatStats::calc_max_hp(tmpl->base_hp, attrs);
        
        // Create Health component (HP)
        registry.emplace<Health>(entity, max_hp, max_hp);
        
        // Store attributes and level
        auto& npc_attrs = registry.emplace<Attributes>(entity);
        npc_attrs = attrs;
        
        auto& npc_level = registry.emplace<LevelData>(entity);
        npc_level = level_data;
        
        // Add derived bonuses
        auto& bonuses = registry.emplace<DerivedBonuses>(entity);
        bonuses.recalculate(attrs);
        
        // Add skills from template
        if (tmpl->skill_count > 0) {
            auto& skills = registry.emplace<SkillSet>(entity);
            for (std::uint8_t i = 0; i < tmpl->skill_count && i < SkillSet::MAX_SKILLS; ++i) {
                skills.add(tmpl->skills[i]);
            }
        }
    } else {
        // Fallback: old hardcoded values if no template found
        registry.emplace<Speed>(entity, 0.5 + random_u32_inclusive(rng, 50) / 100.0);
        registry.emplace<Health>(entity, 50, 50);
        registry.emplace<FactionMember>(entity, FactionID::Faction1);
    }

    // Type-specific setup (for special behaviors and inventories)
    switch (type) {
        case NPCType::Peasant:
            registry.emplace<PeasantTag>(entity);
            break;

        case NPCType::Woodcutter:
            registry.emplace<WoodcutterTag>(entity);
            registry.emplace<WoodcutterWork>(entity);
            registry.emplace<InventoryComponent>(entity);
            break;

        case NPCType::Merchant:
            registry.emplace<MerchantTag>(entity);
            {
                auto& inv = registry.emplace<InventoryComponent>(entity);
                inv.data.set_capital(500.0 + random_u32_inclusive(rng, 500));
            }
            break;

        case NPCType::Caravan:
            registry.emplace<CaravanTag>(entity);
            {
                auto& inv = registry.emplace<InventoryComponent>(entity);
                inv.data.set_capital(2000.0 + random_u32_inclusive(rng, 3000));
            }
            break;

        case NPCType::Bandit:
            registry.emplace<BanditTag>(entity);
            break;

        case NPCType::Guard:
            registry.emplace<GuardTag>(entity);
            break;

        case NPCType::Witch:
            registry.emplace<WitchTag>(entity);
            registry.emplace<SpecialNPC>(entity);
            {
                auto& witch_behavior = registry.emplace<WitchBehavior>(entity);
                witch_behavior.teleport_cooldown = static_cast<std::int32_t>(
                    random_u32_inclusive(
                        rng,
                        WitchBehavior::kMaxTeleportCooldown - WitchBehavior::kMinTeleportCooldown)
                    + WitchBehavior::kMinTeleportCooldown);
            }
            break;

        case NPCType::Sorceress:
            registry.emplace<SorceressTag>(entity);
            registry.emplace<SpecialNPC>(entity);
            break;

        default:
            break;
    }

    // Random chance for SpecialNPC tag
    if (random_u32_inclusive(rng, 100) > 95) {
        if (!registry.all_of<SpecialNPC>(entity)) {
            registry.emplace<SpecialNPC>(entity);
        }
    }

    // Character info (name, personality, gender, race)
    auto& info = registry.emplace<CharacterInfo>(entity);
    static const char* const syl1[] = {"Bel", "Gar", "Mar", "Kael", "Jor", "Zan", "Thor", "Ray"};
    static const char* const syl2[] = {"dor", "van", "ius", "eth", "lin", "morn", "tor", "gan"};
    char name_buf[32];
    std::snprintf(name_buf,
                  sizeof(name_buf),
                  "%s%s",
                  syl1[random_u32_inclusive(rng, 7)],
                  syl2[random_u32_inclusive(rng, 7)]);
    info.set_name(name_buf);

    static const char* const traits[] =
        {"Aggressive", "Calm", "Arrogant", "Fearful", "Merciless", "Flirty"};
    info.set_personality(traits[random_u32_inclusive(rng, 5)]);
    info.gender = static_cast<Gender>(
        random_u32_inclusive(rng, static_cast<std::uint32_t>(Gender::Count) - 1));
    info.race = Race::Human;

    return entity;
}

inline entt::entity spawn_player(World& world, TilePosition pos, rng_t& rng) {
    auto entity = world.create_entity(pos);
    auto& registry = world.registry;

    registry.emplace<PlayerTag>(entity);
    registry.emplace<PreviousPosition>(entity, pos);
    registry.emplace<VisualPos>(entity, static_cast<float>(pos.x), static_cast<float>(pos.y));
    registry.emplace<Speed>(entity, 1.0);
    registry.emplace<Health>(entity, 100, 100);
    registry.emplace<PlayerStateComponent>(entity);
    registry.emplace<PlayerAim>(entity);
    registry.emplace<Reputation>(entity);
    registry.emplace<InventoryComponent>(entity);
    registry.emplace<FactionMember>(entity, FactionID::Neutral);

    auto& skills = registry.emplace<SkillSet>(entity);
    skills.add(SkillID::Punch);
    skills.add(SkillID::Kick);
    skills.add(SkillID::Wait);

    (void)rng;

    return entity;
}

}  // namespace ecs
