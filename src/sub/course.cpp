// Маршрутные поля курса (M-420) — бейк и обслуживание. Закон формы — шапка
// course.h; здесь живут три работы: растр солидов, агрегат весов чанками и
// целочисленная Дейкстра Диала с пулом записей, ограниченным ПО ПОСТРОЕНИЮ.
#include "sub/course.h"

#include <algorithm>

namespace sm::sub {
namespace {

// Чанк пересборки весов за один тик: 48 строк × 384 ячейки × 64 тайла ≈
// 1.2 млн чтений байта — тот же разряд работы, что один проход боевого
// поля (build_influence_field, каждый тик); полная свежесть весов — за
// kCourseDim/48 = 8 тиков. Баланс: двигается замером, не формой.
constexpr std::int32_t kCourseWeightRowsPerTick = 48;

// Растр солидных футпринтов в бит-на-тайл. Полный проход — только нулевым
// чанком поколения весов (после инвалидации), не в каждом тике.
void stamp_solids(CourseWorld& cw, const Structure* structs, std::size_t n) {
    cw.solidBits.fill(0ull);
    if (structs == nullptr) return;
    for (std::size_t i = 0; i < n; ++i) {
        const Structure& s = structs[i];
        if (!structure_is_solid(s.kind)) continue;
        // Палуба ЕСТЬ дорога (map_data.h: «a bridge IS road») — мост не
        // преграда курсу, он его лучшая строка.
        if (s.kind == Structure::Bridge) continue;
        // Пролёт на плоскости МИРА (zWorld) — настил над водой/головой,
        // низ его не стоит на земле; перемычка выше роста колонны тела —
        // проём, тело проходит под ней. Точность остаётся за resolve_step:
        // поле лишь советует.
        if (s.zWorld) continue;
        if (s.zBase >= kCourseLintelClearM) continue;
        const float hx = structure_half_x(s);
        const float hy = structure_half_y(s);
        const float r = hx + hy;   // мажоранта проекции OBB на оси
        const int x0 = std::max(0, int(s.x - r));
        const int x1 = std::min(kFullSize - 1, int(s.x + r));
        const int y0 = std::max(0, int(s.y - r));
        const int y1 = std::min(kFullSize - 1, int(s.y + r));
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                // Центр тайла внутри ориентированного футпринта — ноль
                // расстояния до поверхности (та же дверь геометрии, что у
                // руки и коллизии; цилиндр считается своим боксом — для
                // совета курса этого достаточно).
                if (structure_surface_dist2(s, float(tx) + 0.5f,
                                            float(ty) + 0.5f) <= 0.0f) {
                    const std::size_t ti =
                        std::size_t(ty) * std::size_t(kFullSize) +
                        std::size_t(tx);
                    cw.solidBits[ti >> 6] |= (1ull << (ti & 63u));
                }
            }
        }
    }
}

// Агрегат весов: средний kTileGroundWeight по 64 тайлам ячейки, солидный
// тайл — спецстрокой kCourseSolidTileWeight. ОДИН прайс-лист с макромаршем.
void rebuild_weight_rows(CourseWorld& cw, const std::uint8_t* tiles,
                         std::int32_t rowBudget) {
    const std::int32_t end =
        std::min(std::int32_t(kCourseDim), cw.weightRowsDone + rowBudget);
    for (std::int32_t cy = cw.weightRowsDone; cy < end; ++cy) {
        for (std::int32_t cx = 0; cx < kCourseDim; ++cx) {
            float sum = 0.0f;
            const std::int32_t tx0 = cx << kCourseCellShift;
            const std::int32_t ty0 = cy << kCourseCellShift;
            for (std::int32_t ty = 0; ty < kCourseCellTiles; ++ty) {
                const std::size_t row =
                    std::size_t(ty0 + ty) * std::size_t(kFullSize) +
                    std::size_t(tx0);
                for (std::int32_t tx = 0; tx < kCourseCellTiles; ++tx) {
                    const std::size_t ti = row + std::size_t(tx);
                    const bool solid =
                        ((cw.solidBits[ti >> 6] >> (ti & 63u)) & 1ull) != 0ull;
                    const std::uint8_t t = tiles[ti];
                    sum += solid ? kCourseSolidTileWeight
                                 : kTileGroundWeight[t < TILE_COUNT ? t : 0u];
                }
            }
            cw.weightQ[std::size_t(cy) * kCourseDim + std::size_t(cx)] =
                std::uint16_t(sum * (float(kCourseWeightQuantum) / 64.0f) +
                              0.5f);
        }
    }
    cw.weightRowsDone = end;
}

