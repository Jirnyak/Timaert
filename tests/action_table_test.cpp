// ЕДИНАЯ ТАБЛИЦА ДЕЙСТВИЙ, ЛОМОТЬ 1 (M-369): что здесь охраняется ЗАКОНОМ.
//
// Прибор судит ЧЕТЫРЕ закона, а не сегодняшние числа каталога:
//
//   1. МАГИЧЕСКИЙ ОТРЕЗОК — АВТОРСКИЙ И ВЫРОВНЕН С КАТАЛОГОМ СПЕЛЛОВ
//      ПОСТРОЧНО (переворот 2026-11-05, форма (А): дом базы/цены/охвата —
//      таблица действий; поправка владельца — «почему просто не сделал
//      универсльно способности?»). Тождество порядка держит static_assert в
//      самой таблице; прибор судит РАНТАЙМ-двери: ординал туда и обратно
//      возвращает ту же строку.
//   2. ДВА СЛОЯ ВЕЛИЧИНЫ (CANON S14 «БАЗОВОЕ И ЭФФЕКТИВНОЕ РЕКОВЕРИ»): база —
//      колонка строки в ЦЕЛЫХ ТИКАХ, эффективное — ОДНА дверь над ней.
//      Нетренированный лист не двигает базу ВОВСЕ; тренированный двигает её
//      ВНИЗ. Утверждается ОТНОШЕНИЕ, а не откалиброванное число (§8 п.4).
//   3. ДОМЕН ТЕМПА ДАЁТ ЛЕСТНИЦА, А НЕ ЗВОНЯЩИЙ. Здесь умер тернарник
//      `cast ? Spellcraft : Armsmaster`, девять раз подставлявшийся по месту.
//   4. ЦЕНА — ТРИ КОЛОНКИ, ОДНА ДВЕРЬ, ВСЁ ИЛИ НИЧЕГО. Ноль есть ЗНАЧЕНИЕ
//      (ЗАКОН СПОСОБНОСТИ п.2), а нехватка не трогает НИ ОДИН пул — это
//      негативный контроль на снятый кламп `if (mp < 0) mp = 0`, которым
//      полуплатёж был выразим.
//
//   5. БОЕВОЙ ОТРЕЗОК (ломоть «базы удара и существа», M-396): база удара —
//      замах ПУСТОЙ РУКИ строкой `strike`, масса оружия — АДДИТИВНАЯ добавка
//      в тиках, база существа — колонка ЕГО строки той же единицей (форма
//      (а), вердикт 2026-11-06), а чем строка кастует — говорит её НАБОР
//      ординалов действий (сентинел −1 умер в kActionNone).
//
//   6. ТРУДОВОЙ ОТРЕЗОК (ломоть «Добыча» M-369): цена труда — ТЕМПОМ
//      (бар / spRatePerDay строки, закон S14.1), и строка ОДНА на оба мира —
//      топор субмира и цикл артели ценят ею же. Якорь мира: «1 добытчик
//      кормит 32 души» (kGatherPerWorkerDay, CANON S10). Отношения, не
//      литералы: зверь вчетверо дороже колоса, поимка лошади — целый бар.
//
// ЯКОРИ — НЕ ПЕРЕСЧЁТ ПРОДАКШЕН-ФОРМУЛЫ (§8 п.5-6), А НЕЗАВИСИМО ИЗВЕСТНЫЕ
// ФАКТЫ МИРА: квант времени мира есть 64 тика в секунде
// (`kTicksPerRealSecond@src/core/time.h`), поэтому авторские 2.3 с fireball
// обязаны дать 147 тиков (2.3 × 64 = 147.2, округление к ближайшему), а 122 с
// armageddon — 7808. Если каталог перебалансируют, якорь упадёт и его
// ПЕРЕПИШУТ вслух — это и есть разница между балансом и законом.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» — в теле коммита ломтя.

#include "check.h"

