#include "macro/player_recovery.h"

#include <algorithm>

namespace sm {

namespace {

void apply_fractional_recovery(float amount,
                               float& accumulator,
                               int& current,
                               int maximum) {
    if (maximum <= 0) {
        current = 0;
        accumulator = 0.0f;
        return;
    }
    if (current >= maximum) {
        current = maximum;
        accumulator = 0.0f;
        return;
    }
    if (amount <= 0.0f) {
        return;
    }

    accumulator += amount;
    const int whole = int(accumulator);
    if (whole <= 0) {
        return;
    }

    current = std::min(maximum, current + whole);
    accumulator -= float(whole);
    if (current >= maximum) {
        current = maximum;
        accumulator = 0.0f;
    }
}

} // namespace

void reset_player_recovery(PlayerRecoveryAccumulator& accumulator) {
    accumulator = PlayerRecoveryAccumulator{};
}

void apply_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator,
                                 float staminaRate) {
    if (minutes <= 0) {
        return;
    }
    if (staminaRate < 0.0f) staminaRate = 0.0f;

    auto& cs = player.combatStats;
    const float minutesScale = float(minutes) / 60.0f;

    // The hourly rates are the sheet's own derived stats (attributes.h
    // calculate_combat_stats) — this file used to restate the HP/MP formulas
    // and a THIRD one for SP; now it only converts per-hour to per-minute.
    // SP: maxSp × kSpRegenPctPerHour × marathon — percent of the bar, so a
    // full rest is 8 game hours for every sheet in the world (Session 21).
    apply_fractional_recovery(cs.spRegen * minutesScale * staminaRate,
                              accumulator.sp,
                              cs.currentSp,
                              cs.maxSp);
    apply_fractional_recovery(cs.hpRegen * minutesScale,
                              accumulator.hp,
                              cs.currentHp,
                              cs.maxHp);
    apply_fractional_recovery(cs.mpRegen * minutesScale,
                              accumulator.mp,
                              cs.currentMp,
                              cs.maxMp);
}

} // namespace sm
