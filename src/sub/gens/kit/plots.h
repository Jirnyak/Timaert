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

// GROUND THE PLOTS MAY NOT TAKE. Plain discs, handed in by whoever knows what
// they are — this placer never learns the word "gate", and nothing has to be
// encoded into the tile byte to reach it.
//
// Why it exists: a town's frontage lines EVERY lane there is, so a plot lands
// wherever a lane passes — including across the mouth of a gateway, which the
// owner photographed at two gates of the upper quarter (2026-09-13). The road
// itself is paved and refused, but the road wanders, and the strip beside it
// inside the opening is bare ground the placer was entitled to. Teaching the
// placer about gates would be a module reaching into a neighbour's business;
// paving a fake apron to trip its own refusal rule would be a patch. A list of
// forbidden discs is neither: it is the caller stating a fact about ITS ground.
struct KeepOut {
    float x, y;
    float r;
};

// Rejection-sample one house onto ground that FRONTS A LANE, inside `area`
// pulled in by `inset`. Independent continuous width/length and a free yaw, so
// houses range from square to ~1:2 barns and no two sit on the same grid.
bool try_add_roadside_house(SubworldMapData& out, Rng& r,
                            const Outline& area, float inset,
                            int minSize, int maxSize, float height,
                            const KeepOut* keepOut = nullptr,
                            int keepOutCount = 0);

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
                 const FrontagePlan& plan, int maxHouses,
                 const KeepOut* keepOut = nullptr, int keepOutCount = 0);

// ── THE BACKLANDS: houses on ground no lane ever reached ──────────────────
//
// A town asks for one hearth per household and its placers seat about three
// quarters of them: the frontage runs out of lane, and the road-gated scatter
// refuses every spot with no paving within ten tiles. The remaining quarter of
// the population had no door at all — which stopped being invisible the day
// the sun began sending people home, and reads from the street as bald patches
// inside the walls.
//
// So this lays the remainder where the lanes never went, and it does not ask
// for a road, because that requirement is exactly what was refusing them
// (owner, 2026-09-13: «пустыри всё равно без улиц есть, пусть там будут
// дома»). It is its own function rather than a flag on the road-gated scatter
// because it answers a DIFFERENT question — that one asks "is there frontage
// here", this one asks "is there room here" — and a primitive that answers two
// questions by a boolean is the dial this module has been getting rid of.
//
// It fills the EMPTIEST ground first: a candidate must sit in a clear
// neighbourhood, and the search runs that neighbourhood down from a burgage
// plot's yard depth (the room a house is entitled to) to a single tile, so the
// biggest bald patches take houses before the gaps between existing plots do.
// Returns how many stood.
int lay_backland_houses(SubworldMapData& out, Rng& r,
                        const Outline& area, float inset,
                        const FrontagePlan& plan, int maxHouses,
                        const KeepOut* keepOut = nullptr, int keepOutCount = 0);

// Plough a rectangle, if nothing built (and no open water) is under it.
bool add_field_rect(SubworldMapData& out, int cx, int cy, int w, int h);

// Sow every ploughed tile in the cell. Density is the cell's own fertility;
// the harvest scar (ctx.cropHarvested) takes the last stands in lattice order,
// so a reaped field comes back thinner and never resurrects its wheat.
void scatter_field_crops(const CellContext& ctx, SubworldMapData& out);

} // namespace sm::sub::kit
