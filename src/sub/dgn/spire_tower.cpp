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

// The same M&M interior scale the house uses (owner ruling 2026-08-12:
// inside is roomier than outside): the 7-tile exterior radius becomes a
// 28-tile hall — a 56-tile round fight floor, wide enough that the guards'
// separation ring and kPlayerMeleeRange (5) never collapse the fight into
// the doorway.
constexpr float kTowerInteriorScale = 4.0f;
// The masonry ring is built of oriented Wall segments; enough of them that
// the largest gap between chords stays under a body radius (1.5 tiles):
// chord sag = R·(1−cos(π/N)) ≈ 0.17 tiles at N=24, R=28 — sealed by
// geometry with margin, not by luck.
constexpr int kWallSegments = 24;
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
    const float r = kSpireTowerRadiusTiles * kTowerInteriorScale;
    room.hx = r;
    room.hy = r;
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
    auto place_prop = [&](Structure::Kind kind, float cx, float cy,
                          std::uint16_t tag) {
        Structure s{};
        s.kind = kind;
        s.x = cx;
        s.y = cy;
        s.hx = kind == Structure::SpireGate
            ? 0.75f // a leaf 1.5 tiles across, like the street door
            : structure_min_half_xy(kind);
        s.hy = structure_min_half_xy(kind);
        s.radius = std::max(s.hx, s.hy);
        s.height = structure_min_height(kind);
        s.tag = tag;
        out.structures.push_back(s);
    };

    // Ground floor: the way back out to the scorched ring, on the same
    // south threshold rule every interior keeps (dungeon_entry_point).
    if (level == 0) {
        float px = 0.0f, py = 0.0f;
        dungeon_entry_point(ctx.dungeon, px, py);
        pave_pad(px, py);
        place_prop(Structure::SpireGate, px,
                   room.cy + r - structure_min_half_xy(Structure::SpireGate),
                   ctx.dungeon.ordinal);
    }
    // The ladder of shafts: W climbs, E descends (dungeon_stair_point).
    if (level < floors - 1) {
        float ux = 0.0f, uy = 0.0f;
        dungeon_stair_point(ctx.dungeon, /*up*/true, ux, uy);
        pave_pad(ux, uy);
        place_prop(Structure::Stairs, ux, uy, /*tag: up*/1);
    }
    if (level > 0) {
        float dx = 0.0f, dy = 0.0f;
        dungeon_stair_point(ctx.dungeon, /*up*/false, dx, dy);
        pave_pad(dx, dy);
        place_prop(Structure::Stairs, dx, dy, /*tag: down*/0);
    }
    // Top storey: the hatch onto the crown, where the orb waits.
    if (level == floors - 1) {
        float hx = 0.0f, hy = 0.0f;
        dungeon_roof_hatch_point(ctx.dungeon, hx, hy);
        pave_pad(hx, hy);
        place_prop(Structure::SpireGate, hx, hy, ctx.dungeon.ordinal);
    }
}

} // namespace sm::sub
