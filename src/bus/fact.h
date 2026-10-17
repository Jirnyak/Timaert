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
    Landmark,   // landmark id — settlements, villages, spires share the ONE
                // subject-ordinal space (nextMacroSpawnOrdinal, M-37)
    Cell,       // the place itself did or suffered it
    Faction,    // faction_index()
    Count
};

enum class FactKind : std::uint16_t {
    None = 0,
    Killed,          // subject killed object            (amount = bodies)
    Died,            // subject died                     (amount = bodies)
    Battle,          // subject fought object            (amount = casualties)
    // (Robbed ВЫРЕЗАН, владелец 2026-09-03: «минимум систем … died уже
    // достаточно» — лут павшего и так молчит правильно: Died/Killed несут
    // бой, поле угрозы грабёж не греет. Сдвиг ординалов = бамп v75.)
    Traded,          // subject traded with object       (amount = value)
    Gathered,        // subject took from the land       (amount = units)
    Built,           // subject raised something         (amount = units)
    Starved,         // subject went hungry              (amount = heads)
    OwnerChanged,    // object now belongs to subject
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
    Spawned,         // subject appeared in the world
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
