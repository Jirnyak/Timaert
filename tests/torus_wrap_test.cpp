// THE wrap, and the fact that there is only one of it.
//
// A torus world is seamless only if EVERY consumer wraps, and for a long time
// six of them wrapped with their own private copy of the same four lines
// (macro_lighting, optics, map_generator, features, pathfinding, deposit_layer).
// Nothing was wrong with any copy — the danger is arithmetic: six bodies are
// six chances for the seventh author to write a modulo without the negative
// case, and a forgotten modulo IS a seam (CANON.md S1).
//
// This test pins the law rather than the bodies: the wrap must be a genuine
// ring homomorphism (translation by the world size changes nothing), it must
// answer for coordinates far outside the map on both sides, and it must fail
// closed on a degenerate world instead of dividing by zero.
#include "check.h"

#include "core/torus.h"
#include "macro/features.h"
#include "macro/deposit_layer.h"

#include <cstdint>
#include <limits>

int main() {
    using namespace sm;
    constexpr int kW = 1024;   // the world (CANON.md S1)

    // ── The ring closes ──────────────────────────────────────────────────
    // Every point has the same answer as the same point one world over, in
    // both directions. This is the whole of seamlessness in one line.
    int checked = 0;
    for (int x = -3 * kW; x <= 3 * kW; x += 7) {
        CHECK(wrapi(x, kW) == wrapi(x + kW, kW), "one world over is the same place");
        CHECK(wrapi(x, kW) == wrapi(x - kW, kW), "and so is one world back");
        CHECK(wrapi(x, kW) >= 0 && wrapi(x, kW) < kW, "the answer is always inside the world");
        ++checked;
    }
    CHECK(checked > 0, "the sweep actually ran");

    // ── The seam is not special ──────────────────────────────────────────
    // Stepping off the last cell lands on the first, and stepping back off the
    // first lands on the last. A world where this fails has an edge.
    CHECK(wrapi(kW - 1 + 1, kW) == 0, "the last cell's neighbour is the first");
    CHECK(wrapi(0 - 1, kW) == kW - 1, "the first cell's neighbour is the last");

    // ── Far outside, and the extremes ────────────────────────────────────
    // A coordinate can arrive from a projection or a fling; the wrap must not
    // overflow on the way in (this is why the one body is 64-bit inside).
    CHECK(wrapi(std::numeric_limits<int>::min(), kW) >= 0,
          "the most negative coordinate still lands inside the world");
    CHECK(wrapi(std::numeric_limits<int>::max(), kW) < kW,
          "and so does the most positive");

    // ── Fail closed, never undefined ─────────────────────────────────────
    // `v % 0` is undefined behaviour. A world with no width is a caller's bug,
    // and the answer to a bug is a defined value, not a crash.
    CHECK(wrapi(5, 0) == 0, "a world with no width wraps to nothing, not to UB");
    CHECK(wrapi(-5, -8) == 0, "and neither does a negative one explode");

    // ── The named doors agree with the one behind them ───────────────────
    // `FeatureLayer::wrap_coord` and `DepositLayer::wrap_index` read as
    // documentation at their call sites, so they survive as names — but they
    // must not survive as second implementations.
    DepositLayer dep;
    dep.width = kW;
    dep.height = kW;
    for (int v = -2048; v <= 2048; v += 13) {
        CHECK(FeatureLayer::wrap_coord(v, kW) == wrapi(v, kW),
              "the feature grid wraps by the one law");
    }
    CHECK(dep.wrap_index(-1, 0) == std::uint32_t(kW - 1),
          "a deposit one cell west of the origin is on the far side of the map");
    CHECK(dep.wrap_index(0, -1) == std::uint32_t(kW) * std::uint32_t(kW - 1),
          "and one cell north of it is on the bottom row");

    // ── ЗАКОН АДРЕСА: клетка есть ОДНО число, сосед есть шаг по нему ─────
    // Свидетель проверяет не тела, а ЗАКОН: адрес маской обязан отвечать то
    // же, что адрес делением, на всём мире и за его краями; шаг обязан быть
    // замкнут на торе; и обратные двери обязаны вернуть ровно то, что
    // положили. Негативный контроль — ниже, отдельным абзацем.
    int addrs = 0;
    for (int y = -kW; y < 2 * kW; y += 37) {
        for (int x = -kW; x < 2 * kW; x += 41) {
            const std::uint32_t idx = cell_of(x, y, kW);
            CHECK(idx == std::uint32_t(wrapi(y, kW)) * std::uint32_t(kW)
                       + std::uint32_t(wrapi(x, kW)),
                  "адрес маской и адрес делением — один адрес");
            CHECK(cell_x(idx, kW) == wrapi(x, kW)
                  && cell_y(idx, kW) == wrapi(y, kW),
                  "обратные двери возвращают ту же клетку");
            ++addrs;
        }
    }
    CHECK(addrs > 0, "перебор адресов действительно состоялся");

    // Шаг к соседу — на всех восьми румбах, у самого шва, где рукописная
    // индексная арифметика и ломается: `idx+1` на правом крае уезжает
    // строкой ниже, `idx+side` на нижнем — за конец мира.
    int steps = 0;
    const int seam[] = {0, 1, kW / 2, kW - 2, kW - 1};
    for (const int sy : seam) {
        for (const int sx : seam) {
            const std::uint32_t at = cell_of(sx, sy, kW);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    CHECK(cell_step(at, dx, dy, kW)
                          == cell_of(sx + dx, sy + dy, kW),
                          "шаг к соседу равен адресу соседа");
                    ++steps;
                }
            }
        }
    }
    CHECK(steps == 5 * 5 * 9, "проверены все румбы во всех углах шва");

    // Шаг любой величины и любого знака — заворот приходит сам, потому что
    // сторона делит 2^32 нацело. Без этого «далеко назад» дало бы мусор.
    CHECK(cell_step(cell_of(3, 3, kW), -5, -9, kW) == cell_of(-2, -6, kW),
          "шаг за начало мира приходит с другой стороны");
    CHECK(cell_step(cell_of(0, 0, kW), 3 * kW + 5, -2 * kW - 7, kW)
          == cell_of(5, -7, kW),
          "шаг на несколько миров сворачивается как один");

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: детектор обязан уметь видеть дефект ─────────
    // Ровно та ошибка, ради которой дверь и заводится: наивный `idx + dx`
    // без маски. На правом крае мира он даёт НЕ соседа, а первую клетку
    // следующей строки. Если этот CHECK когда-нибудь станет зелёным —
    // значит проверка выше перестала что-либо проверять.
    const std::uint32_t edge = cell_of(kW - 1, 7, kW);
    CHECK(edge + 1u != cell_step(edge, 1, 0, kW),
          "наивная индексная арифметика на шве ВРЁТ — иначе дверь не нужна");
    CHECK(cell_step(edge, 1, 0, kW) == cell_of(0, 7, kW),
          "а дверь возвращает клетку с другого края той же строки");

    // Сторона, не бывшая степенью двойки, — ошибка звонящего. Ответ на
    // ошибку есть определённое значение, а не UB (тот же закон, что у wrapi
    // выше).
    CHECK(world_side_log2(1024) == 10 && world_side_log2(128) == 7,
          "лог стороны берётся у степени двойки");
    CHECK(world_side_log2(1000) == 0 && world_side_log2(0) == 0
          && world_side_log2(-4) == 0,
          "сторона не степень двойки — ответ определён, а не UB");

    return sm::test::report("torus_wrap_test");
}