// Сиды румбового поля — ячейки кромки, чья внешняя сторона смотрит в румб:
// кардинал — вся своя сторона, диагональ — две полустороны своего угла.
template <typename Push>
void seed_rumb(std::uint8_t rumb, Push&& push) {
    const std::int32_t d = kCourseDim;
    const std::int32_t h = d / 2;
    const std::int32_t last = d - 1;
    const auto row = [&](std::int32_t y, std::int32_t xa, std::int32_t xb) {
        for (std::int32_t x = xa; x < xb; ++x) push(y * d + x);
    };
    const auto col = [&](std::int32_t x, std::int32_t ya, std::int32_t yb) {
        for (std::int32_t y = ya; y < yb; ++y) push(y * d + x);
    };
    switch (Dir(rumb)) {
        case Dir::N:  row(0, 0, d); break;
        case Dir::S:  row(last, 0, d); break;
        case Dir::E:  col(last, 0, d); break;
        case Dir::W:  col(0, 0, d); break;
        case Dir::NE: row(0, h, d); col(last, 0, h); break;
        case Dir::SE: col(last, h, d); row(last, h, d); break;
        case Dir::SW: row(last, 0, h); col(0, h, d); break;
        case Dir::NW: col(0, 0, h); row(0, 0, h); break;
    }
}

// Бейк одного поля — мультиисточниковая целочисленная Дейкстра Диала.
// Корректность очереди держат два факта из course.h: кольцо больше
// максимального ребра (две живые дистанции не делят корзину) и пул записей
// ёмкостью 8N (толчок случается не чаще улучшения по ребру, ребро
// улучшает не чаще одного раза — на осадке своего источника; осадка одна).
void bake_field(CourseWorld& cw, int slot) {
    auto& step = cw.fields[std::size_t(slot)].step;
    cw.dist.fill(kCourseNone);
    step.fill(kCourseNoStep);
    cw.bucketHead.fill(kCourseNone);
    std::uint32_t entryCount = 0;
    std::uint32_t live = 0;
    const auto push = [&](std::uint32_t node, std::uint32_t nd) {
        const std::uint32_t e = entryCount;
        ++entryCount;
        cw.entryNode[e] = node;
        const std::uint32_t b = nd & (kCourseBucketRing - 1u);
        cw.entryNext[e] = cw.bucketHead[b];
        cw.bucketHead[b] = e;
        ++live;
    };
    if (slot < kCourseRumbFields) {
        seed_rumb(std::uint8_t(slot), [&](std::int32_t c) {
            cw.dist[std::size_t(c)] = 0;
            push(std::uint32_t(c), 0);
        });
    } else {
        const std::uint32_t c = cw.meta[std::size_t(slot)].key;
        if (c >= std::uint32_t(kCourseCells)) return;
        cw.dist[c] = 0;
        push(c, 0);
    }
    std::uint32_t cur = 0;
    while (live > 0) {
        const std::uint32_t b = cur & (kCourseBucketRing - 1u);
        std::uint32_t e = cw.bucketHead[b];
        if (e == kCourseNone) {
            ++cur;
            continue;
        }
        cw.bucketHead[b] = kCourseNone;
        while (e != kCourseNone) {
            const std::uint32_t node = cw.entryNode[e];
            e = cw.entryNext[e];
            --live;
            if (cw.dist[node] != cur) continue;   // протухшая запись пула
            const std::int32_t y = std::int32_t(node) / kCourseDim;
            const std::int32_t x = std::int32_t(node) - y * kCourseDim;
            for (int dd = 0; dd < 8; ++dd) {
                const std::int32_t nx = x + kDirOffsets[dd][0];
                const std::int32_t ny = y + kDirOffsets[dd][1];
                if (nx < 0 || ny < 0 || nx >= kCourseDim || ny >= kCourseDim)
                    continue;   // окно — НЕ тор: кромка есть кромка
                const std::uint32_t n = std::uint32_t(ny * kCourseDim + nx);
                const std::uint32_t stepLen =
                    (dd & 1) ? kCourseStepDiagonal : kCourseStepCardinal;
                const std::uint32_t nd =
                    cur + std::uint32_t(cw.weightQ[n]) * stepLen;
                if (nd < cw.dist[n]) {
                    cw.dist[n] = nd;
                    // Шаг ячейки n — К её предшественнику на пути к цели.
                    step[n] = std::uint8_t((dd + 4) & 7);
                    push(n, nd);
                }
            }
        }
        ++cur;   // вес ≥ 8 квантов ⇒ толчки строго дальше текущей корзины
    }
}

} // namespace

