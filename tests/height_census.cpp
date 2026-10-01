// HEIGHT CENSUS — ход 0 наряда M-192: прибор, который обязан отработать ДО
// первой правки генератора гор (вердикт владельца 2026-10-01; отдельное его
// требование — «прибор обязан сохранять саму карту высот картинкой»).
//
// Он отвечает на три вопроса о ПОЛЕ высот макромира, и каждый ответ становится
// ПРИЁМОЧНЫМ числом для ридж-октав и кривой переноса, которые идут следом:
//
//   1. ГДЕ ЛЕЖИТ ЭНЕРГИЯ. Пирамида скользящих средних по тору делит дисперсию
//      поля на полосы масштабов (1–2, 2–4, … 128–256, ≥256 клеток). Аналитика
//      NEXT_SESSION — «мельчайшая октава ≈3 % диапазона, то есть ~24 м ряби на
//      4 км» — здесь ПРЕДСКАЗАНИЕ, которое этот проход либо подтверждает, либо
//      убивает. Приёмка ридж-работы формулируется против этих полос: «энергия
//      на 2–4 клетках была X %, стала Y %».
//   2. НАСКОЛЬКО ОНО КРУТО. |Δh| между 4-соседями по суше — в метрах на клетку
//      и в градусах, p50/p90/p99/max, по всей карте и по одному горному биому.
//      Клетка — 1024 тайла (`kCellSize@src/sub/map_data.h`); субмир мерит Z в
//      метрах, а XY в тайлах, то есть «1 тайл = 1 м» есть собственная условность
//      движка, и градусы ниже её проговаривают вслух.
//   3. ДО КАКОЙ ВЫСОТЫ ОНО РЕАЛЬНО ДОХОДИТ. Перцентили поля по суше. Параметр
//      кривой переноса фитуется по НИМ, и никогда по номинальной 1.0: fBm своих
//      границ не достигает, и кривая, посаженная на 1.0, молча выдала бы мир на
//      3.7 км там, где одобрено 9.9.
//
// ИЗМЕРИТЕЛЬНЫЙ ПОЛ, НАЗВАННЫЙ ВСЛУХ: поле хранится БАЙТОМ, поэтому мельчайший
// выразимый шаг — 1/255 = 5.88 м, то есть 0.33° на клетку. Все числа ниже
// квантованы этим, и сам пол есть довод за `uint16`-карту высот: с кривой
// переноса тот же байт стоит ~52 м на горной линии.
//
// Река здесь не мешает и не прячется: `generate_terrain` врезает русла НИЖЕ
// уровня моря, поэтому карбованные клетки выпадают из маски суши сами, а их
// число печатается отдельной строкой — чтобы «сколько поля съели реки» было
// видно, а не предполагалось.
//
// СОЗНАТЕЛЬНО НЕ ctest, по причине `balance_run`: это ЗАМЕР, А НЕ ВЕРДИКТ.
// Запуск: ./build/height_census [сид …]   (по умолчанию пять сидов ниже)
// Картинки: $TIMAERT_CENSUS_DIR или /tmp.
#include "macro/map_generator.h"
#include "sub/height.h"     // kHeightScaleM — ЕДИНСТВЕННАЯ дверь метров
#include "sub/map_data.h"   // kCellSize — ширина клетки в тайлах
#include "tables/biomes.h"

// PNG-энкодер. Вендорный заголовок спотыкается о
// -Wmissing-field-initializers / -Wdeprecated (свои `{ 0 }` и один sprintf) —
// глушим локально, как в `app/smoke.cpp`, чтобы сборка осталась чистой без
// правки третьей стороны.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// Горизонтальная ширина клетки в метрах. Не новая константа: это ровно
// `kCellSize` тайлов при условности движка «тайл = метр» (см. шапку).
constexpr float kCellSpanM = float(sm::sub::kCellSize);

// Шаг байтовой карты в метрах — измерительный пол этого прибора.
constexpr float kByteStepM = sm::sub::kHeightScaleM / 255.0f;

