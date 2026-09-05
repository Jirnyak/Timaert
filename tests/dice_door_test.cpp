// Locks the dice-door contract (core/dice.h, CANON S13/S14).
//
// What is genuinely promised and asserted here:
//   * bounds: an NdM roll lands in [n, n·m], always;
//   * Nd1 is FIXED: returns n and consumes zero rng draws, so fixed damage
//     cannot perturb a seeded stream;
//   * stream contract: a roll consumes exactly n draws;
//   * the crit law: frequency tracks luck·5 per mille (1 LCK = 0.5%), zero
//     luck draws nothing, 200+ luck always crits;
//   * dice_mean_x2 agrees with what roll_dice actually produces — the
//     auto-resolve door and the fought door share one expectation.
//
// Every sweep counts what it examined and asserts the count (testing law #3).
#include "check.h"

#include "core/dice.h"

#include <cstdint>
#include <cstdlib>

namespace {

void test_bounds() {
    const sm::Dice kShapes[] = {{1, 6}, {3, 4}, {2, 12}, {5, 2}, {20, 12}};
    int rolls = 0, outside = 0;
    sm::Rng rng(1u);
    for (const sm::Dice& d : kShapes) {
        for (int i = 0; i < 10000; ++i) {
            ++rolls;
            const int v = sm::roll_dice(rng, d);
            if (v < int(d.n) || v > int(d.n) * int(d.m)) ++outside;
        }
    }
    CHECK(rolls == 50000 && outside == 0, "every NdM roll lands in [n, n*m]");
}

void test_fixed_nd1_draws_nothing() {
    sm::Rng rng(7u);
    const std::uint32_t before = rng.state;
    int calls = 0, wrong = 0;
    for (int n = 0; n <= 8; ++n) {
        ++calls;
        if (sm::roll_dice(rng, {std::uint8_t(n), 1}) != n) ++wrong;
    }
    CHECK(calls == 9 && wrong == 0, "Nd1 returns exactly n — fixed damage is a roll in name only");
    CHECK(rng.state == before, "Nd1 consumes ZERO rng draws — fixed sources cannot shift a stream");

    // Negative control: a real die DOES move the stream.
    sm::roll_dice(rng, {1, 2});
    CHECK(rng.state != before, "a real die consumes the stream (the zero-draw check can fail)");
}

void test_roll_consumes_exactly_n_draws() {
    sm::Rng a(31337u);
    sm::Rng b(31337u);
    const sm::Dice d{3, 6};
    sm::roll_dice(a, d);
    int draws = 0;
    for (int i = 0; i < int(d.n); ++i, ++draws) b.next_u32();
    CHECK(draws == 3 && a.state == b.state,
          "an NdM roll consumes exactly n draws — nothing hidden leaks draws");
}

void test_crit_law() {
    // Zero luck must not touch the stream (mirror of the Nd1 contract).
    sm::Rng z(55u);
    const std::uint32_t before = z.state;
    int zeroCalls = 0, zeroProcs = 0;
    for (int i = 0; i < 1000; ++i) {
        ++zeroCalls;
        if (sm::crit_procs(z, 0)) ++zeroProcs;
    }
    CHECK(zeroCalls == 1000 && zeroProcs == 0 && z.state == before,
          "LCK 0 never crits and consumes ZERO rng draws");

    // Frequency tracks the law luck*5/1000. Expected counts are derived from
    // the same formula the code reads, not restated literals (testing law #4);
    // slack 1% of samples is ~6 sigma at the worst point of the sweep.
    const int kLucks[] = {20, 100, 199};
    const int kSamples = 40000;
    int points = 0, offLaw = 0;
    sm::Rng rng(4096u);
    for (int luck : kLucks) {
        ++points;
        int procs = 0;
        for (int i = 0; i < kSamples; ++i)
            if (sm::crit_procs(rng, luck)) ++procs;
        const long want = long(kSamples) * long(luck) * 5L / 1000L;
        const long slack = kSamples / 100;
        if (std::labs(long(procs) - want) > slack) ++offLaw;
    }
    CHECK(points == 3 && offLaw == 0,
          "crit frequency tracks luck*5 per mille — 1 LCK = 0.5% crit, verbatim");

    // 200 luck = 1000 per mille: the draw cannot lose.
    sm::Rng full(77u);
    int fullCalls = 0, fullProcs = 0;
    for (int i = 0; i < 1000; ++i) {
        ++fullCalls;
        if (sm::crit_procs(full, 200 + (i & 55))) ++fullProcs;
    }
    CHECK(fullCalls == 1000 && fullProcs == 1000, "LCK 200+ crits every single hit");
}

void test_determinism() {
    sm::Rng a(0xC0FFEEu);
    sm::Rng b(0xC0FFEEu);
    int rolls = 0, diverged = 0;
    for (int i = 0; i < 100; ++i) {
        ++rolls;
        if (sm::roll_dice(a, {2, 8}) != sm::roll_dice(b, {2, 8})) ++diverged;
        if (sm::crit_procs(a, 50) != sm::crit_procs(b, 50)) ++diverged;
    }
    CHECK(rolls == 100 && diverged == 0, "same seed + same args = same rolls and crits, always");
}

void test_mean_x2_agrees_with_the_roll() {
    // The two doors (rolled damage, auto-resolve expectation) must describe
    // the same die. Assert the empirical mean against dice_mean_x2, not
    // against a restated literal (testing law #4).
    const sm::Dice kShapes[] = {{1, 6}, {2, 6}, {3, 4}, {4, 1}};
    int shapes = 0, disagreed = 0;
    for (const sm::Dice& d : kShapes) {
        ++shapes;
        const int kSamples = 40000;
        long sum = 0;
        sm::Rng rng(4242u);
        for (int i = 0; i < kSamples; ++i) sum += sm::roll_dice(rng, d);
        // sum·2 ≈ samples·mean_x2; 0.2 pips of doubled-mean slack
        // (~14 sigma for the widest die here — fails only if the law is wrong).
        const long want = long(kSamples) * long(sm::dice_mean_x2(d));
        const long slack = long(kSamples) / 5;
        if (std::labs(sum * 2 - want) > slack) ++disagreed;
    }
    CHECK(shapes == 4 && disagreed == 0,
          "dice_mean_x2 is the expectation of roll_dice — auto-resolve and fought combat share one law");
    CHECK(sm::dice_mean_x2({5, 1}) == 10, "Nd1 expectation is exactly n (doubled: 2n)");
}

} // namespace

int main() {
    test_bounds();
    test_fixed_nd1_draws_nothing();
    test_roll_consumes_exactly_n_draws();
    test_crit_law();
    test_determinism();
    test_mean_x2_agrees_with_the_roll();
    return sm::test::report("dice_door_test");
}
