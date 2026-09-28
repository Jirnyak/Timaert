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
#include "macro/anketa.h"
#include "macro/character_sheet.h"   // body_max_hp — THE ceiling door
#include "macro/economy.h"
#include "macro/npc.h"               // npc_def — THE row the ceiling reads
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

    LevelData ld = default_level_data();
    award_exp(ld, 100, multPct);
    if (ld.exp != 110) std::fprintf(stderr, "  exp=%d\n", ld.exp);
    CHECK(ld.exp == 110,
          "wis 10 is +1% per point: a grant of 100 lands as 110");

    LevelData ld2 = default_level_data();
    award_exp(ld2, 25, multPct);  // 27.5 -> round half up -> 28
    if (ld2.exp != 28) std::fprintf(stderr, "  exp=%d\n", ld2.exp);
    CHECK(ld2.exp == 28,
          "the dividend rounds half UP — a half point of xp is never eaten");

    LevelData ld3 = default_level_data();
    award_exp(ld3, 100, 100);
    CHECK(ld3.exp == 100,
          "a sheet with no dividend (100%) leaves the grant untouched");
}

// ── 3. One price law ──────────────────────────────────────────────────────
// Canon (S25): наценка = РАЗНИЦА торговых сил двух анкет, одна формула на обе
// половины. Сильнее на 10 пунктов — покупаю за 90 и продаю за 110; РАВНЫЕ
// стороны торгуют ровно по цене. Спред 0.7 и клампы 0.5/1.5 умерли вместе с
// «домом» у сделки, а контекстный множитель (настроение места, нрав купца) —
// 2026-09-19: у цены остались кривая дефицита и разница торговых сил.
void test_one_price_law_reads_only_the_difference_in_bargaining() {
    using namespace sm;
    CHECK(trade_price(100, 10, 0, true) == 90,
          "buying: ten points of advantage take a hundred down to ninety");
    CHECK(trade_price(100, 0, 10, true) == 110,
          "buying weaker by ten costs a hundred and ten — the SAME difference, "
          "mirrored");
    CHECK(trade_price(100, 42, 42, true) == 100,
          "equal sheets buy at the price itself, however strong both are");
    CHECK(trade_price(100, 42, 42, false) == 100,
          "...and sell at it too: the law reads the difference, not the size");
    CHECK(trade_price(100, 10, 0, false) == 110,
          "selling: ten points of advantage take a hundred up to a hundred "
          "and ten");

    // ПОЛА СКИДКИ 0.5 НЕТ (S25): граница выводится из анкет или её не
    // существует — назначенный пол вернул бы кламп под другим именем.
    // Подавляющий перевес доводит цену до ПОЛА ЗАКОНА, единицы: это «ничто не
    // бесплатно», а не потолок наценки.
    CHECK(trade_price(100, 200, 0, true) == 1,
          "an overwhelming advantage runs into the law's own floor of one "
          "coin, not into a named discount ceiling");
    CHECK(trade_price(1, 0, 0, false) >= 1,
          "nothing in the world is free: a price never falls below one coin");
}

// ── 4. THE recovery door (CANON S14 «один рычаг», 2026-09-07) ─────────────
// Shape, not pinned numbers (testing law #4/#5): each claim breaks alone.
void test_the_recovery_door_is_one_lever() {
    using namespace sm;
    const float base = 0.5f;   // the one-handed anchor
    Attributes a{};
    a[AttributeId::Spd] = 0;   // the bases start at 1 — zero the ONE input
    Skills s{};
    const int blank = recovery_steps(base, a, s, SkillId::Armsmaster);
    CHECK(blank == int(steps_from_seconds(base)),
          "a zeroed sheet swings at exactly the row's own base tempo");

    // Spd quickens through the SAME asymptote the legs walk on…
    Attributes fast = a;
    fast[AttributeId::Spd] = 50;
    const int quick = recovery_steps(base, fast, s, SkillId::Armsmaster);
    CHECK(quick < blank, "Spd quickens the arm, as it quickens the legs");
    CHECK(quickness_pct(50) == 150,
          "and through the legs' OWN curve: half-saturation sits at spd 50");

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
    CHECK(recovery_steps(0.001f, cap, s, SkillId::Armsmaster) == 1,
          "the floor is ONE simulation step: no action is ever free of time");
}

} // namespace

int main() {
    test_a_moved_ceiling_keeps_the_wound_fraction();
    test_wisdom_pays_a_dividend_on_every_grant();
    test_one_price_law_reads_only_the_difference_in_bargaining();
    test_the_recovery_door_is_one_lever();
    return sm::test::report("rpg_rules_test");
}
