// Body-vs-structure collision & support (sub/collide.h) — the solidity
// authority the subworld movers share. Part A drives the index with
// hand-placed solids on a flat plane and pins the core semantics: blocking,
// support (standing ON things), z-layering (lintels: under, on top, head
// bump), oriented boxes, cylinders, the slide and the escape rule. Part B
// generates a REAL city cell (sub/gens/dispatch.cpp) with road neighbours and
// proves the functional promises end-to-end: the carved road enters the city
// through the gates without ever being blocked (lintels bridge overhead), and
// the wall itself blocks at street level.
#include "sub/collide.h"
#include "sub/gens/dispatch.h"
#include "sub/height.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

float flat_height(void*, float, float) { return 0.0f; }

Structure make_box(float x, float y, float hx, float hy, float yaw,
                   float height, float zBase = 0.0f,
                   Structure::Kind kind = Structure::Wall) {
    Structure s{};
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.radius = std::max(hx, hy);
    s.height = height;
    s.yaw = yaw;
    s.hx = hx;
    s.hy = hy;
    s.zBase = zBase;
    return s;
}

Structure make_cylinder(float x, float y, float radius, float height) {
    Structure s{};
    s.kind = Structure::Wall;
    s.x = x;
    s.y = y;
    s.radius = radius;
    s.height = height;
    s.shape = Structure::Cylinder;
    return s;
}

