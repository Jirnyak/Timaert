#include "sub/base_generator.h"
#include "tables/forest.h"
#include "sub/height.h"
#include "sub/material.h"   // kMtnGrassTopH / kMtnRockBaseH — THE treeline band
#include "sub/tree_atlas.h"
#include "core/rng.h"
#include "core/math.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

// Tree height bands are METRES (see BiomeConfig): a mature stand runs roughly
// 10-20 m, with the cold/dry margins stunted and the tropics overtopping it.
static const BiomeConfig kConfigs[11] = {
    /* Tundra   */ {0.018f, 6,  6.0f, 10.0f, 0.8f},
    /* Taiga    */ {0.20f,  3, 12.0f, 19.0f, 1.0f},
    /* Snow     */ {0.025f, 5,  7.0f, 11.0f, 0.9f},
    /* Valley   */ {0.050f, 4, 11.0f, 18.0f, 1.0f},
    /* Meadow   */ {0.035f, 4, 11.0f, 18.0f, 1.0f},
    /* Swamp    */ {0.080f, 3,  9.0f, 15.0f, 0.3f},
    /* Desert   */ {0.004f, 8,  6.0f, 10.0f, 0.6f},
    /* Steppe   */ {0.018f, 5,  9.0f, 14.0f, 0.8f},
    /* Tropics  */ {0.25f,  2, 13.0f, 20.0f, 1.0f},
    /* Water    */ {0.0f,   16,11.0f, 18.0f, 0.5f},
    /* Mountain */ {0.02f,  6,  8.0f, 14.0f, 1.0f},
};
const BiomeConfig& biome_config(Biome b) {
    const int i = int(b);
    return kConfigs[(i >= 0 && i < int(sizeof(kConfigs) / sizeof(kConfigs[0]))) ? i : 0];
}

// Per-feature subworld height amplifier removed — TS-faithful generator
// derives mountain influence from biome+feature directly.

// TS-faithful integer hash noise (mirrors `terrainNoise` in
// subworld/base-generator.ts). Used by smooth_noise_ts for biome dune /
// swamp layers so terrain matches TS reference shape.
static float terrain_noise_ts(int x, int y, std::uint32_t seed) {
    std::uint32_t v = (std::uint32_t(x) * 374761393u) ^ (std::uint32_t(y) * 668265263u)
                    ^ (seed * 2246822519u);
    v = (v ^ (v >> 13)) * 1274126177u;
    v ^= v >> 16;
    return float(v) / 4294967295.0f;
}

// Single-octave smoothstep value noise on the TS hash. Mirrors
// `smoothTerrainNoise` from base-generator.ts.
//
// `period` is how many lattice cells the WHOLE WORLD spans at this frequency;
// 0 means "no world around this call" (bare fixtures and tests). When it is
// given, the lattice is snapped to a whole number of cells and the sample point
// stretched to match, so the field meets itself exactly at the world's edge —
// the same construction the macro layer uses (CANON.md S1). It has to be the
// WORLD's span and nothing smaller: a period that fits inside the view is what
// makes procedural ground look like wallpaper, and a period of a million tiles
// cannot repeat inside a thousand kilometres.
static float smooth_noise_ts(float x, float y, std::uint32_t seed,
                             float period = 0.0f) {
    int wrapTo = 0;
    if (period >= 1.0f) {
        const float whole = std::round(period);
        const float k = whole / period;
        x *= k;
        y *= k;
        wrapTo = int(whole);
    }
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    int ix0 = ix, iy0 = iy, ix1 = ix + 1, iy1 = iy + 1;
    if (wrapTo > 0) {
        ix0 = wrapi(ix0, wrapTo); iy0 = wrapi(iy0, wrapTo);
        ix1 = wrapi(ix1, wrapTo); iy1 = wrapi(iy1, wrapTo);
    }
    float n00 = terrain_noise_ts(ix0, iy0, seed);
    float n10 = terrain_noise_ts(ix1, iy0, seed);
    float n01 = terrain_noise_ts(ix0, iy1, seed);
    float n11 = terrain_noise_ts(ix1, iy1, seed);
    return n00 * (1 - sx) * (1 - sy)
         + n10 * sx * (1 - sy)
         + n01 * (1 - sx) * sy
         + n11 * sx * sy;
}

// NO SEA-LEVEL ALIAS LIVES HERE ANY MORE. It used to — `kWaterLevel =
// WATER_LEVEL` — and an alias is how a value that should have been an argument
// stays a constant: two of this file's laws read it instead of asking the
// scene, so a world whose sea had moved got a valley floor and a swamp bed
// measured from somebody else's water. The plane arrives as `seaLevel` now,
// from CellContext (map_data.h).

// СИД СОБСТВЕННОГО ШУМА ЗЕМЛИ — ОДИН НА ВЕСЬ ФАЙЛ. Он стоял двумя копиями
// одного литерала под ДВУМЯ именами — `kDetailSeed` у стопки детали и
// `kRidgeSeed` у хребтов, — а два имени у одного числа есть приглашение
// развести их правкой одного. Деталь и хребет сидят на одном сиде нарочно:
// это собственный шум ОДНОЙ земли, а не два независимых поля.
constexpr std::uint32_t kDetailSeed = 0xD37A115u;

