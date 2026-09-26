#include "check.h"
#include "macro/npc_spawn.h"
#include "ecs/components.h"
#include "macro/store.h"

#include <cstdio>

namespace {

sm::Landmark make_settlement(int id, int x, int y) {
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = id;
    s.name = "Test Settlement";
    s.x = x;
    s.y = y;
    s.population = 1000;
    s.factionIdx = 0;
    return s;
}

int count_macro_npcs(const sm::ecs::World& world) {
    // ФЛИП 1в: макро-сквад = носитель MacroSlot; состояние — колонки store.
    int count = 0;
    for (auto entity :
         const_cast<sm::ecs::World&>(world).reg.view<sm::ecs::MacroSlot>()) {
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
    auto& w = const_cast<sm::ecs::World&>(world);
    const sm::MacroStore& st = sm::store_of(w);
    for (auto entity : w.reg.view<sm::ecs::MacroSlot>()) {
        const auto& pools = st.pools[sm::slot_of(w.reg, entity)];
        if (pools.maxHp <= 0 || pools.hp <= 0) ++bad;
        if (pools.maxMp <= 0 || pools.mp <= 0) ++bad;
    }
    return bad;
}

bool positions_inside_map(const sm::ecs::World& world, int mapW, int mapH) {
    auto& w = const_cast<sm::ecs::World&>(world);
    const sm::MacroStore& st = sm::store_of(w);
    for (auto entity : w.reg.view<sm::ecs::MacroSlot>()) {
        const auto& c = st.cell[sm::slot_of(w.reg, entity)];
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

    auto worldStore_ = sm::make_macro_store();

    sm::store_attach(world, worldStore_.get());
    sm::spawn_macro_npcs(gs, world, sm::store_of(world), invalidTerrain, 123u);

    // FAIL-CLOSED, BOTH WAYS. A terrain the world cannot read must not
    // silence the world (no NPCs at all), and must not let it write outside
    // itself either. The two used to share `return fail(...)`, so the first
    // ended the test and the second was never reached on the run that needed
    // it most.
    const int spawned = count_macro_npcs(world);
    CHECK(spawned > 0,
          "unreadable terrain does not suppress macro NPC spawns — the world "
          "falls back, it does not go empty");
    CHECK(positions_inside_map(world, gs.mapW, gs.mapH),
          "...and every fallback position still lands INSIDE the map");
    CHECK(bodies_without_a_full_block(world) == 0,
          "every body the world raises is born with EVERY pool filled — "
          "mana included, for all of them, not for the player alone");

    sm::TerrainData mismatchedTerrain;
    mismatchedTerrain.width = 8;
    mismatchedTerrain.height = 8;
    mismatchedTerrain.rgba.assign(std::size_t(8 * 8 * 4), 0u);

    sm::ecs::World mismatchWorld;

    auto mismatchWorldStore_ = sm::make_macro_store();

    sm::store_attach(mismatchWorld, mismatchWorldStore_.get());
    sm::spawn_macro_npcs(gs, mismatchWorld, sm::store_of(mismatchWorld), mismatchedTerrain, 124u);
    CHECK(count_macro_npcs(mismatchWorld) > 0,
          "terrain whose size disagrees with the map is treated as ABSENT "
          "terrain, not as a reason to spawn nobody");
    CHECK(positions_inside_map(mismatchWorld, gs.mapW, gs.mapH),
          "...and its fallback positions stay inside the map too");

    sm::GameState invalidMap;
    invalidMap.mapW = 0;
    invalidMap.mapH = 0;
    invalidMap.landmarks.push_back(make_settlement(8, 0, 0));

    sm::ecs::World invalidMapWorld;

    auto invalidMapWorldStore_ = sm::make_macro_store();

    sm::store_attach(invalidMapWorld, invalidMapWorldStore_.get());
    sm::spawn_macro_npcs(invalidMap, invalidMapWorld, sm::store_of(invalidMapWorld), invalidTerrain, 125u);
    // THE NEGATIVE CONTROL of the two above, and the reason they are not
    // vacuous: a world with no dimensions has nowhere to put anybody, so the
    // spawner must refuse rather than fall back. If this ever passed by
    // spawning zero for the WRONG reason, the two "does not go empty" checks
    // above would be the ones to notice.
    CHECK(count_macro_npcs(invalidMapWorld) == 0,
          "a map with no dimensions fails CLOSED — nobody is spawned at all");

    std::printf("npc_spawn_contract_test: ok spawned=%d mismatch=%d\n",
                spawned, count_macro_npcs(mismatchWorld));
    return sm::test::report("npc_spawn_contract_test");
}
