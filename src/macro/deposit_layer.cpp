#include "macro/deposit_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "core/field_noise.h"   // the ONE noise stack of the world's fields
#include "core/table_guard.h"
#include "macro/biomes.h"
// kGathererReach — the reach field's radius IS the profession's reach, read
// from the resource rows rather than restated here.
#include "macro/resource_field.h"

namespace sm {

namespace {

// ── The FIELD law of geology (owner 2026-08-31, v72) ─────────────────────
// «Естественная диффузионная полевая генерация месторождений — шум; горы
// дают ВЕСА, не гейт». Ore is a CONCENTRATION FIELD: the same torus-tiling
// fbm stack the climate is made of (terrain_fbm, map_generator.h), shaped
// by the kind's profile, weighted by the kind's terrain affinity, and
// thresholded — a vein exists where the field crests, and its richness is
// the excess over the threshold, so nests are fat at the core and lean at
// the rims: the natural deposit profile. Nests ARE the clusters the mine's
// consolidation folds («шахта всасывает связное месторождение»). The old
// law — a bare hash roll per cell — scattered lone veins that clustered
// with nothing; it fed the world for a day and starved the mint forever.
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
    DepositKind  kind;        // MUST equal the row's index (guard below)
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
// НЕТ»). Род редок тем, что его несут НЕМНОГИЕ КЛЕТКИ (порог + сродство), и
// дорог тем, что дорога его СТРОКА КАТАЛОГА. Сколько лежит в клетке — один
// закон на все роды, потому что камень, железо и золото суть просто ресурсы.
//
// ЗДЕСЬ СТОЯЛА КОЛОНКА `unitScale` — «сколько единиц в самой богатой клетке
// этого рода», по числу на род: золото 1, серебро 5, медь 30, железо 2048,
// глина 12288, камень 65536. Ни одно из них не было выведено из мира; шапка
// этой же таблицы признавалась, что они «откалиброваны против мировых итогов»
// УЖЕ МЁРТВОГО закона генерации, то есть подогнаны под вчерашний результат.
// Это нарушение ЗАКОНА КОНСТАНТ в чистом виде, и владелец снял колонку
// целиком, а не перетюнил.
//
constexpr DepositGenRow kDepositGen[kDepositKindCount] = {
    //                       profile            affinity              period thresh  salt
    {DepositKind::Clay,   OreProfile::Blob,  OreAffinity::RiverMoisture, 16.0f, 0.60f, 0xC1A70000u},
    {DepositKind::Iron,   OreProfile::Ridge, OreAffinity::MountainHeight, 8.0f, 0.82f, 0x1F0E0000u},
    {DepositKind::Stone,  OreProfile::Blob,  OreAffinity::MountainHeight, 8.0f, 0.60f, 0x570E0000u},
    // The mint metals: the lowest period and the highest bar — few nests,
    // truly rare, but a found one is a mining town's whole reason. Copper is
    // the base metal of the three, so its bar is the lowest of them.
    {DepositKind::Silver, OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.96f, 0x517E0000u},
    {DepositKind::Copper, OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.90f, 0xC0BB0000u},
    {DepositKind::Gold,   OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.995f, 0x901D0000u},
};
static_assert(rows_in_enum_order(kDepositGen, &DepositGenRow::kind),
              "kDepositGen row order must mirror DepositKind");

// ПЕРВЫЙ ЖИЛЕЦ `cell_step` — и ровно тот случай, ради которого дверь
// заводилась: обход 3×3 по тору. Здесь стояли ДВЕ рукописные свёртки
// `((v % n) + n) % n`, последние в макромире (перепись 2026-09-23).
bool river_adjacent(const TerrainData& t, int x, int y) {
    if (!t.has_river_storage() || !world_shape_ok(t.width, t.height))
        return false;
    const std::uint32_t at = cell_of(x, y, t.width);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (t.riverData[cell_step(at, dx, dy, t.width)] == 255) return true;
        }
    }
    return false;
}

} // namespace

const char* deposit_commodity_id(DepositKind kind) {
    return deposit_def(kind).commodityId;
}

void allocate_deposit_fields(DepositLayer& layer, int width, int height) {
    layer.width = width;
    layer.height = height;
    // A field is born with the world rather than on first use: one allocated
    // lazily is one whose first reader pays for everyone. The reach radius is
    // READ FROM THE ROW, never restated — that column is the whole reason the
    // neighbourhood question is a property of the resource and not of geology.
    // Every vein row reaches exactly as far as a gatherer walks. The NUMBER
    // is read here rather than the registry row, because the row's table sits
    // behind the growth laws and those pull the ECS into targets that do not
    // link it. The agreement is not left to trust: macro_stock.cpp carries a
    // static_assert that all four rows declare exactly this, so a row that
    // ever wants a different reach breaks the build at the table, which is
    // where such a decision would be made.
    for (int k = 0; k < kDepositKindCount; ++k) {
        layer.cells[std::size_t(k)].allocate(width, height, kGathererReach);
    }
}

