#include "macro/spawners.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{

bool expect(bool ok, const char *msg)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL feature_layer_parity_test: %s\n", msg);
        return false;
    }
    return true;
}

sm::TerrainData make_terrain(int w, int h, std::uint8_t height)
{
    sm::TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, 0);
    td.riverData.assign(std::size_t(w) * std::size_t(h), 0);
    for (int i = 0; i < w * h; ++i)
    {
        const std::size_t s = std::size_t(i) * 4u;
        td.rgba[s + 0] = height;
        td.rgba[s + 1] = 128;
        td.rgba[s + 2] = 128;
        td.rgba[s + 3] = height < 102 ? 0 : 255;
    }
    return td;
}

void set_height(sm::TerrainData &td, int x, int y, std::uint8_t height)
{
    const std::size_t s = (std::size_t(y) * std::size_t(td.width)
                           + std::size_t(x)) * 4u;
    td.rgba[s + 0] = height;
    td.rgba[s + 3] = height < 102 ? 0 : 255;
}

void set_alpha(sm::TerrainData &td, int x, int y, std::uint8_t alpha)
{
    const std::size_t s = (std::size_t(y) * std::size_t(td.width)
                           + std::size_t(x)) * 4u;
    td.rgba[s + 3] = alpha;
}

std::size_t idx(const sm::TerrainData &td, int x, int y)
{
    return std::size_t(y) * std::size_t(td.width) + std::size_t(x);
}

// Reference reimplementation of build_feature_layer's composing passes.
// Features are MAN-MADE only: mountains are the elevation-classified Mountain
// biome and forests are the tree-count field (macro/tree_layer.h) — neither
// appears here. Passes apply last-writer-wins: dirt roads, then roads.
sm::FeatureLayer build_reference_feature_layer(
    const sm::TerrainData &td,
    const std::vector<std::uint8_t> &roadMask,
    const std::vector<std::uint8_t> *dirtMask)
{
    sm::FeatureLayer fl;
    fl.resize(td.width, td.height);
    if (td.width <= 0 || td.height <= 0 || fl.data.empty())
        return fl;

    const std::size_t total = std::size_t(td.width) * std::size_t(td.height);
    if (td.rgba.size() < total * 4u)
        return fl;

    constexpr std::uint8_t kSeaLvl8 = std::uint8_t(0.40f * 255.0f);
    auto is_water = [&](std::size_t i)
    {
        return td.rgba[i * 4u + 3] == 0 || td.rgba[i * 4u + 0] < kSeaLvl8;
    };

    if (dirtMask)
    {
        const std::size_t limit = dirtMask->size() < total ? dirtMask->size() : total;
        for (std::size_t i = 0; i < limit; ++i)
            if ((*dirtMask)[i] > 0 && !is_water(i))
                fl.data[i] = sm::FT_DirtRoad;
    }
    const std::size_t roadLimit = roadMask.size() < total ? roadMask.size() : total;
    for (std::size_t i = 0; i < roadLimit; ++i)
        if (roadMask[i] > 0 && !is_water(i))
            fl.data[i] = sm::FT_Road;
    return fl;
}

bool test_feature_priority_and_water_filter()
{
    sm::TerrainData td = make_terrain(4, 4, 140);
    set_height(td, 2, 2, 0);   // below sea level -> water-filter divergence

    // (0,1): dirt only. (1,0): road only. (1,1): dirt -> road (road wins).
    // (2,2): dirt+road but water -> must stay empty. (3,0): dirt (wrap test).
    std::vector<std::uint8_t> road(std::size_t(td.width) * td.height, 0);
    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    dirt[idx(td, 0, 1)] = 255;
    dirt[idx(td, 1, 1)] = 255;
    dirt[idx(td, 2, 2)] = 255;
    dirt[idx(td, 3, 0)] = 255;
    road[idx(td, 1, 0)] = 255;
    road[idx(td, 1, 1)] = 255;
    road[idx(td, 2, 2)] = 255;

    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, road, &dirt);

    bool ok = true;
    ok &= expect(fl.width == 4 && fl.height == 4,
                 "feature layer dimensions must match terrain");
    ok &= expect(fl.at(0, 1) == sm::FT_DirtRoad,
                 "dirt-road pass must stamp connector cells");
    ok &= expect(fl.at(1, 0) == sm::FT_Road,
                 "road pass must stamp main road cells");
    ok &= expect(fl.at(1, 1) == sm::FT_Road,
                 "road pass must have highest feature priority");
    ok &= expect(fl.at(2, 2) == sm::FT_None,
                 "native feature layer must keep water cells empty");
    ok &= expect(fl.at(-1, 0) == sm::FT_DirtRoad,
                 "feature lookup must wrap negative x toroidally");
    ok &= expect(fl.at(5, 1) == sm::FT_Road,
                 "feature lookup must wrap positive x toroidally");
    return ok;
}

