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
#include "tables/econ_laws.h"   // kGatherPerWorkerDay — якорь темпа труда (S10)
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
    Strike, // бой: замах тела — пустая рука; масса оружия говорит ДОБАВКОЙ
    Spell,  // магия: авторский отрезок ниже, построчно выровнен с kSpellDefs
    Gather, // труд: выемка стока мира — жатва, охота, рубка, копка, поимка
    Craft,  // труд: партия у верстака — спецтаблица рецептов ссылается сюда
    Count
};

// АРХЕТИП ВЫВОДИТСЯ ИЗ ТИПА ОТРЕЗКОМ — ноль хранимых байт (DOD п.9:
// выводимое не хранится). Форма та же, что у `squad_archetype@src/tables/squad_type.h`:
// границы групп стоят здесь и только здесь.
inline constexpr ActionType kActionTypeMagicFirst = ActionType::Spell;
inline constexpr ActionType kActionTypeMagicLast  = ActionType::Spell;
inline constexpr ActionType kActionTypeLabourFirst = ActionType::Gather;
inline constexpr ActionType kActionTypeLabourLast  = ActionType::Craft;

inline constexpr ActionArchetype archetype_of(ActionType t) {
    return (t >= kActionTypeMagicFirst && t <= kActionTypeMagicLast)
               ? ActionArchetype::Magic
           : (t >= kActionTypeLabourFirst && t <= kActionTypeLabourLast)
               ? ActionArchetype::Labour
               : ActionArchetype::Combat;
}
static_assert(archetype_of(ActionType::Spell) == ActionArchetype::Magic,
              "магический отрезок обязан накрывать свой единственный тип: "
              "разъезд лестницы с группами не ловится компилятором иначе");
static_assert(archetype_of(ActionType::Strike) == ActionArchetype::Combat,
              "удар — боевой архетип: его темп платит Армсмастер, не "
              "Спеллкрафт");
static_assert(archetype_of(ActionType::Gather) == ActionArchetype::Labour,
              "добыча — трудовой архетип: её темп платит Армсмастер (руки), "
              "а цену — закон бара (S14.1), не плоская колонка");
static_assert(archetype_of(ActionType::Craft) == ActionArchetype::Labour,
              "крафт — тот же трудовой архетип, что добыча: один закон цены "
              "(бар/темп), один домен темпа; вторая лестница труда не "
              "заводится");

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

    // ЦЕНА ТРУДА — ТЕМПОМ, НЕ ПЛОСКОЙ КОЛОНКОЙ (CANON S14.1, носитель закона
    // `sp_price@src/tables/econ_laws.h`: «ratePerDay is a COLUMN of whatever
    // is acting» — у добычи действует сама строка). Цена акта = бар / это
    // число актов в день, и платится она дверью ИСТОЩЕНИЯ В МИНУС
    // (`apply_stamina_cost`): отказа у труда нет, пустой бар кусает HP.
    // 0 — ЗНАЧЕНИЕ «цена не темповая»: правят плоские колонки выше, sp_price
    // к строке не зовётся; оба направления прибиты static_assert ниже.
    std::int32_t spRatePerDay;

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
              "+ 4 темп цены + 2 право + 2 лестница — ровно 32, колонка темпа "
              "заняла бывший паддинг указателя (DOD: у структуры, чей размер "
              "назван, размер под static_assert)");
static_assert(std::is_trivially_copyable_v<ActionDef>);