// ЁМКОСТЬ КЛЕТКИ — ЧИСТАЯ ФУНКЦИЯ ТЕРРАИНА И СИДА, как фертильность
// (владелец, 2026-09-22: «метал не убывает как и фертильность… та же механика
// с шахтой как с полем»). Она НЕ УБЫВАЕТ НИКОГДА: убывает и заживает только
// разработанное, ровно как стоящий урожай на пашне.
//
// Почему это функция, а не второй массив: шум дорог, но спрашивают его ТОЛЬКО
// на сезонном срезе ходока роста (клеток/32 в день), а горячий путь читает
// сохранённое текущее число. Второе поле ёмкости стоило бы 25 МиБ и не купило
// бы ничего.
std::int32_t deposit_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                               float seaLevel, DepositKind kind, int x, int y) {
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return 0;
    }
    // Сторона МИРА → заворот маской (ЗАКОН АДРЕСА); wrapi здесь был
    // аппаратным делением на пути сезонного ходока.
    const int wx = wrap_axis(x, terrain.width);
    const int wy = wrap_axis(y, terrain.height);
    const std::uint8_t sea8 = std::uint8_t(seaLevel * 255.0f);
    if (terrain.is_water(wx, wy, sea8)) return 0;
    const DepositGenRow& g = kDepositGen[std::size_t(kind)];
    // The terrain WEIGHT (never a gate): metals ride height⁴ — mountains ~1,
    // plains vanishing but legal; clay rides the river-wetted lowland.
    float weight = 0.0f;
    switch (g.affinity) {
        case OreAffinity::MountainHeight: {
            const float h01 = float(terrain.height_at(wx, wy)) / 255.0f;
            weight = h01 * h01 * h01 * h01;
            break;
        }
        case OreAffinity::RiverMoisture: {
            const float m01 = float(terrain.moisture_at(wx, wy)) / 255.0f;
            weight = river_adjacent(terrain, wx, wy) ? m01 : m01 / 8.0f;
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
    // СКОЛЬКО В КЛЕТКЕ — ОДИН ЗАКОН НА ВСЕ РОДЫ: насколько шум перевалил за
    // свой гребень, растянутое на всю ширину клетки поля. Камень, железо и
    // золото считаются ОДИНАКОВО.
    return std::max<std::int32_t>(
        1, std::int32_t(float(kMaxFieldUnitsPerCell) * (c - g.threshold)
                        / (1.0f - g.threshold)));
}

DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed, float seaLevel) {
    DepositLayer layer;
    layer.width = terrain.width;
    layer.height = terrain.height;
    layer.birthSeed = seed;
    layer.birthSeaLevel = seaLevel;
    allocate_deposit_fields(layer, terrain.width, terrain.height);
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return layer;
    }
    // Генерация — ТА ЖЕ чистая функция, что потом залечивает клетку: один
    // закон ёмкости, а не два спеллинга одного шума.
    for (int y = 0; y < terrain.height; ++y) {
        for (int x = 0; x < terrain.width; ++x) {
            for (int k = 0; k < kDepositKindCount; ++k) {
                const std::int32_t amount = deposit_virgin_at(
                    terrain, seed, seaLevel, DepositKind(k), x, y);
                if (amount <= 0) continue;
                layer.cells[std::size_t(k)].write(x, y, amount);
                layer.virginUnits[std::size_t(k)] += amount;
            }
        }
    }
    // The geology fingerprint — the calibration eye of the field law and the
    // money supply's own birth certificate (a mint metal's units × its
    // catalog price). Printed off the REGISTRY, one line per row: the
    // hand-unrolled four-kind version silently stopped naming the metals the
    // day a fifth row was born, which is the one thing a calibration
    // instrument must never do (the fold-up lesson, problems §…).
    std::fprintf(stderr, "[deposits]");
    for (int k = 0; k < kDepositKindCount; ++k) {
        std::fprintf(stderr, " %s=%lld/%zu",
                     kDepositDefs[std::size_t(k)].commodityId,
                     (long long)layer.virginUnits[std::size_t(k)],
                     std::size_t(layer.cells[std::size_t(k)].liveCells));
    }
    std::fprintf(stderr, "  (units/cells)\n");
    return layer;
}

