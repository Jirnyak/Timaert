// КАК СКВАД ВЫГЛЯДИТ НА МАКРОКАРТЕ — ОДНА ТАБЛИЦА НА ВСЕ РОДА ОСИ.
//
// Здесь стояла `kLandmarkDraw` — «презентация МЕСТА», и подвижные рода были в
// ней тремя нулевыми строками. Нули держали не пустоту, а ВТОРОЙ ПРОХОД
// РИСОВКИ: места рисовал обход `for_each_landmark` своей строкой, сквады —
// свой цикл по слотам строкой тела, и чтобы город не нарисовался ДВАЖДЫ,
// между ними стоял ранний выход по архетипу — ровно тот гейт, который
// запрещает шапка `squad_archetype@src/tables/squad_type.h` (M-280).
//
// ВЕРДИКТ ВЛАДЕЛЬЦА 2026-10-19, дословно: «ОПЯТЬ ЛАНДМАРКИ ЭТИ!!! ЭТО ВОНЮЧЕЕ
// ЛЕГАСИ!!! У НАС ЕДИНАЯ ССИЕМА СКВАДОВ ПОЧЕМУ ИХ НЕ НАРИСУЕТ? ИЛИ ПРОБЛЕМА
// ЧТО У ГОРОДОВ СПРАЙТ ТИПА ВЫЛЕЗАЕТ НА СОСЕДНИЕ КЛЕТКИ? ЭТО ДА НО ТИПА НАДО
// РЕШИТЬ ЧЕРЕЗ АРХИЕТПЫ У НАС ТЕПЕРЬ СИСТМА СКВАДЫ АНКЕТЫ ТИПЫ АРХЕТИПЫ».
// Значит: проход ОДИН на все сквады, а то, чем город отличается от ходока, —
// КОЛОНКИ, и групповые из них ключуются АРХЕТИПОМ (`kArchetypeDraw` ниже).
// Гейт раннего выхода умер вместе со вторым проходом: порождать его больше
// нечем — ни один слот не пропускается по роду, различается лишь то, какие
// колонки он читает.
//
// Цвет сюда НЕ переезжает: `color` строки реестра остаётся ОДНИМ источником
// цвета для карты, миникарты и списков (вердикт владельца 2026-08-14).
//
// Новый род = его строка реестра + одна строка здесь. Род без нарисованного
// арта ставит `SpriteId::None` — `sprite_get()` вернёт null, и рисовка падает
// на глиф-кружок цветом строки реестра. «НЕТ АРТА» НЕ ЗНАЧИТ «НЕ РИСУЕТСЯ»:
// так живут пять из восьми неподвижных родов, и `static_assert` в конце файла
// держит это утверждение под компилятором.
#pragma once
#include <cstddef>
#include <cstdint>

#include "assets/sprite_atlas.h"
#include "core/table_guard.h"
#include "tables/squad_type.h"

namespace sm::ui {

// Minimap mark shape. Ring = filled dot + dark outline (cities), Dot = bare
// filled dot (villages), Diamond = 4-point poly + dark outline (spires).
enum class MiniShape : std::uint8_t { Ring, Dot, Diamond };

// ── ГРУППОВЫЕ ЗАКОНЫ ПРЕДСТАВЛЕНИЯ — КЛЮЧ АРХЕТИПА ────────────────────────
// Архетип здесь работает ровно так, как разрешает его собственная шапка:
// КЛЮЧОМ ТАБЛИЦЫ, а не гейтом прохода. Четыре вопроса, ответ на которые
// одинаков у всей группы и потому не повторяется в каждой строке рода.
//
// РАЗНЫЙ КЛЮЧ — НЕ СПУТНИК. DOD п.9 запрещает таблицу-спутник по ТОМУ ЖЕ
// ключу; здесь ключи разные: `kSquadDraw` спрашивают родом («какая у НЕГО
// картинка и каких размеров»), эту — архетипом («как карта обходится с ЭТОЙ
// группой»).
struct ArchetypeDrawRow {
    SquadArchetype arch;   // MUST equal its index — asserted below

    // СКОЛЬКО КЛЕТОК ЗАНИМАЕТ МЕТКА. Вопрос владельца («у городов спрайт
    // вылезает на соседние клетки») получил здесь свою колонку: прежде 2.0
    // было ГЛОБАЛЬНОЙ константой `kLandmarkCellSpan`, то есть ответом за всех
    // сразу, а ходок обходил её своей отдельной формулой размера.
    float cellSpan;

