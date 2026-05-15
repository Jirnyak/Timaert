#include "macro/spawners.h"

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace
{

bool expect(bool ok, const char* msg)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL road_river_generation_test: %s\n", msg);
        return false;
    }
    return true;
}

sm::TerrainData make_terrain(int w, int h, std::uint8_t height)
{
    sm::TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    td.riverData.assign(std::size_t(w) * std::size_t(h), 0);
    for (int i = 0; i < w * h; ++i)
    {
        const std::size_t s = std::size_t(i) * 4;
        td.rgba[s + 0] = height;
        td.rgba[s + 1] = 128;
        td.rgba[s + 2] = 128;
        td.rgba[s + 3] = height < 102 ? 0 : 255;
    }
    return td;
}

void set_cell(sm::TerrainData& td, int x, int y, std::uint8_t height)
{
    const std::size_t s = (std::size_t(y) * td.width + x) * 4;
    td.rgba[s + 0] = height;
    td.rgba[s + 3] = height < 102 ? 0 : 255;
}

sm::City make_city(int x, int y, int connection)
{
    sm::City c{};
    c.x = x;
    c.y = y;
    c.kingdomIdx = 0;
    c.population = 100;
    for (int& v : c.connections)
    {
        v = -1;
    }
    c.connections[0] = connection;
    return c;
}

bool has_connection(const sm::City& c, int target)
{
    for (int v : c.connections)
    {
        if (v == target)
        {
            return true;
        }
    }
    return false;
}

bool test_road_prunes_water_only_connection()
{
    sm::TerrainData td = make_terrain(5, 5, 0);
    set_cell(td, 1, 1, 160);
    set_cell(td, 3, 3, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    bool ok = true;
    ok &= expect(stats.attemptedEdges == 1, "water-only edge should be attempted once");
    ok &= expect(stats.keptEdges == 0, "water-only edge must not survive");
    ok &= expect(stats.prunedEdges == 1, "water-only edge must be pruned");
    ok &= expect(stats.componentPrunedEdges == 1,
                 "water-only edge should be rejected before expensive A*");
    ok &= expect(!has_connection(p.cities[0], 1) && !has_connection(p.cities[1], 0),
                 "pruned Politik edge must be removed from both cities");
    for (std::size_t i = 0; i < roads.size(); ++i)
    {
        ok &= expect(roads[i] == 0, "pruned road mask must stay empty");
        if (!ok)
        {
            break;
        }
    }
    return ok;
}

bool test_road_survives_land_detour_without_water_cells()
{
    sm::TerrainData td = make_terrain(5, 3, 160);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 2, y, 0);
    }

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(3, 1, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    bool ok = true;
    ok &= expect(stats.attemptedEdges == 1, "detour edge should be attempted once");
    ok &= expect(stats.keptEdges == 1, "land detour edge should survive");
    ok &= expect(stats.prunedEdges == 0, "land detour edge should not be pruned");
    ok &= expect(stats.componentPrunedEdges == 0,
                 "land detour edge must still run through road A*");
    ok &= expect(has_connection(p.cities[0], 1) && has_connection(p.cities[1], 0),
                 "surviving Politik edge must remain connected");
    for (int y = 0; y < td.height; ++y)
    {
        const int idx = y * td.width + 2;
        ok &= expect(roads[std::size_t(idx)] == 0,
                     "surviving road mask must not stamp rejected water cells");
        if (!ok)
        {
            break;
        }
    }
    return ok;
}

