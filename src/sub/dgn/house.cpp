// House interior — the first dungeon module. Self-contained: all
// house-interior geometry lives here; dispatch.cpp only routes. The interior
// is a projection of the door's Structure record — same seed, same footprint
// ⇒ byte-identical rooms and furniture, nothing persisted.
#include "sub/dgn/dispatch.h"

#include "core/rng.h"

#include <algorithm>
#include <cstdint>

namespace sm::sub {

// ── Interior geometry constants ─────────────────────────────────────────────
// M&M scale (owner ruling 2026-08-12): the interior keeps the facade's
// proportions and multiplies them. ×4 is derived from the combat invariant:
// the smallest roadside house is ~4×4 facade tiles (gens/dispatch city band),
// so its hall becomes 16×16 — a half-width of 8 tiles, strictly wider than
// kPlayerMeleeRange (5), so even the tightest interior fight has room to
// manoeuvre instead of collapsing into a doorway clinch.
constexpr float kInteriorScale = 4.0f;
// The room may never touch its cell's edge: the dungeon window's ring cells
// are sealed Void filler that must stay OUTSIDE the playable geometry. One
// wall thickness + one body diameter of apron, rounded to a power of two.
constexpr float kCellApronTiles = 32.0f;
// Storey step: wall height comes from the one structure table
// (structure_min_height(Wall) = 4 m — the same solid every rampart uses);
// the ceiling slab above it is 1 m thick — non-zero so it is an honest solid
// for projectiles from below and a floor for the storey above (Inc 3).
constexpr float kCeilingSlabM = 1.0f;
// A doorway in an internal partition: the body diameter (2 × 1.5, sub/body.h)
// plus one tile of clearance — two fighters cannot pass abreast, one walks
// through without wall-scraping.
constexpr float kDoorGapTiles = 4.0f;
// A hall is split into rooms only while each half keeps at least this span:
// wider than kPlayerMeleeRange on both sides of a doorway, with furniture
// clearance to spare — a room you can fight in, not a corridor you clip
// through.
constexpr float kMinRoomSpanTiles = 12.0f;

DungeonRoom dungeon_house_room(const DungeonRef& ref) {
    DungeonRoom room;
    // Centre of the cell — the window is centred on the door's macro cell,
    // so the room sits where the player-centred camera expects the world.
    room.cx = float(kCellSize) / 2.0f;
    room.cy = float(kCellSize) / 2.0f;
    // Interior = facade proportions × M&M scale. The floor of the clamp is
    // the House record's own degenerate floor (structure_min_half_xy) scaled
    // like any real facade; the ceiling keeps the room inside its cell.
    const float minHalf = structure_min_half_xy(Structure::House) * kInteriorScale;
    const float maxHalf = float(kCellSize) / 2.0f - kCellApronTiles;
    room.hx = std::clamp(ref.footHx * kInteriorScale, minHalf, maxHalf);
    room.hy = std::clamp(ref.footHy * kInteriorScale, minHalf, maxHalf);
    return room;
}

namespace {

// Per-cell tile write (kCellSize stride — NEVER tile_index(), whose stride is
// the 3×3 composite; the unit test bought this comment with a heap overflow).
inline std::size_t cell_at(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

// One solid interior wall segment + its painted masonry band.
void stamp_wall(SubworldMapData& out, float cx, float cy, float hx, float hy,
                float wallH) {
    Structure s{};
    s.kind = Structure::Wall;
    s.x = cx;
    s.y = cy;
    s.radius = std::max(hx, hy);
    s.height = wallH;
    s.hx = hx;
    s.hy = hy;
    out.structures.push_back(s);
    const int bx0 = int(cx - hx), bx1 = int(cx + hx);
    const int by0 = int(cy - hy), by1 = int(cy + hy);
    for (int y = by0; y <= by1; ++y) {
        for (int x = bx0; x <= bx1; ++x) {
            const std::size_t i = cell_at(x, y);
            out.tiles[i] = TILE_WALL;
            out.trav[i] = 0;
        }
    }
}

// An axis-aligned room rectangle (interior tile bounds, inclusive-ish floats).
struct RoomRect {
    float x0, y0, x1, y1;
    float spanX() const { return x1 - x0; }
    float spanY() const { return y1 - y0; }
};

// One piece of furniture: a solid Furnish box (wood flavour in the box pass)
// with a painted TILE_HOUSE footprint. Placement requires the box PLUS a
// 2-tile clearance ring to be plain hall floor — the ring is what keeps a bed
// out of a doorway (gap tiles are flanked by TILE_WALL inside 2 tiles) and
// leaves a body-wide corridor around every piece.
bool try_place_furnish(SubworldMapData& out, Rng& rng, const RoomRect& r,
                       float hx, float hy, float heightM) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float fx = r.x0 + hx + 2.0f
            + rng.next_f01() * std::max(0.0f, r.spanX() - 2.0f * (hx + 2.0f));
        const float fy = r.y0 + hy + 2.0f
            + rng.next_f01() * std::max(0.0f, r.spanY() - 2.0f * (hy + 2.0f));
        const int cx0 = int(fx - hx) - 2, cx1 = int(fx + hx) + 2;
        const int cy0 = int(fy - hy) - 2, cy1 = int(fy + hy) + 2;
        bool clear = true;
        for (int y = cy0; y <= cy1 && clear; ++y) {
            for (int x = cx0; x <= cx1; ++x) {
                if (out.tiles[cell_at(x, y)] != TILE_SQUARE) {
                    clear = false;
                    break;
                }
            }
        }
        if (!clear) continue;
        Structure s{};
        s.kind = Structure::Furnish;
        s.x = fx;
        s.y = fy;
        s.radius = std::max(hx, hy);
        s.height = heightM;
        s.hx = hx;
        s.hy = hy;
        out.structures.push_back(s);
        for (int y = int(fy - hy); y <= int(fy + hy); ++y) {
            for (int x = int(fx - hx); x <= int(fx + hx); ++x) {
                const std::size_t i = cell_at(x, y);
                out.tiles[i] = TILE_HOUSE;
                // A solid's footprint is not walkable ground — the same
                // bookkeeping every wall does. Blocking itself lives in the
                // structure index (sub/collide.h); this keeps the grid from
                // ever disagreeing with it.
                out.trav[i] = 0;
            }
        }
        return true;
    }
    return false;
}

} // namespace

