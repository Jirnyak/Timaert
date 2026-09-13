// Plots — the ground a place PARCELS OUT: what is built on it and what is
// sown on it.
//
// Two primitives and their consequences:
//
//   · A BUILDING is an oriented box. `add_house_obb` is the one door for
//     raising one: it stamps the rotated footprint, levels that footprint by
//     cut-and-fill, emits the solid, and hangs the door that names it. The
//     ORDINAL law lives here — a door carries the count of houses this cell
//     had raised before it, and the engine finds the interior behind a door by
//     counting the same way (engine.cpp enter_dungeon_by_door). Two places
//     counting, one rule: `house_count` is that rule, and it is public so
//     nobody re-implements it.
//
//   · A FIELD is a rectangle of ploughed ground, and what grows on it is not
//     the field's business but the CELL's: `scatter_field_crops` sows every
//     TILE_FIELD in the cell, whoever ploughed it, so a village garden and
//     open farmland grow the same wheat through the same door.
//
// What is NOT here: where the parcels go. A radial city, a ribbon village and
// a graveyard of grave plots each decide that themselves; they come back here
// to actually lay one.
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/map_data.h"

#include "core/rng.h"

namespace sm::sub::kit {

// How many houses this cell has raised so far — i.e. the ordinal the next one
// gets. THE counting rule; the engine's door→interior resolver replays it.
std::uint16_t house_count(const SubworldMapData& out);

// Raise one oriented building and hang its door. `requireClear` refuses the
// spot if the footprint (plus a one-tile ring) touches ground some other
// generator step already decided. Returns false if it did not fit.
bool add_house_obb(SubworldMapData& out, float cx, float cy,
                   float hx, float hy, float yaw, float height,
                   bool requireClear);

// The one building a place is BUILT AROUND — a keep, a chapter house, a
// barrack block. Placed regardless of what is under it (it was there first, in
// the fiction) with a modest random lean so it does not read as a diagram.
bool stamp_landmark_house(SubworldMapData& out, Rng& r,
                          float cx, float cy, int w, int h, float height);

// Rejection-sample one house onto ground that FRONTS A LANE, inside `area`
// pulled in by `inset`. Independent continuous width/length and a free yaw, so
// houses range from square to ~1:2 barns and no two sit on the same grid.
bool try_add_roadside_house(SubworldMapData& out, Rng& r,
                            const Outline& area, float inset,
                            int minSize, int maxSize, float height);

// ── FRONTAGE: houses stand ON a street, not near one ──────────────────────
// The rejection scatter below (`try_add_roadside_house`) asks only "is there a
// lane within ten tiles?", so its houses sit BESIDE streets at random angles —
// which from the air reads as buildings dropped on a meadow, the owner's exact
// complaint (2026-09-13). A town is its street frontage: a plot fronts the
// lane, its door opens onto it, and its neighbour is a plot's width away.
//
// Walks each segment and lays plots down both sides. Returns how many stood.
// The door law is unchanged — every house still gets exactly one door carrying
// its ordinal — it is only the ORIENTATION that is now decided rather than
// rolled: the door faces the street it fronts.
struct FrontagePlan {
    float setback;       // gap between the carriageway edge and the wall line
    float depthMin, depthMax;   // how far back a plot runs from the street
    float widthMin, widthMax;   // frontage taken along the street
    float gapMin, gapMax;       // space between neighbours (0 = terraced)
    float heightMin, heightMax;
};

int lay_frontage(SubworldMapData& out, Rng& r,
                 const float* x0, const float* y0,
                 const float* x1, const float* y1,
                 const float* halfWidth, int segCount,
                 const FrontagePlan& plan, int maxHouses);

// Plough a rectangle, if nothing built (and no open water) is under it.
bool add_field_rect(SubworldMapData& out, int cx, int cy, int w, int h);

// Sow every ploughed tile in the cell. Density is the cell's own fertility;
// the harvest scar (ctx.cropHarvested) takes the last stands in lattice order,
// so a reaped field comes back thinner and never resurrects its wheat.
void scatter_field_crops(const CellContext& ctx, SubworldMapData& out);

} // namespace sm::sub::kit