bool test_empty_and_malformed_inputs_are_safe()
{
    sm::FeatureLayer empty;
    empty.resize(0, 4);
    empty.set(0, 0, sm::FT_Road);

    sm::FeatureLayer shortStorage;
    shortStorage.width = 4;
    shortStorage.height = 4;
    shortStorage.data.assign(1, std::uint8_t(sm::FT_DirtRoad));
    shortStorage.set(3, 3, sm::FT_Road);

    sm::FeatureLayer invalidStorage;
    invalidStorage.resize(2, 1);
    invalidStorage.data[0] = 255u;
    invalidStorage.data[1] = std::uint8_t(sm::FT_DirtRoad);

    sm::FeatureLayer invalidSet;
    invalidSet.resize(1, 1);
    invalidSet.set(0, 0, static_cast<sm::FeatureType>(255u));

    sm::FeatureLayer validStorage;
    validStorage.resize(2, 1);
    validStorage.data[0] = std::uint8_t(sm::FT_Road);
    validStorage.data[1] = std::uint8_t(sm::FT_DirtRoad);

    std::vector<std::uint8_t> sanitized;

    sm::TerrainData td = make_terrain(2, 2, 200);
    std::vector<std::uint8_t> shortRoad{255};
    std::vector<std::uint8_t> shortDirt{0, 255};
    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, shortRoad, &shortDirt);

    bool ok = true;
    ok &= expect(empty.width == 0 && empty.height == 0 && empty.data.empty(),
                 "empty feature resize must clear storage");
    ok &= expect(empty.at(0, 0) == sm::FT_None,
                 "empty feature lookup must be safe");
    ok &= expect(sm::FeatureLayer::wrap_coord(std::numeric_limits<int>::min(), 3) == 1,
                 "feature coordinate wrap must handle INT_MIN without overflow");
    ok &= expect(shortStorage.at(0, 0) == sm::FT_DirtRoad,
                 "short feature storage must allow valid prefix lookup");
    ok &= expect(shortStorage.at(3, 3) == sm::FT_None,
                 "short feature storage must fail closed outside backing data");
    ok &= expect(invalidStorage.at(0, 0) == sm::FT_None,
                 "invalid feature bytes must decode to None");
    ok &= expect(invalidStorage.at(1, 0) == sm::FT_DirtRoad,
                 "valid feature bytes must decode unchanged");
    ok &= expect(invalidStorage.has_invalid_cell_bytes(),
                 "complete feature storage must report invalid cell bytes");
    ok &= expect(invalidStorage.copy_sanitized_cells(sanitized)
                     && sanitized.size() == 2u
                     && sanitized[0] == std::uint8_t(sm::FT_None)
                     && sanitized[1] == std::uint8_t(sm::FT_DirtRoad),
                 "feature sanitized copy must normalize invalid bytes only");
    const std::uint8_t *invalidUpload =
        invalidStorage.complete_cells_or_sanitized(sanitized);
    ok &= expect(invalidUpload == sanitized.data()
                     && sanitized[0] == std::uint8_t(sm::FT_None)
                     && sanitized[1] == std::uint8_t(sm::FT_DirtRoad),
                 "feature upload view must use sanitized scratch for invalid cells");
    const std::uint8_t *validUpload =
        validStorage.complete_cells_or_sanitized(sanitized);
    ok &= expect(validUpload == validStorage.data.data() && sanitized.empty(),
                 "feature upload view must keep direct storage for valid cells");
    ok &= expect(!shortStorage.has_invalid_cell_bytes(),
                 "short feature storage must not scan outside valid cell backing");
    ok &= expect(!shortStorage.copy_sanitized_cells(sanitized) && sanitized.empty(),
                 "short feature storage must not produce a complete sanitized copy");
    ok &= expect(shortStorage.complete_cells_or_sanitized(sanitized) == nullptr
                     && sanitized.empty(),
                 "short feature storage must not expose an upload view");
    ok &= expect(invalidSet.data[0] == std::uint8_t(sm::FT_None)
                     && invalidSet.at(0, 0) == sm::FT_None,
                 "feature setter must sanitize invalid enum casts");

    sm::FeatureLayer hugeExtent;
    hugeExtent.width = std::numeric_limits<int>::max();
    hugeExtent.height = 1;
    hugeExtent.data.assign(1u, std::uint8_t(sm::FT_Road));
    hugeExtent.set(std::numeric_limits<int>::max() - 1, 0, sm::FT_DirtRoad);
    ok &= expect(hugeExtent.at(0, 0) == sm::FT_Road
                     && hugeExtent.at(std::numeric_limits<int>::max() - 1, 0) == sm::FT_None,
                 "malformed huge feature extents must wrap safely and fail closed");
    ok &= expect(!sm::FeatureLayer::is_valid_byte(255u)
                     && sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_DirtRoad)),
                 "feature byte validation must reject unknown values only");
    // The renumber guard: byte 3 was FT_DirtRoad before forests left the
    // feature grid (FT_Tree=2 removed, FT_DirtRoad 3 → 2). A stale 3 must
    // now be an INVALID byte that decodes to None, never a road.
    ok &= expect(!sm::FeatureLayer::is_valid_byte(3u)
                     && sm::FeatureLayer::decode(3u) == sm::FT_None,
                 "stale pre-renumber byte 3 must fail closed to None");
    ok &= expect(fl.at(0, 0) == sm::FT_Road,
                 "short road masks must apply prefix bytes");
    ok &= expect(fl.at(1, 0) == sm::FT_DirtRoad,
                 "short dirt masks must apply prefix bytes");
    return ok;
}

