// ПОЛЯ КУРСА СУБМИРА — ДЕЙКСТРА ПО ВЕСАМ ПОВЕРХНОСТИ, ДОРОГА ТЯНЕТ САМА
// (ломоть 3 эпика M-419; ряд M-420: «не тупые прямолинейные а по весам
// поверхностей и с учётом преград»; «по этому маршруту они будут
// минимизировать вес поврехности то есть бесплатно сами потянутсмя к
// дороге»).
//
// Предмет — src/sub/course.{h,cpp}. Якоря — ОТНОШЕНИЯ из той же таблицы,
// что читает код (kTileGroundWeight), не пересказанные числа (§8 п.4):
//  - ВЕСА: дорога < луг < болото < вода < солидная ячейка — лестница
//    макрозакона пережила агрегат 8×8;
//  - ТОЧКА: спуск поля доводит ходока до ячейки-цели за ограниченное
//    число шагов (монотонность спуска = отсутствие цикла в пределе);
//  - ДОРОГА ТЯНЕТ: на болоте с Г-образной дорогой путь по полю идёт
//    дорогой, а не прямой — «тянутся к дороге» НЕ закодировано, оно
//    выпадает из Дейкстры;
//  - ВОРОТА ВЫПАДАЮТ САМИ: поток через глухую стену идёт ПРОЁМОМ, а не
//    прямой («сверхдорогая строка», вердикт владельца 2026-11-07);
//  - РУМБ: румбовое поле выводит на СВОЮ кромку;
//  - КАП НЕ МОЛЧИТ: 17-я уникальная точка-цель вытесняет LRU со счётчиком.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-07, числа фактические):
//  - спуск перевёрнут (step[n] = dd вместо (dd+4)&7 — шаг ОТ цели) →
//    КРАСНЫЙ, 9 из 14: обе точки не доходят, дорога/ворота/все румбы мимо;
//  - веса уравнены (weightQ = константа в rebuild_weight_rows) →
//    КРАСНЫЙ, 4 из 14: обе «ЛЕСТНИЦЫ ВЕСОВ», «ДОРОГА ТЯНЕТ ≥60%»,
//    «ВОРОТА: кросс в проёме»;
//  - солиды не штампуются (ранний return в stamp_solids) →
//    КРАСНЫЙ, 2 из 14: «ЛЕСТНИЦА ВЕСОВ (солид)», «ВОРОТА: кросс в проёме»;
//  - сиды румба E посеяны западной кромкой (col(0,…) вместо col(last,…)) →
//    КРАСНЫЙ, 1 из 14: «РУМБ E выводит на восточную кромку»;
//  - ДОЛЖНА МОЛЧАТЬ: kCourseSolidTileWeight 40 → 42 (баланс, не форма:
//    лестница и проём целы при любой доминирующей цене) → ЗЕЛЁНЫЙ, 14/14.
//    42, а не 48: 48 квантов × 8 × диагональ 3 = 1152 пробило бы кольцо
//    Диала, и его static_assert ПРАВИЛЬНО остановил бы сборку — это стена
//    формы, не предмет мутации.
#include "check.h"

#include "sub/course.h"

#include <cstdint>
#include <memory>
#include <vector>

using sm::sub::CourseWorld;
using sm::sub::Structure;
using sm::sub::course_cell_of;
using sm::sub::course_dir_point;
using sm::sub::course_dir_rumb;
using sm::sub::course_maintain;
using sm::sub::kCourseCellTiles;
using sm::sub::kCourseDim;
using sm::sub::kCourseNoStep;
using sm::sub::kDirOffsets;
using sm::sub::kFullSize;

namespace {

// Мир теста: композит тайлов на куче (9.4 МБ — фикстура, не кадр) + список
// строений + мир курса. Свидетель рожает своё предусловие сам (§8 п.11).
struct Fix {
    std::vector<std::uint8_t> tiles;
    std::vector<Structure> structs;
    std::unique_ptr<CourseWorld> cw = sm::sub::make_course_world();
    explicit Fix(std::uint8_t fill)
        : tiles(std::size_t(kFullSize) * std::size_t(kFullSize), fill) {}
    void pump(int iters) {
        for (int i = 0; i < iters; ++i) {
            course_maintain(*cw, tiles.data(), structs.data(), structs.size());
        }
    }
    void paint(int x0, int y0, int x1, int y1, std::uint8_t t) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                tiles[std::size_t(y) * kFullSize + std::size_t(x)] = t;
            }
        }
    }
    std::uint16_t cellW(int cx, int cy) const {
        return cw->weightQ[std::size_t(cy) * kCourseDim + std::size_t(cx)];
    }
};

