#include "macro/player_recovery.h"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL player_recovery_parity_test: %s\n", label);
        ++g_failures;
    }
}

void test_default_fractional_recovery_matches_ts_rate() {
    // Rates are INPUTS here (recovery reads CombatStats.*Regen, per game
    // hour); what this test guards is the fractional-carry mechanism itself.
    // 10.1/h: six minutes accumulate 1.01 — one visible point, no truncation.
    sm::PlayerState player{};
    player.combatStats.currentSp = 0;
    player.combatStats.currentHp = 0;
    player.combatStats.currentMp = 0;
    player.combatStats.maxSp = 100;
    player.combatStats.maxHp = 100;
    player.combatStats.maxMp = 100;
    player.combatStats.spRegen = 10.1f;
    player.combatStats.hpRegen = 10.1f;
    player.combatStats.mpRegen = 10.1f;

    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_minute_recovery(player, 5, accumulator);
    expect(player.combatStats.currentSp == 0
           && player.combatStats.currentHp == 0
           && player.combatStats.currentMp == 0,
           "sub-integer TS recovery is accumulated, not truncated into stats");

    sm::apply_minute_recovery(player, 1, accumulator);
    expect(player.combatStats.currentSp == 1
           && player.combatStats.currentHp == 1
           && player.combatStats.currentMp == 1,
           "six default minutes recover one visible point");
}

void test_attribute_rate_and_max_clamp() {
    // The rates are no longer restated here: recovery consumes the sheet's own
    // derived stats (calculate_combat_stats), so a tougher sheet recovers more
    // per hour because its DERIVED rates say so — HP/MP via the attribute
    // rate, SP as a percent of the bar (kSpRegenPctPerHour, Session 21).
    sm::PlayerState player{};
    player.sheet.attributes.end = 20;
    player.sheet.attributes.vit = 20;
    player.sheet.attributes.wil = 20;
    player.combatStats = sm::calculate_combat_stats(player.sheet.attributes,
                                                    player.sheet.skills);
    const int maxSp = player.combatStats.maxSp;   // 300: the END bar
    player.combatStats.currentSp = maxSp - 1;
    player.combatStats.currentHp = player.combatStats.maxHp - 1;
    player.combatStats.currentMp = player.combatStats.maxMp - 1;

    // 5 minutes: SP earns 300 × 1/8 / 12 ≈ 3.1 points, HP/MP one each — all
    // clamp to their maxima instead of overshooting.
    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_minute_recovery(player, 5, accumulator);
    expect(player.combatStats.currentSp == maxSp
           && player.combatStats.currentHp == player.combatStats.maxHp
           && player.combatStats.currentMp == player.combatStats.maxMp,
           "derived sheet rates recover the pools and clamp to max");

    player.combatStats.currentSp = maxSp;
    accumulator.sp = 0.9f;
    sm::apply_minute_recovery(player, 1, accumulator);
    expect(accumulator.sp == 0.0f, "full stat clears stale fractional accumulator");
}

void test_zero_minutes_are_noop() {
    sm::PlayerState player{};
    player.combatStats.currentSp = 10;
    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_minute_recovery(player, 0, accumulator);
    expect(player.combatStats.currentSp == 10, "zero minutes are a no-op");
}

} // namespace

int main() {
    test_default_fractional_recovery_matches_ts_rate();
    test_attribute_rate_and_max_clamp();
    test_zero_minutes_are_noop();

    if (g_failures != 0) {
        return 1;
    }

    std::puts("OK player_recovery_parity_test rate=ok clamp=ok zero=ok");
    return 0;
}