#include "bus/anketa.h"       // recovery_steps / pay_action_cost — дверь и цена
#include "core/pools.h"       // ecs::Pools — тройка HP/MP/SP
#include "core/time.h"        // kTicksPerRealSecond — квант, на котором стоят якори
#include "tables/actions.h"   // ЕДИНАЯ строка действия
#include "tables/econ_laws.h" // sp_price/kGatherPerWorkerDay — закон цены труда
#include "macro/econ_day.h"   // kRecipes — спецтаблица-половина крафта (M-369)
#include "tables/items.h"     // weapon_mass_recovery_ticks — добавка массы (M-396)
#include "tables/npc.h"       // kWitchCombat/kPeasantCombat — наборы строк существ

#include <cstdio>
#include <cstring>

namespace {

using sm::ActionArchetype;
using sm::ActionScope;
using sm::ActionType;
using sm::SkillId;

// ── 1. Отрезок и ординал: авторский отрезок выровнен с каталогом ──────────
void segment_is_aligned_with_spells() {
    CHECK(sm::kActionMagicCount == sm::kSpellCount,
                    "магический отрезок накрывает ВСЕ строки спеллов");
    CHECK(sm::kActionCount == sm::kActionCombatCount + sm::kActionMagicCount
                                  + sm::kActionLabourCount,
                    "ординальное пространство действий есть сумма отрезков");
    CHECK(sm::kActionLabourFirst == sm::kActionMagicFirst + sm::kSpellCount,
                    "трудовой отрезок стоит ВПЛОТНУЮ за магическим: дыра "
                    "была бы ординалом без строки");

    int samples = 0, mismatches = 0;
    for (int i = 0; i < sm::kSpellCount; ++i) {
        const int ord = sm::action_of_spell(i);
        if (!sm::action_ordinal_valid(ord)) { ++mismatches; continue; }
        const sm::ActionDef& a = sm::action_def(ord);
        // Ключ ТОТ ЖЕ посимвольно: авторских списка два, но порядок один —
        // разъезд ловит static_assert таблицы, прибор дублирует рантаймом.
        if (std::strcmp(a.id, sm::kSpellDefs[i].id) != 0) ++mismatches;
        if (a.type != ActionType::Spell) ++mismatches;
        if (sm::archetype_of(a.type) != ActionArchetype::Magic) ++mismatches;
        ++samples;
    }
    // Цикл, который мерит, обязан утверждать, что ПОМЕРИЛ (§8 п.3).
    CHECK(samples == sm::kSpellCount && mismatches == 0,
                    "ординал спелл → действие → строка возвращает ТУ ЖЕ строку");

    CHECK(!sm::action_ordinal_valid(-1)
                        && !sm::action_ordinal_valid(sm::kActionCount),
                    "гейт каталога отвергает ординал вне пространства");
}

// ── 2. Якори базы: квант мира, а не калибровка ────────────────────────────
void base_is_ticks_anchored_to_the_world_quantum() {
    CHECK(sm::kTicksPerRealSecond == 64,
                    "якори ниже стоят на кванте мира: сутки 8192 тика при 64 "
                    "тиках в секунде (ЗАКОН КАЛИБРОВКИ В ТИКАХ)");

    const sm::ActionDef& fire =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("fireball")));
    const sm::ActionDef& arma =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("armageddon")));
    const sm::ActionDef& haste =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("haste")));

    CHECK(fire.recoveryTicks == 147u,
                    "fireball: авторские 2.3 с есть 147 тиков мира");
    CHECK(arma.recoveryTicks == 7808u,
                    "armageddon: авторские 122 с есть 7808 тиков — и это "
                    "худшая база каталога, на которой выведена ширина колонки");
    CHECK(haste.recoveryTicks == 0u,
                    "НОЛЬ БАЗЫ ЕСТЬ ЗНАЧЕНИЕ, А НЕ ОТСУТСТВИЕ: сустейн не "
                    "занимает тело вовсе (ЗАКОН СПОСОБНОСТИ п.2)");
}

