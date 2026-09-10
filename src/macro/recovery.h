// THE recovery of a body's bars — one law, one implementation, EVERY body
// (CANON S14 «три ресурса, один закон восстановления»; owner 2026-09-09:
// «никакого особенного игрока и ущербных НПЦ»; owner 2026-09-10: «реген
// только один когда стоишь на месте в макромире (типа привал) и это всё»).
//
// This file was `player_recovery.h`, and the player half of the name was the
// defect: it held an App-side fractional accumulator that never reached the
// save, and a per-minute function that read cached hourly rates off the
// player's private CombatStats while npc_ai re-derived the same arithmetic
// inline. Landing 4 killed all of it — the remainder lives in Pools beside
// its bar, the rate is derived on the spot, and both scales call the one
// function below.
#pragma once

namespace sm {
namespace ecs { struct Pools; }

// THE fractional recovery of ONE bar, and the only implementation of it in
// the game. Whole points move into `current`, the sub-point remainder waits
// in `carry`, and a full bar cannot bank rest — that last rule is what stops
// an hour spent at full health from paying out the moment the first step is
// taken. `amount` is per-call, already scaled by the caller's slice of time.
void recover_bar(float amount, float& carry, int& current, int maximum);

// THE rest slice of one BODY — all three bars of its Pools block at the one
// rate (attributes.h kRestRegenPctPerHour), `hours` already gated by the
// caller: a body that is not standing in a macro camp simply does not call
// (the march heals nothing, and underground NOTHING recovers — owner
// 2026-09-10). Marathon multiplies the SP rate ONLY, through the same skill
// law both scales read. SP settles through THE signed carry
// (movement_cost.h settle_sp_carry) — the remainder a march spends out of —
// and a full bar cannot BANK rest on any of the three.
void rest_pools(ecs::Pools& pools, float hours, int marathonRank);

} // namespace sm
