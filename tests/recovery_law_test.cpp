// THE rest law (macro/recovery.h rest_pools) — the one implementation both
// scales call since landing 4. This file was `player_recovery_parity_test`:
// it guarded the agreement between the player's cached-rate recovery and the
// macro AI's inline copy of the same formulas. That agreement is now held by
// construction — one function — so what is left to guard is the LAW itself:
// the derived rate, the fractional carries, the full-bar rule, the signed
// SP remainder, Marathon's one lever, and slice-invariance (the property the
// conservation smokes lean on).
#include "check.h"

#include "ecs/pools.h"
#include "macro/attributes.h"
#include "macro/recovery.h"

#include <cmath>

namespace {

sm::ecs::Pools empty_body(int maxHp, int maxMp, int maxSp) {
    sm::ecs::Pools p{};
    p.maxHp = maxHp;
    p.maxMp = maxMp;
    p.maxSp = maxSp;
    return p;
}

void test_fractional_recovery_accumulates() {
    // 100-point bars at THE rate (kRestRegenPctPerHour = 1/8): 12.5/h.
    // Four minutes accumulate 0.833 — nothing visible; two more cross 1.0.
    sm::ecs::Pools p = empty_body(100, 100, 100);
    sm::rest_pools(p, 4.0f / 60.0f, 0);
    CHECK(p.sp == 0 && p.hp == 0 && p.mp == 0,
          "sub-integer recovery is accumulated, not truncated into the bar");
    sm::rest_pools(p, 2.0f / 60.0f, 0);
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
    sm::rest_pools(p, 5.0f / 60.0f, 0);
    CHECK(p.sp == c.maxSp && p.hp == c.maxHp && p.mp == c.maxMp,
          "derived-ceiling rates recover the pools and clamp to max");

    p.sp = c.maxSp;
    p.spCarry = 0.9f;
    sm::rest_pools(p, 1.0f / 60.0f, 0);
    CHECK(p.spCarry == 0.0f,
          "a full bar cannot bank rest: a POSITIVE remainder on it is dropped");

    // ...and the other sign is not the same thing. A march that begins from a
    // full bar owes a fraction the instant it takes its first step; clearing
    // that as "stale" would leak cost at the start of every journey.
    p.sp = c.maxSp;
    p.spCarry = -0.4f;
    sm::rest_pools(p, 1.0f / 60.0f, 0);
    CHECK(p.spCarry == -0.4f,
          "a NEGATIVE remainder on a full bar is march debt, and it is kept");
}

void test_marathon_multiplies_sp_only() {
    // Marathon is the ONE lever on the rest rate, and it touches SP alone
    // (CANON S14: bar and rest are two levers; Bodybuilding fattens the bar,
    // Marathon shortens the night). Rank 100 = ×2 under THE skill law.
    sm::ecs::Pools slow = empty_body(100, 100, 100);
    sm::ecs::Pools fast = empty_body(100, 100, 100);
    sm::rest_pools(slow, 1.0f, 0);
    sm::rest_pools(fast, 1.0f, 100);
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
        sm::rest_pools(sliced, 1.0f / 60.0f, 17);
    }
    sm::rest_pools(whole, 1.0f, 17);
    CHECK(sliced.hp == whole.hp && sliced.mp == whole.mp
              && sliced.sp == whole.sp,
          "sixty minute-slices buy exactly what one hour buys");
    CHECK(whole.hp > 0, "the slicing check measured a real recovery");
}

void test_zero_hours_are_noop() {
    sm::ecs::Pools p = empty_body(100, 100, 100);
    p.sp = 10;
    sm::rest_pools(p, 0.0f, 0);
    CHECK(p.sp == 10, "zero hours are a no-op");
}

} // namespace

int main() {
    test_fractional_recovery_accumulates();
    test_derived_ceilings_recover_and_clamp();
    test_marathon_multiplies_sp_only();
    test_slices_equal_one_rest();
    test_zero_hours_are_noop();
    return sm::test::report("recovery_law_test");
}