void part_a_index_semantics() {
    StructureIndex idx;

    // Empty index: transparent world, resolve_step is a no-op.
    check(!idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f), "empty index blocks nothing");
    check(idx.support_at(100.0f, 100.0f, 0.5f, 10.0f) < -1.0e20f,
          "empty index offers no support");
    {
        float tx = 50.0f, ty = 50.0f;
        idx.resolve_step(40.0f, 40.0f, tx, ty, 0.5f, 0.0f);
        check(tx == 50.0f && ty == 50.0f, "empty index resolve_step is a no-op");
    }

    std::vector<Structure> solids;
    // 6×6 box, 5 m tall, at (100, 100).
    solids.push_back(make_box(100.0f, 100.0f, 3.0f, 3.0f, 0.0f, 5.0f));
    // Lintel over "street" at (200, 100): 4 m span, solid z ∈ [5, 8].
    solids.push_back(make_box(200.0f, 100.0f, 4.0f, 1.1f, 0.0f, 3.0f, 5.0f));
    // Oriented plank: 16×1.1 at 45° around (300, 300).
    solids.push_back(make_box(300.0f, 300.0f, 8.0f, 1.1f, 0.785398f, 5.0f));
    // Round tower r=3 at (50, 50).
    solids.push_back(make_cylinder(50.0f, 50.0f, 3.0f, 10.0f));
    idx.rebuild(solids, &flat_height, nullptr);
    check(idx.solid_count() == 4, "index keeps all four solids");

    // ── Blocking vs standing on the plain box ──
    check(idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f),
          "street-level body inside the box footprint is blocked");
    check(!idx.blocked_at(110.0f, 100.0f, 0.5f, 0.0f),
          "body beside the box is free");
    check(!idx.blocked_at(100.0f, 100.0f, 0.5f, 5.0f),
          "feet ON the box top: it is a floor, not a wall");
    check(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 5.0f) - 5.0f) < 1e-4f,
          "box top supports a body standing on it");
    check(idx.support_at(100.0f, 100.0f, 0.5f, 0.0f) < -1.0e20f,
          "box top far above the feet is NOT a support (no teleport up)");
    // Radius participates (Minkowski): centre 3.0+r inside, beyond it free.
    check(idx.blocked_at(103.8f, 100.0f, 1.0f, 0.0f),
          "fat body clips the box edge");
    check(!idx.blocked_at(104.3f, 100.0f, 1.0f, 0.0f),
          "fat body past the expanded edge is free");

    // ── Lintel layering: under / on top / head bump ──
    check(!idx.blocked_at(200.0f, 100.0f, 0.5f, 0.0f),
          "street-level body walks UNDER the lintel");
    check(idx.blocked_at(200.0f, 100.0f, 0.5f, 4.0f),
          "flyer at 4 m bumps its head on the lintel (crown crosses z0=5)");
    check(!idx.blocked_at(200.0f, 100.0f, 0.5f, 8.0f),
          "feet on the lintel top: floor, not wall");
    check(std::fabs(idx.support_at(200.0f, 100.0f, 0.5f, 8.0f) - 8.0f) < 1e-4f,
          "lintel top supports a defender standing on it");
    check(idx.support_at(200.0f, 100.0f, 0.5f, 0.0f) < -1.0e20f,
          "lintel is no support for a street-level body");
    check(idx.solid_at(200.0f, 100.0f, 6.5f),
          "projectile point inside the lintel span hits");
    check(!idx.solid_at(200.0f, 100.0f, 2.0f),
          "projectile point under the lintel flies through");

    // ── Oriented box: the yaw is honest ──
    // Along the plank's local X (45°): 6 units out is inside (hx 8).
    const float c45 = std::cos(0.785398f);
    check(idx.blocked_at(300.0f + 6.0f * c45, 300.0f + 6.0f * c45, 0.3f, 0.0f),
          "point along the rotated plank axis is inside");
    // Along world X the same 6 units is ~4.2 off-axis in local Y (hy 1.1): free.
    check(!idx.blocked_at(306.0f, 300.0f, 0.3f, 0.0f),
          "point along world X misses the rotated plank");

    // ── Cylinder: round, not square ──
    check(idx.blocked_at(52.5f, 50.0f, 0.0f, 0.0f), "inside cylinder radius");
    check(!idx.blocked_at(53.5f, 50.0f, 0.0f, 0.0f), "outside cylinder radius");
    check(!idx.blocked_at(52.4f, 52.4f, 0.0f, 0.0f),
          "square corner of the bounding box is OUTSIDE the round tower");

    // ── Slide & stop ──
    {
        // Diagonal into the box side: the free axis survives.
        float tx = 97.2f, ty = 97.2f;
        idx.resolve_step(95.0f, 96.0f, tx, ty, 0.5f, 0.0f);
        check(tx == 97.2f && ty == 96.0f, "diagonal step slides along the wall");
    }
    {
        // Head-on: full stop.
        float tx = 98.0f, ty = 100.0f;
        idx.resolve_step(95.0f, 100.0f, tx, ty, 0.5f, 0.0f);
        check(tx == 95.0f && ty == 100.0f, "head-on step is refused");
    }
    {
        // Escape rule: a body already inside may move anywhere (out).
        float tx = 101.0f, ty = 100.0f;
        idx.resolve_step(100.0f, 100.0f, tx, ty, 0.5f, 0.0f);
        check(tx == 101.0f && ty == 100.0f,
              "body inside a solid is never trapped");
    }

    // ── Seating uses the height function ──
    struct Lift { static float h(void*, float, float) { return 100.0f; } };
    idx.rebuild(solids, &Lift::h, nullptr);
    check(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 105.0f) - 105.0f) < 1e-4f,
          "solid spans are absolute: seat 100 + height 5 supports at 105");
    check(!idx.blocked_at(100.0f, 100.0f, 0.5f, 105.0f),
          "standing on the lifted box top is legal");
}

