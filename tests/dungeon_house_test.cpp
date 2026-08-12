// Locks the HOUSE INTERIOR dungeon generator (sub/dgn/house.cpp routed via
// sub/dgn/dispatch.cpp and the dispatch_generate early-return in
// sub/gens/dispatch.cpp).
//
// What is promised and asserted here:
//   1. Determinism — the interior is a pure projection of the door's
//      DungeonRef: same context twice ⇒ byte/field-identical map; a different
//      exterior footprint ⇒ a different tile grid (the footprint DRIVES the
//      geometry, it is not decoration).
//   2. Room geometry through the ONE shared rule (dungeon_room): centred on
//      the cell, half-extents clamped between the House record's degenerate
//      floor (scaled like any facade) and the cell interior minus the sealed
//      apron; paved+walkable inside, sealed rock outside.
//   3. The entry pad lies strictly inside the room and its 3×3 is road and
//      walkable — the threshold the engine and the generator must agree on.
//   4. The perimeter is sealed by GEOMETRY: every point of the ring one wall
//      half-thickness outside the room interior lies inside at least one
//      ground-level solid Wall footprint (measuring loop: samples > 0 &&
//      uncovered == 0).
//   5. Exactly one lifted structure (the ceiling slab), seated exactly on the
//      wall crowns (zBase == structure_min_height(Wall)), covering the room.
//   6. NEGATIVE CONTROL — remove one wall and the same detector MUST report
//      uncovered ring samples, proving it can see a hole.
//   7. The Void filler (dungeon window ring cells) builds nothing walkable:
//      no structures, trav all 0, zero paved tiles, dead-flat heightmap.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

// Per-cell buffers are kCellSize-strided (the same convention every open-air
// generator writes and seamless_manager's composite blit reads).
std::size_t cell_index(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

CellContext make_ctx(std::uint8_t kind, float footHx, float footHy) {
    CellContext ctx{};
    ctx.cx = 3;
    ctx.cy = -2;
    ctx.macroHeight = 0.62f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.landmarkSettlementId = -1;
    ctx.landmarkSize = 0;
    ctx.seed = 0xD00DCAFEu;
    ctx.dungeon.kind = kind;
    ctx.dungeon.level = 0;
    ctx.dungeon.ordinal = 7;
    ctx.dungeon.footHx = footHx;
    ctx.dungeon.footHy = footHy;
    return ctx;
}

// Full production route: dispatch_generate must itself divert into the
// dungeon pipeline when the context carries a door.
void generate(const CellContext& ctx, SubworldMapData& out) {
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = ctx.macroHeight;
        nbB[i] = ctx.biome;
        nbF[i] = std::uint8_t(FT_None);
    }
    dispatch_generate(ctx, nbH, nbB, nbF, out);
}

bool structures_identical(const std::vector<Structure>& a,
                          const std::vector<Structure>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Structure& s = a[i];
        const Structure& t = b[i];
        if (s.kind != t.kind || s.shape != t.shape) return false;
        if (s.x != t.x || s.y != t.y) return false;
        if (s.radius != t.radius || s.height != t.height) return false;
        if (s.yaw != t.yaw || s.hx != t.hx || s.hy != t.hy) return false;
        if (s.zBase != t.zBase) return false;
    }
    return true;
}

// Sealed-perimeter detector. Walks the ring one wall half-thickness outside
// the room interior and counts samples not covered by any GROUND-LEVEL solid
// Wall footprint (yaw == 0 axis-aligned boxes via the one structure-geometry
// contract). The ceiling slab (zBase > 0) hangs above head height and must
// NOT count as sealing, or a lidded but wall-less room would pass.
struct RingCoverage {
    int samples = 0;
    int uncovered = 0;
};
RingCoverage ring_coverage(const DungeonRoom& room,
                           const std::vector<Structure>& structures) {
    const float wallHalf = structure_min_half_xy(Structure::Wall);
    const float rx = room.hx + wallHalf; // ring radius: inside the wall band
    const float ry = room.hy + wallHalf;
    RingCoverage cov{};
    auto probe = [&](float px, float py) {
        ++cov.samples;
        for (const Structure& s : structures) {
            if (s.kind != Structure::Wall) continue;
            if (s.zBase != 0.0f) continue;                    // lifted = lid
            if (!structure_is_solid(s.kind)) continue;        // must block
            if (std::fabs(px - s.x) <= structure_half_x(s) &&
                std::fabs(py - s.y) <= structure_half_y(s)) {
                return;                                       // covered
            }
        }
        ++cov.uncovered;
    };
    const int kSteps = 256; // dense enough that a one-tile gap cannot slip by
    for (int i = 0; i <= kSteps; ++i) {
        const float t = -1.0f + 2.0f * float(i) / float(kSteps); // [-1, 1]
        probe(room.cx + t * rx, room.cy - ry);                   // north edge
        probe(room.cx + t * rx, room.cy + ry);                   // south edge
        probe(room.cx - rx, room.cy + t * ry);                   // west edge
        probe(room.cx + rx, room.cy + t * ry);                   // east edge
    }
    return cov;
}

