#include "macro/biomes.h"
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

    bool expect(bool ok, const char *msg)
    {
        if (!ok)
        {
            std::fprintf(stderr, "FAIL: %s\n", msg);
            return false;
        }
        return true;
    }

    sm::PathCostData flat_grid(int w, int h, float cost)
    {
        sm::PathCostData data;
        data.width = w;
        data.height = h;
        data.costGrid.assign(std::size_t(w) * std::size_t(h), cost);
        return data;
    }

    sm::TerrainData make_terrain(int w, int h)
    {
        sm::TerrainData td;
        td.width = w;
        td.height = h;
        td.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, 0u);
        for (int i = 0; i < w * h; ++i)
        {
            const std::size_t s = std::size_t(i) * 4u;
            td.rgba[s + 0] = 140u;
            td.rgba[s + 1] = 128u;
            td.rgba[s + 2] = 128u;
            td.rgba[s + 3] = 255u;
        }
        return td;
    }

} // namespace

int main()
{
    bool ok = true;

    ok &= expect(nearly(sm::biome_sp_weight(sm::Water), 10.0f),
                 "water biome weight must be 10");
    ok &= expect(nearly(sm::cell_sp_weight(sm::Water, sm::FT_Road), 1.0f),
                 "road feature must override water biome cost");
    ok &= expect(nearly(sm::travel_stamina_cost(
                            sm::cell_sp_weight(sm::Meadow, sm::FT_DirtRoad), 1.0f),
                        1.5f * sm::kStaminaPerCell),
                 "one dirt-road cell costs its weight x kStaminaPerCell");
    ok &= expect(nearly(sm::cell_sp_weight(sm::Meadow, sm::FT_None, true), 3.0f),
                 "forest-class cell must carry the old FT_Tree drag");
    ok &= expect(nearly(sm::cell_sp_weight(sm::Meadow, sm::FT_Road, true), 1.0f),
                 "a road through a forest must keep its road weight");
    ok &= expect(nearly(sm::cell_sp_weight(static_cast<sm::Biome>(255), sm::FT_None), 2.0f),
                 "unknown biome must match TS default movement weight");
    ok &= expect(nearly(sm::cell_sp_weight(static_cast<sm::Biome>(255),
                                           static_cast<sm::FeatureType>(255)), 2.0f),
                 "unknown feature and biome must fail closed to TS default movement weight");
    ok &= expect(nearly(sm::cell_sp_weight(sm::Meadow,
                                           static_cast<sm::FeatureType>(255)), 2.0f),
                 "unknown feature must fall through to biome movement weight");

    sm::PathCostData grid = flat_grid(5, 5, 1.0f);
    sm::PathResult wrapped = sm::find_path(grid, 0, 0, 4, 0, 8);
    ok &= expect(wrapped.found, "torus neighbor path should be found");
    ok &= expect(wrapped.path.size() == 2, "torus neighbor path should be one edge");
    if (wrapped.path.size() == 2)
    {
        ok &= expect(wrapped.path[0].x == 0 && wrapped.path[0].y == 0,
                     "path should start at wrapped start");
        ok &= expect(wrapped.path[1].x == 4 && wrapped.path[1].y == 0,
                     "path should end at wrapped goal");
    }

    sm::PathResult capped = sm::find_path(grid, 0, 0, 4, 0, 1);
    ok &= expect(!capped.found && capped.path.empty(),
                 "maxSteps cap should stop search before second pop");

    sm::PathResult enoughBudget = sm::find_path(grid, 0, 0, 4, 0, 2);
    ok &= expect(enoughBudget.found,
                 "same path should succeed when cap allows target pop");

    sm::TerrainData td = make_terrain(2, 1);
    ok &= expect(td.cell_count() == 2u && td.has_rgba_storage(),
                 "terrain storage helpers must accept valid RGBA backing data");
    ok &= expect(!td.has_river_storage(),
                 "terrain river helper must reject missing river backing data");
    sm::FeatureLayer fullFeatures;
    fullFeatures.resize(2, 1);
    fullFeatures.set(0, 0, sm::FT_Road);

    // Cell (1,0) is a mountain by ELEVATION (biome), not a feature: raise its
    // height above the mountain level so build_cost_grid classifies it Mountain
    // and pulls the 5.0 weight from the biome table.
    sm::TerrainData mtnTerrain = make_terrain(2, 1);
    mtnTerrain.rgba[4] = 220u; // height 0.863 >= kMountainBiomeLevel (0.75)

    const sm::PathCostData withFeatures = sm::build_cost_grid(mtnTerrain, &fullFeatures);
    ok &= expect(withFeatures.width == 2 && withFeatures.height == 1,
                 "valid cost grid must preserve terrain dimensions");
    ok &= expect(withFeatures.costGrid.size() == 2u,
                 "valid cost grid must have one cost per cell");
    if (withFeatures.costGrid.size() == 2u)
    {
        ok &= expect(nearly(withFeatures.costGrid[0], 1.0f),
                     "road feature must apply road movement cost");
        ok &= expect(nearly(withFeatures.costGrid[1], 5.0f),
                     "mountain biome (by height) must apply mountain movement cost");
    }

    sm::TerrainData seaLevelTerrain = make_terrain(2, 1);
    seaLevelTerrain.rgba[0] = 102u; // cell 0 height 0.40 — the float sea-level probe
    // Cell 1 stays ordinary mid-elevation land. Its height must remain BELOW the
    // mountain level (kMountainBiomeLevel) or biome_at reclassifies it as the
    // Mountain biome and it would no longer carry the default climate biome cost.
    seaLevelTerrain.rgba[4] = 140u;
    const float defaultLandWeight = sm::cell_sp_weight(
        sm::biome_from_climate(128.0f / 255.0f, 128.0f / 255.0f),
        sm::FT_None);
    const sm::PathCostData nonByteSeaLevel =
        sm::build_cost_grid(seaLevelTerrain, nullptr, 0.401f);
    ok &= expect(nonByteSeaLevel.costGrid.size() == 2u,
                 "non-byte-aligned sea level grid must be complete");
    if (nonByteSeaLevel.costGrid.size() == 2u)
    {
        ok &= expect(nearly(nonByteSeaLevel.costGrid[0], 10.0f),
                     "cost-grid sea-level comparison must match TS float threshold");
        ok &= expect(nearly(nonByteSeaLevel.costGrid[1], defaultLandWeight),
                     "land above non-byte sea level must keep biome cost");
    }
    const sm::PathCostData negativeSeaLevel =
        sm::build_cost_grid(seaLevelTerrain, nullptr, -0.1f);
    ok &= expect(negativeSeaLevel.costGrid.size() == 2u
                     && nearly(negativeSeaLevel.costGrid[0], defaultLandWeight),
                 "negative sea level must match TS and produce no water cells");
    const sm::PathCostData highSeaLevel =
        sm::build_cost_grid(seaLevelTerrain, nullptr, 1.1f);
    ok &= expect(highSeaLevel.costGrid.size() == 2u
                     && nearly(highSeaLevel.costGrid[0], 10.0f)
                     && nearly(highSeaLevel.costGrid[1], 10.0f),
                 "sea level above one must match TS and mark all cells water");

    sm::FeatureLayer shortFeatures;
    shortFeatures.width = 2;
    shortFeatures.height = 1;
    shortFeatures.data.assign(1u, std::uint8_t(sm::FT_Road));
    const sm::PathCostData noFeatures = sm::build_cost_grid(td, nullptr);
    const sm::PathCostData shortFeatureCosts = sm::build_cost_grid(td, &shortFeatures);
    ok &= expect(shortFeatureCosts.costGrid == noFeatures.costGrid,
                 "short feature storage must be ignored by cost-grid builder");

    sm::FeatureLayer mismatchedFeatures;
    mismatchedFeatures.resize(1, 2);
    mismatchedFeatures.set(0, 0, sm::FT_Road);
    const sm::PathCostData mismatchCosts = sm::build_cost_grid(td, &mismatchedFeatures);
    ok &= expect(mismatchCosts.costGrid == noFeatures.costGrid,
                 "dimension-mismatched feature storage must be ignored");

    sm::FeatureLayer invalidFeatures;
    invalidFeatures.resize(2, 1);
    invalidFeatures.data[0] = 255u;
    invalidFeatures.set(1, 0, sm::FT_Road);
    const sm::PathCostData invalidFeatureCosts = sm::build_cost_grid(td, &invalidFeatures);
    ok &= expect(invalidFeatureCosts.costGrid.size() == 2u,
                 "invalid-byte feature grid must still build a complete cost grid");
    if (invalidFeatureCosts.costGrid.size() == 2u && noFeatures.costGrid.size() == 2u)
    {
        ok &= expect(nearly(invalidFeatureCosts.costGrid[0], noFeatures.costGrid[0]),
                     "invalid feature byte must fail closed to biome movement cost");
        ok &= expect(nearly(invalidFeatureCosts.costGrid[1], 1.0f),
                     "valid feature byte after invalid byte must still apply");
    }

    sm::TerrainData malformedTerrain = td;
    malformedTerrain.rgba.resize(1u);
    ok &= expect(!malformedTerrain.has_rgba_storage(),
                 "terrain storage helper must reject short RGBA backing data");
    const sm::PathCostData malformedCosts =
        sm::build_cost_grid(malformedTerrain, &fullFeatures);
    ok &= expect(malformedCosts.width == 0 && malformedCosts.height == 0
                     && malformedCosts.costGrid.empty(),
                 "malformed terrain storage must fail closed");

    sm::FeatureLayer zoneFeatures;
    zoneFeatures.resize(4, 4);
    sm::FeatureLayer shortZoneFeatures;
    shortZoneFeatures.width = 4;
    shortZoneFeatures.height = 4;
    shortZoneFeatures.data.assign(1u, std::uint8_t(sm::FT_DirtRoad));
    const std::vector<sm::ZoneSeed> noSeeds;
    const sm::ZoneLayer baselineZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures, nullptr);
    ok &= expect(baselineZones.has_complete_storage(),
                 "generated zone layer must expose complete storage");
    const sm::ZoneLayer shortFeatureZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, shortZoneFeatures, nullptr);
    ok &= expect(shortFeatureZones.data == baselineZones.data
                     && shortFeatureZones.field == baselineZones.field,
                 "short feature storage must be ignored by zone generator");
    sm::FeatureLayer invalidZoneFeatures;
    invalidZoneFeatures.resize(4, 4);
    invalidZoneFeatures.data[0] = 255u;
    const sm::ZoneLayer invalidFeatureZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, invalidZoneFeatures, nullptr);
    ok &= expect(invalidFeatureZones.data == baselineZones.data
                     && invalidFeatureZones.field == baselineZones.field,
                 "invalid feature bytes must be ignored by zone generator");
    std::vector<std::uint8_t> waterMask(std::size_t(4 * 4 * 4), 255u);
    // Land cells must be mid-elevation, not peaks: an all-255 mask would make
    // every land cell the Mountain biome (height >= kMountainBiomeLevel) and pull
    // in the mountain danger boost. This block exercises the WATER boost, so knock
    // the height (red) channel down to ordinary ground.
    for (int i = 0; i < 4 * 4; ++i)
        waterMask[std::size_t(i) * 4u + 0u] = 128u;
    waterMask[3] = 0u; // cell 0 alpha -> water
    const sm::ZoneLayer waterZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures,
                           waterMask.data(), waterMask.size());
    ok &= expect(waterZones.has_complete_storage(),
                 "valid water-mask zone generation must expose complete storage");
    ok &= expect(nearly(waterZones.field[0],
                        std::min(1.0f, baselineZones.field[0] + 0.05f)),
                 "valid water mask must apply TS water boost to matching cells");
    ok &= expect(nearly(waterZones.field[1], baselineZones.field[1]),
                 "valid water mask must not alter land cells");
    const sm::ZoneLayer shortWaterZones =
        sm::generate_zones(4, 4, 123u, noSeeds, noSeeds, zoneFeatures,
                           waterMask.data(), 3u);
    ok &= expect(shortWaterZones.data == baselineZones.data
                     && shortWaterZones.field == baselineZones.field,
                 "short supplied water mask must be ignored by zone generator");
    ok &= expect(sm::generate_zones(0, 4, 123u, noSeeds, noSeeds,
                                    zoneFeatures, nullptr).data.empty(),
                 "invalid zone dimensions must return an empty layer");
    sm::ZoneLayer malformedZones;
    malformedZones.width = 2;
    malformedZones.height = 2;
    malformedZones.data.assign(1u, 255u);
    ok &= expect(!malformedZones.has_complete_storage(),
                 "short zone storage must not report complete storage");
    ok &= expect(malformedZones.at(0, 0) == 0u,
                 "invalid zone bytes must decode to safe zone zero");
    ok &= expect(malformedZones.at(1, 1) == 0u,
                 "out-of-backing zone lookup must fail closed");

    if (!ok)
        return 1;
    std::printf("pathfinding_parity_test: ok\n");
    return 0;
}