std::unique_ptr<CourseWorld> make_course_world() {
    // Скобки пишут нули по всему блоку — страницы оплачены при рождении,
    // профиль пустой улицы и полной один (ЗАКОН СТАБИЛЬНОСТИ).
    return std::unique_ptr<CourseWorld>(new CourseWorld());
}

void course_invalidate(CourseWorld& cw) {
    cw.weightRowsDone = 0;
    for (auto& m : cw.meta) {
        if (m.state == kCourseFresh) m.state = kCourseWanted;
    }
}

void course_maintain(CourseWorld& cw, const std::uint8_t* tiles,
                     const Structure* structs, std::size_t structCount) {
    if (tiles == nullptr) return;
    if (cw.weightRowsDone < std::int32_t(kCourseDim)) {
        if (cw.weightRowsDone == 0) stamp_solids(cw, structs, structCount);
        rebuild_weight_rows(cw, tiles, kCourseWeightRowsPerTick);
        return;   // поля печём только по свежим весам
    }
    // Один бейк за тик: румбовые первыми (их делят все транзитные тела),
    // из точечных — самый свежий спрос.
    int pick = -1;
    for (int i = 0; i < kCourseRumbFields; ++i) {
        if (cw.meta[std::size_t(i)].state == kCourseWanted) {
            pick = i;
            break;
        }
    }
    if (pick < 0) {
        std::uint32_t best = 0;
        for (int i = kCourseRumbFields; i < kCourseFieldCount; ++i) {
            const auto& m = cw.meta[std::size_t(i)];
            if (m.state != kCourseWanted) continue;
            if (pick < 0 || m.lastUseTick >= best) {
                pick = i;
                best = m.lastUseTick;
            }
        }
    }
    if (pick >= 0) {
        bake_field(cw, pick);
        cw.meta[std::size_t(pick)].state = kCourseFresh;
    }
}

std::uint8_t course_dir_rumb(CourseWorld& cw, std::uint8_t rumb, float xTiles,
                             float yTiles) {
    if (rumb >= std::uint8_t(kCourseRumbFields)) return kCourseNoStep;
    auto& m = cw.meta[rumb];
    m.key = rumb;
    if (m.state == kCourseEmpty) m.state = kCourseWanted;
    if (m.state != kCourseFresh) return kCourseNoStep;
    return cw.fields[rumb].step[std::size_t(course_cell_of(xTiles, yTiles))];
}

std::uint8_t course_dir_point(CourseWorld& cw, float txTiles, float tyTiles,
                              float xTiles, float yTiles,
                              std::uint32_t nowTick) {
    const std::uint32_t key = std::uint32_t(course_cell_of(txTiles, tyTiles));
    int slot = -1;
    for (int i = kCourseRumbFields; i < kCourseFieldCount; ++i) {
        const auto& m = cw.meta[std::size_t(i)];
        if (m.state != kCourseEmpty && m.key == key) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // Свободный слот, иначе LRU-вытеснение СО СЧЁТЧИКОМ — кап точечных
        // полей не молчит (прецедент portalOverflows).
        int lru = -1;
        std::uint32_t lruTick = kCourseNone;
        for (int i = kCourseRumbFields; i < kCourseFieldCount; ++i) {
            const auto& m = cw.meta[std::size_t(i)];
            if (m.state == kCourseEmpty) {
                slot = i;
                break;
            }
            if (m.lastUseTick < lruTick) {
                lruTick = m.lastUseTick;
                lru = i;
            }
        }
        if (slot < 0) {
            slot = lru;
            ++cw.pointOverflows;
        }
        if (slot < 0) return kCourseNoStep;
        auto& m = cw.meta[std::size_t(slot)];
        m.key = key;
        m.state = kCourseWanted;
    }
    auto& m = cw.meta[std::size_t(slot)];
    m.lastUseTick = nowTick;
    if (m.state != kCourseFresh) return kCourseNoStep;
    return cw.fields[std::size_t(slot)]
        .step[std::size_t(course_cell_of(xTiles, yTiles))];
}

} // namespace sm::sub