// ── ЛЕСТНИЦА ОКТАВ ЗЕМЛИ — ВЫВЕДЕНА ИЗ ДВУХ ЧИСЕЛ МИРА, А НЕ НАЗНАЧЕНА ────
//
// ВЕРХ — КЛЕТКА. Длиннее клетки форму земли даёт МАКРОПОЛЕ (ЗАКОН ПОЛЯ), и
// собственная октава субмира на тех длинах спорила бы с ним за один и тот же
// силуэт.
//
// НИЗ — НАЙКВИСТ МЕША. Композит рисуется вершинной сеткой с шагом
// `kHeightQuadTiles`, значит волна короче двух шагов не рисуется ВОВСЕ —
// сэмплировать её значит алиасить. Это то же правило, которым дальние кольца
// роняют деталь, просто записанное для самого мелкого меша в игре.
//
// МЕЖДУ НИМИ — ДИАДНО, и шесть октав здесь не выбраны, а посчитаны:
// log2(1024/32) + 1. Лестница ложится ровно на кольца дальнего мира (кольцо 0
// шагом 32 м несёт λ ≥ 64, кольцо 1 — λ ≥ 256, кольцо 2 — λ ≥ 1024), поэтому
// «деталь УБИРАЕТСЯ, а не подменяется» перестаёт быть прозой.
//
// ЗАТУХАНИЕ — ЕДИНСТВЕННОЕ, ЧТО НЕ ВЫВОДИТСЯ, И ЭТО ВЕРДИКТ ВЛАДЕЛЬЦА
// (2026-10-03, дословно о симптоме: «стало слишком холмисто и холмы не крпыне
// плавные а много маленньких эт некрасиво»; выбор из трёх — «сразу 0.35,
// максимально гладко»). Амплитуда падает в g раз на октаву вниз, значит УКЛОН
// падает в 2g = 0.70 раза: крупная форма главная, мелкая читается фактурой.
// При g = 0.5 вклад каждой октавы в уклон был бы равным — это классический
// фрактал, и именно он даёт «много мелких холмов».
//
// ЧТО ЗДЕСЬ СТОЯЛО: две октавы, λ 125 и 50 тайлов, веса 0.5 и 0.25. Замер
// `relief_census` на мире владельца показал их цену прямо: от 128 м к 64 м σ
// падала с 2.11 до 2.04 м, то есть уклон на следующей октаве УДВАИВАЛСЯ, а
// между 125 м и клеткой не было ничего вовсе — отсюда вблизи мелкая
// холмистость, вдали стол (M-207 и bugs.md Б1 — один спектр, два симптома).
// ВЕРХ — ЧИСЛО АВТОРСКОЕ, И ЭТО СКАЗАНО ВСЛУХ. Выводить его не из чего:
// длиннее клетки форму даёт макрополе, короче 32 м меш не рисует, а ГДЕ между
// ними стоит самая крупная собственная форма земли — вопрос вида, не
// инварианта. Провенанс у числа есть: на этой ступени лежала авторская октава
// λ=125 тайлов, и землю с ней владелец принимал глазами. Попытка увести верх к
// целой клетке (2026-10-03, затухание 0.35) прошла проверку глазами и была
// ОТВЕРГНУТА: «сглаживание было ошибкой» — уклон холма падал 3.94° → 0.64°.
constexpr int   kDetailTopTiles    = 128;
constexpr int   kDetailBottomTiles = 2 * kHeightQuadTiles;     // 32
// ЗАТУХАНИЕ — ЕДИНСТВЕННАЯ НЕЙТРАЛЬНАЯ ТОЧКА ШКАЛЫ. При 0.5 вклад каждой
// октавы в УКЛОН одинаков, то есть ни один масштаб не привилегирован; выше —
// мелкое начинает править крутизной (та самая «мелкая холмистость»), ниже —
// земля гладится до блина. Это не вкус, это единственное значение, при котором
// лестница самоподобна.
constexpr float kDetailGain        = 0.5f;
// Сколько ступеней вмещает лестница от клетки до найквиста меша — СЧИТАЕТСЯ,
// а не объявляется: это log2(верх/низ)+1, записанное циклом, потому что
// `std::log2` не constexpr. Больше этого числа октав не бывает ни у какой
// лестницы этого мира, поэтому оно же и длина массивов ниже.
constexpr int detail_octave_cap() {
    int n = 0;
    for (int lam = kDetailTopTiles; lam >= kDetailBottomTiles; lam /= 2) ++n;
    return n;
}
constexpr int kDetailOctaveCap = detail_octave_cap();
static_assert(kDetailGain > 0.0f && kDetailGain <= 0.5f,
              "затухание выше 0.5 отдало бы крутизну мелким октавам — это и "
              "есть «много маленьких холмов», которое владелец забраковал");

struct DetailStack {
    float freq[kDetailOctaveCap]{};     // циклов на тайл
    float weight[kDetailOctaveCap]{};
    float norm  = 0.0f;
    int   count = 0;                    // сколько ступеней реально построено
};
// Лестница строится ОТ ВЕРХА ВНИЗ до найквиста меша: число ступеней есть
// следствие двух концов, а не третий параметр.
constexpr DetailStack make_detail_stack(int topTiles, float gain) {
    DetailStack s{};
    float w = 1.0f;
    for (int lam = topTiles;
         lam >= kDetailBottomTiles && s.count < kDetailOctaveCap; lam /= 2) {
        s.freq[s.count]   = 1.0f / float(lam);
        s.weight[s.count] = w;
        s.norm += w;
        ++s.count;
        w *= gain;
    }
    return s;
}
constexpr DetailStack kDetail = make_detail_stack(kDetailTopTiles, kDetailGain);
static_assert(kDetail.count == kDetailOctaveCap,
              "авторская лестница обязана дойти от клетки до найквиста меша "
              "целиком — иначе её верх или низ не то, чем назван");

// The near generator's detail stack, with the octaves a mesh cannot draw left
// out (base_generator.h). The frequencies, weights and normalisation are the
// ones the ground itself is made of — this is the same noise, sampled by
// somebody who can only afford some of it.
float terrain_detail01(int gx, int gy, float worldTiles,
                       float minWavelengthTiles) {
    // THE FULL weight of the stack, always — see the header. An octave the
    // mesh cannot draw hands over its own mean (0.5) and keeps its seat, so
    // removing it neither MOVES the ground (the mean is unchanged) nor
    // AMPLIFIES what is left (the survivors keep their authored share). The
    // old spelling divided by the survivors' weight and did the second: the
    // λ=125 octave came out 1.5× louder on the far world than the near
    // generator gives it. It also needed a special case for "no octave
    // survives" — with the full normaliser that case is the same arithmetic,
    // 0.5·ΣW / ΣW, and the branch is gone.
    const auto per = [worldTiles](float freq) { return worldTiles * freq; };
    float sum = 0.0f;
    for (int o = 0; o < kDetail.count; ++o) {
        // λ = 1/freq tiles. An octave shorter than the mesh can resolve is not
        // removed for taste: sampling it would only alias.
        const bool drawable = 1.0f / kDetail.freq[o] >= minWavelengthTiles;
        sum += (drawable ? smooth_noise_ts(float(gx) * kDetail.freq[o],
                                           float(gy) * kDetail.freq[o],
                                           kDetailSeed, per(kDetail.freq[o]))
                         : 0.5f) * kDetail.weight[o];
    }
    return std::clamp(sum / kDetail.norm, 0.0f, 1.0f);
}

// THE FAR WORLD'S GROUND (base_generator.h). It lives here, next to the near
// generator's own loop, so both reach the ONE transfer curve — which is the
// whole of what M-201 was.
float far_height01(int gx, int gy, float macroH01, float peak01,
                   float ridgeWeight, float worldTiles, float seaLevel,
                   float gradient01, float heightScale, float mtnScale,
                   float minWavelengthTiles) {
    // The manifold, plus every octave of ground the mesh can carry. The
    // relief term is the near generator's own: macroH² concentrates the
    // ground's own variation on high land and keeps lowlands calm, and the
    // biome-edge gradient lifts it where two kinds of land meet.
    float h = macroH01;
    if (minWavelengthTiles > 0.0f && heightScale > 0.0f && mtnScale > 0.0f) {
        const float noise = terrain_detail01(gx, gy, worldTiles,
                                             minWavelengthTiles);
        const float relief = macroH01 * macroH01 + gradient01;
        // DETAIL ON THE CURVE, not stretched by it — the same factor, from the
        // same door, that `generate_heightmap` puts on this very term. Without
        // it the far ground's own noise was worth 2^((h−0.4)·10) times more
        // metres than the near ground's: ×4.6 at a foothill's 0.61, ×16 on a
        // massif's shoulder. That is not a rounding — it is the FRAME the owner
        // photographed round the 3×3 window, measured at a median 57.9 m of
        // near↔far disagreement on foothill cells against 5.6 m with the factor
        // in place (M-201, three seeds).
        h += (noise - 0.5f) * relief * heightScale * mtnScale
           * detail_field_scale(macroH01);
    }
    if (ridgeWeight <= 0.01f) return std::clamp(h, 0.0f, 2.0f);
    return std::clamp(mountain_ridges01(h, gx, gy, macroH01, peak01,
                                        ridgeWeight, worldTiles,
                                        /*coarseOnly=*/true, seaLevel),
                      0.0f, 2.0f);
}