// ── 3. Два слоя: база vs эффективное ──────────────────────────────────────
void effective_is_a_door_over_the_base() {
    const sm::ActionDef& fire =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("fireball")));

    const sm::Attributes blankA{};
    const sm::Skills     blankS{};
    const int untrained = sm::recovery_steps(fire.recoveryTicks, blankA,
                                             blankS,
                                             sm::action_tempo_generic(fire));
    CHECK(untrained == int(fire.recoveryTicks),
                    "нетренированный лист НЕ ДВИГАЕТ базу: эффективное равно "
                    "базовому, то есть слои не перепутаны местами");

    sm::Attributes fastA{};
    fastA[sm::AttributeId::Spd] = 100;
    sm::Skills skilledS{};
    skilledS[SkillId::Spellcraft] = 100;
    const int trained = sm::recovery_steps(fire.recoveryTicks, fastA, skilledS,
                                           sm::action_tempo_generic(fire));
    // ОТНОШЕНИЕ, а не число: перекалибровка ставки Spd или процента за ранг
    // не смеет красить этот прибор (§8 п.4, ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
    CHECK(trained < untrained && trained >= 1,
                    "резвость и ранг генерика двигают эффективное ВНИЗ, и "
                    "никогда ниже одного тика — пол есть квант, не кламп");

    // Ненулевая база не умеет стать нулём ни при каком листе: иначе действие
    // перестало бы занимать тело, и в пошаговом режиме мир не двигался бы.
    int samples = 0, zeros = 0;
    for (int i = 0; i < sm::kActionCount; ++i) {
        const sm::ActionDef& a = sm::action_def(i);
        if (a.recoveryTicks == 0u) continue;
        if (sm::recovery_steps(a.recoveryTicks, fastA, skilledS,
                               sm::action_tempo_generic(a)) < 1) ++zeros;
        ++samples;
    }
    CHECK(samples > 0 && zeros == 0,
                    "ненулевая база занимает тело хотя бы на один тик при "
                    "любом листе");
}

// ── 4. Домен темпа — свойство лестницы ────────────────────────────────────
void tempo_domain_comes_from_the_ladder() {
    const sm::ActionDef& fire =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("fireball")));
    CHECK(sm::action_tempo_generic(fire) == SkillId::Spellcraft,
                    "каст платит Спеллкрафтом, и это говорит АРХЕТИП строки, "
                    "а не подстановка звонящего");
    CHECK(sm::tempo_generic(ActionArchetype::Combat)
                        == SkillId::Armsmaster
                    && sm::tempo_generic(ActionArchetype::Labour)
                        == SkillId::Armsmaster
                    && sm::tempo_generic(ActionArchetype::Interaction)
                        == SkillId::Armsmaster,
                    "всё физическое — руки: у труда и интеракции нет своей "
                    "третьей ручки темпа");
}

// ── 5. Охват: три значения, четвёртого нет ────────────────────────────────
void scope_has_three_values_and_no_nowhere() {
    const sm::ActionDef& fire =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("fireball")));
    const sm::ActionDef& arrow =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("void_arrow")));
    CHECK(fire.scope == ActionScope::MicroMacro,
                    "fireball законен обоими мирами");
    CHECK(arrow.scope == ActionScope::MicroOnly,
                    "void_arrow законен только внизу — ось охвата несёт это "
                    "ОДНОЙ колонкой вместо пары булевых");

    int samples = 0;
    for (int i = 0; i < sm::kActionCount; ++i) {
        const sm::ActionDef& a = sm::action_def(i);
        // Перечисление из трёх значений делает «нигде» невыразимым; этот цикл
        // сторожит не его, а ПОЛНОТУ покрытия: каждая строка названа.
        if (a.scope == ActionScope::MicroOnly
            || a.scope == ActionScope::MacroOnly
            || a.scope == ActionScope::MicroMacro) ++samples;
    }
    CHECK(samples == sm::kActionCount,
                    "у КАЖДОЙ строки назван охват: строка, применимая нигде, "
                    "незаконна");
}

