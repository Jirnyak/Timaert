#include "check.h"

#include "tables/biomes.h"
#include "macro/features.h"
#include "macro/movement_cost.h"
#include "macro/pathfinding.h"
#include "macro/zones.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace
{

    bool nearly(float a, float b)
    {
        return std::fabs(a - b) < 0.0001f;
    }

    sm::PathCostData flat_grid(int w, int h, float cost)
    {
        sm::PathCostData data;
        data.width = w;
        data.height = h;
        data.costGrid.assign(std::size_t(w) * std::size_t(h), cost);
        return data;
    }

    // ── ФИКСТУРА АВТОРИТ УРОВНЕМ ПОЛЯ, А НЕ БАЙТОМ КАРТЫ ─────────────────
    // Карта хранит СЛОВО (`kFieldWordMax`): байт 140, уложенный в канал как
    // есть, значит не «суша 0.549», а 0.002 — воду, и компилятор об этом
    // молчит. Поэтому каждый уровень едет единственной дверью записи
    // `field_word_of`, обратной к `field01_of`, которой его читает мир.
    constexpr float kLandLevel01 = 140.0f / 255.0f;   // суша, но НЕ гора
    constexpr std::uint16_t kClimateMid =
        sm::field_word_of(128.0f / 255.0f);           // середина матрицы
    constexpr std::uint16_t kLandMask = std::uint16_t(sm::kFieldWordMax);

    sm::TerrainData make_terrain(int w, int h)
    {
        sm::TerrainData td;
        td.width = w;
        td.height = h;
        td.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, 0u);
        for (int i = 0; i < w * h; ++i)
        {
            const std::size_t s = std::size_t(i) * 4u;
            td.rgba[s + 0] = sm::field_word_of(kLandLevel01);
            td.rgba[s + 1] = kClimateMid;
            td.rgba[s + 2] = kClimateMid;
            td.rgba[s + 3] = kLandMask;
        }
        // РОЖДЕНИЕ КАРТЫ КОНЧАЕТСЯ ВЫПЕЧКОЙ ПОЛЯ БИОМА (ЗАКОН ПОЛЯ): живой мир
        // читает поле, а не каскад, поэтому карта без выпечки — карта
        // НЕДОРОЖДЁННАЯ, и её биом честно отвечает водой.
        sm::bake_biomes(td);
        return td;
    }

} // namespace

