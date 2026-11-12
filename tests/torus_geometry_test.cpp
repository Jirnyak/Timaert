// Locks the torus bearing helpers (core/torus.h) that drive the macro-road
// "doubled diagonal" fix.
//
// The road network is an MST + one redundancy edge per city (politik.cpp).
// The redundancy edge used to fan out almost parallel to a road the city
// already had — three roughly-collinear cities A–B–C (MST links A–B, B–C)
// gained a bypass A→C that visually doubled the A–B–C corridor. The fix guards
// that pass with `torus_bearings_parallel`: a candidate whose bearing shadows
// an existing road is skipped. This test encodes exactly those road scenarios
// so the geometry can't silently drift.
//
// EVERY assertion here used to be spelled `if (!x) return fail("…")` inside
// main. Three things were wrong with that and none of them were visible:
//   * `fail()` handed check.h a HARDCODED "tests/torus_geometry_test.cpp" and
//     line 0, so a failure named the file and lied about the line;
//   * on the green path nothing was counted at all — the file's only counted
//     check was the un-failable `CHECK(true, "every gate above held")` at the
//     bottom, a ritual that exists solely to satisfy "zero checks is a failure";
//   * the FIRST broken fact ended the test, so a session fixing one of them
//     learned nothing about the other eighteen.
#include "check.h"

#include "core/torus.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки — здесь стояло 100×100,
// и фикстура лгала о мире ровно там, где дверь с 2026-11-06 заворачивает
// МАСКОЙ (torus_fold): на не-степени-двойки маска бессмысленна по построению.
constexpr int W = 128, H = 128;
// cos thresholds: kRoadFanCosThreshold in politik.cpp is 0.90 (~26°).
constexpr float kFan = 0.90f;

// ── torus_bearing: unit direction, shortest wrapped delta ──────────────────
void test_bearing_is_a_unit_direction() {
    using namespace sm;
    float ux = 0.0f, uy = 0.0f;

    CHECK_OR_RETURN(torus_bearing(10, 10, 20, 10, W, H, ux, uy),
                    "two distinct cells have a bearing between them (east)");
    CHECK(approx(ux, 1.0f) && approx(uy, 0.0f), "due east reads as (+1,0)");

    CHECK_OR_RETURN(torus_bearing(10, 10, 10, 20, W, H, ux, uy),
                    "two distinct cells have a bearing between them (south)");
    CHECK(approx(ux, 0.0f) && approx(uy, 1.0f), "due south reads as (0,+1)");

    // Perfect diagonal normalizes to (√½,√½).
    CHECK_OR_RETURN(torus_bearing(0, 0, 10, 10, W, H, ux, uy),
                    "a diagonal pair has a bearing");
    CHECK(approx(ux, 0.70710678f) && approx(uy, 0.70710678f),
          "the bearing is a UNIT vector — a perfect diagonal normalizes");
}

void test_coincident_cells_have_no_bearing() {
    using namespace sm;
    float ux = 1.0f, uy = 1.0f;   // seeded non-zero: the door must CLEAR them
    CHECK(!torus_bearing(5, 5, 5, 5, W, H, ux, uy),
          "a cell has no bearing to itself — the door says so");
    CHECK(approx(ux, 0.0f) && approx(uy, 0.0f),
          "and it zeroes the vector rather than leaving the caller's garbage");
}

// ЗАКОН АДРЕСА: мир связный тор, поэтому «далеко» через шов — это БЛИЗКО.
void test_bearing_takes_the_short_way_across_the_seam() {
    using namespace sm;
    float ux = 0.0f, uy = 0.0f;
    // From x=95 to x=2 on a width-128 torus is +35 east (the short way), NOT
    // −93 west. A world that is merely tiled would answer west here.
    CHECK_OR_RETURN(torus_bearing(95, 50, 2, 50, W, H, ux, uy),
                    "cells on opposite sides of the seam have a bearing");
    CHECK(approx(ux, 1.0f) && approx(uy, 0.0f),
          "across the seam the bearing takes the SHORT way (+east), not the "
          "long way round — the world is connected, not tiled");
}

