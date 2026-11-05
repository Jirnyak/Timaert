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
    CHECK(sm::kActionCount == sm::kActionMagicFirst + sm::kSpellCount,
                    "ординальное пространство действий есть сумма отрезков");

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

} // namespace

int main() {
    segment_is_aligned_with_spells();
    base_is_ticks_anchored_to_the_world_quantum();
    effective_is_a_door_over_the_base();
    tempo_domain_comes_from_the_ladder();
    scope_has_three_values_and_no_nowhere();
    cost_is_all_or_nothing_across_three_pools();
    return sm::test::report("action_table_test");
}
