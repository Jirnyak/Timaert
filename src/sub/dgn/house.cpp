// House interior — the first dungeon module (Inc 1: one sealed hall).
// Self-contained: all house-interior geometry lives here; dispatch.cpp only
// routes. The interior is a projection of the door's Structure record — same
// seed, same footprint ⇒ byte-identical room, nothing persisted.
#include "sub/dgn/dispatch.h"

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
            const std::size_t i = std::size_t(y) * kCellSize + std::size_t(x);
            out.tiles[i] = TILE_SQUARE;
            out.trav[i] = 1;
        }
    }

    // Four walls as oriented solids. North/south spans overlap the corners so
    // the perimeter is sealed by geometry, not by luck.
    auto wall = [&](float cx, float cy, float hx, float hy) {
        Structure s{};
        s.kind = Structure::Wall;
        s.x = cx;
        s.y = cy;
        s.radius = std::max(hx, hy);
        s.height = wallH;
        s.hx = hx;
        s.hy = hy;
        out.structures.push_back(s);
        // Paint the footprint band so the minimap/material read as masonry.
        const int bx0 = int(cx - hx), bx1 = int(cx + hx);
        const int by0 = int(cy - hy), by1 = int(cy + hy);
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                const std::size_t i = std::size_t(y) * kCellSize + std::size_t(x);
                out.tiles[i] = TILE_WALL;
                out.trav[i] = 0;
            }
        }
    };
    const float spanX = room.hx + 2.0f * wallHalf;
    wall(room.cx, room.cy - room.hy - wallHalf, spanX, wallHalf);   // north
    wall(room.cx, room.cy + room.hy + wallHalf, spanX, wallHalf);   // south
    wall(room.cx - room.hx - wallHalf, room.cy, wallHalf, room.hy); // west
    wall(room.cx + room.hx + wallHalf, room.cy, wallHalf, room.hy); // east

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

    // Exit pad: the threshold you appear on and leave from (the shared
    // dungeon_entry_point rule). Paved as road so the way out is readable.
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);
    for (int y = int(py) - 1; y <= int(py) + 1; ++y) {
        for (int x = int(px) - 1; x <= int(px) + 1; ++x) {
            const std::size_t i = std::size_t(y) * kCellSize + std::size_t(x);
            out.tiles[i] = TILE_ROAD;
            out.trav[i] = 1;
        }
    }
}

} // namespace sm::sub