// Полная шкала карты уклонов: 1024 м подъёма на клетку = 45°. Кодируется
// ЛОГАРИФМОМ, потому что кривая переноса меняет уклоны в десятки раз, а
// картинка «до» и «после» обязана остаться сравнимой по одной и той же шкале:
// линейная шкала после кривой была бы сплошным белым.
constexpr float kSlopeFullScaleM = kCellSpanM;

float percentile(std::vector<float>& sorted, float q) {
    if (sorted.empty()) return 0.0f;
    const std::size_t i = std::size_t(q * float(sorted.size() - 1) + 0.5f);
    return sorted[std::min(i, sorted.size() - 1)];
}

float degrees_per_cell(float riseM) {
    return std::atan(riseM / kCellSpanM) * 57.2957795f;
}

// Скользящее среднее s×s по ТОРУ, разделимое, бегущей суммой: O(N) на масштаб.
// Именно скользящее, а не блочное прореживание: блок теряет фазу и завышает
// мелкие полосы на решётке, а нам нужна доля дисперсии, а не картинка.
std::vector<float> box_blur_torus(const std::vector<float>& src, int w, int h,
                                  int s) {
    std::vector<float> mid(src.size(), 0.0f), out(src.size(), 0.0f);
    const int half = s / 2;
    const auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
    for (int y = 0; y < h; ++y) {
        const std::size_t row = std::size_t(y) * std::size_t(w);
        double sum = 0.0;
        for (int k = 0; k < s; ++k) sum += src[row + std::size_t(wrap(k - half, w))];
        for (int x = 0; x < w; ++x) {
            mid[row + std::size_t(x)] = float(sum / double(s));
            sum -= src[row + std::size_t(wrap(x - half, w))];
            sum += src[row + std::size_t(wrap(x - half + s, w))];
        }
    }
    for (int x = 0; x < w; ++x) {
        double sum = 0.0;
        for (int k = 0; k < s; ++k)
            sum += mid[std::size_t(wrap(k - half, h)) * std::size_t(w) + std::size_t(x)];
        for (int y = 0; y < h; ++y) {
            out[std::size_t(y) * std::size_t(w) + std::size_t(x)] = float(sum / double(s));
            sum -= mid[std::size_t(wrap(y - half, h)) * std::size_t(w) + std::size_t(x)];
            sum += mid[std::size_t(wrap(y - half + s, h)) * std::size_t(w) + std::size_t(x)];
        }
    }
    return out;
}

double variance_of(const std::vector<float>& f) {
    if (f.empty()) return 0.0;
    double mean = 0.0;
    for (float v : f) mean += double(v);
    mean /= double(f.size());
    double acc = 0.0;
    for (float v : f) { const double d = double(v) - mean; acc += d * d; }
    return acc / double(f.size());
}

struct SeedResult {
    std::uint32_t seed = 0;
    float landFrac = 0.0f;
    float mtnFrac = 0.0f;
    float riverFrac = 0.0f;
    float p50 = 0.0f, p90 = 0.0f, p99 = 0.0f, p999 = 0.0f, maxH = 0.0f;
    // ЗАПАС ПОЛЯ СВЕРХУ. Синтез складывает базовый fBm с континентальным
    // сдвигом и членом хребтов, а потом КЛАМПИТ в [0,1]: если верхний хвост
    // упирается в единицу, вершины мира — плоские столы по построению, и
    // никакая вертикальная шкала этого не лечит. Поэтому доля насыщения
    // печатается наравне с перцентилями: это вопрос «есть ли куда расти».
    float satFrac = 0.0f;     // суши ровно на 255
    float near1Frac = 0.0f;   // суши на >= 250 (0.98)
    float bedFrac = 0.0f;     // всей карты ровно на 0 — то же снизу
    float bandShare[9] = {};   // 1-2, 2-4, 4-8, 8-16, 16-32, 32-64, 64-128, 128-256, >=256
    float bandSigmaM[9] = {};
    float slopeP50 = 0.0f, slopeP90 = 0.0f, slopeP99 = 0.0f, slopeMax = 0.0f;
    float mtnP50 = 0.0f, mtnP90 = 0.0f, mtnP99 = 0.0f, mtnMax = 0.0f;
};

