// RELIEF CENSUS — ПЕРЕПИСЬ РЕЛЬЕФА СУБМИРА ПО ДЛИНАМ ВОЛН, В МЕТРАХ.
//
// `height_census` меряет ПОЛЕ МАКРОМИРА — клетку 1024 м и крупнее. Под ним, на
// той же земле, лежит всё, что видит игрок ногами: деталь субмира с длинами
// волн в десятки и сотни метров. Её не мерил никто, и из-за этого два доклада
// владельца о ВИДЕ земли спорили друг с другом, оставаясь оба без числа:
//
//   «рядом с городом и реками дальний мир слишком платообразный и слишком
//    равномерный» (наряд M-207) — то есть на СОТНЯХ метров энергии мало;
//   «в 3х3 субмире слишком шумный рельеф … он должен быть более равномерный
//    (холмы норм но плавные)» — то есть на ДЕСЯТКАХ метров её слишком много.
//
// Оба утверждения про ОДИН спектр, и спорить о них без разложения по полосам
// значит спорить о вкусе. Этот прибор печатает полосу за полосой: сколько
// МЕТРОВ ряби живёт на каждой длине волны у равнины, холма, подножия и горы.
//
// МЕТОД. Диадная пирамида: уровень k есть карта, усреднённая блоками 2^k×2^k,
// и энергия полосы [2^k, 2^(k+1)) есть var(уровень k) − var(уровень k+1).
// Скользящее среднее — фильтр низких частот, поэтому разность и есть то, что
// масштаб 2^k держит, а 2^(k+1) уже потерял. Размеры — степени двойки по
// построению (клетка 1024 тайла), значит у пирамиды нет ни краевого случая, ни
// остатка. σ полосы печатается СРАЗУ В МЕТРАХ: карта субмира переводится через
// `height_m` ДО разложения, а не после, — кривая переноса нелинейна, и
// умножать дисперсию поля на наклон в медиане (как вынужден height_census) тут
// не нужно.
//
// ТАЙЛ ЕСТЬ МЕТР (условность движка, `kTileMeters` = 1), поэтому полоса
// «32–64 тайла» и есть «32–64 метра», и уклон между соседними тайлами есть
// уклон на метре.
//
// СОЗНАТЕЛЬНО НЕ ctest, по причине `balance_run` и `height_census`: это ЗАМЕР,
// а не вердикт. Запуск: ./build/relief_census [сид …]
#include "macro/map_generator.h"
#include "sub/base_generator.h"
#include "sub/height.h"
#include "sub/map_data.h"
#include "sub/gens/dispatch.h"
#include "tables/biomes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using namespace sm;

constexpr int kW = 1024;                       // сторона мира, клеток
constexpr int kCS = sm::sub::kCellSize;        // тайлов в клетке
constexpr int kBands = 11;                     // 1-2 … 1024

// Дисперсия выборки.
double variance_of(const std::vector<float>& v) {
    if (v.size() < 2) return 0.0;
    double s = 0.0;
    for (float f : v) s += double(f);
    const double mean = s / double(v.size());
    double acc = 0.0;
    for (float f : v) { const double d = double(f) - mean; acc += d * d; }
    return acc / double(v.size());
}

// Один шаг пирамиды: блок 2×2 → один сэмпл. Сторона всегда чётная (степень
// двойки), поэтому остатка нет и обрезать нечего.
std::vector<float> halve(const std::vector<float>& src, int side) {
    const int h = side / 2;
    std::vector<float> out(std::size_t(h) * std::size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < h; ++x) {
            const std::size_t a = std::size_t(2 * y) * std::size_t(side) + std::size_t(2 * x);
            const std::size_t b = a + std::size_t(side);
            out[std::size_t(y) * std::size_t(h) + std::size_t(x)] =
                0.25f * (src[a] + src[a + 1] + src[b] + src[b + 1]);
        }
    return out;
}

struct Sample {
    double bandSigmaM[kBands] = {};   // σ полосы, метры
    double slopeP50 = 0.0, slopeP90 = 0.0, slopeMax = 0.0;   // градусы на метр
    double spanM = 0.0;               // размах высот по клетке, метры
    double macroH = 0.0;
};

