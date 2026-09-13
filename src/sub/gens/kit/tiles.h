// Tile primitives — the smallest shared vocabulary of the open-air generator
// modules: paint a rectangle, ask what stands nearby, wipe the decor layer,
// and reconcile the tile grid with the finished heightmap.
//
// These are the kit's floor. They know nothing about settlements, roads or
// biomes; a module composes them into its own content. The tile CLASS question
// ("has a generator already decided what this ground is?") deliberately does
// NOT live here — it is the Tile vocabulary's own law, `tile_is` /
// `kTileBuilt` in sub/map_data.h, so the base terrain pass and the populator
// can ask it too without depending on a generator.
#pragma once
#include "sub/map_data.h"

namespace sm::sub::kit {

// Drop the scatter's decor tiles back to bare ground. A settlement clears the
// ambient tree litter its own cell inherited before it builds on it.
void clear_decor_tiles(SubworldMapData& out);

// Paint an axis-aligned rectangle of `tile` (clipped to the cell) and set its
// traversability. Unconditional: the caller decides what it is allowed to
// overwrite, because only the caller knows what it is stamping.
void stamp_rect(SubworldMapData& out, int x, int y, int w, int h,
                std::uint8_t tile, std::uint8_t trav);

// Is there a tile of this kind within `radius` (Chebyshev) of (x, y)?
bool has_tile_near(const SubworldMapData& out, int x, int y,
                   int radius, std::uint8_t tile);

// Reconcile tiles with the finished heightmap: ground below the water plane
// becomes water, the damp margin beside it becomes shore, and ground that rose
// back above the plane stops being either. BUILT ground is never touched — a
// road, a wall or a house is where a generator decided it is, and the terrain
// does not get a second vote (that is what `kTileBuilt` is for).
//
// Idempotent, so it may be run mid-generation by a module that needs its water
// classified early (the coastal tree scatter does) and again by the dispatch
// post-pass after road smoothing has moved heights.
void sync_water_tiles_from_heightmap(SubworldMapData& out);

} // namespace sm::sub::kit
