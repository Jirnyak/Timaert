// THE carry-overload surcharge — what a back carries over its capacity, and
// the integer SP that costs per hour. One law for any back on the map.
//
// It is all that is left of this file: until 2026-10-06 it also owned the
// per-cell travel price, and that whole question died with the hour quantum
// (macro/movement_cost.h — the burn and the two processes it belongs to).
#pragma once

#include "macro/movement_cost.h"
#include "macro/anketa.h"
#include <cmath>

namespace sm {

struct CharacterSheet;
struct Inventory;

// The ONE spelling of the carry-overload surcharge: capacity from the sheet,
// carried weight from the bag, and the "any overload hurts" ceil (sub-1kg
// overload still costs 1 SP). Was copy-pasted in travel.cpp and app/main.cpp.
struct OverloadCharge {
    float overload = 0.0f;  // kg carried over capacity
    int   cost = 0;         // its integer SP surcharge
};
// THE overload law, for any back on the map. It was called
// `player_overload_charge` and only the player was ever charged by it, so a
// caravan hauling a ton of iron marched as briskly as an empty scout — the
// weight was a number in a panel, not a cost. Owner's ruling, 2026-08-27:
// «да, перегруз универсальный всем».
OverloadCharge overload_charge(const CharacterSheet& sheet,
                               const Inventory& inventory);

// The same law from the CACHED capacity a macro leader carries on his runtime
// (ecs::MacroNpcRuntime::carryCap), so a think prices its load without
// rebuilding a derived sheet it does not store. Inline: the macro AI is the
// caller, and it must not have to link the travel translation unit (and its
// terrain/feature world) to ask what a pack weighs.
inline OverloadCharge overload_charge_from_capacity(float capacityKg,
                                                    const Inventory& inventory) {
    const float carried = inventory_weight(inventory);
    const float overload = get_overload_penalty(carried, capacityKg);
    // Bars are integer POD (Pools). Preserve the "any overload hurts"
    // behaviour instead of silently truncating sub-1kg overload to zero.
    return {overload, overload > 0.0f ? int(std::ceil(overload)) : 0};
}

// (No MacroTravelCost, no macro_travel_cost_for_cell, no
// drain_player_sp_for_macro_cell. All three answered «what does CROSSING THIS
// CELL cost», and on 2026-10-06 that question stopped existing: a body pays
// for HOURS, not for cells (movement_cost.h burn_stamina_per_hour). Their one
// production caller — the player's per-cell charge in app/main.cpp — went with
// them, and the ground under his feet is now read where every squad reads it:
// one weight from the one baked cost grid, no second resolve out of terrain +
// features + trees that could disagree with it at the coast.
//
// What survived is the OVERLOAD law above, because it priced the body's LOAD
// rather than its step, and the hourly burn carries it as its own term.)

} // namespace sm
