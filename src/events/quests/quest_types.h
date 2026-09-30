// Quest types — six universal verbs. Mirrors quests/quest-types.ts.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "events/event_types.h"

namespace sm
{

    enum class QuestCategory : std::uint8_t
    {
        Main,
        Side,
        Procedural
    };

    // ТРИ РОДА ЦЕЛИ, И У КАЖДОГО ЕСТЬ ПРОИЗВОДИТЕЛЬ КОНТЕНТА. Список этот
    // не «шесть универсальных глаголов» из старого замысла, а ровно то, что
    // генератор поручений умеет выдать (`content/quests/procedural.cpp`);
    // ветка, которую никто не производит, — это switch, ждущий контента,
    // который никогда не придёт, и она обязана умереть первой (вердикт
    // владельца 2026-09-28: «сносим всё что не системное»).
    //
    // Снято 2026-09-28 (M-116): `FindLocation` (ждал `EventTag::PlayerMove` с
    // точным совпадением клетки — генератор такой цели не делал ни разу; с
    // ним ушли колонки `cellX`/`cellY`) и `WaitAt` (ждал `TimeAdvance`,
    // колонки `hoursRequired`/`hoursWaited`). Раньше (M-116 ч.1) так же ушёл
    // `InteractCell` со своей колонкой `action`.
    enum class ObjectiveKind : std::uint8_t
    {
        VisitCell,
        DeliverItems,
        DestroyNpc,
    };

    struct Objective
    {
        ObjectiveKind kind = ObjectiveKind::VisitCell;
        bool completed = false;

        // Generic fields — interpretation depends on kind.
        int ix = 0, iy = 0;
        int subX = 0, subY = 0;
        float radius = 0;
        std::string itemId{};
        int quantity = 0;
        int targetSettlementId = 0;
        int npcType = 0;
        int count = 0, killed = 0;
        // (`zoneRadius` снят 2026-09-28: писался генератором и ездил в сейве,
        //  но не читался НИ ОДНОЙ строкой логики — колонка-сирота, DOD п.9.)
    };

    // ТРИ РОДА НАГРАДЫ, ПО ТОМУ ЖЕ ПРАВИЛУ. Снято 2026-09-28 (M-116):
    // `Item` — печатала предмет В СУМКУ ИГРОКА из воздуха, дарителя в звонке
    // не было ни одного; это предметная половина того же дефекта, чью денежную
    // половину снёс M-139, и дыра остаётся ОТКРЫТОЙ до пула лута, без времянки
    // (AGENTS §1). `Event` — награда, отправлявшая ПРОИЗВОЛЬНЫЙ `GameEvent`,
    // то есть чёрный ход в шину мимо всякого рода; производителя в контенте у
    // неё не было. С ними ушли колонки `Reward::itemId` и `Reward::event`.
    enum class RewardKind : std::uint8_t
    {
        Gold,
        Xp,
        Reputation
    };
    struct Reward
    {
        RewardKind kind = RewardKind::Gold;
        int amount = 0;
        std::string faction{};
        int delta = 0;
    };

    struct Quest
    {
        // The quest's IDENTITY: a monotonic persistent ordinal drawn from
        // GameState.nextQuestOrdinal at ACCEPT — the moment an offer stops
        // being a projection (offer lists regenerate from the seed per
        // settlement per day, exactly like the subworld regenerates from the
        // macro) and becomes an object the world stores, saves and names in
        // events (ev.a). Same law as nextMacroSpawnOrdinal (CANON S20.1,
        // M-37): a hash of a string is not an identity — the
        // FNV `quest_id_key` that lived here collided silently and forever.
        // 0 = an OFFER not yet accepted (the reserved "no quest", like
        // landmark id 0). Width: uint32 — a count, never negative; even 100
        // accepted quests a day for 300 game years is ~4M « 2^32.
        std::uint32_t ordinal = 0;
        // OFFER PROVENANCE — what the old id string encoded, as POD. Each
        // generator fires at most once per settlement per day
        // (content/quests/procedural.cpp generate_for_context), so the triple
        // {giverSettlementId, offerSlot, bornDay} names an offer uniquely:
        // it is what "have I already taken/done this offer?" compares
        // (QuestEngine::is_known), and it dies with its day — tomorrow's
        // offers are new triples by construction.
        // offerSlot: index into the generator table (7 rows today, uint8
        // caps at 255 — the table would have to grow 36× to overflow).
        // bornDay: the day the offer was generated; -1 = no provenance
        // (authored quests, fixtures) — a legal negative, hence signed.
        std::uint8_t offerSlot = 0;
        std::int32_t bornDay = -1;
        std::string title{}, description{};
        QuestCategory category = QuestCategory::Procedural;
        int giverSettlementId = 0;
        std::vector<Objective> objectives{};
        std::vector<Reward> rewards{};
        std::vector<GameEvent> onAccept{};
        int expireDay = -1;
        int difficulty = 1;
    };

    // One offer names one (giver, slot, day) triple — see Quest above.
    inline bool same_offer(const Quest &a, const Quest &b) noexcept
    {
        return a.giverSettlementId == b.giverSettlementId
            && a.offerSlot == b.offerSlot
            && a.bornDay == b.bornDay;
    }

} // namespace sm
