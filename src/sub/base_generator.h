// Universal subworld foundation — heightmap, BiomeConfig, grid primitives.
// Mirrors subworld/base-generator.ts.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "sub/map_data.h"

namespace sm::sub
{

    // THE PLANE ITSELF LIVES IN map_data.h — beside the two structs that own
    // it (`CellContext::seaLevel` carries it into generation,
    // `SubworldMapData::waterLevel` is the finished scene's answer). This file
    // holds the LAWS that consume it, and every one of them takes the plane as
    // an ARGUMENT rather than reading a constant.
    //
    // That is not style. The plane used to be spelled twice here — `WATER_LEVEL`
    // for the subworld's water surface and `kMacroSeaLevel` for "where macro
    // flips Water ↔ Land" — both hardcoded 0.40, both claiming to be
    // independent of the other. They never were: the remap below divides by one
    // and multiplies by the other, so they are the SAME number wearing two
    // names, and the macroworld's plane is an editor value the player moves
    // (`LayerParameters::seaLevel`, slider 0.10..0.80). Measured on the owner's
    // stand at 0.60: a water cell legally carries macroH up to 0.60, so
    // `t = macroH / 0.40` reached 1.5 (there was no upper clamp) and the water
    // BED came out at 1.5² × 0.40 = 0.90 while the visible plane still drew at
    // 0.40 — water buried under its own hills, «воды вообще не видно». Below
    // 0.40 the same arithmetic fails the other way and drowns the land. One
    // number, one argument, and both directions retire together.