void gen_dungeon_house(const CellContext& ctx, SubworldMapData& out) {
    const std::size_t n = std::size_t(kCellSize) * kCellSize;
    const DungeonRoom room = dungeon_house_room(ctx.dungeon);
    const float wallHalf = structure_min_half_xy(Structure::Wall);
    const float wallH    = structure_min_height(Structure::Wall);

    // Flat field at the door cell's altitude: the interior floor is the land
    // the house stands on. Outside the room: sealed rock, unwalkable.
    out.heightmap.assign(n, ctx.macroHeight);
    out.tiles.assign(n, std::uint8_t(TILE_ROCK));
    out.trav.assign(n, 0);
    out.structures.clear();
    out.waterLevel = 0.0f; // an interior has no sea

    // Paved hall floor.
    const int x0 = int(room.cx - room.hx), x1 = int(room.cx + room.hx);
    const int y0 = int(room.cy - room.hy), y1 = int(room.cy + room.hy);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const std::size_t i = cell_at(x, y);
            out.tiles[i] = TILE_SQUARE;
            out.trav[i] = 1;
        }
    }

    // Four outer walls as oriented solids. North/south spans overlap the
    // corners so the perimeter is sealed by geometry, not by luck.
    const float spanX = room.hx + 2.0f * wallHalf;
    stamp_wall(out, room.cx, room.cy - room.hy - wallHalf, spanX, wallHalf,
               wallH); // north
    stamp_wall(out, room.cx, room.cy + room.hy + wallHalf, spanX, wallHalf,
               wallH); // south
    stamp_wall(out, room.cx - room.hx - wallHalf, room.cy, wallHalf, room.hy,
               wallH); // west
    stamp_wall(out, room.cx + room.hx + wallHalf, room.cy, wallHalf, room.hy,
               wallH); // east

    // Ceiling slab: one lifted solid covering hall + walls. zBase puts its
    // underside exactly at the wall crowns, so the box is sealed from above —
    // projectiles die on it and the sky is architecture, not weather.
    Structure lid{};
    lid.kind = Structure::Wall;
    lid.x = room.cx;
    lid.y = room.cy;
    lid.hx = room.hx + 2.0f * wallHalf;
    lid.hy = room.hy + 2.0f * wallHalf;
    lid.radius = std::max(lid.hx, lid.hy);
    lid.zBase = wallH;
    lid.height = kCeilingSlabM;
    out.structures.push_back(lid);

    // The threshold's position is fixed by the shared dungeon_entry_point
    // rule — resolved BEFORE the splits so a partition can be kept off it.
    // On a stair level "the pad" IS the shaft you came down/up, so this one
    // call covers every storey.
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);
    // Which shaft pads this storey carries. The NW shaft joins 0↔+1, the NE
    // shaft joins 0↔-1 — so a pad exists on BOTH storeys a shaft connects,
    // and the engine reads the direction off the current level. `worldSeed`
    // and the cell come through the context, so the cellar coin lands the
    // same way on every storey of one house.
    const int level = int(ctx.dungeon.level);
    const bool hasUpper = dungeon_has_upper(ctx.dungeon);
    const bool hasCellar =
        dungeon_has_cellar(ctx.dungeon, ctx.worldSeed, ctx.cx, ctx.cy);
    const bool padNW = (level == 0 && hasUpper) || level == 1;
    const bool padNE = (level == 0 && hasCellar) || level == -1;
    float nwX = 0.0f, nwY = 0.0f, neX = 0.0f, neY = 0.0f;
    dungeon_stair_point(ctx.dungeon, /*up*/true, nwX, nwY);
    dungeon_stair_point(ctx.dungeon, /*up*/false, neX, neY);

    // ── Rooms: split the hall while there is span to spare ─────────────────
    // Each split runs one full partition across a rectangle's longer axis
    // with a single doorway. A straight wall with one gap keeps both halves
    // connected, so connectivity holds by construction at every depth.
    Rng rng(ctx.seed ^ 0x0D9E0A11u);
    RoomRect rects[4] = {{float(x0), float(y0), float(x1), float(y1)}, {}, {}, {}};
    int rectCount = 1;
    // A cellar is ONE vault: no partitions, no furniture. That is what makes
    // it the house's fight room (Inc 4 puts the vermin here) rather than a
    // second set of bedrooms.
    const int splitBudget = level < 0 ? 0 : 3;
    for (int split = 0; split < splitBudget && rectCount < 4; ++split) {
        // Widest rectangle first — halls split before closets do.
        int pick = 0;
        for (int i = 1; i < rectCount; ++i) {
            if (std::max(rects[i].spanX(), rects[i].spanY())
                > std::max(rects[pick].spanX(), rects[pick].spanY())) {
                pick = i;
            }
        }
        RoomRect r = rects[pick];
        const bool alongX = r.spanX() >= r.spanY(); // partition ⊥ longer axis
        const float span = alongX ? r.spanX() : r.spanY();
        if (span < 2.0f * kMinRoomSpanTiles) break;
        // Partition coordinate: 40–60 % of the span, so no room degenerates.
        float cut = (alongX ? r.x0 : r.y0)
            + span * (0.4f + rng.next_f01() * 0.2f);
        // Keep a partition off every pad on this storey: the wall STRUCTURE
        // would stand on the very tile a body materialises on (the pad's
        // paint is repaired below, the solid would not be). One doorway width
        // + a tile of clearance decides "too close". Shaft pads sit in the
        // north corners, so a horizontal partition can foul them too.
        const float padAxis[3] = {px, padNW ? nwX : px, padNE ? neX : px};
        const float padAxisY[3] = {py, padNW ? nwY : py, padNE ? neY : py};
        for (int k = 0; k < 3; ++k) {
            const float v = alongX ? padAxis[k] : padAxisY[k];
            if (std::abs(cut - v) < kDoorGapTiles + 1.0f) {
                cut = v + (cut < v ? -(kDoorGapTiles + 1.0f)
                                   : (kDoorGapTiles + 1.0f));
            }
        }
        // A nudge must never push the partition out of the rectangle it
        // divides (a wall outside the hall, or a room of zero width). Both
        // halves keep the manoeuvre span the split was allowed for.
        const float lo0 = (alongX ? r.x0 : r.y0) + kMinRoomSpanTiles;
        const float hi0 = (alongX ? r.x1 : r.y1) - kMinRoomSpanTiles;
        if (lo0 > hi0) break;               // no honest cut left in this rect
        cut = std::clamp(cut, lo0, hi0);
        // Doorway centre along the partition's run, clear of the outer walls.
        const float run0 = (alongX ? r.y0 : r.x0) + kDoorGapTiles;
        const float run1 = (alongX ? r.y1 : r.x1) - kDoorGapTiles;
        const float gap = run0 + rng.next_f01() * std::max(0.0f, run1 - run0);
        const float g0 = gap - kDoorGapTiles * 0.5f;
        const float g1 = gap + kDoorGapTiles * 0.5f;
        // Two wall segments flanking the doorway.
        if (alongX) {
            const float lo = r.y0 - wallHalf, hi = r.y1 + wallHalf;
            stamp_wall(out, cut, (lo + g0) * 0.5f, wallHalf, (g0 - lo) * 0.5f,
                       wallH);
            stamp_wall(out, cut, (g1 + hi) * 0.5f, wallHalf, (hi - g1) * 0.5f,
                       wallH);
            rects[rectCount++] = {cut + wallHalf + 1.0f, r.y0, r.x1, r.y1};
            rects[pick].x1 = cut - wallHalf - 1.0f;
        } else {
            const float lo = r.x0 - wallHalf, hi = r.x1 + wallHalf;
            stamp_wall(out, (lo + g0) * 0.5f, cut, (g0 - lo) * 0.5f, wallHalf,
                       wallH);
            stamp_wall(out, (g1 + hi) * 0.5f, cut, wallHalf, (hi - g1) * 0.5f,
                       wallH);
            rects[rectCount++] = {r.x0, cut + wallHalf + 1.0f, r.x1, r.y1};
            rects[pick].y1 = cut - wallHalf - 1.0f;
        }
    }

    // Pads BEFORE furniture: the tiles a body materialises on — the exterior
    // threshold and every shaft this storey carries — paved as road so the
    // ways out read at a glance, and so furniture clearance (which demands
    // plain TILE_SQUARE) can never wall one in. A stair level has no
    // exterior door: its own arrival pad IS the shaft, already covered here.
    auto pave_pad = [&](float cx, float cy) {
        for (int y = int(cy) - 1; y <= int(cy) + 1; ++y) {
            for (int x = int(cx) - 1; x <= int(cx) + 1; ++x) {
                const std::size_t i = cell_at(x, y);
                out.tiles[i] = TILE_ROAD;
                out.trav[i] = 1;
            }
        }
    };
    // Every threshold is a PROP you can see and aim at, not a patch of paint:
    // the way out is the same kind of door that let you in, and each shaft
    // carries a stair block. The interaction comes from their table rows.
    auto place_prop = [&](Structure::Kind kind, float cx, float cy,
                          std::uint16_t tag, float yaw) {
        Structure s{};
        s.kind = kind;
        s.x = cx;
        s.y = cy;
        s.yaw = yaw;
        s.hx = structure_min_half_xy(kind) * (kind == Structure::Door ? 3.0f
                                                                      : 1.0f);
        s.hy = structure_min_half_xy(kind);
        s.radius = std::max(s.hx, s.hy);
        s.height = structure_min_height(kind);
        s.tag = tag;
        out.structures.push_back(s);
    };
    if (level == 0) {
        pave_pad(px, py);
        // The exit door stands ON the south wall the pad faces, so it reads
        // as the way out from anywhere in the room.
        place_prop(Structure::Door, px, room.cy + room.hy + wallHalf, 0, 0.0f);
    }
    if (padNW) {
        pave_pad(nwX, nwY);
        place_prop(Structure::Stairs, nwX, nwY, /*tag: up*/1, 0.0f);
    }
    if (padNE) {
        pave_pad(neX, neY);
        place_prop(Structure::Stairs, neX, neY, /*tag: down*/0, 0.0f);
    }

    // ── Furniture: every room gets a household's worth ──────────────────────
    // Sizes in tiles (half-extents) at the body scale the combat runs on:
    // a bed outspans a body, a table seats one each side, a chest hugs a
    // corner. Heights stay under the Furnish stub floor's intent (waist-high).
    // A cellar keeps its floor clear (splitBudget above): stores are what the
    // vermin displaced.
    if (level >= 0) {
        for (int i = 0; i < rectCount; ++i) {
            const RoomRect& r = rects[i];
            (void)try_place_furnish(out, rng, r, 2.0f, 1.2f, 0.6f); // bed
            (void)try_place_furnish(out, rng, r, 1.6f, 1.6f, 0.9f); // table
            (void)try_place_furnish(out, rng, r, 1.0f, 0.8f, 0.6f); // chest
        }
    }
}

} // namespace sm::sub
