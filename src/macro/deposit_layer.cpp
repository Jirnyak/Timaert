#include "macro/deposit_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
// generation folds into a worked cap («кап шахты использует локальное
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
constexpr DepositGenRow kDepositGen[] = {
    //  profile            affinity                     period thresh  salt
    {OreProfile::Blob,  OreAffinity::RiverMoisture, 16.0f, 0.60f, 0xC1A70000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 8.0f, 0.82f, 0x1F0E0000u},
    {OreProfile::Blob,  OreAffinity::MountainHeight, 8.0f, 0.60f, 0x570E0000u},
    // The rare crests: the lowest period and the highest bar — few nests,
    // truly rare, but a found one is a mining town's whole reason.
    {OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.96f, 0x517E0000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.90f, 0xC0BB0000u},
    {OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.995f, 0x901D0000u},
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
bool water_adjacent(const TerrainData& t, int x, int y) {
    if (!t.has_rgba_storage() || !world_shape_ok(t.width, t.height))
        return false;
    const std::uint32_t at = cell_of(x, y, t.width);
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
                                 const DepositGenRow& g, int wx, int wy) {
    // The terrain WEIGHT (never a gate): metals ride height⁴ — mountains ~1,
    // plains vanishing but legal; clay rides the river-wetted lowland.
    float weight = 0.0f;
    switch (g.affinity) {
        case OreAffinity::MountainHeight: {
            const float h01 = field01_of(terrain.height_at(wx, wy));
            weight = h01 * h01 * h01 * h01;
            break;
        }
        case OreAffinity::RiverMoisture: {
            const float m01 = field01_of(terrain.moisture_at(wx, wy));
            weight = water_adjacent(terrain, wx, wy) ? m01 : m01 / 8.0f;
            break;
        }
    }
    // Below-threshold weight cannot crest whatever the noise says — skip the
    // fbm for the 90% of cells it cannot help.
    if (weight <= g.threshold) return 0;
    const float ux = (float(wx) + 0.5f) / float(terrain.width);
    const float uy = (float(wy) + 0.5f) / float(terrain.height);
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
    // lazily is one whose first reader pays for everyone. The reach radius is
    // READ FROM THE ROW's home (kGathererReach lives beside the resource
    // rows): every vein reaches exactly as far as a gatherer walks.
    layer.cells.allocate(width, height, kGathererReach);
}

// ЁМКОСТЬ КЛЕТКИ — ЧИСТАЯ ФУНКЦИЯ ТЕРРАИНА И СИДА, как фертильность
// (владелец, 2026-09-22: «метал не убывает как и фертильность»). Сумма
// компонент; кламп к ширине клетки поля — у единственной двери записи
// (ResourceGrid::write), здесь сумма честная.
std::int32_t vein_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                            int x, int y) {
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return 0;
    }
    // Сторона МИРА → заворот маской (ЗАКОН АДРЕСА).
    const int wx = wrap_axis(x, terrain.width);
    const int wy = wrap_axis(y, terrain.height);
    // Уровень моря НЕ ПАРАМЕТР этой двери: плоскость одна на мир и её несёт
    // сама карта (`TerrainData::seaLevel8`).
    if (terrain.is_water(wx, wy)) return 0;
    std::int64_t sum = 0;
    for (int k = 0; k < kDepositGenCount; ++k) {
        sum += component_virgin_at(terrain, seed, kDepositGen[k], wx, wy);
    }
    return std::int32_t(std::min<std::int64_t>(sum, kMaxFieldUnitsPerCell));
}

DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed) {
    DepositLayer layer;
    layer.birthSeed = seed;
    allocate_deposit_fields(layer, terrain.width, terrain.height);
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return layer;
    }
    for (int y = 0; y < terrain.height; ++y) {
        for (int x = 0; x < terrain.width; ++x) {
            const std::int32_t amount = vein_virgin_at(terrain, seed, x, y);
            if (amount <= 0) continue;
            layer.cells.write(x, y, amount);
            layer.virginUnits += amount;
        }
    }
    // The geology fingerprint — the calibration eye of the field law.
    std::fprintf(stderr, "[deposits] vein=%lld/%d  (units/cells)\n",
                 (long long)layer.virginUnits, int(layer.cells.liveCells));
    return layer;
}

void create_deposit(DepositLayer& layer, int x, int y, std::int32_t amount) {
    if (layer.width <= 0 || layer.height <= 0) return;
    if (amount <= 0) return;
    layer.cells.write(x, y, amount);
    ++layer.revision;
}

} // namespace sm
