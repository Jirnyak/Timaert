// ГРУППОВЫЕ ЗАКОНЫ ПРЕДСТАВЛЕНИЯ СКВАДА НА МАКРОКАРТЕ — КЛЮЧ АРХЕТИПА.
//
// СТРОКА РОДА («какая картинка и каких размеров» — SquadDrawRow/kSquadDraw)
// УЕХАЛА 2026-11-01 В ЕДИНУЮ ТАБЛИЦУ РОДОВ `kSquadKinds@src/tables/squad_type.h`
// (ломоть 5б M-235, вердикт владельца: таблица родов ОДНА на все сквады,
// спрайт — просто число). Туда же уехали MiniShape и вердикт о цвете
// 2026-08-14 («ОДИН источник цвета») — источник по-прежнему один, он
// переехал целиком. История слияния двух проходов рисовки (M-280, вердикт
// «ОПЯТЬ ЛАНДМАРКИ ЭТИ!!!») живёт теперь у kSquadKinds и в строках ниже.
//
// Здесь остались ГРУППОВЫЕ законы — колонки, одинаковые у всей группы оси:
// проход рисовки ОДИН на все сквады, а то, чем город отличается от ходока, —
// КОЛОНКИ, и групповые из них ключуются АРХЕТИПОМ (`kArchetypeDraw` ниже).
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/table_guard.h"
#include "tables/squad_type.h"

namespace sm::ui {

// ── ГРУППОВЫЕ ЗАКОНЫ ПРЕДСТАВЛЕНИЯ — КЛЮЧ АРХЕТИПА ────────────────────────
// Архетип здесь работает ровно так, как разрешает его собственная шапка:
// КЛЮЧОМ ТАБЛИЦЫ, а не гейтом прохода. Четыре вопроса, ответ на которые
// одинаков у всей группы и потому не повторяется в каждой строке рода.
//
// РАЗНЫЙ КЛЮЧ — НЕ СПУТНИК. DOD п.9 запрещает таблицу-спутник по ТОМУ ЖЕ
// ключу; здесь ключи разные: `kSquadKinds` спрашивают родом («какая у НЕГО
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

// «НЕТ АРТА» НЕ ЗНАЧИТ «НЕ РИСУЕТСЯ» — ПОД КОМПИЛЯТОРОМ, А НЕ ПОД ПАМЯТЬЮ
// РЕВЬЮЕРА. Пять неподвижных родов несут `SpriteId::None` и метят карту
// глиф-кружком цветом своей строки; судить рисовку колонкой `sprite` —
// ошибка, стоившая наряду M-280 первой постановки задачи (2026-10-19).
// Страж стоит ЗДЕСЬ, а не у таблицы родов, потому что сводит ОБЕ половины
// закона: строку рода (kSquadKinds) и групповой закон её группы
// (pictureFromBody её архетипа).
static_assert(squad_kind(SquadType::Ruin).sprite == SpriteId::None
                  && squad_kind(SquadType::Ruin).maxPx > 0.0f
                  && !archetype_draw(squad_archetype(SquadType::Ruin))
                          .pictureFromBody,
              "безартовый неподвижный род рисуется глиф-кружком своей строки, "
              "а не фигуркой тела");

} // namespace sm::ui
