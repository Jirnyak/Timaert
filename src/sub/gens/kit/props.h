// Props — the small standing things that turn a layout into a place.
//
// Both of these are placed by reading the ground rather than by being told
// where to go: the lamps find the lane and step onto its verge, the well finds
// the paved centre whoever paved it. That is what lets a city and a village —
// and later a camp or a chapter house — share them without either module
// knowing what the other laid down.
//
// What a prop IS (how big, how far it lights, what E does to it) is never
// decided here: it is its row in kStructureKindRows (sub/map_data.h). This
// file only decides WHERE.
#pragma once
#include "sub/map_data.h"

#include <cstdint>

namespace sm::sub::kit {

// Lamp posts along everything paved within `radius` of `center`. Spacing is
// derived from the lantern's own reach, so the pools overlap and a street
// reads as lit rather than as a string of beads.
void scatter_street_lanterns(SubworldMapData& out, int center,
                             int radius, std::uint32_t seed);

// The heart of a settlement: a well a few paces off the exact centre (a square
// with a hole dead in the middle is a diagram, not a place) and a signboard
// facing it across the paving.
void stamp_village_green(SubworldMapData& out, int center, std::uint32_t seed);

} // namespace sm::sub::kit
