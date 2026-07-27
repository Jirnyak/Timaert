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

void apply_macro_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator) {
    if (minutes <= 0) {
        return;
    }

    auto& cs = player.combatStats;
    const auto& attr = player.sheet.attributes;
    const float minutesScale = float(minutes) / 60.0f;
    const float basePerHour = 10.0f;

    apply_fractional_recovery(basePerHour * (1.0f + float(attr.end) * 0.01f) * minutesScale,
                              accumulator.sp,
                              cs.currentSp,
                              cs.maxSp);
    apply_fractional_recovery(basePerHour * (1.0f + float(attr.vit) * 0.01f) * minutesScale,
                              accumulator.hp,
                              cs.currentHp,
                              cs.maxHp);
    apply_fractional_recovery(basePerHour * (1.0f + float(attr.wil) * 0.01f) * minutesScale,
                              accumulator.mp,
                              cs.currentMp,
                              cs.maxMp);
}

} // namespace sm
