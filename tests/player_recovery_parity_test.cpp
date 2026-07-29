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
    sm::PlayerState player{};
    player.sheet.attributes.end = 1;
    player.sheet.attributes.vit = 1;
    player.sheet.attributes.wil = 1;
    player.combatStats.currentSp = 0;
    player.combatStats.currentHp = 0;
    player.combatStats.currentMp = 0;
    player.combatStats.maxSp = 100;
    player.combatStats.maxHp = 100;
    player.combatStats.maxMp = 100;

    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_macro_minute_recovery(player, 5, accumulator);
    expect(player.combatStats.currentSp == 0
           && player.combatStats.currentHp == 0
           && player.combatStats.currentMp == 0,
           "sub-integer TS recovery is accumulated, not truncated into stats");

    sm::apply_macro_minute_recovery(player, 1, accumulator);
    expect(player.combatStats.currentSp == 1
           && player.combatStats.currentHp == 1
           && player.combatStats.currentMp == 1,
           "six default minutes recover one visible point");
}

void test_attribute_rate_and_max_clamp() {
    sm::PlayerState player{};
    player.sheet.attributes.end = 20;
    player.sheet.attributes.vit = 20;
    player.sheet.attributes.wil = 20;
    player.combatStats.currentSp = 99;
    player.combatStats.currentHp = 49;
    player.combatStats.currentMp = 9;
    player.combatStats.maxSp = 100;
    player.combatStats.maxHp = 50;
    player.combatStats.maxMp = 10;

    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_macro_minute_recovery(player, 5, accumulator);
    expect(player.combatStats.currentSp == 100
           && player.combatStats.currentHp == 50
           && player.combatStats.currentMp == 10,
           "high attributes recover at TS base-rate scaling and clamp to max");

    player.combatStats.currentSp = 100;
    accumulator.sp = 0.9f;
    sm::apply_macro_minute_recovery(player, 1, accumulator);
    expect(accumulator.sp == 0.0f, "full stat clears stale fractional accumulator");
}

void test_zero_minutes_are_noop() {
    sm::PlayerState player{};
    player.combatStats.currentSp = 10;
    sm::PlayerRecoveryAccumulator accumulator{};
    sm::apply_macro_minute_recovery(player, 0, accumulator);
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