const char* kBandName[9] = {"1-2", "2-4", "4-8", "8-16", "16-32",
                            "32-64", "64-128", "128-256", ">=256"};

void write_pngs(const sm::TerrainData& td, std::uint32_t seed) {
    const int w = td.width, h = td.height;
    const char* dir = std::getenv("TIMAERT_CENSUS_DIR");
    char path[512];

    // (1) Само поле высот, как оно лежит в памяти — серым, без раскраски и без
    // подмешанной воды: вопрос владельца был про КАРТУ ВЫСОТ, а не про карту
    // мира, и любая подкраска сделала бы её вторым ответом на другой вопрос.
    std::vector<std::uint8_t> grey(std::size_t(w) * std::size_t(h));
    for (std::size_t i = 0; i < grey.size(); ++i) grey[i] = td.rgba[i * 4u];
    std::snprintf(path, sizeof(path), "%s/timaert_height_%u.png",
                  dir ? dir : "/tmp", seed);
    std::printf("  png  %s %s\n",
                stbi_write_png(path, w, h, 1, grey.data(), w) ? "wrote" : "FAILED",
                path);

    // (2) Карта УКЛОНОВ — прямой портрет того, что этот наряд лечит. Значение
    // клетки: максимум |Δh| по четырём соседям, в метрах подъёма на клетку,
    // по логарифмической шкале (0 м → чёрный, 1024 м/клетку = 45° → белый).
    std::vector<std::uint8_t> slope(std::size_t(w) * std::size_t(h));
    const float logFull = std::log2(1.0f + kSlopeFullScaleM);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float c = float(td.height_at(x, y)) / 255.0f;
            float worst = 0.0f;
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const float n = float(td.height_at(x + dx[k], y + dy[k])) / 255.0f;
                worst = std::max(worst, std::fabs(n - c));
            }
            const float m = worst * sm::sub::kHeightScaleM;
            const float t = std::log2(1.0f + m) / logFull;
            slope[std::size_t(y) * std::size_t(w) + std::size_t(x)] =
                std::uint8_t(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    std::snprintf(path, sizeof(path), "%s/timaert_slope_%u.png",
                  dir ? dir : "/tmp", seed);
    std::printf("  png  %s %s (log2, 45° = белый)\n",
                stbi_write_png(path, w, h, 1, slope.data(), w) ? "wrote" : "FAILED",
                path);
}

// Разъём для РАЗДЕЛЬНОГО замера слагаемых синтеза. Поле складывается из трёх
// членов (базовый fBm + континентальный сдвиг + член хребтов) и кламмится в
// [0,1]; когда верхний хвост упёрся в единицу, вопрос «чей это вклад» решается
// ТОЛЬКО тем, что член выключают и меряют заново. Ручки уже есть в
// `LayerParameters` — прибор их не изобретает, он их берёт.
float env_override(const char* name, float dflt) {
    if (const char* v = std::getenv(name)) return float(std::atof(v));
    return dflt;
}

