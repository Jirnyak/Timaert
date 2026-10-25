// THE law of a body's bars over time (macro/recovery.h
// settle_pools_over_time) — ONE door, TWO processes composing by SIGN, never
// by a branch. WHICH of the two is always-on was INVERTED by the owner on
// 2026-10-24 (дословно: «надо наоборот всегда пассивный реген и при
// передвижение жжот сп по весу»), and this file is the witness of the new
// order, not of the old:
//   · the SP REGEN is flat, weight-blind and UNCONDITIONAL;
//   · the SP BURN runs at the rate of the ground under the body and ONLY
//     while that body is in motion;
//   · HP and MP still recover only at a halt — «марш не лечит НИЧЕГО»
//     (CANON S14) survives on those two bars and is lifted on SP.
//
// This file was `player_recovery_parity_test`, then the guard of the one rest
// law. What it guards now is that law plus the half that replaced the price of
// a step: the derived rate, the fractional carries, the full-bar rule, the
// signed SP remainder, Marathon's one lever, slice-invariance, the CADENCE-FREE
// exhaustion bite — and the property the inversion exists for, namely that a
// STANDING body pays nothing for its ground and nothing for its load, however
// monstrous either is (M-310).
#include "check.h"

#include "ecs/pools.h"
#include "macro/anketa.h"
#include "macro/movement_cost.h"
#include "macro/recovery.h"

#include <cmath>
#include <utility>

namespace {

sm::ecs::Pools empty_body(int maxHp, int maxMp, int maxSp) {
    sm::ecs::Pools p{};
    p.maxHp = maxHp;
    p.maxMp = maxMp;
    p.maxSp = maxSp;
    return p;
}

// THE regen half in isolation: a body standing on weightless ground. Zero is a
// legal rate for this door and not a special case — the door is agnostic about
// who calls it and with what (AGENTS ЗАКОН АГНОСТИЧНОСТИ), which is exactly
// what lets a unit test take the two processes apart.
int rest_only(sm::ecs::Pools& p, float hours, int marathonRank) {
    return sm::settle_pools_over_time(p, hours, marathonRank,
                                      /*burnPerHour=*/0.0f,
                                      /*resting=*/true);
}

void test_fractional_recovery_accumulates() {
    // 100-point bars at THE rate (kRestRegenPctPerHour = 1/8): 12.5/h.
    // Four minutes accumulate 0.833 — nothing visible; two more cross 1.0.
    sm::ecs::Pools p = empty_body(100, 100, 100);
    rest_only(p, 4.0f / 60.0f, 0);
    CHECK(p.sp == 0 && p.hp == 0 && p.mp == 0,
          "sub-integer recovery is accumulated, not truncated into the bar");
    rest_only(p, 2.0f / 60.0f, 0);
    CHECK(p.sp == 1 && p.hp == 1 && p.mp == 1,
          "six minutes at the one rate recover one visible point");
}

void test_derived_ceilings_recover_and_clamp() {
    // The ceilings come from the sheet's own door (bar_ceilings) — a tougher
    // sheet recovers more per hour BECAUSE its bar is bigger, never because
    // of a second rate. END/WIL 20: maxSp = 100 + ((20+20)>>1)*10 = 300.
    sm::Attributes a{};
    a[sm::AttributeId::End] = 20;
    a[sm::AttributeId::Wil] = 20;
    const sm::BarCeilings c = sm::bar_ceilings(a, sm::Skills{}, 100, 100, 100);
    sm::ecs::Pools p = empty_body(c.maxHp, c.maxMp, c.maxSp);
    p.hp = c.maxHp - 1;
    p.mp = c.maxMp - 1;
    p.sp = c.maxSp - 1;
    // 5 minutes: SP earns 300 × 1/8 / 12 ≈ 3.1 points, HP/MP over one each —
    // all clamp to their maxima instead of overshooting.
    rest_only(p, 5.0f / 60.0f, 0);
    CHECK(p.sp == c.maxSp && p.hp == c.maxHp && p.mp == c.maxMp,
          "derived-ceiling rates recover the pools and clamp to max");

    p.sp = c.maxSp;
    p.spCarry = 0.9f;
    rest_only(p, 1.0f / 60.0f, 0);
    CHECK(p.spCarry == 0.0f,
          "a full bar cannot bank rest: a POSITIVE remainder on it is dropped");

    // ...and the other sign is not the same thing: a march that begins from a
    // full bar owes a fraction the instant it takes its first step, and
    // FORGIVING that would leak cost at the start of every journey. The clamp
    // above touches only the positive side, so a debt can be repaid but never
    // waived — shown here where the burn outruns the rest, so there is nothing
    // to repay it with.
    // On a HUMAN bar (110 — bar_ceilings of a bare sheet) and IN MOTION, where
    // open water's 20 SP/h genuinely outruns the 13.75 of rest. The veteran's
    // 300-point bar above would out-regenerate even the sea, which is the burn
    // being absolute while the regen is proportional — true, and the wrong
    // fixture for this claim. `resting=false` is likewise not decoration: since
    // 2026-10-24 a HALTED body has no burn at all, so a standing fixture could
    // not express march debt in the first place.
    sm::ecs::Pools debtor = empty_body(100, 100, 110);
    debtor.sp = 110;
    debtor.spCarry = -0.4f;
    sm::settle_pools_over_time(
        debtor, 1.0f / 60.0f, 0,
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Biome::Water)),
        /*resting=*/false);
    CHECK(debtor.spCarry < -0.4f,
          "a NEGATIVE remainder on a full bar is march debt: it is never "
          "waived by the full-bar clamp, and an hour that burns more than it "
          "repays drives it DEEPER");
}