// ── torus_bearings_parallel: the road-fan decision ─────────────────────────
void test_road_fan_suppresses_duplicates() {
    using namespace sm;
    // DOUBLED DIAGONAL (the bug): A–B–C collinear east. A already links B;
    // the redundancy candidate C sits dead ahead → suppress.
    CHECK(torus_bearings_parallel(0, 50, 10, 50, 20, 50, W, H, kFan),
          "a collinear A-B-C bypass is a duplicate road, and is suppressed");
    // A shallow fan just under the angle threshold is still a duplicate:
    // candidate (20,53) tilts a little, dot ≈ 0.989 > 0.90.
    CHECK(torus_bearings_parallel(0, 50, 20, 50, 20, 53, W, H, kFan),
          "a shallow fan inside the angle threshold is a duplicate too");
    // Wrap-aware: two roads that only LOOK far apart across the seam are still
    // parallel — the same connectedness the bearing test states above.
    CHECK(torus_bearings_parallel(95, 50, 2, 50, 6, 50, W, H, kFan),
          "duplicate roads are spotted ACROSS the seam, not only within a tile");
}

// The negative controls of the same door, and they are the half that matters:
// a guard that flags everything would pass every assertion above.
void test_road_fan_keeps_genuine_alternates() {
    using namespace sm;
    CHECK(!torus_bearings_parallel(50, 50, 60, 50, 50, 60, W, H, kFan),
          "a PERPENDICULAR redundancy edge is a genuine alternate route, kept");
    CHECK(!torus_bearings_parallel(50, 50, 60, 50, 40, 50, W, H, kFan),
          "a road going the OTHER way closes a real loop, kept");
    CHECK(!torus_bearings_parallel(0, 0, 10, 0, 10, 10, W, H, kFan),
          "a 45-degree fan (dot ~0.707, below the threshold) is a wide useful "
          "loop, kept");
    CHECK(!torus_bearings_parallel(5, 5, 5, 5, 20, 20, W, H, kFan),
          "an existing edge with no bearing is never 'parallel' to anything");
    CHECK(!torus_bearings_parallel(5, 5, 20, 20, 5, 5, W, H, kFan),
          "a candidate edge with no bearing is never 'parallel' either");
}

} // namespace

// ── ГЕОМЕТРИЯ НАД ИНДЕКСОМ (M-462 / M-463) ───────────────────────────────
// ЗАКОН: макромир говорит индексами клеток; смещение, путь и сосед к цели
// берут ИНДЕКСЫ. Цена шага — пара целых: прямой 2, диагональный 3
// (`kStepStraight` / `kStepDiagonal`), путь — 2·max + min в полушагах.
//
// ЯКОРЯ НЕЗАВИСИМЫ ОТ ФОРМУЛЫ (§8 п.6): путь сверяется (а) с ходьбой по
// соседям, оплаченной поштучно, и (б) с евклидом — отношением, названным в
// выводе пары: решётка никогда не короче земли и не длиннее её в √5/2 раз.
void test_cell_delta_folds_across_the_seam() {
    using namespace sm;
    const std::uint32_t a = cell_of(126, 5, W);
    const std::uint32_t b = cell_of(1, 5, W);
    const CellDelta ab = cell_delta(a, b, W);
    const CellDelta ba = cell_delta(b, a, W);
    CHECK(ab.dx == 3 && ab.dy == 0,
          "через шов по x: от 126 до 1 — три клетки на восток, не 125 на запад");
    CHECK(ba.dx == -3 && ba.dy == 0, "обратно — три клетки на запад");
    const CellDelta v = cell_delta(cell_of(7, 127, W), cell_of(7, 2, W), W);
    CHECK(v.dx == 0 && v.dy == 3, "через шов по y — то же, и x не задет");
    // Заём: x «до» меньше x «от», а y отличается — младшие биты обязаны дать
    // разность x, не тронув y (и наоборот).
    const CellDelta m = cell_delta(cell_of(100, 10, W), cell_of(90, 14, W), W);
    CHECK(m.dx == -10 && m.dy == 4,
          "заём между осями: разность индексов под маской есть разность x");
}