float crest_jitter01(int cellGX, int cellGY, std::uint32_t worldSeed) {
    return terrain_noise_ts(cellGX, cellGY,
                            cell_seed(worldSeed, cellGX, cellGY) ^ 0x5A17u);
}

TerrainMod terrain_mod_for(SquadType landmark, FeatureType feature) {
    // ONE data table: how strongly each macro content class calms the terrain
    // it stands on. damp scales down ridge/noise for the whole cell; plateauR
    // is the radius (tiles) of the radial pull toward the cell-centre height.
    // A SETTLEMENT'S GROUND IS NOT A TABLE (owner, 2026-09-13). A city used to
    // damp its whole cell to nothing and pull 280 tiles of it flat — so the
    // town grew on a plate, and the growth that is supposed to run long down a
    // valley and stop at a bluff (sub/gens/kit/growth.h) had no relief left to
    // read. Every city came out a circle, and the cause was here, not there.
    //
    // What must be flat is the HEART: the market and the streets that meet on
    // it. The outskirts keep their land. `plateauR` is therefore the market and
    // one block of approach on each side, and `damp` leaves the cell most of
    // its character instead of erasing it.
    TerrainMod m{};
    switch (landmark) {
        case SquadType::City:    m = {0.6f,  96.0f}; break;
        case SquadType::Village: m = {0.5f,  64.0f}; break;
        case SquadType::Ruin:    m = {0.6f, 120.0f}; break;
        case SquadType::Spire:   m = {0.6f, 120.0f}; break;
        // Registry kinds no world places yet (Lair/Shrine/Mine/Tower): bare
        // ground until each kind's generator module lands — a landmark calms
        // the terrain only once something actually stands on it.
        //
        // ВЫПИСАНЫ ПОИМЁННО, И `default:` СНЯТ (ЗАКОН СЛОВАРЯ п.4): он был
        // ЕДИНСТВЕННЫМ среди четырёх switch-ей по этой оси, а три соседних
        // (`stamp_settlement_features`, диспетчер макро-ИИ,
        // `resolve_mode`) держат полное покрытие намеренно и прямо
        // запрещают `default` своими комментариями. Здесь он глушил
        // `-Wswitch` ровно там, где тот нужен: следующий НЕПОДВИЖНЫЙ род
        // получал бы `{0, 0}` молча — то есть «не трогать рельеф» вместо
        // «про тебя забыли», и отличить одно от другого было бы нечем.
        case SquadType::Lair:
        case SquadType::Shrine:
        case SquadType::Mine:
        case SquadType::Tower:
        // Подвижные роды оси: сквад, который ХОДИТ, рельеф не ровняет —
        // он на клетке не стоит, он через неё идёт.
        case SquadType::Artel:
        case SquadType::Caravan:
        case SquadType::Collector:
        case SquadType::Count:
        case SquadType::None:    break;
    }
    if (feature == FT_Road || feature == FT_DirtRoad) {
        m.damp = std::max(m.damp, 0.55f);
    }
    // Ploughed farmland is worked ground: calmer than wilderness, but the
    // plough follows the land more than a road bed does.
    if (feature == FT_Field) {
        m.damp = std::max(m.damp, 0.35f);
    }
    return m;
}

// Domain-warped 2-octave ridged multifractal. Kept deliberately
// LOW-FREQUENCY (wavelengths ~250 and ~110 tiles) so mountains read as
// smooth coherent massifs. Higher-frequency ridge octaves (≥0.02) aliased
// badly on the 16-tile-spaced terrain mesh, producing the "chaotic spiky
// peaks" the minimap never showed (the minimap low-passes the heightmap).
// Keeping the ridge content itself below the mesh Nyquist was necessary but
// NOT sufficient: the crest shaping function also matters. The classic ridged
// fold (1−|2s−1|)² has a slope discontinuity at every noise 0.5-crossing, and
// that corner synthesises high-frequency harmonics of the (low-frequency) base
// field that alias back onto the mesh. We use a C1 smooth crest instead — see
// the octave loop below — which is what actually made the 3D relief match the
// smooth shaded relief on the map.
//   The ridge ceiling is blended from the same 3×3 macro context as the base
//   terrain.  Peaks may rise slightly above 1.0, but a soft compression avoids
//   both the old over-tall walls and the later flat 1.0 plateau.
static float soft_compress_peak(float h) {
    if (h <= 1.0f) return h;
    const float excess = h - 1.0f;
    return 1.0f + 0.20f * (1.0f - std::exp(-excess / 0.20f));
}

