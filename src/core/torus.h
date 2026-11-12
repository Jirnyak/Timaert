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
// map page past ~1.5 world widths and every place, pin and player mark
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
//
// СВЁРТКА ПОЛНАЯ С 2026-11-06: тело — `torus_delta` (remainder складывает
// ЛЮБУЮ разницу). Прежняя форма «|a−b|, и если больше полупериода — period −
// d» исправляла ровно ОДИН период и при |a−b| > period возвращала
// ОТРИЦАТЕЛЬНОЕ расстояние (посчитано: torus_span(0, 2000, 1024) = −976),
// которое квадрат у torus_dist_sq съедал МОЛЧА — ровно тот грех, в котором
// шапка torus_delta обвиняет одиночное условное вычитание. Для входов внутри
// периода ответ побайтово прежний (remainder точен по IEEE).
inline float torus_span(float a, float b, float period) {
    return std::fabs(torus_delta(a - b, period));
}

// СВЁРТКА ГОТОВОЙ ЦЕЛОЙ РАЗНИЦЫ КЛЕТОЧНОЙ ОСИ МИРА: маска сворачивает ЛЮБУЮ
// разницу в [0, side) ОДНИМ действием (сторона — степень двойки, ЗАКОН
// АДРЕСА), складка добивает знак — (−side/2, side/2]. Форма родилась руками
// как `wrap_dc` в frame_fill.cpp и оказалась СИЛЬНЕЕ тогдашней канонической
// двери (та исправляла один период); с 2026-11-06 тело ОДНО и живёт здесь.
// `side`, не бывшая стороной МИРА, сюда не передаётся — как у `wrap_axis`.
inline int torus_fold(int d, int side) {
    const int m = int(std::uint32_t(d) & (std::uint32_t(side) - 1u));
    return m > side / 2 ? m - side : m;
}

// Знаковая версия для целых осей: «куда и насколько», короткой стороной.
inline int torus_offset(int from, int to, int side) {
    return torus_fold(to - from, side);
}

// ЧЕБЫШЕВСКАЯ ДИСТАНЦИЯ КЛЕТОК НА ТОРЕ — одно тело вместо ДЕВЯТИ рукописных
// «abs + min(d, side−d)» (перепись 2026-11-06: ruins, spires, spawners ×2,
// npc_spawn, world_gen ×2, state — у всех входы уже в [0, side), поэтому
// сведение побайтово нейтрально). Мир квадратен — одна сторона на обе оси.
inline int torus_cheb_cells(int ax, int ay, int bx, int by, int side) {
    const int dx = std::abs(torus_fold(ax - bx, side));
    const int dy = std::abs(torus_fold(ay - by, side));
    return dx > dy ? dx : dy;
}

// ── ГЕОМЕТРИЯ НАД ИНДЕКСОМ: МАКРОМИР ГОВОРИТ КЛЕТКАМИ (M-462 / M-463) ────
// Вердикт владельца 2026-11-12, дословно: «никаких флоатов координат по
// системам у нас так - в макромире вся симуляция ИСКЛЮЧИТЕЛЬНО через индексы
// клеточек связного тора 1024х1024». Четыре двери выше отвечают «какая это
// клетка»; двери ниже —
// «как клетки лежат друг относительно друга», и берут они ИНДЕКСЫ, а не пару
// координат и не дробную точку. До них этот вопрос задавался флоатами: думка
// разворачивала индекс в пару `float x, y`, считала `torus_dist_sq` и резала
// результат обратно `int(...)` — сто таких преобразований на одном файле.
//
// ЦЕНА ШАГА — ПАРА ЦЕЛЫХ, ОДНА НА ВСЮ ИГРУ: ПРЯМОЙ 2, ДИАГОНАЛЬНЫЙ 3
// (вердикт владельца 2026-11-12 на вариант «прямой шаг стоит 2, диагональный
// 3, целые; расстояние = 2·max + min», дословно: «ну вроде это норм выглядит
// если математически обосновано строго и элегнатно то давай его»).
//
// ВЫВОД, И ОН НЕ «ВЫГЛЯДИТ ГЛАДКО»:
//   · клетка есть квадрат земли (субмир разворачивает её в квадрат тайлов),
//     значит центр диагонального соседа дальше в √2, и √2 лежит между двумя
//     родными метриками решётки: «король» даёт 1 (Чебышёв), «ладья» — 2
//     (Манхэттен). Полтора — их середина, а путь в полушагах есть буквально
//     их сумма: 2·max + min = max + (|dx| + |dy|);
//   · 3/2 — первая подходящая дробь разложения √2 в цепную дробь
//     (дальше 7/5, 17/12): лучшее приближение со знаменателем не больше двух;
//   · это МЕТРИКА: 3 ≤ 2 + 2 (диагональ не дороже двух прямых), а две
//     диагонали 3 + 3 не дешевле двух прямых 2 + 2, которые они заменяют;
//     значит кратчайший путь — «сколько можно диагоналей, остальное прямо»,
//     и формула ниже точна, а не оценка;
//   · отношение к евклиду лежит в отрезке [1, √5/2]: решётка НИКОГДА не
//     короче земли и длиннее не более чем на 11.8 % (на чистой диагонали —
//     на 6.1 %). При «короле» (диагональ = 1) она была короче до 29.3 %.
// Корня и приближения в коде при этом нет — только эти два целых.
inline constexpr int kStepStraight = 2;
inline constexpr int kStepDiagonal = 3;
static_assert(kStepStraight < kStepDiagonal
                  && kStepDiagonal <= 2 * kStepStraight,
              "цена шага обязана быть метрикой: диагональ дороже прямого, но "
              "не дороже двух прямых — иначе кратчайший путь перестаёт быть "
              "«диагонали, потом прямо» и формула пути врёт");