// Жадный спуск: шаг на ячейку по направлению поля, пока поле отвечает.
template <typename DirFn, typename Visit>
int walk(DirFn&& dirAt, float& x, float& y, int maxSteps, Visit&& visit) {
    int steps = 0;
    while (steps < maxSteps) {
        const std::uint8_t d = dirAt(x, y);
        if (d == kCourseNoStep) break;
        x += float(kDirOffsets[d][0] * kCourseCellTiles);
        y += float(kDirOffsets[d][1] * kCourseCellTiles);
        ++steps;
        visit(x, y);
    }
    return steps;
}
const auto kNoVisit = [](float, float) {};

// Солидная стена курса: ориентированный бокс рода Wall (solid в строке
// рода), осевой — yaw 0.
Structure wall_box(float x, float y, float hx, float hy) {
    Structure s{};
    s.kind = Structure::Wall;
    s.x = x;
    s.y = y;
    s.radius = hx;
    s.height = 3.0f;
    s.hx = hx;
    s.hy = hy;
    return s;
}

} // namespace

int main() {
    // ── ЛЕСТНИЦА ВЕСОВ: агрегат 8×8 хранит порядок макрозакона ───────────
    {
        Fix f(sm::sub::TILE_GRASS);
        f.paint(64, 64, 71, 71, sm::sub::TILE_ROAD);     // ячейка (8,8)
        f.paint(80, 64, 87, 71, sm::sub::TILE_SHORE);    // ячейка (10,8)
        f.paint(96, 64, 103, 71, sm::sub::TILE_WATER);   // ячейка (12,8)
        f.structs.push_back(wall_box(115.5f, 67.5f, 4.5f, 4.5f)); // ячейка (14,8)
        f.pump(8);   // kCourseDim / kCourseWeightRowsPerTick — веса целиком
        const std::uint16_t road = f.cellW(8, 8);
        const std::uint16_t grass = f.cellW(9, 8);
        const std::uint16_t swamp = f.cellW(10, 8);
        const std::uint16_t water = f.cellW(12, 8);
        const std::uint16_t solid = f.cellW(14, 8);
        CHECK(road < grass && grass < swamp && swamp < water,
              "ЛЕСТНИЦА ВЕСОВ: дорога < луг < болото < вода — порядок "
              "макрозакона пережил агрегат");
        CHECK(water < solid,
              "ЛЕСТНИЦА ВЕСОВ (солид): кладка дороже воды — сверхдорогая "
              "строка, не стена");
    }

    // ── ТОЧКА НА ЛУГУ: спуск доводит, кап точек не молчит ────────────────
    {
        Fix f(sm::sub::TILE_GRASS);
        f.pump(8);
        const float tx = 2404.0f, ty = 2404.0f;
        CHECK(course_dir_point(*f.cw, tx, ty, 404.0f, 404.0f, 1)
                  == kCourseNoStep,
              "ТОЧКА: непечёное поле отвечает kCourseNoStep — фолбэк "
              "звонящего, не мусор");
        f.pump(1);   // один бейк за тик
        float x = 404.0f, y = 404.0f;
        const int steps = walk(
            [&](float px, float py) {
                return course_dir_point(*f.cw, tx, ty, px, py, 2);
            },
            x, y, 2048, kNoVisit);
        CHECK(course_cell_of(x, y) == course_cell_of(tx, ty),
              "ТОЧКА: жадный спуск пришёл в ячейку-цель");
        CHECK(steps > 0 && steps <= 600,
              "ТОЧКА: путь ограничен — спуск монотонен, цикла нет");
        // Кап точечных слотов НЕ молчит: ещё 16 уникальных целей поверх
        // одной занятой — вытеснение LRU считается счётчиком.
        for (int i = 0; i < 16; ++i) {
            (void)course_dir_point(*f.cw, float(100 + i * 32), 100.0f, 404.0f,
                                   404.0f, std::uint32_t(10 + i));
        }
        CHECK(f.cw->pointOverflows >= 1,
              "КАП ТОЧЕК: 17-я уникальная цель вытесняет LRU со счётчиком — "
              "не молча");
    }

    // ── ДОРОГА ТЯНЕТ: Г-образная дорога на болоте бьёт прямую ────────────
    {
        Fix f(sm::sub::TILE_SHORE);
        f.paint(800, 400, 807, 2407, sm::sub::TILE_ROAD);    // вертикаль
        f.paint(800, 2400, 1607, 2407, sm::sub::TILE_ROAD);  // горизонталь
        f.pump(8);
        const float tx = 1604.0f, ty = 2404.0f;
        (void)course_dir_point(*f.cw, tx, ty, 804.0f, 404.0f, 1);
        f.pump(1);
        float x = 804.0f, y = 404.0f;
        int onRoad = 0;
        const int steps = walk(
            [&](float px, float py) {
                return course_dir_point(*f.cw, tx, ty, px, py, 2);
            },
            x, y, 2048,
            [&](float px, float py) {
                if (f.tiles[std::size_t(py) * kFullSize + std::size_t(px)]
                    == sm::sub::TILE_ROAD) {
                    ++onRoad;
                }
            });
        CHECK(course_cell_of(x, y) == course_cell_of(tx, ty),
              "ДОРОГА ТЯНЕТ: путь дошёл до цели");
        CHECK(steps > 0 && onRoad * 10 >= steps * 6,
              "ДОРОГА ТЯНЕТ: ≥60% шагов по дороге — крюк дорогой дешевле "
              "прямой по болоту, ничем не закодировано");
    }

    // ── ВОРОТА ВЫПАДАЮТ САМИ: кросс стены — в проёме, не по прямой ───────
    {
        Fix f(sm::sub::TILE_GRASS);
        // Глухая стена x≈1536 во всю высоту, проём — тайлы y ∈ [1492,1540].
        f.structs.push_back(wall_box(1536.0f, 745.5f, 8.0f, 746.0f));
        f.structs.push_back(wall_box(1536.0f, 2306.0f, 8.0f, 765.5f));
        f.pump(8);
        const float tx = 2004.0f, ty = 1516.0f;
        (void)course_dir_point(*f.cw, tx, ty, 1004.0f, 504.0f, 1);
        f.pump(1);
        float x = 1004.0f, y = 504.0f;
        int crossCy = -1;
        const int steps = walk(
            [&](float px, float py) {
                return course_dir_point(*f.cw, tx, ty, px, py, 2);
            },
            x, y, 2048,
            [&](float px, float py) {
                if (crossCy < 0 && px >= 1544.0f) {
                    crossCy = int(py) / kCourseCellTiles;
                }
            });
        CHECK(course_cell_of(x, y) == course_cell_of(tx, ty) && steps > 0,
              "ВОРОТА: путь сквозь стену дошёл до цели");
        // Прямая из старта в цель пересекла бы стену у cy≈130; проём —
        // ячейки 186..192. Кросс обязан лежать у проёма.
        CHECK(crossCy >= 184 && crossCy <= 194,
              "ВОРОТА: кросс стены в проёме — ячейка с проёмом дешевле "
              "глухой, Дейкстра ведёт в ворота сама");
    }

    // ── РУМБЫ: каждое поле выводит на СВОЮ кромку ────────────────────────
    {
        Fix f(sm::sub::TILE_GRASS);
        f.pump(8);
        CHECK(course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::E), 1536.0f,
                              1536.0f) == kCourseNoStep,
              "РУМБ: непечёное румбовое поле отвечает kCourseNoStep");
        (void)course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::N), 1536.0f,
                              1536.0f);
        (void)course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::NE), 1536.0f,
                              1536.0f);
        f.pump(3);   // три бейка
        float ex = 1536.0f, ey = 1536.0f;
        const int eSteps = walk(
            [&](float px, float py) {
                return course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::E),
                                       px, py);
            },
            ex, ey, 500, kNoVisit);
        CHECK(eSteps > 0 && int(ex) / kCourseCellTiles == kCourseDim - 1,
              "РУМБ E выводит на восточную кромку");
        float nx = 1536.0f, ny = 1536.0f;
        const int nSteps = walk(
            [&](float px, float py) {
                return course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::N),
                                       px, py);
            },
            nx, ny, 500, kNoVisit);
        CHECK(nSteps > 0 && int(ny) / kCourseCellTiles == 0,
              "РУМБ N выводит на северную кромку");
        float dx = 1536.0f, dy = 1536.0f;
        const int dSteps = walk(
            [&](float px, float py) {
                return course_dir_rumb(*f.cw, std::uint8_t(sm::sub::Dir::NE),
                                       px, py);
            },
            dx, dy, 500, kNoVisit);
        const int dcx = int(dx) / kCourseCellTiles;
        const int dcy = int(dy) / kCourseCellTiles;
        CHECK(dSteps > 0
                  && ((dcy == 0 && dcx >= kCourseDim / 2)
                      || (dcx == kCourseDim - 1 && dcy < kCourseDim / 2)),
              "РУМБ NE выводит на свой угол — полустороны севера и востока");
    }

    return sm::test::report("course_field_test");
}