bool set_deposit_remaining(DepositLayer& layer, DepositKind kind,
                           int x, int y, std::int32_t remaining) {
    if (layer.width <= 0 || layer.height <= 0) return false;
    ResourceGrid& g = layer.grid(kind);
    // ГЕОЛОГИЮ ДОБЫЧА НЕ ИЗОБРЕТАЕТ: эта дверь двигает то, что уже стоит, и
    // в пустую клетку не пишет. Возврат выработанной клетки к жизни идёт
    // своей дверью — create_deposit, — и идёт он только там, где ЁМКОСТЬ
    // клетки положительна (macro_stock.cpp deposit_apply).
    //
    // (ЗАКОН АННИГИЛЯЦИИ, 2026-08-28, «выработанная жила — это жила, которой
    // больше нет», ОТМЕНЁН вердиктом владельца 2026-09-22: «метал не убывает
    // как и фертильность». Ноль в клетке больше не приговор — это выработка,
    // и она заживает.)
    if (g.at(x, y) == 0) return false;
    g.write(x, y, remaining <= 0 ? 0 : remaining);
    ++layer.revision;
    return true;
}

void create_deposit(DepositLayer& layer, DepositKind kind,
                    int x, int y, std::int32_t amount) {
    if (layer.width <= 0 || layer.height <= 0) return;
    // Every entry is ALIVE (annihilation law): genesis of an empty vein
    // would mint the "dry cell" state back into existence.
    if (amount <= 0) return;
    // Birth and refill are the same write; the grid tells them apart itself
    // (a cell going 0 -> non-zero is the birth) so neither this door nor any
    // future one can double-stamp a re-opened vein.
    layer.grid(kind).write(x, y, amount);
    ++layer.revision;
}

void restore_deposit_cells(DepositLayer& layer, const DepositLayer& loaded) {
    if (layer.width <= 0 || layer.height <= 0) return;
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        ResourceGrid& g = layer.cells[k];
        // Zero the whole field first: the loaded world is the world now, and a
        // vein the save does not carry is a vein the save says is gone.
        for (int y = 0; y < layer.height; ++y)
            for (int x = 0; x < layer.width; ++x) g.write(x, y, 0);
        loaded.cells[k].for_each_live(
            [&](std::uint32_t idx, std::int32_t remaining) {
                if (remaining <= 0) return;   // pre-annihilation dry cell: gone
                g.write(loaded.cells[k].x_of(idx), loaded.cells[k].y_of(idx),
                        remaining);
            });
        // virginUnits stays the LAYER's own: it was derived when this layer
        // was built from terrain + seed, which is exactly the baseline the
        // loaded world was born with. The reach field needs no re-derivation
        // either — it rode every write above.
    }
    ++layer.revision;
}

int consolidate_deposit_cluster(DepositLayer& layer, DepositKind kind,
                                int x, int y) {
    if (layer.width <= 0 || layer.height <= 0) return 0;
    ResourceGrid& g = layer.grid(kind);
    const std::uint32_t mineIdx = layer.wrap_index(x, y);
    if (g.at(x, y) == 0) return 0;   // no vein under the mine — nothing owns
    // BFS over live same-kind cells, 8-adjacent. Фронтир — ИНДЕКСЫ: сосед
    // есть cell_step (ЗАКОН АДРЕСА); прежний фронтир пар x/y существовал
    // ровно потому, что этой двери в проекте не было.
    std::vector<std::uint32_t> frontier{mineIdx};
    std::vector<std::uint32_t> seen{mineIdx};
    std::int64_t sum = g.at(x, y);
    int absorbed = 0;
    while (!frontier.empty()) {
        const std::uint32_t c = frontier.back();
        frontier.pop_back();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const std::uint32_t idx = cell_step(c, dx, dy, layer.width);
                if (std::find(seen.begin(), seen.end(), idx) != seen.end())
                    continue;
                const std::int32_t here = g.at_index(idx);
                if (here == 0) continue;
                seen.push_back(idx);
                sum += here;
                g.write(g.x_of(idx), g.y_of(idx), 0);
                                       // the absorbed vein leaves the map —
                                       // and its reach disc with it, inside
                                       // the write, not beside it
                ++absorbed;
                frontier.push_back(idx);
            }
        }
    }
    if (absorbed > 0) {
        // Clamped into the cell's own width — a cluster that somehow beats
        // int32 keeps the ceiling rather than wrapping (says so out loud).
        g.write(x, y, std::int32_t(std::min<std::int64_t>(sum, 0x7FFFFFFF)));
        ++layer.revision;
    }
    return absorbed;
}

} // namespace sm
