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
#include "sub/collide.h"   // kBodyHeightM — рост, за которым прячутся
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

namespace {

// ── БАРХАНЫ — СОБСТВЕННАЯ ФАКТУРА ЭТОГО МОДУЛЯ, ПОВЕРХ ГОТОВОЙ БАЗЫ ───────
// Голая земля — вотчина этого модуля, и пустыня входит в неё; ветка по
// СВОЕМУ биому внутри модуля законна ровно потому, что это его собственная
// композиция (`gens.h`: модуль владеет своим содержанием), а не вопрос к миру.
// До этого барханы стояли ВНУТРИ `generate_heightmap`, вперемешку с общей
// стопкой, — то, что владелец велел уничтожить (2026-10-03: «уничтожаем все
// перемешки, приводим основу к контексту единому из макромира»).
//
// ВЫСОТА ВЫВЕДЕНА ИЗ ТЕЛА: бархан есть то, за чем ПРЯЧУТСЯ, значит его размах
// подошва→гребень равен `kBodyHeightM` — росту от ступни до макушки
// (`sub/collide.h`). Прежняя амплитуда была 0.15 ЕДИНИЦ ПОЛЯ = **225 м** по
// кривой, в 130 раз выше тела; замер давал пустыне уклон тайл-тайл p50
// **29.8°** — круче ГОРЫ (21.3°). И две колонки прямо спорили: `heightScale`
// пустыни 0.004 — самый гладкий грунт во всей таблице — против генератора на
// две сотни метров.
//
// АНИЗОТРОПИЯ — ЭТО И ЕСТЬ БАРХАН: гребень длиннее поперёк, чем вдоль, и
// отношение 3:2 осталось тем же, что было. Длина волны — ступень лестницы
// земли: 128 тайлов, видимый размер дюнной гряды; короче 32 м меш не несёт
// вовсе.
constexpr float kDuneTiles     = 128.0f;   // ступень лестницы = длина гряды
constexpr float kDuneAcross    = 1.5f;     // гребень вытянут 3:2
constexpr std::uint32_t kDuneSeed = 0xD0E5A17u;

void desert_dunes(const GenInput& in, SubworldMapData& out) {
    if (out.heightmap.size() != std::size_t(kCellSize) * kCellSize) return;
    float sand[9];
    bool any = false;
    for (int i = 0; i < 9; ++i) {
        sand[i] = in.nbBiome[i] == Biome::Desert ? 1.0f : 0.0f;
        any = any || sand[i] > 0.0f;
    }
    if (!any) return;
    const int worldTiles = in.ctx.worldCellsX > 0
        ? in.ctx.worldCellsX * kCellSize : 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            // Вес СВОЕГО биома — той же дверью, которой сшита база
            // (`nb_weights@src/sub/base_generator.h`), иначе гряда обрывалась
            // бы на шве клетки.
            const float w = blend9(sand, nb_weights(x, y, kCellSize));
            if (w <= 0.01f) continue;
            const int gx = worldTiles > 0
                ? wrapi(in.ctx.cx * kCellSize + x, worldTiles)
                : in.ctx.cx * kCellSize + x;
            const int gy = worldTiles > 0
                ? wrapi(in.ctx.cy * kCellSize + y, worldTiles)
                : in.ctx.cy * kCellSize + y;
            const float n = smooth_noise01(float(gx) / kDuneTiles,
                                           float(gy) / (kDuneTiles * kDuneAcross),
                                           kDuneSeed);
            float& h = out.heightmap[std::size_t(y) * kCellSize + x];
            h += (n - 0.5f) * w * field_delta_of_m(kBodyHeightM, h);
        }
    }
}

} // namespace

// Plain biome ground tiles + stitched cross-cell tree scatter.
void gen_open(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const int* nbTreeCount = in.nbTreeCount;
    desert_dunes(in, out);
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Plain wilderness: no road stitching. TS only grows connecting roads when
    // the cell itself is road-like (road feature or landmark); a bare
    // grassland/open cell next to a road must stay road-free.
    // fill_base_tiles already stamps decorative trees but with a local RNG
    // — overwrite with the global stitched scatter so neighbouring open
    // cells line up tree distributions seamlessly.
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
