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
namespace ecs { struct Pools; }

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

// THE fractional recovery of ONE bar, and the only implementation of it in
// the game (CANON S14 «три ресурса, один закон восстановления»; owner,
// 2026-09-09: «никакого особенного игрока и ущербных НПЦ»). Whole points move
// into `current`, the sub-point remainder waits in `carry`, and a full bar
// cannot bank rest — that last rule is what stops an hour spent at full health
// from paying out the moment the first step is taken.
//
// It lives in the header because the macro AI's camp (npc_ai.cpp
// settle_march_rhythm) mends a lord's wound through THIS function, not through
// a second copy of the same arithmetic: an NPC that heals by its own rules is
// how a wound became permanent for everyone but the player and stayed that way
// until the post-demo audit. `amount` is per-call, already scaled by the
// caller's slice of time and by its rest gate.
void recover_bar(float amount, float& carry, int& current, int maximum);

// THE rest slice of one BODY — all three bars of its Pools block at the one
// rate (attributes.h kRestRegenPctPerHour), `hours` already gated by the
// caller: a body that is not at rest simply does not call (CANON S14 — the
// march heals nothing; owner 2026-09-10: «реген только один когда стоишь на
// месте в макромире (типа привал) и это всё»). Marathon multiplies the SP
// rate ONLY, through the same skill law both scales read. SP settles through
// THE signed carry (movement_cost.h settle_sp_carry) — the remainder a march
// spends out of — and a full bar cannot BANK rest on any of the three: the
// positive remainder dies with the fill, or an hour idled at full pays out
// the moment the first step is taken.
//
// This function is the reason the formula exists ONCE: before it, the player
// read a cached hourly rate off CombatStats while npc_ai re-derived the same
// arithmetic inline — a drifted copy held together by a parity test.
void rest_pools(ecs::Pools& pools, float hours, int marathonRank);

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
