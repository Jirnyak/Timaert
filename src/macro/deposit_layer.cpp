#include "macro/deposit_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/field_noise.h"   // the ONE noise stack of the world's fields

namespace sm {

namespace {

// ── The FIELD law of geology (owner 2026-08-31, v72) ─────────────────────
// «Естественная диффузионная полевая генерация месторождений — шум; горы
// дают ВЕСА, не гейт». Ore is a CONCENTRATION FIELD: the same torus-tiling
// fbm stack the climate is made of (terrain_fbm, map_generator.h), shaped
// by the component's profile, weighted by its terrain affinity, and
// thresholded — a vein exists where the field crests, and its richness is
// the excess over the threshold, so nests are fat at the core and lean at
// the rims: the natural deposit profile. Nests ARE the clusters the mine
// generation folds into a feature-number cap («кап шахты использует локальное
// значение связности жил», вердикт 2026-10-23).
//
// ШЕСТЬ КОМПОНЕНТ — НЮАНС ГЕНЕРАЦИИ, НЕ ОСЬ МИРА (вердикт 2026-10-22 «жилы
// единые»): прежде каждая строка была РОДОМ жилы со своим полем; теперь это
// однократные буферы одного закона (ЗАКОН ГЕНЕРАЦИИ п.3) — глина ложится у
// воды, камень и металл по хребтам, редкие гребни реже частых, — а поле у
// них ОДНО: ёмкость клетки есть СУММА компонент. Что из жилы добывается,
// решает фича шахты, не геология.
//
// Profiles: Blob = the moisture field's own rounded lenses; Ridge =
// pow(1−|fbm|, 3) — the SAME trick the mountain chains are drawn with, so
// metal runs in veins along orogeny, which is where metal actually runs.
// Affinity: a WEIGHT, never a gate — metals concentrate where the land is
// high (weight = height⁴: mountains ~1, plains ~nothing but never zero),
// clay where rivers wet the lowland.
enum class OreProfile : std::uint8_t { Blob, Ridge };
enum class OreAffinity : std::uint8_t { MountainHeight, RiverMoisture };
struct DepositGenRow {
    OreProfile   profile;
    OreAffinity  affinity;
    // WHOLE tiles across the torus — a fractional period cuts a seam the
    // seed hides inside the map (the mountain-cliff scar of
    // map_generator.cpp, same lesson). Lower period = fewer, larger nests.
    float        period;
    float        threshold;   // the field's crest line: vein above, none below
    std::uint32_t salt;
};
// РЕДКОСТЬ ЖИВЁТ В ДВУХ КОЛОНКАХ, И ТРЕТЬЕЙ НЕ БЫВАЕТ (владелец, 2026-09-22:
// «это оч тупая система ВЫРЕЗАТЬ И УНИЧТОЖИТЬ ЭТО ОШИБКА… ТАКОГО ЗАМЫСЛА
// НЕТ»). Компонента редка тем, что её несут НЕМНОГИЕ КЛЕТКИ (порог +
// сродство). Сколько лежит в клетке — один закон на все компоненты.
// ПОРОГИ ПЕРЕКАЛИБРОВАНЫ 2026-10-23 (вердикт: «число шахт должно быть
// пропорционально числу поселений (не по точному числу а по порядку)») —
// прежняя геология давала 147 кластеров на мир против ~2270 поселений, то
// есть шахта была на полтора порядка реже города. Числа — баланс (ЗАКОН
// ПРИОРИТЕТА СТРУКТУРЫ), подобраны замером [mines] clusters/near.
constexpr DepositGenRow kDepositGen[] = {
    //  profile            affinity                     period thresh  salt
    {OreProfile::Blob,  OreAffinity::RiverMoisture, 96.0f, 0.40f, 0xC1A70000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 64.0f, 0.55f, 0x1F0E0000u},
    {OreProfile::Blob,  OreAffinity::MountainHeight, 64.0f, 0.40f, 0x570E0000u},
    // The rare crests: the highest bars — few nests, truly rare, but a
    // found one is a mining town's whole reason.
    {OreProfile::Ridge, OreAffinity::MountainHeight, 48.0f, 0.74f, 0x517E0000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 48.0f, 0.66f, 0xC0BB0000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 48.0f, 0.82f, 0x901D0000u},
};
constexpr int kDepositGenCount =
    int(sizeof(kDepositGen) / sizeof(kDepositGen[0]));

// ПЕРВЫЙ ЖИЛЕЦ `cell_step` — и ровно тот случай, ради которого дверь
// заводилась: обход 3×3 по тору. Здесь стояли ДВЕ рукописные свёртки
// `((v % n) + n) % n`, последние в макромире (перепись 2026-09-23).
// ВОПРОС БЫЛ ПРО ВОДУ, А СПРАШИВАЛОСЬ ПРО РЕКУ (M-211). Маска русла снесена:
// река перестала быть структурой мира, от неё остался прокоп в рельефе, и вода
// в нём — та же вода, что в море, по тому же единственному порогу. Россыпь
// ложится у воды, а не у «реки»: сорта воды в этом мире нет и заводить его
// значило бы вернуть снесённый второй спеллинг.
bool water_adjacent(const TerrainData& t, std::uint32_t at) {
    if (!t.has_rgba_storage() || !world_shape_ok(t.width, t.height))
        return false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (t.is_water(cell_step(at, dx, dy, t.width))) return true;
        }
    }
    return false;
}

