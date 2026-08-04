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

// `staminaRate` scales the SP half only: 1.0 for a body at rest, and
// macro/movement_cost.h kMarchRecoveryPct while it is on the move. Legs in
// motion are not resting — and while they recovered anyway, travel was paid for
// by a standing income rather than by the traveller. Health and mana are
// unaffected: this is a statement about stamina, not about healing.
void apply_macro_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator,
                                 float staminaRate = 1.0f);

} // namespace sm
