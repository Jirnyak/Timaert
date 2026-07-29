// Universal combat + squad records.
//
// Soldiers are persistent NPC-kind records. Combat stats are read from
// the NPC registry's CombatTemplate; there is no separate unit schema.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm {

inline constexpr int kMaxSoldierLevel = 32767;

struct CombatTemplate {
    enum AttackKind : std::uint8_t { Melee = 0, Missile = 1 };
    float       hp;
    float       damage;
    float       speed;
    float       attackRange;
    float       cooldown;
    const char* label;
    AttackKind  attackKind = Melee;
    float       missileSpeed = 0.0f;
    float       missileBlast = 0.0f;
    std::uint32_t missileColorRGBA = 0xFFFFFFFFu;
};

struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle
    std::uint8_t  kind     = 0; // NPCType value; validated by npc.h helpers
    std::int16_t  level    = 1;
};

struct SoldierSquad {
    std::vector<SoldierRecord> members;
};

inline SoldierSquad default_squad() { return {}; }

inline int normalize_soldier_level(int level) {
    if (level < 1) return 1;
    if (level > kMaxSoldierLevel) return kMaxSoldierLevel;
    return level;
}

inline SoldierRecord make_soldier(std::uint8_t kind, int level,
                                  std::uint32_t entityId) {
    SoldierRecord s{};
    s.entityId = entityId;
    s.kind = kind;
    s.level = std::int16_t(normalize_soldier_level(level));
    return s;
}

inline int total_soldiers(const SoldierSquad& squad) {
    return static_cast<int>(squad.members.size());
}

inline int count_soldiers_of_kind(const SoldierSquad& squad, std::uint8_t kind) {
    int n = 0;
    for (const auto& s : squad.members) {
        if (s.kind == kind) ++n;
    }
    return n;
}

inline int count_soldiers_with_entity_id(const SoldierSquad& squad,
                                         std::uint32_t entityId) {
    int n = 0;
    for (const auto& s : squad.members) {
        if (s.entityId == entityId) ++n;
    }
    return n;
}

inline bool remove_one_soldier_by_entity_id(SoldierSquad& squad,
                                            std::uint32_t entityId) {
    auto it = std::find_if(squad.members.begin(), squad.members.end(),
        [entityId](const SoldierRecord& s) {
            return s.entityId == entityId;
        });
    if (it == squad.members.end()) return false;
    squad.members.erase(it);
    return true;
}

inline int soldier_level_factor(int level) {
    const int safeLevel = normalize_soldier_level(level);
    return 1 + (safeLevel - 1) / 3;
}

inline void reserve_soldiers_for_append(SoldierSquad& squad, std::size_t addCount) {
    if (addCount == 0u) return;
    const std::size_t required = squad.members.size() + addCount;
    if (squad.members.capacity() >= required) return;

    std::size_t next = squad.members.capacity();
    if (next < 4u) next = 4u;
    while (next < required) {
        const std::size_t grown = next * 2u;
        if (grown <= next) {
            next = required;
            break;
        }
        next = grown;
    }
    squad.members.reserve(next);
}

inline void add_squad(SoldierSquad& target, const SoldierSquad& src) {
    if (src.members.empty()) return;
    if (&target == &src) {
        const std::size_t n = target.members.size();
        reserve_soldiers_for_append(target, n);
        for (std::size_t i = 0; i < n; ++i) {
            target.members.push_back(target.members[i]);
        }
        return;
    }
    reserve_soldiers_for_append(target, src.members.size());
    target.members.insert(target.members.end(), src.members.begin(), src.members.end());
}

inline int drain_squad(SoldierSquad& squad) {
    const int total = total_soldiers(squad);
    squad.members.clear();
    return total;
}

} // namespace sm