    // THE cell-skeleton height law (normalised 0..1): the deterministic
    // per-cell column the generator bilinears into its macro manifold BEFORE
    // any noise or ridges (base_generator.cpp remapped[] / peakHeight[]).
    // Shared with the shadow-march apron (vk_renderer_3d upload): terrain
    // beyond the loaded window is represented by this skeleton at macro-cell
    // resolution, so out-of-window massifs keep casting into the window and
    // the march never pops at a recenter. Mountains answer with their CREST
    // base (the peak law minus its per-cell jitter/neighbour terms, unclamped
    // — the generator adds those on top): the crest is what decides "does
    // that ridge block the sun", and it is the level real in-window ridges
    // reach, so a massif sliding from apron to window changes its cast
    // shadow least.
    //
    // `seaLevel` is the SCENE'S plane (CellContext::seaLevel), inherited from
    // the macroworld's own — the law has no plane of its own to fall back on,
    // deliberately: a default here is how the two worlds drifted apart in the
    // first place.
    // РЕЛЬЕФ — ЭТО ПОЛЕ, И ОНО АГНОСТИЧНО (вердикт владельца 2026-09-30,
    // дословно): «уровень моря это буквально где будет вода начинаться в
    // макромире биом и где плоскость воды в микромире и всё»; «всё остальное
    // рельеф не волнует, вода там не вода, он агностичен»; «рельеф под ними
    // остаётся»; «у нас единая система рельефа от макромира».
    //
    // Макромир так и живёт: `is_water(cell) = rgba[cell*4] < seaLevel16`
    // (macro/map_generator.h) — ОДНО поле, ОДИН порог над ним, и `biome_at`
    // строит на нём весь каскад. Субмир был единственным местом, где то же
    // поле переписывалось тремя кривыми по флагу биома клетки: водяная жала
    // рельеф квадратично, сухопутная поднимала его на kLandMargin и
    // растягивала, горная сплющивала. Одно макро-число давало три разные
    // высоты в зависимости от того, как клетку назвали.
    //
    // ТЕПЕРЬ ОСТАЛАСЬ ОДНА ВЕТКА, И ОНА НЕ ПОДДЕЛКА ПОЛЯ, А БЮДЖЕТ ГРЕБНЯ.
    // Массив получает амплитуду хребтов сверху (mountain_ridges01), и сумма
    // обязана остаться под потолком меш-алиасинга, который охраняет
    // mountain_mesh_smoothness_test. Число пережило два круга ревью владельца
    // с замерами углов: 80-градусные стены (p50 43° / p90 70° / p99 78°,
    // e7bb958a) → перелёт в однородные купола (31/49/61, отвергнуто) →
    // компромисс 40/56/66 (2aa0c52f). Такое не сносят заодно — но оно и не
    // бесплатно: kMountainBiomeLevel = 0.75, значит все горы мира стоят в
    // полосе 0.9125..0.95, то есть в 56 метрах друг от друга, и на дальности,
    // где Найквист съедает хребты, массив читается плоским столом. Это
    // названо владельцем и решается отдельно.
    //
    // `isWater` и `seaLevel` ушли из сигнатуры вместе с кривыми: закон о
    // высоте больше не знает, что такое вода, и не может узнать.
    // ── ЗДЕСЬ ЖИЛА `skeleton_cell_height01`, И ОНА СНЕСЕНА ЦЕЛИКОМ ──
    // (вердикт владельца 2026-10-01: «сносить обе ветки», «приводи к
    // системности»). Она подменяла высоту горной клетке: поле гор лежит в
    // [kMountainBiomeLevel, 1.0], а ветка сплющивала его в полосу шириной
    // 0.056. Три следствия, все найдены владельцем на кадрах:
    //   • ВЕРХ МАССИВА — ПЛОСКИЙ СТОЛ, а на границе биома сосед 0.62 оставался
    //     0.62, сосед 0.63 прыгал на 0.894: обрыв 0.27 единицы поля за ОДНУ
    //     клетку. Гора была не горой, а месой, и «стена» — её борт;
    //   • NEAR И FAR РАСХОДИЛИСЬ НА ЭТОТ ЖЕ ПРЫЖОК (ближний путь размазывал
    //     его билинейным блендом, дальний ставил ступенью): дальняя гора
    //     вставала выше ближней земли на сотни метров. Замер: 467 м → 17 м;
    //   • её КЛАМП `[0.80, 1.04]` пережил первый снос в дальнем пути и, уже
    //     без ветки, перестал ограничивать и начал ПОДНИМАТЬ: медиана горных
    //     клеток 0.70, то есть весь дальний массив прибивался к одной полке
    //     3.4 км — плоская полоса на горизонте вместо гряды.
    //
    // ПОСЛЕ СНОСА ЗАКОН ОДИН И ОН НЕВЫРАЗИМО ПРОСТ: ВЫСОТА КЛЕТКИ ЕСТЬ ЕЁ
    // МАКРОВЫСОТА. Второго ответа нет не потому, что его согласовали, а
    // потому, что его негде написать — функции больше не существует, и оба
    // пути читают одно число. Распределение гор при этом то, которое владелец
    // принял замером: p25 1274 м, p50 1965 м, выше 10 км 0.005 % карты,
    // десятки в центре гряд с подъёмом 53 км; пик мира 10 623 м не упал.
    // The crest's per-cell jitter — hash noise of the cell's own PLACE and the
    // world seed. Declared here because the crest law above is inline and the
    // far world needs both; defined in base_generator.cpp beside the rest of
    // the noise stack.
    float crest_jitter01(int cellGX, int cellGY, std::uint32_t worldSeed);