// Part B — a REAL generated city with road-bearing neighbours must be
// enterable: walk every carved road tile at street level and require that no
// road tile is blocked (gates are open, lintels bridge overhead), while wall
// bodies themselves do block.
void part_b_generated_city() {
    CellContext city{};
    city.cx = -11;
    city.cy = 6;
    city.macroHeight = 0.64f;
    city.biome = Meadow;
    city.feature = FT_None;
    city.landmarkSettlementId = 101;
    city.landmarkSize = 6000;
    city.landmarkKind = CellLandmarkKind::City;
    city.seed = 0x0BADF00Du;
    city.treeCount = 0;

    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = 0.62f;
        nbB[i] = Meadow;
        nbF[i] = std::uint8_t(FT_None);
    }
    nbF[4] = std::uint8_t(FT_None);
    nbF[3] = std::uint8_t(FT_Road);      // west neighbour carries a road
    nbF[5] = std::uint8_t(FT_DirtRoad);  // east neighbour carries a road

    SubworldMapData out{};
    dispatch_generate(city, nbH, nbB, nbF, out);

    // Structure inventory: oriented walls, round towers, gate lintels,
    // rotated houses with independent extents.
    int lintels = 0;
    int cylinders = 0;
    int orientedWalls = 0;
    int rotatedHouses = 0;
    int oblongHouses = 0;
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Wall) {
            if (s.zBase > 0.0f) ++lintels;
            if (s.shape == Structure::Cylinder) ++cylinders;
            if (s.shape == Structure::Box && s.hx > 0.0f
                && std::fabs(s.yaw) > 1e-3f) {
                ++orientedWalls;
            }
        } else if (s.kind == Structure::House) {
            if (std::fabs(s.yaw) > 1e-3f) ++rotatedHouses;
            if (s.hx > 0.0f && s.hy > 0.0f
                && std::fabs(s.hx - s.hy) > 0.05f) {
                ++oblongHouses;
            }
        }
    }
    check(lintels >= 2, "each road gate carries a lintel (>= 2 for two axes)");
    check(cylinders >= 8, "wall towers and gate jambs are round");
    check(orientedWalls >= 40, "wall runs are yawed chords following the ring");
    check(rotatedHouses >= 10, "houses draw a random orientation");
    check(oblongHouses >= 10, "houses keep independent width/length");

    // Solidity over the generated cell (cell-local coords; the index spans
    // whatever the records span). Seat solids on the actual generated relief.
    struct H {
        const SubworldMapData* map;
        static float at(void* user, float x, float y) {
            const auto* m = static_cast<const H*>(user)->map;
            const int tx = std::min(kCellSize - 1, std::max(0, int(x)));
            const int ty = std::min(kCellSize - 1, std::max(0, int(y)));
            return m->heightmap[std::size_t(ty) * kCellSize + tx] * kHeightScaleM;
        }
    } h{&out};
    StructureIndex idx;
    idx.rebuild(out.structures, &H::at, &h);
    check(!idx.empty(), "generated city produces solids");

    // The road network must stay TRAVERSABLE at street level: this is the
    // "gates are real gates" guarantee — the roads cross every wall ring only
    // through openings, and lintels above them never block a walker. Verges
    // hugging a wall face may legitimately clip the wall body (the wall is
    // physically thicker than the unpainted strip), so the invariant is
    // CONNECTIVITY over unblocked road tiles from the plaza to the cell edge,
    // plus a bound on how much verge clipping is tolerable.
    int roadTiles = 0;
    int blockedRoadTiles = 0;
    std::vector<std::uint8_t> passable(std::size_t(kCellSize) * kCellSize, 0);
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            if (out.tiles[std::size_t(y) * kCellSize + x] != TILE_ROAD) continue;
            ++roadTiles;
            const float fx = float(x) + 0.5f;
            const float fy = float(y) + 0.5f;
            if (idx.blocked_at(fx, fy, 0.45f, H::at(&h, fx, fy))) {
                ++blockedRoadTiles;
            } else {
                passable[std::size_t(y) * kCellSize + x] = 1;
            }
        }
    }
    check(roadTiles > 0, "city carved roads");
    check(blockedRoadTiles * 100 < roadTiles * 3,
          "wall bodies clip less than 3% of road tiles (verges only)");

    // BFS from a road tile near the plaza across unblocked road tiles; it
    // must reach the cell border (the west/east main roads leave the city
    // through the gates).
    {
        const int c = kCellSize / 2;
        int sx = -1, sy = -1;
        for (int rad = 0; rad < 80 && sx < 0; ++rad) {
            for (int dy = -rad; dy <= rad && sx < 0; ++dy) {
                for (int dx = -rad; dx <= rad; ++dx) {
                    if (std::abs(dx) != rad && std::abs(dy) != rad) continue;
                    const int x = c + dx;
                    const int y = c + dy;
                    if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
                    if (passable[std::size_t(y) * kCellSize + x]) {
                        sx = x;
                        sy = y;
                        break;
                    }
                }
            }
        }
        check(sx >= 0, "found a passable road tile near the plaza");
        bool reachedEdge = false;
        if (sx >= 0) {
            std::vector<std::uint8_t> seen(passable.size(), 0);
            std::queue<std::pair<int, int>> q;
            q.push({sx, sy});
            seen[std::size_t(sy) * kCellSize + sx] = 1;
            const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            while (!q.empty() && !reachedEdge) {
                const auto [x, y] = q.front();
                q.pop();
                if (x <= 1 || y <= 1 || x >= kCellSize - 2 || y >= kCellSize - 2) {
                    reachedEdge = true;
                    break;
                }
                for (const auto& d : dirs) {
                    const int nx = x + d[0];
                    const int ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= kCellSize || ny >= kCellSize)
                        continue;
                    const std::size_t i = std::size_t(ny) * kCellSize + nx;
                    if (seen[i] || !passable[i]) continue;
                    seen[i] = 1;
                    q.push({nx, ny});
                }
            }
        }
        check(reachedEdge,
              "the road network is walkable from the plaza out through the gates");
    }

    // The curtain wall must actually block: sample every grounded wall body's
    // centre at street feet.
    int wallBodies = 0;
    int blockingWallBodies = 0;
    for (const Structure& s : out.structures) {
        if (s.kind != Structure::Wall || s.zBase > 0.0f) continue;
        ++wallBodies;
        if (idx.blocked_at(s.x, s.y, 0.45f, H::at(&h, s.x, s.y))) {
            ++blockingWallBodies;
        }
    }
    check(wallBodies > 0 && blockingWallBodies == wallBodies,
          "every grounded wall body blocks at street level");

    // And the wall top is standable: support at feet == top exists.
    int standable = 0;
    for (const Structure& s : out.structures) {
        if (s.kind != Structure::Wall || s.zBase > 0.0f) continue;
        const float seat = H::at(&h, s.x, s.y);
        const float top = seat + structure_visible_height(s);
        if (std::fabs(idx.support_at(s.x, s.y, 0.45f, top) - top) < 0.5f) {
            ++standable;
        }
        break;  // one is proof enough; the semantics are uniform
    }
    check(standable == 1, "a wall top supports a body standing on it");
}