// ── 1. Determinism + footprint drives geometry ─────────────────────────────
void test_determinism() {
    const CellContext ctx = make_ctx(DungeonRef::House, 8.0f, 6.0f);
    CHECK(resolve_mode(ctx) == SubworldMode::Dungeon,
          "a context carrying a door must resolve to SubworldMode::Dungeon");

    SubworldMapData a{}, b{};
    generate(ctx, a);
    generate(ctx, b);
    CHECK(a.tiles == b.tiles, "same door twice: identical tile grid");
    CHECK(a.trav == b.trav, "same door twice: identical traversability");
    CHECK(a.heightmap == b.heightmap, "same door twice: identical heightmap");
    CHECK(structures_identical(a.structures, b.structures),
          "same door twice: identical structures, field for field");

    // A different exterior footprint must produce a different interior —
    // otherwise footHx/footHy are dead weight and every house is one room.
    SubworldMapData c{};
    generate(make_ctx(DungeonRef::House, 20.0f, 16.0f), c);
    CHECK(a.tiles.size() == c.tiles.size(),
          "both footprints fill the same per-cell grid");
    int differing = 0;
    for (std::size_t i = 0; i < a.tiles.size() && i < c.tiles.size(); ++i) {
        if (a.tiles[i] != c.tiles[i]) ++differing;
    }
    CHECK(differing > 0,
          "a bigger exterior footprint must change the interior tile grid");
}

// ── 2. Room geometry from the shared rule ──────────────────────────────────
void test_room_geometry() {
    const CellContext ctx = make_ctx(DungeonRef::House, 8.0f, 6.0f);
    const DungeonRoom room = dungeon_room(ctx.dungeon);

    // The window is centred on the door's cell, so the room is cell-centred.
    CHECK(room.cx == float(kCellSize) / 2.0f && room.cy == float(kCellSize) / 2.0f,
          "the room is centred on its cell");

    // The M&M clamp: never smaller than the House record's own degenerate
    // floor scaled like any facade, never past the cell apron. Derive both
    // bounds from the tables the generator reads, not from copies.
    const float minHalf = structure_min_half_xy(Structure::House) * 4.0f;
    const float maxHalf = float(kCellSize) / 2.0f - 32.0f;
    CHECK(room.hx >= minHalf && room.hx <= maxHalf,
          "room half-extent X respects the M&M clamp");
    CHECK(room.hy >= minHalf && room.hy <= maxHalf,
          "room half-extent Y respects the M&M clamp");
    // Inside the clamp, the interior keeps the facade's proportions scaled
    // UP (owner ruling: inside is roomier than outside) — same ratio on both
    // axes, strictly greater than 1.
    CHECK(room.hx / ctx.dungeon.footHx == room.hy / ctx.dungeon.footHy,
          "mid-range footprints scale both axes by the one factor");
    CHECK(room.hx > ctx.dungeon.footHx,
          "the interior is roomier than the facade (M&M scale > 1)");
    // The clamp floor and ceiling really clamp.
    DungeonRef tiny = ctx.dungeon;
    tiny.footHx = tiny.footHy = 0.25f;
    DungeonRef huge = ctx.dungeon;
    huge.footHx = huge.footHy = float(kCellSize);
    CHECK(dungeon_room(tiny).hx == minHalf,
          "a degenerate footprint clamps up to the House-record floor");
    CHECK(dungeon_room(huge).hx == maxHalf,
          "an oversized footprint clamps down to the cell apron");

    SubworldMapData out{};
    generate(ctx, out);
    const int cx = int(room.cx), cy = int(room.cy);
    CHECK(out.tiles[cell_index(cx, cy)] == TILE_SQUARE,
          "the room centre is paved TILE_SQUARE");
    CHECK(out.trav[cell_index(cx, cy)] == 1, "the room centre is walkable");
    // Well outside the walls but inside the cell: sealed rock.
    CHECK(out.tiles[cell_index(8, 8)] == TILE_ROCK,
          "outside the walls the cell is sealed rock");
    CHECK(out.trav[cell_index(8, 8)] == 0,
          "outside the walls nothing is walkable");
    // The interior floor is the land the house stands on.
    CHECK(out.heightmap[cell_index(cx, cy)] == ctx.macroHeight,
          "the interior floor sits at the door cell's altitude");
}