float mountain_ridges01(float h, int gx, int gy, float macroH,
                        float peakTarget, float rw,
                        float worldTiles, bool coarseOnly, float seaLevel) {
    if (rw <= 0.01f) return h;
    // Every octave below closes on the world: the period handed to the noise is
    // the world's tile span at that frequency.
    const auto per = [worldTiles](float freq) { return worldTiles * freq; };
    const float wx = float(gx)
        + (smooth_noise_ts(float(gx) * 0.002f + 71.7f,
                           float(gy) * 0.002f, kDetailSeed, per(0.002f)) - 0.5f) * 90.0f;
    const float wy = float(gy)
        + (smooth_noise_ts(float(gx) * 0.002f,
                           float(gy) * 0.002f + 31.1f, kDetailSeed, per(0.002f)) - 0.5f) * 90.0f;
    constexpr float kFreqs[2] = {0.0026f, 0.006f};
    float ridge = 0.0f, amp = 1.0f, wt = 1.0f;
    for (int o = 0; o < 2; ++o) {
        float sig = smooth_noise_ts(wx * kFreqs[o], wy * kFreqs[o], kDetailSeed,
                                    per(kFreqs[o]));
        // COMPROMISE crest (owner round 3): the C1 parabola 4s(1−s) alone
        // made homogeneous hills — no crests, no gullies, no character; the
        // classic ridged fold (1−|2s−1|)² alone aliased its corner harmonics
        // onto the 16-tile mesh (the old "chaotic spiky peaks"). BLEND them:
        // the fold contributes the sharp V-ridge / ravine STRUCTURE, the
        // parabola rounds the very apex enough to keep mesh-scale curvature
        // under the smoothness-test aliasing ceiling. Wavelengths sit between
        // the old 250/110 and the over-smoothed 555/250.
        const float fold = 1.0f - std::fabs(2.0f * sig - 1.0f);
        const float para = 4.0f * sig * (1.0f - sig);
        sig = fold * fold * 0.45f + para * 0.55f;
        sig = std::min(sig * wt, 1.0f);
        wt = std::min(1.0f, sig * 1.6f);
        ridge += sig * amp;
        amp  *= 0.5f;
    }
    ridge = std::min(1.0f, ridge * 0.80f);
    // Micro-crag octave: a LOW-amplitude (~0.004 ≈ 6 m) wave at λ≈120 tiles.
    // Mesh-vertex curvature scales with A/λ² while slope scales with A/λ, so
    // this restores the craggy mountain "grain" the long-wave rebalance took
    // away (mountains must keep more mesh-scale character than meadows —
    // mountain_mesh_smoothness_test parity) at a slope cost of only a few
    // degrees. λ=120 tiles stays well above the 32-tile mesh Nyquist.
    // The crag grain is THE fine octave, and the far world stops before it
    // (CANON S18.1). ~6 m at a ~120-tile wavelength: under a pixel long before
    // anything a far ring draws, so carrying it would be paying to render what
    // the air already ate.
    const float crag = coarseOnly
        ? 0.5f   // the octave's own mean — removing detail must not MOVE the
                 // ground, only stop varying it
        : smooth_noise_ts(wx * 0.0085f, wy * 0.0085f,
                          kDetailSeed ^ 0x9E3779B9u, per(0.0085f));
    const float cragAmp = 0.004f + 0.004f * ridge; // crags live on the ridges
    // Valley floor must track the surrounding macro altitude, not collapse
    // to half of it. The original `macroH * 0.5f` produced a 400+ m trench
    // around mountain features whenever neighbour land cells sat above
    // ~0.55 macroH (their land-remap put them well above the mountain's
    // off-ridge floor). 0.92 keeps mountain valleys gently lower than the
    // surrounding plain (≈ 8 % drop) so ridges still rise visibly above
    // the basin without creating a moat at the foot of the wall.
    // Deeper valley floor than the hills round (0.90 → 0.86): ridges rise
    // AND ravines cut — the расселины the flat 0.90 floor erased.
    const float valleyFloor = std::max(seaLevel + 0.08f, macroH * 0.88f);
    const float peak        = std::max(valleyFloor + 0.05f, peakTarget);
    // ── ГРЕБЕНЬ — ДЕТАЛЬ СУБМИРА, А НЕ МАКРОГОРА (M-199) ─────────────────
    // Здесь стояло абсолютное поле `valleyFloor + ridge*(peak − valleyFloor)`,
    // и оно уходило на кривую переноса ЦЕЛИКОМ, как будто это макрорельеф.
    // Макрогора — `macroH`, число КЛЕТКИ 1024 м; а всё, что лепит эта функция,
    // живёт на длинах волн 118–385 ТАЙЛОВ, то есть ВНУТРИ клетки. Значит это
    // деталь, и она обязана нести `detail_field_scale` ровно как шум, дюны и
    // болото двадцатью строками ниже по вызову.
    //
    // ЗАМЕРЕНО (кадры владельца 2026-10-01 + счёт): на высоте поля 0.9 наклон
    // кривой 48 000 м/ед., поэтому рельеф гребня весил 6 518 м при длине волны
    // 250 м, а краг — 288 м при 118 м. Гора превращалась в ежа из вертикальных
    // игл, под которыми собственно горы видно не было: макромассив на том же
    // месте поднимается всего на 52–108 м НА КЛЕТКУ.
    //
    // ФОРМА: функция по-прежнему строит ту же поверхность, но результат берётся
    // как ОТКЛОНЕНИЕ от макровысоты и кладётся на кривую. У воды множитель
    // ровно 1 — берег и равнинные гряды не двигаются ни на метр; наверху он
    // 1/32, и местный рельеф возвращается к тем сотням метров, в которых его
    // и подбирали (приёмка 2aa0c52f: p50 40° / p90 56° / p99 66°).
    const float detail      = detail_field_scale(macroH);
    const float rawMtn      = valleyFloor + ridge * (peak - valleyFloor)
                            + (crag - 0.5f) * cragAmp * 2.0f;
    const float mtnH        = soft_compress_peak(macroH + (rawMtn - macroH) * detail);
    // NOISY massif edge (owner: like the coastline, never a solid straight
    // ramp): a low-frequency warp shifts the ridge-weight threshold so the
    // massif FINGERS into the plain — foothill spurs and bays instead of a
    // clean contour. Smoothstep keeps both ends C1.
    const float edgeN = smooth_noise_ts(wx * 0.0035f + 211.0f,
                                        wy * 0.0035f + 97.0f, kDetailSeed,
                                        per(0.0035f));
    const float t     = std::clamp(rw * 1.2f + (edgeN - 0.5f) * 0.55f,
                                   0.0f, 1.0f);
    const float blend = t * t * (3.0f - 2.0f * t);
    return h * (1.0f - blend) + mtnH * blend;
}

NbWeights nb_weights(int x, int y, int cellSize) {
    // Центры клеток стоят на 0.5/1.5/2.5 сетки окна; тайл (x,y) центральной
    // клетки лежит на 1 + x/cellSize, отсюда −0.5 и зажим в [0,2].
    const float invCS = 1.0f / float(cellSize);
    const float gx = float(x) * invCS + 1.0f;
    const float gy = float(y) * invCS + 1.0f;
    const int   x0 = std::clamp(int(std::floor(gx - 0.5f)), 0, 2);
    const int   y0 = std::clamp(int(std::floor(gy - 0.5f)), 0, 2);
    const int   x1 = std::min(2, x0 + 1);
    const int   y1 = std::min(2, y0 + 1);
    const float fx = std::clamp((gx - 0.5f) - float(x0), 0.0f, 1.0f);
    const float fy = std::clamp((gy - 0.5f) - float(y0), 0.0f, 1.0f);
    NbWeights w{};
    w.i00 = y0 * 3 + x0; w.i10 = y0 * 3 + x1;
    w.i01 = y1 * 3 + x0; w.i11 = y1 * 3 + x1;
    w.w00 = (1 - fx) * (1 - fy); w.w10 = fx * (1 - fy);
    w.w01 = (1 - fx) * fy;       w.w11 = fx * fy;
    return w;
}

