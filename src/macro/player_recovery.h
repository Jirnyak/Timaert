// THE per-minute player recovery — one law for both worlds.
//
// It was macro-only, and that was not a design: underground NOTHING came back,
// not health, not mana, not stamina, because this call simply was not on that
// branch. Owner ruling 2026-08-20: recovery is driven by TIME, so a body
// standing still under a hill mends at the same rate per game hour as one
// standing still on the road — which, since the clock down there crawls at
// kSubworldTickDivisor steps per tick, means sixteen times slower by the wall
// clock. Waiting out a wound underground is meant to cost a real wait.
#pragma once

#include "macro/state.h"

namespace sm {

struct PlayerRecoveryAccumulator {
    float hp = 0.0f;
    float mp = 0.0f;
    // (No `sp`. Stamina has ONE carry and it is signed, because a march spends
    // through the same remainder a rest fills — movement_cost.h
    // settle_sp_carry, kept on the player's squad entity like every lord's.
    // A separate regen-only slot here made a third implementation of one idea
    // and quietly zeroed itself at a full bar.)
};

void reset_player_recovery(PlayerRecoveryAccumulator& accumulator);

// `staminaRate` scales the SP half only: 1.0 for a body at rest, and
// macro/movement_cost.h kMarchRecoveryPct while it is on the move. Legs in
// motion are not resting — and while they recovered anyway, travel was paid for
// by a standing income rather than by the traveller. Health and mana are
// unaffected: this is a statement about stamina, not about healing.
void apply_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator,
                                 float& spCarry,
                                 float staminaRate = 1.0f);

} // namespace sm
