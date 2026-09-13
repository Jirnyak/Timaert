// THE CITY WALL — masonry, and only a city raises it.
//
// Until 2026-09-13 this was "the wall", one ring model with a
// `WallStyle{height, towers}` dial serving the city's curtain, the upper
// quarter's enceinte AND the village's wall at once. Owner's ruling that day:
// the city gets its own wall generator, the village its own stockade
// (gens/village_palisade.h), the ruin keeps the one it already had. Two
// modules that look alike in places are not a debt; one module with dials for
// three different things is — and the bill came in visible form, a hamlet of a
// hundred souls standing behind eight metres of ashlar with round towers.
//
// What survives here is the masonry, and the two laws it exists to keep:
//
//   · A GATE IS WHERE THE ROAD IS. It used to be where the compass said: a
//     fixed-width corridor along the pure bearing of a road-bearing neighbour,
//     cut from the cell centre outward. But the road it was cut for does not
//     run down that bearing — it runs to a jittered edge anchor up to 153
//     tiles off-axis — so the ring got an empty arch over grass AND a second
//     opening where the real road crossed, often a few tiles apart. That is
//     the "two gates side by side" the owner saw. The corridor is gone: the
//     caller carves its roads FIRST, and the ring opens exactly where it finds
//     paving under itself.
//
//   · THE RING IS DECIDED BEFORE ANYTHING IS BUILT ON IT. The wall used to be
//     stamped last, after houses and fields, and yielded to both — so a house
//     grown into an inward dip of the ring produced no wall tiles and no wall
//     solids there at all: a hole you could walk through. The outline is now
//     computed first and handed to every other placer (kit/outline.h), so
//     nothing is ever built where the wall is going to stand.
//
// The ring's noise amplitudes are NOT local: they live in sub/city_layout.h,
// because the citizen populator derives the ring's worst INWARD excursion from
// them to know what "inside the walls" means.
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include "core/rng.h"

namespace sm::sub {

struct CurtainStyle {
    float height;    // curtain height, metres — courses of the wall module
    bool  towers;    // round towers where the ring turns
};

// Raise the curtain. Returns how many gates it cut, writing up to `maxGates`
// of them; openings beyond that are still cut, just not reported.
//
// `clip`, when given, is a boundary this ring may not cross: samples outside it
// are not built at all. That is how an inner enclosure SHARES a side with the
// curtain instead of running its own wall alongside it — the upper quarter is
// backed into the city wall, so the arc that would have stood outside the town
// is simply the city wall, and stamping it again gave the doubled wall the
// owner photographed (2026-09-13).
int stamp_city_wall(SubworldMapData& out, const kit::Outline& outline,
                    const CurtainStyle& style, kit::WallGate* gates,
                    int maxGates, const kit::Outline* clip = nullptr);

} // namespace sm::sub
