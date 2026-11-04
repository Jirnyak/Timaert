// ЕДИНАЯ ТАБЛИЦА ДЕЙСТВИЙ — ОДНА СТРОКА НА ВСЁ, ЧТО КТО-ЛИБО ДЕЛАЕТ.
//
// Вердикт владельца 2026-11-04, дословно: «У НАС УЖЕ ДОЛЖНО ПО ЗАМЫСЛУ БЫТЬ
// ЕДИНАЯ СИСТЕМА СПОСОБНОСТЕЙ/ДЕЙСТВИЙ/УМЕНИЙ ОНА ОДНА ТАМ И СПЕЛЫ И КРАФТЫ И
// РАБОТЫ И ПРОСТО ЛЮБЫЕ ДЕЙСТВИЯ ТИПА АТАКИ !!! ЭТО ЕДИНАЯ СИСТЕМА»; на вопрос
// об охвате первого шага — «Да, сразу оба мира»; и о слоях величины: «есть
// базовое рековери у каждого действия и есть эффективное рековери зи атрибутов
// скилов модифакиторов и контекста парметров (расширяемо)». Наряд M-369.
//
// ЗАЧЕМ ФАЙЛ СУЩЕСТВУЕТ, ЧИСЛОМ. База действия была названа ШЕСТЬЮ носителями:
// `recovery@src/tables/spells.h` (каст) · `useSeconds@src/tables/items.h`
// (зелье) · `cooldown@src/tables/army.h` (удар существа) ·
// `weapon_swing_seconds@src/tables/items.h` (удар оружием — выводится из массы,
// строки нет) · `actSeconds@src/sub/map_data.h` (дверь, обыск, чтение) ·
// `kHarvestActSeconds@src/sub/ability.h` (рубка — константа); у крафта и работы
// базы нет вовсе, и он завёл СВОЙ счётчик занятости. Шесть имён одной величины
// врут не теоретически: шапка самой двери восстановления обещала «the item
// row's recovery, the spell row's cooldown» — то есть путала имена своих же
// входов, потому что уследить за шестью нельзя.
//
// ПОЧЕМУ В `tables/`, А НЕ В ОДНОМ ИЗ МИРОВ. Два носителя из шести лежат под
// `src/sub/`, а `src/macro/` не вправе включать `src/sub/` (AGENTS §11) —
// значит «единая система» внутри одного из миров невозможна ПО ПОСТРОЕНИЮ, а
// не по недосмотру. `tables/` — нейтральный слой общих таблиц, который видят
// оба мира (ФРЕЙМ п.8: таблицы фреймом не являются, они уже по обе стороны).
//
// ЧЕГО В ЭТОЙ ТАБЛИЦЕ НЕТ И НЕ БУДЕТ — ЧЛЕНСТВА (вердикт владельца того же дня,
// дословно: «аватары это вополещние сквада таблица анкет так что за них
// отвечате их СИСТЕМА СКВАДОВ АНКЕТ / а мобы это таблца мобов то есть за нхи
// отвечат она / а спосбоднсти это АНОСТИЧНАЯ СИСТЕМА у всего что даёт
// рекавера»). За НАБОР доступных действий отвечает система-ВЛАДЕЛЕЦ актора;
// здесь нет и не появится колонки «кому это позволено». CANON, блок
// AI-МОДУЛИ, пункт 1б.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "core/time.h"          // steps_from_seconds — авторские секунды → ТИКИ, constexpr
#include "tables/attributes.h"  // SkillId — чей ранг пускает к строке
#include "tables/spells.h"      // kSpellDefs — магический отрезок ВЫВОДИТСЯ отсюда

