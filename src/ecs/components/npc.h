#pragma once

#include "core/types.h"
#include "core/tile_map.h"
#include "systems/economy.h"
#include "systems/skills.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <cassert>

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

    [[nodiscard]] bool has_home() const noexcept {
        return home_idx >= 0;
    }
    [[nodiscard]] bool has_target() const noexcept {
        return target_idx >= 0;
    }
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
        for (auto& s : skills)
            s = SkillID::Wait;
    }
};

struct SpecialNPC {};

struct InventoryComponent {
    Inventory data;
};

static_assert(sizeof(AIBehavior) <= 16, "AIBehavior too large");
static_assert(sizeof(SettlementLink) <= 8, "SettlementLink too large");
static_assert(sizeof(CombatStats) <= 16, "CombatStats too large");
static_assert(sizeof(CharacterInfo) <= 72, "CharacterInfo too large (32+32+1+1+padding)");
static_assert(sizeof(SkillSet) <= 40, "SkillSet too large (MAX_SKILLS * sizeof(SkillID) + count)");
static_assert(sizeof(WoodcutterWork) <= 8, "WoodcutterWork too large");
static_assert(sizeof(NPCTag) <= 4, "NPCTag too large");

// InventoryComponent wraps Inventory which is LARGE (~3KB):
//   - std::array<std::uint16_t, 512> items = 1024 bytes
//   - std::array<ItemType, 512> item_types = 2048 bytes (assuming 4-byte enum)
//   Total: ~3072 bytes (exceeds 64 byte cache line target)
//
// This is acceptable because:
// 1. Not all entities have InventoryComponent (only merchants, caravans, woodcutters)
// 2. Inventory access is infrequent (only during trade/resource operations)
// 3. Alternative approaches (InventoryRef with separate storage) add complexity
//
// If inventory iteration becomes a bottleneck, consider:
// - Store inventories in separate container, use InventoryRef { uint32_t id; }
// - Split into InventoryHot (capital, item_count) and InventoryCold (full data)
static_assert(sizeof(InventoryComponent) > 64, "InventoryComponent is large - see comments above");

// EnTT optimizes empty types (no data members) to use zero storage
// Verify marker-only tags have no data members
static_assert(std::is_empty_v<PeasantTag>, "PeasantTag should be empty tag");
static_assert(std::is_empty_v<WoodcutterTag>, "WoodcutterTag should be empty tag");
static_assert(std::is_empty_v<MerchantTag>, "MerchantTag should be empty tag");
static_assert(std::is_empty_v<CaravanTag>, "CaravanTag should be empty tag");
static_assert(std::is_empty_v<BanditTag>, "BanditTag should be empty tag");
static_assert(std::is_empty_v<GuardTag>, "GuardTag should be empty tag");
static_assert(std::is_empty_v<WitchTag>, "WitchTag should be empty tag");
static_assert(std::is_empty_v<SpecialNPC>, "SpecialNPC should be empty tag");

// Note: NPCTag has data (NPCType type), so it's NOT empty - this is intentional
// The marker tags (PeasantTag, etc.) are for view filtering, NPCTag.type stores the enum
static_assert(!std::is_empty_v<NPCTag>, "NPCTag should have type data");

// Use this to verify tag consistency in spawn functions
#ifdef NDEBUG
template <typename Registry, typename Entity>
inline void ECS_VERIFY_NPC_TAG_CONSISTENCY(const Registry&, Entity) noexcept {
}
#else
template <typename Registry, typename Entity>
inline void ECS_VERIFY_NPC_TAG_CONSISTENCY(const Registry& registry, Entity entity) {
    if (registry.template all_of<NPCTag>(entity)) {
        const auto& tag = registry.template get<NPCTag>(entity);
        assert((tag.type == NPCType::Peasant) == registry.template all_of<PeasantTag>(entity));
        assert((tag.type == NPCType::Woodcutter)
               == registry.template all_of<WoodcutterTag>(entity));
        assert((tag.type == NPCType::Merchant) == registry.template all_of<MerchantTag>(entity));
        assert((tag.type == NPCType::Caravan) == registry.template all_of<CaravanTag>(entity));
        assert((tag.type == NPCType::Bandit) == registry.template all_of<BanditTag>(entity));
        assert((tag.type == NPCType::Guard) == registry.template all_of<GuardTag>(entity));
        assert((tag.type == NPCType::Witch) == registry.template all_of<WitchTag>(entity));
    }
}
#endif

}  // namespace ecs