bool test_feature_layer_reference_matrix()
{
    bool ok = true;
    std::uint32_t seed = 0x5eed1234u;
    auto next_u8 = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return std::uint8_t(seed >> 24);
    };

    for (int w = 1; w <= 5; ++w)
    {
        for (int h = 1; h <= 4; ++h)
        {
            sm::TerrainData td = make_terrain(w, h, 140);
            const std::size_t total = std::size_t(w) * std::size_t(h);
            for (std::size_t i = 0; i < total; ++i)
            {
                const std::uint8_t height = next_u8();
                td.rgba[i * 4u + 0] = height;
                td.rgba[i * 4u + 3] = height < 42 || (next_u8() & 7u) == 0u ? 0 : 255;
            }

            const std::size_t roadSize = total > 1 ? total - 1 : 0;
            const std::size_t dirtSize = total + 2u;
            std::vector<std::uint8_t> road(roadSize, 0);
            std::vector<std::uint8_t> dirt(dirtSize, 0);
            for (std::size_t i = 0; i < road.size(); ++i)
                road[i] = (next_u8() & 3u) == 0u ? 255 : 0;
            for (std::size_t i = 0; i < dirt.size(); ++i)
                dirt[i] = (next_u8() & 5u) == 0u ? 255 : 0;

            const sm::FeatureLayer actual =
                sm::build_feature_layer(td, road, &dirt);
            const sm::FeatureLayer expected =
                build_reference_feature_layer(td, road, &dirt);

            ok &= expect(actual.width == expected.width && actual.height == expected.height,
                         "reference matrix dimensions must match");
            ok &= expect(actual.data == expected.data,
                         "reference matrix must match feature pass contract");
        }
    }
    return ok;
}

bool test_feature_land_mask_trusts_alpha()
{
    // The feature layer's only height-adjacent responsibility is honouring
    // the land/water mask: roads never stamp a cell whose alpha marks it as
    // water, regardless of its stored height.
    sm::TerrainData td = make_terrain(3, 1, 240);
    set_alpha(td, 2, 0, 0);   // force cell (2,0) to water via the land mask

    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    dirt[idx(td, 0, 0)] = 255;
    dirt[idx(td, 2, 0)] = 255;
    const std::vector<std::uint8_t> empty(std::size_t(td.width) * td.height, 0);
    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, empty, &dirt);

    bool ok = true;
    ok &= expect(fl.at(0, 0) == sm::FT_DirtRoad,
                 "dirt pass must stamp features on land cells");
    ok &= expect(fl.at(2, 0) == sm::FT_None,
                 "native feature layer must trust alpha-zero land mask");
    return ok;
}

bool test_feature_water_filter_uses_map_sea_level()
{
    sm::TerrainData td = make_terrain(3, 1, 120);
    set_height(td, 0, 0, 80);
    set_height(td, 1, 0, 100);
    set_height(td, 2, 0, 120);
    set_alpha(td, 0, 0, 255);
    set_alpha(td, 1, 0, 255);
    set_alpha(td, 2, 0, 255);

    std::vector<std::uint8_t> road(std::size_t(td.width) * td.height, 0);
    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    road[idx(td, 0, 0)] = 255;
    dirt[idx(td, 1, 0)] = 255;

    const sm::FeatureLayer lowSea =
        sm::build_feature_layer(td, road, &dirt, 0.30f);
    const sm::FeatureLayer defaultSea =
        sm::build_feature_layer(td, road, &dirt, 0.40f);

    bool ok = true;
    ok &= expect(lowSea.at(0, 0) == sm::FT_Road,
                 "feature water filter must use active low sea level for roads");
    ok &= expect(lowSea.at(1, 0) == sm::FT_DirtRoad,
                 "feature water filter must use active low sea level for dirt roads");
    ok &= expect(defaultSea.at(0, 0) == sm::FT_None,
                 "feature water filter must reject cells below active default sea level");
    ok &= expect(defaultSea.at(1, 0) == sm::FT_None,
                 "feature water filter must reject dirt cells below active default sea level");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= test_feature_priority_and_water_filter();
    ok &= test_empty_and_malformed_inputs_are_safe();
    ok &= test_feature_layer_reference_matrix();
    ok &= test_feature_land_mask_trusts_alpha();
    ok &= test_feature_water_filter_uses_map_sea_level();

    if (!ok)
        return 1;
    std::printf("feature_layer_parity_test: ok\n");
    return 0;
}
