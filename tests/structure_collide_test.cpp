// Body-vs-structure collision & support (sub/collide.h) — the solidity
// authority the subworld movers share. Part A drives the index with
// hand-placed solids on a flat plane and pins the core semantics: blocking,
// support (standing ON things), z-layering (lintels: under, on top, head
// bump), oriented boxes, cylinders, the slide and the escape rule. Part B
// generates a REAL city cell (sub/gens/dispatch.cpp) with road neighbours and
// proves the functional promises end-to-end: the carved road enters the city
// through the gates without ever being blocked (lintels bridge overhead), and
// the wall itself blocks at street level.
#include "check.h"

#include "sub/city_layout.h"
#include "sub/collide.h"
#include "sub/gens/kit/outline.h"   // Outline::kBearings — the ring's own resolution
#include "sub/gens/dispatch.h"
#include "sub/height.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <queue>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

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
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f), "empty index blocks nothing");
    CHECK(idx.support_at(100.0f, 100.0f, 0.5f, 10.0f) < -1.0e20f,
          "empty index offers no support");
    {
        float tx = 50.0f, ty = 50.0f;
        idx.resolve_step(40.0f, 40.0f, tx, ty, 0.5f, 0.0f);
        CHECK(tx == 50.0f && ty == 50.0f, "empty index resolve_step is a no-op");
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
    CHECK(idx.solid_count() == 4, "index keeps all four solids");

    // ── Blocking vs standing on the plain box ──
    CHECK(idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f),
          "street-level body inside the box footprint is blocked");
    CHECK(!idx.blocked_at(110.0f, 100.0f, 0.5f, 0.0f),
          "body beside the box is free");
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 5.0f),
          "feet ON the box top: it is a floor, not a wall");
    CHECK(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 5.0f) - 5.0f) < 1e-4f,
          "box top supports a body standing on it");
    CHECK(idx.support_at(100.0f, 100.0f, 0.5f, 0.0f) < -1.0e20f,
          "box top far above the feet is NOT a support (no teleport up)");
    // Radius participates (Minkowski): centre 3.0+r inside, beyond it free.
    CHECK(idx.blocked_at(103.8f, 100.0f, 1.0f, 0.0f),
          "fat body clips the box edge");
    CHECK(!idx.blocked_at(104.3f, 100.0f, 1.0f, 0.0f),
          "fat body past the expanded edge is free");

    // ── Lintel layering: under / on top / head bump ──
    CHECK(!idx.blocked_at(200.0f, 100.0f, 0.5f, 0.0f),
          "street-level body walks UNDER the lintel");
    CHECK(idx.blocked_at(200.0f, 100.0f, 0.5f, 4.0f),
          "flyer at 4 m bumps its head on the lintel (crown crosses z0=5)");
    CHECK(!idx.blocked_at(200.0f, 100.0f, 0.5f, 8.0f),
          "feet on the lintel top: floor, not wall");
    CHECK(std::fabs(idx.support_at(200.0f, 100.0f, 0.5f, 8.0f) - 8.0f) < 1e-4f,
          "lintel top supports a defender standing on it");
    CHECK(idx.support_at(200.0f, 100.0f, 0.5f, 0.0f) < -1.0e20f,
          "lintel is no support for a street-level body");
    CHECK(idx.solid_at(200.0f, 100.0f, 6.5f),
          "projectile point inside the lintel span hits");
    CHECK(!idx.solid_at(200.0f, 100.0f, 2.0f),
          "projectile point under the lintel flies through");

    // ── Oriented box: the yaw is honest ──
    // Along the plank's local X (45°): 6 units out is inside (hx 8).
    const float c45 = std::cos(0.785398f);
    CHECK(idx.blocked_at(300.0f + 6.0f * c45, 300.0f + 6.0f * c45, 0.3f, 0.0f),
          "point along the rotated plank axis is inside");
    // Along world X the same 6 units is ~4.2 off-axis in local Y (hy 1.1): free.
    CHECK(!idx.blocked_at(306.0f, 300.0f, 0.3f, 0.0f),
          "point along world X misses the rotated plank");

    // ── Cylinder: round, not square ──
    CHECK(idx.blocked_at(52.5f, 50.0f, 0.0f, 0.0f), "inside cylinder radius");
    CHECK(!idx.blocked_at(53.5f, 50.0f, 0.0f, 0.0f), "outside cylinder radius");
    CHECK(!idx.blocked_at(52.4f, 52.4f, 0.0f, 0.0f),
          "square corner of the bounding box is OUTSIDE the round tower");

    // ── Slide & stop ──
    {
        // Diagonal into the box side: the free axis survives.
        float tx = 97.2f, ty = 97.2f;
        idx.resolve_step(95.0f, 96.0f, tx, ty, 0.5f, 0.0f);
        CHECK(tx == 97.2f && ty == 96.0f, "diagonal step slides along the wall");
    }
    {
        // Head-on: full stop.
        float tx = 98.0f, ty = 100.0f;
        idx.resolve_step(95.0f, 100.0f, tx, ty, 0.5f, 0.0f);
        CHECK(tx == 95.0f && ty == 100.0f, "head-on step is refused");
    }
    {
        // Escape rule: a body already inside may move anywhere (out).
        float tx = 101.0f, ty = 100.0f;
        idx.resolve_step(100.0f, 100.0f, tx, ty, 0.5f, 0.0f);
        CHECK(tx == 101.0f && ty == 100.0f,
              "body inside a solid is never trapped");
    }

    // ── Seating uses the height function ──
    struct Lift { static float h(void*, float, float) { return 100.0f; } };
    idx.rebuild(solids, &Lift::h, nullptr);
    CHECK(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 105.0f) - 105.0f) < 1e-4f,
          "solid spans are absolute: seat 100 + height 5 supports at 105");
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 105.0f),
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
    city.landmark.id = 101;
    city.landmark.size = 6000;
    city.landmark.kind = LandmarkType::City;
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
    dispatch_generate(city, nbH, nbB, /*nbBiome5*/nullptr, nbF, out);

    // ── Structure inventory ───────────────────────────────────────────────
    // THE CURTAIN'S OWN DIMENSION, asked of the public ring model the
    // generator itself reads (sub/city_layout.h). A TOWER reads as a tower
    // only when it stands proud of the curtain on BOTH faces by the curtain's
    // own thickness (2 × halfThickness) — a round body centred on the ring
    // protrudes `radius − halfThickness` per side, so that law is
    // radius >= 3 × halfThickness. Anything rounder but thinner is a gate
    // JAMB: the stub that finishes a curtain end.
    const float halfThick = kSettlementWallRing.halfThickness;
    const float kTowerR   = halfThick * 3.0f;

    int lintels = 0;
    int towers = 0;
    int jambs = 0;
    int orientedWalls = 0;
    int houses = 0;
    int rotatedHouses = 0;
    int oblongHouses = 0;
    int thinMasonry = 0;        // round bodies thinner than the curtain itself
    float maxChordHx = 0.0f;    // longest curtain piece the generator emitted
    float openingLen = 0.0f;    // ring length that is gateway, not curtain
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Wall) {
            if (s.zBase > 0.0f) {
                ++lintels;
                // Over-stated on purpose: a lintel overhangs its opening on
                // both sides, so 2·hx is MORE than the gap it bridges, and
                // over-subtracting keeps the chord floor below conservative.
                openingLen += 2.0f * s.hx;
            }
            if (s.shape == Structure::Cylinder) {
                if (s.radius >= kTowerR - 1e-4f) ++towers; else ++jambs;
                if (s.radius < halfThick) ++thinMasonry;
            }
            if (s.shape == Structure::Box && s.hx > 0.0f
                && std::fabs(s.yaw) > 1e-3f) {
                ++orientedWalls;
                if (s.zBase == 0.0f) maxChordHx = std::max(maxChordHx, s.hx);
            }
        } else if (s.kind == Structure::House) {
            ++houses;
            if (std::fabs(s.yaw) > 1e-3f) ++rotatedHouses;
            if (s.hx > 0.0f && s.hy > 0.0f
                && std::fabs(s.hx - s.hy) > 0.05f) {
                ++oblongHouses;
            }
        }
    }
    // ── The inventory's laws, every number a function of the generator's own
    // ── input: the feature's POPULATION and the public ring model.
    //
    // Every literal that used to stand here (`cylinders >= 8`,
    // `orientedWalls >= 40`, `rotatedHouses >= 10`, `oblongHouses >= 10`) was
    // calibrated on one run of one seed and derived from nothing (M-132). A
    // city's only inputs are its population, its 3×3 surroundings and its seed
    // — so a claim about what it built has to come from those.
    CHECK(lintels >= 2, "each road gate carries a lintel (>= 2 for two axes)");

    // THE RING'S LENGTH, from the population alone. A town grows until it holds
    // the ground its hearths need (city_target_area, itself population ÷ hearth
    // law × ground-per-house). The isoperimetric inequality gives the SHORTEST
    // closed curve that can enclose that area — 2√(πA), with equality only for
    // a perfect circle — so an organically grown ring is always longer than
    // this. Measured for pop 6000: 1838 tiles of chord against a floor of 1493.
    constexpr float kPi = 3.14159265f;
    const int population = int(city.landmark.size);
    const float ringFloor =
        2.0f * std::sqrt(kPi * city_target_area(population));

    // …and the CHORD COUNT follows by division: to cover a length with pieces
    // none longer than the longest piece present, you need at least
    // length/longest of them. Both terms are measured from this very run, so no
    // retune of the piece length can make the claim stale.
    CHECK_OR_RETURN(maxChordHx > 0.0f,
                    "the ring is built from ORIENTED chords at all — a town "
                    "walled by four axis-aligned slabs is not a town");
    const int chordFloor = int((ringFloor - openingLen) / (2.0f * maxChordHx));
    if (orientedWalls < chordFloor) {
        std::fprintf(stderr,
            "  pop=%d area=%.0f ringFloor=%.0f openings=%.0f maxHx=%.2f "
            "need>=%d got=%d\n", population, double(city_target_area(population)),
            double(ringFloor), double(openingLen), double(maxChordHx),
            chordFloor, orientedWalls);
    }
    CHECK(orientedWalls >= chordFloor,
          "the curtain's yawed chords are enough to CLOSE a ring around the "
          "ground this population needs — the count follows the town's size, "
          "it is not a number");

    // ROUND STONE BY ITS ROLE. The two round bodies of a curtain are the tower
    // and the gate jamb, and they are told apart by the law above, not by a
    // hand-picked radius.
    CHECK(thinMasonry == 0,
          "no round body on the ring is thinner than the curtain it belongs "
          "to — masonry, never a pebble");
    // A curtain with no tower does not read as fortified. The floor is the
    // ring's own shape: it carries a non-zero THIRD harmonic
    // (kSettlementWallRing.harmonic3Amp), so it has three lobes and therefore
    // at least three salients for a tower to flank. The count itself is
    // deliberately parameter-free in the generator (towers stand where the ring
    // turns), so a higher floor would be an invented number — measured 23.
    static_assert(kSettlementWallRing.harmonic3Amp > 0.0f,
                  "the ring is lobed — that is what gives a tower its corner");
    CHECK(towers >= 3,
          "the ring is TOWERED at its salients — at least one per lobe of its "
          "lowest harmonic");
    // THE OTHER HALF OF THAT LAW — "not every other node, which is a lattice"
    // — IS DELIBERATELY NOT ASSERTED HERE, and the reason is worth writing
    // down: a city has more than one ring (the upper quarter carries its own
    // curtain with its own towers), so this count is a SUM over rings and
    // cannot be compared against one ring's Outline::kBearings. Measured 23 for
    // two rings — a ceiling of 32 would pass today and red the day a third
    // ring lands, which is a witness guarding an accident rather than a law.
    // Attributing a tower to a ring needs the layout the generator holds, and
    // copying that here would be a second implementation (§8 п.5).
    // A jamb finishes a curtain end, so an arch is jambed — unless that spot is
    // itself roadway, where the generator rightly skips it (measured: 7 jambs
    // for 4 arches).
    CHECK(jambs >= lintels,
          "every arch is finished by a jamb, bar the ones whose footing is the "
          "road itself");

    // HOUSES: stated as relations, so no retune of a town's size can date them.
    CHECK_OR_RETURN(houses > 0, "the city built houses to assert about");
    CHECK(rotatedHouses > houses - rotatedHouses,
          "a random orientation is the RULE of a house, not the exception — "
          "the axis-aligned stamp is dead");
    CHECK(oblongHouses > houses - oblongHouses,
          "so is an independent width and length: most houses are oblong, not "
          "square");

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
    CHECK(!idx.empty(), "generated city produces solids");

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
    CHECK(roadTiles > 0, "city carved roads");
    CHECK(blockedRoadTiles * 100 < roadTiles * 3,
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
        CHECK(sx >= 0, "found a passable road tile near the plaza");
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
        CHECK(reachedEdge,
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
    CHECK(wallBodies > 0 && blockingWallBodies == wallBodies,
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
    CHECK(standable == 1, "a wall top supports a body standing on it");
}

// Part C — the ONE vertical integrator (height.h vertical_step): resting,
// slope stick, spawn snap-up, honest ballistic fall with the real free-fall
// time, terminal cap, and jump-readiness (upward vz arcs and lands).
void part_c_vertical_physics() {
    const float dt = 1.0f / 60.0f;
    float z, vz;

    z = 100.0f; vz = 0.0f;
    CHECK(vertical_step(100.0f, dt, z, vz, true) && z == 100.0f && vz == 0.0f,
          "resting body stays grounded");

    z = 100.9f; vz = 0.0f;
    CHECK(vertical_step(100.0f, dt, z, vz, true) && z == 100.0f,
          "a drop inside kGroundStickM sticks to the support (slope walk)");

    z = 0.0f; vz = 0.0f;
    CHECK(vertical_step(900.0f, dt, z, vz, true) && z == 900.0f,
          "a body below its support snaps up (arriving is not falling)");

    // 10 m ledge drop: ballistic fall, landing exactly on the support in the
    // real free-fall time sqrt(2h/g) ≈ 1.43 s (± one tick).
    z = 110.0f; vz = 0.0f;
    int ticks = 0;
    float prevZ = z;
    bool monotonic = true;
    while (!vertical_step(100.0f, dt, z, vz, false) && ticks < 600) {
        if (z >= prevZ) monotonic = false;
        prevZ = z;
        ++ticks;
    }
    const float fallS = float(ticks + 1) * dt;
    CHECK(z == 100.0f && vz == 0.0f, "fall lands exactly on the support");
    CHECK(monotonic, "fall is monotonic (no bounce)");
    CHECK(std::fabs(fallS - std::sqrt(2.0f * 10.0f / kGravityMps2)) < 2.5f * dt,
          "10 m fall takes the honest free-fall time");

    // Terminal velocity cap.
    z = 10000.0f; vz = 0.0f;
    for (int i = 0; i < 600; ++i) vertical_step(0.0f, dt, z, vz, false);
    CHECK(vz >= -kTerminalFallMps - 1e-3f,
          "long fall is capped at terminal speed");

    // Jump-ready: an upward vz leaves the ground, arcs, and lands back.
    // Driven at the SHIPPING tick (1/64 s, not this part's 1/60) because the
    // bug this guards against was arithmetic: g = 8, v = 4 and dt = 1/64 are
    // all po2, so vz lands on EXACTLY 0.0f at the apex — and the old
    // integrator read "vz == 0" as "resting", stuck the body to the ground
    // 1 m below (inside kGroundStickM) and swallowed the entire descent. The
    // arc looked right at the apex, which is why the old check passed: it
    // measured the top of the jump and never the way down.
    const float jdt = 1.0f / 64.0f;
    z = 100.0f; vz = kJumpSpeedMps;
    CHECK(!vertical_step(100.0f, jdt, z, vz, false) && z > 100.0f,
          "upward velocity lifts off (gravity still applies)");
    ticks = 0;
    float apex = z;
    int apexTick = 0;
    while (!vertical_step(100.0f, jdt, z, vz, false) && ticks < 600) {
        ++ticks;
        if (z > apex) { apex = z; apexTick = ticks; }
    }
    CHECK(z == 100.0f && vz == 0.0f, "jump arc returns to the ground");
    // Apex ≈ v²/2g (one-tick integration slack), and safely under the wall.
    CHECK(std::fabs((apex - 100.0f)
                    - kJumpSpeedMps * kJumpSpeedMps / (2.0f * kGravityMps2))
              < 0.15f,
          "jump apex matches v^2/2g");
    // THE regression: a jump has two halves. Rise and fall are symmetric, so
    // the descent must take as many ticks as the climb (± one tick of
    // integration slack). The apex teleport made this number 0.
    CHECK(ticks - apexTick >= apexTick - 1,
          "the jump FALLS back down instead of snapping (no apex teleport)");
    CHECK(apexTick > 8, "the climb lasts a real arc, not a couple of ticks");
    // A rising body inside stick range is NOT resting: the same snap, asked
    // for directly. This is the one-liner the bug was made of.
    z = 100.9f; vz = 1.0f;
    CHECK(!vertical_step(100.0f, jdt, z, vz, false) && z > 100.9f,
          "a body climbing through stick range keeps climbing");

    // Fall damage: kinetic energy above the safe speed, radius as mass.
    CHECK(fall_damage(kFallSafeSpeedMps - 1.0f, 1.5f) == 0.0f,
          "landing under the safe speed is free");
    const float v10 = std::sqrt(2.0f * kGravityMps2 * 10.0f); // 10 m drop
    const float expect = kFallDamagePerEnergy * 0.5f * 1.5f
        * (v10 * v10 - kFallSafeSpeedMps * kFallSafeSpeedMps);
    CHECK(std::fabs(fall_damage(v10, 1.5f) - expect) < 1e-3f,
          "10 m fall damage = excess kinetic energy");
    CHECK(fall_damage(v10, 3.0f) > fall_damage(v10, 0.5f),
          "heavier bodies fall harder");
    // A jump can never hurt: its landing speed is its take-off speed.
    CHECK(fall_damage(kJumpSpeedMps, 3.0f) == 0.0f,
          "a jump landing is always under the safe speed");
}

// ── PART D: the ground under the feet is the SUPPORT's, not the column's ──
// The owner's law (2026-08-30), found on a bridge: a tile is one value per
// column of the map and cannot say "water below, masonry three metres up", so
// a body crossing a deck was priced as wading the river it was walking over.
// What CARRIES a body lays the ground it walks (map_data.h walkTile).
void part_d_support_lays_the_ground() {
    StructureIndex idx;
    std::vector<Structure> solids;
    // A deck 3 m up over water, and a lantern beside it that lays nothing.
    Structure deck = make_box(100.0f, 100.0f, 8.0f, 2.0f, 0.0f,
                              /*height*/1.0f, /*zBase*/3.0f, Structure::Bridge);
    solids.push_back(deck);
    Structure lamp = make_box(140.0f, 100.0f, 0.4f, 0.4f, 0.0f,
                              /*height*/2.0f, /*zBase*/0.0f, Structure::Lantern);
    solids.push_back(lamp);
    idx.rebuild(solids, &flat_height, nullptr);

    // ON the deck (feet at its top): the river underneath is not what you
    // walk on — the deck is, and a deck lays road.
    CHECK(idx.walk_tile_at(100.0f, 100.0f, 0.5f, 4.0f,
                           std::uint8_t(TILE_WATER)) == std::uint8_t(TILE_ROAD),
          "a body on the deck walks on road, not on the river below it");
    // UNDER the deck, wading: the deck carries somebody else, not you.
    CHECK(idx.walk_tile_at(100.0f, 100.0f, 0.5f, 0.0f,
                           std::uint8_t(TILE_WATER)) == std::uint8_t(TILE_WATER),
          "a body under the deck is still in the water");
    // BESIDE it: no solid, the terrain answers.
    CHECK(idx.walk_tile_at(160.0f, 100.0f, 0.5f, 0.0f,
                           std::uint8_t(TILE_GRASS)) == std::uint8_t(TILE_GRASS),
          "away from solids the terrain's own tile stands");
    // A solid that lays no ground of its own is TRANSPARENT to the law: it
    // must not overwrite the road under it with "nothing" (the reason
    // kWalkTileTransparent is a sentinel and not just TILE_EMPTY).
    CHECK(idx.walk_tile_at(140.0f, 100.0f, 0.5f, 2.0f,
                           std::uint8_t(TILE_ROAD)) == std::uint8_t(TILE_ROAD),
          "standing on a lantern does not turn the road below into nothing");
    // And the law is worth something only if the two answers DIFFER: the
    // negative control of the whole part — deck ground beats column ground.
    CHECK(kTileMovementSpeed[TILE_ROAD] > kTileMovementSpeed[TILE_WATER],
          "the law is only visible because road and water differ in speed");
}

} // namespace

int main() {
    part_a_index_semantics();
    part_b_generated_city();
    part_c_vertical_physics();
    part_d_support_lays_the_ground();
    return sm::test::report("structure_collide_test");
}
