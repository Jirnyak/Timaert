// THE per-minute player recovery — one law for both worlds.
//
// It was macro-only, and that was not a design: underground NOTHING came back,
// not health, not mana, not stamina, because this call simply was not on that
// branch. Owner ruling 2026-08-20: recovery is driven by TIME, so a body
// standing still under a hill mends at the same rate per game hour as one
// standing still on the road — which, since the clock down there crawls at
// kSubworldTickDivisor steps per tick, means kSubworldTickDivisor times slower by the wall
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

// `restRate` gates ALL THREE bars (CANON S14, one recovery law; owner,
// 2026-09-03): 1.0 for a body at rest, macro/movement_cost.h
// kMarchRecoveryPct (zero) while it is on the move. Legs in motion are not
// resting — and while HP/MP recovered anyway, the road healed wounds for
// free; now a wound, an empty well and an empty bar all wait for camp.
void apply_minute_recovery(PlayerState& player,
                                 int minutes,
                                 PlayerRecoveryAccumulator& accumulator,
                                 float& spCarry,
                                 float restRate = 1.0f);

} // namespace sm
