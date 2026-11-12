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

    // ПРЕДМЕТ ШИНЫ — ПРОСЬБЫ ПРЕДСТАВЛЕНИЮ И ВНУТРЕННИЕ ПРОСЬБЫ app→logic,
    // И БОЛЬШЕ НИЧЕГО (M-410, 2026-11-06). «Событием МИРА» здесь не живёт
    // ни один тег: факты мира — РОД 5 канонического фрейма (bus/fact.h,
    // летопись; вниз пакетом, вверх дельтой, в сейв дампом), и квесты
    // судят ЛЕТОПИСЬЮ (kTaskVerbs — «квест = предикат над фактами»), а не
    // шиной. Правило тега прежнее: тег есть тег, когда что-то в src/ его
    // КОНСТРУИРУЕТ и что-то в src/ его ЧИТАЕТ; остальное — мертвечина, и
    // она уже отрастала заново (M-116 снёс трёх читателей без эмитента —
    // M-410 нашёл трёх НОВЫХ таких же).
    //
    // Снесено M-410 (2026-11-06), поимённо: NpcDeath (голый тег — оба
    // конца умерли ломтём 5 M-171), PlayerMove / SpellCast / TimeAdvance /
    // PlayerLeaveSettlement (эмитент без продакшен-читателя; двое жили
    // только смоуком — свидетели мёртвого канала снесены с каналом),
    // PlayerGoldChange / ReputationChange / SpawnEntity (читатель без
    // эмитента — теоретический канал emit_all(choice.effects) кормился
    // только пустыми effects, которых контент не авторит), алиас
    // LastSerializable (врал именем: в сейв не идёт ни байта шины) и
    // стражи kEventEffectAlreadyApplied («TSPA», читатели умерли с
    // ветками) и kNoNpcType (сирота NpcDeath).
    // Прежние сносы: 16 тегов 2026-08-05; SettlementVisit /
    // PlaceChangeOwner / WorldCellChange (M-116 часть 1); ApplyEffect /
    // CodexUnlock / BattleStart (M-116, 2026-09-28); четыре квестовых —
    // ломоть 3 M-72.
    //
    // ShowDialog/ShowStory/StoryResult/PlayerEnterSettlement — просьбы
    // представлению (CANON: «Окна диалогов и сюжетные слайды — не факты,
    // а просьбы к UI от логических узлов»), выселяются в ui/ нарядом
    // M-175. SpireDepleted→SpellLearned — внутренняя просьба app→logic
    // (слив факта Drained учит спеллу; реестр спеллов знает только
    // content/). Нумерация плотная и неявная; сейва это не касается.
    enum class EventTag : std::uint16_t
    {
        SpellLearned = 0, // s1 = spell id; петля аппликатора (анонс учёбы)
        ShowDialog,       // s1 = title, s2 = description, ix = choice count
        ShowStory,        // s1 = source node, s2 = story id, ix/iy/a/b = counts
        StoryResult,      // storyResult = choices keyed by phase id
        Custom,           // ДЕФОЛТ ИНИЦИАЛИЗАЦИИ, НЕ СОБЫТИЕ: the value a
                          // freshly declared GameEvent::tag (below) and
                          // ConditionSlot::tag (events/logic_nodes.h) carry
                          // before their author names one. Nothing in the world
                          // emits it; making the default a real tag would have
                          // every uninitialised event claim to be that one.
        PlayerEnterSettlement, // s1 = settlement name, a/ix = settlement id
        SpireDepleted,    // a = spire id, b = spell registry ordinal,
                          // ix/iy = the spire's macro cell. Слив факта
                          // Drained (spire_fact_sink): the spell lands via
                          // the SpellLearned event the applicator emits
                          // after resolving the ordinal.
        Count,            // НЕ ТЕГ — выведенный счёт (ёмкость подписок
                          // шины); новый тег встаёт ВЫШЕ этой строки.
    };

    struct DialogChoicePayload;
    struct StoryResultPayload;

    struct GameEvent
    {
        EventTag tag = EventTag::Custom;
        // Пространство id зависит от тега (квесты — ординалы, SpireDepleted
        // — ид шпиля/спелла…). У NpcDeath идентичности здесь НЕТ ВОВСЕ:
        // обе сироты (сценная и макро) снесены вердиктами 2026-10-10,
        // системная форма идентичности придёт эпиком событий/фрейма.
        std::uint32_t a = 0, b = 0; // packed handle / entity / ordinal ids
        // (Пара флоатов `fx`/`fy` стояла здесь без единого писателя и
        //  читателя — дробная точка, которой у мира нет; снята M-462.)
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