void test_marathon_multiplies_sp_only() {
    // Marathon is the ONE lever on the rest rate, and it touches SP alone
    // (CANON S14: bar and rest are two levers; Bodybuilding fattens the bar,
    // Marathon shortens the night). Rank 100 = ×2 under THE skill law.
    sm::ecs::Pools slow = empty_body(100, 100, 100);
    sm::ecs::Pools fast = empty_body(100, 100, 100);
    rest_only(slow, 1.0f, 0);
    rest_only(fast, 1.0f, 100);
    // Earned = bar + remainder: the carry keeps the comparison exact where
    // the truncation into whole points would round the doubling away.
    const float slowEarned = float(slow.sp) + slow.spCarry;
    const float fastEarned = float(fast.sp) + fast.spCarry;
    CHECK(std::fabs(fastEarned - 2.0f * slowEarned) < 0.001f
              && slowEarned > 0.0f,
          "marathon 100 doubles the SP slice of an hour");
    CHECK(fast.hp == slow.hp && fast.mp == slow.mp,
          "marathon leaves HP and MP at the plain rate");
}

void test_slices_equal_one_rest() {
    // The property the conservation smokes lean on: a rest paid in many
    // small slices lands exactly where one slice of the same total lands —
    // the carry makes the roundings identical.
    sm::ecs::Pools sliced = empty_body(110, 110, 110);
    sm::ecs::Pools whole = empty_body(110, 110, 110);
    for (int i = 0; i < 60; ++i) {
        rest_only(sliced, 1.0f / 60.0f, 17);
    }
    rest_only(whole, 1.0f, 17);
    CHECK(sliced.hp == whole.hp && sliced.mp == whole.mp
              && sliced.sp == whole.sp,
          "sixty minute-slices buy exactly what one hour buys");
    CHECK(whole.hp > 0, "the slicing check measured a real recovery");
}

void test_zero_hours_are_noop() {
    sm::ecs::Pools p = empty_body(100, 100, 100);
    p.sp = 10;
    CHECK(rest_only(p, 0.0f, 0) == 0 && p.sp == 10,
          "zero hours are a no-op");
}

// ── THE BURN HALF — A PROCESS OF THE ROAD, AND ONLY OF IT (2026-10-24) ───

void test_march_pays_its_burn_against_the_passive_regen() {
    // A marching body: BOTH processes run, and the bar moves by their SIGNED
    // sum. This is the inversion itself — before 2026-10-24 the assertion here
    // was `−roadBurn`, because the regen was the half movement switched off.
    sm::ecs::Pools p = empty_body(110, 110, 110);
    p.hp = 50; p.mp = 50; p.sp = 55;
    const float roadBurn =
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road));
    const float regen = sm::rest_stamina_per_hour(110, 0);
    sm::settle_pools_over_time(p, 1.0f, 0, roadBurn, /*resting=*/false);
    const float moved = float(p.sp) + p.spCarry - 55.0f;
    CHECK(std::fabs(moved - (regen - roadBurn)) < 0.001f,
          "an hour of marching moves the bar by REGEN MINUS the ground's "
          "burn, carry included — both processes, one sign");
    // ...and the direction of that sum on the best ground in the world is UP,
    // which is the owner's inversion stated as a consequence rather than a
    // formula. Asserted separately and deliberately: it is the one claim of
    // this file that the 2026-10-06 law forbade outright, so a silent revert
    // to the old order lands here.
    CHECK(moved > 0.0f,
          "a road march NETS a gain: «марш не лечит» is lifted on SP");
    // «Марш не лечит НИЧЕГО» (CANON S14) on the two bars where it SURVIVES.
    // It covered all three until 2026-10-24; SP is now the exception, and that
    // is why this check names its bars instead of saying «no bar».
    CHECK(p.hp == 50 && p.mp == 50 && p.hpCarry == 0.0f && p.mpCarry == 0.0f,
          "marching moves no HEALTH and no MANA — not even a carry");
}

