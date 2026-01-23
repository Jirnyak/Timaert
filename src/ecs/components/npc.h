#pragma once

#include "core/types.h"
#include "core/tile_map.h"
#include "systems/economy.h"
#include "systems/skills.h"
#include <cstdint>
#include <cstring>

namespace ecs {

struct NPCTag {
    NPCType type = NPCType::None;
};

struct PeasantTag {};
struct WoodcutterTag {};
struct MerchantTag {};
struct CaravanTag {};
struct BanditTag {};
struct GuardTag {};
struct WitchTag {};

struct AIBehavior {
    NPCState state = NPCState::Idle;
    std::int32_t idle_timer = 0;
    std::int32_t action_timer = 0;
};

struct SettlementLink {
    std::int32_t home_idx = -1;
    std::int32_t target_idx = -1;
    
    [[nodiscard]] bool has_home() const noexcept { return home_idx >= 0; }
    [[nodiscard]] bool has_target() const noexcept { return target_idx >= 0; }
};

struct WoodcutterWork {
    TilePosition target_tree = INVALID_POS;
};

struct CharacterInfo {
    char name[32]{};
    char personality[32]{};
    Gender gender = Gender::Male;
    Race race = Race::Human;
    
    void set_name(const char* n) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    
    void set_personality(const char* p) {
        std::strncpy(personality, p, sizeof(personality) - 1);
        personality[sizeof(personality) - 1] = '\0';
    }
};

struct CombatStats {
    std::int32_t lust = 0;
    std::int32_t max_lust = 100;
    std::int32_t will = 100;
    std::int32_t max_will = 100;
};

struct SkillSet {
    static constexpr std::size_t MAX_SKILLS = 8;
    SkillID skills[MAX_SKILLS]{};
    std::uint8_t count = 0;
    
    void add(SkillID id) {
        if (count < MAX_SKILLS) {
            skills[count++] = id;
        }
    }
    
    void clear() {
        count = 0;
        for (auto& s : skills) s = SkillID::Wait;
    }
};

struct SpecialNPC {};

struct InventoryComponent {
    Inventory data;
};

static_assert(sizeof(AIBehavior) <= 16, "AIBehavior too large");
static_assert(sizeof(SettlementLink) <= 8, "SettlementLink too large");
static_assert(sizeof(CombatStats) <= 16, "CombatStats too large");

} // namespace ecs
