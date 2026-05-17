// Event types: discriminated union via tag + payload-by-pointer indirection
// would explode this header. We use a flat tag enum + a generic struct that
// carries everything any event needs (entity ids, ints, floats, strings).
// Mirrors event-types.ts collapsed for C++.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sm
{

    enum class EventTag : std::uint16_t
    {
        PlayerMove = 0,
        PlayerLevelUp,
        PlayerDeath,
        NpcSpawn,
        NpcDeath,
        NpcGreeted,
        Encounter,
        SettlementVisit,
        SettlementChangeOwner,
        QuestStart = 9,     // s1 = quest id, a = stable quest key
        QuestUpdate = 10,   // s1 = quest id, a = stable quest key
        QuestComplete = 11, // s1 = quest id, a = stable quest key
        QuestFail = 12,     // s1 = quest id, a = stable quest key
        QuestAbandoned,
        SpellCast,
        SpellLearned,
        Trade,
        LandmarkChangeOwner,
        WorldCellChange,
        TimeAdvance,
        PlayerGoldChange, // ix = delta (signed)
        ApplyEffect,      // s1 = effectType, ix = value
        BattleStart,      // s1 = enemyName, s2 = enemyType, ix = enemyLevel
        CodexUnlock,      // s1 = entryId
        ReputationChange, // s1 = factionId, ix = delta
        ShowDialog,       // s1 = title, s2 = description, ix = choice count
        ShowStory,        // s1 = source node, s2 = story id, ix/iy/a/b = counts
        StoryResult,      // storyResult = choices keyed by phase id
        SpawnEntity,      // s1 = npc type id/name, ix/iy = x/y, a = level
        Custom,
        PlayerEnterSettlement, // s1 = settlement name, a/ix = settlement id
        PlayerLeaveSettlement, // s1 = settlement name, a/ix = settlement id
        NpcHpChange,           // a = npc id, ix = hp delta
        SettlementMoodChange,  // a/ix = settlement id, s1/s2 = old/new mood
        PlayerStatChange,      // s1 = stat id, ix/iy = old/new value
        BattleEnd,             // ix = victory, s1 = enemyName, a = lootGold
        MagicSurge,            // ix/iy = x/y, fx = intensity
        FactionRelationChange, // s1/s2 = faction ids, ix/iy = old/new relation
        DialogStart,           // s1 = dialog id, a = optional npc id
        CameraMove,            // fx/fy = camera center
        LastSerializable = CameraMove,

        // Compatibility aliases retained for existing native call sites/saves.
        QuestAccepted = 9,
        QuestObjectiveProgress = 10,
        QuestCompleted = 11,
        QuestFailed = 12,
    };

    static_assert(static_cast<std::uint16_t>(EventTag::QuestStart) == std::uint16_t{9});
    static_assert(static_cast<std::uint16_t>(EventTag::QuestUpdate) == std::uint16_t{10});
    static_assert(static_cast<std::uint16_t>(EventTag::QuestComplete) == std::uint16_t{11});
    static_assert(static_cast<std::uint16_t>(EventTag::QuestFail) == std::uint16_t{12});
    static_assert(EventTag::QuestAccepted == EventTag::QuestStart);
    static_assert(EventTag::QuestObjectiveProgress == EventTag::QuestUpdate);
    static_assert(EventTag::QuestCompleted == EventTag::QuestComplete);
    static_assert(EventTag::QuestFailed == EventTag::QuestFail);
    static_assert(static_cast<std::uint16_t>(EventTag::NpcHpChange) == std::uint16_t{32});
    static_assert(static_cast<std::uint16_t>(EventTag::SettlementMoodChange) == std::uint16_t{33});
    static_assert(static_cast<std::uint16_t>(EventTag::PlayerStatChange) == std::uint16_t{34});
    static_assert(static_cast<std::uint16_t>(EventTag::BattleEnd) == std::uint16_t{35});
    static_assert(static_cast<std::uint16_t>(EventTag::MagicSurge) == std::uint16_t{36});
    static_assert(static_cast<std::uint16_t>(EventTag::FactionRelationChange) == std::uint16_t{37});
    static_assert(static_cast<std::uint16_t>(EventTag::DialogStart) == std::uint16_t{38});
    static_assert(static_cast<std::uint16_t>(EventTag::CameraMove) == std::uint16_t{39});
    static_assert(EventTag::LastSerializable == EventTag::CameraMove);

    struct DialogChoicePayload;
    struct StoryResultPayload;

    struct GameEvent
    {
        EventTag tag = EventTag::Custom;
        std::uint32_t a = 0, b = 0; // entity / settlement / npc ids
        float fx = 0, fy = 0;
        int ix = 0, iy = 0;
        std::string s1{}, s2{}; // ids, text payloads
        std::shared_ptr<std::vector<DialogChoicePayload>> dialogChoices = nullptr;
        std::shared_ptr<StoryResultPayload> storyResult = nullptr;
    };

    struct DialogChoicePayload
    {
        std::string label{};
        std::string nodeId{};
        std::vector<GameEvent> effects{};
    };

    struct StoryResultPayload
    {
        std::string sourceNodeId{};
        std::string storyId{};
        std::vector<std::pair<std::string, std::string>> values{};
    };

    struct WorldHistoryEntry
    {
        std::uint32_t tick = 0;
        int day = 0, hour = 0;
        GameEvent event{};
    };

} // namespace sm