bool test_road_uses_a_star_on_open_land_connection()
{
    sm::TerrainData td = make_terrain(8, 8, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(6, 6, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    bool ok = true;
    ok &= expect(stats.attemptedEdges == 1, "open-land edge should be attempted once");
    ok &= expect(stats.keptEdges == 1, "open-land edge should survive");
    ok &= expect(stats.prunedEdges == 0, "open-land edge should not be pruned");
    ok &= expect(stats.expansions > 0,
                 "road tracing must use A* for terrain-cost validation");
    ok &= expect(!roads.empty(), "open-land road mask should be allocated");
    return ok;
}

bool test_road_tracing_uses_map_sea_level()
{
    sm::TerrainData td = make_terrain(5, 1, 90);
    for (std::size_t i = 0; i < td.cell_count(); ++i)
    {
        td.rgba[i * 4u + 3] = 255u;
    }

    sm::Politik lowSeaPolitik;
    lowSeaPolitik.mapW = td.width;
    lowSeaPolitik.mapH = td.height;
    lowSeaPolitik.cities.push_back(make_city(0, 0, 1));
    lowSeaPolitik.cities.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats lowSeaStats;
    const std::vector<std::uint8_t> lowSeaRoads =
        sm::trace_roads(td, lowSeaPolitik, &lowSeaStats, 0.30f);

    sm::Politik defaultSeaPolitik;
    defaultSeaPolitik.mapW = td.width;
    defaultSeaPolitik.mapH = td.height;
    defaultSeaPolitik.cities.push_back(make_city(0, 0, 1));
    defaultSeaPolitik.cities.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats defaultSeaStats;
    const std::vector<std::uint8_t> defaultSeaRoads =
        sm::trace_roads(td, defaultSeaPolitik, &defaultSeaStats, 0.40f);

    bool ok = true;
    ok &= expect(lowSeaStats.keptEdges == 1 && lowSeaStats.prunedEdges == 0,
                 "road tracing must use active low sea level for land connectivity");
    ok &= expect(!lowSeaRoads.empty() && lowSeaRoads[0] == 255u,
                 "active low sea road trace must stamp reachable land");
    ok &= expect(defaultSeaStats.keptEdges == 0 && defaultSeaStats.componentPrunedEdges == 1,
                 "road tracing must reject the same cells below active default sea level");
    ok &= expect(!has_connection(defaultSeaPolitik.cities[0], 1)
                     && !has_connection(defaultSeaPolitik.cities[1], 0),
                 "default-sea rejected road must prune Politik edges");
    ok &= expect(!defaultSeaRoads.empty() && defaultSeaRoads[0] == 0u,
                 "default-sea rejected road must not stamp water cells");
    return ok;
}

bool test_large_road_search_prunes_over_budget_detour()
{
    sm::TerrainData td = make_terrain(300, 300, 160);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 150, y, 0);
    }
    set_cell(td, 150, 0, 160);
    set_cell(td, 150, td.height - 1, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(120, 150, 1));
    p.cities.push_back(make_city(180, 150, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    bool ok = true;
    ok &= expect(stats.attemptedEdges == 1, "over-budget detour edge should be attempted once");
    ok &= expect(stats.componentPrunedEdges == 0,
                 "over-budget detour is same land component and must not component-prune");
    ok &= expect(stats.keptEdges == 0,
                 "over-budget detour must not spend a full-map A* to survive");
    ok &= expect(stats.prunedEdges == 1,
                 "over-budget detour must be capped and pruned");
    ok &= expect(stats.expansions > 0,
                 "over-budget detour must run bounded road A*");
    ok &= expect(stats.expansions <= 4096,
                 "large-map road A* must stay inside the fixed expansion cap");
    for (int y = 0; y < td.height; ++y)
    {
        ok &= expect(roads[std::size_t(y) * td.width + 150] == 0,
                     "over-budget road search must not stamp rejected water wall cells");
        if (!ok)
        {
            break;
        }
    }
    return ok;
}

bool test_tree_spawner_respects_river_buffer()
{
    sm::TerrainData dry = make_terrain(64, 64, 150);
    sm::TerrainData river = dry;
    for (int y = 0; y < river.height; ++y)
    {
        river.riverData[std::size_t(y) * river.width + 32] = 255;
    }

    const std::vector<sm::TreePoint> dryTrees = sm::spawn_trees(dry, std::uint32_t{42}, 0.18f);
    const std::vector<sm::TreePoint> riverTrees = sm::spawn_trees(river, std::uint32_t{42}, 0.18f);

    bool ok = true;
    ok &= expect(!dryTrees.empty(), "control terrain should spawn at least one tree");
    ok &= expect(!riverTrees.empty(), "river terrain should still spawn trees away from rivers");
    ok &= expect(riverTrees.size() < dryTrees.size(),
                 "river exclusion should remove some otherwise valid tree cells");

    for (const sm::TreePoint& t : riverTrees)
    {
        int dx = t.x - 32;
        if (dx > river.width / 2)
        {
            dx -= river.width;
        }
        if (dx < -river.width / 2)
        {
            dx += river.width;
        }
        ok &= expect(dx < -2 || dx > 2,
                     "trees must respect TS two-cell river exclusion buffer");
        if (!ok)
        {
            break;
        }
    }
    return ok;
}

bool test_tree_spawner_uses_map_sea_level()
{
    sm::TerrainData td = make_terrain(64, 64, 90);
    for (std::size_t i = 0; i < td.cell_count(); ++i)
    {
        td.rgba[i * 4u + 3] = 255u;
    }

    const std::vector<sm::TreePoint> lowSeaTrees =
        sm::spawn_trees(td, std::uint32_t{42}, 0.18f, 0.30f);
    const std::vector<sm::TreePoint> defaultSeaTrees =
        sm::spawn_trees(td, std::uint32_t{42}, 0.18f, 0.40f);

    bool ok = true;
    ok &= expect(!lowSeaTrees.empty(),
                 "tree spawner must allow active low-sea shoreline land cells");
    ok &= expect(defaultSeaTrees.empty(),
                 "tree spawner must reject cells below active default sea level");
    return ok;
}

bool test_malformed_terrain_fails_closed()
{
    sm::TerrainData td;
    td.width = 4;
    td.height = 4;
    td.rgba.assign(3u, 255u);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(0, 0, 1));
    p.cities.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);
    const std::vector<sm::TreePoint> trees = sm::spawn_trees(td, std::uint32_t{7}, 0.18f);

    bool ok = true;
    ok &= expect(!td.has_rgba_storage(),
                 "malformed terrain helper must reject short RGBA storage");
    ok &= expect(roads.empty(),
                 "road tracing must fail closed on malformed terrain storage");
    ok &= expect(trees.empty(),
                 "tree spawning must fail closed on malformed terrain storage");
    ok &= expect(stats.cityCount == 2 && stats.attemptedEdges == 0
                     && stats.keptEdges == 0 && stats.prunedEdges == 0,
                 "malformed road tracing must not mutate road stats beyond city count");
    return ok;
}

