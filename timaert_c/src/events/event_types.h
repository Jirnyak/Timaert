// Event types — discriminated union via tag + payload-by-pointer indirection
// would explode this header. We use a flat tag enum + a generic struct that
// carries everything any event needs (entity ids, ints, floats, strings).
// Mirrors event-types.ts collapsed for C++.
#pragma once
#include <cstdint>
#include <string>

namespace sm {

enum class EventTag : std::uint16_t {
    PlayerMove = 0,
    PlayerLevelUp,
    PlayerDeath,
    NpcSpawn,
    NpcDeath,
    NpcGreeted,
    Encounter,
    SettlementVisit,
    SettlementChangeOwner,
    QuestAccepted,
    QuestObjectiveProgress,
    QuestCompleted,
    QuestFailed,
    QuestAbandoned,
    SpellCast,
    SpellLearned,
    Trade,
    LandmarkChangeOwner,
    WorldCellChange,
    TimeAdvance,
    PlayerGoldChange,    // ix = delta (signed)
    ApplyEffect,         // s1 = effectType, ix = value
    BattleStart,         // s1 = enemyName, s2 = enemyType, ix = enemyLevel
    CodexUnlock,         // s1 = entryId
    ReputationChange,    // s1 = factionId, ix = delta
    Custom,
};

struct GameEvent {
    EventTag    tag;
    std::uint32_t a = 0, b = 0; // entity / settlement / npc ids
    float        fx = 0, fy = 0;
    int          ix = 0, iy = 0;
    std::string  s1, s2;        // ids, action verbs
};

struct WorldHistoryEntry {
    std::uint32_t tick;
    int day, hour;
    GameEvent event;
};

} // namespace sm