    // THE cell-skeleton CREST law (normalised 0..1) — the peak a cell's ridges
    // aim at, and the twin of skeleton_cell_height01 above. Extracted from the
    // generator's own loop so there is exactly ONE of it: the far world builds
    // the same massifs the near world does, or the canon's «та же гора»
    // (S18.1) is a wish rather than a property.
    //
    // Every input is a property of the CELL'S PLACE — its macro height, whether
    // it is mountain, how many of its four neighbours are, and its wrapped
    // index — so two windows containing the same cell compute the same crest.
    // (They did not always: the jitter used to be seeded from whichever cell
    // was the window CENTRE, and massif borders stepped 8 m for it.)
    inline float skeleton_cell_peak01(float macroH, bool isMountain,
                                      int adjMountain, int cellGX, int cellGY,
                                      std::uint32_t worldSeed, float seaLevel) {
        const float jitter = crest_jitter01(cellGX, cellGY, worldSeed) - 0.5f;
        if (isMountain) {
            // ПОЛ 0.80 БЫЛ ВТОРОЙ ПОЛОВИНОЙ ТОЙ ЖЕ ВЕТКИ: он поднимал гребень
            // низкой горы в верхнюю полосу независимо от её земли, то есть
            // возвращал иглы ровно там, где ветка высоты их уже сгладила.
            // Гребень целится ВЫШЕ СВОЕЙ СОБСТВЕННОЙ ЗЕМЛИ, как и у равнины.
            return std::clamp(macroH + float(adjMountain) * 0.02f
                                  + jitter * 0.045f,
                              seaLevel + 0.10f, 1.05f);
        }
        // The crest floor is the plane plus a WIDTH: a non-mountain cell's
        // ridges aim at least this far above the water, whatever the water is.
        // `seaLevel` survives HERE and only here, because this is the one place
        // that genuinely asks about the plane — not about the ground's shape.
        return std::clamp(macroH
                              + 0.07f + float(adjMountain) * 0.015f
                              + jitter * 0.03f,
                          seaLevel + 0.10f, 1.05f);
    }

    // HOW LOUD A CELL'S OWN GROUND IS — one door, because it was two answers.
    //
    // This number scales the ground's detail octaves: a massif's flank is
    // rougher than a meadow, and a cell touching a massif is rougher than one
    // that does not. It was written out twice — once in the near generator's
    // per-cell loop and once in the far renderer's column gather — and the two
    // spellings agreeing was luck, not construction. It is also the column a
    // witness has to feed BOTH sides to compare them, and a witness carrying a
    // third copy would be testing that the copy was made (AGENTS testing law
    // 5), not that the worlds agree.
    //
    // 0.15 on a mountain cell against 0.1 + 0.1 per mountain NEIGHBOUR: the
    // massif itself keeps its detail modest because its shape is the ridge
    // law's business, while the apron around it — the foothills — is where the
    // ground's own noise does the most work. Four mountain neighbours reach
    // 0.5, the loudest ground in the world.
    constexpr float cell_mtn_scale01(bool isMountain, int adjMountain) {
        return isMountain ? 0.15f : (0.1f + float(adjMountain) * 0.1f);
    }

    // WHAT A CELL'S CONTENT DOES TO ITS OWN GROUND — the second one-door
    // column, and for the same reason as the first.
    //
    // A road bed, a town, a ploughed field CALM the land they stand on
    // (`terrain_mod_for` below states how much). The calming is applied to the
    // cell's OWN columns BEFORE any blending, so that a damped cell beside a
    // wild one still blends smoothly instead of stopping at a seam — and
    // because it happens before the blend, it is not a correction anyone can
    // apply afterwards.
    //
    // BOTH GROUNDS CALL THIS, and that is the whole point. The near generator
    // knew the law and the far world did not, which made a damped cell a seam
    // wherever it met the window's rim — and roads run in networks, so it met
    // it constantly. Measured on three seeds against a control row with no
    // content (frame −0.0 m): a road cell disagreed with the far ground by
    // p90 27–52 m and up to 126 m; through this door, p90 5.1 m and max
    // 14.9 m, i.e. the control row itself.
    constexpr void apply_cell_damp(float damp, float& mtnScale, float& ridgeW,
                                   float& gradient01) {
        if (!(damp > 0.0f)) return;
        const float d = damp < 1.0f ? damp : 1.0f;
        mtnScale   *= 1.0f - 0.7f * d;
        ridgeW     *= 1.0f - d;
        gradient01 *= 1.0f - 0.6f * d;
    }

