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

    // Every tag below has at least one producer AND at least one consumer, or
    // is one edit away from it — the 2026-08-05 producer/consumer census
    // (audit.md Часть II) deleted the 16 tags nothing outside this enum ever
    // referenced (PlayerLevelUp, PlayerDeath, NpcSpawn, NpcGreeted, Encounter,
    // SettlementChangeOwner, QuestAbandoned, Trade, NpcHpChange,
    // SettlementMoodChange, PlayerStatChange, BattleEnd, MagicSurge,
    // FactionRelationChange, DialogStart, CameraMove). Values are DENSE and
    // implicit now: the old TS-parity numeric anchors died with the parity,
    // and the renumbering invalidated saves => kSaveVersion 20.
    enum class EventTag : std::uint16_t
    {
        PlayerMove = 0,
        NpcDeath,         // a = entity id, b = killer, ix = NPCKind.type
        SettlementVisit,
        QuestStart,       // s1 = quest id, a = stable quest key
        QuestUpdate,      // s1 = quest id, a = stable quest key
        QuestComplete,    // s1 = quest id, a = stable quest key
        QuestFail,        // s1 = quest id, a = stable quest key
        SpellCast,
        SpellLearned,
        LandmarkChangeOwner,
        WorldCellChange,
        TimeAdvance,      // a = day, iy = hour, ix = 1 event per elapsed hour
        PlayerGoldChange, // ix = delta, iy = optional new total
        ApplyEffect,      // s1 = effectType, ix = value
        BattleStart,      // s1 = enemyName, s2 = enemyType, ix = enemyLevel
        CodexUnlock,      // s1 = entryId
        ReputationChange, // s1 = factionId, ix = delta, iy = optional new value
        ShowDialog,       // s1 = title, s2 = description, ix = choice count
        ShowStory,        // s1 = source node, s2 = story id, ix/iy/a/b = counts
        StoryResult,      // storyResult = choices keyed by phase id
        SpawnEntity,      // s1 = npc type id/name, ix/iy = x/y, a = level
        Custom,
        PlayerEnterSettlement, // s1 = settlement name, a/ix = settlement id
        PlayerLeaveSettlement, // s1 = settlement name, a/ix = settlement id
        SpireDepleted,    // a = spire id, b = spell registry ordinal,
                          // ix/iy = the spire's macro cell. The world fact
                          // "this spire is consumed"; the spell itself lands
                          // via the SpellLearned event the app layer emits
                          // after resolving the ordinal (only content/ knows
                          // the registry).
        LastSerializable = SpireDepleted,

        // Compatibility aliases retained for existing native call sites.
        QuestAccepted = QuestStart,
        QuestObjectiveProgress = QuestUpdate,
        QuestCompleted = QuestComplete,
        QuestFailed = QuestFail,
    };

    static_assert(EventTag::QuestAccepted == EventTag::QuestStart);
    static_assert(EventTag::QuestObjectiveProgress == EventTag::QuestUpdate);
    static_assert(EventTag::QuestCompleted == EventTag::QuestComplete);
    static_assert(EventTag::QuestFailed == EventTag::QuestFail);
    static_assert(EventTag::LastSerializable == EventTag::SpireDepleted);

    // Native-only guard: event is still observable/history-visible, but the
    // TS-equivalent state mutation already happened before emit.
    constexpr std::uint32_t kEventEffectAlreadyApplied = 0x54535041u; // "TSPA"

    struct DialogChoicePayload;
    struct StoryResultPayload;

    // NpcDeath carries the dead body's NPCKind.type in `ix`. A body with no
    // NPCKind at all reports this instead of a plausible-looking 0, which is a
    // real type id (Peasant) and would have been counted as one.
    inline constexpr int kNoNpcType = -1;

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

    // WHAT HAPPENED, kept for the record — and deliberately LESS than the live
    // event. History is purely runtime (it is never serialised) and nothing
    // has ever read a dialog's buttons or a story's result out of it: those
    // are this FRAME's presentation, copied into App-owned storage the moment
    // they arrive. The bus used to deep-copy both `shared_ptr`s into every
    // history entry anyway — two atomic refcount bumps per event per tick, for
    // a payload no reader could reach from here.
    struct WorldHistoryEntry
    {
        std::uint32_t tick = 0;
        int day = 0, hour = 0;
        GameEvent event{};   // payload pointers deliberately left null
    };

} // namespace sm
