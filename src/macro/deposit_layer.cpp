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
    // Units per full point of excess concentration — calibrated so world
    // totals stay the order the hash law produced (silver especially: the
    // world's money supply IS its silver geology × catalog value 32).
    std::int32_t unitScale;
    std::uint32_t salt;
};
// КАМЕНЬ: 65536 → 32768 (2026-09-22, вместе с сужением клетки поля до uint16).
// Старое число было ровно НА ЕДИНИЦУ выше потолка uint16, и оно единственное
// во всей таблице не влезало. Перетюнёвка ничего не стоит, потому что в тот же
// день камень стал ЗАЖИВАТЬ: бесконечность во времени заменила бесконечность в
// запасе, и вторая перестала быть нужна. Мир камня вдвое меньше по единовре-
// менному запасу и по-прежнему неисчерпаем по времени (добыча за 512 дней —
// 740 тыс. против ~50 млн в земле).
//
// Thresholds and scales are CALIBRATED against the hash law's world totals
// (the fingerprint line below is the instrument): the money supply and the
// tool economy must not jump an order of magnitude because the SHAPE of
// geology changed. Targets (1024², seed-family means): clay ~1.2M units,
// iron ~0.8M, stone ~100M (quasi-infinite).
//
// THE MINT METALS' VALUE CEILING (owner verdict 2026-09-18, «пересчёт жил
// ÷10»): a metal's units × its catalog price is the world's coin ceiling in
// that metal, and the price is the mint yield (32) × the nominal it strikes.
// Silver went from nominal 1 to nominal 10 with the three-coin family, so
// its price went 32 → 320 and its VEINS are rescaled ÷10 to hold the same
// ceiling the balance runs were calibrated against (~16M of value).
//
// THE RARITY LADDER IS THE OWNER'S (2026-09-18): «медь чаще, серебро
// среднее, золото самое редкое». It is spelled in TWO columns, because
// rarity has two halves: the threshold decides HOW MANY cells carry the
// metal at all (copper 0.90 → thousands of nests, silver 0.96 → a couple of
// thousand, gold 0.995 → a few hundred), and unitScale decides how rich a
// nest is. Measured at 1024² ([deposits] fingerprint below is the
// instrument), one seed:
//   copper 87k units over 6.2k cells × price 128   ≈ 11.2M of value
//   silver  5k units over 2.3k cells × price 1280  ≈  6.4M
//   gold   278 units over 278 cells  × price 12800 ≈  3.6M
// — the ladder holds on both halves, and the world ceiling (~21M) is the
// order the single-metal world was calibrated at (~16M). Every number here
// is a balance-run tunable; the yield they are all divided against lives on
// the coin rows (items.cpp), not here.
constexpr DepositGenRow kDepositGen[kDepositKindCount] = {
    //                       profile            affinity              period thresh scale  salt
    {DepositKind::Clay,   OreProfile::Blob,  OreAffinity::RiverMoisture, 16.0f, 0.60f,  12288, 0xC1A70000u},
    {DepositKind::Iron,   OreProfile::Ridge, OreAffinity::MountainHeight, 8.0f, 0.82f,   2048, 0x1F0E0000u},
    {DepositKind::Stone,  OreProfile::Blob,  OreAffinity::MountainHeight, 8.0f, 0.60f,  32768, 0x570E0000u},
    // The mint metals: the lowest period and the highest bar — few nests,
    // truly rare, but a found one is a mining town's whole reason. Copper is
    // the base metal of the three, so its bar is the lowest of them.
    {DepositKind::Silver, OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.96f,      5, 0x517E0000u},
    {DepositKind::Copper, OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.90f,     30, 0xC0BB0000u},
    {DepositKind::Gold,   OreProfile::Ridge, OreAffinity::MountainHeight, 6.0f, 0.995f,     1, 0x901D0000u},
};
static_assert(rows_in_enum_order(kDepositGen, &DepositGenRow::kind),
              "kDepositGen row order must mirror DepositKind");

bool river_adjacent(const TerrainData& t, int x, int y) {
    if (!t.has_river_storage()) return false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int xi = ((x + dx) % t.width + t.width) % t.width;
            const int yi = ((y + dy) % t.height + t.height) % t.height;
            if (t.riverData[std::size_t(yi) * std::size_t(t.width) + std::size_t(xi)]
                == 255) {
                return true;
            }
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
// Сторож ширины: проверяется на СБОРКЕ (static_assert ниже), а функция
// существует затем, чтобы свидетель мог назвать закон вслух.
namespace {
constexpr bool all_kinds_fit_cell() {
    for (int k = 0; k < kDepositKindCount; ++k) {
        if (kDepositGen[std::size_t(k)].unitScale > kMaxFieldUnitsPerCell) {
            return false;
        }
    }
    return true;
}
}  // namespace
static_assert(all_kinds_fit_cell(),
              "род руды жирнее клетки поля (uint16): либо перетюнь unitScale, "
              "либо расширяй ВЕСЬ штабель полей — ширина у рядов одна");
bool deposit_kinds_fit_field_cell() { return all_kinds_fit_cell(); }

std::int32_t deposit_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                               float seaLevel, DepositKind kind, int x, int y) {
    if (terrain.width <= 0 || terrain.height <= 0
        || !terrain.has_rgba_storage()) {
        return 0;
    }
    const int wx = wrapi(x, terrain.width);
    const int wy = wrapi(y, terrain.height);
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
    return std::max<std::int32_t>(
        1, std::int32_t(float(g.unitScale) * (c - g.threshold)
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
    // BFS over live same-kind cells, 8-adjacent, torus-wrapped. The frontier
    // is coordinates (a flat index cannot step to its neighbours across the
    // wrap without re-deriving x/y anyway).
    std::vector<std::pair<int, int>> frontier{{x, y}};
    std::vector<std::uint32_t> seen{mineIdx};
    std::int64_t sum = g.at(x, y);
    int absorbed = 0;
    while (!frontier.empty()) {
        const auto [cx, cy] = frontier.back();
        frontier.pop_back();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = cx + dx, ny = cy + dy;
                const std::uint32_t idx = layer.wrap_index(nx, ny);
                if (std::find(seen.begin(), seen.end(), idx) != seen.end())
                    continue;
                const std::int32_t here = g.at(nx, ny);
                if (here == 0) continue;
                seen.push_back(idx);
                sum += here;
                g.write(nx, ny, 0);    // the absorbed vein leaves the map —
                                       // and its reach disc with it, inside
                                       // the write, not beside it
                ++absorbed;
                frontier.emplace_back(nx, ny);
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
