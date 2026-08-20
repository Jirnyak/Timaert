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
    // ── Spatial / perception, shared by every fighter table ────────────────
    // Both authoring tables embed a CombatTemplate (NpcTypeDef::combat and
    // FaunaEntry::combat), so these two columns make body size and eyesight
    // DATA for humanoids and monsters alike: a dragon is wide and sees far
    // because of its row, not because of a branch in the engine.
    //
    // bodyRadius — physical half-width in world units (1 unit ≈ 1 m). Drives
    // separation, the engagement ring and hit reach. A creature row may leave
    // this at 0 and let FaunaEntry::radius (which also scales its sprite, so
    // visual size and body size cannot drift) speak instead.
    float       bodyRadius = 0.55f;
    // bodyHeight — how tall this thing is, in metres, for the eye. Shared by
    // both authoring tables exactly like bodyRadius above (owner's ruling,
    // 2026-08-06: ONE column for humanoids and monsters alike), so a dragon
    // towers because of its row and not because of a branch in the renderer.
    // 0 = not stated; sub/body.h then derives it — a humanoid is a person, a
    // creature is as tall as the proportion the renderer used to hardcode.
    float       bodyHeight = 0.0f;
    // sight — how far this fighter notices an enemy on its own. It is NOT an
    // aggro leash: awareness relays through a formation (see the alert chain in
    // sub/battle.h), so a rear rank charges because its front rank saw, while a
    // lone animal that noticed nothing stays put.
    float       sight = 200.0f;
};

struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle
    // WHAT this member is, in the ONE id space every body already shares with
    // the ECS (`ecs::NPCKind.type`): a humanoid is its `NPCType` ordinal, a
    // monster is `0x100 | catalog index` (macro/fauna.h). Sixteen bits, because
    // a beast must be able to stand in a roster exactly like a man — a wolf
    // pack IS a squad, and the byte this used to be could not say so
    // (CANON.md S4/S16). Validated by npc.h `valid_npc_kind`.
    std::uint16_t kind     = 0;
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

inline SoldierRecord make_soldier(std::uint16_t kind, int level,
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

inline int count_soldiers_of_kind(const SoldierSquad& squad, std::uint16_t kind) {
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

// The record-list form exists because a roster does not always wear the
// SoldierSquad wrapper (ecs::SquadRoster holds the bare vector); the wrapper
// form delegates so there is ONE removal, not a dialect per holder.
inline bool remove_one_soldier_by_entity_id(std::vector<SoldierRecord>& members,
                                            std::uint32_t entityId) {
    auto it = std::find_if(members.begin(), members.end(),
        [entityId](const SoldierRecord& s) {
            return s.entityId == entityId;
        });
    if (it == members.end()) return false;
    members.erase(it);
    return true;
}

inline bool remove_one_soldier_by_entity_id(SoldierSquad& squad,
                                            std::uint32_t entityId) {
    return remove_one_soldier_by_entity_id(squad.members, entityId);
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

// Record-list form for the same reason as removal above: one append, whoever
// holds the vector (SoldierSquad, ecs::SquadRoster, a battle outcome list).
inline void add_soldiers(SoldierSquad& target,
                         const std::vector<SoldierRecord>& src) {
    if (src.empty()) return;
    if (&target.members == &src) {
        const std::size_t n = target.members.size();
        reserve_soldiers_for_append(target, n);
        for (std::size_t i = 0; i < n; ++i) {
            target.members.push_back(target.members[i]);
        }
        return;
    }
    reserve_soldiers_for_append(target, src.size());
    target.members.insert(target.members.end(), src.begin(), src.end());
}

inline void add_squad(SoldierSquad& target, const SoldierSquad& src) {
    add_soldiers(target, src.members);
}

} // namespace sm
