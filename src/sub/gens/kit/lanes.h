// STREETS AS HYPHAE — a network that grows toward what it is for.
//
// The radial-concentric plan this replaces drew geometry and hoped it would
// read as a town. It did not, and the owner named every reason from a single
// aerial frame (2026-09-13): avenues evenly spaced from a random rotation, so
// they lined up with nothing — least of all the gates the traffic actually
// comes through; a ring road at 0.92 of the usable radius, which is to say
// scraping the curtain; frontage stubs fired off at random angles to die in
// the grass; and every one of them the same width, so a market street and a
// back alley were indistinguishable.
//
// A hypha grows toward food. A street's food is somewhere to go, so:
//
//   · THE TRUNKS are the roads that must exist: gate → heart. They are laid
//     first and widest, because the traffic that made them is the town's own
//     reason for having a gate.
//   · A TIP BRANCHES at intervals, and each generation is NARROWER than its
//     parent — a trunk throws streets, a street throws alleys. That is the
//     hierarchy, and it comes free with the growth rather than being assigned.
//   · A TIP DIES where it is not needed: on ground already SERVED by another
//     lane, at the wall, in the water. This is what makes the coverage even
//     without any global plan — a branch into a dense quarter dies at once, a
//     branch into empty ground runs.
//   · A TIP THAT MEETS A LANE JOINS IT (anastomosis) instead of running
//     alongside. That is where the loops and the blocks come from, and it is
//     what kills the parallel duplicates the old plan was full of.
//
// The network is handed back as its segments, because the houses are laid
// ALONG it — a town is its street frontage (sub/gens/kit/plots.h).
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/map_data.h"

#include "core/rng.h"

#include <vector>

namespace sm::sub::kit {

// A lane's rank IS its generation, and its width follows from that.
enum class LaneRank : std::uint8_t { High = 0, Street, Alley, Count };

// Half-width in tiles, derived from the BODY this world is built around
// (macro/npc.h kNpcBodyRadiusDefault = 0.55 ⇒ a man is 1.1 tiles wide):
//
//   alley  — two men pass shoulder to shoulder          2 body-widths
//   street — a cart passes a man                        3
//   high   — two carts pass                             4
//
// Nothing here is a look: a street is as wide as what has to get down it.
float lane_half_width(LaneRank rank);

struct LaneSeg {
    float x0, y0, x1, y1;
    LaneRank rank;
};

struct LaneNet {
    std::vector<LaneSeg> segs;
};

struct LanePlan {
    float stepTiles;      // how far a tip advances between decisions
    float branchEvery;    // tiles of growth between branches off one tip
    float wanderRad;      // how much a tip may drift per step
    float mergeRadius;    // a tip this close to existing paving joins it
    float serveRadius;    // how far either side of a lane counts as served
    int   heartSpokes;    // streets leaving the market square itself
    float heartRadius;    // …starting at its RIM, not its middle: a street
                          // begun at the centre has its first tiles paved over
                          // by the square, so the network ends up joined only
                          // THROUGH the square — and a well standing on the one
                          // remaining tile severs it (caught by
                          // structure_collide_test's plaza-to-gate walk).
    // WHY THE GROWTH STOPS. A lane exists so that houses can front it, so the
    // network is finished when it offers as much frontage as the town has
    // buildings to put on it — length × two sides ÷ a plot's street face.
    // Growing to an arbitrary cap instead gave a 380-house town twenty times
    // the street it could ever build on: mile after mile of empty lane, which
    // is precisely the "roads that don't mean anything" the owner saw.
    float frontageTiles;  // total lane length wanted, tiles
    int   maxSegs;        // hard backstop only; the frontage target is the law
};

// Grow the network inside `area` pulled in by `wallInset`, from `sources`
// (the gates) toward the heart. `r` drives the wander and the branch sides.
LaneNet grow_lanes(SubworldMapData& out, const Outline& area, float wallInset,
                   const float* sourceX, const float* sourceY, int sourceCount,
                   float heartX, float heartY,
                   const LanePlan& plan, Rng& r);

// The POMERIUM: the lane that runs the whole way round just inside the wall.
// Not scenery — it is how a garrison reaches any stretch of its own curtain
// without threading someone's yard, and it is why the ring road that used to
// graze the masonry was the right idea in the wrong place. An alley: nothing
// bigger needs to get round the back.
void carve_pomerium(SubworldMapData& out, const Outline& area, float inset,
                    LaneNet& net);

} // namespace sm::sub::kit
