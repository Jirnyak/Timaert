// Torus geometry helpers — wraparound for the macroworld.
#pragma once
#include <bit>
#include <cmath>
#include <cstdint>

namespace sm {

// ── ЗАКОН АДРЕСА (владелец, 2026-09-23) ──────────────────────────────────
// Дословно: «мир абсолютно всегда степень двойки и он всегда связный тор
// клеточный ЭТО ЯДРО ЯДРИЩЕ САМОЕ», «мир всегда степень двойки и всегда
// квадратный и дефолт 1024х1024 и всегда связный тор».
//
// Это ИНВАРИАНТ, а не текущее значение, и из него следует ВСЁ ниже:
// сторона — степень двойки ⇒ заворот есть МАСКА, а не деление; мир
// квадратен ⇒ у обеих осей одна маска; тор связный ⇒ шаг к соседу никогда
// не «выходит за край», он просто другой индекс.
//
// ПОЧЕМУ ЭТИ ЧЕТЫРЕ ДВЕРИ ЕСТЬ ЕДИНСТВЕННЫЙ АДРЕС КЛЕТКИ. Перепись
// 2026-09-23 насчитала в `src/` СЕМЬ разных механизмов заворота, 15
// именованных дверей (шесть со своим независимым кодом), 34 сырых сайта,
// семь шаблонов выражения индекса и 65 обходов соседей, расписывающих
// заворот руками каждый на свой лад — при том что скелет утверждал «одна
// свёртка на весь проект». Два из этих ответов — `ecs::cell_index` (маска)
// и `ResourceGrid::index` (деление) — дают одно и то же только потому, что
// мир случайно квадратный, и стоят при этом по-разному примерно в двадцать
// раз.
//
// ЦЕНА ДЕЛЕНИЯ, НАЗВАННАЯ ЧИСЛОМ: `wrapi` заворачивает через `std::int64_t
// %` по РАНТАЙМ-делителю, то есть аппаратным целочисленным делением
// (~20-40 тактов). Маска — один такт. Разница держалась ровно на том, что
// канонической двери не сообщили инвариант, который владелец назвал выше.
//
// ГРАНИЦА: `wrapi`/`wrapf` ниже остаются, но ТОЛЬКО для периодов, которые
// миром не являются — период шума в `zones.cpp` (`width/period`) степенью
// двойки не обязан быть. Свернуть КЛЕТКУ МИРА через `wrapi` — дефект.

// ИНВАРИАНТ МИРА ОДНОЙ ФУНКЦИЕЙ — чтобы каждый слой проверял его ОДИНАКОВО.
// Слой, родившийся не по инварианту, обязан отвечать fail-closed, а не «почти
// правильно»: адрес считается маской, и на неквадратном мире маска даёт не
// медленный ответ, а МОЛЧА НЕВЕРНЫЙ. Эта функция и есть место, где «мир
// всегда степень двойки и всегда квадрат» перестаёт быть комментарием.
inline bool world_shape_ok(int width, int height) {
    return width > 0 && width == height
        && std::has_single_bit(std::uint32_t(width));
}

// Сторона мира степенью двойки → её лог. Fail-closed: сторона, не бывшая
// степенью двойки, есть ошибка звонящего, а не место для падения.
inline int world_side_log2(int side) {
    return (side > 0 && std::has_single_bit(std::uint32_t(side)))
         ? std::countr_zero(std::uint32_t(side)) : 0;
}

// ОДНА ОСЬ МИРА. Для мест, где по существу нужна КООРДИНАТА, а не адрес —
// геометрия, отрисовка, диалог с чужим индексом. Заворот маской, потому что
// сторона мира есть степень двойки (ЗАКОН АДРЕСА). `side`, не бывшая стороной
// МИРА, сюда не передаётся: для периодов, которые миром не являются (период
// шума), есть `wrapi` ниже, и это ЕДИНСТВЕННОЕ его законное применение.
inline int wrap_axis(int v, int side) {
    return int(std::uint32_t(v) & (std::uint32_t(side) - 1u));
}

// АДРЕС КЛЕТКИ — ОДНО ЧИСЛО. Заворот обеих осей — одна маска (мир квадратен).
inline std::uint32_t cell_of(int x, int y, int side) {
    const std::uint32_t m = std::uint32_t(side) - 1u;
    return ((std::uint32_t(y) & m) << world_side_log2(side))
         |  (std::uint32_t(x) & m);
}
inline int cell_x(std::uint32_t idx, int side) {
    return int(idx & (std::uint32_t(side) - 1u));
}
inline int cell_y(std::uint32_t idx, int side) {
    return int((idx >> world_side_log2(side)) & (std::uint32_t(side) - 1u));
}

// ШАГ К СОСЕДУ — ТА САМАЯ ДВЕРЬ, КОТОРОЙ В ПРОЕКТЕ НЕ БЫЛО. Ради неё всё и
// затевалось: 65 обходов соседей в `src/` заворачивались руками, потому что
// индексная арифметика на торе НЕ замкнута сама по себе — `idx + 1` есть
// сосед справа ВЕЗДЕ, кроме правого края, где он молча перепрыгивает на
// следующую строку, а `idx + side` — сосед снизу везде, кроме нижнего.
//
// Почему здесь нет извлечения x: младшие биты индекса И ЕСТЬ x, поэтому
// `(idx + dx) & mask` заворачивает x, не трогая y. И почему `dx` любой знак
// и любая величина: сторона делит 2^32 нацело (она степень двойки), значит
// беззнаковый заворот по 2^32 согласован с маской по стороне — отрицательный
// шаг приходит правильным сам, без ветки.
inline std::uint32_t cell_step(std::uint32_t idx, int dx, int dy, int side) {
    const std::uint32_t m = std::uint32_t(side) - 1u;
    const int sh = world_side_log2(side);
    const std::uint32_t x = (idx + std::uint32_t(dx)) & m;
    const std::uint32_t y = ((idx >> sh) + std::uint32_t(dy)) & m;
    return (y << sh) | x;
}

// THE wrap. Six copies of this function lived in macro/ — `wrap_index` twice,
// `wrap_cell`, `wrap_coord`, `pf_wrap` — one per author who needed a torus and
// did not look for the one that existed. That is six chances to forget the
// modulo on the day a new field is written, and a forgotten modulo IS a seam
// (CANON.md S1: seamlessness is a property of construction).
//
// It takes the strongest body of the six: a 64-bit intermediate, so a
// coordinate far outside the map cannot overflow on the way in, and a
// fail-closed answer for a degenerate size, because `v % 0` is undefined
// behaviour and a world with no width is a caller's bug, not a crash site.
inline int wrapi(int v, int size) {
    if (size <= 0) return 0;
    std::int64_t m = std::int64_t(v) % std::int64_t(size);
    if (m < 0) m += size;
    return int(m);
}

// THE float wrap. Contract: the answer lies in [0, size) — HALF-OPEN, `size`
// itself is never returned. The half-open half of that had to be written out
// and then enforced, because the obvious spelling does not deliver it: for a
// small negative `v` (say −1e-7 on a 1024-wide world) `fmod` returns a tiny
// negative, `m + size` rounds UP to exactly `size` in float, and the caller
// gets a coordinate one past the last cell. That is the `next_f01()` hole
// again (rng.h, problems.md) — a range documented open at the top and closed
// by rounding — and at this door it is a seam: `int(wrapf(...))` indexes the
// row after the last one.
inline float wrapf(float v, float size) {
    float m = std::fmod(v, size);
    if (m < 0) m += size;
    if (m >= size) m = 0.0f;   // rounded up onto the seam — that IS the origin
    return m;
}

// THE signed shortest offset between two points of a wrapped axis: which way,
// and how far, is `to − from` when there is no long way round. `std::remainder`
// is the whole answer — it folds ANY difference into (−period/2, +period/2],
// however many worlds apart the two numbers drifted.
//
// The UI carried two copies of this (macro_overlay.cpp, map_screen.cpp) written
// as a single conditional subtraction, which corrects exactly ONE period: pan a
// map page past ~1.5 world widths and every landmark, pin and player mark
// silently misses by a whole world and is culled off-screen — the "a marker
// cannot be found across the seam" report.
inline float torus_delta(float d, float period) {
    return period > 0.0f ? std::remainder(d, period) : d;
}

// КРАТЧАЙШИЙ РАЗМАХ ПО ОДНОЙ ОСИ — одна формула на весь проект, в двух
// представлениях. Половинный размах был расписан руками ЧЕТЫРЕЖДЫ внутри
// этого самого файла (`torus_dist`, `torus_dist_sq`, `torus_bearing`,
// `torus_step_toward`) и ещё дважды снаружи — `politik.cpp torus_dist2` и
// `pathfinding.cpp octile_torus`. Шесть тел одной арифметики: ровно та
// форма, из-за которой UI однажды промахивался мимо метки на целый мир.
inline float torus_span(float a, float b, float period) {
    const float d = std::fabs(a - b);
    return d > period * 0.5f ? period - d : d;
}
// Знаковая версия для целых осей: «куда и насколько», короткой стороной.
inline int torus_offset(int from, int to, int period) {
    int d = to - from;
    if (d >  period / 2) d -= period;
    else if (d < -period / 2) d += period;
    return d;
}

inline float torus_dist(float ax, float ay, float bx, float by, float w, float h) {
    const float dx = torus_span(ax, bx, w);
    const float dy = torus_span(ay, by, h);
    return std::sqrt(dx * dx + dy * dy);
}

inline float torus_dist_sq(float ax, float ay, float bx, float by, float w, float h) {
    const float dx = torus_span(ax, bx, w);
    const float dy = torus_span(ay, by, h);
    return dx * dx + dy * dy;
}

// Unit bearing (direction) from (fx,fy) to (tx,ty) on a w×h torus, using the
// shortest wrapped delta. Writes the normalized direction into ux,uy and
// returns true; when the two points coincide it writes {0,0} and returns
// false (no meaningful bearing). Uses the same integer half-extent wrap as
// torus_dist_sq so bearings agree with the distance metric.
inline bool torus_bearing(int fx, int fy, int tx, int ty, int w, int h,
                          float& ux, float& uy) {
    const int dx = torus_offset(fx, tx, w);
    const int dy = torus_offset(fy, ty, h);
    const float len = std::sqrt(float(dx) * float(dx) + float(dy) * float(dy));
    if (len < 1e-6f) { ux = 0.0f; uy = 0.0f; return false; }
    ux = float(dx) / len;
    uy = float(dy) / len;
    return true;
}

// True when the bearing f→b is a shallow, SAME-direction fan off the bearing
// f→a — i.e. the two roads leaving f point nearly the same way, so a second
// road to b would read as a doubled diagonal of the road to a. Opposite
// bearings (dot < 0, a genuine alternate route) and coincident points return
// false. `cosThreshold` is cos(min separation angle): closer to 1 prunes only
// the most parallel fans, closer to 0 prunes wider ones.
inline bool torus_bearings_parallel(int fx, int fy, int ax, int ay,
                                     int bx, int by, int w, int h,
                                     float cosThreshold) {
    float ux, uy, vx, vy;
    if (!torus_bearing(fx, fy, ax, ay, w, h, ux, uy)) return false;
    if (!torus_bearing(fx, fy, bx, by, w, h, vx, vy)) return false;
    return (ux * vx + uy * vy) > cosThreshold;
}

struct Step { int nx, ny; };
inline Step torus_step_toward(int fx, int fy, int tx, int ty, int w, int h) {
    const int dx = torus_offset(fx, tx, w);
    const int dy = torus_offset(fy, ty, h);
    int nx = fx + (dx == 0 ? 0 : (dx > 0 ? 1 : -1));
    int ny = fy + (dy == 0 ? 0 : (dy > 0 ? 1 : -1));
    return {wrapi(nx, w), wrapi(ny, h)};
}

} // namespace sm
