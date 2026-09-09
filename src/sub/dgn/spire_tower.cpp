// Spire tower interior — one round hall per storey, climbed bottom to top.
// Self-contained module (dispatch.cpp only routes): the tower every spire
// raises is the SAME tower (gens/dispatch gen_spire stamps one law's
// cylinder), so unlike a house the interior ignores the door's footprint —
// what varies between spires is the storey count, and that is the spell's
// tier riding DungeonRef::ordinal. Nothing persisted: same identity, same
// hall, byte for byte.
#include "sub/dgn/dispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sm::sub {

namespace {

// The masonry ring is built of oriented Wall segments; enough of them that
// the chord's sag stays well under the wall's own half-thickness (1.2 tiles),
// so the ring is sealed by geometry rather than by luck, and enough that the
// eye reads a circle rather than a polygon at arm's length: sag =
// R·(1−cos(π/N)) ≈ 0.24 tiles at N=32, R=48.
constexpr int kWallSegments = 32;
// Ceiling slab over the hall, the house's own 1 m — an honest solid for
// projectiles, and the visual floor of the storey above.
constexpr float kCeilingSlabM = 1.0f;

inline std::size_t cell_at(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

} // namespace

int dungeon_spire_tower_floors(const DungeonRef& ref) {
    // ordinal = the spell's tier (1..5). Clamp because a foreign ordinal
    // must degrade to a legal tower, not to UB — the clamp bounds are the
    // spell tier domain (macro/spells.h: tier 1..5).
    return std::clamp(int(ref.ordinal), 1, 5);
}

DungeonRoom dungeon_spire_tower_room(const DungeonRef&) {
    DungeonRoom room;
    room.cx = float(kCellSize) / 2.0f;
    room.cy = float(kCellSize) / 2.0f;
    // The hall is its own number (dgn/dispatch.h), not the facade times a
    // scale: every spire raises the same tower, so there is no varying input
    // to derive from — only a fight floor to state.
    room.hx = kSpireTowerHallRadiusTiles;
    room.hy = kSpireTowerHallRadiusTiles;
    return room;
}

void gen_dungeon_spire_tower(const CellContext& ctx, SubworldMapData& out) {
    const std::size_t n = std::size_t(kCellSize) * kCellSize;
    const DungeonRoom room = dungeon_spire_tower_room(ctx.dungeon);
    const float wallHalf = structure_min_half_xy(Structure::Wall);
    const float wallH    = structure_min_height(Structure::Wall);
    const float r        = room.hx;

    // Sealed rock everywhere, one round paved hall carved at the centre.
    out.heightmap.assign(n, ctx.macroHeight);
    out.tiles.assign(n, std::uint8_t(TILE_ROCK));
    out.trav.assign(n, 0);
    out.structures.clear();
    out.waterLevel = 0.0f; // an interior has no sea

    const float r2 = r * r;
    for (int y = int(room.cy - r); y <= int(room.cy + r); ++y) {
        for (int x = int(room.cx - r); x <= int(room.cx + r); ++x) {
            const float dx = float(x) + 0.5f - room.cx;
            const float dy = float(y) + 0.5f - room.cy;
            if (dx * dx + dy * dy > r2) continue;
            const std::size_t i = cell_at(x, y);
            out.tiles[i] = TILE_SQUARE;
            out.trav[i] = 1;
        }
    }

    // The masonry ring: oriented Wall chords tangent to the circle, each a
    // half-chord long plus a wall's own half so neighbours overlap and the
    // perimeter is sealed by geometry. TILE_WALL bookkeeping under each
    // segment keeps the grid agreeing with the collision index.
    const float ringR = r + wallHalf;
    const float halfChord =
        ringR * std::sin(3.14159265f / float(kWallSegments)) + wallHalf;
    for (int s = 0; s < kWallSegments; ++s) {
        const float a = (float(s) + 0.5f) * 2.0f * 3.14159265f
                      / float(kWallSegments);
        Structure seg{};
        seg.kind = Structure::Wall;
        seg.x = room.cx + std::cos(a) * ringR;
        seg.y = room.cy + std::sin(a) * ringR;
        seg.yaw = a + 3.14159265f / 2.0f; // tangent to the circle
        seg.hx = halfChord;
        seg.hy = wallHalf;
        seg.radius = seg.hx;
        seg.height = wallH;
        out.structures.push_back(seg);
        const int bx0 = int(seg.x - halfChord), bx1 = int(seg.x + halfChord);
        const int by0 = int(seg.y - halfChord), by1 = int(seg.y + halfChord);
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                const float dx = float(x) + 0.5f - room.cx;
                const float dy = float(y) + 0.5f - room.cy;
                const float d2 = dx * dx + dy * dy;
                // Paint only the annulus the ring actually occupies, or the
                // corner of a chord's box would wall the hall's own floor.
                if (d2 < r2 || d2 > (ringR + wallHalf) * (ringR + wallHalf)) {
                    continue;
                }
                const std::size_t i = cell_at(x, y);
                out.tiles[i] = TILE_WALL;
                out.trav[i] = 0;
            }
        }
    }

    // Ceiling: one lifted cylinder capping hall + ring, exactly the house
    // lid's law with the tower's own shape.
    Structure lid{};
    lid.kind = Structure::Wall;
    lid.shape = Structure::Cylinder;
    lid.x = room.cx;
    lid.y = room.cy;
    lid.radius = ringR + wallHalf;
    lid.zBase = wallH;
    lid.height = kCeilingSlabM;
    out.structures.push_back(lid);

    const int level  = int(ctx.dungeon.level);
    const int floors = dungeon_spire_tower_floors(ctx.dungeon);

    auto pave_pad = [&](float cx, float cy) {
        for (int y = int(cy) - 1; y <= int(cy) + 1; ++y) {
            for (int x = int(cx) - 1; x <= int(cx) + 1; ++x) {
                const std::size_t i = cell_at(x, y);
                out.tiles[i] = TILE_ROAD;
                out.trav[i] = 1;
            }
        }
    };
    // Ground floor: the way back out to the scorched ring, on the same
    // south threshold rule every interior keeps (dungeon_entry_point). The
    // only prop this module places by hand — a leaf hanging on the inner face
    // of the ring, 1.5 tiles across like the street door it mirrors.
    if (level == 0) {
        float px = 0.0f, py = 0.0f;
        dungeon_entry_point(ctx.dungeon, px, py);
        pave_pad(px, py);
        Structure gate{};
        gate.kind = Structure::SpireGate;
        gate.x = px;
        gate.y = room.cy + r - structure_min_half_xy(Structure::SpireGate);
        gate.hx = 0.75f;
        gate.hy = structure_min_half_xy(Structure::SpireGate);
        gate.radius = std::max(gate.hx, gate.hy);
        gate.height = structure_min_height(Structure::SpireGate);
        gate.tag = ctx.dungeon.ordinal;
        out.structures.push_back(gate);
    }
    // The ladder of shafts: W climbs, E descends (dungeon_stair_point). Each
    // end is stamped by the shared shaft law — a ladder to a ceiling opening
    // going up, a lid in the floor going down.
    if (level < floors - 1) {
        float ux = 0.0f, uy = 0.0f;
        dungeon_stair_point(ctx.dungeon, /*up*/true, ux, uy);
        pave_pad(ux, uy);
        stamp_dungeon_shaft(out, /*up*/true, ux, uy, wallH);
    }
    if (level > 0) {
        float dx = 0.0f, dy = 0.0f;
        dungeon_stair_point(ctx.dungeon, /*up*/false, dx, dy);
        pave_pad(dx, dy);
        stamp_dungeon_shaft(out, /*up*/false, dx, dy, wallH);
    }
    // Top storey: the climb OUT, onto the crown where the orb waits. It is the
    // last rung of the same ladder and looks like one — the opening it reaches
    // is the crown's own hatch seen from underneath. A door leaf stood here
    // once, which read as a wardrobe in the middle of a round room and told
    // the eye nothing about the sky above it.
    if (level == floors - 1) {
        float hx = 0.0f, hy = 0.0f;
        dungeon_roof_hatch_point(ctx.dungeon, hx, hy);
        pave_pad(hx, hy);
        stamp_dungeon_shaft(out, /*up*/true, hx, hy, wallH);
    }
}

} // namespace sm::sub