// ── 6. Цена: одна дверь, три пула, всё или ничего ─────────────────────────
void cost_is_all_or_nothing_across_three_pools() {
    const sm::ActionDef& fire =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("fireball")));
    CHECK(fire.costMp == 60 && fire.costHp == 0 && fire.costSp == 0,
                    "цена каста стоит ТРЕМЯ колонками, и два нуля в ней — "
                    "значения, а не отсутствие");

    // НЕХВАТКА НЕ ТРОГАЕТ НИ ОДИН ПУЛ. Это негативный контроль на снятый
    // кламп: прежде пул дотягивался до нуля, то есть полуплатёж был выразим.
    sm::ecs::Pools poor{};
    poor.hp = 10; poor.mp = 59; poor.sp = 10;
    const bool paidPoor = sm::pay_action_cost(poor, fire);
    CHECK(!paidPoor && poor.hp == 10 && poor.mp == 59 && poor.sp == 10,
                    "нечем платить — не списано НИЧЕГО, ни из одного пула");

    sm::ecs::Pools rich{};
    rich.hp = 10; rich.mp = 60; rich.sp = 10;
    const bool paidRich = sm::pay_action_cost(rich, fire);
    CHECK(paidRich && rich.mp == 0 && rich.hp == 10 && rich.sp == 10,
                    "заплатив, действие списывает РОВНО свои три колонки");

    // ЦЕНА НЕОТРИЦАТЕЛЬНА У ВСЕГО КАТАЛОГА: отрицательная колонка была бы
    // лечением-через-каст мимо закона лечения — у авторской таблицы это
    // судит прибор, потому что тип int32 выбран пулом, а не знаком цены.
    int costSamples = 0, negative = 0;
    for (int i = 0; i < sm::kActionCount; ++i) {
        const sm::ActionDef& a = sm::action_def(i);
        if (a.costHp < 0 || a.costMp < 0 || a.costSp < 0) ++negative;
        ++costSamples;
    }
    CHECK(costSamples == sm::kActionCount && negative == 0,
                    "цена не бывает отрицательной ни в одном пуле: минус тут "
                    "был бы вторым входом в лечение");

    // Нулевая цена платится из пустого пула: ноль есть значение и здесь.
    const sm::ActionDef& haste =
        sm::action_def(sm::action_of_spell(sm::spell_ordinal("haste")));
    sm::ecs::Pools empty{};
    const bool paidFree = sm::pay_action_cost(empty, haste);
    CHECK(paidFree && empty.hp == 0 && empty.mp == 0 && empty.sp == 0,
                    "бесплатное действие проходит дверь цены при пустых пулах");
}