// Вклад ОДНОЙ генераторной компоненты в ёмкость клетки.
std::int32_t component_virgin_at(const TerrainData& terrain,
                                 std::uint32_t seed,
                                 const DepositGenRow& g, std::uint32_t cell) {
    // The terrain WEIGHT (never a gate): metals ride height⁴ — mountains ~1,
    // plains vanishing but legal; clay rides the river-wetted lowland.
    float weight = 0.0f;
    switch (g.affinity) {
        case OreAffinity::MountainHeight: {
            const float h01 = field01_of(terrain.height_at(cell));
            weight = h01 * h01 * h01 * h01;
            break;
        }
        case OreAffinity::RiverMoisture: {
            const float m01 = field01_of(terrain.moisture_at(cell));
            weight = water_adjacent(terrain, cell) ? m01 : m01 / 8.0f;
            break;
        }
    }
    // Below-threshold weight cannot crest whatever the noise says — skip the
    // fbm for the 90% of cells it cannot help.
    if (weight <= g.threshold) return 0;
    // Точка карты для шума — геометрия ГЕНЕРАЦИИ: индекс раскладывается в
    // координаты здесь, у единственного, кому они нужны.
    const float ux = (float(cell_x(cell, terrain.width)) + 0.5f)
                   / float(terrain.width);
    const float uy = (float(cell_y(cell, terrain.width)) + 0.5f)
                   / float(terrain.height);
    const float fseed = float(seed % 100000u);
    const float n = terrain_fbm(ux * g.period, uy * g.period, 3, 0.5f,
                                g.period, fseed + float(g.salt & 0xFFFFu));
    const float c = weight
        * (g.profile == OreProfile::Ridge
               ? std::pow(1.0f - std::fabs(n), 3.0f)
               : n * 0.5f + 0.5f);
    if (c <= g.threshold) return 0;
    // СКОЛЬКО В КЛЕТКЕ — ОДИН ЗАКОН НА ВСЕ КОМПОНЕНТЫ: насколько шум
    // перевалил за свой гребень, растянутое на всю ширину клетки поля.
    return std::max<std::int32_t>(
        1, std::int32_t(float(kMaxFieldUnitsPerCell) * (c - g.threshold)
                        / (1.0f - g.threshold)));
}

} // namespace

void allocate_deposit_fields(DepositLayer& layer, int width, int height) {
    layer.width = width;
    layer.height = height;
    // A field is born with the world rather than on first use: one allocated
    // lazily is one whose first reader pays for everyone.
    layer.cells_.allocate(width, height);
}

void deposit_set(DepositLayer& layer, std::uint32_t cell, std::int32_t value) {
    // Кламп ширины клетки и счёт живых стоят в самой записи поля
    // (`ResourceGrid::write`); неродившееся поле запись глотает там же.
    layer.cells_.write(cell, value);
}

// ЁМКОСТЬ КЛЕТКИ — ЧИСТАЯ ФУНКЦИЯ ТЕРРАИНА И СИДА, как фертильность
// (владелец, 2026-09-22: «метал не убывает как и фертильность»). Сумма
// компонент; кламп к ширине клетки поля — у единственной двери записи
// (ResourceGrid::write), здесь сумма честная.
std::int32_t vein_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                            std::uint32_t cell) {
    if (!world_shape_ok(terrain.width, terrain.height)
        || !terrain.has_rgba_storage() || cell >= terrain.cell_count()) {
        return 0;
    }
    // Уровень моря НЕ ПАРАМЕТР этой двери: плоскость одна на мир и её несёт
    // сама карта (`TerrainData::seaLevel8`).
    if (terrain.is_water(cell)) return 0;
    std::int64_t sum = 0;
    for (int k = 0; k < kDepositGenCount; ++k) {
        sum += component_virgin_at(terrain, seed, kDepositGen[k], cell);
    }
    return std::int32_t(std::min<std::int64_t>(sum, kMaxFieldUnitsPerCell));
}

void build_deposit_layer(DepositLayer& layer, const TerrainData& terrain,
                         std::uint32_t seed) {
    layer.birthSeed = seed;
    layer.virginUnits = 0;
    allocate_deposit_fields(layer, terrain.width, terrain.height);
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return;
    }
    // Обход всех клеток мира идёт по индексу: порядок тот же, что у прежних
    // вложенных циклов по строкам, поэтому отпечаток геологии не сдвинулся.
    const std::uint32_t cells = std::uint32_t(layer.cells().cell_count());
    for (std::uint32_t c = 0; c < cells; ++c) {
        const std::int32_t amount = vein_virgin_at(terrain, seed, c);
        if (amount <= 0) continue;
        deposit_set(layer, c, amount);
        layer.virginUnits += amount;
    }
    // The geology fingerprint — the calibration eye of the field law.
    std::fprintf(stderr, "[deposits] vein=%lld/%d  (units/cells)\n",
                 (long long)layer.virginUnits, int(layer.cells().liveCells));
}

std::int32_t vein_cluster_cap(const DepositLayer& layer, std::uint32_t root) {
    if (!layer.cells().live()) return 0;
    if (layer.cells().at(root) == 0) return 0;
    // Однократные буферы вопроса (ЗАКОН ГЕНЕРАЦИИ п.3 — и у сезонного
    // визита цена та же: кластеры суть гнёзда в десятки клеток).
    std::vector<std::uint32_t> frontier{root};
    std::vector<std::uint32_t> seen{root};
    std::int64_t sum = 0;
    while (!frontier.empty()) {
        const std::uint32_t c = frontier.back();
        frontier.pop_back();
        sum += layer.cells().at(c);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const std::uint32_t n = cell_step(c, dx, dy, layer.width);
                if (layer.cells().at(n) == 0) continue;
                if (std::find(seen.begin(), seen.end(), n) != seen.end())
                    continue;
                seen.push_back(n);
                frontier.push_back(n);
            }
        }
    }
    return std::int32_t(std::min<std::int64_t>(sum, kMaxFieldUnitsPerCell));
}

} // namespace sm
