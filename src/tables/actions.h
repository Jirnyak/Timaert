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
#include "tables/spells.h"      // kSpellDefs — тождество отрезка прибито к каталогу

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
    Spell,  // магия: авторский отрезок ниже, построчно выровнен с kSpellDefs
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
    // Колонка castTime умерла В ЭТО ЧИСЛО ещё у спелла (fireball 2.0+0.3 →
    // 2.3): пред-актовая задержка и пост-актовый долг тратят ОДНО время боя,
    // закон один (вердикт 2026-09-09) — акт всегда мгновенен.
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

// ── МАГИЧЕСКИЙ ОТРЕЗОК — АВТОРСКИЙ; ДОМ УНИВЕРСАЛЬНЫХ ЧИСЕЛ ТЕПЕРЬ ЗДЕСЬ ──
//
// Здесь стоял ВЫВОД из kSpellDefs, и это была инверсия замысла — поправка
// владельца 2026-11-04, дословно: «нет же это както тупа ты делаешь формально
// одну систему а на деле внутри остаются и спелы и всё … почему просто не
// сделал универсльно способности?». Направление перевёрнуто 2026-11-05
// (брейншторм M-369, форма (А)): авторский дом БАЗЫ, ЦЕНЫ и ОХВАТА — таблица
// действий, а строка спелла несёт только своё (школу, кости, доставку);
// колонки recovery/manaCost/hasMicro/hasMacro у SpellDef снесены, так что
// второго дома этих чисел не существует ФИЗИЧЕСКИ, не дисциплиной.
//
// ПОРЯДОК СТРОК — ЗАКОН: отрезок выровнен с kSpellDefs ПОСТРОЧНО (ординал
// действия = kActionMagicFirst + ординал спелла), и тождество ключей прибито
// static_assert-ом ниже — разъезд порядка НЕСОБИРАЕМ. Авторская единица базы
// — секунды через constexpr-перевод в тики на месте строки (вердикт M-396:
// хранится ЦЕЛЫЙ тик, секунда живёт только в авторинге).
//
// Право каста даёт КНИГА (анкета), а не ранг: выучен — значит можно; поэтому
// gateSkill = SkillId::Count («рангом не ограничено») у всего отрезка —
// ровно вердикт о членстве (за набор отвечает система-владелец актора).
//
// Прежний страж «всякий спелл кастуем хоть где-то» умер ВМЕСТЕ с парой булей:
// ось охвата авторится enum-ом из трёх значений, четвёртое («нигде»)
// невыразимо по построению — стена стала типом.
inline constexpr ActionDef kMagicActions[] = {
    {.id = "fireball",        .recoveryTicks = steps_from_seconds(2.3f),
     .costHp = 0, .costMp = 60,   .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "ice_shard",       .recoveryTicks = steps_from_seconds(1.7f),
     .costHp = 0, .costMp = 30,   .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "magic_bolt",      .recoveryTicks = steps_from_seconds(0.0f),
     .costHp = 0, .costMp = 10,   .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroOnly},
    {.id = "lightning_chain", .recoveryTicks = steps_from_seconds(4.1f),
     .costHp = 0, .costMp = 60,   .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "energy_beam",     .recoveryTicks = steps_from_seconds(2.9f),
     .costHp = 0, .costMp = 100,  .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroOnly},
    {.id = "armageddon",      .recoveryTicks = steps_from_seconds(122.0f),
     .costHp = 0, .costMp = 1000, .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "haste",           .recoveryTicks = steps_from_seconds(0.0f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "flight",          .recoveryTicks = steps_from_seconds(0.0f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroMacro},
    {.id = "possession",      .recoveryTicks = steps_from_seconds(4.0f),
     .costHp = 0, .costMp = 200,  .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroOnly},
    {.id = "void_arrow",      .recoveryTicks = steps_from_seconds(0.8f),
     .costHp = 0, .costMp = 30,   .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Spell,   .scope = ActionScope::MicroOnly},
};

// ── ТАБЛИЦА И ЕЁ ОТРЕЗКИ ─────────────────────────────────────────────────
// Отрезки стоят в порядке лестницы. Отрезок нулевой длины — это ЗНАЧЕНИЕ
// («строк этого архетипа пока нет»), а не заглушка: ординалы при приходе
// нового отрезка ЗАКОННО сдвигаются (ЗАКОН ЛЕСТНИЦЫ п.3, формат сейва
// ограничением не является).
inline constexpr int kActionCombatFirst = 0;
inline constexpr int kActionCombatCount = 0;
inline constexpr int kActionMagicFirst  = kActionCombatFirst + kActionCombatCount;
// Счёт ВЫВОДИТСЯ из таблицы (ЗАКОН СЛОВАРЯ п.3: ручной счёт рядом с таблицей
// — второй словарь).
inline constexpr int kActionMagicCount  = int(std::size(kMagicActions));
inline constexpr int kActionCount       = kActionMagicFirst + kActionMagicCount;

// ── ТОЖДЕСТВО ОТРЕЗКА С КАТАЛОГОМ СПЕЛЛОВ — СТЕНА ПОД КОМПИЛЯТОРОМ ──────
// Ключ сверяется ПОСИМВОЛЬНО (указатели двух литералов сравнивать нельзя,
// и их равенство ничего бы не значило): каждая строка магического отрезка
// обязана стоять под ординалом СВОЕГО спелла и быть типом Spell.
inline constexpr bool action_id_eq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (*a != *b) return false;
    return *a == *b;
}
inline constexpr bool magic_segment_matches_spells() {
    if (kActionMagicCount != kSpellCount) return false;
    for (int i = 0; i < kSpellCount; ++i) {
        const ActionDef& a = kMagicActions[i];
        if (!action_id_eq(a.id, kSpellDefs[i].id)) return false;
        if (a.type != ActionType::Spell) return false;
    }
    return true;
}
static_assert(magic_segment_matches_spells(),
              "магический отрезок таблицы действий обязан идти СТРОКА В "
              "СТРОКУ с kSpellDefs (тот же ключ под тем же ординалом, тип "
              "Spell): красный assert значит, что порядок строк разъехался "
              "— чини ПОРЯДОК, а не стену");

static_assert(kActionMagicFirst == kActionCombatFirst + kActionCombatCount,
              "отрезки обязаны лежать ВПЛОТНУЮ: дыра между ними есть ординал "
              "без строки, то есть молчаливое «нет вещи» внутри адресного "
              "пространства");

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
