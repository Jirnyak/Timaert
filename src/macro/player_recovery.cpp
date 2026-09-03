#include "macro/player_recovery.h"
#include "macro/movement_cost.h"

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
                                 float& spCarry,
                                 float restRate) {
    if (minutes <= 0) {
        return;
    }
    if (restRate < 0.0f) restRate = 0.0f;

    auto& cs = player.combatStats;
    const float minutesScale = float(minutes) / 60.0f;

    // The hourly rates are the sheet's own derived stats (attributes.h
    // calculate_combat_stats) — this file used to restate the HP/MP formulas
    // and a THIRD one for SP; now it only converts per-hour to per-minute.
    // ONE recovery law, ONE gate (CANON S14; owner, 2026-09-03): every bar
    // refills as a percent of itself per hour OF REST, and `restRate` gates
    // all three — a marching body recovers nothing, health and mana included
    // (the wounded traveller stays wounded until he makes camp).
    // Stamina goes through THE signed carry (movement_cost.h) — the same
    // remainder the march spends out of, so a rest that pays off half a point
    // of an exhaustion debt is expressible instead of being rounded away by an
    // accumulator that only knew how to count upward.
    const float regen = cs.spRegen * minutesScale * restRate;
    if (regen > 0.0f && cs.currentSp < cs.maxSp) {
        spCarry += regen;
        settle_sp_carry(cs.currentSp, cs.maxSp, spCarry);
    }
    // A full bar cannot BANK rest, so a positive remainder on it is nothing —
    // and dropping it is what stops an hour spent at full health from paying
    // out the moment the first step is taken. A NEGATIVE remainder is the
    // opposite thing entirely: a fraction of a march already owed, and
    // forgiving it here would leak cost every time a journey began from full.
    // The old unsigned accumulator could not tell the two apart, because it
    // could not represent the second one at all.
    if (cs.currentSp >= cs.maxSp) {
        cs.currentSp = cs.maxSp;
        if (spCarry > 0.0f) spCarry = 0.0f;
    }
    apply_fractional_recovery(cs.hpRegen * minutesScale * restRate,
                              accumulator.hp,
                              cs.currentHp,
                              cs.maxHp);
    apply_fractional_recovery(cs.mpRegen * minutesScale * restRate,
                              accumulator.mp,
                              cs.currentMp,
                              cs.maxMp);
}

} // namespace sm