SeedResult census_seed(std::uint32_t seed) {
    sm::LayerParameters lp{};
    lp.seed = seed;   // всё остальное — дефолты структуры, то есть мир игры
    lp.continentIntensity = env_override("TIMAERT_CENSUS_CONTINENT",
                                         lp.continentIntensity);
    lp.ridgeIntensity     = env_override("TIMAERT_CENSUS_RIDGE", lp.ridgeIntensity);
    lp.heightScale        = env_override("TIMAERT_CENSUS_GAMMA", lp.heightScale);
    sm::TerrainData td = sm::generate_terrain(1024, 1024, lp);
    // Биомы печёт `bake_biomes` — единственная дверь (её `generate_terrain` не
    // зовёт, это последний акт рождения мира у пекаря). Прибор зовёт ЕЁ, а не
    // сравнивает высоту с порогом сам: «горная ли клетка» обязано иметь один
    // ответ и здесь тоже.
    sm::bake_biomes(td);

    SeedResult r;
    r.seed = seed;
    const int w = td.width, h = td.height;
    const std::size_t n = std::size_t(w) * std::size_t(h);

    // Поле как единый плоский массив нормированных высот — то же, что читает
    // игра (байт/255), включая врезанные русла.
    std::vector<float> field(n);
    for (std::size_t i = 0; i < n; ++i) field[i] = float(td.rgba[i * 4u]) / 255.0f;

    // ── 3. ПЕРЦЕНТИЛИ ПО СУШЕ ────────────────────────────────────────────
    std::vector<float> land;
    land.reserve(n);
    std::size_t mtn = 0, river = 0, sat = 0, near1 = 0, bed = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b = td.rgba[i * 4u];
        if (td.riverData.size() == n && td.riverData[i] > 0) ++river;
        if (b == 0u) ++bed;
        if (td.is_water(std::uint32_t(i))) continue;        // ОДИН ответ про воду
        land.push_back(field[i]);
        if (b == 255u) ++sat;
        if (b >= 250u) ++near1;
        if (sm::biome_at_cell(td, std::uint32_t(i)) == sm::Biome::Mountain) ++mtn;
    }
    std::sort(land.begin(), land.end());
    r.landFrac  = float(land.size()) / float(n);
    r.mtnFrac   = float(mtn) / float(n);
    r.riverFrac = float(river) / float(n);
    r.satFrac   = land.empty() ? 0.0f : float(sat) / float(land.size());
    r.near1Frac = land.empty() ? 0.0f : float(near1) / float(land.size());
    r.bedFrac   = float(bed) / float(n);
    r.p50  = percentile(land, 0.50f);
    r.p90  = percentile(land, 0.90f);
    r.p99  = percentile(land, 0.99f);
    r.p999 = percentile(land, 0.999f);
    r.maxH = land.empty() ? 0.0f : land.back();

    // ── 1. ДИСПЕРСИЯ ПО МАСШТАБАМ ────────────────────────────────────────
    // Полоса [s, 2s) = var(сглаженное на s) − var(сглаженное на 2s): скользящее
    // среднее есть фильтр низких частот, поэтому разность и есть энергия,
    // которую масштаб s держит, а 2s уже потерял.
    const double varTotal = variance_of(field);
    double varAt[10];
    varAt[0] = varTotal;                       // s = 1 (само поле)
    int s = 2;
    for (int k = 1; k <= 9; ++k, s *= 2)
        varAt[k] = variance_of(box_blur_torus(field, w, h, s));
    for (int k = 0; k < 8; ++k) {
        const double bandVar = std::max(0.0, varAt[k] - varAt[k + 1]);
        r.bandShare[k]  = varTotal > 0.0 ? float(bandVar / varTotal * 100.0) : 0.0f;
        r.bandSigmaM[k] = float(std::sqrt(bandVar)) * sm::sub::kHeightScaleM;
    }
    r.bandShare[8]  = varTotal > 0.0 ? float(varAt[8] / varTotal * 100.0) : 0.0f;
    r.bandSigmaM[8] = float(std::sqrt(std::max(0.0, varAt[8]))) * sm::sub::kHeightScaleM;

    // ── 2. УКЛОНЫ МЕЖДУ СОСЕДЯМИ ─────────────────────────────────────────
    // Только пары СУША–СУША: пара с водой мерила бы глубину врезки русла или
    // берег, а вопрос наряда — крутизна рельефа.
    std::vector<float> slopes, mtnSlopes;
    slopes.reserve(n * 2u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (td.is_water(x, y)) continue;
            const float c = float(td.height_at(x, y)) / 255.0f;
            const bool cMtn = sm::biome_at_cell(td, x, y) == sm::Biome::Mountain;
            const int dx[2] = {1, 0}, dy[2] = {0, 1};
            for (int k = 0; k < 2; ++k) {
                const int nx = x + dx[k], ny = y + dy[k];
                if (td.is_water(nx, ny)) continue;
                const float nb = float(td.height_at(nx, ny)) / 255.0f;
                const float riseM = std::fabs(nb - c) * sm::sub::kHeightScaleM;
                slopes.push_back(riseM);
                if (cMtn || sm::biome_at_cell(td, nx, ny) == sm::Biome::Mountain)
                    mtnSlopes.push_back(riseM);
            }
        }
    }
    std::sort(slopes.begin(), slopes.end());
    std::sort(mtnSlopes.begin(), mtnSlopes.end());
    r.slopeP50 = percentile(slopes, 0.50f);
    r.slopeP90 = percentile(slopes, 0.90f);
    r.slopeP99 = percentile(slopes, 0.99f);
    r.slopeMax = slopes.empty() ? 0.0f : slopes.back();
    r.mtnP50 = percentile(mtnSlopes, 0.50f);
    r.mtnP90 = percentile(mtnSlopes, 0.90f);
    r.mtnP99 = percentile(mtnSlopes, 0.99f);
    r.mtnMax = mtnSlopes.empty() ? 0.0f : mtnSlopes.back();

    write_pngs(td, seed);
    sm::destroy_terrain(td);
    return r;
}

