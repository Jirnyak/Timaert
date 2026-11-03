// ФАКТ — род 5 канонического фрейма (M-171, ломоть 3): кто, что, где,
// сколько — POD из ординалов, 32 байта. Формы FactSubject/FactKind/WorldFact
// переехали сюда из macro/chronicle.h: строка пакета живёт ниже обоих миров
// (вниз — окно фактов клеток фрейма, вверх — строки-операции дельты).
// ТАБЛИЦА деяний (интерес, слава) и сама летопись — macro/chronicle.h.
#pragma once
#include <cstdint>

namespace sm {

// ── Who a fact is about ──────────────────────────────────────────────────
enum class FactSubject : std::uint8_t {
    None = 0,
    Squad,      // ecs::MacroSpawnId — the macro world's actors, and the main
                // authors of its history
    Rooted,     // rooted-squad id — settlements, villages, spires share the
                // ONE subject-ordinal space (nextMacroSpawnOrdinal, M-37)
    Cell,       // the place itself did or suffered it
    Faction,    // faction_index()
    Count
};

enum class FactKind : std::uint16_t {
    None = 0,
    Killed,          // subject killed object            (amount = bodies)
    Died,            // subject died                     (amount = bodies)
    // (Robbed ВЫРЕЗАН, владелец 2026-09-03: «минимум систем … died уже
    // достаточно» — лут павшего и так молчит правильно: Died/Killed несут
    // бой, поле угрозы грабёж не греет. Сдвиг ординалов = бамп v75.)
    //
    // (Род `Battle` УНИЧТОЖЕН 2026-11-03, вердикт владельца «Снести сирот
    // сейчас». «Subject fought object (amount = casualties)» — НОЛЬ
    // отправителей во всём дереве: бой мира говорит `Killed` победителем и
    // `Died` каждым осиротевшим домом (`record_battle_facts@src/macro/squad.h`,
    // ОБА авторезолва), то есть исход назван ДВУМЯ строками с разных сторон, а
    // третья, «они сражались», не писалась никогда. Заведут заново, если у
    // мира появится событие СРАЖЕНИЯ, у которого нет победителя.)
    Traded,          // subject traded with object       (amount = value)
    // (Род `Gathered` УНИЧТОЖЕН 2026-11-03 тем же вердиктом. «Subject took
    // from the land (amount = units)» — НОЛЬ отправителей: взятое у земли
    // мир записывает `Drained` там, где взятое НЕ ВОЗВРАЩАЕТСЯ (жила,
    // орб), а возобновимое — лес, урожай, фауна — не деяние летописи вовсе,
    // оно убыль ПОЛЯ, и пишет её ведомость стока. Заведут вместе с
    // механикой, которой «нарубил» будет деянием.)
    // (Род `Built` УНИЧТОЖЕН 2026-11-03 тем же вердиктом. «Subject raised
    // something» — НОЛЬ отправителей: фичу на клетку ставит генерация
    // (ЗАКОН ГЕНЕРАЦИИ: фича ДО сквада) и плуг одной клеткой
    // (`plough_field_cell@src/macro/macro_stock.cpp`), и ни один из двух
    // летописи не говорит. Заведут вместе со стройкой как способностью.)
    Starved,         // subject went hungry              (amount = heads)
    // (Род `OwnerChanged` УНИЧТОЖЕН 2026-11-03 тем же вердиктом. «Object now
    // belongs to subject» — НОЛЬ отправителей: смены владельца в мире НЕ
    // ПРОИСХОДИТ, потому что её механика (осада, захват) не построена, а
    // фракции идут позже всего (ФРЕЙМ, род 6: «мы ещё не долшли до фракций в
    // полировке ядра»). Заведут вместе с захватом.)
    Explored,        // subject learned a place          (amount = depth)
    // Took from the world something that does not come back — a spire's orb,
    // a vein worked out. NOT "used a thing": that is a mechanism, and this is
    // a deed (see the note above the enum).
    Drained,
    // (Род `Discovered` УНИЧТОЖЕН 2026-10-11, вердикт владельца: «снести
    // строку». Единственным его писателем было РОЖДЕНИЕ ЖИЛ в течение партии,
    // вырезанное нарядом M-81 («НЕ БУДЕТ РОЖАТЬСЯ ЖИЛ В ТЕЧЕНИ ВАРТИИ МЫ ЭТО
    // ВЫРЕЗАЕМ»), и с того дня род факта стоял строкой словаря с НУЛЁМ
    // отправителей — та же колонка-сирота, только в каталоге (DOD п.9).
    // Когда у открытия появится настоящий открыватель — данж, руина, чёрный
    // артефакт (эпик M-248), — род заведут заново вместе с ним.)
    //
    // (Род `Spawned` УНИЧТОЖЕН 2026-11-03 тем же вердиктом. «Subject appeared
    // in the world» — НОЛЬ отправителей: рождение сквада есть СЛОТ стора
    // (`birth_squad@src/macro/birth_squad.h`), и спрашивают о нём колонки, а
    // не летопись. «Появился» — не деяние: у него нет ни славы, ни интереса,
    // и строка стояла с `baseRenown` 0 именно поэтому.)
    QuestTaken,      // subject took object's contract
    QuestDone,       // subject finished it
    // Subject paid its suzerain the tithe (amount = coin) — the feudal tax
    // graph's own record (owner 2026-08-30, CANON S24). Appended, so saved
    // ordinals stay.
    Taxed,
    Count
};

// ── The record ───────────────────────────────────────────────────────────
//
// 32 bytes exactly, and every field a number. `day` rather than a tick: a
// chronicle is asked in DAYS ("the last thirty"), and `seq` already orders
// two facts inside one day, so a second time column would be a second answer
// to one question.
struct WorldFact {
    std::int32_t  day = 0;        // world day this happened on
    std::uint32_t seq = 0;        // global fact number; also the ring's proof
    std::uint32_t nextInCell = 0; // intrusive chain, newest→oldest, per index cell
    std::uint16_t kind = 0;       // FactKind
    std::uint8_t  subjectKind = 0;// FactSubject
    std::uint8_t  objectKind = 0; // FactSubject
    std::uint32_t subject = 0;    // ordinal in the subject kind's own space
    std::uint32_t object = 0;
    std::int16_t  x = 0, y = 0;   // the macro cell it happened on
    std::int32_t  amount = 0;     // what the kind's row says it means
};
static_assert(sizeof(WorldFact) == 32,
              "a fact is 32 bytes: it is scanned by the thousand and saved whole");

} // namespace sm