    // THE MOUNTAIN SILHOUETTE — and THE far world's, because it is the same
    // function (CANON S18.1: «дальний рельеф не имеет права быть похожим шумом,
    // он обязан быть той же функцией, усечённой»).
    //
    // `coarseOnly` drops the fine octave the near ground carries — a ~6 m crag
    // grain at a ~120-tile wavelength, which at any distance where the far
    // world is drawn has been under a pixel for kilometres. That is the canon's
    // law of distance made literal: DETAIL IS REMOVED, never substituted. What
    // survives is the massif: the same warp, the same two crest octaves, the
    // same compression, the same period closing on the same world.
    //
    // `h` is the manifold the ridges rise out of; `macroH` / `peakTarget` /
    // `ridgeWeight` are the cell columns blended at this tile; `gx, gy` are
    // WORLD TILE coordinates (wrapped by the caller — the noise closes on
    // `worldTiles` and a tile is its place).
    // `seaLevel` floors the valley between the ridges: a massif's basin may sit
    // lower than the plain around it, never below the water.
    float mountain_ridges01(float h, int gx, int gy, float macroH,
                            float peakTarget, float ridgeWeight,
                            float worldTiles, bool coarseOnly, float seaLevel);

    // THE GROUND'S OWN DETAIL, and the law of WHICH octaves a mesh may carry.
    //
    // The near generator lays two octaves over its macro manifold: λ≈125 and
    // λ≈50 tiles. Dropping both from the far world was the mistake that made
    // its lowland a billiard table — at three kilometres a 125 m feature
    // spans 2.4° of screen, which is not detail, it is the ground.
    //
    // What decides is not a flag but NYQUIST: an octave whose wavelength is
    // shorter than twice the mesh's spacing cannot be drawn by that mesh at
    // all — sampling it only aliases. So `minWavelengthTiles` = 2 × step, and
    // the law reads: carry every octave the mesh can represent, drop exactly
    // those it cannot. That is «деталь УБИРАЕТСЯ, а не подменяется» with a
    // measure attached, and it is also why the finished far world is RINGS:
    // halving the step doubles the octaves it may carry, so the detail comes
    // back as you approach instead of being switched on.
    //
    // AND «УБИРАЕТСЯ» IS A STATEMENT ABOUT THE SURVIVORS TOO. A dropped octave
    // hands over its OWN MEAN, and the sum is divided by the FULL weight of the
    // stack — never by the weight of whoever is left. Dividing by the survivors
    // rescales them: with λ=50 gone, the λ=125 octave came out at 0.5/0.5 = 1.0
    // instead of its authored 0.5/0.75, i.e. 1.5× the amplitude the near ground
    // gives it, and the far world drew a LOUDER version of the same shape.
    // Measured contribution to the near↔far gap at the foothills: 7.2 → 5.6 m
    // of median disagreement (M-201). Substituting detail is what the law
    // forbids, and amplifying what remains is substitution.
    float terrain_detail01(int gx, int gy, float worldTiles,
                           float minWavelengthTiles);

    // THE FAR WORLD'S GROUND, in normalised height. It is the near generator
    // with its detail removed and nothing added: the macro manifold the cells
    // blend into, plus the massif that rises out of it at its coarse octaves.
    //
    // What is deliberately ABSENT is every term that is detail by nature —
    // the multi-octave ground noise, dunes, swamp dips, the settlement
    // plateau. None of them is visible at the ranges this draws, and a far
    // world that carried them would be paying to render what the air has
    // already eaten.
    //
    // The caller supplies the cell columns already blended at this tile (the
    // same bilinear over the four nearest cell CENTRES the near generator
    // does), because a far mesh samples a coarse cell grid once and reads many
    // tiles out of it — asking per tile would re-derive the same nine cells
    // for every vertex.
    //
    // DEFINED BESIDE THE NEAR LAW, IN base_generator.cpp — so that the two
    // grounds read ONE curve out of one translation unit, and «the far ground
    // is the near ground with detail removed» is a property of where the code
    // lives rather than a promise.
    //
    // It used to be an inline body here, and that is how it went a fortnight
    // without `detail_field_scale` (M-201): the curve arrived in a4f3b0b6,
    // the near generator picked it up in its own .cpp, and a header body had
    // no obvious way to reach it — `sub/height.h` appeared to sit ABOVE this
    // file. It did not: height.h asked for this header and used nothing from
    // it (every name it wants — WATER_LEVEL, kCellSize, kFullSize,
    // SubworldMapData — is map_data.h's), so the wall was an accident of one
    // #include line, not a layer. The line is corrected; the curve now sits
    // where it belongs, BELOW the generator, and this law stays here because
    // the near law is here.
    float far_height01(int gx, int gy, float macroH01, float peak01,
                       float ridgeWeight, float worldTiles,
                       float seaLevel,
                       float gradient01 = 0.0f,
                       float heightScale = 0.0f,
                       float mtnScale = 0.0f,
                       float minWavelengthTiles = 0.0f);

