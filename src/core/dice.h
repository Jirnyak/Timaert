// THE dice door (CANON S13): every die the game rolls goes through
// roll_dice() — damage, loot, any future table that says "NdM". There is no
// second randomness law in combat: no to-hit, no reroll knob. Fixed damage is
// the degenerate die Nd1 and consumes ZERO rng draws, so a fixed-damage
// source can never perturb a seeded stream.
//
// LCK's ONE reader also lives here (CANON S14: "LCK — дверь кубов"): the
// crit. Owner verdict 2026-09-05 — LCK does not reroll dice; each point of
// LCK is a 0.5% chance the hit IGNORES ARMOUR completely. The damage door
// asks crit_procs() before mitigating; nothing else reads LCK.
//
// Integer arithmetic throughout — combat laws are integer by law (CANON S13:
// "кубы целочисленны по природе"); this door is where combat floats die.
#pragma once
#include "rng.h"

#include <cstdint>

namespace sm {

// NdM as table data: n dice of m faces. m == 1 is the fixed-damage case.
// uint8 by ЗАКОН ТИПА: authored rows live around 1d2..20d12 — the numeric
// ceiling (255d255) is an order of magnitude past any row a table will ever
// carry, so the wider type would buy nothing.
struct Dice {
    std::uint8_t n = 0;  // number of dice; 0 = "no roll", total 0
    std::uint8_t m = 1;  // faces per die; 1 = fixed value n
};

// One die, uniform in [1, m]. Integer path — no floats. Modulo bias over
// xorshift32's 2^32 codes is < m/2^32 (~one part in 16 million at d255):
// unobservable next to the dice themselves.
inline int roll_die(Rng& rng, int m) {
    return 1 + int(rng.next_u32() % std::uint32_t(m));
}

// THE roll: sum of n uniform dice, exactly n rng draws (zero when m == 1).
inline int roll_dice(Rng& rng, Dice d) {
    if (d.m <= 1) return int(d.n);
    int total = 0;
    for (int i = 0; i < int(d.n); ++i) total += roll_die(rng, d.m);
    return total;
}

// The crit law: chance = luck·5 per mille — "1 LCK = 0.5% crit" verbatim
// (owner, 2026-09-05), so the endgame anchor LCK 100 crits every other hit
// and LCK 200+ always does. The fiction (owner): a crit is the blade finding
// the ARMOUR GAP — joint, visor slit — which is why it ignores armour and
// why it multiplies nothing: the wound is the weapon's honest dice. luck <= 0
// draws nothing — a luckless attacker cannot shift a seeded stream.
inline bool crit_procs(Rng& rng, int luck) {
    if (luck <= 0) return false;
    return int(rng.next_u32() % 1000u) < luck * 5;
}

// Auto-resolve reads the die's EXPECTATION, not a roll — the macro fight has
// no per-swing rng (combat.md: fighter_power is the algebraic inverse of the
// fought path). Doubled so it stays exact in ints: E[NdM]·2 = n·(m+1).
inline int dice_mean_x2(Dice d) { return int(d.n) * (int(d.m) + 1); }

} // namespace sm