void generate_heightmap(std::vector<float>& out, int cellSize,
                        const float nbHeights[9],
                        const Biome nbBiome[9],
                        const Biome* nbBiome5,
                        // `std::uint32_t seed` снят 2026-10-09: тело его не
                        // читало — высоту ведут globalOffset* и worldSeed
                        // (дальний мир), а клеточный сид был вторым.
                        Biome biome,
                        int globalOffsetX, int globalOffsetY, float seaLevel,
                        const TerrainMod* nbMods, int worldCellsX,
                        std::uint32_t worldSeed) {
    // The world's tile span — what every global-coordinate noise below closes
    // on. 0 (a bare fixture with no world around it) means "do not wrap", which
    // is what the tests that generate a lone cell want.
    const float worldTiles = float(std::max(0, worldCellsX)) * float(cellSize);
    out.assign(std::size_t(cellSize) * cellSize, 0.0f);

    // ── Per-cell traits (TS parity) ──
    // Mountain influence: 0.15 in mountain cells, 0.1 + 0.1·adjMtn elsewhere.
    // Ridge weight: 1 in mountain cells, 0 elsewhere — drives apply_mountain_ridges.
    // Macro gradient: max 4-conn |Δh| in macro space — boosts relief at biome
    // boundaries (steep shores get rougher noise than flat plains).
    // Height scales / dune / swamp flags: from per-neighbour BiomeConfig.
    // Remapped macro height: water cells → squared deep-ocean curve, land
    // cells → linear lift. Bilinear blend of the remapped values produces
    // the smooth heightmap manifold (natural shorelines, river banks for
    // a single-cell water tile, plain → foothill → peak gradients).
    float mountainScale[9];
    float ridgeWeight[9];
    float macroGradient[9];
    float heightScale[9];
    float remapped[9];
    float peakHeight[9];
    for (int i = 0; i < 9; ++i) {
        // Mountains are a biome now (elevation-classified), so ridge/peak
        // amplification keys off the neighbour biome, not a feature byte.
        const bool isMtn = nbBiome[i] == Biome::Mountain;
        const int cx = i % 3, cy = i / 3;
        // A CELL COUNTS ITS OWN NEIGHBOURS. With the wider ring every one of
        // the nine can, including the rim; without it the rim sees only
        // inward and its count depends on who is looking (base_generator.h).
        int adjMtn = 0;
        if (nbBiome5 != nullptr) {
            const int X = cx + 1, Y = cy + 1;          // into the 5×5
            if (nbBiome5[Y * 5 + X - 1] == Biome::Mountain) ++adjMtn;
            if (nbBiome5[Y * 5 + X + 1] == Biome::Mountain) ++adjMtn;
            if (nbBiome5[(Y - 1) * 5 + X] == Biome::Mountain) ++adjMtn;
            if (nbBiome5[(Y + 1) * 5 + X] == Biome::Mountain) ++adjMtn;
        } else {
            if (cx > 0 && nbBiome[i - 1] == Biome::Mountain) ++adjMtn;
            if (cx < 2 && nbBiome[i + 1] == Biome::Mountain) ++adjMtn;
            if (cy > 0 && nbBiome[i - 3] == Biome::Mountain) ++adjMtn;
            if (cy < 2 && nbBiome[i + 3] == Biome::Mountain) ++adjMtn;
        }
        mountainScale[i] = cell_mtn_scale01(isMtn, adjMtn);
        ridgeWeight  [i] = isMtn ? 1.0f : 0.0f;

        float maxDiff = 0.0f;
        const float mh = nbHeights[i];
        if (cx > 0) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i - 1]));
        if (cx < 2) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i + 1]));
        if (cy > 0) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i - 3]));
        if (cy < 2) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i + 3]));
        macroGradient[i] = maxDiff;

        const auto& bc = biome_config(nbBiome[i]);
        heightScale[i] = bc.heightScale;

        // Water: t=1 at shoreline, t=0 at deep ocean, squared so deep water
        // sits well below the plane. Land: lifted from kLandFloor (shoreline)
        // to 1.0 (peak). Высота клетки ЕСТЬ её макровысота (ветка мертва)
        // (base_generator.h) — the shadow apron reads the same door.
        remapped[i] = mh;   // рельеф ЕСТЬ макровысота (горная ветка мертва)

        // Universal flattening (terrain_mod_for): a cell that carries a road
        // or a settlement calms its OWN ridge/noise/gradient columns. Applied
        // per-cell before the bilinear blend, so a damped city cell next to a
        // wild mountain cell still blends smoothly — the massif fades into
        // the town's table instead of stopping at a seam.
        apply_cell_damp(nbMods ? nbMods[i].damp : 0.0f,
                        mountainScale[i], ridgeWeight[i], macroGradient[i]);

        // THE CELL'S OWN PLACE, AND THE CELL'S OWN SEED.
        //
        // This crest jitter belongs to the neighbour cell, so every window
        // that contains that cell has to compute the SAME number for it —
        // otherwise two neighbours blend different crest targets into the
        // column they share and the ground steps at their border. It used to
        // take the CENTRE cell's `seed`, which makes the jitter a property of
        // WHO IS ASKING rather than of the place: measured on two adjacent
        // mountain cells with real per-cell seeds, their shared boundary
        // disagreed by 8.0 m against a 1.6 m worst step inside the cell — a
        // five-fold cliff, exactly the kind of seam CANON.md S1/S2 forbids.
        // (Invisible to the suite because the identity fixture handed both
        // cells ONE seed; with one seed the same measurement reads 1.0×.)
        //
        // The place is the WRAPPED cell index — the torus law the tile
        // coordinates below already obey — and the seed is that place's own,
        // through the one door (`cell_seed`, map_data.h).
        const int rawGX = globalOffsetX / cellSize + cx - 1;
        const int rawGY = globalOffsetY / cellSize + cy - 1;
        const int cellGX = worldCellsX > 0 ? wrapi(rawGX, worldCellsX) : rawGX;
        const int cellGY = worldCellsX > 0 ? wrapi(rawGY, worldCellsX) : rawGY;
        // THE crest law, through its one door (base_generator.h) — the same
        // one the far world builds its massifs with, so the ridge seen from
        // thirty kilometres is the ridge you walk up to (CANON S18.1).
        peakHeight[i] = skeleton_cell_peak01(mh, isMtn, adjMtn,
                                             cellGX, cellGY, worldSeed,
                                             seaLevel);
    }

    // Any settlement plateau in the 3×3 ring? (Pixel-loop guard.)
    bool anyPlateau = false;
    if (nbMods) {
        for (int i = 0; i < 9; ++i)
            if (nbMods[i].plateauR > 0.0f) { anyPlateau = true; break; }
    }

    for (int y = 0; y < cellSize; ++y) {
        // Bilinear sample weights: u,v ∈ [0,1] over the centre cell map
        // to gx,gy ∈ [0.5..1.5] in 3×3 grid space (cell centres at 0.5,
        // 1.5, 2.5). Same convention as TS.
        for (int x = 0; x < cellSize; ++x) {
            // ОДНА дверь соглашения — ею же пользуются модули, кладущие свою
            // фактуру поверх этой базы (base_generator.h nb_weights).
            const NbWeights nbw = nb_weights(x, y, cellSize);
            const auto blend = [&](const float* tbl) {
                return blend9(tbl, nbw);
            };

            float macroH = blend(remapped);
            const float localHS  = blend(heightScale);
            float localGrd = blend(macroGradient);
            float localMtn  = blend(mountainScale);
            const float localPeak = blend(peakHeight);
            float rw        = blend(ridgeWeight);

            // Settlement plateau: pull the manifold toward the settlement
            // cell's own centre height with a smoothstep radial falloff, and
            // damp the remaining noise/ridges by the same weight. Keyed to
            // the settlement cell's centre in GLOBAL cell-grid coordinates
            // and its pure remapped[] height, so every neighbouring cell
            // computes the identical pull — no seams. Gives walls and houses
            // a natural table instead of a mountain face.
            float plateauW = 0.0f;
            if (anyPlateau) {
                for (int i = 0; i < 9; ++i) {
                    const float R = nbMods[i].plateauR;
                    if (R <= 0.0f) continue;
                    const float dx = float(x) - (float(i % 3) - 0.5f) * float(cellSize);
                    const float dy = float(y) - (float(i / 3) - 0.5f) * float(cellSize);
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d >= R * 2.0f) continue;
                    // Full strength inside R, smoothstep skirt out to 2R.
                    const float t = std::clamp(2.0f - d / R, 0.0f, 1.0f);
                    const float w = t * t * (3.0f - 2.0f * t);
                    // LOCAL smoothing only (owner: «просто сглажено локально»
                    // — a town on a hill or at a massif's foot should stay
                    // picturesque): the pull toward the settlement centre
                    // height is a gentle ~35%, and even ridges keep a quarter
                    // of their body through town — softened shoulders, never
                    // a wall, never a plate. House pads still flatten their
                    // own footprints regardless (the house-pad OBB flatten).
                    macroH += (remapped[i] - macroH) * (w * 0.35f);
                    localMtn *= 1.0f - w * 0.8f;
                    localGrd *= 1.0f - w * 0.8f;
                    rw       *= 1.0f - w * 0.75f;
                    plateauW = std::max(plateauW, w);
                }
            }

            // ── ФАКТУРЫ БИОМА ЗДЕСЬ БОЛЬШЕ НЕТ, И ЭТО ЗАКОН ВЛАДЕЛЬЦА ────
            // Дословно (2026-10-03): «у каждого должен быть свой модуль
            // генератор (которые инкапсулированы DOD и могут дублировать и тд
            // и не пересекаются друг с другом ЭТО ГЛАВНОЕ) но сам рельеф
            // основа должен быть единым … ГЕНЕРАТОР МОДУЛЯ МОЖЕТ ДОП ПОВЕРХ
            // МЕНЯТЬ КАК ЕМУ УГОДНО»; и тогда же: «уничтожаем все перемешки,
            // приводим основу к контексту единому из макромира».
            //
            // Здесь стояли ДВЕ чужие фактуры — болотные мочажины и барханы, —
            // вперемешку с общей стопкой, со своими частотами и АБСОЛЮТНЫМИ
            // амплитудами в единицах поля: 0.05 и 0.04 у болота, 0.15 у
            // пустыни, то есть 75, 60 и 225 МЕТРОВ по кривой против 2.7 м,
            // которые на той же клетке даёт общая стопка. Замер назвал цену:
            // уклон тайл-тайл p50 у болота 17.4°, у пустыни 29.8° против 0.8°
            // у холма и 21.3° у ГОРЫ — болото и пустыня были круче гор.
            //
            // Теперь база есть РОВНО макроконтекст: макровысота девяти клеток,
            // их колонки, содержание клетки (дамп и плато) и одна стопка
            // детали. Фактура биома живёт в его собственном модуле
            // (`gens/swamp.cpp`, `gens/open.cpp`) и кладётся ПОВЕРХ готовой
            // базы — через `nb_weights` (base_generator.h), чтобы её вес был
            // сшит по 3×3 тем же соглашением, которым сшита база.

            // Multi-octave terrain noise in global tile coords with a fixed
            // world seed → continuous across cell boundaries.
            // The tile's place IN THE WORLD, and the world is a torus: a cell
            // reached by walking east off the last column is the same cell as
            // the one entered from the map, so its tiles must carry the same
            // global coordinate either way. Wrapping here rather than trusting
            // every caller is what makes that a property of construction — and
            // it costs nothing at the seam, because the noise above closes on
            // the same span (CANON.md S1/S2).
            const int gxi = worldTiles > 0.0f
                ? wrapi(globalOffsetX + x, int(worldTiles)) : globalOffsetX + x;
            const int gyi = worldTiles > 0.0f
                ? wrapi(globalOffsetY + y, int(worldTiles)) : globalOffsetY + y;
            // ── СТОПКА ДЕТАЛИ ОДНА, И ЗВОНЯТ В НЕЁ ОБА МИРА ──────────────
            // Здесь стояли те же частоты и те же веса, написанные от руки
            // второй раз: 0.008/0.5 и 0.02/0.25 с делением на 0.75 — ровно
            // `terrain_detail01` и ничего больше. Два написания одной стопки
            // расходятся молча: правка спектра в одном из них развела бы
            // ближнюю землю с дальней, не уронив ни теста, ни сборки (DOD п.6
            // — два ответа на один вопрос о мире).
            //
            // Предел длины волны у БЛИЖНЕГО пути — НОЛЬ, и это не «выключено»,
            // а значение: композит рисует мешем с вершиной на
            // `kHeightQuadTiles` тайлов, то есть несёт всё, что стопка даёт.
            // Дальний путь передаёт сюда 2×шаг своего кольца, и этим вся
            // разница между мирами исчерпывается.
            const float noise = terrain_detail01(gxi, gyi, worldTiles, 0.0f);

            // Smooth manifold: relief = macroH² + gradient. Macro height
            // squared concentrates noise on hills/peaks and keeps lowlands
            // calm; gradient lifts noise at biome edges so transitions
            // look natural.
            const float relief = macroH * macroH + localGrd;
            // DETAIL, placed on the curve rather than stretched by it — every
            // local term below carries this factor (sub/height.h
            // detail_field_scale). The macro relief and the ridges do NOT:
            // those ARE the mountain, and making them steep with altitude is
            // the whole point of the curve.
            const float detail = detail_field_scale(macroH);
            float h = macroH
                    + (noise - 0.5f) * relief * localHS * localMtn * detail;

            if (rw > 0.0f) {
                h = mountain_ridges01(h, gxi, gyi, macroH, localPeak, rw,
                                      worldTiles, /*coarseOnly=*/false,
                                      seaLevel);
            }

            // Clamp broad for safety; mountain ridge output itself is kept
            // near the TS 0..1 relief range, while water/swamp/plain logic
            // remains on the same smooth manifold.
            out[std::size_t(y) * cellSize + x] = std::clamp(h, 0.0f, 2.0f);
        }
    }
    (void)biome;
}