    struct BiomeConfig
    {
        float treeDensity;
        int treeStep;
        // Height band of a mature tree in METRES, before the per-species
        // scale (sub/tree_atlas.h). The band belongs to the PLACE: the same
        // pine is a 7 m tundra scrub and a 20 m taiga mast. Rolled per tree
        // and stored verbatim in Structure::height.
        float treeMinHeightM, treeMaxHeightM;
        float heightScale;
        // NO `waterLevel` COLUMN. All eleven rows held the same number, and
        // ELEVEN COPIES OF ONE NUMBER ARE NOT A COLUMN (DOD p.9): it had zero
        // readers — the scene's plane comes from `SubworldMapData::waterLevel`,
        // a dungeon's from its own kind row. A biome does not get to decide
        // where the sea is; the world does.
    };

    const BiomeConfig &biome_config(Biome b);

    // Universal terrain flattening from macro features & landmarks
    // ----------------------------------------------------------------
    // One data row per macro-cell content class decides how much a cell's
    // terrain calms down so the authored content sits naturally:
    //   `damp`     0..1 — scales DOWN ridge weight, mountain noise and the
    //              biome-edge gradient boost for the whole cell (applied to
    //              the per-cell 3×3 tables BEFORE bilinear blending, so
    //              transitions into un-damped neighbours stay seamless).
    //   `plateauR` tiles — radius of a radial pull toward the cell-centre
    //              macro height (smoothstep falloff, full strength at the
    //              centre). Settlements build around their cell centre, so
    //              this gives walls/houses a natural table to stand on
    //              instead of hanging off a mountain face. 0 = no plateau.
    // Adding a new glowing/flattening content class is one row in
    // `terrain_mod_for` — no per-mode terrain code.
    struct TerrainMod
    {
        float damp     = 0.0f;
        float plateauR = 0.0f;
    };

    // Combined mod for a cell: max(damp), max(plateauR) of its landmark class
    // (City/Village/Ruin/Spire) and its feature (roads damp their whole cell —
    // the carved corridor then reads as a pass, not a cliff stair).
    TerrainMod terrain_mod_for(LandmarkType landmark, FeatureType feature);

    // Relief uplift is derived from `nbBiome[9]` directly: mountain cells
    // (Biome::Mountain, elevation-classified) get a per-cell mountainScale of
    // 0.15 and ridgeWeight 1; neighbours of mountain cells get a gradient
    // 0.1 + 0.1 × adjMtn, all bilinearly blended. Features never affect
    // height — they scatter on top (trees) or carve tiles (roads).
    // (The 0.3 / 0.15 this used to quote were the numbers before the mountain
    // seam was flattened, 2026-09-16 — and the far world mirrors these very
    // columns, so a stale number here is a stale number on the horizon.)

