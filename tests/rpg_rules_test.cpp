// Locks three owner rulings of 2026-08-05:
//
//   1. NO FREE FULL HEAL — recompute_combat_maxima grows the maxima and
//      PRESERVES the current pools (clamped). The full restore in
//      calculate_combat_stats belongs to creation and the level-up. (The bug:
//      every '+' click on an attribute/skill was a free full heal because the
//      UI recomputed through the creating function.)
//   2. THE WIS DIVIDEND — award_exp(ld, amount, expMult) scales the grant by
//      the sheet's expMult (+1% per wis point), round half up. Before this,
//      wis was computed and consumed by nothing.
//   3. ONE PRICE LAW — player_trade_price = the canonical charisma+bargaining
//      pricing (formerly dead code while three homegrown UI laws diverged)
//      with context (mood/trait) as one multiplier column.

#include "check.h"
#include "macro/attributes.h"
#include "macro/economy.h"

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

    // ── 1. Point spend preserves the pools ──────────────────────────────
    {
        Attributes a{};
        Skills s{};
        CombatStats c = calculate_combat_stats(a, s);
        c.currentHp = 7;
        c.currentMp = 3;
        c.currentSp = 5;
        a.vit += 1;  // the spend
        recompute_combat_maxima(c, a, s);
        if (c.maxHp != calculate_combat_stats(a, s).maxHp) {
            return fail("maxima must recompute from the new attributes");
        }
        if (c.currentHp != 7 || c.currentMp != 3 || c.currentSp != 5) {
            std::fprintf(stderr, "hp=%d mp=%d sp=%d\n",
                         c.currentHp, c.currentMp, c.currentSp);
            return fail("spending a point must not touch the current pools");
        }
        // Shrinking maxima (e.g. a future curse) clamps the pools down.
        CombatStats over = calculate_combat_stats(a, s);
        over.currentHp = over.maxHp + 500;
        recompute_combat_maxima(over, a, s);
        if (over.currentHp != over.maxHp) {
            return fail("currents must clamp into the new maxima");
        }
        // The level-up path (calculate_combat_stats) still fully restores.
        const CombatStats fresh = calculate_combat_stats(a, s);
        if (fresh.currentHp != fresh.maxHp || fresh.currentSp != fresh.maxSp) {
            return fail("the level-up restore must stay a FULL restore");
        }
    }

    // ── 2. The wis dividend ─────────────────────────────────────────────
    {
        Attributes a{};
        a.wis = 10;
        const float mult = calculate_derived(a, Skills{}).expMult;
        LevelData ld = default_level_data();
        award_exp(ld, 100, mult);
        if (ld.exp != 110) {
            std::fprintf(stderr, "exp=%d\n", ld.exp);
            return fail("wis 10 must turn 100 xp into 110");
        }
        LevelData ld2 = default_level_data();
        award_exp(ld2, 25, mult);  // 27.5 -> round half up -> 28
        if (ld2.exp != 28) {
            std::fprintf(stderr, "exp=%d\n", ld2.exp);
            return fail("the dividend rounds half up (25 * 1.1 = 28)");
        }
        LevelData ld3 = default_level_data();
        award_exp(ld3, 100, 1.0f);
        if (ld3.exp != 100) return fail("expMult 1.0 must be the identity");
    }

    // ── 3. One price law ────────────────────────────────────────────────
    {
        // Canon base: buy discount 1% cha + 2% bargaining, floor 0.5;
        // sell 0.7 × (1 + same bonus), cap 1.5. Context multiplies the base
        // value BEFORE the canon so its clamps still govern.
        if (player_trade_price(100, 10, 0, 1.0f, true) != 90) {
            return fail("buy: cha 10 must price 100 at 90");
        }
        if (player_trade_price(100, 10, 0, 1.0f, false) != 77) {
            return fail("sell: cha 10 must price 100 at 77 (0.7 * 1.1)");
        }
        if (player_trade_price(100, 10, 0, 1.2f, true)
            != player_trade_price(120, 10, 0, 1.0f, true)) {
            return fail("context multiplier must equal scaling the base value");
        }
        if (player_trade_price(100, 200, 0, 1.0f, true) != 50) {
            return fail("buy discount must floor at 0.5 of base");
        }
        if (player_trade_price(1, 0, 0, 0.1f, false) < 1) {
            return fail("prices never fall below 1 gold");
        }
    }

    std::printf("rpg_rules_test: preserve=ok wis=ok price_law=ok\n");
    CHECK(true, "every gate above held");
    return sm::test::report("rpg_rules_test");
}
