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
#include "sub/body.h"   // kHumanHeightM — единственный рост человека
#include "sub/gens/kit/outline.h"   // Outline::kBearings — the ring's own resolution
#include "sub/gens/dispatch.h"
#include "sub/height.h"
#include "sub/movement.h"           // the OTHER mover — part E drives both
#include "bus/anketa.h"             // quickness_pct — the M-389 pace ceiling
#include "tables/attributes.h"      // skill_mult_of(Athletics, …)
#include "tables/movement_cost.h"   // kSubworldWalkTilesPerSecond
#include "tables/npc.h"             // kNpcBodyRadiusDefault

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
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

// РОСТ ЧЕЛОВЕКА — ОДИН НА МИР (`kHumanHeightM@src/sub/body.h`). Прибор
// называет его сам, потому что дефолта у дверей больше нет: «забыл
// передать высоту» стало несобираемым (M-401).
constexpr float kManH = sm::sub::kHumanHeightM;
// ДВА КРАЯ ШКАЛЫ РОСТА — ОДНО ЧИСЛО НА ПРИБОР. Части F и E судят ОДНУ и ту же
// пару тел двумя дверями (точечной и мувером), поэтому числа обязаны быть
// общими: разъехавшись, они позволили бы одной части «проходить» там, где
// другая «стоит», и расхождение читалось бы как дефект мира.
constexpr float kHareH = 0.5f;     // заяц: макушка ниже низа перекладины
constexpr float kDragonH = 6.0f;   // дракон: своя колонка строки, 6 м

// ВЫСОТА ТЕЛА РЕШАЕТ, КТО ПРОХОДИТ ПОД ПОДНЯТЫМ СОЛИДОМ (M-401, вердикт
// владельца 2026-11-04: «единая система игрок = нпц … да все одинаково
// сталкиваются»).
//
// Один солид, одно место, ДВА роста — два ответа. До наряда дверь брала
// гуманоидный дефолт 1.7 м для ВСЕХ, поэтому заяц и дракон судились одной
// макушкой: под перекладиной проходили ОБА. Теперь высоту называет звонящий,
// и геометрия отвечает честно: низ солида ниже макушки — стена; выше — проход.
void part_f_height_decides_who_passes_under() {
    StructureIndex idx;
    std::vector<Structure> solids;
    // Перекладина ворот: низ на двух метрах, толщиной метр — span 2.0..3.0.
    solids.push_back(make_box(100.0f, 100.0f, 2.0f, 2.0f, 0.0f,
                              /*height*/1.0f, /*zBase*/2.0f));
    idx.rebuild(solids, &flat_height, nullptr);

    constexpr float kR = 0.55f;

    const bool hareBlocked =
        idx.blocked_at(100.0f, 100.0f, kR, 0.0f, kHareH);
    const bool manBlocked =
        idx.blocked_at(100.0f, 100.0f, kR, 0.0f, kManH);
    const bool dragonBlocked =
        idx.blocked_at(100.0f, 100.0f, kR, 0.0f, kDragonH);

    CHECK(!hareBlocked,
          "заяц проходит ПОД перекладиной: его макушка ниже низа солида, и "
          "это геометрия, а не поблажка роду");
    CHECK(dragonBlocked,
          "дракон той же перекладиной ОСТАНОВЛЕН: его макушка выше её низа");
    CHECK(hareBlocked != dragonBlocked,
          "ОДИН солид судит два тела ПО-РАЗНОМУ — ровно то, чего гуманоидный "
          "дефолт сделать не мог");
    // ЯКОРЬ на человека: он тоже проходит (1.8 < 2.0), значит проверка выше
    // не держится на крайностях шкалы и поймает сдвиг низа перекладины.
    CHECK(!manBlocked,
          "человек проходит под перекладиной на двух метрах — якорь середины");
    std::fprintf(stderr,
                 "[m401] перекладина z 2.0..3.0: заяц %.1f м — %s · человек "
                 "%.1f м — %s · дракон %.1f м — %s\n",
                 double(kHareH), hareBlocked ? "стена" : "проход",
                 double(kManH), manBlocked ? "стена" : "проход",
                 double(kDragonH), dragonBlocked ? "стена" : "проход");
    std::fflush(stderr);
}

