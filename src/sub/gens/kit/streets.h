// Roads — how a cell is CROSSED and how a place is reached.
//
// Two laws live here, and both are shared by every module that lays a lane:
//
//   · THE SEAM CONTRACT. A road only means something if it continues into the
//     next cell, and a cell is generated alone, knowing nothing of what its
//     neighbour decided. So the meeting point is derived, not negotiated:
//     `symmetric_edge_seed` hashes the two cell keys in sorted order, so both
//     sides of a seam compute the SAME anchor tile, and `carve_road_leg`'s
//     endpoint damping is exactly zero at both ends, so the lane actually
//     arrives on it. Perturb either and every road in the world tears at every
//     seam — this is the most fragile invariant in the generators.
//
//   · THE GRADE LAW. A road that charges a slope is a road nobody would have
//     built. `road_centreline` runs a coarse A* whose step cost punishes grade
//     quadratically, so switchbacks emerge rather than being authored.
//
// What is NOT here: any particular town's street PLAN. A radial-concentric
// city, a spoked village and a grid camp are each their own module's content;
// they all come back to `carve_organic_road` to actually cut the ground.
#pragma once
#include "sub/map_data.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sm::sub::kit {

// THE grade penalty — how much dearer a slope is than flat ground, per unit of
// grade squared. Quadratic, so a 26° direct climb costs ~5× a flat detour and
// a 45° one ~19×: switchbacks win emergently rather than being authored.
//
// Public because it is not the road's private opinion, it is this world's
// price of a slope, and a settlement's growth pays exactly the same price when
// it decides which way to spread (sub/gens/kit/growth.h). Two numbers for one
// quantity would let a town sprawl up a hillside its own roads refuse to climb.
inline constexpr float kGradePenalty = 18.0f;

// Is this macro feature byte a road for CONNECTIVITY purposes? A bridge is —
// the banks' roads aim their edge anchors at it and its own carve line runs
// bank to bank. Wooden or stone, the crossing connects either way (v72).
bool is_road_feature(std::uint8_t f);

// The neighbour directions (of the 8) that carry a road. Order is the
// kDirOffsets order, so it is stable across runs and across cells.
struct RoadDirSet {
    std::array<int, 8> dx{};
    std::array<int, 8> dy{};
    std::array<float, 8> angle{};
    int count = 0;
};

// The same set, as a SETTLEMENT reads it: which way its main roads leave, and
// whether it is connected to the road network at all.
struct RoadAxisSet {
    std::array<int, 8> dx{};
    std::array<int, 8> dy{};
    std::array<float, 8> angle{};
    int count = 0;
    bool anchored = false;   // false ⇒ an unconnected place: internal lanes only
};

RoadDirSet  connected_road_dirs(const std::uint8_t nbFeature[9]);
RoadAxisSet settlement_road_axes(const std::uint8_t nbFeature[9]);

// THE seam rendezvous: the edge tile a lane toward neighbour (dx, dy) must
// hit. Symmetric in the two cells, so both sides derive the same point without
// ever seeing each other's map.
void edge_anchor_target(const CellContext& ctx, int dx, int dy, int& ox, int& oy);

// Cut a lane from (x1, y1) to (x2, y2): grade-aware centreline, then the
// organic wiggle per leg. Overwrites everything but house footprint.
void carve_organic_road(SubworldMapData& out, int x1, int y1, int x2, int y2,
                        std::uint32_t worldSeed);

// A place's main roads: one lane from its heart to each road-bearing
// neighbour's edge anchor. The two spellings differ only in their seed salt —
// `landmark_anchor` is what a ruin's remembered road uses, `settlement_main`
// what a living town's does — so a ruin and a town on the same cell seed do
// not lay the same tracks.
void carve_landmark_anchor_roads(SubworldMapData& out, const CellContext& ctx,
                                 const RoadDirSet& dirs, int center,
                                 std::uint32_t seed);
void carve_settlement_main_roads(SubworldMapData& out, const CellContext& ctx,
                                 const RoadAxisSet& axes, int center,
                                 std::uint32_t seed);

// A LANE of a given half-width, laid straight from a to b. No pathfinding and
// no wiggle: a town's ground is deliberately levelled (base_generator.h
// TerrainMod plateau), so the grade-aware detour that a cross-country road
// needs has nothing to bite on inside a town and only made the streets wander
// for no reason the player could read. The shape of a street comes from how it
// GREW (sub/gens/kit/lanes.h), not from the rasteriser.
void carve_lane(SubworldMapData& out, float x0, float y0, float x1, float y1,
                float halfWidth);

// One leg of a lane, raw. Modules that lay their own polyline (a bridge
// approach, a field track) use this directly; everything else goes through
// carve_organic_road.
void carve_road_leg(SubworldMapData& out, int x1, int y1, int x2, int y2,
                    std::uint32_t worldSeed, float ampTiles);

// The grade-aware centreline alone, without cutting anything — for a module
// that needs to KNOW where a lane would run before it commits.
void road_centreline(const SubworldMapData& out, int x1, int y1, int x2, int y2,
                     std::vector<std::array<int, 2>>& pts);

} // namespace sm::sub::kit