// ── 7. Боевой отрезок: замах, добавка массы, набор строки (M-396) ────────
void combat_segment_strike_mass_and_row_sets() {
    const sm::ActionDef& strike = sm::action_def(sm::kActionStrike);
    CHECK(std::strcmp(strike.id, "strike") == 0
                        && strike.type == ActionType::Strike
                        && sm::archetype_of(strike.type)
                               == ActionArchetype::Combat,
                    "первая боевая строка — strike, и лестница зовёт её боем");
    CHECK(strike.recoveryTicks == 96u,
                    "strike: замах пустой руки есть 96 тиков мира (1.5 с × "
                    "64) — число бывшей kHandSwingS, не сдвинутое переездом");
    CHECK(strike.costHp == 0 && strike.costMp == 0 && strike.costSp == 0,
                    "удар сегодня бесплатен ТРЕМЯ нулями-значениями");
    CHECK(strike.scope == ActionScope::MicroMacro,
                    "удар законен обоими мирами: внизу бьёт тело, наверху — "
                    "авторезолв");

    // ДОБАВКА МАССЫ — АДДИТИВНА И ЦЕЛАЯ (вердикт 2026-11-04: база = пустая
    // рука, масса складывает, не множит). Якорь мира: кинжал в 1000 г бил
    // 2.0 с все эпохи этого закона — 96 + 32 = 128 тиков, та же величина.
    CHECK(sm::weapon_mass_recovery_ticks(nullptr) == 0u,
                    "голая рука не несёт добавки: её масса ноль ПО ПРИРОДЕ");
    sm::ItemDef dagger{};
    dagger.massG = 1000;
    CHECK(sm::weapon_mass_recovery_ticks(&dagger) == 32u,
                    "килограмм железа стоит 32 тика замаха (0.5 с/кг × 64)");
    CHECK(strike.recoveryTicks + sm::weapon_mass_recovery_ticks(&dagger)
                        == 128u,
                    "кинжал: 96 + 32 = 128 тиков — прежние 2.0 с закона "
                    "массы, число мира переездом не сдвинуто");

    // НАБОР СТРОКИ СУЩЕСТВА: чем кастует — говорит row_cast_action над
    // ординалами ЕДИНОЙ таблицы; не-кастер честно отвечает kActionNone.
    const std::uint8_t witchCast = sm::row_cast_action(sm::kWitchCombat);
    CHECK(witchCast != sm::kActionNone
                        && witchCast == std::uint8_t(sm::action_of_spell(
                               sm::spell_ordinal("magic_bolt"))),
                    "ведьма несёт свой болт ординалом ДЕЙСТВИЯ в наборе "
                    "строки — сентинел −1 умер");
    CHECK(sm::row_cast_action(sm::kPeasantCombat) == sm::kActionNone,
                    "крестьянин не кастует: пустой набор отвечает kActionNone");
    // Негативный контроль двери: БОЕВОЙ ординал в наборе кастом не считается
    // — вопрос решает отрезок лестницы, а не занятость слота.
    sm::CombatTemplate rowWithStrike = sm::kPeasantCombat;
    rowWithStrike.actions[0] = sm::action_slot("strike");
    CHECK(sm::row_cast_action(rowWithStrike) == sm::kActionNone,
                    "strike в наборе — не каст: дверь спрашивает АРХЕТИП "
                    "ординала, а не факт занятости слота");

    // База существа — та же единица, что у строки действия (слияние имени и
    // типа, M-396). Якорь кванта: крестьянин авторит 3.0 с = 192 тика.
    CHECK(sm::kPeasantCombat.recoveryTicks == 192u,
                    "база удара крестьянина: авторские 3.0 с есть 192 тика — "
                    "колонка строки существа слита с единицей таблицы действий");
}