void test_cell_dist_is_the_two_three_metric() {
    using namespace sm;
    const std::uint32_t o = cell_of(40, 40, W);
    CHECK(cell_dist(o, o, W) == 0, "та же клетка — путь ноль");
    CHECK(cell_dist(o, cell_of(43, 40, W), W) == 3 * kStepStraight,
          "три клетки прямо — три прямых шага");
    CHECK(cell_dist(o, cell_of(43, 43, W), W) == 3 * kStepDiagonal,
          "три клетки по диагонали — три диагональных шага");
    CHECK(cell_dist(cell_of(127, 0, W), cell_of(1, 0, W), W)
              == 2 * kStepStraight,
          "через шов: две клетки, а не сто двадцать шесть");

    // ЯКОРЬ (а): путь равен цене ходьбы по соседям в сторону цели, где
    // каждый шаг оплачен по своему роду. ЯКОРЬ (б): отношение к евклиду.
    // Симметрия и неравенство треугольника — свойства метрики, без которых
    // «ближайший» и «не дальше R» не значат ничего.
    int samples = 0, walkMismatch = 0, asym = 0, triBroken = 0;
    int shorterThanGround = 0, longerThanBound = 0;
    const double bound = std::sqrt(5.0) / 2.0;
    for (int dy = -9; dy <= 9; ++dy) {
        for (int dx = -9; dx <= 9; ++dx) {
            const std::uint32_t t = cell_step(o, dx, dy, W);
            const int d = cell_dist(o, t, W);
            ++samples;
            if (d != cell_dist(t, o, W)) ++asym;

            int paid = 0;
            std::uint32_t at = o;
            for (int guard = 0; guard < 64 && at != t; ++guard) {
                const std::uint32_t nx = cell_toward(at, t, W);
                const CellDelta s = cell_delta(at, nx, W);
                paid += (s.dx != 0 && s.dy != 0) ? kStepDiagonal
                                                 : kStepStraight;
                at = nx;
            }
            if (at != t || paid != d) ++walkMismatch;

            if (dx != 0 || dy != 0) {
                const double ground = std::sqrt(double(dx * dx + dy * dy));
                const double ratio = double(d) / (double(kStepStraight) * ground);
                if (ratio < 1.0 - 1e-9) ++shorterThanGround;
                if (ratio > bound + 1e-9) ++longerThanBound;
            }
            // Треугольник через фиксированную третью клетку.
            const std::uint32_t via = cell_of(44, 37, W);
            if (d > cell_dist(o, via, W) + cell_dist(via, t, W)) ++triBroken;
        }
    }
    CHECK(samples == 19 * 19, "замер состоялся: весь бокс 19×19 обойдён");
    CHECK(walkMismatch == 0,
          "путь есть цена ходьбы по соседям: диагонали по 3, прямые по 2");
    CHECK(asym == 0, "путь симметричен");
    CHECK(triBroken == 0, "неравенство треугольника держится");
    CHECK(shorterThanGround == 0,
          "решётка НИКОГДА не короче земли: путь не меньше евклида");
    CHECK(longerThanBound == 0,
          "и длиннее земли не более чем в √5/2 раза (11.8 %)");
}

void test_cell_within_takes_its_radius_in_cells() {
    using namespace sm;
    const std::uint32_t o = cell_of(40, 40, W);
    CHECK(cell_within(o, cell_of(46, 40, W), 6, W),
          "шесть клеток прямо — в радиусе шести");
    CHECK(!cell_within(o, cell_of(47, 40, W), 6, W),
          "седьмая прямо — уже нет");
    CHECK(cell_within(o, cell_of(44, 44, W), 6, W),
          "четыре диагонали стоят шести прямых — на границе, внутри");
    CHECK(!cell_within(o, cell_of(45, 45, W), 6, W),
          "пятая диагональ — снаружи: радиус не квадрат 13×13");
    CHECK(cell_within(o, o, 0, W) && !cell_within(o, cell_of(41, 40, W), 0, W),
          "радиус ноль — только сама клетка");
}

void test_cell_toward_steps_one_neighbour() {
    using namespace sm;
    const std::uint32_t a = cell_of(10, 10, W);
    CHECK(cell_toward(a, cell_of(20, 13, W), W) == cell_of(11, 11, W),
          "обе оси ненулевые — шаг диагональный");
    CHECK(cell_toward(a, cell_of(20, 10, W), W) == cell_of(11, 10, W),
          "одна ось — шаг прямой");
    CHECK(cell_toward(a, a, W) == a, "к самой себе — стоит на месте");
    CHECK(cell_toward(cell_of(127, 3, W), cell_of(2, 3, W), W)
              == cell_of(0, 3, W),
          "через шов — на восток, короткой стороной");
}

int main() {
    test_bearing_is_a_unit_direction();
    test_coincident_cells_have_no_bearing();
    test_bearing_takes_the_short_way_across_the_seam();
    test_road_fan_suppresses_duplicates();
    test_road_fan_keeps_genuine_alternates();
    test_cell_delta_folds_across_the_seam();
    test_cell_dist_is_the_two_three_metric();
    test_cell_within_takes_its_radius_in_cells();
    test_cell_toward_steps_one_neighbour();
    return sm::test::report("torus_geometry_test");
}
