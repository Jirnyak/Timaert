#include "macro/plates.h"

#include "core/rng.h"

#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// СКОЛЬКО ЯЧЕЕК РЕШЁТКИ СМОТРЕТЬ ВОКРУГ. Затравка гуляет в пределах СВОЕЙ
// ячейки (`kPlateJitter` = 1.0), поэтому ближайшая затравка лежит не дальше
// соседней ячейки, а вторая и третья — не дальше второй. Кольцо 2 (окно 5×5)
// накрывает обе с запасом; кольцо 1 давало бы верный ответ про ближайшую и
// врало бы про вторую ровно на тройных стыках, то есть ровно там, где стоит
// гряда.
constexpr int kPlateScanRing = 2;

// ШИРИНА ЯДРА СХОДИМОСТИ — ОДНА ЯЧЕЙКА РЕШЁТКИ. Сходимость есть вопрос о
// ПЛИТАХ, а не о шве, поэтому её ядро меряется пролётом плиты, а не шириной
// шва: сузить его до шва значило бы спросить «давят ли здесь» у двух соседей
// вместо всей округи, и ответ начал бы скакать на тройных стыках.
constexpr float kSqueezeSigmaLattice = 1.0f;

// Квадрат расстояния в единицах решётки.
inline float dist2(float ax, float ay, float bx, float by) {
    const float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

} // namespace

PlateMap make_plates(int side, std::uint32_t seed) {
    PlateMap map;
    // Мир меньше одного пролёта швов не имеет — отказ в точке РОЖДЕНИЯ, а не
    // выдуманная решётка из одной плиты (§5 п.3).
    if (side < kPlateSpanCells)
        return map;

    const int L = side / kPlateSpanCells;
    // Мир, которому не хватает ординалов, тоже отказывает В ТОЧКЕ РОЖДЕНИЯ, а
    // не обрезает решётку молча: обрезанная решётка дала бы плиты, зовущиеся
    // «никем», и гряды в случайных местах.
    if (L > kMaxPlates / L)
        return map;

    map.lattice = L;
    map.rows.resize(std::size_t(L) * std::size_t(L));

    for (int gy = 0; gy < L; ++gy) {
        for (int gx = 0; gx < L; ++gx) {
            const std::uint32_t ux = std::uint32_t(gx);
            const std::uint32_t uy = std::uint32_t(gy);
            // Три независимых розыгрыша одной дверью — `hash3@src/core/rng.h`
            // («Deterministic 32-bit hash (mix of x,y,seed)»). Своего хеша
            // здесь не заводится: второй словарь перемешивания был бы ровно
            // тем же дефектом, что второй словарь чего угодно.
            const std::uint32_t hJx = hash3(ux, uy, seed);
            const std::uint32_t hJy = hash3(ux, uy, seed ^ 0x9E3779B9u);
            const std::uint32_t hDr = hash3(ux, uy, seed ^ 0xD1F77E51u);
            // Те же 24 старших бита, что у `Rng::next_f01` — одна арифметика
            // доли на проект, и верхней единицы не бывает по построению.
            const float jx = float(hJx >> 8) * 0x1.0p-24f * kPlateJitter;
            const float jy = float(hJy >> 8) * 0x1.0p-24f * kPlateJitter;
            const float dr = float(hDr >> 8) * 0x1.0p-24f;

            PlateRow& row = map.rows[std::size_t(gy) * std::size_t(L) + std::size_t(gx)];
            row.u = (float(gx) + jx) / float(L);
            row.v = (float(gy) + jy) / float(L);
            row.drift = dr * 6.2831853f;   // полный оборот
            // АВТОРСКИЙ СТОЛБЕЦ ПУСТ, И ЭТО ЗНАЧЕНИЕ: «дизайнер не прислал»
            // тождественно «прислал ровно серое» (см. шапку заголовка).
            row.levelBias = 0.0f;
        }
    }
    return map;
}

