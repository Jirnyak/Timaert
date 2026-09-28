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

    // Every tag below HAS A PRODUCER, and that sentence stood here while it was
    // FALSE — the M-116 census (2026-09-28) counted three tags nobody in the
    // world emitted, each kept alive by its own READER arm. So the rule is
    // written as the census states it: a tag is a tag when something in `src/`
    // constructs it; a reader alone is not a producer, and a shape reachable
    // only through authored data (Quest::onAccept, Reward::event,
    // DialogChoicePayload::effects) that no content actually authors is not one
    // either.
    //
    // Deleted by that rule, 2026-09-28 (owner's ruling «сносим всё что не
    // системное»): ApplyEffect (its arm carried the last string verb table —
    // "heal_hp"/"restore_mp"/"drain_sp"/"grant_xp" — restating arithmetic the
    // bonus registry already owns), CodexUnlock (the codex bits live on and are
    // seeded by macro/state.cpp), BattleStart (its reader booted the subworld
    // and spawned a body BY STRING NAME — a second way into combat beside the
    // one that matters, detect_forced_encounter → PreBattle).
    // Earlier passes deleted 16 tags (2026-08-05: PlayerLevelUp, PlayerDeath,
    // NpcSpawn, NpcGreeted, Encounter, SettlementChangeOwner, QuestAbandoned,
    // Trade, NpcHpChange, SettlementMoodChange, PlayerStatChange, BattleEnd,
    // MagicSurge, FactionRelationChange, DialogStart, CameraMove) and three
    // more (M-116 part 1: SettlementVisit, LandmarkChangeOwner, WorldCellChange).
    // Values are DENSE and implicit: the old TS-parity numeric anchors died
    // with the parity, and every renumbering invalidates saves (kSaveVersion).
    //
    // Six tags below have a producer but no PRODUCTION reader — PlayerMove,
    // QuestStart, QuestUpdate, SpellCast, TimeAdvance, PlayerLeaveSettlement.
    // They are facts without a listener, and who listens is M-116 (CANON S20);
    // they are NOT dead ends of the same kind as the deleted ones.
    // ShowDialog/ShowStory/StoryResult are not world facts at all — they are
    // requests to the UI (CANON.md «Окна диалогов и сюжетные слайды — не факты,
    // а просьбы к UI от логических узлов») and leave with M-175.
    enum class EventTag : std::uint16_t
    {
        PlayerMove = 0,
        NpcDeath,         // a = entity id, b = killer, ix = NPCKind.type
        QuestStart,       // a = quest ordinal, s2 = title (feed display)
        QuestUpdate,      // a = quest ordinal
        QuestComplete,    // a = quest ordinal
        QuestFail,        // a = quest ordinal, s2 = "expired"/"abandoned"
        SpellCast,
        SpellLearned,
        TimeAdvance,      // a = day, iy = hour, ix = 1 event per elapsed hour
        PlayerGoldChange, // ix = delta, iy = optional new total
        ReputationChange, // s1 = factionId, ix = delta, iy = optional new value
        ShowDialog,       // s1 = title, s2 = description, ix = choice count
        ShowStory,        // s1 = source node, s2 = story id, ix/iy/a/b = counts
        StoryResult,      // storyResult = choices keyed by phase id
        SpawnEntity,      // s1 = npc type id/name, ix/iy = x/y, a = level
        Custom,           // ДЕФОЛТ ИНИЦИАЛИЗАЦИИ, НЕ СОБЫТИЕ: the value a
                          // freshly declared GameEvent::tag (below) and
                          // ConditionSlot::tag (events/logic_nodes.h) carry
                          // before their author names one. Nothing in the world
                          // emits it; making the default a real tag would have
                          // every uninitialised event claim to be that one.
        PlayerEnterSettlement, // s1 = settlement name, a/ix = settlement id
        PlayerLeaveSettlement, // s1 = settlement name, a/ix = settlement id
        SpireDepleted,    // a = spire id, b = spell registry ordinal,
                          // ix/iy = the spire's macro cell. The world fact
                          // "this spire is consumed"; the spell itself lands
                          // via the SpellLearned event the app layer emits
                          // after resolving the ordinal (only content/ knows
                          // the registry).
        LastSerializable = SpireDepleted,
    };

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
        // Пространство id зависит от тега: макро-NpcDeath несёт ПАКОВАННЫЙ
        // хэндл store (macro_handle_bits, все единицы = никого), сценная
        // смерть — биты entt-энтити тела, квесты — свои ординалы.
        std::uint32_t a = 0, b = 0; // packed handle / entity / ordinal ids
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

} // namespace sm