void print_seed(const SeedResult& r) {
    std::printf("\n=== СИД %u ===\n", r.seed);
    std::printf("  суша %.1f %% карты · биом Mountain %.2f %% · клеток рек %.2f %%\n",
                r.landFrac * 100.0f, r.mtnFrac * 100.0f, r.riverFrac * 100.0f);
    std::printf("  ВЫСОТА ПО СУШЕ (нормир. / метров при линейных %.0f м):\n",
                sm::sub::kHeightScaleM);
    const float sc = sm::sub::kHeightScaleM;
    std::printf("    p50 %.4f (%6.1f м)  p90 %.4f (%6.1f м)  p99 %.4f (%6.1f м)"
                "  p99.9 %.4f (%6.1f м)  max %.4f (%6.1f м)\n",
                r.p50, r.p50 * sc, r.p90, r.p90 * sc, r.p99, r.p99 * sc,
                r.p999, r.p999 * sc, r.maxH, r.maxH * sc);
    std::printf("    ЗАПАС СВЕРХУ: суши ровно на 1.0 — %.2f %%, на >=0.98 — %.2f %%"
                "  (дна ровно на 0.0: %.2f %% карты)\n",
                r.satFrac * 100.0f, r.near1Frac * 100.0f, r.bedFrac * 100.0f);
    std::printf("  ДИСПЕРСИЯ ПО МАСШТАБАМ (клеток; доля %% / сигма полосы в метрах):\n   ");
    for (int k = 0; k < 9; ++k) std::printf("  %8s", kBandName[k]);
    std::printf("\n   ");
    for (int k = 0; k < 9; ++k) std::printf("  %7.2f%%", r.bandShare[k]);
    std::printf("\n   ");
    for (int k = 0; k < 9; ++k) std::printf("  %7.1fм", r.bandSigmaM[k]);
    std::printf("\n  УКЛОН СОСЕД-СОСЕД, суша (м на клетку / градусов):\n");
    std::printf("    вся суша: p50 %5.1f м (%4.2f°)  p90 %5.1f м (%4.2f°)"
                "  p99 %6.1f м (%4.2f°)  max %6.1f м (%4.2f°)\n",
                r.slopeP50, degrees_per_cell(r.slopeP50),
                r.slopeP90, degrees_per_cell(r.slopeP90),
                r.slopeP99, degrees_per_cell(r.slopeP99),
                r.slopeMax, degrees_per_cell(r.slopeMax));
    std::printf("    горы:     p50 %5.1f м (%4.2f°)  p90 %5.1f м (%4.2f°)"
                "  p99 %6.1f м (%4.2f°)  max %6.1f м (%4.2f°)\n",
                r.mtnP50, degrees_per_cell(r.mtnP50),
                r.mtnP90, degrees_per_cell(r.mtnP90),
                r.mtnP99, degrees_per_cell(r.mtnP99),
                r.mtnMax, degrees_per_cell(r.mtnMax));
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::uint32_t> seeds;
    for (int i = 1; i < argc; ++i)
        seeds.push_back(std::uint32_t(std::strtoul(argv[i], nullptr, 10)));
    if (seeds.empty()) seeds = {1u, 7u, 999u, 12345u, 2026u};

    std::printf("HEIGHT CENSUS — поле высот макромира, 1024x1024, дефолты "
                "LayerParameters (то есть мир игры).\n");
    std::printf("Клетка %.0f м по горизонтали; вертикаль %.0f м на единицу; "
                "байтовый пол %.2f м (%.2f° на клетку).\n",
                kCellSpanM, sm::sub::kHeightScaleM, kByteStepM,
                degrees_per_cell(kByteStepM));
    {
        const sm::LayerParameters d{};
        std::printf("Синтез: континент %.2f · хребты %.2f · гамма %.2f "
                    "(дефолты %.2f/%.2f/%.2f; правятся TIMAERT_CENSUS_"
                    "CONTINENT/_RIDGE/_GAMMA).\n",
                    env_override("TIMAERT_CENSUS_CONTINENT", d.continentIntensity),
                    env_override("TIMAERT_CENSUS_RIDGE", d.ridgeIntensity),
                    env_override("TIMAERT_CENSUS_GAMMA", d.heightScale),
                    d.continentIntensity, d.ridgeIntensity, d.heightScale);
    }

    std::vector<SeedResult> all;
    for (std::uint32_t s : seeds) {
        all.push_back(census_seed(s));
        print_seed(all.back());
    }

    // ── ПРИЁМОЧНАЯ СТРОКА ────────────────────────────────────────────────
    // Среднее по сидам: один сид не число (§5 п.4), и приёмка ридж-работы
    // сравнивается именно с этими средними.
    SeedResult m{};
    const float inv = 1.0f / float(all.size());
    for (const SeedResult& r : all) {
        m.p50 += r.p50 * inv; m.p90 += r.p90 * inv; m.p99 += r.p99 * inv;
        m.p999 += r.p999 * inv; m.maxH += r.maxH * inv;
        m.mtnFrac += r.mtnFrac * inv; m.landFrac += r.landFrac * inv;
        m.satFrac += r.satFrac * inv; m.near1Frac += r.near1Frac * inv;
        m.slopeP50 += r.slopeP50 * inv; m.slopeP90 += r.slopeP90 * inv;
        m.slopeP99 += r.slopeP99 * inv; m.slopeMax += r.slopeMax * inv;
        m.mtnP50 += r.mtnP50 * inv; m.mtnP90 += r.mtnP90 * inv;
        m.mtnP99 += r.mtnP99 * inv; m.mtnMax += r.mtnMax * inv;
        for (int k = 0; k < 9; ++k) {
            m.bandShare[k] += r.bandShare[k] * inv;
            m.bandSigmaM[k] += r.bandSigmaM[k] * inv;
        }
    }
    m.seed = 0;
    std::printf("\n=== СРЕДНЕЕ ПО %zu СИДАМ — ЭТО И ЕСТЬ ПРИЁМОЧНАЯ БАЗА ===\n",
                all.size());
    std::printf("  суша %.1f %% · горный биом %.2f %%\n",
                m.landFrac * 100.0f, m.mtnFrac * 100.0f);
    std::printf("  высота по суше: p50 %.4f  p90 %.4f  p99 %.4f  p99.9 %.4f  max %.4f\n",
                m.p50, m.p90, m.p99, m.p999, m.maxH);
    std::printf("  насыщение сверху: %.2f %% суши на 1.0, %.2f %% на >=0.98\n",
                m.satFrac * 100.0f, m.near1Frac * 100.0f);
    std::printf("  энергия 2-4 клетки: %.2f %% (сигма %.1f м) | 4-8: %.2f %% (%.1f м)"
                " | 8-16: %.2f %% (%.1f м)\n",
                m.bandShare[1], m.bandSigmaM[1], m.bandShare[2], m.bandSigmaM[2],
                m.bandShare[3], m.bandSigmaM[3]);
    std::printf("  уклон гор: p50 %.1f м (%.2f°)  p90 %.1f м (%.2f°)  p99 %.1f м (%.2f°)\n",
                m.mtnP50, degrees_per_cell(m.mtnP50),
                m.mtnP90, degrees_per_cell(m.mtnP90),
                m.mtnP99, degrees_per_cell(m.mtnP99));
    return 0;
}