namespace sm {

// ── ЛЕСТНИЦА: ДЕЙСТВИЯ → АРХЕТИПЫ → ТИПЫ → СТРОКИ ────────────────────────
//
// Четыре архетипа названы владельцем (бой · магия · труд · интеракция). Вверх
// — колонка, вниз — ОТРЕЗОК: «все строки архетипа» есть непрерывный диапазон
// ординалов, никогда фильтр по колонке (ЗАКОН ЛЕСТНИЦЫ п.2).
enum class ActionArchetype : std::uint8_t {
    Combat,       // удар, выстрел — руки
    Magic,        // каст любой школы
    Labour,       // добыча, крафт, работа — тоже руки
    Interaction,  // дверь, обыск, глоток, рычаг (до рождения диалог-системы)
    Count
};

// ТИПЫ приходят ПО ЛОМТЮ, вместе со своими строками. Перечислять тип, у
// которого ещё нет ни одной строки, запрещено: это заглушка (§1 ZERO MOCKS), а
// лестница судится не длиной перечисления, а непрерывностью отрезков.
enum class ActionType : std::uint8_t {
    Spell,  // магия: строки ВЫВЕДЕНЫ из kSpellDefs (ниже)
    Count
};

// АРХЕТИП ВЫВОДИТСЯ ИЗ ТИПА ОТРЕЗКОМ — ноль хранимых байт (DOD п.9:
// выводимое не хранится). Форма та же, что у `squad_archetype@src/tables/squad_type.h`:
// границы групп стоят здесь и только здесь.
inline constexpr ActionType kActionTypeMagicFirst = ActionType::Spell;
inline constexpr ActionType kActionTypeMagicLast  = ActionType::Spell;

inline constexpr ActionArchetype archetype_of(ActionType t) {
    return (t >= kActionTypeMagicFirst && t <= kActionTypeMagicLast)
               ? ActionArchetype::Magic
               : ActionArchetype::Combat;  // недостижимо, пока тип один
}
static_assert(archetype_of(ActionType::Spell) == ActionArchetype::Magic,
              "магический отрезок обязан накрывать свой единственный тип: "
              "разъезд лестницы с группами не ловится компилятором иначе");

// ДОМЕН ТЕМПА — ЭТО СВОЙСТВО АРХЕТИПА, А НЕ КОЛОНКА СТРОКИ, и здесь умирает
// тернарник `cast ? SkillId::Spellcraft : SkillId::Armsmaster`, девять раз
// подставлявшийся звонящими. Генерик-пара двигает ТЕМП (CANON S14 «один рычаг
// на ручку»), типовой скилл остаётся рычагом СИЛЫ — одна ручка в двух итогах
// давала бы скрытый квадрат (замерено: ×121 DPS одним вложением).
inline constexpr SkillId tempo_generic(ActionArchetype a) {
    return a == ActionArchetype::Magic ? SkillId::Spellcraft
                                       : SkillId::Armsmaster;
}
static_assert(tempo_generic(ActionArchetype::Magic) == SkillId::Spellcraft);
static_assert(tempo_generic(ActionArchetype::Combat) == SkillId::Armsmaster);
static_assert(tempo_generic(ActionArchetype::Labour) == SkillId::Armsmaster,
              "труд — это руки: добыча и крафт платят Армсмастером, а не "
              "своей третьей ручкой");

// ── ОСЬ ОХВАТА ───────────────────────────────────────────────────────────
// «со спецификой только макро только микро и микромакро» (владелец,
// 2026-11-01). Здесь умирает пара булевых колонок спелла `hasMicro`/`hasMacro`:
// два бита на три законных значения держали и ЧЕТВЁРТОЕ — «нигде», то есть
// строку, которую нельзя применить ни одним миром. Такая строка незаконна, и
// теперь это проверяет компилятор (static_assert ниже), а не ревью.
enum class ActionScope : std::uint8_t { MicroOnly, MacroOnly, MicroMacro };

// ── СТРОКА ДЕЙСТВИЯ ──────────────────────────────────────────────────────
struct ActionDef {
    // Авторский ключ — ТОЛЬКО граница мира (ЗАКОН СЛОВАРЯ п.1: внутри
    // симуляции живут ординалы, строки стоят на входе).
    const char* id;

    // БАЗОВОЕ РЕКОВЕРИ, В ЦЕЛЫХ ТИКАХ. Первый слой величины: эффективное
    // считает ОДНА дверь (`recovery_steps@src/bus/anketa.h`) из этой базы,
    // резвости Spd, ранга генерика домена и модификаторов контекста.
    // Тик — квант времени мира (`kTicksPerRealSecond@src/core/time.h` = 64), и
    // шаг восстановления субмира есть ТО ЖЕ ЧИСЛО по построению
    // (`kStepsPerSecond@src/core/time.h`), поэтому одна колонка служит обоим
    // миром и секундам внутри симуляции делать нечего (ЗАКОН ТИПА п.5).
    // Ширина: 32 бита — это ВЕЛИЧИНА, а не индекс, и сужать её ради байтов
    // запрещено (DOD п.2); авторские 122 с дают 7808 тиков, запас тридцатикратный.
    std::uint32_t recoveryTicks;

    // ЦЕНА — ТРИ КОЛОНКИ, И НОЛЬ В НИХ ЗАКОННОЕ ЗНАЧЕНИЕ, А НЕ ОТСУТСТВИЕ
    // (ЗАКОН СПОСОБНОСТИ п.2). Знаковый тип — не страх беззнакового, а тип
    // пула, из которого цена вычитается: смешанных сравнений на пути траты
    // быть не должно (ЗАКОН ТИПА п.2-3).
    std::int32_t costHp;
    std::int32_t costMp;
    std::int32_t costSp;

    // ПРАВО НА СТРОКУ — ТИПОВОЙ скилл и его минранг. Это ДРУГОЙ вопрос, чем
    // домен темпа выше: форма уже работает у рецептов (`craft` + `minRank` в
    // `RecipeDef@src/macro/econ_day.h`). `SkillId::Count` значит «рангом не
    // ограничено» — прецедент тот же, что у спящей школы тега.
    SkillId      gateSkill;
    std::uint8_t minRank;