PlateSample plate_sample(const PlateMap& map, float u, float v,
                         float seamWidthScale) {
    PlateSample out;
    if (!map.valid())
        return out;

    const int L = map.lattice;
    const float fl = float(L);
    // Запрос в единицах решётки. Координата приходит в долях тора и может
    // выйти за [0,1) — её искривил звонящий; ячейка ниже сворачивается сама,
    // поэтому сворачивать здесь нечего.
    const float gx = u * fl;
    const float gy = v * fl;
    const int bx = int(std::floor(gx));
    const int by = int(std::floor(gy));

    // Полуширина шва В ЕДИНИЦАХ РЕШЁТКИ. Превращается в вес ниже: вес плиты
    // спадает от ближайшей ровно за это расстояние.
    const float edge = std::max(1e-4f,
        (float(kPlateEdgeCells) / float(kPlateSpanCells)) * seamWidthScale);
    const float sigma2 = kSqueezeSigmaLattice * kSqueezeSigmaLattice;

    // Два прохода по одному окну: первый ищет ближайшую (от неё отсчитываются
    // веса), второй копит ответы. Окно 5×5 — 25 ячеек, проход однократный в
    // рождении мира.
    constexpr int kScanDim = 2 * kPlateScanRing + 1;
    float px[kScanDim * kScanDim], py[kScanDim * kScanDim];
    int   pid[kScanDim * kScanDim];
    int   n = 0;
    float dmin2 = 0.0f;
    int   nearest = -1;

    for (int oy = -kPlateScanRing; oy <= kPlateScanRing; ++oy) {
        for (int ox = -kPlateScanRing; ox <= kPlateScanRing; ++ox) {
            const int cx = bx + ox;
            const int cy = by + oy;
            // Заворот решётки. `L` есть сторона мира, делённая на пролёт, и
            // обе — степени двойки (ЗАКОН АДРЕСА), значит это маска; знаковый
            // остаток берётся явно, потому что `cx` законно отрицателен.
            const int wx = ((cx % L) + L) % L;
            const int wy = ((cy % L) + L) % L;
            const std::size_t ri = std::size_t(wy) * std::size_t(L) + std::size_t(wx);
            const PlateRow& row = map.rows[ri];
            // Представитель плиты РЯДОМ С ЗАПРОСОМ: её строка хранит затравку
            // в долях тора, а окно смотрит и за край — сдвиг `cx - wx` кратен
            // стороне решётки и возвращает затравку на свою копию тора.
            px[n] = row.u * fl + float(cx - wx);
            py[n] = row.v * fl + float(cy - wy);
            pid[n] = int(ri);
            const float d2 = dist2(gx, gy, px[n], py[n]);
            if (nearest < 0 || d2 < dmin2) {
                dmin2 = d2;
                nearest = n;
            }
            ++n;
        }
    }
    if (nearest < 0)
        return out;

    const float dmin = std::sqrt(dmin2);
    float wSum = 0.0f, biasAcc = 0.0f;
    float gSum = 0.0f, pushAcc = 0.0f;

    for (int i = 0; i < n; ++i) {
        const float d = std::sqrt(dist2(gx, gy, px[i], py[i]));
        const PlateRow& row = map.rows[std::size_t(pid[i])];

        // ── ВЕС ШВА: спадает от ближайшей плиты за ширину шва ──────────────
        // Внутри плиты ненулевой ровно один, у шва два-три, на тройном стыке
        // три — отсюда и плоская долина, и уступ, и отсутствие прямых резов.
        const float w = 1.0f - (d - dmin) / edge;
        if (w > 0.0f) {
            wSum += w;
            biasAcc += w * row.levelBias;
        }

        // ── СХОДИМОСТЬ: НЕПРЕРЫВНОЕ ПОЛЕ, А НЕ СВОЙСТВО ПАРЫ ──────────────
        // Пара ближайших плит меняется СКАЧКОМ на биссектрисе второго и
        // третьего соседа, поэтому сходимость, посчитанная по паре, режет
        // хребет прямой линией от каждого тройного стыка — поймано картинкой
        // уклонов, числами не видно. Вопрос здесь другой и он гладкий:
        // «сколько плит ДАВИТ в эту точку» — ход каждой соседки, спроецированный
        // на вектор к точке, под гладким ядром. Вектор (точка − затравка) не
        // нормируется: он гладок всюду, включая саму затравку, а нормированный
        // в ней не определён.
        const float g = std::exp(-dist2(gx, gy, px[i], py[i]) / sigma2);
        gSum += g;
        pushAcc += g * (std::cos(row.drift) * (gx - px[i])
                      + std::sin(row.drift) * (gy - py[i]));
    }

    out.levelBias = wSum > 0.0f ? biasAcc / wSum : 0.0f;
    // ШОВ ЕСТЬ ИЗБЫТОК ВЕСА. Один вес — глубина плиты (ноль); два и больше —
    // шов. Больше единицы не бывает нужно: гряда уже во всю силу.
    out.seam = std::min(1.0f, std::max(0.0f, wSum - 1.0f));
    out.squeeze = gSum > 0.0f ? pushAcc / gSum : 0.0f;
    out.nearest = PlateOrdinal(pid[nearest]);
    return out;
}

} // namespace sm