int main()
{
    CHECK(nearly(sm::biome_sp_weight(sm::Water), 10.0f),
                 "water biome weight must be 10");
    CHECK(nearly(sm::cell_sp_weight(sm::Water, sm::FT_Road), 1.0f),
                 "road feature must override water biome cost");
    CHECK(nearly(sm::travel_stamina_cost(
                            sm::cell_sp_weight(sm::Meadow, sm::FT_DirtRoad), 1.0f),
                        1.5f * sm::kStaminaPerCell),
                 "one dirt-road cell costs its weight x kStaminaPerCell");
    // The canopy is a CONTINUOUS contribution now (the sum law, 2026-08-24):
    // meadow ground 2.0 + kCanopySpWeight × density, thickening smoothly —
    // and an engineered bed is a CUT: the road gates the canopy off.
    CHECK(nearly(sm::cell_sp_weight(sm::Meadow, sm::FT_None, 1.0f),
                        2.0f + sm::kCanopySpWeight),
                 "a full thicket adds the whole canopy weight to its ground");
    CHECK(nearly(sm::cell_sp_weight(sm::Meadow, sm::FT_None, 0.5f),
                        2.0f + 0.5f * sm::kCanopySpWeight),
                 "half the trees add half the canopy — no boolean cliff");
    CHECK(nearly(sm::cell_sp_weight(sm::Meadow, sm::FT_Road, 1.0f), 1.0f),
                 "a road through a forest is a cut: the bed gates the canopy");
    CHECK(nearly(sm::cell_sp_weight(static_cast<sm::Biome>(255), sm::FT_None), 2.0f),
                 "an out-of-table biome FAILS CLOSED to the default weight - never to 0.0, which would be free movement");
    CHECK(nearly(sm::cell_sp_weight(static_cast<sm::Biome>(255),
                                           static_cast<sm::FeatureType>(255)), 2.0f),
                 "garbage in BOTH columns still fails closed to the default weight");
    CHECK(nearly(sm::cell_sp_weight(sm::Meadow,
                                           static_cast<sm::FeatureType>(255)), 2.0f),
                 "unknown feature must fall through to biome movement weight");

    sm::PathCostData grid = flat_grid(5, 5, 1.0f);
    sm::PathResult wrapped = sm::find_path(grid, 0, 0, 4, 0, 8);
    CHECK(wrapped.found, "torus neighbor path should be found");
    CHECK(wrapped.path.size() == 2, "torus neighbor path should be one edge");
    if (wrapped.path.size() == 2)
    {
        CHECK(wrapped.path[0].x == 0 && wrapped.path[0].y == 0,
                     "path should start at wrapped start");
        CHECK(wrapped.path[1].x == 4 && wrapped.path[1].y == 0,
                     "path should end at wrapped goal");
    }

    sm::PathResult capped = sm::find_path(grid, 0, 0, 4, 0, 1);
    CHECK(!capped.found && capped.path.empty(),
                 "maxSteps cap should stop search before second pop");

    sm::PathResult enoughBudget = sm::find_path(grid, 0, 0, 4, 0, 2);
    CHECK(enoughBudget.found,
                 "same path should succeed when cap allows target pop");

    sm::TerrainData td = make_terrain(2, 2);
    CHECK(td.cell_count() == 4u && td.has_rgba_storage(),
                 "terrain storage helpers must accept valid RGBA backing data");
    sm::FeatureLayer fullFeatures;
    fullFeatures.resize(2, 2);
    fullFeatures.set(0, 0, sm::FT_Road);

    // Cell (1,0) is a mountain by ELEVATION (biome), not a feature: raise its
    // height above the mountain level so build_cost_grid classifies it Mountain
    // and pulls the 5.0 weight from the biome table.
    sm::TerrainData mtnTerrain = make_terrain(2, 2);
    // Уровень 0.863 >= kMountainBiomeLevel (0.625 — шапка здесь годами звала
    // его 0.75, и это была ложь свидетеля о пороге, а не о клетке).
    mtnTerrain.rgba[4] = sm::field_word_of(220.0f / 255.0f);
    // ВЫСОТА ИЗМЕНЕНА — ПОЛЕ БИОМА ПЕРЕПЕКАЕТСЯ (ЗАКОН ПОЛЯ): мастер и поле
    // не имеют права разойтись, иначе у карты снова два ответа.
    sm::bake_biomes(mtnTerrain);

    const sm::PathCostData withFeatures = sm::build_cost_grid(mtnTerrain, &fullFeatures);
    CHECK(withFeatures.width == 2 && withFeatures.height == 2,
                 "valid cost grid must preserve terrain dimensions");
    CHECK(withFeatures.costGrid.size() == 4u,
                 "valid cost grid must have one cost per cell");
    if (withFeatures.costGrid.size() == 4u)
    {
        CHECK(nearly(withFeatures.costGrid[0], 1.0f),
                     "road feature must apply road movement cost");
        CHECK(nearly(withFeatures.costGrid[1], 5.0f),
                     "mountain biome (by height) must apply mountain movement cost");
    }

    // ВОДА — ПЛОСКОСТЬ МОРЯ КАРТЫ, А НЕ БАЙТ МАСКИ (вердикт владельца
    // 2026-09-25 «НИКАКИХ МАСОК», M-109). Этот блок звался mask-* и
    // утверждал обратное: он делал воду, ОПУСКАЯ ТОЛЬКО альфу и не трогая
    // высоту, а комментарий прямо говорил «маска — авторитет». Это и был
    // коастальный двойной ответ, а не защита от него.
    // ЗАКОН АДРЕСА: мир КВАДРАТЕН и сторона — степень двойки. Здесь стояло
    // 2×1 — форма, которой мир не бывает; дверь биома отвечала на ней лишь
    // потому, что не спрашивала о форме, а `is_water` рядом спрашивала. Носитель
    // свидетеля — «плоскость решает, а маска не авторитет» — сохранён целиком:
    // под вопросом те же две клетки 0 и 1, остальные две просто суша.
    sm::TerrainData maskTerrain = make_terrain(2, 2);
    // Климат берётся ИЗ КАРТЫ её же дверью чтения (`field01_of`), а не вторым
    // правописанием словаря: иначе ожидание и мир делили бы литерал `/255`.
    const float defaultLandWeight = sm::cell_sp_weight(
        sm::biome_from_climate(sm::field01_of(maskTerrain.rgba[2]),
                               sm::field01_of(maskTerrain.rgba[1])),
        sm::FT_None);
    // cell 0: высота НИЖЕ плоскости — вот и вода
    maskTerrain.rgba[0] = sm::field_word_of(40.0f / 255.0f);
    // ВЫСОТА ИЗМЕНЕНА — ПОЛЕ БИОМА ПЕРЕПЕКАЕТСЯ (ЗАКОН ПОЛЯ): мастер и поле
    // не имеют права разойтись, иначе у карты снова два ответа.
    sm::bake_biomes(maskTerrain);
    const sm::PathCostData maskCosts =
        sm::build_cost_grid(maskTerrain, nullptr);
    CHECK(maskCosts.costGrid.size() == 4u,
                 "mask grid must be complete");
    if (maskCosts.costGrid.size() == 4u)
    {
        CHECK(nearly(maskCosts.costGrid[0], 10.0f)
                         && maskCosts.water[0] == 1u,
                     "клетка ниже плоскости моря стоит и флажится как вода");
        CHECK(nearly(maskCosts.costGrid[1], defaultLandWeight)
                         && maskCosts.water[1] == 0u,
                     "клетка выше плоскости держит цену своего биома");
    }
    // НЕГАТИВНЫЙ КОНТРОЛЬ ЗАКОНА: поднять высоту выше плоскости и СОЛГАТЬ
    // маской (A = 0, «вода») — цена воды обязана исчезнуть вместе с высотой.
    // Пока маска была авторитетом, этот же случай красил бы наоборот.
    maskTerrain.rgba[0] = sm::field_word_of(128.0f / 255.0f);
    maskTerrain.rgba[3] = 0u;   // маска «вода» — сентинель канала, не уровень
    // ВЫСОТА ИЗМЕНЕНА — ПОЛЕ БИОМА ПЕРЕПЕКАЕТСЯ (ЗАКОН ПОЛЯ): мастер и поле
    // не имеют права разойтись, иначе у карты снова два ответа.
    sm::bake_biomes(maskTerrain);
    const sm::PathCostData maskFlipped =
        sm::build_cost_grid(maskTerrain, nullptr);
    CHECK(maskFlipped.costGrid.size() == 4u
                     && nearly(maskFlipped.costGrid[0], defaultLandWeight)
                     && maskFlipped.water[0] == 0u,
                 "маска солгала про воду — мир ответил по плоскости, землёй");

    sm::FeatureLayer shortFeatures;
    shortFeatures.width = 2;
    shortFeatures.height = 2;
    shortFeatures.data.assign(1u, std::uint8_t(sm::FT_Road));
    const sm::PathCostData noFeatures = sm::build_cost_grid(td, nullptr);
    const sm::PathCostData shortFeatureCosts = sm::build_cost_grid(td, &shortFeatures);
    CHECK(shortFeatureCosts.costGrid == noFeatures.costGrid,
                 "short feature storage must be ignored by cost-grid builder");

    sm::FeatureLayer mismatchedFeatures;
    mismatchedFeatures.resize(1, 2);
    mismatchedFeatures.set(0, 0, sm::FT_Road);
    const sm::PathCostData mismatchCosts = sm::build_cost_grid(td, &mismatchedFeatures);
    CHECK(mismatchCosts.costGrid == noFeatures.costGrid,
                 "dimension-mismatched feature storage must be ignored");

    sm::FeatureLayer invalidFeatures;
    invalidFeatures.resize(2, 2);
    invalidFeatures.data[0] = 255u;
    invalidFeatures.set(1, 0, sm::FT_Road);
    const sm::PathCostData invalidFeatureCosts = sm::build_cost_grid(td, &invalidFeatures);
    CHECK(invalidFeatureCosts.costGrid.size() == 4u,
                 "invalid-byte feature grid must still build a complete cost grid");
    if (invalidFeatureCosts.costGrid.size() == 4u && noFeatures.costGrid.size() == 4u)
    {
        CHECK(nearly(invalidFeatureCosts.costGrid[0], noFeatures.costGrid[0]),
                     "invalid feature byte must fail closed to biome movement cost");
        CHECK(nearly(invalidFeatureCosts.costGrid[1], 1.0f),
                     "valid feature byte after invalid byte must still apply");
    }

    sm::TerrainData malformedTerrain = td;
    malformedTerrain.rgba.resize(1u);
    CHECK(!malformedTerrain.has_rgba_storage(),
                 "terrain storage helper must reject short RGBA backing data");
    const sm::PathCostData malformedCosts =
        sm::build_cost_grid(malformedTerrain, &fullFeatures);
    CHECK(malformedCosts.width == 0 && malformedCosts.height == 0
                     && malformedCosts.costGrid.empty(),
                 "malformed terrain storage must fail closed");

    sm::FeatureLayer zoneFeatures;
    zoneFeatures.resize(4, 4);
    sm::FeatureLayer shortZoneFeatures;
    shortZoneFeatures.width = 4;
    shortZoneFeatures.height = 4;
    shortZoneFeatures.data.assign(1u, std::uint8_t(sm::FT_DirtRoad));
    const std::vector<sm::ZoneSeed> noSeeds;
    // The continuous zone value exists only DURING the bake (the stored float
    // twin was 4 MiB with no game reader — cut 2026-08-24); the test asks for
    // the optional capture, because the +0.05 water boost is finer than the
    // 0..9 quantisation.
    std::vector<float> baselineCont;
    const sm::ZoneLayer baselineZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures, nullptr,
                           nullptr, &baselineCont);
    CHECK(baselineZones.has_complete_storage(),
                 "generated zone layer must expose complete storage");
    std::vector<float> shortFeatCont;
    const sm::ZoneLayer shortFeatureZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, shortZoneFeatures,
                           nullptr, nullptr, &shortFeatCont);
    CHECK(shortFeatureZones.data == baselineZones.data
                     && shortFeatCont == baselineCont,
                 "short feature storage must be ignored by zone generator");
    sm::FeatureLayer invalidZoneFeatures;
    invalidZoneFeatures.resize(4, 4);
    invalidZoneFeatures.data[0] = 255u;
    std::vector<float> invalidFeatCont;
    const sm::ZoneLayer invalidFeatureZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, invalidZoneFeatures,
                           nullptr, nullptr, &invalidFeatCont);
    CHECK(invalidFeatureZones.data == baselineZones.data
                     && invalidFeatCont == baselineCont,
                 "invalid feature bytes must be ignored by zone generator");
    // ЗОНЫ СПРАШИВАЮТ КАРТУ, А НЕ ЕЁ БАЙТЫ (M-109): здесь стоял сырой
    // буфер RGBA, и «вода» задавалась байтом маски вручную. Теперь вода — это
    // высота ниже плоскости моря самой карты.
    // Land cells must be mid-elevation, not peaks: all-255 height would make
    // every land cell the Mountain biome (height >= kMountainBiomeLevel) and pull
    // in the mountain danger boost. This block exercises the WATER boost, so the
    // height (red) channel stays ordinary ground.
    sm::TerrainData waterTd;
    waterTd.width = 4;
    waterTd.height = 4;
    // Заливка ПОЛНЫМ уровнем (байт 255 = 1.0): климат и маска на максимуме,
    // высота ниже переписывается серединой — ровно как было.
    waterTd.rgba.assign(std::size_t(4 * 4 * 4),
                        std::uint16_t(sm::kFieldWordMax));
    waterTd.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    for (int i = 0; i < 4 * 4; ++i)
        waterTd.rgba[std::size_t(i) * 4u + 0u] =
            sm::field_word_of(128.0f / 255.0f);
    // cell 0 below the plane -> water
    waterTd.rgba[0] = sm::field_word_of(40.0f / 255.0f);
    std::vector<float> waterCont;
    const sm::ZoneLayer waterZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures,
                           &waterTd, nullptr, &waterCont);
    CHECK(waterZones.has_complete_storage(),
                 "valid water-mask zone generation must expose complete storage");
    CHECK(nearly(waterCont[0],
                        std::min(1.0f, baselineCont[0] + 0.05f)),
                 "a water cell lifts the zone value of ITS OWN cell and leaves the neighbouring land alone");
    CHECK(nearly(waterCont[1], baselineCont[1]),
                 "water must not alter land cells");
    std::vector<float> shortWaterCont;
    sm::TerrainData shortTd = waterTd;
    shortTd.rgba.resize(3u);   // storage that does not cover the map
    const sm::ZoneLayer shortWaterZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures,
                           &shortTd, nullptr, &shortWaterCont);
    CHECK(shortWaterZones.data == baselineZones.data
                     && shortWaterCont == baselineCont,
                 "terrain whose storage does not cover the map must be ignored");
    CHECK(sm::generate_zones(0, 4, 123u, noSeeds, noSeeds,
                                    zoneFeatures, nullptr).data.empty(),
                 "invalid zone dimensions must return an empty layer");
    sm::ZoneLayer malformedZones;
    malformedZones.width = 2;
    malformedZones.height = 2;
    malformedZones.data.assign(1u, 255u);
    CHECK(!malformedZones.has_complete_storage(),
                 "short zone storage must not report complete storage");
    // The danger is a 0..255 CONTINUUM (owner 2026-08-24): every byte is a
    // legal value — the door returns it raw — and only a cell the short
    // storage cannot back fails closed to zero.
    CHECK(malformedZones.at(0, 0) == 255u,
                 "a stored danger byte must come back untouched");
    CHECK(malformedZones.at(1, 1) == 0u,
                 "a cell past the short storage must fail closed to zero");
    CHECK(malformedZones.at(1, 1) == 0u,
                 "out-of-backing zone lookup must fail closed");

    return sm::test::report("pathfinding_parity_test");
}