// ── БОЕВОЙ ОТРЕЗОК — ПЕРВЫЕ СТРОКИ (M-396) ───────────────────────────────
//
// `strike` — ЗАМАХ ПУСТОЙ РУКИ, самый медленный боец игры при нулевом листе.
// Вердикт владельца 2026-11-04 (CANON S14 «БАЗОВОЕ И ЭФФЕКТИВНОЕ РЕКОВЕРИ»):
// база удара есть замах ПУСТОЙ РУКИ, а масса оружия — АДДИТИВНАЯ добавка в
// тиках (`weapon_mass_recovery_ticks@src/tables/items.h`), не множитель.
// Сюда умерла константа kHandSwingS = 1.5f: у действия теперь СТРОКА, как
// велит ЗАКОН СПОСОБНОСТИ п.1, и 1.5 с = 96 тиков — то же число мира.
//
// Цена 0/0/0 — ЗНАЧЕНИЕ, не отсутствие (ЗАКОН СПОСОБНОСТИ п.2): удар сегодня
// бесплатен, цена SP придёт правкой колонки, не новой веткой. Право не
// гейтится рангом (SkillId::Count): бить может всякий, Армсмастер двигает
// ТЕМП через генерик домена, не право.
//
// Естественный удар СУЩЕСТВА строки здесь не имеет (вердикт владельца
// 2026-11-06, форма (а)): его база — колонка `recoveryTicks` ЕГО строки
// (`CombatTemplate@src/tables/army.h`) — «у массовки всё из его таблицы»
// (ЗАКОН ДВУХ ТАБЛИЦ), слитая с этой колонкой по имени, типу и единице.
// Авторские удары с колонками стана/отброса лягут сюда строками, когда
// придут их модули (брейншторм M-369: «у боя малая таблица ударов»).
inline constexpr ActionDef kCombatActions[] = {
    {.id = "strike",          .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Strike,  .scope = ActionScope::MicroMacro},
};

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