void test_ground_decides_whether_the_march_pays_for_itself() {
    // THE ladder, through the door rather than through the compile-time
    // guards, and since 2026-10-24 it is a ladder of the MARCH: walking the
    // road gains, swimming open water loses. No predicate names water anywhere
    // — the sign of (regen − burn) does it.
    const float road =
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road));
    const float sea =
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Biome::Water));
    sm::ecs::Pools onRoad = empty_body(110, 110, 110);
    sm::ecs::Pools atSea = empty_body(110, 110, 110);
    onRoad.sp = 55; atSea.sp = 55;
    sm::settle_pools_over_time(onRoad, 1.0f, 0, road, /*resting=*/false);
    sm::settle_pools_over_time(atSea, 1.0f, 0, sea, /*resting=*/false);
    CHECK(float(onRoad.sp) + onRoad.spCarry > 55.0f,
          "an hour of ROAD march repays more than it burns");
    CHECK(float(atSea.sp) + atSea.spCarry < 55.0f,
          "an hour SWUM in open water burns more than the passive regen "
          "repays — the ocean is dear by its PRICE, with no wall and no "
          "predicate");

    // ...AND THE SAME TWO GROUNDS UNDER A HALTED BODY ARE INDISTINGUISHABLE.
    // This is the half the inversion added, and it is why the sea no longer
    // drowns a body that stops in it: the ground enters the law only through
    // the burn, and a halt has no burn to enter.
    sm::ecs::Pools campRoad = empty_body(110, 110, 110);
    sm::ecs::Pools campSea = empty_body(110, 110, 110);
    campRoad.sp = 55; campSea.sp = 55;
    sm::settle_pools_over_time(campRoad, 1.0f, 0, road, /*resting=*/true);
    sm::settle_pools_over_time(campSea, 1.0f, 0, sea, /*resting=*/true);
    CHECK(campRoad.sp == campSea.sp && campRoad.spCarry == campSea.spCarry,
          "a HALTED body cannot tell the open sea from a paved road: its "
          "ground costs it nothing at all");
    CHECK(float(campSea.sp) + campSea.spCarry > 55.0f,
          "...and it RECOVERS out there, which is the whole point of the "
          "inversion — standing is never a negative");
}

void test_standing_ignores_its_load_however_monstrous() {
    // THE OWNER'S VERDICT OF 2026-10-24, AS AN ASSERTION (дословно: «город
    // может быть "технически перегружен" но ему всё равно потому что он не
    // ходит (зато если какоето метсо будет ходить то сразу перегруз будет жечь
    // сп!»). The burden is a FACTOR of the burn, so a halted body — whose burn
    // is zero — cannot be hurt by it however heavy the pack, and nothing had
    // to be gated to make that so.
    //
    // WHY THIS WITNESS EXISTS AT ALL, числом: while the burden was an added SP
    // term, the worst city of M-310 carried 379 361 kg against a 99 560 kg
    // back, paid 279 801 SP an hour STANDING STILL, and the world erased its
    // own places — 2275 → 90, population 1 090 311 → 52 445. Nothing in the
    // suite saw it.
    const float seaBurn = sm::biome_sp_weight(sm::Biome::Water);
    // The three fixtures: an empty back, that measured city (1 + 279 801 /
    // 99 560), and an absurdity far past anything the world can build.
    const float cityBurden = 1.0f + 279801.0f / 99560.0f;
    int samples = 0;
    int standSp = 0;
    float standCarry = 0.0f;
    float prevMarchLeft = 1.0e9f;
    for (const float burden : {1.0f, cityBurden, 2798.0f}) {
        sm::ecs::Pools camped = empty_body(110, 110, 110);
        sm::ecs::Pools walking = empty_body(110, 110, 110);
        camped.sp = 55; walking.sp = 55;
        const float rate = sm::burn_stamina_per_hour(seaBurn, burden);
        sm::settle_pools_over_time(camped, 1.0f, 0, rate, /*resting=*/true);
        sm::settle_pools_over_time(walking, 1.0f, 0, rate, /*resting=*/false);
        if (samples == 0) {
            standSp = camped.sp;
            standCarry = camped.spCarry;
        }
        CHECK(camped.sp == standSp && camped.spCarry == standCarry,
              "a HALTED body lands on the same point whatever it is carrying "
              "— the load is a factor of a burn it does not pay");
        const float marchLeft = float(walking.sp) + walking.spCarry;
        CHECK(marchLeft < prevMarchLeft,
              "...while a MARCHING body pays strictly more for a heavier pack "
              "— the negative control, so the equality above is not the "
              "burden being ignored everywhere");
        prevMarchLeft = marchLeft;
        ++samples;
    }
    CHECK(samples == 3, "the burden sweep really ran all three fixtures");
}

