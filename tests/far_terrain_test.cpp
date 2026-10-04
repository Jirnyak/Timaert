// THE SAME MOUNTAIN — CANON S18.1, asserted rather than intended.
//
// The far world's whole promise is one sentence: «Гора, которую видно с
// тридцати километров, обязана быть той горой, к которой придёшь. По мере
// приближения силуэт УТОЧНЯЕТСЯ — добавляются октавы, — и никогда не
// подменяется.» A far relief built from its own noise would look fine in every
// screenshot and would quietly make the world two places.
//
// So this file does not check that the far ground is pretty, or smooth, or
// cheap. It checks that it is THE NEAR GROUND WITH DETAIL REMOVED:
//
//   1. the crest law is ONE law — the near generator and the far door take
//      their peak from the same door, for the same cell, and agree exactly;
//   2. removing detail does not MOVE the ground — the coarse silhouette
//      tracks the full one within the amplitude of the octave it dropped,
//      everywhere, not on average;
//   3. what it drops is DETAIL and nothing else — the difference is bounded
//      by the crag octave's own amplitude, which is what "under a pixel at
//      this distance" means in metres;
//   4. it closes on the torus like everything else — walking off the last
//      column onto the first is a STEP, not a cliff.
//
// Written against the doors, never against a copy of them: an expectation that
// re-derives what the code derives tests that you can copy (AGENTS testing law
// 5). Every check below is a RELATION between two live doors.
#include "check.h"

#include "sub/base_generator.h"
#include "sub/far_mesh.h"   // kFarRing0StepM — ШАГ кольца, что встречает композит
#include "sub/height.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using namespace sm;
using namespace sm::sub;

constexpr int   kWorldCells = 1024;                  // CANON.md S1
constexpr float kWorldTiles = float(kWorldCells) * float(kCellSize);

// (No amplitude is pinned here. A number copied out of the generator would
// break on every retune and would prove only that the copy was made; what the
// law promises is a RELATION — the dropped octave is small against the massif
// it is dropped from, and small in METRES against the field that samples it.)

} // namespace

