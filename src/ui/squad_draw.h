// КАК СКВАД ВЫГЛЯДИТ НА МАКРОКАРТЕ — ОДНА ТАБЛИЦА НА ВСЕ РОДА ОСИ.
//
// Здесь стояла `kPlaceDraw` — «презентация МЕСТА», и подвижные рода были в
// ней тремя нулевыми строками. Нули держали не пустоту, а ВТОРОЙ ПРОХОД
// РИСОВКИ: места рисовал обход `for_each_place` своей строкой, сквады —
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
    // было ГЛОБАЛЬНОЙ константой `kPlaceCellSpan`, то есть ответом за всех
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
    {.arch = SquadArchetype::Place, .cellSpan = 2.0f, .remembered = true, .pictureFromBody = false, .layer = 0, .figuresFromPools = false},
    {.arch = SquadArchetype::Worker, .cellSpan = 1.0f, .remembered = false, .pictureFromBody = true, .layer = 1, .figuresFromPools = true},
    {.arch = SquadArchetype::Roamer, .cellSpan = 1.0f, .remembered = false, .pictureFromBody = true, .layer = 1, .figuresFromPools = true},
    {.arch = SquadArchetype::Hero, .cellSpan = 1.0f, .remembered = false, .pictureFromBody = true, .layer = 1, .figuresFromPools = true},
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
    SpriteId spriteDepleted;  // variant for a consumed place (== sprite
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
    {.type = SquadType::None, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 12.0f, .maxPx = 56.0f, .minZoom = 10.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 0.0f},
    // Столица носит ГОРОДСКОЙ спрайт — своего арта у неё ещё нет, и
    // «нет арта» не значит «не рисуется» (ассерт ниже): крупнее города на
    // карте и тем отличима, пока художник не дал ей строку спрайта.
    {.type = SquadType::Capital, .sprite = SpriteId::City,
     .spriteDepleted = SpriteId::City,
     .basePx = 32.0f, .maxPx = 224.0f, .minZoom = 0.0f, .labelZoom = 6.0f,
     .mini = MiniShape::Ring, .miniR = 3.0f},
    {.type = SquadType::City, .sprite = SpriteId::City,
     .spriteDepleted = SpriteId::City,
     .basePx = 28.0f, .maxPx = 192.0f, .minZoom = 0.0f, .labelZoom = 6.0f,
     .mini = MiniShape::Ring, .miniR = 3.0f},
    {.type = SquadType::Village, .sprite = SpriteId::Village,
     .spriteDepleted = SpriteId::Village,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 3.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
    {.type = SquadType::Spire, .sprite = SpriteId::SpireActive,
     .spriteDepleted = SpriteId::SpireSpent,
     .basePx = 26.0f, .maxPx = 160.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Diamond, .miniR = 4.0f},
    {.type = SquadType::Ruin, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
    {.type = SquadType::Lair, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
    {.type = SquadType::Shrine, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
    {.type = SquadType::Mine, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
    {.type = SquadType::Tower, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 22.0f, .maxPx = 144.0f, .minZoom = 0.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 1.6f},
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
    {.type = SquadType::Artel, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 12.0f, .maxPx = 56.0f, .minZoom = 10.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 0.0f},
    {.type = SquadType::Caravan, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 12.0f, .maxPx = 56.0f, .minZoom = 10.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 0.0f},
    {.type = SquadType::Collector, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 12.0f, .maxPx = 56.0f, .minZoom = 10.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 0.0f},
    // Дракон — архетип Hero, и картинку он берёт С ТЕЛА, как всякий ходок
    // (`pictureFromBody`): его строка существа несёт `SpriteId::Dragon`.
    // Поэтому здесь не спрайт, а размеры и порог зума.
    {.type = SquadType::Dragon, .sprite = SpriteId::None,
     .spriteDepleted = SpriteId::None,
     .basePx = 12.0f, .maxPx = 56.0f, .minZoom = 10.0f, .labelZoom = 0.0f,
     .mini = MiniShape::Dot, .miniR = 0.0f},
};

// РАЗМЕР СТРОКИ НАЗВАН — ЗНАЧИТ ПРИБИТ (DOD п.10). Он был НАЗВАН в ряду M-235
// реестра («28 Б посчитано по объявлению») и там же помечен как единственный
// из трёх сестёр БЕЗ пина: названный, но не прибитый размер — это проза,
// которая гниёт молча. Теперь он ЗАМЕРЕН, а не посчитан: проба `== 1` на
// clang, note «expression evaluates to '28 == 1'».
//
// РАСКЛАДКА, ПО КОТОРОЙ ВЫХОДЯТ 28: `alignof` = 4 (самый широкий член —
// `float`, указателей в строке нет). Три байта головы пакуются вместе —
// `type` на 0, `sprite` на 1, `spriteDepleted` на 2, байт 3 выравнивание;
// четыре `float` занимают 4..19; `mini` один байт на 20, 21..23 —
// выравнивание; `miniR` на 24..27.
//
// ДА, `mini` СТОИТ ОДИН МЕЖДУ ДВУМЯ `float` И СТОИТ 4 БАЙТА ВМЕСТО 1 — это
// ровно форма шрама DOD п.2 («одинокий узкий член рядом с широким не
// экономит НИЧЕГО»). Переставить его к трём головным байтам дало бы 24, и
// это НЕ СДЕЛАНО, причина ОДНА и она про выигрыш: это `constexpr`-таблица в
// бинарнике — 14 строк × 28 = 392 байта, на кап мира она не умножается, а
// DOD п.2 требует достигать `sizeof` там, где узкий тип покупает память НА
// ЭНТИТИ. Здесь он не покупает ничего, а правка без своего выигрыша
// устраняется, а не обосновывается (ЗАКОН КОНСТАНТ, ЗАКОН КАПА п.4).
//
// ЗДЕСЬ СТОЯЛА ВТОРАЯ ПРИЧИНА — «строки позиционные, перестановка села бы в
// соседние поля молча», — И ОНА СНЯТА В ТОТ ЖЕ ДЕНЬ: поля обеих таблиц этого
// файла теперь НАЗЫВАЮТСЯ, порядок судит `-Wreorder-init-list`. Значит
// перестановка стала ТЕХНИЧЕСКИ безопасной и всё равно не делается — по
// первой причине. Так и должно читаться: запрет снят, выигрыша нет.
// Пин нужен независимо от всего этого: он запрещает доливать колонку МОЛЧА —
// строку меняют осознанно, вместе с этим числом (§8 п.6).
static_assert(sizeof(SquadDrawRow) == 28,
              "SquadDrawRow layout changed — say WHICH column and why");

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
