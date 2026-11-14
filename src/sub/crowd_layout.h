// РАСКЛАДКА НАСЕЛЕНИЯ СЦЕНЫ ПО РОДУ СКВАДА — ДАННЫЕ СУБМИРА У СУБМИРА.
//
// Три числа приехали сюда 2026-11-02 из умирающего реестра мест
// (ломоть 5б M-235: `crowdHabitat`/`crowdInsideShift`/`garrisonShift` в
// `PlaceDef` читал ТОЛЬКО субмир — раскладка толпы сцены есть частность
// ПОТРЕБИТЕЛЯ, и её числа живут у него; ЗАКОН АГНОСТИЧНОСТИ: слой выше —
// редактор — держит свои числа у себя). Долг канала упал: четыре чтения
// `place_def` из src/sub умерли вместе с транзитным включением
// `macro/place_registry.h` в `sub/map_data.h`.
//
// КЛЮЧ — ОСЬ РОДА (tables/squad_type.h, общий слой L0): сцена спрашивает,
// КАКОЙ сквад воплощается. Биты ареала — ИМЕНАМИ из tables/npc.h (тот же
// слой), поэтому стражи-цитаты «u16-литерал обязан совпасть с kHab*» умерли
// ПО ПОСТРОЕНИЮ: второго правописания бита здесь нет.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/table_guard.h"   // rows_in_enum_order — ключ строки есть индекс
#include "tables/npc.h"         // kHabTown/kHabSpire/kHabRuin — биты ареала
#include "tables/squad_type.h"  // SquadType — ось рода, ключ таблицы

namespace sm::sub {

// «Род гарнизона не держит» — ПОСЛЕДНЕЕ значение типа, не −1 (ЗАКОН УЗКОГО
// ИНДЕКСА: сдвиг законно мал, 0xFF недостижим как честный сдвиг).
inline constexpr std::uint8_t kNoGarrison = 0xFF;

struct CrowdLayoutRow {
    SquadType type;  // MUST equal the row's index (guard below)
    // Полоса ареала ТОЛПЫ этого рода: его население, ставшее видимым
    // (уличный люд, домочадцы за дверью, зал с гарнизоном). 0 = род толпы
    // не держит; семья толпы — колонка, никогда имя в пикере (§42).
    std::uint16_t crowdHabitat = 0;
    // Сколько душ уходит С УЛИЦЫ на этажи интерьера: pop >> this, остаток —
    // толпа снаружи (глаз владельца 2026-09-11: «снаружи больше сотни,
    // внутри десятки на ярус»). Сдвиг po2, не процент.
    std::uint8_t crowdInsideShift = 0;
    // Целевая сила гарнизона: population >> this («гарнизон = армия
    // ландмарка», §42); kNoGarrison = род гарнизона не держит.
    std::uint8_t garrisonShift = kNoGarrison;
};

inline constexpr CrowdLayoutRow kCrowdLayout[std::size_t(SquadType::Count)] = {
    {.type = SquadType::None},
    {.type = SquadType::Capital,
     .crowdHabitat = kHabTown, .crowdInsideShift = 0, .garrisonShift = 3},
    {.type = SquadType::City,
     .crowdHabitat = kHabTown, .crowdInsideShift = 0, .garrisonShift = 3},
    {.type = SquadType::Village,
     .crowdHabitat = kHabTown, .crowdInsideShift = 0, .garrisonShift = 3},
    // Демоны шпиля — его НАСЕЛЕНИЕ (не дичь клетки, §42 Инк 5); четверть
    // уходит на ярусы башни («улица — толпа, башня держит четверть»).
    {.type = SquadType::Spire,
     .crowdHabitat = kHabSpire, .crowdInsideShift = 2},
    // Толпа руины СНЕСЕНА (M-374, вердикт владельца: «толпа — легаси»):
    // руина — чистая фича без сквада (M-320), её обитателей спавнит МОДУЛЬ
    // полосой FaunaBand (kFeatureGenRows@src/sub/gens/dispatch.cpp), а не
    // раскладка толпы поселения. Ряд пуст, как у Shrine/Mine/Tower.
    {.type = SquadType::Ruin},
    // Бит руины — он же словарь нор (что заползает в погреба и пещеры);
    // у ЛОГОВА он остаётся полосой толпы.
    {.type = SquadType::Lair, .crowdHabitat = kHabRuin},
    {.type = SquadType::Shrine},
    {.type = SquadType::Mine},
    {.type = SquadType::Tower},
    // Подвижные роды толпу сцены не держат: их люди — головы инвентаря,
    // воплощаемые проекцией сквада, а не уличным спавном.
    {.type = SquadType::Artel},
    {.type = SquadType::Ploughmen},
    {.type = SquadType::Hunters},
    {.type = SquadType::Flaxers},
    {.type = SquadType::Miners},
    {.type = SquadType::Woodcutters},
    {.type = SquadType::Caravan},
    {.type = SquadType::Collector},
    {.type = SquadType::Dragon},
    {.type = SquadType::Nomad},
};

static_assert(rows_in_enum_order(kCrowdLayout, &CrowdLayoutRow::type),
              "kCrowdLayout row order must mirror SquadType — the enum "
              "index IS the binding");

// РАЗМЕР НАЗВАН — ЗНАЧИТ ПРИБИТ (DOD п.10): пин запрещает доливать колонку
// молча. type+pad, habitat на 2..3, два сдвига на 4..5, хвост до кратного
// двух.
static_assert(sizeof(CrowdLayoutRow) == 6,
              "CrowdLayoutRow layout changed — say WHICH column and why");

inline constexpr const CrowdLayoutRow& crowd_layout(SquadType t) {
    return kCrowdLayout[std::size_t(t)];
}

} // namespace sm::sub

#include "core/row_law.h"
// Контур строк файла — судит компилятор (`core/row_law.h`, M-169).
TIMAERT_ROW(sm::sub::CrowdLayoutRow);
