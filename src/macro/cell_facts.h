// THE macro answer to "what is this cell" (CANON S6, 2026-08-24) — one
// assembler over the layer envelope (macro/macro_world.h).
//
// Every mechanic that wants a cell's facts asks HERE and gets the contribution
// of EVERY system that has one — biome (the one cascade), feature, the
// landmark standing on it with its live fields, trees, fertility, elevation,
// danger zone, land owner, the season's temperature shift. A system that does
// not apply contributes its zero through a null envelope layer — data, not a
// second code path. Before this door the subworld's resolve_context, the
// fauna capacity, the travel pricer and the spawners each assembled their own
// partial copy of this struct, and two of the copies had already drifted.
//
// PERFORMANCE CONTRACT (the door's half of CANON S26): cell_facts is NEVER
// called from a hot loop. A* and the greedy squad step read the baked
// PathCostData; the subworld caches the facts of its nine window cells and
// invalidates on crossing. This function is for bake time, cell entry and
// events — the places that can afford to ask everything at once.
#pragma once
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/landmark_grid.h"
#include "macro/macro_world.h"

#include <cstdint>

namespace sm {

// The landmark standing on the cell, with its LIVE fields resolved from
// GameState at the moment of asking (population and ownership drift daily;
// the grid only answers WHO — macro/landmark_grid.h).
struct LandmarkFacts {
    LandmarkType type = LandmarkType::None;
    int  id = -1;          // id within its kind's register; -1 = none
    int  size = 0;         // population (settlement) / spell tier (spire)
    int  kingdomIdx = -1;  // owning kingdom; -1 = none
    bool depleted = false; // a spire whose orb is gone
};

struct CellFacts {
    int x = 0;             // wrapped world-cell coordinates
    int y = 0;
    Biome        biome = Biome::Water;   // the one cascade (biome_at_cell)
    FeatureType  feature = FT_None;      // road / dirt road / field
    LandmarkFacts landmark{};
    int   treeCount = 0;      // macro tree count; -1 = layer not wired
    float height01 = 0.0f;    // normalized elevation (terrain R)
    float fertility01 = 0.0f; // moisture (terrain G) — the wheat driver
    float temperature01 = 0.0f;   // RAW climate (terrain B): classification
                                  //   never shifts with the season
    float seasonTempOffset = 0.0f; // the season's shift, ITS OWN column —
                                   //   it used to ride smuggled inside a
                                   //   shifted temperature (foliage wants it,
                                   //   classification must not see it)
    std::uint8_t zone = 0;    // danger 0..9 (macro/zones.h); 0 if not wired
    std::int8_t  ownerKingdom = -1; // land owner (politik cellOwner); -1 none
    int  cropHarvested = 0;   // the wheat scar: what the sickle already took
    bool water = false;       // biome == Water, pre-answered for one-fact
                              //   consumers
};

// Assemble the facts of one cell. Torus-wrapped; every missing envelope layer
// reads as its zero contribution, and a missing terrain answers open water.
CellFacts cell_facts(const MacroWorld& w, int x, int y);

} // namespace sm
