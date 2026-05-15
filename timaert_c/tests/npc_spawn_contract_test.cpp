#include "macro/npc_spawn.h"
#include "ecs/components.h"

#include <cstdio>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL npc_spawn_contract_test: %s\n", msg);
    return 1;
}

sm::Settlement make_settlement(int id, int x, int y) {
    sm::Settlement s{};
    s.id = id;
    s.name = "Test Settlement";
    s.x = x;
    s.y = y;
    s.population = 1000;
    s.mood = sm::SettlementMood::Stable;
    s.kingdomIdx = 0;
    s.economy = "farming";
    return s;
}

int count_macro_npcs(const sm::ecs::World& world) {
    int count = 0;
    auto view = world.reg.view<const sm::ecs::NPCKind, const sm::ecs::Position>();
    for (auto entity : view) {
        (void)entity;
        ++count;
    }
    return count;
}

bool positions_inside_map(const sm::ecs::World& world, int mapW, int mapH) {
    auto view = world.reg.view<const sm::ecs::NPCKind, const sm::ecs::Position>();
    for (auto entity : view) {
        const auto& p = view.template get<const sm::ecs::Position>(entity);
        if (p.x < 0.0f || p.x >= float(mapW) || p.y < 0.0f || p.y >= float(mapH))
            return false;
    }
    return true;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = 16;
    gs.mapH = 16;
    gs.settlements.push_back(make_settlement(7, 8, 8));

    sm::TerrainData invalidTerrain;
    invalidTerrain.width = 0;
    invalidTerrain.height = 0;
    invalidTerrain.rgba.assign(3u, 255u);

    sm::ecs::World world;
    sm::spawn_macro_npcs(gs, world, invalidTerrain, 123u);

    const int spawned = count_macro_npcs(world);
    if (spawned <= 0)
        return fail("invalid terrain should not suppress all macro NPC spawns");
    if (!positions_inside_map(world, gs.mapW, gs.mapH))
        return fail("macro NPC fallback positions must stay inside map bounds");

    sm::TerrainData mismatchedTerrain;
    mismatchedTerrain.width = 8;
    mismatchedTerrain.height = 8;
    mismatchedTerrain.rgba.assign(std::size_t(8 * 8 * 4), 0u);

    sm::ecs::World mismatchWorld;
    sm::spawn_macro_npcs(gs, mismatchWorld, mismatchedTerrain, 124u);
    if (count_macro_npcs(mismatchWorld) <= 0)
        return fail("mismatched terrain should be treated as absent terrain");
    if (!positions_inside_map(mismatchWorld, gs.mapW, gs.mapH))
        return fail("mismatched terrain fallback positions must stay inside map bounds");

    sm::GameState invalidMap;
    invalidMap.mapW = 0;
    invalidMap.mapH = 0;
    invalidMap.settlements.push_back(make_settlement(8, 0, 0));

    sm::ecs::World invalidMapWorld;
    sm::spawn_macro_npcs(invalidMap, invalidMapWorld, invalidTerrain, 125u);
    if (count_macro_npcs(invalidMapWorld) != 0)
        return fail("invalid map dimensions must fail closed without NPC spawns");

    std::printf("npc_spawn_contract_test: ok spawned=%d mismatch=%d\n",
                spawned, count_macro_npcs(mismatchWorld));
    return 0;
}