// ── ТРУДОВОЙ ОТРЕЗОК — ДОБЫЧА (ломоть «Добыча» M-369) ───────────────────
//
// ОДНА СТРОКА — ОБА МИРА (замысел владельца 2026-11-06: сквад с модулем
// добычи «через ту же способность меняет значение в поле — то есть единая
// системность»): внизу акт исполняет ТЕЛО (`harvest_action@src/sub/engine.cpp`
// читает строку по роду пропа в руке), наверху — МОДУЛЬ сквада (спецтаблица
// добычи `kGathererDefs@src/macro/npc_ai.cpp` ссылается ординалом). ЧТО
// считается целью — дело исполнителя своего мира (проп / клетка worksite);
// какой СТОК покидает мир и ГДЕ работать — колонки спецтаблицы: реестр
// полей — слой macro, и L0 его не видит ПО ПОСТРОЕНИЮ (§11). Это та же
// форма «спецтаблицы-половины одного пространства ординалов», какой магия
// делит строку с kSpellDefs.
//
// ОБЪЁМ ЗА АКТ — ЕДИНИЦА ЗАКОНОМ, НЕ КОЛОНКОЙ (вердикт 2026-09-16: «рука
// берёт один объект всегда»; артель умножает РУКАМИ, не строкой): колонка
// сплошных единиц не имела бы различающего читателя (DOD п.9).
//
// ТЕМП ИСТОЧНИКА — spRatePerDay, он и цена (бар/темп), и дневная норма
// одного работника; якорь — kGatherPerWorkerDay («1 добытчик кормит 32
// души», CANON S10). Пашня и лес — полный якорь; охота и лён — четверть
// (зверь бегает, а колос стоит; та же земля, своя культура — числа-крутилки
// дубль-прогона, переехавшие из спецтаблицы ЦЕЛИКОМ, не сдвинутые).
//
// БАЗА — ЗАМАХ РУКИ: та же нижняя граница strike, что держала умершую здесь
// константу kHarvestActSeconds (1.5 с = 96 тиков; страж — в проверке
// отрезка ниже). ПОИМКА ЛОШАДИ — акт ценой в ЦЕЛЫЙ бар (темп 1, S10
// «ЛОШАДЬ — ЮНИТ»: поимка стоит работнику ДЕНЬ), и её база — рабочий ДЕНЬ
// тиками: «сделал → вишу», длительность ЕСТЬ восстановление (ЗАКОН
// СПОСОБНОСТИ п.1); макро прочтёт эту базу счётчиком, когда stateTimer
// умрёт в рековери (поздний ломоть M-369 по ряду).
inline constexpr ActionDef kLabourActions[] = {
    {.id = "reap_wheat",      .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = kGatherPerWorkerDay,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MicroMacro},
    {.id = "hunt_fauna",      .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = kGatherPerWorkerDay / 4,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MacroOnly},
    {.id = "reap_flax",       .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = kGatherPerWorkerDay / 4,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MacroOnly},
    {.id = "mine_vein",       .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = kGatherPerWorkerDay,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MacroOnly},
    {.id = "fell_tree",       .recoveryTicks = steps_from_seconds(1.5f),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = kGatherPerWorkerDay,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MicroMacro},
    {.id = "catch_livestock", .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 1,
     .gateSkill = SkillId::Count, .minRank = 0,
     .type = ActionType::Gather,  .scope = ActionScope::MacroOnly},

    // ── КРАФТ (ломоть «крафт + работа» M-369) ────────────────────────────
    // ОДНА СТРОКА — ОБА МИРА: наверху день места назначает руки по этим же
    // строкам (`econ_produce_day@src/macro/econ_day.cpp`), внизу верстак руки
    // ценит партию тем же темпом (вкладка Craft, `src/ui/overlays.cpp`).
    // Спецтаблица рецептов (`kRecipes@src/macro/econ_day.h`) ссылается сюда
    // ординалом и несёт только СВОЁ — ЧТО выходит из партии; состав и выход
    // остаются строкой каталога (ЗАКОН ДВУХ ТАБЛИЦ).
    //
    // ТЕМП — ПАРТИЙ НА ЧЕЛОВЕКО-ДЕНЬ, перенесён ДОСЛОВНО из умершей колонки
    // `perDay` авторской таблицы материи (items.cpp kPartsAuthoring):
    // self-play баланс держится ровно на этих числах, перенос — не сдвиг.
    // Цена партии руки = бар/темп (sp_price), как у всякого труда.
    //
    // ПРАВО — ПЕРВЫЕ СТРОКИ ТАБЛИЦЫ С НЕНУЛЕВЫМ ГЕЙТОМ: пара «ремесло +
    // минранг» переехала сюда из колонок рецепта (`craft`/`minRank`), и
    // теперь город и рука спрашивают ОДНУ строку (гейт руки = гейт города,
    // M-146: игрок с нулевым кузнечным монету больше не чеканит). Ранги
    // оружия/кожи/щитов — новый авторинг, баланс; деревообработки отдельным
    // ремеслом нет (пять ремёсел — слово владельца), дерево квартирует у
    // masonry («что сложено и сколочено»), как мебель и резьба.
    //
    // БАЗА — РАБОЧИЙ ДЕНЬ ТИКАМИ («сделал → вишу», M-326: выпуск падает
    // мгновенно, затем висит восстановление; квантование партий отклонено
    // владельцем). Макро сегодня держит тот же день своим счётчиком
    // (`stateTimer = kCraftThinksPerDay`) и прочтёт эту базу, когда счётчик
    // умрёт в рековери (поздний ломоть M-369).
    {.id = "mint_coin",       .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 4,
     .gateSkill = SkillId::Blacksmith, .minRank = 30,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "fire_bricks",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 8,
     .gateSkill = SkillId::Masonry, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "weave_cloth",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 4,
     .gateSkill = SkillId::Tailoring, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_tools",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Blacksmith, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "join_furniture",  .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Masonry, .minRank = 10,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "build_wagon",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 1,
     .gateSkill = SkillId::Blacksmith, .minRank = 15,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "cut_jewelry",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 1,
     .gateSkill = SkillId::Blacksmith, .minRank = 20,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "carve_figure",    .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Masonry, .minRank = 5,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "hew_statue",      .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 1,
     .gateSkill = SkillId::Masonry, .minRank = 25,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "brew_potion_hp",  .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 8,
     .gateSkill = SkillId::Alchemy, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "brew_potion_mp",  .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 8,
     .gateSkill = SkillId::Alchemy, .minRank = 10,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_dagger",    .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 4,
     .gateSkill = SkillId::Blacksmith, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_sword",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Blacksmith, .minRank = 5,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_spear",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Blacksmith, .minRank = 3,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_axe",       .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Blacksmith, .minRank = 5,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_mace",      .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Blacksmith, .minRank = 8,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "carve_staff",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 4,
     .gateSkill = SkillId::Masonry, .minRank = 1,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "carve_bow",       .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Masonry, .minRank = 5,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "sew_leather",     .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Tailoring, .minRank = 5,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "forge_buckler",   .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 4,
     .gateSkill = SkillId::Blacksmith, .minRank = 3,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "bind_round_shield", .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 2,
     .gateSkill = SkillId::Masonry, .minRank = 3,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
    {.id = "bind_tower_shield", .recoveryTicks = std::uint32_t(kTicksPerDay),
     .costHp = 0, .costMp = 0,    .costSp = 0,
     .spRatePerDay = 1,
     .gateSkill = SkillId::Masonry, .minRank = 8,
     .type = ActionType::Craft,   .scope = ActionScope::MicroMacro},
};

// ── ТАБЛИЦА И ЕЁ ОТРЕЗКИ ─────────────────────────────────────────────────
// Отрезки стоят в порядке лестницы. Отрезок нулевой длины — это ЗНАЧЕНИЕ
// («строк этого архетипа пока нет»), а не заглушка: ординалы при приходе
// нового отрезка ЗАКОННО сдвигаются (ЗАКОН ЛЕСТНИЦЫ п.3, формат сейва
// ограничением не является).
// Счёт ВЫВОДИТСЯ из таблиц (ЗАКОН СЛОВАРЯ п.3: ручной счёт рядом с таблицей
// — второй словарь).
inline constexpr int kActionCombatFirst = 0;
inline constexpr int kActionCombatCount = int(std::size(kCombatActions));
inline constexpr int kActionMagicFirst  = kActionCombatFirst + kActionCombatCount;
inline constexpr int kActionMagicCount  = int(std::size(kMagicActions));
inline constexpr int kActionLabourFirst = kActionMagicFirst + kActionMagicCount;
inline constexpr int kActionLabourCount = int(std::size(kLabourActions));
inline constexpr int kActionCount       = kActionLabourFirst + kActionLabourCount;

// Именованный ординал замаха: его базу читают руки
// (`hand_strike_fields@src/bus/anketa.cpp` — пустая рука плюс добавка массы).
inline constexpr int kActionStrike = kActionCombatFirst;

// Именованные ординалы добычи СУБМИРА: резолвер цели сцены (`harvest_action@
// src/sub/engine.cpp` — колос жнут, прочее под топором рубят) читает их
// строки напрямую, потому что спецтаблица добычи ему недоступна по слою
// (она macro). Макро именованных ординалов не держит — он ссылается
// колонкой спецтаблицы.
inline constexpr int kActionReapWheat = kActionLabourFirst + 0;
inline constexpr int kActionFellTree  = kActionLabourFirst + 4;

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
    return ord < kActionMagicFirst
               ? kCombatActions[std::size_t(ord - kActionCombatFirst)]
           : ord < kActionLabourFirst
               ? kMagicActions[std::size_t(ord - kActionMagicFirst)]
               : kLabourActions[std::size_t(ord - kActionLabourFirst)];
}
static_assert(action_id_eq(action_def(kActionStrike).id, "strike"),
              "именованный ординал замаха обязан смотреть на свою строку: "
              "сдвиг боевого отрезка без правки kActionStrike несобираем");
