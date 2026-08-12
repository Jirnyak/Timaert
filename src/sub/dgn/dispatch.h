// Dungeon (interior-scene) generators. Mirrors sub/gens/dispatch: each
// interior kind is a self-contained module TU; this header is the registry.
// A dungeon cell is generated INSTEAD of the open-air pipeline — the module
// writes the whole kCellSize² cell (tiles, trav, heightmap, structures) and
// owes the base terrain generator nothing.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

// Interior room rectangle (cell-local tile coords) derived from the door's
// exterior footprint — the ONE geometry rule shared by the generator, the
// engine's entry placement, and the tests. Dispatches on ref.kind.
struct DungeonRoom {
    float cx = 0.0f, cy = 0.0f; // room centre, tiles within the cell
    float hx = 0.0f, hy = 0.0f; // interior half-extents, tiles
};
DungeonRoom dungeon_room(const DungeonRef& ref);

// Where a body entering this interior stands, and where E walks back out —
// the exit pad (cell-local tiles). One point on both sides of the door, so
// the engine and the generator can never disagree about the threshold.
void dungeon_entry_point(const DungeonRef& ref, float& x, float& y);

// Single generation entry point — dispatches on ctx.dungeon.kind. Void fills
// a sealed filler ring cell; every real kind routes to its module.
void dispatch_generate_dungeon(const CellContext& ctx, SubworldMapData& out);

// House interior (sub/dgn/house.cpp — self-contained module).
void gen_dungeon_house(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_house_room(const DungeonRef& ref);

} // namespace sm::sub