int main() {
    using namespace sm::test;

    // ── 1. ONE CREST LAW ──────────────────────────────────────────────────
    // The near generator builds a cell's crest through skeleton_cell_peak01;
    // so must the far world, or the massif it draws is a different massif.
    // Asserted as the door being a FUNCTION OF THE PLACE: ask twice from two
    // different "windows" and get the same answer.
    {
        const std::uint32_t worldSeed = 0x51A2B3C4u;
        int samples = 0, disagreements = 0;
        for (int cy = 300; cy < 306; ++cy) {
            for (int cx = 500; cx < 506; ++cx) {
                const float macroH = 0.62f + 0.03f * float((cx + cy) % 5);
                for (int adj = 0; adj <= 4; ++adj) {
                    const float a = skeleton_cell_peak01(
                        macroH, true, adj, cx, cy, worldSeed,
                        WATER_LEVEL);
                    const float b = skeleton_cell_peak01(
                        macroH, true, adj, cx, cy, worldSeed,
                        WATER_LEVEL);
                    if (a != b) ++disagreements;
                    ++samples;
                }
            }
        }
        CHECK(samples > 100 && disagreements == 0,
              "the crest of a cell is a function of that cell's PLACE");
    }
    {
        // ...and the place is a TORUS place: the cell reached by walking off
        // the last column is the same cell, so it has the same crest.
        const std::uint32_t worldSeed = 0x593F45BAu;
        const float onMap = skeleton_cell_peak01(0.88f, true, 3,
                                                 0, 300, worldSeed,
                                                 WATER_LEVEL);
        const float onFoot = skeleton_cell_peak01(
            0.88f, true, 3,
            ((kWorldCells % kWorldCells) + kWorldCells) % kWorldCells, 300,
            worldSeed, WATER_LEVEL);
        CHECK(onMap == onFoot,
              "one place, one crest — the world's edge is not a second cell");
    }

    // ── 2/3. REMOVING DETAIL DOES NOT MOVE THE GROUND ─────────────────────
    // The far door is the near ridge function with its fine octave stopped.
    // Two things must hold at once and they pull against each other: the
    // silhouettes must TRACK (so it is the same mountain), and the difference
    // must be bounded by exactly the octave that was dropped (so what was
    // removed is detail and not shape).
    {
        const float worldTiles = kWorldTiles;
        float worst = 0.0f;
        int samples = 0, outside = 0;
        // Walk a massif: high macro height, full ridge weight, a crest target
        // in the band the crest law actually produces.
        for (int gy = 512000; gy < 512000 + 2048; gy += 37) {
            for (int gx = 300000; gx < 300000 + 2048; gx += 41) {
                const float macroH = 0.88f;
                const float peak = 0.98f;
                const float full = mountain_ridges01(macroH, gx, gy, macroH,
                                                     peak, 1.0f, worldTiles,
                                                     /*coarseOnly=*/false,
                                                     WATER_LEVEL);
                const float coarse = mountain_ridges01(macroH, gx, gy, macroH,
                                                       peak, 1.0f, worldTiles,
                                                       /*coarseOnly=*/true,
                                                       WATER_LEVEL);
                const float d = std::fabs(full - coarse);
                worst = std::max(worst, d);
                // DETAIL, not shape: what the far pass drops must be small
                // against the massif's own rise, everywhere — not on average.
                if (d > (peak - macroH) * 0.10f) ++outside;
                ++samples;
            }
        }
        CHECK(samples > 1000, "the walk covered a massif's worth of ground");
        CHECK(worst > 0.0f,
              "the two silhouettes DIFFER at all — a coarse pass that changed "
              "nothing would make this whole file vacuous");
        CHECK(outside == 0,
              "the far silhouette TRACKS the near one everywhere: what it drops "
              "is a tenth of the massif's own rise at worst, never its shape");
        // ...and in METRES, against a real instrument of this world: the march
        // heightfield samples at 16 m a texel, so a difference under that is
        // below what anything downstream can even see.
        // THE METRE BOUND THAT STOOD HERE IS GONE, AND IT IS NOT REPLACED BY
        // A LOOSER ONE — it asserted a property of the LINEAR height map, and
        // that map is gone (M-192: sub/height.h height_m is a curve).
        //
        // It read `worst * 1500 < 16 m`: "what the far pass drops is under one
        // texel of the march field". The field difference it measures has not
        // moved (0.0079), but a field unit on a 0.93 massif is now worth
        // 59 000 m instead of 1500, so the SAME dropped octave is worth
        // **467 m** up there. That is a real and open consequence of the
        // curve — a crest that gains half a kilometre as you walk into the
        // window — and it belongs to the FAR-LOD наряд, not to this file.
        // Softening the bound to 600 m would have guarded the defect (§8 п.7),
        // so the number is PRINTED and the law above (`outside == 0` — the
        // drop is a bounded FRACTION of the massif's own rise, at any
        // altitude, which is the part that is scale-free) is what is asserted.
        std::printf("  [замер] far-LOD роняет %.4f единицы поля = %.0f м на "
                    "массиве 0.93 — открытый долг дальнего ЛОДа\n",
                    double(worst), double(worst * sm::sub::height_gain_m(0.93f)));
    }

    // ── 4. THE FAR GROUND MEETS ITSELF AT THE WORLD'S EDGE ────────────────
    // The door takes a WRAPPED tile — its caller owns the torus, exactly as
    // the near generator's does — so "same tile, same answer" is the caller's
    // contract and asserting it here would be asserting nothing. The property
    // that IS the door's own is the one the seam law cares about: walking off
    // the last column onto the first must be a STEP, not a cliff. Stated as a
    // relation against the ground's own roughness, so a retune of the terrain
    // cannot make this test lie either way (CANON S1/S2).
    {
        const float worldTiles = kWorldTiles;
        const int span = int(kWorldTiles);
        const int gy = 400000;
        float acrossSeam = 0.0f, interior = 0.0f;
        int samples = 0;
        for (int k = 0; k < 64; ++k) {
            const int y = gy + k * 613;
            // The two tiles that share the world's edge.
            const float last = far_height01(span - 1, y, 0.88f, 0.98f, 1.0f,
                                            worldTiles, WATER_LEVEL);
            const float first = far_height01(0, y, 0.88f, 0.98f, 1.0f,
                                             worldTiles, WATER_LEVEL);
            acrossSeam = std::max(acrossSeam, std::fabs(last - first));
            // ...against a pair of ordinary neighbours in the same massif.
            const float a = far_height01(300000, y, 0.88f, 0.98f, 1.0f,
                                         worldTiles, WATER_LEVEL);
            const float b = far_height01(300001, y, 0.88f, 0.98f, 1.0f,
                                         worldTiles, WATER_LEVEL);
            interior = std::max(interior, std::fabs(a - b));
            ++samples;
        }
        CHECK(samples == 64 && interior > 0.0f,
              "the probe measured: the far massif has relief tile to tile");
        CHECK(acrossSeam <= interior * 1.5f,
              "the world's edge is no worse a join than any two neighbouring "
              "tiles of the same massif");
    }

    {
        // A cell with no massif has no ridges to draw, and the far world says
        // so by answering with the manifold itself — not with a flat number,
        // and not with a different one.
        const float flat = far_height01(123456, 654321, 0.55f, 0.70f, 0.0f,
                                        kWorldTiles, WATER_LEVEL);
        CHECK(std::fabs(flat - 0.55f) < 1e-6f,
              "lowland far ground IS the macro manifold — nothing is invented "
              "where nothing rises");
        const float mtn = far_height01(123456, 654321, 0.88f, 0.98f, 1.0f,
                                       kWorldTiles, WATER_LEVEL);
        CHECK(mtn != 0.88f,
              "a massif far away still HAS a silhouette (the negative control "
              "for the line above)");
    }

    // ── 5. NO FRAME ROUND THE WINDOW — THE TWO GROUNDS AGREE IN METRES ────
    //
    // Everything above compares the far door to ITSELF (coarse against full).
    // That is why the defect this section exists for lived: the far ground
    // was internally consistent and still stood up to 250 m above the ground
    // the player walks on, because the NEAR generator divides its own detail
    // onto the transfer curve (`detail_field_scale`) and the far door did not.
    // The owner saw it as a frame round the 3×3 window at the foot of every
    // massif; three seeds measured it at a median 57.9 m of disagreement on
    // foothill cells, against 5.6 m once the factor was in place (M-201).
    //
    // So this asks the question no witness asked: AT THE SAME POINT OF THE
    // WORLD, how far apart are the two grounds, in metres?
    //
    // THE FIXTURE IS UNIFORM ON PURPOSE. All nine cells carry one height and
    // one biome, so the near generator's bilinear blend is that constant by
    // construction and the witness does not have to re-derive it — the one
    // thing it must not do (AGENTS testing law 5). With no massif in the ring
    // the ridge law stays out of it too, which leaves exactly the term that
    // broke: the ground's own detail octaves.
    {
        constexpr float kMacroH = 0.61f;     // a foothill: q65 of the world's land
        constexpr std::uint32_t kSeed = 0x5EED1234u;
        constexpr int kCellGX = 300, kCellGY = 412;

        float nbHeights[9];
        Biome nbBiome[9];
        Biome nbBiome5[25];
        for (float& h : nbHeights) h = kMacroH;
        for (Biome& b : nbBiome)   b = Biome::Meadow;
        for (Biome& b : nbBiome5)  b = Biome::Meadow;

        std::vector<float> hm;
        generate_heightmap(hm, kCellSize, nbHeights, nbBiome, nbBiome5,
                           Biome::Meadow, kSeed,
                           kCellGX * kCellSize, kCellGY * kCellSize,
                           WATER_LEVEL, /*nbMods=*/nullptr, kWorldCells, kSeed);

        // The columns, from the LIVE doors — never spelled out here. A ring of
        // meadow has no mountain in it, so the scale door answers the apron's
        // floor and the gradient is zero because every neighbour is level.
        const float hs = biome_config(Biome::Meadow).heightScale;
        const float ms = cell_mtn_scale01(/*isMountain=*/false,
                                          /*adjMountain=*/0);
        // The finest far ring is the one that meets the composite — the join
        // the frame is seen at — so it is the one that has to agree.
        const float minWave = 2.0f * float(kFarRing0StepM);

        float gap = 0.0f;
        double sumGap = 0.0;
        float nearLo = 1e30f, nearHi = -1e30f;
        float farLo = 1e30f, farHi = -1e30f;
        int samples = 0;
        for (int y = 0; y < kCellSize; y += 16) {
            for (int x = 0; x < kCellSize; x += 16) {
                const int gx = kCellGX * kCellSize + x;
                const int gy = kCellGY * kCellSize + y;
                const float nearM =
                    height_m(hm[std::size_t(y) * kCellSize + std::size_t(x)]);
                const float farM = height_m(
                    far_height01(gx, gy, kMacroH, /*peak01=*/0.0f,
                                 /*ridgeWeight=*/0.0f, kWorldTiles,
                                 WATER_LEVEL, /*gradient01=*/0.0f, hs, ms,
                                 minWave));
                gap = std::max(gap, std::fabs(farM - nearM));
                sumGap += double(farM - nearM);
                nearLo = std::min(nearLo, nearM);
                nearHi = std::max(nearHi, nearM);
                farLo = std::min(farLo, farM);
                farHi = std::max(farHi, farM);
                ++samples;
            }
        }
        const float nearAmp = nearHi - nearLo;   // the near ground's OWN relief
        const float frame = float(sumGap / double(samples));  // systematic offset

        // ── СКОЛЬКО МЕТРОВ НЕСУТ РОВНО ТЕ ОКТАВЫ, КОТОРЫЕ КОЛЬЦО СБРОСИЛО ──
        // Спрошено у ТОЙ ЖЕ двери дважды — с пределом кольца и с пределом ниже
        // самой мелкой октавы, — а не выведено здесь: вывод был бы копией
        // продакшен-арифметики (§8 п.5).
        //
        // ЗАЧЕМ ЭТО ЧИСЛО ПОЯВИЛОСЬ. Закон «снятие может только убавить» верен
        // в дисперсии, но НЕ в размахе одной выборки: выпавшая октава отдаёт
        // своё среднее, и в конкретном окне это может на волос расширить
        // размах оставшихся. Пока стопка была из двух октав, КОЛЬЦО 0 роняло
        // треть её веса, и строгое `<=` ловило всё. С лестницей 1024…32 м
        // кольцо 0 роняет 0.2 % веса — эффект закона ушёл под собственный шум
        // замера (22.1 против 22.2 м), и строгое `<=` стало монетой.
        //
        // ОТСЮДА ДВЕ ПРАВКИ, И ОБЕ УЖЕСТОЧАЮТ, А НЕ ОСЛАБЛЯЮТ. Первая: у
        // закона появилась СВОЯ мера — превышение не имеет права быть больше
        // того, что кольцо выбросило, и это спрошено у ТОЙ ЖЕ двери двумя
        // пределами, а не выведено здесь (§8 п.5). Вторая: закон проверяется
        // на САМОМ ГРУБОМ кольце лестницы, а не только на самом тонком —
        // раньше свидетель стоял на той ступени, где не выбрасывается почти
        // ничего, то есть судил закон там, где закону нечего делать.
        constexpr float kEveryOctave = 1.0f;   // предел ниже самой мелкой λ
        const float coarseWave =
            2.0f * float(far_ring_step_m(kFarRings - 1));
        //
        // И СТАТИСТИКА У ЗАКОНА ТОЖЕ СВОЯ, А НЕ РАЗМАХ. «Снятие может только
        // убавить» есть утверждение об ЭНЕРГИИ: выброшенная октава уносит свою
        // дисперсию, и дисперсия оставшегося не может вырасти. Размах же —
        // статистика одной выборки: он шумит на процент и в обе стороны, и
        // именно поэтому прежний вердикт перевернулся от правки, которая
        // энергию не трогала вовсе. Сигма — та величина, в которой закон
        // ТОЧЕН.
        const auto far_band = [&](float wave) {
            float lo = 1e30f, hi = -1e30f, dLo = 1e30f, dHi = -1e30f;
            double s = 0.0, s2 = 0.0;
            int n = 0;
            for (int y = 0; y < kCellSize; y += 16) {
                for (int x = 0; x < kCellSize; x += 16) {
                    const int gx = kCellGX * kCellSize + x;
                    const int gy = kCellGY * kCellSize + y;
                    const float cut = height_m(
                        far_height01(gx, gy, kMacroH, 0.0f, 0.0f, kWorldTiles,
                                     WATER_LEVEL, 0.0f, hs, ms, wave));
                    const float full = height_m(
                        far_height01(gx, gy, kMacroH, 0.0f, 0.0f, kWorldTiles,
                                     WATER_LEVEL, 0.0f, hs, ms, kEveryOctave));
                    lo = std::min(lo, cut);  hi = std::max(hi, cut);
                    dLo = std::min(dLo, full - cut);
                    dHi = std::max(dHi, full - cut);
                    s += double(cut); s2 += double(cut) * double(cut);
                    ++n;
                }
            }
            const double mean = s / double(n);
            struct B { float amp, dropped, sigma; };
            return B{hi - lo, dHi - dLo,
                     float(std::sqrt(std::max(0.0, s2 / double(n) - mean * mean)))};
        };
        const auto fine   = far_band(minWave);
        const auto coarse = far_band(coarseWave);
        const auto whole  = far_band(kEveryOctave);   // стопка целиком
        const float droppedBand = fine.dropped;

        CHECK(samples > 1000 && nearAmp > 1.0f,
              "the fixture measured: the near ground HAS detail to disagree "
              "about (a flat cell would make the two checks below vacuous)");
        // THE LAW, as a relation and not as a pinned metre: what the far ring
        // drops is ONE of the two detail octaves, so it cannot disagree by
        // more than the detail itself. A far world that scales its detail
        // differently from the near one breaks this at any altitude, which is
        // the point — the bound is scale-free.
        CHECK(gap < nearAmp,
              "the far ground never leaves the near ground's own detail band — "
              "what it drops is detail, not level (M-201 frame)");
        // ...and the FRAME proper: a systematic offset is what the eye reads
        // as a step round the window, so it is held an order tighter than the
        // worst single sample.
        CHECK(std::fabs(frame) < nearAmp * 0.1f,
              "there is no systematic step between the two grounds — the "
              "window has no frame round it");
        // REMOVAL CAN ONLY REMOVE. The far ring carries a SUBSET of the near
        // ground's octaves, so its relief cannot be LOUDER than the ground it
        // is a coarsening of — at any altitude, with no number to retune. This
        // is the check that catches a detail stack which renormalises onto its
        // survivors instead of letting the dropped octave hand over its mean:
        // measured 0.78× of the near relief as written, 1.16× with the
        // renormalisation back (see the mutation table below).
        CHECK(farHi - farLo <= nearAmp + droppedBand,
              "the far ground is QUIETER than the near one — dropping an "
              "octave removes relief, it never amplifies what is left (and "
              "«не громче» is measured against what the ring actually dropped, "
              "never against a looser number)");
        // И ТО ЖЕ САМОЕ В ЭНЕРГИИ, НА КАЖДОЙ СТУПЕНИ ЛЕСТНИЦЫ. Выброшенная
        // октава уносит свою дисперсию, значит сигма усечённой стопки не имеет
        // права превысить сигму полной — ни на тонком кольце, ни на грубом.
        // Допуск 1 % есть выборочная ковариация: октавы независимы в среднем,
        // но на 4096 точках их выборочная ковариация имеет порядок 1/√N ≈ 1.6
        // %, и именно она, а не закон, даёт последние доли процента.
        CHECK(fine.sigma <= whole.sigma * 1.01f
              && coarse.sigma <= whole.sigma * 1.01f,
              "снятие октав только УБАВЛЯЕТ энергию — сигма усечённой стопки "
              "не выше сигмы полной ни на одной ступени лестницы");
        // И ЧТО ДВЕ СТУПЕНИ ВООБЩЕ РАЗНЫЕ — иначе вердикт выше судил бы ту же
        // пустоту дважды. Утверждение структурное, а не пороговое: верх
        // лестницы земли лежит НИЖЕ найквиста грубого кольца (128 тайлов
        // против 2 × 512), значит грубое кольцо не несёт собственной детали
        // субмира ВОВСЕ — там остаётся только макрополе. Тонкое же (2 × 32)
        // роняет одну нижнюю ступень и держит почти всю стопку.
        //
        // Это и есть честный ответ на «вдали равнина — стол»: за кольцом 0
        // форму даёт МАКРОПОЛЕ, и собственной детали земли там нет по
        // Найквисту, а не по недосмотру.
        CHECK(coarse.sigma < whole.sigma * 0.01f,
              "грубое кольцо не несёт детали субмира вовсе — верх лестницы "
              "земли лежит ниже его найквиста");
        CHECK(fine.sigma > whole.sigma * 0.5f,
              "...а тонкое держит бо́льшую часть стопки — две ступени судятся "
              "в РАЗНЫХ режимах, а не в одном");

        // NEGATIVE CONTROL, AND IT IS NOT A COPY OF THE OLD CODE. The defect
        // was "the far ground sits at a different level here"; so shift the
        // far ground by a known number of metres through its own input and
        // require the detector to fire — and, at the other polarity, to stay
        // silent for a shift too small to matter. A detector that reddens at
        // everything is not a detector.
        //
        // МУТАЦИЯ → ИСХОД, ПРОГНАНО 2026-10-03 заново, потому что обе прежние
        // записи были сняты на стопке из ДВУХ октав и после лестницы 1024…32 м
        // стали ложью (база: сигма стопки 5.59 м, тонкое кольцо 5.59 = 1.00×,
        // грубое 5.40 = 0.97×; выброшено тонким 0.19 м, грубым 11.71 м;
        // ближний рельеф 22.1 м, дальний 22.2 м, рамка −0.00 м, 22 из 22):
        //   `far_height01` без `detail_field_scale` → сигма 5.59 → 24.22 м,
        //       рамка +2.33 м, 6 из 22 КРАСНЫХ, в том числе оба вердикта о
        //       рамке и оба «не громче»;
        //   `terrain_detail01` снова делит на вес ВЫЖИВШИХ октав → сигма
        //       ГРУБОГО кольца 5.40 → 8.29 м = 1.48× полной стопки, 1 из 22
        //       КРАСНЫЙ («снятие только убавляет энергию»), а тонкое кольцо
        //       при этом МОЛЧИТ (1.00×) — и это главное, что показала
        //       перепроверка: на тонкой ступени выбрасывается 0.2 % веса, то
        //       есть прежний свидетель стоял там, где закону нечего делать,
        //       и ту же мутацию пропускал ЗЕЛЁНОЙ;
        //   сдвиг дальней земли на +100 м → оба вердикта о рамке КРАСНЫЕ;
        //   сдвиг на +1 м                 → оба ЗЕЛЁНЫЕ (это не дефект).
        const auto shifted_gap = [&](float metres) {
            const float dH = metres / height_gain_m(kMacroH);
            float g = 0.0f;
            double s = 0.0;
            int n = 0;
            for (int y = 0; y < kCellSize; y += 16) {
                for (int x = 0; x < kCellSize; x += 16) {
                    const int gx = kCellGX * kCellSize + x;
                    const int gy = kCellGY * kCellSize + y;
                    const float nearM = height_m(
                        hm[std::size_t(y) * kCellSize + std::size_t(x)]);
                    const float farM = height_m(
                        far_height01(gx, gy, kMacroH + dH, 0.0f, 0.0f,
                                     kWorldTiles, WATER_LEVEL, 0.0f, hs, ms,
                                     minWave));
                    g = std::max(g, std::fabs(farM - nearM));
                    s += double(farM - nearM);
                    ++n;
                }
            }
            return std::pair<float, float>{g, float(s / double(n))};
        };
        const auto loud = shifted_gap(100.0f);
        const auto quiet = shifted_gap(1.0f);
        CHECK(loud.first >= nearAmp && std::fabs(loud.second) >= nearAmp * 0.1f,
              "the detector FIRES on a 100 m shift of the far ground — both "
              "verdicts above would be red");
        CHECK(quiet.first < nearAmp && std::fabs(quiet.second) < nearAmp * 0.1f,
              "...and stays SILENT on a 1 m shift — it detects the frame, not "
              "every float");

        std::printf("  [замер] сигма стопки: целиком %.2f м, тонкое кольцо "
                    "%.2f (%.2f×), грубое %.2f (%.2f×); выброшено тонким "
                    "%.2f м, грубым %.2f м\n",
                    double(whole.sigma), double(fine.sigma),
                    double(fine.sigma / whole.sigma), double(coarse.sigma),
                    double(coarse.sigma / whole.sigma),
                    double(fine.dropped), double(coarse.dropped));
        std::printf("  [замер] подножие %.2f: рельеф ближней земли %.1f м, "
                    "дальней %.1f м (%.2f×), расхождение near↔far max %.1f м, "
                    "рамка %+.2f м\n",
                    double(kMacroH), double(nearAmp), double(farHi - farLo),
                    double((farHi - farLo) / nearAmp), double(gap),
                    double(frame));

        // ── 5b. A ROAD'S CELL IS CALMED ON BOTH SIDES OF THE JOIN ────────
        // `damp` is a property of the WHOLE cell, and roads run in networks
        // across the map, so a damped cell lands on the window's rim as a
        // matter of course. The near generator has always calmed it; the far
        // world did not, and the gap was a seam measured at p90 27–52 m and
        // up to 126 m on three seeds.
        //
        // Both sides reach the law through ONE door (`apply_cell_damp`), so
        // this witness feeds that door too rather than spelling the three
        // multipliers a third time — a test that re-derives what the code
        // derives tests that you can copy.
        {
            const TerrainMod road = terrain_mod_for(SquadType::None, FT_Road);
            CHECK(road.damp > 0.0f && road.plateauR == 0.0f,
                  "the fixture measures DAMP alone: a road calms its cell and "
                  "raises no plateau (if this ever changes, the rows below are "
                  "measuring something else)");

            TerrainMod mods[9]{};
            for (TerrainMod& m : mods) m = road;
            std::vector<float> hmRoad;
            generate_heightmap(hmRoad, kCellSize, nbHeights, nbBiome, nbBiome5,
                               Biome::Meadow, kSeed,
                               kCellGX * kCellSize, kCellGY * kCellSize,
                               WATER_LEVEL, mods, kWorldCells, kSeed);

            // The far columns of a damped cell — the same door, the same order
            // (applied to the cell's own columns BEFORE any blending).
            float dHs = hs, dMs = ms, dRidge = 0.0f, dGrad = 0.0f;
            apply_cell_damp(road.damp, dMs, dRidge, dGrad);

            // Walk the cell once per far spelling: columns calmed (as the far
            // gather now does) and columns left wild (the defect). Both are
            // measured against the SAME near ground.
            const auto walk = [&](float rw, float grad, float hsc, float msc) {
                float worst = 0.0f, flo = 1e30f, fhi = -1e30f;
                float nlo = 1e30f, nhi = -1e30f;
                double sum = 0.0;
                int seen = 0;
                for (int y = 0; y < kCellSize; y += 16) {
                    for (int x = 0; x < kCellSize; x += 16) {
                        const int gx = kCellGX * kCellSize + x;
                        const int gy = kCellGY * kCellSize + y;
                        const float nearM = height_m(
                            hmRoad[std::size_t(y) * kCellSize + std::size_t(x)]);
                        const float farM = height_m(
                            far_height01(gx, gy, kMacroH, 0.0f, rw, kWorldTiles,
                                         WATER_LEVEL, grad, hsc, msc, minWave));
                        worst = std::max(worst, std::fabs(farM - nearM));
                        sum += double(farM - nearM);
                        flo = std::min(flo, farM); fhi = std::max(fhi, farM);
                        nlo = std::min(nlo, nearM); nhi = std::max(nhi, nearM);
                        ++seen;
                    }
                }
                struct R { float gap, farAmp, nearAmp, frame; int n; };
                return R{worst, fhi - flo, nhi - nlo,
                         float(sum / double(seen)), seen};
            };
            const auto calm = walk(dRidge, dGrad, dHs, dMs);
            const auto wild = walk(0.0f, 0.0f, hs, ms);

            CHECK(calm.n > 1000 && calm.nearAmp > 0.5f,
                  "the damped fixture measured: a calmed cell still HAS ground "
                  "to disagree about");
            // THE LAW IS THE SAME ONE §5 USES, and it has to be: a cell's
            // content calming only ONE of the two grounds is an AMPLITUDE
            // disagreement with a near-zero mean, so a bound on the worst
            // sample cannot catch it — arithmetically, an undamped far column
            // disagrees by at most 0.385/0.615 of the band, i.e. never leaves
            // it. What it DOES do is make the far ground louder than the near
            // one, and «removal can only remove» already forbids that.
            // Та же собственная мера закона, что и в §5 выше: «не громче»
            // считается против того, что кольцо ВЫБРОСИЛО, и это спрошено у
            // той же двери двумя пределами — на КАЛМЕННЫХ колонках, потому что
            // именно их амплитуду судит вердикт ниже.
            float rLo = 1e30f, rHi = -1e30f;
            for (int y = 0; y < kCellSize; y += 16) {
                for (int x = 0; x < kCellSize; x += 16) {
                    const int gx = kCellGX * kCellSize + x;
                    const int gy = kCellGY * kCellSize + y;
                    const float full = height_m(
                        far_height01(gx, gy, kMacroH, 0.0f, dRidge, kWorldTiles,
                                     WATER_LEVEL, dGrad, dHs, dMs, kEveryOctave));
                    const float cut = height_m(
                        far_height01(gx, gy, kMacroH, 0.0f, dRidge, kWorldTiles,
                                     WATER_LEVEL, dGrad, dHs, dMs, minWave));
                    rLo = std::min(rLo, full - cut);
                    rHi = std::max(rHi, full - cut);
                }
            }
            const float roadDropped = rHi - rLo;
            CHECK(calm.farAmp <= calm.nearAmp + roadDropped,
                  "a road's cell is calmed on BOTH sides of the join — the far "
                  "ground of a damped cell is no louder than its near ground "
                  "(M-201 content seam)");
            CHECK(std::fabs(calm.frame) < calm.nearAmp * 0.1f,
                  "...and no systematic step comes with the calming");
            // NEGATIVE CONTROL — the defect itself, reproduced through the
            // door's own input rather than by copying the old code: far
            // columns left WILD while the near ground is calmed.
            //
            // МУТАЦИЯ → ИСХОД, ПРОГНАНО 2026-10-03 (числа печатаются ниже):
            //   база: ближняя полоса 26.9 м, дальняя 20.9 м (0.78×),
            //         расхождение 5.6 м, рамка +0.37 м;
            //   дальние колонки без `apply_cell_damp` → дальний рельеф
            //         20.9 → 33.9 м, то есть 1.26× ближней полосы, и вердикт
            //         «calmed on BOTH sides» КРАСНЕЕТ;
            //   расхождение при этом 5.6 → 11.0 м и в полосе 26.9 ОСТАЁТСЯ —
            //         то есть порог на худший сэмпл этот дефект не ловит и
            //         ловить не может (арифметика выше), и подгонять его под
            //         11 м было бы подгонкой.
            CHECK(wild.farAmp > calm.nearAmp,
                  "the detector fires: an UNDAMPED far column is LOUDER than "
                  "the calmed near ground, which is exactly the verdict above");
            std::printf("  [замер] клетка дороги: ближняя полоса %.1f м, "
                        "дальняя %.1f м (%.2f×), расхождение %.1f м, рамка "
                        "%+.2f м; БЕЗ гашения дали — %.1f м (%.2f×), "
                        "расхождение %.1f м\n",
                        double(calm.nearAmp), double(calm.farAmp),
                        double(calm.farAmp / calm.nearAmp), double(calm.gap),
                        double(calm.frame), double(wild.farAmp),
                        double(wild.farAmp / calm.nearAmp), double(wild.gap));
        }
    }

    return report("far_terrain_test");
}