void fill_base_tiles(std::vector<std::uint8_t>& tiles, int cellSize,
                     Biome biome, std::uint32_t seed) {
    tiles.assign(std::size_t(cellSize) * cellSize, std::uint8_t(TILE_GRASS));
    if (biome == Biome::Water) {
        std::fill(tiles.begin(), tiles.end(), std::uint8_t(TILE_WATER));
    }
    // NOTE: this used to also stamp decorative TILE_TREE_DECOR with a local
    // RNG. Those phantom tiles had NO Structure::Tree behind them — the 2D
    // map showed trees that did not exist in 3D (owner report: "unclear what
    // criteria draw trees on the minimap"). scatter_universal_trees is the
    // ONE tree authority now: every TILE_TREE_DECOR is a real tree.
    (void)seed;
}

void scatter_universal_trees(SubworldMapData& out,
                             int cellSize,
                             int globalOffsetX, int globalOffsetY,
                             const Biome nbBiome[9],
                             const int nbTreeCount[9],
                             int clearRadius,
                             std::uint32_t seed) {
    const Biome biome = nbBiome[4];
    if (biome == Biome::Water && out.heightmap.empty()) return;
    const auto& cfg = biome_config(biome == Biome::Water ? Biome::Meadow
                                                         : biome);

    // ── Count-driven 3×3-contextual tree density ──
    // Each ring cell contributes a TREE RATE (trees per tile²) derived from
    // its macro tree COUNT (macro/tree_layer.h — the ONE scalar authority;
    // 16384 = the golden densest forest): rate = count / (cellArea · yield),
    // where kTreeScatterYield is the measured mean survival of the FBM
    // cluster gate below, so the EXPECTED number of placed trees over a full
    // flat cell ≈ its count. The per-node rate is the UNSHARPENED bilinear
    // blend of the ring — so a forest surrounded by forests stays uniformly
    // dense to its very edge, while a plain bordering a forest grows trees
    // gradually on its forest side (опушка) over the full cell width, the
    // same emergent-context rule the heightmap manifold uses. Water cells
    // carry count 0 and contribute nothing (their dry margins inherit trees
    // from the land side of the blend).
    const float invAreaYield = 1.0f
        / (float(cellSize) * float(cellSize) * kTreeScatterYield);
    float rate[9];
    float maxRate = 0.0f;
    for (int i = 0; i < 9; ++i) {
        const int cnt = std::clamp(nbTreeCount[i], 0, kMaxTreesPerCell);
        rate[i] = float(cnt) * invAreaYield;
        maxRate = std::max(maxRate, rate[i]);
    }
    if (maxRate <= 0.0f) return;

    // One GLOBAL lattice for every cell (the old per-biome scan step made
    // tree spacing jump at cell borders). Density is carried entirely by the
    // per-node probability: p = blendedRate · step² preserves each biome's
    // trees-per-area in cell interiors.
    constexpr int step = 2;
    const int gox = globalOffsetX, goy = globalOffsetY;

    // Align scan start to the nearest greater multiple of `step` so adjacent
    // cells use the same world-aligned grid (no seams).
    auto align_start = [&](int g) {
        int rem = ((g % step) + step) % step;
        return g + ((step - rem) % step);
    };
    const int gStartX = align_start(gox);
    const int gStartY = align_start(goy);
    const int gEndX   = gox + cellSize;
    const int gEndY   = goy + cellSize;

    const int cx = cellSize / 2, cy = cellSize / 2;
    const int clearSq = clearRadius * clearRadius;
    const float invCS = 1.0f / float(cellSize);

    Rng sizeRng(seed ^ 0xA17EE5u);

    for (int gy = gStartY; gy < gEndY; gy += step) {
        const int y = gy - goy;
        if (y < 0 || y >= cellSize) continue;
        // Bilinear y-weights (same 0.5-centre convention as the heightmap).
        const float gyf = (float(y) + 0.5f) * invCS + 1.0f;
        const int   y0  = std::clamp(int(std::floor(gyf - 0.5f)), 0, 2);
        const int   y1  = std::min(2, y0 + 1);
        const float fy  = std::clamp((gyf - 0.5f) - float(y0), 0.0f, 1.0f);
        for (int gx = gStartX; gx < gEndX; gx += step) {
            const int x = gx - gox;
            if (x < 0 || x >= cellSize) continue;
            const std::size_t idx = std::size_t(y) * cellSize + x;
            const std::uint8_t tile = out.tiles[idx];
            // Only place on empty / biome ground tiles. Keep walls, roads,
            // houses, water, etc. untouched.
            if (tile == TILE_ROAD || tile == TILE_HOUSE || tile == TILE_WALL
             || tile == TILE_FIELD || tile == TILE_WATER || tile == TILE_SHORE
             || tile == TILE_SQUARE || tile == TILE_TREE_DECOR) continue;

            // Suppress urban centre.
            if (clearRadius > 0) {
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy < clearSq) continue;
            }

            const float gxf = (float(x) + 0.5f) * invCS + 1.0f;
            const int   x0  = std::clamp(int(std::floor(gxf - 0.5f)), 0, 2);
            const int   x1  = std::min(2, x0 + 1);
            const float fx  = std::clamp((gxf - 0.5f) - float(x0), 0.0f, 1.0f);
            const float blendedRate =
                  rate[y0 * 3 + x0] * (1.0f - fx) * (1.0f - fy)
                + rate[y0 * 3 + x1] * fx * (1.0f - fy)
                + rate[y1 * 3 + x0] * (1.0f - fx) * fy
                + rate[y1 * 3 + x1] * fx * fy;
            if (blendedRate <= 0.0f) continue;

            // FBM cluster gate (global coords, seamless across cells).
            const float n1 = smooth_noise_ts(float(gx) * 0.015f,
                                             float(gy) * 0.015f, seed);
            const float n2 = smooth_noise_ts(float(gx) * 0.04f,
                                             float(gy) * 0.04f, seed + 7u);
            const float fbm = n1 * 0.7f + n2 * 0.3f;
            const float ns = std::clamp((fbm - 0.2f) / 0.5f, 0.0f, 1.0f);
            const float density = std::min(0.85f,
                blendedRate * float(step * step)) * ns;

            // Position-based hash for placement decision (deterministic per
            // global tile so adjacent cells produce identical trees).
            const float posHash = terrain_noise_ts(gx * 7 + 12345,
                                                   gy * 13 + 67890, seed);
            if (posHash >= density) continue;

            // Smooth alpine treeline + slope rule (owner: a massif's base may
            // be fully forested, its peaks carry nothing — like real
            // mountains). Heightmap is normalised against the 1500 m
            // kHeightScaleM (sub/height.h); trees thin out from
            // kMtnGrassTopH=0.72 (1080 m) and stop at kMtnRockBaseH=0.92
            // (1380 m) — the ONE treeline band, owned by sub/material.h so
            // the ground's grass→rock dither and the trees agree by sharing
            // the constants. Sized to the rebalanced massifs (floor ~0.60,
            // peaks ~0.98) so the upper slopes go bare while plains/forest
            // cells (~0.45-0.70) are untouched. Sampled from the cell
            // heightmap so the cap follows the actual relief.
            if (!out.heightmap.empty()) {
                const float hNorm = out.heightmap[idx];
                const float t = std::clamp(
                    (hNorm - kMtnGrassTopH) / (kMtnRockBaseH - kMtnGrassTopH),
                    0.0f, 1.0f);
                const float treelineSurvive = 1.0f - t * t * (3.0f - 2.0f * t);
                if (treelineSurvive < 1e-3f) continue;
                // Per-tile reroll against the smoothstep survival prob.
                const float survHash = terrain_noise_ts(gx * 31 + 99991,
                                                        gy * 37 + 88883, seed);
                if (survHash > treelineSurvive) continue;
                // No trees on faces steeper than ~35° (tan ≈ 0.70): a crown
                // pasted on a scarp reads as wallpaper, not a forest. Central
                // differences over ±2 tiles, clamped at the cell border —
                // deterministic because every global tile is scattered by
                // exactly one owning cell from its own heightmap.
                const int xm = std::max(0, x - 2), xp = std::min(cellSize - 1, x + 2);
                const int ym = std::max(0, y - 2), yp = std::min(cellSize - 1, y + 2);
                const float gxs = (out.heightmap[std::size_t(y) * cellSize + xp]
                                 - out.heightmap[std::size_t(y) * cellSize + xm])
                                / float(std::max(1, xp - xm));
                const float gys = (out.heightmap[std::size_t(yp) * cellSize + x]
                                 - out.heightmap[std::size_t(ym) * cellSize + x])
                                / float(std::max(1, yp - ym));
                // A GRADIENT, so it is priced by the curve's gain HERE
                // (sub/height.h height_gain_m) and not by the shoreline's:
                // the same field gradient is worth 1.5 km per unit at the
                // water and 1.5 Mm per unit at the roof, and trees care about
                // the metres, not the field.
                const float slope = std::sqrt(gxs * gxs + gys * gys)
                                  * height_gain_m(
                                        out.heightmap[std::size_t(y) * cellSize + x]);
                if (slope > 0.70f) continue;
            }

            // One procedural roll per tree inside the biome's metric band —
            // the individual's base height. The species multiplier and the
            // billboard that height becomes are sub/tree_atlas.h's business
            // (the species is only resolved at draw time, from the blended
            // macro temperature), so nothing here duplicates the draw law.
            const float treeH = cfg.treeMinHeightM
                + sizeRng.next_f01() * (cfg.treeMaxHeightM - cfg.treeMinHeightM);

            Structure s{};
            s.kind   = Structure::Tree;
            s.x      = float(x) + 0.5f;
            s.y      = float(y) + 0.5f;
            // Nominal footprint of the crown (metres). Trees are not solid,
            // so this is the record's own idea of its extent; the drawn quad
            // takes its width from tree_billboard() under the same ratio.
            s.radius = treeH * kTreeCrownRatio;
            s.height = treeH;
            out.structures.push_back(s);
            out.tiles[idx] = TILE_TREE_DECOR;
        }
    }
}