// ── 3. Entry pad ────────────────────────────────────────────────────────────
void test_entry_pad() {
    const CellContext ctx = make_ctx(DungeonRef::House, 8.0f, 6.0f);
    const DungeonRoom room = dungeon_room(ctx.dungeon);
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);
    CHECK(std::fabs(px - room.cx) < room.hx && std::fabs(py - room.cy) < room.hy,
          "the entry pad lies strictly inside the room");

    SubworldMapData out{};
    generate(ctx, out);
    int roadTiles = 0, walkable = 0, samples = 0;
    for (int y = int(py) - 1; y <= int(py) + 1; ++y) {
        for (int x = int(px) - 1; x <= int(px) + 1; ++x) {
            ++samples;
            if (out.tiles[cell_index(x, y)] == TILE_ROAD) ++roadTiles;
            if (out.trav[cell_index(x, y)] == 1) ++walkable;
        }
    }
    CHECK(samples == 9 && roadTiles == 9,
          "the 3×3 around the entry point is paved TILE_ROAD");
    CHECK(walkable == 9, "the 3×3 around the entry point is walkable");
}

// ── 4 + 5 + 6. Sealed perimeter, ceiling, and the negative control ─────────
void test_sealed_box() {
    const CellContext ctx = make_ctx(DungeonRef::House, 8.0f, 6.0f);
    const DungeonRoom room = dungeon_room(ctx.dungeon);
    SubworldMapData out{};
    generate(ctx, out);

    const RingCoverage cov = ring_coverage(room, out.structures);
    CHECK(cov.samples > 0 && cov.uncovered == 0,
          "every ring sample around the room lies inside a solid wall");

    // Exactly one lifted structure: the ceiling slab on the wall crowns.
    int lifted = 0;
    Structure lid{};
    for (const Structure& s : out.structures) {
        if (s.zBase > 0.0f) {
            ++lifted;
            lid = s;
        }
    }
    CHECK(lifted == 1, "exactly one lifted structure — the ceiling slab");
    CHECK(lid.zBase == structure_min_height(Structure::Wall),
          "the slab's underside sits exactly on the wall crowns");
    const float corners[4][2] = {
        {room.cx - room.hx, room.cy - room.hy},
        {room.cx + room.hx, room.cy - room.hy},
        {room.cx - room.hx, room.cy + room.hy},
        {room.cx + room.hx, room.cy + room.hy},
    };
    bool covers = std::fabs(room.cx - lid.x) <= structure_half_x(lid) &&
                  std::fabs(room.cy - lid.y) <= structure_half_y(lid);
    for (int i = 0; i < 4; ++i) {
        covers = covers &&
                 std::fabs(corners[i][0] - lid.x) <= structure_half_x(lid) &&
                 std::fabs(corners[i][1] - lid.y) <= structure_half_y(lid);
    }
    CHECK(covers, "the ceiling slab covers the room centre and all corners");

    // NEGATIVE CONTROL: punch out one wall and the same detector must SEE the
    // hole — otherwise "sealed" above proved only that the detector is blind.
    std::vector<Structure> punched = out.structures;
    bool removed = false;
    for (std::size_t i = 0; i < punched.size(); ++i) {
        if (punched[i].kind == Structure::Wall && punched[i].zBase == 0.0f) {
            punched.erase(punched.begin() + long(i));
            removed = true;
            break;
        }
    }
    CHECK(removed, "fixture: there is a ground-level wall to remove");
    const RingCoverage holed = ring_coverage(room, punched);
    CHECK(holed.samples > 0 && holed.uncovered > 0,
          "with one wall removed the detector must report uncovered samples");
}

// ── 7. Void filler ──────────────────────────────────────────────────────────
void test_void_filler() {
    const CellContext ctx = make_ctx(DungeonRef::Void, 0.0f, 0.0f);
    SubworldMapData out{};
    generate(ctx, out);

    CHECK(out.structures.empty(), "the Void filler builds nothing");
    int walkable = 0, paved = 0;
    for (std::size_t i = 0; i < out.trav.size(); ++i) {
        if (out.trav[i] != 0) ++walkable;
    }
    for (std::size_t i = 0; i < out.tiles.size(); ++i) {
        if (out.tiles[i] == TILE_SQUARE) ++paved;
    }
    CHECK(out.trav.size() > 0 && walkable == 0,
          "nothing in a Void cell is walkable");
    CHECK(out.tiles.size() > 0 && paved == 0,
          "a Void cell has no paved room anywhere");
    float hmin = 1e9f, hmax = -1e9f;
    for (float h : out.heightmap) {
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
    }
    CHECK(out.heightmap.size() > 0 &&
              hmin == ctx.macroHeight && hmax == ctx.macroHeight,
          "a Void cell's heightmap is dead flat at the door cell's altitude");
}

} // namespace

int main() {
    test_determinism();
    test_room_geometry();
    test_entry_pad();
    test_sealed_box();
    test_void_filler();
    return sm::test::report("dungeon_house_test");
}
