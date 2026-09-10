#include "check.h"
#include "macro/npc_spawn.h"
#include "ecs/components.h"

#include <cstdio>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/npc_spawn_contract_test.cpp", 0);
    return 1;
}

sm::Landmark make_settlement(int id, int x, int y) {
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = id;
    s.name = "Test Settlement";
    s.x = x;
    s.y = y;
    s.population = 1000;
    s.mood = sm::SettlementMood::Stable;
    s.kingdomIdx = 0;
    return s;
}

int count_macro_npcs(const sm::ecs::World& world) {
    int count = 0;
    auto view = world.reg.view<const sm::ecs::NPCKind, const sm::ecs::MacroCell>();
    for (auto entity : view) {
        (void)entity;
        ++count;
    }
    return count;
}

// ── NO BODY IS BORN SHORT OF A BAR (CANON S14; owner, 2026-09-09) ─────────
// «Это РПГ, у всех должна быть HP SP MP». Mana was the player's private
// property for the whole life of the project — not by a ruling, but because
// `project_combat` computed maxMp at every birth in the game and dropped it
// on the floor, and nobody ever looked. A missing bar is invisible exactly
// like a skill no formula reads: nothing crashes, nothing warns, the body
// simply is not what it claims to be.
//
// So the guard is a HEADCOUNT over the world's own spawn door, not a sample:
// every macro NPC the genesis raises must carry a full block. The day a new
// pool joins ecs::Pools, this is the line that fails until every birth fills
// it.
int bodies_without_a_full_block(const sm::ecs::World& world) {
    int bad = 0;
    auto view = world.reg.view<const sm::ecs::NPCKind, const sm::ecs::Pools>();
    for (auto entity : view) {
        const auto& pools = view.get<const sm::ecs::Pools>(entity);
        if (pools.maxHp <= 0 || pools.hp <= 0) ++bad;
        if (pools.maxMp <= 0 || pools.mp <= 0) ++bad;
    }
    return bad;
}

bool positions_inside_map(const sm::ecs::World& world, int mapW, int mapH) {
    auto view = world.reg.view<const sm::ecs::NPCKind, const sm::ecs::MacroCell>();
    for (auto entity : view) {
        const auto& c = view.template get<const sm::ecs::MacroCell>(entity);
        const int x = sm::ecs::cell_x(c, mapW);
        const int y = sm::ecs::cell_y(c, mapW);
        if (x < 0 || x >= mapW || y < 0 || y >= mapH) return false;
    }
    return true;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = 16;
    gs.mapH = 16;
    gs.landmarks.push_back(make_settlement(7, 8, 8));

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
    CHECK(bodies_without_a_full_block(world) == 0,
          "every body the world raises is born with EVERY pool filled — "
          "mana included, for all of them, not for the player alone");

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
    invalidMap.landmarks.push_back(make_settlement(8, 0, 0));

    sm::ecs::World invalidMapWorld;
    sm::spawn_macro_npcs(invalidMap, invalidMapWorld, invalidTerrain, 125u);
    if (count_macro_npcs(invalidMapWorld) != 0)
        return fail("invalid map dimensions must fail closed without NPC spawns");

    std::printf("npc_spawn_contract_test: ok spawned=%d mismatch=%d\n",
                spawned, count_macro_npcs(mismatchWorld));
    CHECK(true, "every gate above held");
    return sm::test::report("npc_spawn_contract_test");
}