// Рельеф ОДНОЙ клетки, через настоящую дверь генератора субмира.
Sample measure_cell(const TerrainData& td, int cx, int cy, std::uint32_t seed) {
    Sample s{};
    float nbH[9];
    Biome nbB[9];
    Biome nb5[25];
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int i = (dy + 1) * 3 + (dx + 1);
            nbH[i] = field01_of(td.height_at(cx + dx, cy + dy));
            nbB[i] = biome_at_cell(td, cell_of(cx + dx, cy + dy, td.width));
        }
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            nb5[(dy + 2) * 5 + (dx + 2)] =
                biome_at_cell(td, cell_of(cx + dx, cy + dy, td.width));
    s.macroH = double(nbH[4]);

    // ЧЕРЕЗ ТУ ЖЕ ДВЕРЬ, ЧТО ЗОВЁТ МИР. База — только половина земли: фактуру
    // своего биома кладёт МОДУЛЬ, поверх готовой базы (`gens/gens.h`), и
    // прибор, меряющий одну базу, не увидел бы ни мочажин, ни барханов —
    // ровно то, на что владелец и жаловался. Содержания при этом нет: у
    // синтетического контекста ни места, ни фичи, то есть мерится чистая
    // земля, как и обещает шапка.
    sub::CellContext ctx{};
    ctx.cx = cx; ctx.cy = cy;
    ctx.worldCellsX = td.width; ctx.worldCellsY = td.height;
    ctx.macroHeight = nbH[4];
    ctx.seaLevel = field01_of(td.seaLevel16);
    ctx.biome = nbB[4];
    ctx.groundBiome = nbB[4];
    ctx.treeCount = 0;          // деревья прибору не нужны, и они не рельеф
    ctx.seed = sub::cell_seed(seed, cx, cy);
    ctx.worldSeed = seed;
    std::uint8_t nbFeature[9]{};
    sub::SubworldMapData md;
    sub::dispatch_generate(ctx, nbH, nbB, nb5, nbFeature, md);
    const std::vector<float>& h01 = md.heightmap;
    // В МЕТРЫ ДО РАЗЛОЖЕНИЯ: кривая переноса нелинейна, и раскладывать поле, а
    // потом умножать на наклон, значит мерить не ту величину.
    std::vector<float> m(h01.size());
    for (std::size_t i = 0; i < h01.size(); ++i) m[i] = sub::height_m(h01[i]);

    const auto mm = std::minmax_element(m.begin(), m.end());
    s.spanM = double(*mm.second - *mm.first);

    // Уклон между соседними тайлами — то, что глаз читает как «шумно».
    std::vector<float> slope;
    slope.reserve(std::size_t(kCS) * std::size_t(kCS));
    for (int y = 0; y < kCS; ++y)
        for (int x = 0; x + 1 < kCS; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(kCS) + std::size_t(x);
            slope.push_back(std::fabs(m[i + 1] - m[i]));
        }
    std::sort(slope.begin(), slope.end());
    const auto deg = [](float dzPerM) {
        return double(std::atan(dzPerM) * 57.2957795f);
    };
    s.slopeP50 = deg(slope[slope.size() / 2]);
    s.slopeP90 = deg(slope[slope.size() * 9 / 10]);
    s.slopeMax = deg(slope.back());

    double varAt[kBands + 1];
    std::vector<float> cur = m;
    int side = kCS;
    varAt[0] = variance_of(cur);
    for (int k = 1; k <= kBands; ++k) {
        cur = halve(cur, side);
        side /= 2;
        varAt[k] = variance_of(cur);
    }
    for (int k = 0; k < kBands; ++k)
        s.bandSigmaM[k] = std::sqrt(std::max(0.0, varAt[k] - varAt[k + 1]));
    return s;
}

struct Klass {
    const char* name;
    std::vector<Sample> got;
};

} // namespace