// ── 8. Трудовой отрезок: одна строка — оба мира, цена темпом (M-369) ─────
void labour_segment_prices_by_rate_for_both_worlds() {
    const sm::ActionDef& fell  = sm::action_def(sm::kActionFellTree);
    const sm::ActionDef& wheat = sm::action_def(sm::kActionReapWheat);
    const sm::ActionDef& hunt =
        sm::action_def(int(sm::action_slot("hunt_fauna")));
    const sm::ActionDef& flax =
        sm::action_def(int(sm::action_slot("reap_flax")));
    const sm::ActionDef& horse =
        sm::action_def(int(sm::action_slot("catch_livestock")));

    CHECK(std::strcmp(fell.id, "fell_tree") == 0
                        && fell.type == ActionType::Gather
                        && sm::archetype_of(fell.type)
                               == ActionArchetype::Labour,
                    "рубка — строка трудового отрезка, и лестница зовёт её "
                    "трудом");

    // ЯКОРЬ МИРА (CANON S10, независимый факт, не пересчёт формулы):
    // «1 добытчик кормит 32 души» — темп пашни и леса есть САМ якорь.
    CHECK(wheat.spRatePerDay == sm::kGatherPerWorkerDay
                        && fell.spRatePerDay == sm::kGatherPerWorkerDay,
                    "темп пашни и леса — якорь мира kGatherPerWorkerDay, а "
                    "не совпадающее с ним число");

    // ОТНОШЕНИЯ, НЕ ЛИТЕРАЛЫ (§8 п.4): зверь бегает, колос стоит — единица
    // охоты вчетверо дороже; лён — та же земля, та же четверть; поимка
    // лошади стоит работнику ЦЕЛЫЙ бар (sp_price при темпе 1 отдаёт бар).
    CHECK(hunt.spRatePerDay * 4 == wheat.spRatePerDay,
                    "охота вчетверо медленнее пашни — зверь честно дороже "
                    "за единицу");
    CHECK(flax.spRatePerDay * 4 == wheat.spRatePerDay,
                    "лён — четверть якоря: та же земля, своя культура");
    CHECK(horse.spRatePerDay == 1
                        && sm::sp_price(977, horse.spRatePerDay) == 977,
                    "поимка лошади — целый бар: темп 1 есть «акт стоит "
                    "рабочий день»");

    // ПОЛНЫЙ БАР = ДНЕВНАЯ НОРМА АКТОВ, ЦЕЛЫМИ: у бара, кратного темпу,
    // цена × темп выбирает его РОВНО — день труда и бар SP одно и то же.
    const int bar = 256;
    CHECK(sm::sp_price(bar, wheat.spRatePerDay) * wheat.spRatePerDay == bar,
                    "бар 256: 32 акта по 8 SP выбирают бар ровно — цена есть "
                    "бар/темп без остатка на кратном баре");

    // ОДНА СТРОКА — ОБА МИРА: обе строки субмира (колос и дерево) законны
    // обоими мирами, и база рубки — число умершей kHarvestActSeconds
    // (1.5 с × 64 = 96 тиков), не сдвинутое переездом в строку.
    CHECK(fell.scope == ActionScope::MicroMacro
                        && wheat.scope == ActionScope::MicroMacro,
                    "жатва и рубка законны обоими мирами: внизу топор, "
                    "наверху цикл артели — одна строка");
    CHECK(fell.recoveryTicks == 96u && wheat.recoveryTicks == 96u,
                    "база рубки и жатвы: 1.5 с = 96 тиков — число умершей "
                    "константы kHarvestActSeconds, переездом не сдвинуто");

    // ДВА ЗАКОНА ЦЕНЫ НЕ ВСТРЕЧАЮТСЯ НА ОДНОЙ СТРОКЕ (стены — static_assert
    // таблицы, рантайм дублирует выборочно): у труда плоские нули-значения,
    // вне труда темп — ноль-значение.
    CHECK(fell.costHp == 0 && fell.costMp == 0 && fell.costSp == 0,
                    "цена труда — темпом: плоские колонки рубки — "
                    "нули-значения");
    CHECK(sm::action_def(sm::kActionStrike).spRatePerDay == 0,
                    "у боевой строки темповой цены нет: ноль — значение "
                    "«не темповая», а не отсутствие");

    // ПОЛНОТА ОТРЕЗКА: цикл, который мерит, утверждает, что померил.
    // «Весь труд — Gather» снято ломтём «крафт + работа» (M-369): трудовых
    // ТИПА два, и они стоят ГРУППАМИ без чересполосицы (ЗАКОН ЛЕСТНИЦЫ п.2).
    int samples = 0, defects = 0;
    ActionType prev = sm::kActionTypeLabourFirst;
    for (int i = 0; i < sm::kActionLabourCount; ++i) {
        const sm::ActionDef& a = sm::action_def(sm::kActionLabourFirst + i);
        if (sm::archetype_of(a.type) != ActionArchetype::Labour) ++defects;
        if (a.type < prev) ++defects;
        prev = a.type;
        if (a.spRatePerDay <= 0) ++defects;
        ++samples;
    }
    CHECK(samples == sm::kActionLabourCount && defects == 0,
                    "каждая строка трудового отрезка — трудового типа "
                    "(Gather|Craft группами) с темповой ценой: нетемповый "
                    "труд отдавал бы весь бар за акт");
}