bool test_politik_malformed_terrain_fails_closed()
{
    sm::TerrainData td;
    td.width = 4;
    td.height = 4;
    td.rgba.assign(3u, 255u);

    sm::Politik p = sm::generate_politik(123u, 8, 8, &td, 102u, 12);

    bool ok = true;
    ok &= expect(!td.has_rgba_storage(),
                 "malformed Politik input must be rejected by terrain helper");
    ok &= expect(p.mapW == 8 && p.mapH == 8,
                 "Politik generation should keep requested valid map dimensions");
    ok &= expect(p.cellOwner.size() == 64u,
                 "Politik generation should allocate ownership for valid map dimensions");
    ok &= expect(!p.cities.empty(),
                 "Politik generation should fall back to no-terrain placement instead of failing open");
    for (const sm::City& c : p.cities)
    {
        ok &= expect(c.x >= 0 && c.x < p.mapW && c.y >= 0 && c.y < p.mapH,
                     "Politik fallback cities must stay inside map bounds");
    }

    sm::snap_cities_to_land(p, td, 102u, 8);
    sm::finalize_politik(p, td, 102u);
    ok &= expect(p.cellOwner.size() == 64u,
                 "malformed terrain finalization must not corrupt ownership storage");

    const sm::Politik invalidMap = sm::generate_politik(123u, 0, 8, &td, 102u, 12);
    ok &= expect(invalidMap.mapW == 0 && invalidMap.mapH == 0
                     && invalidMap.cellOwner.empty() && invalidMap.cities.empty(),
                 "invalid Politik map dimensions must fail closed");
    return ok;
}