int main(int argc, char** argv) {
    std::vector<std::uint32_t> seeds;
    for (int i = 1; i < argc; ++i) seeds.push_back(std::uint32_t(std::atoi(argv[i])));
    if (seeds.empty()) seeds = {12345u, 777u, 2026u};

    std::printf(
        "RELIEF CENSUS — рельеф СУБМИРА по длинам волн, в метрах. Клетка %d "
        "тайлов, тайл = метр.\n"
        "Полоса [s, 2s) = var(усреднённое на s) − var(усреднённое на 2s); "
        "карта переведена в метры ДО разложения.\n"
        "Содержания (дороги, поселения, пашни) НЕТ — мерится чистая земля.\n\n",
        kCS);

    // Пятый род — не высотный, а СОДЕРЖАТЕЛЬНЫЙ: окно, в котором лежит клетка
    // русла. Он стоит отдельной строкой затем, что русло держит плоскость моря
    // на ЛЮБОЙ высоте (наряд M-197), то есть вносит в окно перепад, которого у
    // соседних клеток того же рода нет. Без этой строки его вклад размазался бы
    // по четырём верхним и стал бы «шумом рельефа вообще».
    Klass classes[7] = {{"РАВНИНА  ", {}}, {"ХОЛМ     ", {}},
                        {"ПОДНОЖИЕ ", {}}, {"ГОРА     ", {}},
                        {"У ВОДЫ   ", {}}, {"БОЛОТО   ", {}},
                        {"ПУСТЫНЯ  ", {}}};

    for (std::uint32_t seed : seeds) {
        LayerParameters p{};
        p.seed = seed;
        const TerrainData td = generate_terrain(kW, kW, p);
        const float sea = field01_of(td.seaLevel16);
        // Три клетки каждого рода на сид, набираются сканом с шагом — число
        // клеток класса не выводится, поэтому берётся первое, что встретилось,
        // и это честно названо: прибор мерит ФОРМУ, а не распределение.
        int want[7] = {3, 3, 3, 3, 3, 3, 3};
        // БЕРЕГ — СУША, У КОТОРОЙ В ОКНЕ 3×3 СТОИТ ВОДА. Прежде здесь
        // спрашивали маску русла, но M-211 снёс реку как структуру (вердикт
        // владельца 2026-10-03: «рек нет как структуры, от них остаются только
        // прокопы в рельефе, и агностично эмерджентно там вода»), — значит
        // вопрос о фактуре берега обязан идти к ОДНОМУ ответу про воду
        // (`is_water`), а не к носителю, которого нет. Класс от этого стал
        // ШИРЕ: он ловит и кромку моря, а не только борт прокопа, и потому
        // честно переименован — прибор мерит рельеф У ВОДЫ.
        const auto water_in_window = [&](int cx, int cy) {
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (td.is_water(cell_of(cx + dx, cy + dy, kW)))
                        return true;
            return false;
        };
        int left = 0;
        for (int w : want) left += w;
        for (int cy = 2; cy < kW - 2 && left > 0; cy += 7) {
            for (int cx = 2; cx < kW - 2; cx += 13) {
                const std::uint32_t idx = cell_of(cx, cy, kW);
                if (td.is_water(idx)) continue;
                const float h = field01_of(td.height_at(cx, cy));
                const Biome b = biome_at_cell(td, idx);
                int k = -1;
                // Биомы со СВОЕЙ фактурой идут первыми: у болота и пустыни
                // сегодня собственные генераторы шума рядом с общей стопкой, и
                // смешивать их с равниной значит прятать ровно тот вопрос,
                // ради которого прибор и зовут.
                if (b == Biome::Swamp)               k = 5;
                else if (b == Biome::Desert)         k = 6;
                else if (water_in_window(cx, cy))    k = 4;
                else if (b == Biome::Mountain)       k = 3;
                else if (h < sea + 0.06f)            k = 0;
                else if (h < sea + 0.18f)            k = 1;
                else if (h < sea + 0.32f)            k = 2;
                if (k < 0 || want[k] == 0) continue;
                --left;
                --want[k];
                classes[k].got.push_back(measure_cell(td, cx, cy, seed));
            }
        }
    }

    std::printf("%s", "          макроH   размах  | уклон тайл-тайл (°)   | "
                      "σ ПОЛОСЫ, метры — длина волны в метрах\n");
    std::printf("%s", "                   по кл.  |  p50    p90    max    | ");
    int lam = 1;
    for (int k = 0; k < kBands; ++k, lam *= 2) std::printf("%6d", lam * 2);
    std::printf("\n");
    for (Klass& c : classes) {
        if (c.got.empty()) { std::printf("%s — не встретилась\n", c.name); continue; }
        Sample a{};
        const double inv = 1.0 / double(c.got.size());
        for (const Sample& s : c.got) {
            a.macroH += s.macroH * inv;  a.spanM += s.spanM * inv;
            a.slopeP50 += s.slopeP50 * inv; a.slopeP90 += s.slopeP90 * inv;
            a.slopeMax += s.slopeMax * inv;
            for (int k = 0; k < kBands; ++k) a.bandSigmaM[k] += s.bandSigmaM[k] * inv;
        }
        std::printf("%s %.3f  %7.1f | %5.2f %6.2f %6.2f | ", c.name, a.macroH,
                    a.spanM, a.slopeP50, a.slopeP90, a.slopeMax);
        for (int k = 0; k < kBands; ++k) std::printf("%6.2f", a.bandSigmaM[k]);
        std::printf("   (%zu клеток)\n", c.got.size());
    }
    std::printf("\nСИДЫ:");
    for (std::uint32_t s : seeds) std::printf(" %u", s);
    std::printf("\n");
    return 0;
}