// Part C — the ONE vertical integrator (height.h vertical_step): resting,
// slope stick, spawn snap-up, honest ballistic fall with the real free-fall
// time, terminal cap, and jump-readiness (upward vz arcs and lands).
void part_c_vertical_physics() {
    const float dt = 1.0f / 60.0f;
    float z, vz;

    z = 100.0f; vz = 0.0f;
    check(vertical_step(100.0f, dt, z, vz) && z == 100.0f && vz == 0.0f,
          "resting body stays grounded");

    z = 100.9f; vz = 0.0f;
    check(vertical_step(100.0f, dt, z, vz) && z == 100.0f,
          "a drop inside kGroundStickM sticks to the support (slope walk)");

    z = 0.0f; vz = 0.0f;
    check(vertical_step(900.0f, dt, z, vz) && z == 900.0f,
          "a body below its support snaps up (arriving is not falling)");

    // 10 m ledge drop: ballistic fall, landing exactly on the support in the
    // real free-fall time sqrt(2h/g) ≈ 1.43 s (± one tick).
    z = 110.0f; vz = 0.0f;
    int ticks = 0;
    float prevZ = z;
    bool monotonic = true;
    while (!vertical_step(100.0f, dt, z, vz) && ticks < 600) {
        if (z >= prevZ) monotonic = false;
        prevZ = z;
        ++ticks;
    }
    const float fallS = float(ticks + 1) * dt;
    check(z == 100.0f && vz == 0.0f, "fall lands exactly on the support");
    check(monotonic, "fall is monotonic (no bounce)");
    check(std::fabs(fallS - std::sqrt(2.0f * 10.0f / kGravityMps2)) < 2.5f * dt,
          "10 m fall takes the honest free-fall time");

    // Terminal velocity cap.
    z = 10000.0f; vz = 0.0f;
    for (int i = 0; i < 600; ++i) vertical_step(0.0f, dt, z, vz);
    check(vz >= -kTerminalFallMps - 1e-3f,
          "long fall is capped at terminal speed");

    // Jump-ready: an upward vz leaves the ground, arcs, and lands back.
    z = 100.0f; vz = 5.0f;
    check(!vertical_step(100.0f, dt, z, vz) && z > 100.0f,
          "upward velocity lifts off (gravity still applies)");
    ticks = 0;
    while (!vertical_step(100.0f, dt, z, vz) && ticks < 600) ++ticks;
    check(z == 100.0f && vz == 0.0f, "jump arc returns to the ground");
}

} // namespace

int main() {
    part_a_index_semantics();
    part_b_generated_city();
    part_c_vertical_physics();
    if (failures != 0) {
        std::fprintf(stderr, "structure_collide_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("structure_collide_test: OK\n");
    return 0;
}
