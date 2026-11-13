// Locks three owner rulings of 2026-08-05:
//
//   1. NO FREE HEAL, NO THEFT — «доля у всех» (owner 2026-09-10): a moved
//      ceiling rescales its bar by FRACTION through the one door
//      (squad.h refresh_body_from_sheet); the full restore belongs to
//      creation alone. (The original bug: every «+» click was a free full
//      heal because the UI recomputed through the creating function.)
//   2. THE WIS DIVIDEND — award_exp(ld, amount, expMult) scales the grant by
//      the sheet's expMult (+1% per wis point), round half up. Before this,
//      wis was computed and consumed by nothing.
//   3. ONE PRICE LAW — trade_price = the canonical charisma+bargaining
//      pricing (formerly dead code while three homegrown UI laws diverged)
//      with context (mood/trait) as one multiplier column.

#include "check.h"
#include "bus/anketa.h"
#include "macro/economy.h"
#include "tables/npc.h"               // npc_def — THE row the ceiling reads
#include "macro/squad.h"

#include <cstdio>

namespace {

// ── 1. A moved ceiling preserves the FRACTION («доля у всех») ──────────────
// Owner 2026-09-10: the lord's level-up law is the ONLY rescale — a point
// spent, a level gained or a coat donned moves the ceiling and the bar follows
// proportionally. Never a free heal (the old player law full-restored on
// level-up), never a theft (the old «keep the number» clamp silently shrank
// the fraction).
void test_a_moved_ceiling_keeps_the_wound_fraction() {
    using namespace sm;
    CharacterSheet sheet{};
    ecs::Pools p{};
    ecs::MacroNpcRuntime rt{};
    refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
    // ОДИН ПОТОЛОК, ОДНА ДВЕРЬ. Ожидание не повторяет числа строки и не
    // переписывает закон: оно спрашивает ТУ ЖЕ таблицу, что читает код
    // (`npc_def(...).combat`), ТОЙ ЖЕ дверью (`body_max_hp`), которой
    // пользуется сам `refresh_body_from_sheet`. Канонические 100/100/100
    // приключенца прибиты У СВОЕГО ИСТОЧНИКА — `static_assert` в `npc.h`
    // рядом с `kAdventurerCombat`, и второй копии здесь не нужно (M-132).
    CHECK(p.maxHp == body_max_hp(sheet, npc_def(NPCType::Adventurer).combat),
          "a fresh body's ceiling is what the ONE ceiling door answers for its "
          "row — the refresh door has no bar law of its own");

    // И этот пол читается ИЗ КОЛОНКИ СТРОКИ, а не из спрятанного дефолта
    // (§41 корень 2: до посадки 4 база жила в аргументе по умолчанию, и
    // строка не могла её переопределить — у всех тел мира был один и тот же
    // смуглённый мимо таблицы потолок). Тот же лист на другой строке обязан
    // дать другой пол, и РАЗНИЦА равна разнице колонок: у пустого листа
    // множитель Bodybuilding — единица, поэтому закон виден начисто.
    ecs::Pools peasantBody{};
    refresh_body_from_sheet(peasantBody, nullptr, sheet, NPCType::Peasant);
    CHECK(peasantBody.maxHp < p.maxHp,
          "the SAME sheet on a leaner row gets a leaner body — a hidden "
          "default would answer identically for both");
    CHECK(p.maxHp - peasantBody.maxHp
              == int(npc_def(NPCType::Adventurer).combat.hp)
                     - int(npc_def(NPCType::Peasant).combat.hp),
          "and the gap between them IS the gap between their rows' own hp "
          "columns — the table decides the floor, nothing else");

    p.hp = p.maxHp / 2;   // wounded at one half
    p.mp = p.maxMp / 4;
    p.sp = p.maxSp;       // rested
    const int ceilingBefore = p.maxHp;
    ++sheet.attributes[AttributeId::End];  // the spend
    refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
    CHECK(p.maxHp == body_max_hp(sheet, npc_def(NPCType::Adventurer).combat),
          "a spent point moves the CEILING: the maxima recompute from the new "
          "attributes, through the same one door");
    CHECK(p.maxHp > ceilingBefore,
          "and it moves it UPWARD — a point of END buys bar, or it bought "
          "nothing");

    const float hpFrac = float(p.hp) / float(p.maxHp);
    if (hpFrac < 0.49f || hpFrac > 0.51f || p.hp >= p.maxHp)
        std::fprintf(stderr, "  hp=%d/%d frac=%.3f\n", p.hp, p.maxHp,
                     double(hpFrac));
    CHECK(hpFrac >= 0.49f && hpFrac <= 0.51f,
          "a body wounded at one half stays wounded at one half when its "
          "ceiling grows — no free heal, no theft");
    CHECK(p.hp < p.maxHp,
          "and it is still WOUNDED: a grown ceiling never tops the bar up");
    CHECK(p.sp == p.maxSp,
          "a rested bar stays rested when its ceiling grows — the fraction "
          "law reads 1.0 as 1.0");

    // A bar whose ceiling did not move is not touched at all: this door is
    // walked every macro tick, so an unconditional rescale would be a slow
    // rounding drain rather than an identity.
    const int hpStable = p.hp;
    refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
    CHECK(p.hp == hpStable,
          "a ceiling that did not move leaves the bar EXACTLY where it was");

    // Dead stays dead: a growing ceiling must not resurrect.
    ecs::Pools corpse{};
    refresh_body_from_sheet(corpse, nullptr, sheet, NPCType::Adventurer);
    corpse.hp = 0;
    ++sheet.attributes[AttributeId::End];
    refresh_body_from_sheet(corpse, nullptr, sheet, NPCType::Adventurer);
    CHECK(corpse.hp == 0,
          "a grown ceiling does not resurrect a body at zero hp — dead stays "
          "dead");
}

// ── 2. The wis dividend ───────────────────────────────────────────────────
void test_wisdom_pays_a_dividend_on_every_grant() {
    using namespace sm;
    Attributes a{};
    a[AttributeId::Wis] = 10;
    const int multPct = calculate_derived(a, Skills{}).expMultPct;

    // ДИВИДЕНД СУДИТСЯ ВНУТРИ ОДНОЙ СТУПЕНИ, И ЭТО ТЕПЕРЬ УСЛОВИЕ ВОПРОСА
    // (M-383): опыт стал КУМУЛЯТИВНЫМ, порог никто не вычитает, значит грант,
    // перешагнувший веху, спрашивал бы уже про уровень, а не про множитель
    // мудрости. Все три гранта ниже лежат под первым порогом — он выражением и
    // назван, чтобы перекалибровка кривой краснила ЗДЕСЬ, а не молча сдвигала
    // предмет проверки.
    CHECK(exp_to_next_level(1) > 110,
          "предусловие: все три гранта ниже первой вехи, значит ни один из них "
          "не превращается в уровень и дивиденд виден начисто");

    LevelData ld = default_level_data();
    award_exp(ld, 100, multPct);
    if (ld.exp != 110) std::fprintf(stderr, "  exp=%d\n", int(ld.exp));
    CHECK(ld.exp == 110 && level_of(ld) == 1,
          "wis 10 is +1% per point: a grant of 100 lands as 110");

    LevelData ld2 = default_level_data();
    award_exp(ld2, 25, multPct);  // 27.5 -> round half up -> 28
    if (ld2.exp != 28) std::fprintf(stderr, "  exp=%d\n", int(ld2.exp));
    CHECK(ld2.exp == 28,
          "the dividend rounds half UP — a half point of xp is never eaten");

    LevelData ld3 = default_level_data();
    award_exp(ld3, 100, 100);
    CHECK(ld3.exp == 100,
          "a sheet with no dividend (100%) leaves the grant untouched");
}

// ── 3. One price law ──────────────────────────────────────────────────────
// Canon (S25, форма цены 2026-11-13): у каждой стороны одна ОЦЕНКА вещи —
// стоимость, поднятая её нуждой и уроненная завалом. В сделке есть СТОЯЩИЙ (к
// нему пришли) и ПРИШЕДШИЙ: взятое у стоящего стоит его оценку плюс его долю
// от неё, отданное ему он засчитывает по своей оценке; доля — отношение сил
// двух анкет. Купить стоит от ×1 до ×2 оценки стоящего, продать — от ÷1 до
// ÷2. Наценка в пользу ПРИШЕДШЕГО («сильнее на десять пунктов — покупаю за 90
// и продаю за 110») умерла: она заставляла стоящего торговать себе в убыток,
// и круг «продал — выкупил» качал из него монету при любом перевесе.
void test_one_price_law_is_the_standing_sides_markup() {
    using namespace sm;
    // Спина обеих сторон велика против груза (завала нет): здесь судится
    // нужда и наценка; завал — у самих дверей ядра и в price_law_test.
    constexpr std::int64_t kWideBack = std::int64_t(1) << 40;
    const RowSide hungry{100, -4, 1, kWideBack};   // остаток счёта четыре
    const RowSide sated{100, 30, 1, kWideBack};    // излишек тридцать
    // Пришедший ОТДАЁТ стоящему единицу: что ему за неё причитается.
    // Силы — (стоящий, пришедший).
    const auto gives = [](const RowSide& standing, int sp, int vp, int n) {
        return ask_reach(lot_take(standing, n), sp, vp);
    };
    // Пришедший БЕРЁТ у стоящего.
    const auto takes = [](const RowSide& standing, int sp, int vp, int n) {
        return take_price(standing, n, sp, vp);
    };

    // Оценки: рынок ценит первую пришедшую единицу в четыре стоимости (нужда
    // три после прихода, полка пуста), сытый — ровно в стоимость.
    CHECK(worth_arriving(hungry, 1) == 400 && worth_leaving(sated, 1) == 100,
          "the hungry side values the unit above its worth, the side without "
          "a need values it at exactly its worth");
    CHECK(gives(hungry, 10, 10, 1) == 266 && takes(sated, 10, 10, 1) == 150,
          "equal sheets: two thirds of the standing side's valuation for what "
          "it is given, one and a half for what is taken from it");
    CHECK(gives(hungry, 10, 30, 1) == 320 && gives(hungry, 30, 10, 1) == 228
              && takes(sated, 10, 30, 1) == 125
              && takes(sated, 30, 10, 1) == 175,
          "the stronger visitor gets more and pays less, the weaker one gets "
          "less and pays more — against the same standing valuation");
    CHECK(gives(hungry, 42, 42, 1) == gives(hungry, 7, 7, 1)
              && takes(sated, 42, 42, 1) == takes(sated, 7, 7, 1),
          "the law reads the ratio of the two powers, not their size");

    // ПОЛА И ПОТОЛКА У ДОЛИ НЕТ ПО ПОСТРОЕНИЮ: отношение сил само лежит между
    // нулём и единицей. Мастер подходит к оценке стоящего и не достигает её;
    // полный ноль получает половину и платит вдвое — и не хуже.
    CHECK(gives(hungry, 1, 510, 1) < 400 && gives(hungry, 1, 510, 1) > 390
              && takes(sated, 1, 510, 1) > 100 && takes(sated, 1, 510, 1) < 110,
          "an overwhelming visitor approaches the standing side's valuation "
          "and never reaches it: the standing side keeps its quantum");
    CHECK(gives(hungry, 10, 0, 1) == 200 && takes(sated, 10, 0, 1) == 200,
          "a visitor with no power at all gets half the standing valuation "
          "and pays double — the worst the law can do to anyone");
    CHECK(gives(hungry, 0, 10, 1) == 400 && takes(sated, 0, 10, 1) == 100,
          "a standing side with no power at all trades at exactly its own "
          "valuation — and never below it");
    // НЕДЕЛИМЫЙ ОСТАТОК — СТОЯЩЕМУ: 100 при силах 1 против 6 — его доля
    // 14.29. Взятое стоит 115, а за отданное на 115 причитается не 100.57, а
    // 100: обратный вопрос не возвращает больше, чем стоило взятие.
    CHECK(takes(sated, 1, 6, 1) == 115
              && ask_reach(115, 1, 6) == 100 && ask_reach(114, 1, 6) == 99,
          "the indivisible quantum of the share stays with the standing side");

    // БЕЗ РАЗНИЦЫ ОЦЕНОК — В УБЫТОК: купить пять у сытого и тут же отдать ему
    // же стоит пришедшему долю стоящего дважды. С бессильным стоящим — вровень.
    RowSide fewer = sated;
    fewer.net -= 5;
    CHECK(gives(fewer, 10, 10, 5) < takes(sated, 10, 10, 5)
              && gives(fewer, 1, 510, 5) < takes(sated, 1, 510, 5)
              && gives(fewer, 0, 10, 5) == takes(sated, 0, 10, 5),
          "buying and selling back at one valuation is a loss for the visitor "
          "whatever the powers — there is no pump");
    // Отнять у голодного то, чего ему не хватает, стоит его оценки и доли
    // сверх неё, как бы силён ни был берущий.
    CHECK(takes(hungry, 1, 510, 1) > worth_leaving(hungry, 1)
              && worth_leaving(hungry, 1) > worth_arriving(hungry, 1),
          "taking from a side what it lacks itself costs more than that "
          "side's own valuation, however strong the taker");
    CHECK(worth_arriving(RowSide{100, 999, 1, kWideBack}, 1) == 100
              && worth_arriving(RowSide{100, 999, 1000, 100000}, 1) == 1,
          "a pile that weighs nothing against the back keeps its worth; the "
          "same count of a heavy thing is a glut");
}

// ── 4. THE recovery door (CANON S14 «один рычаг», 2026-09-07) ─────────────
// Shape, not pinned numbers (testing law #4/#5): each claim breaks alone.
void test_the_recovery_door_is_one_lever() {
    using namespace sm;
    // База В ТИКАХ: дверь восстановления берёт квант мира, а не секунды
    // (M-369). Перевод стоит ОДИН раз здесь — ровно там, где кончается
    // авторская запись и начинается симуляция.
    const std::uint32_t base = steps_from_seconds(0.5f);  // the one-handed anchor
    Attributes a{};
    a[AttributeId::Spd] = 0;   // the bases start at 1 — zero the ONE input
    Skills s{};
    const int blank = recovery_steps(base, a, s, SkillId::Armsmaster);
    CHECK(blank == int(base),
          "a zeroed sheet swings at exactly the row's own base tempo");

    // Spd quickens through the SAME rate the legs walk on…
    Attributes fast = a;
    fast[AttributeId::Spd] = 50;
    const int quick = recovery_steps(base, fast, s, SkillId::Armsmaster);
    CHECK(quick < blank, "Spd quickens the arm, as it quickens the legs");

    // …И СТАВКА НАЗВАНА В ЕДИНИЦЕ МИРА, А НЕ ПРОЦЕНТОМ ЗА ОЧКО (M-389).
    // Здесь стояло `quickness_pct(50) == 150` — пересказ числа умершей
    // асимптоты (§8 п.4). Утверждается ОТНОШЕНИЕ, выведенное из тех же двух
    // констант, что читает дверь: столько-то очков покупают одну лишнюю
    // клетку в час из восьми клеток мира, значит удвоение стоит их
    // ПРОИЗВЕДЕНИЕ. Перекалибровка ставки этих строк не краснит — они про
    // форму закона, а не про выбранное число.
    const int doubling = kSpdPctDivisor;   // очки за клетку × клетки в час
    // (Что ГОЛОЕ тело — база атрибута = 1 — даёт тоже ровно 100 %, есть
    // следствие нынешней ставки, а не закон: делитель 128 больше ста, поэтому
    // первое очко усекается в ноль. Запинить это значило бы охранять
    // калибровку — мутация 16 → 8 поймала такую строку здесь и была права.)
    CHECK(quickness_pct(0) == 100,
          "100 % ЕСТЬ марш мира в точности: невложенная резвость идёт мировым "
          "шагом и не покупает ничего — это единица шкалы, а не калибровка");
    CHECK(quickness_pct(doubling) == 200,
          "а удвоение стоит РОВНО произведение двух чисел мира — очков за "
          "клетку на клетки в час, и ничего кроме");
    CHECK(quickness_pct(2 * doubling) == 300,
          "форма ЛИНЕЙНА и без предела: два удвоения дают ×3, где снесённая "
          "асимптота давала ×1.9 и дальше не росла вовсе");

    // ОДИН РЫЧАГ, ДВА ЧИТАТЕЛЯ: марш МНОЖИТ на это число, рековери ДЕЛИТ на
    // него (CANON S14 «один рычаг на ручку»). Утверждается не форма кривой —
    // её пинят три проверки выше, — а то, что число ОДНО: иначе у рековери
    // завелась бы своя кривая темпа, то есть второй закон.
    Attributes anchor = a;
    anchor[AttributeId::Spd] = 100;            // якорь эндгейма, внутри байта
    CHECK(recovery_steps(base, anchor, s, SkillId::Armsmaster)
              == int(base) * 100 / quickness_pct(100),
          "марш и восстановление делят ОДНО число: один множит, другой делит");

    // И ЗАТУХАНИЕ: тот же рычаг делителем даёт 1/x, поэтому потолка ему не
    // нужно (владелец, 2026-11-04: «рековери не может быть меньше нуля») —
    // очко на вершине шкалы экономит МЕНЬШЕ, чем очко у подножия. База взята
    // длинной нарочно: на коротком замахе целые шаги разницы не увидят.
    const std::uint32_t longBase = steps_from_seconds(10.0f);
    const auto steps_at = [&](int spd) {
        Attributes at = a;
        at[AttributeId::Spd] = std::uint8_t(spd);
        return recovery_steps(longBase, at, s, SkillId::Armsmaster);
    };
    CHECK(steps_at(20) - steps_at(21) > steps_at(254) - steps_at(255),
          "отдача очка САМА затухает — делитель, а не линия, и потому ниже "
          "нуля восстановление не уходит ни на какой шкале");
    CHECK(steps_at(255) >= 1 && doubling <= 255,
          "а удвоение ДОСТИЖИМО в типе атрибута: ставка, обещающая ступень "
          "за пределом байта, обещала бы несуществующее");

    // …and the GENERIC skill multiplies on top — the typed one must NOT
    // (one handle, one lever: Sword already multiplied the dice).
    Skills sw{};
    sw[SkillId::Sword] = 100;
    CHECK(recovery_steps(base, a, sw, SkillId::Armsmaster) == blank,
          "a TYPED weapon skill does not touch the tempo — it already "
          "multiplies the dice, and one handle pulls one lever");
    s[SkillId::Armsmaster] = 100;
    const int master = recovery_steps(base, a, s, SkillId::Armsmaster);
    CHECK(master < blank, "the GENERIC skill is the one that quickens");

    // Healthy to the ENGINE caps (attribute 255, rank 100): monotone,
    // positive, floored at the simulation's own quantum — never 0.
    Attributes cap{};
    cap[AttributeId::Spd] = 255;
    const int demigod = recovery_steps(base, cap, s, SkillId::Armsmaster);
    CHECK(demigod >= 1, "even at the byte cap a swing costs at least one step");
    CHECK(demigod <= master, "and the curve stays monotone all the way there");
    // Под-квантовая база (0.001 с) есть НОЛЬ тиков, и пол двери всё равно
    // отдаёт один шаг: действие не бывает бесплатным по времени.
    CHECK(recovery_steps(steps_from_seconds(0.001f), cap, s,
                         SkillId::Armsmaster) == 1,
          "the floor is ONE simulation step: no action is ever free of time");
}

} // namespace

int main() {
    test_a_moved_ceiling_keeps_the_wound_fraction();
    test_wisdom_pays_a_dividend_on_every_grant();
    test_one_price_law_is_the_standing_sides_markup();
    test_the_recovery_door_is_one_lever();
    return sm::test::report("rpg_rules_test");
}
