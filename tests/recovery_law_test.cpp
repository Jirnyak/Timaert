// THE law of a body's bars over time (macro/recovery.h
// settle_pools_over_time) — ONE door, TWO always-on processes since the
// owner's ruling of 2026-10-06: a BURN that runs every hour at the rate of the
// ground under the body, and a REGEN that is flat, weight-blind and switched
// off by movement. They compose by SIGN, never by a branch.
//
// This file was `player_recovery_parity_test`, then the guard of the one rest
// law. What it guards now is that law plus the half that replaced the price of
// a step: the derived rate, the fractional carries, the full-bar rule, the
// signed SP remainder, Marathon's one lever, slice-invariance — and the two
// properties the new half brings, namely that marching heals NOTHING on any
// bar and that the exhaustion bite is CADENCE-FREE.
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
                                      /*regenerates=*/true);
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
    // On a HUMAN bar (110 — bar_ceilings of a bare sheet), where open water's
    // 20 SP/h genuinely outruns the 13.75 of rest. The veteran's 300-point bar
    // above would out-regenerate even the sea, which is the burn being
    // absolute while the regen is proportional — true, and the wrong fixture
    // for this claim.
    sm::ecs::Pools debtor = empty_body(100, 100, 110);
    debtor.sp = 110;
    debtor.spCarry = -0.4f;
    sm::settle_pools_over_time(
        debtor, 1.0f / 60.0f, 0,
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Biome::Water)),
        /*regenerates=*/true);
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

// ── THE BURN HALF (2026-10-06) ───────────────────────────────────────────

void test_burn_runs_while_regen_is_off() {
    // A marching body: the burn runs, the regen does not, and the law is the
    // SIGN of the two — not a branch that skips the call. A full bar on the
    // road loses its hour honestly.
    sm::ecs::Pools p = empty_body(110, 110, 110);
    p.hp = 50; p.mp = 50; p.sp = 110;
    const float roadBurn =
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road));
    sm::settle_pools_over_time(p, 1.0f, 0, roadBurn, /*regenerates=*/false);
    CHECK(float(p.sp) + p.spCarry > 110.0f - roadBurn - 0.001f
              && float(p.sp) + p.spCarry < 110.0f - roadBurn + 0.001f,
          "an hour of marching costs exactly its ground's burn, carry included");
    // «Марш не лечит НИЧЕГО» (CANON S14) — and all THREE bars, which is the
    // half that used to be held by the shape of the callers and is now one
    // argument. A marching lord mending in the saddle was expressible until
    // this line existed.
    CHECK(p.hp == 50 && p.mp == 50 && p.hpCarry == 0.0f && p.mpCarry == 0.0f,
          "marching moves NO bar up — not stamina, not health, not mana");
}

void test_ground_decides_whether_standing_still_wins() {
    // THE ladder, through the door rather than through the compile-time
    // guards: standing on the road gains, standing in open water loses. No
    // predicate names water anywhere — the sign of (regen − burn) does it.
    const float road =
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road));
    const float sea =
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Biome::Water));
    sm::ecs::Pools onRoad = empty_body(110, 110, 110);
    sm::ecs::Pools atSea = empty_body(110, 110, 110);
    onRoad.sp = 55; atSea.sp = 55;
    sm::settle_pools_over_time(onRoad, 1.0f, 0, road, /*regenerates=*/true);
    sm::settle_pools_over_time(atSea, 1.0f, 0, sea, /*regenerates=*/true);
    CHECK(float(onRoad.sp) + onRoad.spCarry > 55.0f,
          "an hour of camp on a road REPAYS more than it burns");
    CHECK(float(atSea.sp) + atSea.spCarry < 55.0f,
          "an hour afloat in open water burns more than any rest repays — "
          "the ocean is lethal by its PRICE, with no wall and no predicate");
}

void test_bite_is_cadence_free() {
    // THE property the whole shape of the bite rests on (movement_cost.h
    // bite_continuous_debt): the HP an hour of debt costs must NOT depend on
    // who settled it. The player's driver slices a game hour into 341 turns,
    // a squad's think into 10.67 — exactly 32× apart — so a bite charged per
    // CALL would have made the open sea 32× deadlier for the player than for
    // a lord standing beside him.
    // MEASURED, both ways (the numbers are recorded so the next reader can
    // repeat them rather than trust them — AGENTS §8 п.6):
    //   standing in the sea, 4 game hours: coarse bite 165 · fine bite 165
    //   marching in it, regen off:         coarse bite 5410 · fine bite 5410
    // A bite charged per CALL instead of per POINT reads 32 against 1024 of
    // them — not a drift, a different world for each driver.
    const float sea =
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Biome::Water));
    const auto drift = [&](int calls, float hours, bool regenerates) {
        sm::ecs::Pools p = empty_body(200, 200, 110);
        p.hp = 200;
        p.sp = 0;
        int bite = 0;
        for (int i = 0; i < calls; ++i) {
            bite += sm::settle_pools_over_time(p, hours, 0, sea, regenerates);
        }
        return std::pair<int, int>{bite, p.sp};
    };
    // 32 thinks of 1/8 h and 1024 turns of 1/256 h are the SAME four hours.
    const auto coarse = drift(32, 0.125f, true);
    const auto fine = drift(1024, 0.00390625f, true);
    const auto coarseMarch = drift(32, 0.125f, false);
    const auto fineMarch = drift(1024, 0.00390625f, false);

    CHECK(coarse.first > 0 && coarseMarch.first > coarse.first,
          "both drivers really went into debt, and the marching one deeper — "
          "without this the equalities below could hold at zero");
    CHECK(coarse.first == fine.first && coarse.second == fine.second,
          "the bite of four hours adrift is the SAME whichever driver settles "
          "it, and the bar lands on the same point");
    CHECK(coarseMarch.first == fineMarch.first
              && coarseMarch.second == fineMarch.second,
          "...and the same holds with the regen off, where the debt is four "
          "times deeper and the quadratic curve least forgiving");
    // NOT asserted: identical HP. The bite is cadence-free, but HP also
    // carries the REGEN, and `recover_bar`'s own older rule — a full bar
    // cannot bank rest — throws away a whole coarse slice at the ceiling and
    // only a fine sliver of a fine one. Measured: 97 against 98 over those
    // four hours. That is the cap rule being cadence-sensitive, not this law,
    // and claiming otherwise is exactly the kind of over-wide assertion that
    // makes a witness fail for the wrong reason.
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
    test_burn_runs_while_regen_is_off();
    test_ground_decides_whether_standing_still_wins();
    test_bite_is_cadence_free();
    test_rising_bar_never_bites();
    return sm::test::report("recovery_law_test");
}