    // Build a kCellSize² heightmap using neighbour-aware blending. `nbHeights`
    // is 9 macro heights in row-major order [NW, N, NE, W, C, E, SW, S, SE];
    // `nbBiome` is 9 matching biome enums (Biome::Mountain drives ridges).
    // Heights are remapped per-cell about `seaLevel` — THE SCENE'S PLANE,
    // inherited from the macroworld (CellContext::seaLevel), never a constant of
    // this layer (water cells get a squared deep-ocean curve, land cells get a
    // linear lift) and
    // ── ВЕС ТАЙЛА ПО 3×3 — ОДНО СОГЛАШЕНИЕ НА БАЗУ И НА ВСЕ МОДУЛИ ────────
    // Центры девяти клеток стоят на 0.5/1.5/2.5 в сетке окна, и ЛЮБАЯ
    // величина, которую надо сшить через границу клетки, блендится по ним
    // билинейно. База делает это со своими колонками; модулю, который кладёт
    // СВОЮ фактуру поверх базы (`gens/gens.h`: модуль опирается на слой НИЖЕ,
    // никогда на соседа), нужен тот же вес — иначе его фактура оборвётся на
    // шве ровно там, где база сшита.
    //
    // Дверь отдаёт ВЕСА, а не готовое число: четыре индекса и четыре доли
    // считаются раз на тайл, а блендить ими можно сколько угодно колонок. Так
    // соглашение написано ОДИН раз и при этом не стоит базе ни одного лишнего
    // умножения в её горячем цикле.
    struct NbWeights {
        int   i00 = 0, i10 = 0, i01 = 0, i11 = 0;   // индексы в кольце [9]
        float w00 = 1.0f, w10 = 0.0f, w01 = 0.0f, w11 = 0.0f;
    };
    NbWeights nb_weights(int x, int y, int cellSize);
    inline float blend9(const float tbl[9], const NbWeights& w) {
        return tbl[w.i00] * w.w00 + tbl[w.i10] * w.w10
             + tbl[w.i01] * w.w01 + tbl[w.i11] * w.w11;
    }

    // then bilinearly blended into a smooth manifold — this single pass
    // produces natural shorelines, river banks for single-cell water, and
    // gradients from plains to foothills to peaks. No post-clamping.
    // `nbMods` (optional, 9 entries) carries the per-neighbour TerrainMod —
    // road/settlement flattening resolved by the caller from macro context.
    // Null means "no macro content anywhere" (bare-terrain tests).
    void generate_heightmap(std::vector<float> &out,
                            int cellSize,
                            const float nbHeights[9],
                            const Biome nbBiome[9],
                            // THE CONTEXT IS ONE RING WIDER THAN THE CELLS IT
                            // BUILDS, and it has to be. The generator asks each
                            // of its nine cells "how many of YOUR four
                            // neighbours are mountain" — a question about that
                            // cell's own place — and a 3×3 cannot answer it for
                            // its own rim: a rim cell cannot see outward, so it
                            // undercounts, and its crest target becomes a
                            // function of WHICH WINDOW is asking. Measured at
                            // the foot of a massif, where it bites hardest:
                            // 8.3-9.7 m of step across a shared border, 1.3-3.4×
                            // the ground's own relief there, against 0.4-1.0×
                            // along the body of the same ridge.
                            //
                            // 25 entries, row-major, the SAME grid one ring
                            // wider: index (1+cy)*5 + (1+cx) is the 3×3 cell
                            // i = cy*3 + cx. Null is legal and means "no world
                            // around this fixture" — then the rim undercounts
                            // exactly as it always did, which is honest for a
                            // lone cell that has no neighbours to miss.
                            const Biome* nbBiome5,
                            Biome biome,
                            std::uint32_t seed,
                            int globalOffsetX,
                            int globalOffsetY,
                            // THE SCENE'S SEA PLANE, normalised — inherited from
                            // the macroworld through CellContext::seaLevel.
                            //
                            // No default, and IN THIS POSITION on purpose. A
                            // default is how the subworld came to remap about
                            // its own 0.40 while the map's plane sat elsewhere.
                            // The position is the other half of the guard:
                            // sitting before a POINTER, an omitted plane cannot
                            // be satisfied by anything, so every stale
                            // positional call fails to COMPILE. Put between
                            // `biome` and `seed` it was silently satisfied by
                            // the seed itself — the call built, the world
                            // remapped about 12345, and only a downstream mesh
                            // test noticed (caught exactly that way,
                            // 2026-09-27).
                            float seaLevel,
                            const TerrainMod *nbMods = nullptr,
                            // The world's width in CELLS. Every global-coordinate
                            // noise below closes on it, so the ground meets
                            // itself at the world's edge instead of stepping
                            // (CANON.md S1). 0 = a lone cell with no world
                            // around it (fixtures): do not wrap.
                            int worldCellsX = 0,
                            // THE WORLD seed, not the centre cell's hash above.
                            // The per-cell crest jitter is a property of the
                            // CELL, so every neighbour must derive it the same
                            // — which is only possible from the world seed plus
                            // the cell's own place (CellContext::worldSeed
                            // exists for exactly this, and the field-plot
                            // lattice already travels on it). 0 is a legal
                            // deterministic value for a fixture; what is NOT
                            // legal is deriving it from whoever is asking.
                            std::uint32_t worldSeed = 0);

