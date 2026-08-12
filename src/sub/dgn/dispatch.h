// Dungeon (interior-scene) generators. Mirrors sub/gens/dispatch: each
// interior kind is a self-contained module TU; this header is the registry.
// A dungeon cell is generated INSTEAD of the open-air pipeline — the module
// writes the whole kCellSize² cell (tiles, trav, heightmap, structures) and
// owes the base terrain generator nothing.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

// THE dungeon seed rule — one identity {door cell, ordinal, level} ⇒ one
// scene, forever. Lives in the dungeon module so the engine (session
// resolver) and the generators (level-independent rolls, e.g. the cellar
// coin, which must read the level-0 stream) share one hash.
std::uint32_t dungeon_scene_seed(std::uint32_t worldSeed, int cx, int cy,
                                 std::uint16_t ordinal, std::int8_t level);

// ── Storeys ─────────────────────────────────────────────────────────────────
// A house is up to three levels: cellar (-1) ↔ ground (0) ↔ upper (+1),
// joined by two stair SHAFTS at fixed corners of the room — NW climbs
// (0↔+1), NE descends (0↔-1). Fixed corners make the shafts a shared rule
// (generator stamps the pads, engine reads E on them, tests assert both)
// instead of plumbed data.
bool dungeon_has_upper(const DungeonRef& ref);
bool dungeon_has_cellar(const DungeonRef& ref, std::uint32_t worldSeed,
                        int cx, int cy);
// Pad centre (cell-local tiles) of the climbing (NW) / descending (NE)
// shaft for this footprint's room. Positions are level-independent — a
// shaft is one vertical line through the house.
void dungeon_stair_point(const DungeonRef& ref, bool up, float& x, float& y);

// The tile an interior's WALKABLE ground is paved with. Placement code (the
// resident and vermin spawners) reads the composite's tiles, so the module
// that carved the place is the one that must say which of them is floor —
// a house is flagged and paved, a cave is bare scree.
Tile dungeon_floor_tile(const DungeonRef& ref);

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

// Cave interior (sub/dgn/cave.cpp — self-contained module). Its "room" is
// the MOUTH chamber: the part the shared rules address, the rest is walked.
void gen_dungeon_cave(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_cave_room(const DungeonRef& ref);

} // namespace sm::sub
