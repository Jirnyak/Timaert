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
#include "macro/attributes.h"
#include "macro/economy.h"
#include "macro/squad.h"

#include <cstdio>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/rpg_rules_test.cpp", 0);
    return 1;
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. A moved ceiling preserves the FRACTION («доля у всех») ────────
    // Owner 2026-09-10: the lord's level-up law is the ONLY rescale — a
    // point spent, a level gained or a coat donned moves the ceiling and the
    // bar follows proportionally. Never a free heal (the old player law
    // full-restored on level-up), never a theft (the old «keep the number»
    // clamp silently shrank the fraction).
    {
        CharacterSheet sheet{};
        ecs::Pools p{};
        ecs::MacroNpcRuntime rt{};
        refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
        if (p.maxHp != bar_ceilings(sheet.attributes, sheet.skills, 100, 100, 100).maxHp) {
            return fail("the adventurer row's base must be the sheet law's "
                        "own 100 — one ceiling, no hidden default");
        }
        p.hp = p.maxHp / 2;   // wounded at one half
        p.mp = p.maxMp / 4;
        p.sp = p.maxSp;       // rested
        ++sheet.attributes[AttributeId::End];  // the spend
        refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
        if (p.maxHp != bar_ceilings(sheet.attributes, sheet.skills,
                                    /*baseHp=*/100, 100, 100).maxHp) {
            return fail("maxima must recompute from the new attributes");
        }
        const float hpFrac = float(p.hp) / float(p.maxHp);
        if (hpFrac < 0.49f || hpFrac > 0.51f || p.hp >= p.maxHp) {
            std::fprintf(stderr, "hp=%d/%d\n", p.hp, p.maxHp);
            return fail("a grown ceiling must keep the wound fraction — "
                        "no free heal, no theft");
        }
        if (p.sp != p.maxSp) {
            return fail("a rested bar stays rested when its ceiling grows");
        }
        // A bar whose ceiling did not move is not touched at all: the
        // every-tick walk must be an identity, not a rounding drain.
        const int hpStable = p.hp;
        refresh_body_from_sheet(p, &rt, sheet, NPCType::Adventurer);
        if (p.hp != hpStable) {
            return fail("an unchanged ceiling must not touch the bar");
        }
        // Dead stays dead: a growing ceiling must not resurrect.
        ecs::Pools corpse{};
        refresh_body_from_sheet(corpse, nullptr, sheet, NPCType::Adventurer);
        corpse.hp = 0;
        ++sheet.attributes[AttributeId::End];
        refresh_body_from_sheet(corpse, nullptr, sheet, NPCType::Adventurer);
        if (corpse.hp != 0) {
            return fail("a grown ceiling must not resurrect a zero hp");
        }
    }

    // ── 2. The wis dividend ─────────────────────────────────────────────
    {
        Attributes a{};
        a[AttributeId::Wis] = 10;
        const int multPct = calculate_derived(a, Skills{}).expMultPct;
        LevelData ld = default_level_data();
        award_exp(ld, 100, multPct);
        if (ld.exp != 110) {
            std::fprintf(stderr, "exp=%d\n", ld.exp);
            return fail("wis 10 must turn 100 xp into 110");
        }
        LevelData ld2 = default_level_data();
        award_exp(ld2, 25, multPct);  // 27.5 -> round half up -> 28
        if (ld2.exp != 28) {
            std::fprintf(stderr, "exp=%d\n", ld2.exp);
            return fail("the dividend rounds half up (25 * 110% = 28)");
        }
        LevelData ld3 = default_level_data();
        award_exp(ld3, 100, 100);
        if (ld3.exp != 100) return fail("expMultPct 100 must be the identity");
    }

    // ── 3. One price law ────────────────────────────────────────────────
    {
        // Canon base: buy discount 1%/point cha + the Trade ROW's percent per
        // bargaining rank (kSkillDefs — 1 today), floor 0.5; sell 0.7 ×
        // (1 + same bonus), cap 1.5. Context multiplies the base value
        // BEFORE the canon so its clamps still govern.
        if (trade_price(100, 10, 0, 1.0f, true) != 90) {
            return fail("buy: cha 10 must price 100 at 90");
        }
        // Bargaining walks the table door: rank 10 at the row's 1%/rank is
        // the same 10% edge charisma 10 buys — the tooltip and the till must
        // quote one column. (The old law charged an inline 2%/rank the
        // panel never promised; no assertion ever pinned it.)
        if (trade_price(100, 0, 10, 1.0f, true) != 90) {
            return fail("buy: bargaining 10 must price 100 at 90 (table 1%/rank)");
        }
        if (trade_price(100, 0, 10, 1.0f, false) != 77) {
            return fail("sell: bargaining 10 must price 100 at 77 (0.7 * 1.1)");
        }
        if (trade_price(100, 10, 0, 1.0f, false) != 77) {
            return fail("sell: cha 10 must price 100 at 77 (0.7 * 1.1)");
        }
        if (trade_price(100, 10, 0, 1.2f, true)
            != trade_price(120, 10, 0, 1.0f, true)) {
            return fail("context multiplier must equal scaling the base value");
        }
        if (trade_price(100, 200, 0, 1.0f, true) != 50) {
            return fail("buy discount must floor at 0.5 of base");
        }
        if (trade_price(1, 0, 0, 0.1f, false) < 1) {
            return fail("prices never fall below 1 gold");
        }
    }

    // ── 4. THE recovery door (CANON S14 «один рычаг», 2026-09-07) ───────
    // Shape, not pinned numbers (testing law #4/#5): each claim breaks alone.
    {
        const float base = 0.5f;   // the one-handed anchor
        Attributes a{};
        a[AttributeId::Spd] = 0;   // the bases start at 1 — zero the ONE input
        Skills s{};
        const int blank = recovery_steps(base, a, s, SkillId::Armsmaster);
        if (blank != int(steps_from_seconds(base))) {
            return fail("a zeroed sheet swings at exactly the row's base");
        }
        // Spd quickens through the SAME asymptote the legs walk on…
        Attributes fast = a;
        fast[AttributeId::Spd] = 50;
        const int quick = recovery_steps(base, fast, s, SkillId::Armsmaster);
        if (!(quick < blank)) return fail("Spd must quicken the arm");
        if (quickness_pct(50) != 150) {
            return fail("half-saturation must sit at spd 50 (the legs' curve)");
        }
        // …and the GENERIC skill multiplies on top — the typed one must NOT
        // (one handle, one lever: Sword already multiplied the dice).
        Skills sw{};
        sw[SkillId::Sword] = 100;
        if (recovery_steps(base, a, sw, SkillId::Armsmaster) != blank) {
            return fail("a typed skill must not touch the tempo");
        }
        s[SkillId::Armsmaster] = 100;
        const int master = recovery_steps(base, a, s, SkillId::Armsmaster);
        if (!(master < blank)) return fail("Armsmaster must quicken the arm");
        // Healthy to the ENGINE caps (attribute 255, rank 100): monotone,
        // positive, floored at the simulation's own quantum — never 0.
        Attributes cap{};
        cap[AttributeId::Spd] = 255;
        const int demigod = recovery_steps(base, cap, s, SkillId::Armsmaster);
        if (!(demigod >= 1 && demigod <= master)) {
            return fail("the law must stay sane to the byte cap");
        }
        if (recovery_steps(0.001f, cap, s, SkillId::Armsmaster) != 1) {
            return fail("the floor is one simulation step");
        }
    }

    std::printf("rpg_rules_test: preserve=ok wis=ok price_law=ok recovery=ok\n");
    CHECK(true, "every gate above held");
    return sm::test::report("rpg_rules_test");
}
