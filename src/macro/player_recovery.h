// Macro-view per-minute player recovery. Mirrors GameScreen.svelte wrapper.
#pragma once

#include "macro/state.h"

namespace sm {

struct PlayerRecoveryAccumulator {
    float hp = 0.0f;
    float mp = 0.0f;
    float sp = 0.0f;
};

void reset_player_recovery(PlayerRecoveryAccumulator& accumulator);
void apply_macro_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator);

} // namespace sm