static_assert(action_id_eq(action_def(kActionReapWheat).id, "reap_wheat")
                  && action_id_eq(action_def(kActionFellTree).id, "fell_tree"),
              "именованные ординалы добычи обязаны смотреть на свои строки: "
              "перестановка трудового отрезка без их правки несобираема");

// ── ЦЕЛОСТНОСТЬ ТРУДОВОГО ОТРЕЗКА — СТЕНА ПОД КОМПИЛЯТОРОМ ──────────────
// Четыре закона одной проверкой: (1) тип строк — трудовой (Gather | Craft),
// и типы идут ГРУППАМИ без чересполосицы (ЗАКОН ЛЕСТНИЦЫ п.2: «все строки
// типа» — непрерывный диапазон ординалов); (2) цена труда ТЕМПОВАЯ
// (rate > 0) при плоских нулях-значениях — два закона цены на одной строке
// невыразимы; (3) акт не быстрее пустого замаха — у топора есть масса, и
// она не бывает меньше нуля (страж умершей константы kHarvestActSeconds,
// теперь на ВЕСЬ отрезок).
inline constexpr bool labour_segment_is_rate_priced() {
    ActionType prev = kActionTypeLabourFirst;
    for (int i = 0; i < kActionLabourCount; ++i) {
        const ActionDef& a = kLabourActions[std::size_t(i)];
        if (archetype_of(a.type) != ActionArchetype::Labour) return false;
        if (a.type < prev) return false;  // чересполосица типов — разрыв отрезка
        prev = a.type;
        if (a.spRatePerDay <= 0) return false;
        if (a.costHp != 0 || a.costMp != 0 || a.costSp != 0) return false;
        if (a.recoveryTicks < action_def(kActionStrike).recoveryTicks)
            return false;
    }
    return true;
}
static_assert(labour_segment_is_rate_priced(),
              "трудовой отрезок: типы Gather|Craft непрерывными группами, "
              "цена темпом (бар/rate, плоские нули — значения), акт не "
              "быстрее замаха strike");