// Subworld road flattening — make road / square tiles look like flat planar
// pieces laid on the terrain, locally curvature-free but still tracking the
// large-scale relief slope. Pipeline:
//
//   1. Each road tile takes a wide 9×9 (r=4) box average from a snapshot of
//      the heightmap so neighbouring road tiles do not feed back into each
//      other within the same pass. This kills the high-frequency bumps the
//      generator stamped through the road corridor.
//
//   2. 12 iterations of pure Laplacian smoothing — `h = (h±1 + h±W) / 4` —
//      restricted to road / square tiles. Discrete Laplacian smoothing
//      converges to a harmonic function: zero local curvature → road tiles
//      become essentially planar over their connected corridor while still
//      following the long-wavelength slope. This is what "flat tiles laid
//      on the relief" reduces to mathematically.
//
//   3. Single shoulder pass — one ring of non-road neighbours is pulled 35 %
//      toward the average of its road-tile neighbours so the road edge does
//      not produce a visible cliff against the surrounding terrain.
//
// Smoothing is sparse: callers provide sorted road/square indices so the
// 3072x3072 composite does not need a full tile-grid scan on worker jobs.
void smooth_road_heights_indexed(std::vector<float>& hm,
                                 const std::vector<std::int32_t>& roadIdx,
                                 int width, int height) {
    if (hm.size() != std::size_t(width) * std::size_t(height)) return;
    if (roadIdx.empty()) return;

    // Pass 1: wide 25x25 (r=12) box average over road / square tiles,
    // sourced from a snapshot. Use an integral image so async composite
    // smoothing stays O(map area + road tiles), not O(road tiles * r^2).
    {
        const std::vector<float> src(hm);
        const int stride = width + 1;
        std::vector<float> integral(std::size_t(stride) * std::size_t(height + 1), 0.0f);
        for (int y = 0; y < height; ++y) {
            float rowSum = 0.0f;
            const std::size_t srcRow = std::size_t(y) * width;
            const std::size_t prevRow = std::size_t(y) * stride;
            const std::size_t dstRow = std::size_t(y + 1) * stride;
            for (int x = 0; x < width; ++x) {
                rowSum += src[srcRow + std::size_t(x)];
                integral[dstRow + std::size_t(x + 1)] =
                    integral[prevRow + std::size_t(x + 1)] + rowSum;
            }
        }

        constexpr int r = 12;
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            const int y0 = std::max(0, y - r), y1 = std::min(height - 1, y + r);
            const int x0 = std::max(0, x - r), x1 = std::min(width  - 1, x + r);
            const std::size_t a = std::size_t(y0) * stride + std::size_t(x0);
            const std::size_t b = std::size_t(y0) * stride + std::size_t(x1 + 1);
            const std::size_t c = std::size_t(y1 + 1) * stride + std::size_t(x0);
            const std::size_t d = std::size_t(y1 + 1) * stride + std::size_t(x1 + 1);
            const float sum = integral[d] - integral[b] - integral[c] + integral[a];
            const int cnt = (x1 - x0 + 1) * (y1 - y0 + 1);
            hm[std::size_t(i)] = cnt > 0 ? sum / float(cnt) : 0.5f;
        }
    }

    // Pass 2: 80 iterations of pure Laplacian smoothing on road tiles
    // only. No centre weight so curvature collapses fastest; iteration
    // count is high enough that the surface converges to harmonic over
    // the whole connected road component, not just locally. End result:
    // each road tile is essentially a flat planar piece sitting on the
    // long-wavelength terrain slope.
    for (int it = 0; it < 80; ++it) {
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            if (x <= 0 || x >= width - 1 || y <= 0 || y >= height - 1) continue;
            hm[i] = (hm[i - 1] + hm[i + 1]
                   + hm[i - width] + hm[i + width]) * 0.25f;
        }
    }

    // Pass 3: shoulder. For every NON-road tile that touches at least one
    // road neighbour, pull its height 55 % toward the average of its
    // road-tile neighbours. Eliminates the visible cliff at the road edge.
    // Keep the accumulation sparse and deterministic: mark road membership in
    // one byte per tile, collect shoulder samples, then sort/group them.
    {
        static constexpr int dx8[8] = {-1, 0, 1,-1, 1,-1, 0, 1};
        static constexpr int dy8[8] = {-1,-1,-1, 0, 0, 1, 1, 1};
        struct ShoulderSample {
            std::int32_t idx;
            float roadH;
        };
        std::vector<std::uint8_t> roadMask(hm.size(), 0);
        for (std::int32_t i : roadIdx) {
            roadMask[std::size_t(i)] = 1;
        }
        std::vector<ShoulderSample> shoulder;
        shoulder.reserve(roadIdx.size() * 4);
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            const float roadH = hm[std::size_t(i)];
            for (int k = 0; k < 8; ++k) {
                const int xn = x + dx8[k], yn = y + dy8[k];
                if (xn <= 0 || xn >= width - 1 || yn <= 0 || yn >= height - 1) continue;
                const std::int32_t j = yn * width + xn;
                if (roadMask[std::size_t(j)] != 0) continue;
                shoulder.push_back(ShoulderSample{j, roadH});
            }
        }
        std::sort(shoulder.begin(), shoulder.end(),
            [](const ShoulderSample& a, const ShoulderSample& b) {
                return a.idx < b.idx;
            });
        std::size_t pos = 0;
        while (pos < shoulder.size()) {
            const std::int32_t idx = shoulder[pos].idx;
            float sum = 0.0f;
            int cnt = 0;
            do {
                sum += shoulder[pos].roadH;
                ++cnt;
                ++pos;
            } while (pos < shoulder.size() && shoulder[pos].idx == idx);

            const float roadAvg = sum / float(cnt);
            const float orig = hm[std::size_t(idx)];
            hm[std::size_t(idx)] = orig + (roadAvg - orig) * 0.70f;
        }
    }
}

void smooth_road_heights(std::vector<float>& hm,
                         const std::vector<std::uint8_t>& tiles,
                         int width, int height) {
    if (hm.size() != std::size_t(width) * std::size_t(height)) return;
    if (tiles.size() != hm.size()) return;

    std::vector<std::int32_t> roadIdx;
    roadIdx.reserve(tiles.size() / 64);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const std::uint8_t tile = tiles[i];
        if (tile == TILE_ROAD || tile == TILE_SQUARE) {
            roadIdx.push_back(std::int32_t(i));
        }
    }
    smooth_road_heights_indexed(hm, roadIdx, width, height);
}

} // namespace sm::sub