// ── 9. Крафтовый подотрезок: рецепт ссылается, строка несёт всё (M-369,
// ломоть «крафт + работа») ───────────────────────────────────────────────
void craft_segment_is_the_recipes_other_half() {
    // 1:1 с рецептами — рантайм-тождество того, что стена
    // `recipe_rows_point_at_craft@src/macro/econ_day.h` держит компилятором.
    CHECK(sm::kActionCraftCount == sm::kRecipeCount,
                    "крафтовых строк ровно столько, сколько рецептов: "
                    "строка без рецепта — колонка-сирота");

    const sm::ActionDef& mint =
        sm::action_def(int(sm::action_slot("mint_coin")));
    const sm::ActionDef& bricks =
        sm::action_def(int(sm::action_slot("fire_bricks")));
    const sm::ActionDef& sword =
        sm::action_def(int(sm::action_slot("forge_sword")));
    const sm::ActionDef& dagger =
        sm::action_def(int(sm::action_slot("forge_dagger")));

    // ПРАВО ПЕРЕЕХАЛО ДОСЛОВНО (колонки craft/minRank рецепта умерли в
    // gateSkill/minRank строки): чеканка — кузнечное дело ранга 30.
    CHECK(mint.gateSkill == sm::SkillId::Blacksmith && mint.minRank == 30,
                    "право чеканки — пара «ремесло + минранг» СТРОКИ "
                    "действия, перенос из колонок рецепта дословный");

    // ТЕМП ПЕРЕЕХАЛ ДОСЛОВНО из умершей колонки perDay материи: кирпич —
    // восьмушка бара, клинок — полдня, лёгкая строка — вдвое резвее клинка.
    CHECK(bricks.spRatePerDay == 8,
                    "темп кирпича: 8 партий на человеко-день — число "
                    "умершей колонки perDay, переездом не сдвинуто");
    CHECK(dagger.spRatePerDay == 2 * sword.spRatePerDay,
                    "лестница темпа снаряжения: лёгкая строка вдвое резвее "
                    "клинка (4 против 2) — отношение, не литерал");
    CHECK(sm::sp_price(256, bricks.spRatePerDay) * bricks.spRatePerDay
                        == 256,
                    "цена партии — ровно доля бара: бар/темп × темп = бар "
                    "(тот же закон sp_price, что у добычи и у руки)");

    // ПОЛНОТА ПОДОТРЕЗКА: у всякой Craft-строки право НАЗВАНО (в отличие от
    // добычи, где SkillId::Count — «рангом не ограничено»), база — рабочий
    // ДЕНЬ тиками («сделал → вишу», M-326), охват — оба мира (город и
    // рука), домен темпа — Армсмастер через лестницу.
    int samples = 0, defects = 0;
    for (int i = 0; i < sm::kActionLabourCount; ++i) {
        const sm::ActionDef& a = sm::action_def(sm::kActionLabourFirst + i);
        if (a.type != ActionType::Craft) continue;
        if (a.gateSkill == sm::SkillId::Count) ++defects;
        if (a.recoveryTicks != std::uint32_t(sm::kTicksPerDay)) ++defects;
        if (a.scope != sm::ActionScope::MicroMacro) ++defects;
        if (sm::action_tempo_generic(a) != sm::SkillId::Armsmaster) ++defects;
        ++samples;
    }
    CHECK(samples == sm::kActionCraftCount && defects == 0,
                    "каждая Craft-строка: право названо, база — день "
                    "(сделал → вишу), охват MicroMacro, темп — Армсмастер");
}

} // namespace

int main() {
    segment_is_aligned_with_spells();
    base_is_ticks_anchored_to_the_world_quantum();
    effective_is_a_door_over_the_base();
    tempo_domain_comes_from_the_ladder();
    scope_has_three_values_and_no_nowhere();
    cost_is_all_or_nothing_across_three_pools();
    combat_segment_strike_mass_and_row_sets();
    labour_segment_prices_by_rate_for_both_worlds();
    craft_segment_is_the_recipes_other_half();
    return sm::test::report("action_table_test");
}
