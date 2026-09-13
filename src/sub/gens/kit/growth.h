// HOW A PLACE TAKES ITS GROUND — the shape of a town, grown rather than drawn.
//
// A settlement used to be a disk: one radius from population, perturbed by two
// harmonics so the wall wandered by ±8 %. From inside, that reads as exactly
// what it is — a circle. Real towns are not circles because the ground is not
// neutral: they run long down a tract and stop dead at a bluff, they crowd the
// dry shoulder of a valley and refuse the marsh, they fill the flat and leave
// the hillside to goats.
//
// So the outline is GROWN. Starting from a core the town is guaranteed to
// hold, the cheapest neighbouring ground is taken again and again until the
// place has the area a population of its size needs. What "cheapest" means is
// the whole design, and it is three things, all of which the world already
// knows about itself:
//
//   · SLOPE, priced by the one law the roads use (kGradePenalty). A town may
//     not sprawl up a hillside its own streets refuse to climb.
//   · WET GROUND is not taken at all. The plough refuses ground below the wet
//     edge (gen_field) and so does the mason; this is what puts a town ON the
//     river rather than IN it.
//   · THE TRACT. Ground within a block of a road is worth twice ordinary
//     ground — which is the whole reason medieval towns are long rather than
//     round, and why they point at the places they trade with.
//
// The result is handed back as an Outline, so every placer downstream — wall,
// streets, plots, fields, the tree line — is already reading it and needed no
// change at all when the shape stopped being a circle.
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/map_data.h"

namespace sm::sub::kit {

// Grow a place's outline on the ground `out` already carries — its finished
// heightmap and whatever roads have been carved so far. (Carve the tract
// FIRST: a town that grows before its road exists cannot lean on it.)
//
//  · `coreRadius` is the disk the place holds no matter what the ground says.
//    It is a GUARANTEE, relied on by everyone who needs to know where the
//    town's people can be (sub/city_layout.h settlement_population_radius), so
//    the grown outline never dips below it.
//  · `targetArea` is how much ground the place occupies in total, in tiles².
//    Growth stops when it has taken that much — so a town that spreads far
//    along a road is correspondingly narrow across it, and a population always
//    gets the room it needs and no more.
//  · `maxRadius` caps the reach so a place cannot outgrow its own cell.
Outline grow_outline(const SubworldMapData& out, float cx, float cy,
                     float coreRadius, float targetArea, float maxRadius);

} // namespace sm::sub::kit