    // Ступень лестницы. Архетип НЕ хранится — он выводится отрезком.
    ActionType  type;
    ActionScope scope;
};
static_assert(sizeof(ActionDef) == 32,
              "раскладка строки действия закреплена: 8 ключ + 4 база + 12 цена "
              "+ 2 право + 2 лестница, и четыре байта добивает выравнивание "
              "указателя (DOD: у структуры, чей размер назван, размер под "
              "static_assert)");
static_assert(std::is_trivially_copyable_v<ActionDef>);

// ── МАГИЧЕСКИЙ ОТРЕЗОК ВЫВОДИТСЯ ИЗ kSpellDefs, А НЕ АВТОРИТСЯ ВТОРЫМ КЛЮЧОМ ──
//
// «Спелл просто частный случай» (владелец) здесь буквален: ординал действия
// спелла есть начало магического отрезка плюс его собственный ординал, а
// универсальные колонки СЧИТАЮТСЯ из его авторской строки на сборке. Поэтому
// таблицы-спутника с тем же ключом не возникает (DOD п.9), и разъехаться с
// источником невыразимо: выведенная колонка не может соврать о своём авторе.
inline constexpr ActionScope spell_scope(const SpellDef& s) {
    return (s.hasMicro && s.hasMacro) ? ActionScope::MicroMacro
           : s.hasMicro               ? ActionScope::MicroOnly
                                      : ActionScope::MacroOnly;
}

inline constexpr bool every_spell_is_castable_somewhere() {
    for (int i = 0; i < kSpellCount; ++i)
        if (!kSpellDefs[i].hasMicro && !kSpellDefs[i].hasMacro) return false;
    return true;
}
static_assert(every_spell_is_castable_somewhere(),
              "строка, которую нельзя применить ни в одном мире, незаконна: "
              "ось охвата несёт ТРИ значения, и четвёртого — «нигде» — у неё "
              "нет. Красный static_assert значит, что в kSpellDefs завелась "
              "мёртвая строка, а не что стена неверна");

inline constexpr std::array<ActionDef, std::size_t(kSpellCount)> make_magic_actions() {
    std::array<ActionDef, std::size_t(kSpellCount)> out{};
    for (int i = 0; i < kSpellCount; ++i) {
        const SpellDef& s = kSpellDefs[i];
        out[std::size_t(i)] = ActionDef{
            .id            = s.id,
            .recoveryTicks = steps_from_seconds(s.recovery),
            .costHp        = 0,
            .costMp        = s.manaCost,
            .costSp        = 0,
            // Право каста даёт КНИГА (анкета), а не ранг: выучен — значит
            // можно. Это ровно вердикт о членстве — отвечает система анкет.
            .gateSkill     = SkillId::Count,
            .minRank       = 0,
            .type          = ActionType::Spell,
            .scope         = spell_scope(s),
        };
    }
    return out;
}

// ── ТАБЛИЦА И ЕЁ ОТРЕЗКИ ─────────────────────────────────────────────────
// Отрезки стоят в порядке лестницы. Отрезок нулевой длины — это ЗНАЧЕНИЕ
// («строк этого архетипа пока нет»), а не заглушка: ординалы при приходе
// нового отрезка ЗАКОННО сдвигаются (ЗАКОН ЛЕСТНИЦЫ п.3, формат сейва
// ограничением не является).
inline constexpr auto kMagicActions = make_magic_actions();

inline constexpr int kActionCombatFirst = 0;
inline constexpr int kActionCombatCount = 0;
inline constexpr int kActionMagicFirst  = kActionCombatFirst + kActionCombatCount;
inline constexpr int kActionMagicCount  = kSpellCount;
inline constexpr int kActionCount       = kActionMagicFirst + kActionMagicCount;

static_assert(kActionMagicFirst == kActionCombatFirst + kActionCombatCount,
              "отрезки обязаны лежать ВПЛОТНУЮ: дыра между ними есть ординал "
              "без строки, то есть молчаливое «нет вещи» внутри адресного "
              "пространства");
static_assert(kActionCount == int(kMagicActions.size()) + kActionCombatCount);

// Ординал → строка. Гейт — вопрос КАТАЛОГА, ровно как у спеллов
// (`spell_ordinal_valid@src/tables/spells.h`).
inline constexpr bool action_ordinal_valid(int ord) {
    return ord >= 0 && ord < kActionCount;
}

inline constexpr const ActionDef& action_def(int ord) {
    return kMagicActions[std::size_t(ord - kActionMagicFirst)];
}

// Спелл → его действие. Одна дверь перевода, чтобы «частный случай» не
// превратился в арифметику по месту вызова.
inline constexpr int action_of_spell(int spellOrd) {
    return kActionMagicFirst + spellOrd;
}

// Домен темпа строки — через лестницу, без ветки у звонящего.
inline constexpr SkillId action_tempo_generic(const ActionDef& a) {
    return tempo_generic(archetype_of(a.type));
}

} // namespace sm