bool test_dirt_roads_fail_closed_on_malformed_inputs()
{
    const std::vector<int> oneVillageX{1};
    const std::vector<int> oneVillageY{1};
    const std::vector<std::uint8_t> shortRoadMask{255u};

    const std::vector<std::uint8_t> badDims =
        sm::trace_dirt_roads(0, 4, shortRoadMask, oneVillageX, oneVillageY, nullptr);
    const std::vector<std::uint8_t> shortMask =
        sm::trace_dirt_roads(4, 4, shortRoadMask, oneVillageX, oneVillageY, nullptr);

    std::vector<std::uint8_t> fullRoadMask(25u, 0u);
    fullRoadMask[std::size_t(2) * 5u + 0u] = 255u;
    const std::vector<int> mismatchedY;
    const std::vector<std::uint8_t> mismatchedVillages =
        sm::trace_dirt_roads(5, 5, fullRoadMask, oneVillageX, mismatchedY, nullptr);

    const std::vector<int> wrappedVillageX{-1};
    const std::vector<int> wrappedVillageY{2};
    const std::vector<std::uint8_t> wrapped =
        sm::trace_dirt_roads(5, 5, fullRoadMask, wrappedVillageX, wrappedVillageY, nullptr);

    const std::vector<std::uint8_t> shortLandMask(3u, 255u);
    const std::vector<std::uint8_t> shortLand =
        sm::trace_dirt_roads(5, 5, fullRoadMask, wrappedVillageX, wrappedVillageY,
                             shortLandMask.data(), shortLandMask.size());

    std::vector<std::uint8_t> landMask(25u * 4u, 255u);
    landMask[(std::size_t(2) * 5u + 4u) * 4u + 3u] = 0u;
    const std::vector<std::uint8_t> waterFiltered =
        sm::trace_dirt_roads(5, 5, fullRoadMask, wrappedVillageX, wrappedVillageY,
                             landMask.data(), landMask.size());

    bool ok = true;
    ok &= expect(badDims.empty(),
                 "dirt-road tracing must fail closed on invalid map dimensions");
    ok &= expect(shortMask.empty(),
                 "dirt-road tracing must fail closed on short road masks");
    ok &= expect(mismatchedVillages.empty(),
                 "dirt-road tracing must fail closed on mismatched village arrays");
    ok &= expect(shortLand.empty(),
                 "dirt-road tracing must fail closed on short terrain land masks");
    ok &= expect(wrapped.size() == 25u,
                 "valid dirt-road trace should allocate a full-size mask");
    ok &= expect(wrapped[std::size_t(2) * 5u + 4u] == 255u,
                 "out-of-range village coordinates should wrap before indexing");
    ok &= expect(wrapped[std::size_t(2) * 5u + 0u] == 0u,
                 "dirt roads must not overwrite the main road target cell");
    ok &= expect(waterFiltered.size() == 25u
                     && waterFiltered[std::size_t(2) * 5u + 4u] == 0u,
                 "dirt-road land mask alpha must filter water cells");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= test_road_prunes_water_only_connection();
    ok &= test_road_survives_land_detour_without_water_cells();
    ok &= test_road_uses_a_star_on_open_land_connection();
    ok &= test_road_tracing_uses_map_sea_level();
    ok &= test_large_road_search_prunes_over_budget_detour();
    ok &= test_tree_spawner_respects_river_buffer();
    ok &= test_tree_spawner_uses_map_sea_level();
    ok &= test_malformed_terrain_fails_closed();
    ok &= test_politik_malformed_terrain_fails_closed();
    ok &= test_dirt_roads_fail_closed_on_malformed_inputs();

    if (!ok)
    {
        return 1;
    }

    std::printf("road_river_generation_test: ok\n");
    return 0;
}
