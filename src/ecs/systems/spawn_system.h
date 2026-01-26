#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/entity.h"
#include "ecs/components/player.h"

namespace ecs {

inline entt::entity spawn_tree(World& world, TilePosition pos) {
    auto entity = world.create_entity(pos);
    world.registry.emplace<ObjectSprite>(entity, ObjectType::Tree);
    return entity;
}

inline entt::entity spawn_npc(World& world, NPCType type, TilePosition pos,
                               std::int32_t home_idx, rng_t& rng) {
    auto entity = world.create_entity(pos);
    auto& registry = world.registry;
    
    registry.emplace<NPCTag>(entity, type);
    registry.emplace<PreviousPosition>(entity, pos);
    registry.emplace<VisualPos>(entity, 
        static_cast<float>(pos.x), static_cast<float>(pos.y));
    registry.emplace<AIBehavior>(entity);
    registry.emplace<SettlementLink>(entity, home_idx, -1);
    
    switch (type) {
        case NPCType::Peasant:
            registry.emplace<PeasantTag>(entity);
            registry.emplace<Speed>(entity, 0.5 + random_u32_inclusive(rng, 50) / 100.0);
            registry.emplace<Health>(entity, 
                50 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)),
                50 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)));
            registry.emplace<FactionMember>(entity, FactionID::Faction1);
            break;
            
        case NPCType::Woodcutter:
            registry.emplace<WoodcutterTag>(entity);
            registry.emplace<WoodcutterWork>(entity);
            registry.emplace<Speed>(entity, 0.5 + random_u32_inclusive(rng, 40) / 100.0);
            registry.emplace<Health>(entity,
                60 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40)),
                60 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40)));
            registry.emplace<FactionMember>(entity, FactionID::Faction1);
            registry.emplace<InventoryComponent>(entity);
            break;
            
        case NPCType::Merchant:
            registry.emplace<MerchantTag>(entity);
            registry.emplace<Speed>(entity, 0.8 + random_u32_inclusive(rng, 40) / 100.0);
            registry.emplace<Health>(entity,
                80 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40)),
                80 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40)));
            registry.emplace<FactionMember>(entity, FactionID::Faction1);
            {
                auto& inv = registry.emplace<InventoryComponent>(entity);
                inv.data.set_capital(500.0 + random_u32_inclusive(rng, 500));
            }
            break;
            
        case NPCType::Caravan:
            registry.emplace<CaravanTag>(entity);
            registry.emplace<Speed>(entity, 0.6 + random_u32_inclusive(rng, 30) / 100.0);
            registry.emplace<Health>(entity,
                200 + static_cast<std::int32_t>(random_u32_inclusive(rng, 100)),
                200 + static_cast<std::int32_t>(random_u32_inclusive(rng, 100)));
            registry.emplace<FactionMember>(entity, FactionID::Faction1);
            {
                auto& inv = registry.emplace<InventoryComponent>(entity);
                inv.data.set_capital(2000.0 + random_u32_inclusive(rng, 3000));
            }
            break;
            
        case NPCType::Bandit:
            registry.emplace<BanditTag>(entity);
            registry.emplace<Speed>(entity, 1.0 + random_u32_inclusive(rng, 50) / 100.0);
            registry.emplace<Health>(entity,
                100 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)),
                100 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)));
            registry.emplace<FactionMember>(entity, FactionID::Faction2);
            {
                auto& skills = registry.emplace<SkillSet>(entity);
                skills.add(SkillID::Punch);
                skills.add(SkillID::Kick);
                skills.add(SkillID::DirtyBlow);
            }
            break;
            
        case NPCType::Guard:
            registry.emplace<GuardTag>(entity);
            registry.emplace<Speed>(entity, 0.7);
            registry.emplace<Health>(entity,
                150 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)),
                150 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50)));
            registry.emplace<FactionMember>(entity, FactionID::Faction1);
            {
                auto& skills = registry.emplace<SkillSet>(entity);
                skills.add(SkillID::Bash);
                skills.add(SkillID::ShieldBash);
            }
            break;
            
        case NPCType::Witch:
            registry.emplace<WitchTag>(entity);
            registry.emplace<Speed>(entity, 0.8);
            registry.emplace<Health>(entity, 80, 80);
            registry.emplace<FactionMember>(entity, FactionID::Neutral);
            registry.emplace<SpecialNPC>(entity);
            break;
            
        default:
            break;
    }
    
    if (random_u32_inclusive(rng, 100) > 95) {
        if (!registry.all_of<SpecialNPC>(entity)) {
            registry.emplace<SpecialNPC>(entity);
        }
    }
    
    auto& info = registry.emplace<CharacterInfo>(entity);
    static const char* syl1[] = {"Bel", "Gar", "Mar", "Kael", "Jor", "Zan", "Thor", "Ray"};
    static const char* syl2[] = {"dor", "van", "ius", "eth", "lin", "morn", "tor", "gan"};
    char name_buf[32];
    std::snprintf(name_buf, sizeof(name_buf), "%s%s", 
                  syl1[random_u32_inclusive(rng, 7)], 
                  syl2[random_u32_inclusive(rng, 7)]);
    info.set_name(name_buf);
    
    static const char* traits[] = {"Aggressive", "Calm", "Arrogant", "Fearful", "Merciless", "Flirty"};
    info.set_personality(traits[random_u32_inclusive(rng, 5)]);
    info.gender = static_cast<Gender>(random_u32_inclusive(rng, static_cast<std::uint32_t>(Gender::Count) - 1));
    info.race = Race::Human;
    
    return entity;
}

inline entt::entity spawn_player(World& world, TilePosition pos, rng_t& rng) {
    auto entity = world.create_entity(pos);
    auto& registry = world.registry;
    
    registry.emplace<PlayerTag>(entity);
    registry.emplace<PreviousPosition>(entity, pos);
    registry.emplace<VisualPos>(entity, 
        static_cast<float>(pos.x), static_cast<float>(pos.y));
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
    
    auto& combat = registry.emplace<CombatStats>(entity);
    combat.lust = 0;
    combat.max_lust = 100;
    combat.will = 100;
    combat.max_will = 100;
    
    (void)rng;
    
    return entity;
}

} // namespace ecs