    // Fill base tiles for a biome (open ground / forest scatter / desert).
    void fill_base_tiles(std::vector<std::uint8_t> &tiles, int cellSize,
                         Biome biome, std::uint32_t seed);

    // Mean survival of the FBM cluster gate in scatter_universal_trees —
    // calibrates macro tree COUNT → per-tile rate so the expected number of
    // PLACED trees over a full flat cell ≈ the cell's count. Measured by
    // tree_layer_test's calibration guard; retune there if the gate changes.
    constexpr float kTreeScatterYield = 0.58f;

    // Universal tree scatterer (evolved from the TS `scatterUniversalTrees`).
    // Walks the cell on ONE globally-aligned lattice keyed by
    // `(globalOffsetX, globalOffsetY)` so adjacent cells stitch their tree
    // distributions seamlessly. Tree density is 3×3-CONTEXTUAL and
    // COUNT-DRIVEN: each ring cell contributes a tree rate (trees/tile²)
    // derived from its macro tree count (`nbTreeCount`, macro/tree_layer.h —
    // the ONE per-cell scalar authority; 16384 = the golden densest forest),
    // bilinearly blended per node — forests deepen among forests, plains grow
    // a smooth опушка on their forest side, water (count 0) contributes none.
    // Placement uses the TS `terrainNoise` hash so identical global
    // coordinates always pick the same trees regardless of which cell
    // computed them. Skips tiles within `clearRadius` of the cell centre
    // (urban modes). Pushes a Structure::Tree per placed tree and stamps
    // TILE_TREE_DECOR — this is the ONE tree authority: every decor tile has
    // a real 3D tree, and felling one writes back to the macro count.
    void scatter_universal_trees(SubworldMapData &out,
                                 int cellSize,
                                 int globalOffsetX, int globalOffsetY,
                                 const Biome nbBiome[9],
                                 const int nbTreeCount[9],
                                 int clearRadius,
                                 std::uint32_t seed);

    // Smooth the heightmap under road / square tiles so paths read as carved
    // into the terrain. The implementation uses a sparse road-index pass, a
    // wide box average, an iterative Laplacian pass, and one shoulder blend.
    // No effect when the cell has no road/square tiles.
    void smooth_road_heights(std::vector<float>& hm,
                             const std::vector<std::uint8_t>& tiles,
                             int width, int height);

    // Same road smoother when the caller already owns a sorted list of
    // road/square tile indices. Used by async seam smoothing to avoid copying
    // and rescanning the full composite tile grid.
    // A terrain post-process has TWO radii and they are not the same number.
    // Confusing them makes a caller either wrong or slow:
    //
    //   INPUT reach  — how far away a height can INFLUENCE the result. Pass 1
    //     is a box average of radius 12; pass 2's 80 Laplacian iterations carry
    //     information roughly one tile per iteration along the road chain. This
    //     is what an apron would have to be sized by if this pass ever moved
    //     into per-cell generation (problems.md #14) — and it is finite, which
    //     is why that move is possible at all.
    //
    //   OUTPUT reach — how far from a road tile a height can actually CHANGE.
    //     Every pass writes road tiles only; pass 3 additionally writes their
    //     8-neighbour shoulder. One tile. This is what decides which cells a
    //     finished run has dirtied, and using the input reach here would mark
    //     seven cells where one was touched.
    inline constexpr int kRoadSmoothInputReach = 12 + 80;
    inline constexpr int kRoadSmoothOutputReach = 1;

    void smooth_road_heights_indexed(std::vector<float>& hm,
                                     const std::vector<std::int32_t>& roadIdx,
                                     int width, int height);

} // namespace sm::sub