void part_a_index_semantics() {
    StructureIndex idx;

    // Empty index: transparent world, resolve_step is a no-op.
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f, kManH), "empty index blocks nothing");
    CHECK(idx.support_at(100.0f, 100.0f, 0.5f, 10.0f) < -1.0e20f,
          "empty index offers no support");
    {
        float tx = 50.0f, ty = 50.0f;
        idx.resolve_step(40.0f, 40.0f, tx, ty, 0.5f, 0.0f, kManH);
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
    CHECK(idx.blocked_at(100.0f, 100.0f, 0.5f, 0.0f, kManH),
          "street-level body inside the box footprint is blocked");
    CHECK(!idx.blocked_at(110.0f, 100.0f, 0.5f, 0.0f, kManH),
          "body beside the box is free");
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 5.0f, kManH),
          "feet ON the box top: it is a floor, not a wall");
    CHECK(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 5.0f) - 5.0f) < 1e-4f,
          "box top supports a body standing on it");
    CHECK(idx.support_at(100.0f, 100.0f, 0.5f, 0.0f) < -1.0e20f,
          "box top far above the feet is NOT a support (no teleport up)");
    // Radius participates (Minkowski): centre 3.0+r inside, beyond it free.
    CHECK(idx.blocked_at(103.8f, 100.0f, 1.0f, 0.0f, kManH),
          "fat body clips the box edge");
    CHECK(!idx.blocked_at(104.3f, 100.0f, 1.0f, 0.0f, kManH),
          "fat body past the expanded edge is free");

    // ── Lintel layering: under / on top / head bump ──
    CHECK(!idx.blocked_at(200.0f, 100.0f, 0.5f, 0.0f, kManH),
          "street-level body walks UNDER the lintel");
    CHECK(idx.blocked_at(200.0f, 100.0f, 0.5f, 4.0f, kManH),
          "flyer at 4 m bumps its head on the lintel (crown crosses z0=5)");
    CHECK(!idx.blocked_at(200.0f, 100.0f, 0.5f, 8.0f, kManH),
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
    CHECK(idx.blocked_at(300.0f + 6.0f * c45, 300.0f + 6.0f * c45, 0.3f, 0.0f, kManH),
          "point along the rotated plank axis is inside");
    // Along world X the same 6 units is ~4.2 off-axis in local Y (hy 1.1): free.
    CHECK(!idx.blocked_at(306.0f, 300.0f, 0.3f, 0.0f, kManH),
          "point along world X misses the rotated plank");

    // ── Cylinder: round, not square ──
    CHECK(idx.blocked_at(52.5f, 50.0f, 0.0f, 0.0f, kManH), "inside cylinder radius");
    CHECK(!idx.blocked_at(53.5f, 50.0f, 0.0f, 0.0f, kManH), "outside cylinder radius");
    CHECK(!idx.blocked_at(52.4f, 52.4f, 0.0f, 0.0f, kManH),
          "square corner of the bounding box is OUTSIDE the round tower");

    // ── Slide & stop ──
    {
        // Diagonal into the box side: the free axis survives.
        float tx = 97.2f, ty = 97.2f;
        idx.resolve_step(95.0f, 96.0f, tx, ty, 0.5f, 0.0f, kManH);
        CHECK(tx == 97.2f && ty == 96.0f, "diagonal step slides along the wall");
    }
    {
        // Head-on: full stop.
        float tx = 98.0f, ty = 100.0f;
        idx.resolve_step(95.0f, 100.0f, tx, ty, 0.5f, 0.0f, kManH);
        CHECK(tx == 95.0f && ty == 100.0f, "head-on step is refused");
    }
    {
        // Escape rule: a body already inside may move anywhere (out).
        float tx = 101.0f, ty = 100.0f;
        idx.resolve_step(100.0f, 100.0f, tx, ty, 0.5f, 0.0f, kManH);
        CHECK(tx == 101.0f && ty == 100.0f,
              "body inside a solid is never trapped");
    }

    // ── Seating uses the height function ──
    struct Lift { static float h(void*, float, float) { return 100.0f; } };
    idx.rebuild(solids, &Lift::h, nullptr);
    CHECK(std::fabs(idx.support_at(100.0f, 100.0f, 0.5f, 105.0f) - 105.0f) < 1e-4f,
          "solid spans are absolute: seat 100 + height 5 supports at 105");
    CHECK(!idx.blocked_at(100.0f, 100.0f, 0.5f, 105.0f, kManH),
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
    // ФИКСТУРА ВРАЛА О МИРЕ, И ЭТО ВСКРЫЛОСЬ 2026-10-26 (bugs.md Б3): здесь
    // стояло `FT_None`, то есть клетка города БЕЗ ФИЧИ — мир такого не рождает.
    // Авторитет здесь ПОЛЕ ФИЧ, одна из двух законных систем (вердикт владельца
    // 2026-10-26: «у нас есть только система поля фич и система сквады всё (у
    // них есть архетипы)»), а «род места» — ЛЕГАСИ ПОД СНОС. Пока связность
    // спрашивали односторонне, ложь была бесплатной; теперь она свойство ПАРЫ
    // (`features_link@src/sub/gens/kit/streets.h`), своя фича участвует в
    // вопросе, и клетка с нулём просила город, не связанный ни с чем — отсюда
    // красные ворота.
    city.feature = FT_City;
    city.rooted.id = 101;
    city.rooted.size = 6000;
    city.rooted.kind = SquadType::City;
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
    const int population = int(city.rooted.size);
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
            return height_m(m->heightmap[std::size_t(ty) * kCellSize + tx]);
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
            if (idx.blocked_at(fx, fy, 0.45f, H::at(&h, fx, fy), kManH)) {
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
        if (idx.blocked_at(s.x, s.y, 0.45f, H::at(&h, s.x, s.y), kManH)) {
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

// ── PART E: A STEP IS A SWEEP, NOT A DESTINATION TEST (M-398) ─────────────
// The defect this guards was STRUCTURAL and its threshold is arithmetic. A
// destination test refuses a body only while the body LANDS inside the
// Minkowski-expanded solid, so the band able to refuse it is 2·halfXy + 2·r
// wide — and a step wider than the band crosses it clean, at any calibration.
// Against the engine's own ceiling the narrowest solids of the world lose:
// the subworld walk is 24 tiles/s (kSubworldWalkTilesPerSecond, under a
// static_assert), the M-389 product ceiling is ×5.98, so a body does 143.5
// tiles/s = 2.24 tiles per 64 Hz frame, while a fence's band is
// 2·0.3 + 2·0.55 = 1.70 and a house's is 2·1.6 + 1.1 = 4.30 (so the house
// falls the moment a frame drops under 33 Hz, and the direct mover already
// hands in 12.8-tile displacements).
//
// THE WITNESS BUILDS ITS OWN FENCE AND HOUSE (AGENTS §8 п.11) out of the
// shipping kind rows' own minimum half-extents — it cannot pass because some
// generator was kind to it on some seed — and it drives BOTH movers, the
// direct one (resolve_step) and the mass pass (steer_bodies through the ONE
// door), because the question used to have two spellings and a fix to either
// would have left the other tunnelling.
void part_e_step_is_a_sweep() {
    // Every number below is READ, never written (AGENTS §8 п.4): a retune of
    // the walk, of the Spd law or of a kind's floor moves the test with it.
    constexpr float kR = kNpcBodyRadiusDefault;
    // The product ceiling M-389 left: quickness_pct at Spd 255 × Athletics 100.
    // The attribute's ceiling is its TYPE's (anketa.h Attributes::score is a
    // byte, and M-389's static_assert is stated against exactly that), the
    // skill's is kMaxSkillRank. Read, not written.
    const int spdCeiling = int(std::numeric_limits<std::uint8_t>::max());
    const float ceilingMult = float(quickness_pct(spdCeiling)) / 100.0f
        * skill_mult_of(SkillId::Athletics, kMaxSkillRank);
    const float tilesPerSecond = kSubworldWalkTilesPerSecond * ceilingMult;
    const float frameStep = tilesPerSecond / float(kTicksPerRealSecond);

    const float fenceHalf = structure_min_half_xy(Structure::Fence);
    const float houseHalf = structure_min_half_xy(Structure::House);
    const float fenceBand = 2.0f * fenceHalf + 2.0f * kR;
    const float houseBand = 2.0f * houseHalf + 2.0f * kR;
    // THE RUN-UP, and it belongs in the arithmetic rather than in the prose.
    // A body standing `kStandoff` clear of the near edge tunnels iff its step
    // exceeds BAND + STANDOFF, not merely the band: the band is the crossing
    // width, the standoff is the ground before it. Writing the test against
    // the band alone left the house case passing under the very mutation it
    // was meant to catch — the destination still landed inside the house.
    constexpr float kStandoff = 0.05f;
    // The premise of the whole part, asserted rather than assumed: the fence
    // IS narrower than one frame step at the ceiling. Were it not, this part
    // would be guarding nothing and must say so out loud.
    CHECK(frameStep > fenceBand + kStandoff,
          "one frame step at the engine's own ceiling clears a fence's whole "
          "refusing band from a standing start — that is why a destination "
          "test cannot hold it");
    CHECK(houseBand + kStandoff > frameStep,
          "a house is WIDER than one ceiling frame step: it falls to a long "
          "step, not to the ceiling — so the two thresholds are different "
          "numbers and both are real");

    // Zero half-extents on purpose: the index applies the kind row's own
    // minimum floor, so these are the honest NARROWEST solids of the world.
    std::vector<Structure> solids;
    solids.push_back(make_box(100.0f, 100.0f, 0.0f, 0.0f, 0.0f,
                              /*height*/2.0f, /*zBase*/0.0f,
                              Structure::Fence));
    solids.push_back(make_box(300.0f, 100.0f, 0.0f, 0.0f, 0.0f,
                              /*height*/6.0f, /*zBase*/0.0f,
                              Structure::House));
    StructureIndex idx;
    idx.rebuild(solids, &flat_height, nullptr);
    CHECK(idx.solid_count() == 2, "the witness built its own fence and house");

    // ── MOVER 1: the direct door ──
    // Start just clear of the fence's band and step one ceiling frame: the
    // destination lands past the far edge, so a destination test calls it free.
    const float fenceStart = 100.0f - fenceHalf - kR - kStandoff;
    {
        float tx = fenceStart + frameStep, ty = 100.0f;
        CHECK(!idx.blocked_at(fenceStart, ty, kR, 0.0f, kManH),
              "the body starts OUTSIDE the fence");
        CHECK(!idx.blocked_at(fenceStart + frameStep, ty, kR, 0.0f, kManH),
              "...and its destination is outside too — the exact blindness a "
              "point test has: both ends free, the wall in between");
        idx.resolve_step(fenceStart, 100.0f, tx, ty, kR, 0.0f, kManH);
        CHECK(tx < 100.0f - fenceHalf,
              "a ceiling-speed step is REFUSED BY THE FENCE: the body stays on "
              "the near side instead of flying through it");
        CHECK(!idx.blocked_at(tx, ty, kR, 0.0f, kManH),
              "...and where it stopped is not itself inside a solid");
    }
    // The HOUSE needs a longer step, and the length comes from the house's own
    // band — no frame rate and no real second enters the number (AGENTS «КАЛИБ-
    // РОВАТЬ ТОЛЬКО В ТИКАХ»). Such a step is not hypothetical: the direct
    // mover is handed 12.8 tiles by the shipping smoke harness
    // (`move_player(0.0f, 12.8f)` in src/app/smoke.cpp), and the ceiling pace
    // reaches this much per frame as soon as a frame runs long.
    {
        const float longStep = houseBand + 2.0f * kStandoff;
        const float startX = 300.0f - houseHalf - kR - kStandoff;
        CHECK(!idx.blocked_at(startX + longStep, 100.0f, kR, 0.0f, kManH),
              "a step derived from the HOUSE's own band lands past it — a "
              "destination test sees nothing here either");
        float tx = startX + longStep, ty = 100.0f;
        idx.resolve_step(startX, 100.0f, tx, ty, kR, 0.0f, kManH);
        CHECK(tx < 300.0f - houseHalf,
              "a long step is refused by the house wall too — the law is the "
              "band, not the speed that produced the step");
    }
    // Diagonal: the slide candidates are swept as well, so sliding along one
    // wall cannot tunnel through the next.
    {
        float tx = fenceStart + frameStep, ty = 100.0f + frameStep;
        idx.resolve_step(fenceStart, 100.0f, tx, ty, kR, 0.0f, kManH);
        CHECK(tx < 100.0f - fenceHalf || ty != 100.0f + frameStep,
              "a diagonal ceiling step past the fence cannot keep BOTH "
              "components");
        CHECK(!idx.blocked_at(tx, ty, kR, 0.0f, kManH),
              "whatever it kept leaves it outside the solid");
    }
    // ── THE ESCAPE RULE IS UNTOUCHED ──
    // The sweep necessarily reports the solid it starts inside, which is what
    // makes this the dangerous case: a sweep without the escape clause would
    // lock a teleported or re-centred body in masonry forever — a new bug
    // traded for the old one.
    {
        float tx = 108.0f, ty = 100.0f;   // straight out THROUGH the fence
        CHECK(idx.blocked_at(100.0f, 100.0f, kR, 0.0f, kManH),
              "the fence centre is inside a solid (precondition)");
        idx.resolve_step(100.0f, 100.0f, tx, ty, kR, 0.0f, kManH);
        CHECK(tx == 108.0f && ty == 100.0f,
              "a body INSIDE a solid may still make a long step out — the "
              "escape rule survives the sweep");
    }

    // ── MOVER 2: the mass pass, through the SAME door ──
    // One body already at the ceiling pace, marching +x into the fence. Its
    // own intent rules its legs (B_Passive), so no field and no enemy touch
    // the result: what is measured is the solid gate and nothing else.
    struct Gate {
        // Высота едет ПАРАМЕТРОМ колбэка (M-401): прибор обязан доказывать
        // тот же путь, которым идёт мувер, а не свой.
        static void step(void* user, float fromX, float fromY, float* toX,
                         float* toY, float r, float z, float bodyH) {
            static_cast<const StructureIndex*>(user)
                ->resolve_step(fromX, fromY, *toX, *toY, r, z, bodyH);
        }
    };
    // РОСТ И ФЛАГИ — ПАРАМЕТРЫ ХОДКИ (M-401): без них прибор судил бы только
    // человека на земле и пережил бы ЗЕЛЁНЫМ две мутации, которые обязаны
    // краснить — возврат пропуска двери летящим и подстановку гуманоидной
    // макушки в мувере. Обе прошли молча на первой версии этого свидетеля.
    auto run_march_as = [&](bool wireSolids, float bodyH,
                            std::uint32_t extraFlags, float& outX) {
        auto crowd = make_body_crowd();
        BodyDesc d{};
        d.x = fenceStart;
        d.y = 100.0f;
        d.z = 0.0f;
        d.radius = kR;
        d.height = bodyH;
        d.speed = tilesPerSecond;
        d.vx = tilesPerSecond;          // already at pace: the first step is a
        d.intentVx = tilesPerSecond;    // full 2.24 tiles, not an acceleration
        d.sight = 1.0f;
        d.faction = 0;
        d.flags = std::uint8_t(B_Passive | extraFlags);
        crowd->add(d);
        MoveGround terrain{};
        terrain.worldMax = 512.0f;
        if (wireSolids) {
            terrain.resolveStep = &Gate::step;
            terrain.solidUser = &idx;
        }
        MoveParams prm{};
        auto fine = make_unit_grid();
        auto pick = make_unit_grid();
        auto field = make_influence_field();
        const float dt = 1.0f / float(kTicksPerRealSecond);
        for (int t = 0; t < 8; ++t) {
            build_unit_grid(*fine, *crowd, fine_cell_for(*crowd, prm), 256);
            build_unit_grid(*pick, *crowd, pick_cell_for(*crowd, prm), 256);
            build_influence_field(*field, *crowd, prm.influenceCell, 512.0f);
            steer_bodies(*crowd, *fine, *pick, *field, terrain, prm, dt,
                         nullptr);
        }
        outX = crowd->x[0];
    };
    auto run_march = [&](bool wireSolids, float& outX) {
        run_march_as(wireSolids, kManH, 0u, outX);
    };
    // The control FIRST, and it is the one that proves the fixture can move at
    // all: with no solid door wired the march walks straight past the fence.
    // Without it, "the body is on the near side" could be true because the
    // body never went anywhere (AGENTS §8 п.3).
    float freeX = 0.0f, gatedX = 0.0f;
    run_march(/*wireSolids*/false, freeX);
    CHECK(freeX > 100.0f + fenceHalf + kR,
          "control: with no solid door the march crosses the fence line — the "
          "fixture really does take ceiling-sized steps");
    run_march(/*wireSolids*/true, gatedX);

    // ── ЕДИНАЯ СИСТЕМА СТОЛКНОВЕНИЯ: ЛЕТЯЩИЙ ТОЖЕ СПРАШИВАЕТ ДВЕРЬ ──────────
    // (M-401, вердикт владельца: «единая система игрок = нпц … да все
    //  одинаково сталкиваются»). До наряда массовый проход пропускал дверь по
    //  флагу `B_Flying`, и летящий НПЦ проходил СКВОЗЬ каменную стену, тогда
    //  как летящий ИГРОК той же стеной останавливался. Один вопрос — два
    //  ответа; теперь ответ один, а «пролететь над верхом» решает геометрия.
    float flyGatedX = 0.0f, flyFreeX = 0.0f;
    run_march_as(/*wireSolids*/false, kManH, B_Flying, flyFreeX);
    run_march_as(/*wireSolids*/true,  kManH, B_Flying, flyGatedX);
    CHECK(flyFreeX > 100.0f + fenceHalf + kR,
          "контроль: без двери летящий проходит забор — ходка летящего тела "
          "вообще движется");
    CHECK(flyGatedX < 100.0f - fenceHalf,
          "а с дверью ЛЕТЯЩИЙ ОСТАНОВЛЕН той же стеной, что и пеший: пропуска "
          "двери по флагу полёта больше нет");

    // ── РОСТ ЕДЕТ ЧЕРЕЗ МУВЕР, А НЕ БЕРЁТСЯ ДЕФОЛТОМ ───────────────────────
    // Низкое тело проходит под той же преградой, что останавливает высокое —
    // и судит это МУВЕР, своей колонкой `BodyCrowd::height`. Преграда здесь
    // поднята (`zBase`), поэтому вопрос решается ровно макушкой.
    {
        std::vector<Structure> lifted;
        // РОД — `Fence`, И ЭТО НЕСУЩЕЕ УСЛОВИЕ, А НЕ ВКУС (M-405). Здесь стоял
        // род по умолчанию (`Wall`), чей ПОЛ ПОЛУРАЗМЕРА равен 1.2 тайла, тогда
        // как отступ `fenceStart` выведен из полуразмера ЗАБОРА (0.3): индекс
        // раздувал преграду до 1.2, и тело стартовало ВНУТРИ неё — где правило
        // побега честно выпускало его наружу. Прибор печатал «поднятый солид не
        // держит никого», а держал он правильно: тот же дракон с отступа 98.20
        // (выведенного из 1.2) отказывался шагать и стоял на месте. Дефект был
        // в предусловии СВИДЕТЕЛЯ, не в мире — поэтому род солида и отступ
        // обязаны выводиться из ОДНОГО числа, и ниже это утверждается вслух.
        lifted.push_back(make_box(100.0f, 100.0f, fenceHalf, fenceHalf, 0.0f,
                                  /*height*/1.0f, /*zBase*/2.0f,
                                  Structure::Fence));
        StructureIndex liftedIdx;
        liftedIdx.rebuild(lifted, &flat_height, nullptr);
        // ПРЕДУСЛОВИЕ, УТВЕРЖДЁННОЕ, А НЕ ПРЕДПОЛОЖЕННОЕ (AGENTS §8 п.11):
        // дракон обязан начинать СНАРУЖИ преграды, иначе измеряется правило
        // побега, а не высота. Ровно это и прошло незамеченным.
        CHECK(!liftedIdx.blocked_at(fenceStart, 100.0f, kR, 0.0f, kDragonH),
              "предусловие: дракон стартует СНАРУЖИ поднятой преграды — иначе "
              "мерился бы побег из солида, а не макушка");
        // ...и преграда обязана быть УЖЕ одного шага, иначе её держал бы не
        // свип, а точечный тест, и ломоть M-398 здесь не проверялся бы.
        CHECK(frameStep > 2.0f * fenceHalf + 2.0f * kR,
              "поднятая преграда уже одного шага потолка — значит держит её "
              "СВИП, а не тест точки назначения");
        auto march_under = [&](float bodyH, float& outX) {
            auto crowd = make_body_crowd();
            BodyDesc d{};
            d.x = fenceStart; d.y = 100.0f; d.z = 0.0f; d.radius = kR;
            d.height = bodyH; d.speed = tilesPerSecond;
            d.vx = tilesPerSecond; d.intentVx = tilesPerSecond;
            d.sight = 1.0f; d.faction = 0; d.flags = B_Passive;
            crowd->add(d);
            MoveGround terrain{};
            terrain.worldMax = 512.0f;
            terrain.resolveStep = &Gate::step;
            terrain.solidUser = &liftedIdx;
            MoveParams prm{};
            auto fine = make_unit_grid(); auto pick = make_unit_grid();
            auto field = make_influence_field();
            const float dt = 1.0f / float(kTicksPerRealSecond);
            for (int t = 0; t < 8; ++t) {
                build_unit_grid(*fine, *crowd, fine_cell_for(*crowd, prm), 256);
                build_unit_grid(*pick, *crowd, pick_cell_for(*crowd, prm), 256);
                build_influence_field(*field, *crowd, prm.influenceCell, 512.0f);
                steer_bodies(*crowd, *fine, *pick, *field, terrain, prm, dt,
                             nullptr);
            }
            outX = crowd->x[0];
        };
        float hareX = 0.0f, dragonX = 0.0f;
        march_under(kHareH, hareX);
        march_under(kDragonH, dragonX);
        CHECK(hareX > 100.0f + fenceHalf + kR,
              "заяц ПРОХОДИТ под поднятой преградой");
        // ПИН НАБЛЮДЕНИЯ M-405 СНЯТ: он утверждал `hareX == dragonX` и был
        // ВЕРЕН — но верен о сломанном предусловии, а не о мире. Теперь прибор
        // стартует снаружи преграды и утверждает ту правду, которой боялся:
        // одна преграда, два роста, ДВА исхода, и судит их МУВЕР.
        CHECK(dragonX < 100.0f - fenceHalf,
              "а дракон поднятой преградой ОСТАНОВЛЕН через мувер: его макушка "
              "выше низа солида, и это решает колонка роста, а не род");
        CHECK(hareX != dragonX,
              "ОДНА преграда судит два тела ПО-РАЗНОМУ и в МАССОВОМ проходе — "
              "ровно то, чего гуманоидный дефолт сделать не мог");
        std::fprintf(stderr,
                     "[m405] ходка под поднятой преградой: заяц x=%.2f "
                     "(прошёл) · дракон x=%.2f (стоит) · летящий x=%.2f\n",
                     double(hareX), double(dragonX), double(flyGatedX));
        std::fflush(stderr);
    }
    CHECK(gatedX < 100.0f - fenceHalf,
          "the MASS PASS is held on the near side of the fence too — both "
          "movers ask the one door, so neither tunnels");
    CHECK(!idx.blocked_at(gatedX, 100.0f, kR, 0.0f, kManH),
          "the mass pass leaves its body outside the solid, not inside it");
}

} // namespace

int main() {
    part_a_index_semantics();
    part_f_height_decides_who_passes_under();
    part_b_generated_city();
    part_c_vertical_physics();
    part_d_support_lays_the_ground();
    part_e_step_is_a_sweep();
    return sm::test::report("structure_collide_test");
}