// Границы КРАФТ-подотрезка — ВЫВОДЯТСЯ из таблицы (ЗАКОН СЛОВАРЯ п.3:
// ручной счёт рядом с таблицей — второй словарь). Их читает стена
// спецтаблицы рецептов (`kRecipes@src/macro/econ_day.h`): ссылка-ординал
// мимо крафтового подотрезка несобираема.
inline constexpr int craft_action_count_() {
    int n = 0;
    for (int i = 0; i < kActionLabourCount; ++i)
        if (kLabourActions[std::size_t(i)].type == ActionType::Craft) ++n;
    return n;
}
inline constexpr int kActionCraftCount = craft_action_count_();
static_assert(kActionCraftCount > 0,
              "тип Craft перечислен — значит у него обязаны быть строки "
              "(шапка ActionType: тип без строк — заглушка)");

// ...и обратная стена: ВНЕ труда темповой цены не существует — ноль там
// есть значение «цена не темповая», и строка с обоими законами цены сразу
// несобираема.
inline constexpr bool rate_price_lives_only_in_labour() {
    for (int i = 0; i < kActionCount; ++i) {
        const ActionDef& a = action_def(i);
        if (archetype_of(a.type) != ActionArchetype::Labour
            && a.spRatePerDay != 0) return false;
    }
    return true;
}
static_assert(rate_price_lives_only_in_labour(),
              "темповая цена — закон ТРУДА: у боевой и магической строки "
              "spRatePerDay обязан быть нулём-значением");

// Спелл → его действие. Одна дверь перевода, чтобы «частный случай» не
// превратился в арифметику по месту вызова.
inline constexpr int action_of_spell(int spellOrd) {
    return kActionMagicFirst + spellOrd;
}
// ...и обратная: ординал магического отрезка → строка спелла. Пара дверей,
// никогда вычитание по месту вызова.
inline constexpr int spell_of_action(int actionOrd) {
    return actionOrd - kActionMagicFirst;
}

// ── НАБОРЫ: ЧЕМ СТРОКА-ВЛАДЕЛЕЦ ССЫЛАЕТСЯ НА ДЕЙСТВИЯ ────────────────────
// Вердикт владельца 2026-11-06, дословно: «существа берут что они могут
// делать в свои таблицы и модули из неё» — набор актора есть МАЛЫЙ МАССИВ
// ординалов этой таблицы в СТРОКЕ владельца (membership здесь не живёт,
// шапка файла). «Нет действия» — ПОСЛЕДНЕЕ значение узкого типа хранения,
// не −1 (ЗАКОН УЗКОГО ИНДЕКСА: слот 0 — законная строка strike).
inline constexpr std::uint8_t kActionNone = 255;
static_assert(kActionCount < int(kActionNone),
              "пространство ординалов действий переросло u8-носитель "
              "наборов: подними тип слота набора вместе с сентинелом");
// Слотов в наборе строки существа — 2: наибольший сегодняшний набор (один
// спелл) плюс слот второго действия; балансное число, поднимается строкой.
inline constexpr int kCreatureActionSlots = 2;

// Авторский ключ → ординал ДЕЙСТВИЯ для u8-слота набора, constexpr: резолв
// происходит В КОМПИЛЯТОРЕ (ЗАКОН СЛОВАРЯ п.1). Неизвестный ключ в
// constexpr-авторинге НЕСОБИРАЕМ: выход за границу таблицы — не константное
// выражение.
inline constexpr std::uint8_t action_slot(const char* id) {
    for (int i = 0; i < kActionCount; ++i)
        if (action_id_eq(action_def(i).id, id)) return std::uint8_t(i);
    return std::uint8_t(
        kMagicActions[std::size_t(kActionCount)].recoveryTicks);
}
static_assert(action_slot("strike") == std::uint8_t(kActionStrike),
              "дверь авторинга наборов обязана находить свою строку");

// Домен темпа строки — через лестницу, без ветки у звонящего.
inline constexpr SkillId action_tempo_generic(const ActionDef& a) {
    return tempo_generic(archetype_of(a.type));
}

} // namespace sm
