// Locks the HOUSE INTERIOR dungeon generator (sub/dgn/house.cpp routed via
// sub/dgn/dispatch.cpp and the dispatch_generate early-return in
// sub/gens/dispatch.cpp) — Inc 2: the hall is SPLIT into rooms by seeded
// partition walls (two segments flanking a doorway gap) and each room gets
// solid Furnish furniture. Layouts differ per seed, so nothing below pins a
// specific interior tile; every claim is an invariant that holds for ANY seed.
//
// What is promised and asserted here:
//   1. Determinism — the interior is a pure projection of the door's
//      DungeonRef: same context twice ⇒ byte/field-identical map; a different
//      exterior footprint ⇒ a different tile grid (the footprint DRIVES the
//      geometry, it is not decoration).
//   2. Room geometry through the ONE shared rule (dungeon_room): centred on
//      the cell, half-extents clamped between the House record's degenerate
//      floor (scaled like any facade) and the cell interior minus the sealed
//      apron; interior paint inside, sealed rock outside.
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
//   8. CONNECTIVITY, for several seeds × footprints — a 4-neighbour flood
//      fill from the entry pad over walkable paint (TILE_SQUARE | TILE_ROAD)
//      reaches EVERY walkable tile: every doorway exists, and no partition or
//      furniture seals a room off. Plus, per case: no ground-level solid
//      footprint overlaps the pad's 3×3; every Furnish lies inside the room;
//      a big footprint actually splits (interior partition segments exist)
//      and furnishes (aggregates asserted, loop counts asserted).
//   9. NEGATIVE CONTROL for the flood-fill detector — paint one full row of
//      rock straight across the cell (severing every doorway column) and the
//      SAME detector must report unreachable walkable tiles.
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
    ctx.landmark.id = -1;
    ctx.landmark.size = 0;
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

// The two tile paints a body may stand on inside a house: hall floor and the
// entry pad's road. TILE_WALL (partition bands) and TILE_HOUSE (furniture
// footprints) are architecture.
bool walkable_tile(std::uint8_t t) {
    return t == TILE_SQUARE || t == TILE_ROAD;
}

// Connectivity detector: 4-neighbour flood fill from (sx, sy) over walkable
// paint. `totalWalkable` recounts the grid it was GIVEN (so a mutated grid is
// recounted, never trusted from before the mutation); `reached` is what the
// fill actually visited — the two can only agree if every doorway exists.
struct Flood {
    int totalWalkable = 0;
    int reached = 0;
};
Flood flood_walkable(const std::vector<std::uint8_t>& tiles, int sx, int sy) {
    Flood f{};
    for (std::uint8_t t : tiles) {
        if (walkable_tile(t)) ++f.totalWalkable;
    }
    if (sx < 0 || sy < 0 || sx >= kCellSize || sy >= kCellSize) return f;
    if (!walkable_tile(tiles[cell_index(sx, sy)])) return f;
    std::vector<std::uint8_t> seen(tiles.size(), 0);
    std::vector<int> stack;
    seen[cell_index(sx, sy)] = 1;
    stack.push_back(sy * kCellSize + sx);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        ++f.reached;
        const int x = i % kCellSize;
        const int y = i / kCellSize;
        auto push = [&](int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= kCellSize || ny >= kCellSize) return;
            const std::size_t j = cell_index(nx, ny);
            if (seen[j] || !walkable_tile(tiles[j])) return;
            seen[j] = 1;
            stack.push_back(ny * kCellSize + nx);
        };
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }
    return f;
}