// СМЕЩЕНИЕ «ОТ → ДО» КОРОТКОЙ СТОРОНОЙ ТОРА, по осям, в клетках. Младшие
// биты индекса И ЕСТЬ x, поэтому разность индексов под маской есть разность
// x: заём из старших бит младших не трогает (та же причина, по которой
// `cell_step` не извлекает x). Складка — дверью `torus_fold`.
struct CellDelta { int dx, dy; };
inline CellDelta cell_delta(std::uint32_t from, std::uint32_t to, int side) {
    const int sh = world_side_log2(side);
    return {torus_fold(int(to - from), side),
            torus_fold(int((to >> sh) - (from >> sh)), side)};
}

// ПУТЬ МЕЖДУ КЛЕТКАМИ В ПОЛУШАГАХ (единица — половина прямого шага, чтобы
// обе цены были целыми): 2·max + min. Ноль значит «та же клетка». Этой дверью
// отвечается всякий вопрос «кто ближе» и «не дальше ли, чем R».
inline int cell_dist(std::uint32_t a, std::uint32_t b, int side) {
    const CellDelta d = cell_delta(a, b, side);
    const int ax = d.dx < 0 ? -d.dx : d.dx;
    const int ay = d.dy < 0 ? -d.dy : d.dy;
    const int lo = ax < ay ? ax : ay;
    const int hi = ax < ay ? ay : ax;
    return kStepStraight * hi + (kStepDiagonal - kStepStraight) * lo;
}

// «НЕ ДАЛЬШЕ R КЛЕТОК ПУТИ» — радиус авторится В КЛЕТКАХ (зрение 6, поводок
// 10), а полушаг остаётся внутренней единицей этой двери: модуль не обязан
// знать, во сколько полушагов обходится клетка.
inline bool cell_within(std::uint32_t a, std::uint32_t b, int cells, int side) {
    return cell_dist(a, b, side) <= cells * kStepStraight;
}

// СОСЕД В СТОРОНУ ЦЕЛИ — один шаг по знаку смещения каждой оси (диагональ,
// пока обе оси ненулевые). Для равных клеток отвечает самой клеткой.
inline std::uint32_t cell_toward(std::uint32_t from, std::uint32_t to,
                                 int side) {
    const CellDelta d = cell_delta(from, to, side);
    return cell_step(from, (d.dx > 0) - (d.dx < 0), (d.dy > 0) - (d.dy < 0),
                     side);
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
// Шаг на одну клетку к цели кратчайшей стороной. Заворот — МАСКОЙ: эта дверь
// ходит ТОЛЬКО по клеткам мира (единственный звонящий — макро-марш), а
// сторона мира всегда степень двойки (ЗАКОН АДРЕСА, «ядро ядрище»). До
// 2026-10-04 здесь стоял `wrapi` — int64-деление по рантайм-делителю на
// КАЖДОМ шаге марша, дефект по шапке этого же файла; прибор его не видел
// дважды: src/core не сканировался (закрыто), а имя `w` не в словаре
// wrapi-детектора (слепота по имени названа в шапке прибора, срок — тип
// стороны мира). Шаг ±1 от клетки в [0, side) даёт [-1, side], и маска
// степени двойки сворачивает оба края точно.
inline Step torus_step_toward(int fx, int fy, int tx, int ty, int w, int h) {
    const int dx = torus_offset(fx, tx, w);
    const int dy = torus_offset(fy, ty, h);
    int nx = fx + (dx == 0 ? 0 : (dx > 0 ? 1 : -1));
    int ny = fy + (dy == 0 ? 0 : (dy > 0 ? 1 : -1));
    return {nx & (w - 1), ny & (h - 1)};
}

} // namespace sm