    // ПОМНИТ ЛИ КАРТА. Здание стоит на месте и потому есть факт карты —
    // рисуется поблёкшим по памяти (knowledge Explored). Человек уходит, и
    // память его не держит: он виден только в прямом виду. Два закона, что
    // жили в двух проходах порознь, теперь колонка одной группы.
    bool remembered;

    // ОТКУДА КАРТИНКА. У неподвижного она в строке его РОДА (город рисуется
    // городом), у ходока — в строке его ТЕЛА (сквад рисуется своим вожаком,
    // СПРАЙТ-ЗАКОН §7). Третьего источника нет.
    bool pictureFromBody;

    // ПОРЯДОК РИСОВКИ: метка под фигуркой. Один проход по слотам даёт порядок
    // СЛОТОВ, а он не закон — он след порядка рождения; слой говорит вслух,
    // что ходок рисуется ПОВЕРХ места, на котором стоит. Иначе артель,
    // вошедшая в город, пропадала бы под его спрайтом в 192 px.
    std::uint8_t layer;

    // КАКИЕ ЦИФРЫ У РЯДА ПАНЕЛИ БЛИЗОСТИ: потолки ТЕЛА (уровень и HP) или
    // ПАСТВА. Колонка заведена M-281, и у неё ровно один читатель —
    // `draw_npc_proximity_panel@src/ui/macro_overlay.cpp` (DOD п.9: читатель
    // назван). Прежде на этот вопрос отвечала ветка по ХРАНИМОМУ роду
    // субъекта, и ветка была последним, что держало тот род живым.
    //
    // ПОЧЕМУ ЭТО ГРУППОВОЙ ВОПРОС, А НЕ ВОПРОС ЖИЗНИ ТЕЛА: у неподвижного
    // сквада пулы ВЕСТИГИАЛЬНЫ — рождение ставит их нулями
    // (`birth_place@src/macro/place_birth.h`), а первый же полученный уровень
    // поднимает ему `maxHp`, оставляя `hp` нулём НАВСЕГДА, потому что дверь
    // пересборки отказывается воскрешать («Dead stays dead», `squad.h`).
    // Значит ни `hp`, ни `maxHp` не отвечают «что показать»: число у места
    // есть, но это паства, а не здоровье.
    bool figuresFromPools;
};

inline constexpr ArchetypeDrawRow
    kArchetypeDraw[std::size_t(SquadArchetype::Count)] = {
    //                        клеток  помнит  картинка с тела  слой  цифры тела
    {SquadArchetype::Place,   2.0f,   true,   false,           0,    false},
    {SquadArchetype::Worker,  1.0f,   false,  true,            1,    true},
    {SquadArchetype::Roamer,  1.0f,   false,  true,            1,    true},
    {SquadArchetype::Hero,    1.0f,   false,  true,            1,    true},
};

static_assert(rows_in_enum_order(kArchetypeDraw, &ArchetypeDrawRow::arch),
              "kArchetypeDraw row order must mirror SquadArchetype — the enum "
              "index IS the binding");

inline constexpr const ArchetypeDrawRow& archetype_draw(SquadArchetype a) {
    return kArchetypeDraw[std::size_t(a)];
}

// Слоёв ровно два, и проход рисовки режет список рисования на столько же.
inline constexpr std::size_t kMapDrawLayers = 2;
static_assert(kArchetypeDraw[std::size_t(SquadArchetype::Place)].layer
                  < kMapDrawLayers
              && kArchetypeDraw[std::size_t(SquadArchetype::Hero)].layer
                  < kMapDrawLayers,
              "слой строки обязан помещаться в число каналов прохода");

// ── СТРОКА РОДА — ЧАСТНОЕ: КАКАЯ КАРТИНКА И КАКИХ РАЗМЕРОВ ────────────────
struct SquadDrawRow {
    SquadType type;        // MUST equal its index — asserted below
    SpriteId sprite;          // None = no art, glyph-circle fallback
    SpriteId spriteDepleted;  // variant for a consumed landmark (== sprite
                              // when the kind cannot deplete)
    float basePx;             // mark_size() floor at low zoom
    float maxPx;              // mark_size() cap at high zoom
    float minZoom;            // hide below this zoom (0 = always visible)
    float labelZoom;          // draw the name at/above this zoom (0 = never)
    MiniShape mini;
    float miniR;              // minimap mark radius, px
};

inline constexpr SquadDrawRow
    kSquadDraw[std::size_t(SquadType::Count)] = {
    // ТИП БЕЗ НАЗНАЧЕНИЯ — ХОДОК, И СТРОКА ОБЯЗАНА ЭТО ПОЗВОЛЯТЬ. `None` есть
    // «точка течи границы миров» (шапка оси): бестиповый сквад, чей архетип
    // Roamer, то есть картинка у него с тела. Нулевой `maxPx` оставил бы его
    // размером 0 — невидимым, — поэтому разброс тот же, что у подвижных.
    {SquadType::None,    SpriteId::None, SpriteId::None,
     12.0f, 56.0f, 10.0f, 0.0f, MiniShape::Dot, 0.0f},
    {SquadType::City,    SpriteId::City, SpriteId::City,
     28.0f, 192.0f, 0.0f, 6.0f, MiniShape::Ring, 3.0f},
    {SquadType::Village, SpriteId::Village, SpriteId::Village,
     22.0f, 144.0f, 3.0f, 0.0f, MiniShape::Dot, 1.6f},
    {SquadType::Spire,   SpriteId::SpireActive, SpriteId::SpireSpent,
     26.0f, 160.0f, 0.0f, 0.0f, MiniShape::Diamond, 4.0f},
    {SquadType::Ruin,    SpriteId::None, SpriteId::None,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {SquadType::Lair,    SpriteId::None, SpriteId::None,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {SquadType::Shrine,  SpriteId::None, SpriteId::None,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {SquadType::Mine,    SpriteId::None, SpriteId::None,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {SquadType::Tower,   SpriteId::None, SpriteId::None,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    // ── ПОДВИЖНЫЕ РОДА: СВОИ ЗНАЧЕНИЯ, А НЕ НУЛИ ────────────────────────
    // Здесь стояли три нулевые строки с надписью «не рисуются этой таблицей»
    // — то есть таблица отказывалась отвечать, и за неё отвечал второй
    // проход. Теперь отвечает она: картинку ходок берёт с тела
    // (`pictureFromBody` его архетипа), а ЭТИ колонки говорят, каких он
    // размеров и с какого зума виден.
    //
    // 12…56 px — прежний разброс фигурки (`clamp(zoom, 12, 56)` старого
    // прохода), выраженный теми же двумя колонками, что у города: при
    // `cellSpan` = 1 и zoom ≥ 4 (`kMacroZoomMin`) формула размера даёт тот же
    // ответ до пикселя. minZoom 10 — прежний ОБЩИЙ гейт прохода фигурок
    // («ниже 10 px/клетку 256-пиксельный спрайт схлопывается в пятно и спорит
    // с процедурными фичами»), ставший колонкой РОДА, как у деревни.
    {SquadType::Artel,     SpriteId::None, SpriteId::None,
     12.0f, 56.0f, 10.0f, 0.0f, MiniShape::Dot, 0.0f},
    {SquadType::Caravan,   SpriteId::None, SpriteId::None,
     12.0f, 56.0f, 10.0f, 0.0f, MiniShape::Dot, 0.0f},
    {SquadType::Collector, SpriteId::None, SpriteId::None,
     12.0f, 56.0f, 10.0f, 0.0f, MiniShape::Dot, 0.0f},
};

static_assert(rows_in_enum_order(kSquadDraw, &SquadDrawRow::type),
              "kSquadDraw row order must mirror SquadType — the enum "
              "index IS the binding");

inline constexpr const SquadDrawRow& squad_draw(SquadType t) {
    return kSquadDraw[std::size_t(t)];
}

// «НЕТ АРТА» НЕ ЗНАЧИТ «НЕ РИСУЕТСЯ» — ПОД КОМПИЛЯТОРОМ, А НЕ ПОД ПАМЯТЬЮ
// РЕВЬЮЕРА. Пять неподвижных родов из восьми несут `SpriteId::None` и метят
// карту глиф-кружком; судить рисовку колонкой `sprite` — ошибка, стоившая
// этому наряду первой постановки задачи (M-280, 2026-10-19).
static_assert(squad_draw(SquadType::Ruin).sprite == SpriteId::None
                  && squad_draw(SquadType::Ruin).maxPx > 0.0f
                  && !archetype_draw(squad_archetype(SquadType::Ruin))
                          .pictureFromBody,
              "безартовый неподвижный род рисуется глиф-кружком своей строки, "
              "а не фигуркой тела");

// Ни один род оси не остаётся невидимым: нулевой `maxPx` означал бы размер 0
// на любом зуме, то есть строку, которая отказывается отвечать, — ровно то,
// чем были три нулевые строки до слияния проходов.
static_assert([] {
    for (const SquadDrawRow& r : kSquadDraw)
        if (r.maxPx <= 0.0f) return false;
    return true;
}(), "строка рода обязана нести свой размер — иначе род невидим");

} // namespace sm::ui