// Pad-safety detector, same style as ring_coverage: each of the pad's 3×3
// tiles is a unit square; a GROUND-LEVEL solid footprint overlapping any of
// them with positive area (strict inequalities — an abutting outer wall is
// legal) would trap or crush the body that materialises there.
struct PadOverlap {
    int samples = 0;
    int overlaps = 0;
};
PadOverlap pad_overlap(const std::vector<Structure>& structures,
                       float px, float py) {
    PadOverlap r{};
    for (int ty = int(py) - 1; ty <= int(py) + 1; ++ty) {
        for (int tx = int(px) - 1; tx <= int(px) + 1; ++tx) {
            ++r.samples;
            for (const Structure& s : structures) {
                if (!structure_is_solid(s.kind)) continue;
                if (s.zBase != 0.0f) continue; // the lid hangs overhead
                const float hx = structure_half_x(s);
                const float hy = structure_half_y(s);
                if (s.x - hx < float(tx + 1) && s.x + hx > float(tx) &&
                    s.y - hy < float(ty + 1) && s.y + hy > float(ty)) {
                    ++r.overlaps;
                }
            }
        }
    }
    return r;
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
    // The room centre may legally sit under a partition band or a furniture
    // footprint on some seeds — assert only what holds for ANY seed: it is
    // INTERIOR paint (the hall pass reached it), never the exterior seal.
    const int cx = int(room.cx), cy = int(room.cy);
    CHECK(out.tiles[cell_index(cx, cy)] != TILE_ROCK,
          "the room centre carries interior paint, not the exterior seal");
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
    // (Partitions and furniture are grounded — zBase 0 — so the lid stays
    // the only lifted body no matter the seed.)
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
    // The first ground-level Wall is an OUTER wall (the generator builds the
    // perimeter before any partition), so the punched hole is on the ring.
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

// ── 8. Rooms, furniture, connectivity — invariants for ANY seed ────────────
void test_rooms_and_connectivity() {
    // Furnish's solidity is the table row the whole increment leans on
    // (rooms fight AROUND furniture) — compile-time constant, asserted as
    // the contract so a retune of kStructureKindRows cannot silently unwire
    // interior collision.
    CHECK(structure_is_solid(Structure::Furnish),
          "contract: Furnish is a solid kind in kStructureKindRows");

    const std::uint32_t seeds[] = {1u, 12345u, 999u};
    // Two exterior footprints: the small facade the geometry tests use, and
    // a square one big enough that the hall has span to spare — partitions
    // and furniture must actually appear there.
    const float foots[][2] = {{8.0f, 6.0f}, {10.0f, 10.0f}};
    constexpr int kSeedCount = int(sizeof(seeds) / sizeof(seeds[0]));
    constexpr int kFootCount = int(sizeof(foots) / sizeof(foots[0]));
    constexpr int kBigFoot = 1; // index of the split-worthy footprint

    int casesTested = 0;
    int padSamples = 0, padOverlaps = 0;
    int bestPartitionSegments = 0; // most interior segments any big-foot seed built
    int furnishTotal = 0;          // Furnish across all big-foot seeds
    int furnishOutsideRoom = 0;    // any Furnish leaking past the hall rect

    for (int fi = 0; fi < kFootCount; ++fi) {
        for (int si = 0; si < kSeedCount; ++si) {
            CellContext ctx =
                make_ctx(DungeonRef::House, foots[fi][0], foots[fi][1]);
            ctx.seed = seeds[si];
            const DungeonRoom room = dungeon_room(ctx.dungeon);
            SubworldMapData out{};
            generate(ctx, out);
            ++casesTested;

            float px = 0.0f, py = 0.0f;
            dungeon_entry_point(ctx.dungeon, px, py);

            // THE key Inc 2 invariant: from the entry pad, the flood fill
            // reaches every walkable tile in the cell — every partition has
            // a live doorway and no furniture seals a room off.
            const Flood f = flood_walkable(out.tiles, int(px), int(py));
            CHECK(f.totalWalkable > 0,
                  "each seeded interior has walkable floor at all");
            CHECK(f.reached == f.totalWalkable,
                  "every walkable tile is reachable from the entry pad");

            // Exit pad safety: no ground-level solid footprint (wall OR
            // furniture) overlaps the 3×3 the body materialises on.
            const PadOverlap po = pad_overlap(out.structures, px, py);
            padSamples += po.samples;
            padOverlaps += po.overlaps;

            // Partition + furniture census for the aggregates below.
            int interiorSegments = 0;
            for (const Structure& s : out.structures) {
                if (s.kind == Structure::Wall && s.zBase == 0.0f &&
                    std::fabs(s.x - room.cx) < room.hx &&
                    std::fabs(s.y - room.cy) < room.hy) {
                    // Ground-level wall whose CENTRE lies strictly inside the
                    // hall: outer walls sit on the perimeter band outside the
                    // interior, so this can only be a partition segment.
                    ++interiorSegments;
                }
                if (s.kind == Structure::Furnish) {
                    if (fi == kBigFoot) ++furnishTotal;
                    // Whole footprint strictly inside the hall rectangle.
                    if (!(std::fabs(s.x - room.cx) + structure_half_x(s)
                              < room.hx &&
                          std::fabs(s.y - room.cy) + structure_half_y(s)
                              < room.hy)) {
                        ++furnishOutsideRoom;
                    }
                }
            }
            if (fi == kBigFoot && interiorSegments > bestPartitionSegments) {
                bestPartitionSegments = interiorSegments;
            }
        }
    }

    CHECK(casesTested == kSeedCount * kFootCount,
          "the sweep tested every seed × footprint case");
    CHECK(padSamples > 0 && padOverlaps == 0,
          "no ground-level solid footprint overlaps any entry pad tile");
    // One partition = two wall segments flanking its doorway; a 40-tile-half
    // hall has span to spare, so at least one tested seed must have split.
    CHECK(bestPartitionSegments >= 2,
          "the big footprint grows interior partition walls on some seed");
    CHECK(furnishTotal > 0,
          "the big footprint furnishes at least one room across the seeds");
    CHECK(furnishOutsideRoom == 0,
          "every Furnish footprint lies strictly inside the hall");
}

// ── 9. Negative control for the connectivity detector ──────────────────────
void test_connectivity_negative_control() {
    CellContext ctx = make_ctx(DungeonRef::House, 10.0f, 10.0f);
    ctx.seed = 1u;
    const DungeonRoom room = dungeon_room(ctx.dungeon);
    SubworldMapData out{};
    generate(ctx, out);
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);

    // Fixture: untouched, the detector reports full connectivity — the state
    // section 8 calls green.
    const Flood before = flood_walkable(out.tiles, int(px), int(py));
    CHECK(before.totalWalkable > 0 && before.reached == before.totalWalkable,
          "fixture: the untouched interior is fully connected");

    // Sever the pad's strip: one full row of rock straight across the cell,
    // north of the pad and clear of its 3×3 — it crosses every doorway
    // column, so no path can round it.
    const int barrierY = int(py) - 3;
    CHECK(barrierY > int(room.cy - room.hy) && barrierY < int(py) - 1,
          "fixture: the barrier row lies inside the room, clear of the pad");
    for (int x = 0; x < kCellSize; ++x) {
        out.tiles[cell_index(x, barrierY)] = TILE_ROCK;
    }

    // Recount AFTER the mutation — the detector must never be handed a stale
    // total it could trivially match. Both sides of the barrier must still
    // hold walkable floor, or "unreachable" would be vacuous.
    int north = 0, south = 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            if (!walkable_tile(out.tiles[cell_index(x, y)])) continue;
            (y < barrierY ? north : south) += 1;
        }
    }
    CHECK(north > 0 && south > 0,
          "fixture: walkable floor survives on both sides of the barrier");

    const Flood after = flood_walkable(out.tiles, int(px), int(py));
    CHECK(after.totalWalkable == north + south,
          "the detector recounts the mutated grid, not the original");
    CHECK(after.reached > 0,
          "the pad's own side is still reachable after the barrier");
    // The fill starts south of the barrier, so everything north of it —
    // known-walkable floor — must be missing from the reach.
    CHECK(after.totalWalkable - after.reached >= north,
          "the severed north side must be unreachable — the detector SEES it");
}

} // namespace

int main() {
    test_determinism();
    test_room_geometry();
    test_entry_pad();
    test_sealed_box();
    test_void_filler();
    test_rooms_and_connectivity();
    test_connectivity_negative_control();
    return sm::test::report("dungeon_house_test");
}