void test_bite_is_cadence_free() {
    // THE property the whole shape of the bite rests on (movement_cost.h
    // bite_spent_debt): the HP an hour of debt costs must NOT depend on
    // who settled it. The player's driver slices a game hour into 341 turns,
    // a squad's think into 10.67 — exactly 32× apart — so a bite charged per
    // CALL would have made the open sea 32× deadlier for the player than for
    // a lord standing beside him.
    // TWO DEPTHS, AND SINCE 2026-10-24 THEY COME FROM THE PACK rather than
    // from switching the regen off — the regen cannot be switched off any
    // more, so the only way to make an hour deeper is to make it dearer, and
    // the burden factor is exactly that knob. Swimming unburdened nets
    // 20 − 13.75 = 6.25 SP/h of debt; swimming under a doubled pack nets
    // 40 − 13.75 = 26.25. Over four hours: 25 and 105 points of debt.
    //
    // A bite charged per CALL instead of per POINT would read 32 against 1024
    // of them — not a drift, a different world for each driver.
    const auto drift = [&](int calls, float hours, float burden) {
        sm::ecs::Pools p = empty_body(200, 200, 110);
        p.hp = 200;
        p.sp = 0;
        const float rate = sm::burn_stamina_per_hour(
            sm::biome_sp_weight(sm::Biome::Water), burden);
        int bite = 0;
        for (int i = 0; i < calls; ++i) {
            bite += sm::settle_pools_over_time(p, hours, 0, rate,
                                               /*resting=*/false);
        }
        return std::pair<int, int>{bite, p.hp};
    };
    // 32 thinks of 1/8 h and 1024 turns of 1/256 h are the SAME four hours.
    const auto coarse = drift(32, 0.125f, 1.0f);
    const auto fine = drift(1024, 0.00390625f, 1.0f);
    const auto coarseHeavy = drift(32, 0.125f, 2.0f);
    const auto fineHeavy = drift(1024, 0.00390625f, 2.0f);

    CHECK(coarse.first > 0 && coarseHeavy.first > coarse.first,
          "both drivers really went into debt, and the laden one deeper — "
          "without this the equalities below could hold at zero");
    CHECK(coarse.first == fine.first && coarse.second == fine.second,
          "the bite of four hours afloat is the SAME whichever driver settles "
          "it, and the health left lands on the same point");
    CHECK(coarseHeavy.first == fineHeavy.first
              && coarseHeavy.second == fineHeavy.second,
          "...and the same holds under a doubled pack, where the debt is four "
          "times deeper and the quadratic curve least forgiving");
    // HP is now asserted, and it could not be before: while the regen rode on
    // `resting`, these fixtures also carried HP recovery, and `recover_bar`'s
    // own older rule — a full bar cannot bank rest — threw away a whole coarse
    // slice at the ceiling and only a fine sliver of a fine one (measured 97
    // against 98). A marching body recovers no HP at all, so the only thing
    // moving it here is the bite, and the claim is clean.
}

void test_rising_bar_never_bites() {
    // The bite is asked after EVERY settle, unconditionally, so a rising bar
    // must be provably free: otherwise every resting body in the world would
    // bleed. A body already in debt that is REPAYING pays nothing.
    sm::ecs::Pools p = empty_body(100, 100, 110);
    p.hp = 100;
    p.sp = -20;
    const int bite = rest_only(p, 1.0f, 0);
    CHECK(bite == 0 && p.hp == 100,
          "a body climbing OUT of debt is not bitten for the debt it had");
    CHECK(p.sp > -20, "the climb-out measured a real repayment");
}

} // namespace

int main() {
    test_fractional_recovery_accumulates();
    test_derived_ceilings_recover_and_clamp();
    test_marathon_multiplies_sp_only();
    test_slices_equal_one_rest();
    test_zero_hours_are_noop();
    test_march_pays_its_burn_against_the_passive_regen();
    test_ground_decides_whether_the_march_pays_for_itself();
    test_standing_ignores_its_load_however_monstrous();
    test_bite_is_cadence_free();
    test_rising_bar_never_bites();
    return sm::test::report("recovery_law_test");
}
