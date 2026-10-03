#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/gens/kit/noise.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/outline.h"
#include "sub/city_layout.h"
#include "core/rng.h"
#include "sub/height.h"
#include "sub/collide.h"   // kStepUpM — рост, через который переступают
#include "macro/tree_layer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

namespace {

// ── МОЧАЖИНЫ — СОБСТВЕННАЯ ФАКТУРА ЭТОГО МОДУЛЯ, ПОВЕРХ ГОТОВОЙ БАЗЫ ──────
// Закон владельца (2026-10-03, дословно): «у каждого должен быть свой модуль
// генератор … но сам рельеф основа должен быть единым … ГЕНЕРАТОР МОДУЛЯ
// МОЖЕТ ДОП ПОВЕРХ МЕНЯТЬ КАК ЕМУ УГОДНО». До этого мочажины стояли ВНУТРИ
// `generate_heightmap`, вперемешку с общей стопкой.
//
// ВЫСОТА ВЫВЕДЕНА ИЗ ТЕЛА, А НЕ НАЗНАЧЕНА: кочка есть то, через что
// ПЕРЕСТУПАЮТ, значит её размах впадина→гребень равен `kStepUpM` — высоте, на
// которую нога встаёт на соседний верх, и это ещё не стена (`sub/collide.h`).
// Прежняя амплитуда была 0.05 + 0.04 ЕДИНИЦ ПОЛЯ, то есть 135 м по кривой:
// в полтораста раз выше тела, ради которого кочка и существует. Замер тогда
// давал уклон тайл-тайл p50 **17.4°** против 0.8° у холма — болото было круче
// гор.
//
// ДЛИНЫ ВОЛН — НА СТУПЕНЯХ ЛЕСТНИЦЫ ЗЕМЛИ, и это не вкус: композит рисует
// вершиной каждые `kHeightQuadTiles` тайлов, значит волна короче двух шагов
// не рисуется вовсе, и сэмплировать её — только алиасить. Прежняя мелкая
// октава стояла на λ=40 м, то есть ПОД найквистом меша: её и было видно как
// «частые маленькие холмики». Настоящей кочки в метр поперёк геометрия этого
// мира нести не может — её место в фактуре грунта, не в рельефе.
//
// СРЕДНЕЕ РОВНО НОЛЬ: уровень земли фактура не трогает по построению, а не по
// договорённости (вердикт «уровень рельефа может быть только один из
// макромира»).
constexpr float kBogLongTiles  = 64.0f;   // ступень лестницы
constexpr float kBogShortTiles = 32.0f;   // найквист меша, самая мелкая
constexpr std::uint32_t kBogSeed = 0xB0901Eu;

void bog_pools(const GenInput& in, SubworldMapData& out) {
    if (out.heightmap.size() != std::size_t(kCellSize) * kCellSize) return;
    // Вес СВОЕГО биома, сшитый по 3×3 той же дверью, которой сшита база
    // (`nb_weights@src/sub/base_generator.h`): иначе фактура оборвалась бы на
    // шве ровно там, где база непрерывна.
    float wet[9];
    bool any = false;
    for (int i = 0; i < 9; ++i) {
        wet[i] = in.nbBiome[i] == Biome::Swamp ? 1.0f : 0.0f;
        any = any || wet[i] > 0.0f;
    }
    if (!any) return;
    const int worldTiles = in.ctx.worldCellsX > 0
        ? in.ctx.worldCellsX * kCellSize : 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const float w = blend9(wet, nb_weights(x, y, kCellSize));
            if (w <= 0.01f) continue;
            // Глобальный тайл, завёрнутый по тору: решётка фактуры встречает
            // себя на шве мира, потому что её шаг — делитель стороны мира.
            const int gx = worldTiles > 0
                ? wrapi(in.ctx.cx * kCellSize + x, worldTiles)
                : in.ctx.cx * kCellSize + x;
            const int gy = worldTiles > 0
                ? wrapi(in.ctx.cy * kCellSize + y, worldTiles)
                : in.ctx.cy * kCellSize + y;
            const float n =
                0.5f * smooth_noise01(float(gx) / kBogLongTiles,
                                      float(gy) / kBogLongTiles, kBogSeed)
              + 0.5f * smooth_noise01(float(gx) / kBogShortTiles,
                                      float(gy) / kBogShortTiles,
                                      kBogSeed ^ 0x9E3779B9u);
            float& h = out.heightmap[std::size_t(y) * kCellSize + x];
            h += (n - 0.5f) * w * field_delta_of_m(kStepUpM, h);
        }
    }
}

} // namespace

void gen_swamp(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const int* nbTreeCount = in.nbTreeCount;
    bog_pools(in, out);
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Wilderness: no road stitching (see gen_open) — the neighbour feature
    // ring is not this module's business.
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
