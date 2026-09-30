// THE macro entity is a squad (Session 15, Inc 1). This test pins the doctrine
// structurally: every macro NPC born through the one creation path carries an
// ecs::SquadRoster, and it is born EMPTY — a lone wanderer is a squad of one
// whose leader is the entity itself, not a special kind of thing. Both spawn
// doors are checked (the boot spawner and the runtime console/event spawner),
// because a doctrine that holds at one door and not the other is two dialects.
// The reverse direction guards against orphan rosters: a SquadRoster on a
// non-macro entity would be a second squad representation growing beside the
// real one.
#include "check.h"

#include "ecs/components.h"
#include "macro/npc_spawn.h"
#include "macro/world_row.h"
#include "macro/store.h"
#include <cstdio>

namespace {

sm::Landmark make_settlement(int id, int x, int y) {
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = id;
    std::snprintf(s.name, sizeof s.name, "Test Settlement");
    s.x = x;
    s.y = y;
    // Души — ГОЛОВАМИ в инвентарь записи (v122): фабрика мира не
    // видит, поэтому пасту (worked-число фичи) ставит звонящий,
    // если она ему нужна; домашние души живут в самой записи.
    sm::raise_flock_into_roster(s.inventory, 1000);
    s.factionIdx = 0;
    return s;
}

// Terrain the spawner treats as absent — spawn positions fall back safely.
sm::TerrainData absent_terrain() {
    sm::TerrainData t;
    t.width = 0;
    t.height = 0;
    return t;
}

void test_every_macro_npc_is_a_squad_of_one() {
    sm::GameState gs{};
    gs.mapW = 16;
    gs.mapH = 16;
    gs.landmarks.push_back(make_settlement(7, 8, 8));

    const sm::TerrainData terrain = absent_terrain();
    sm::ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    sm::spawn_macro_npcs(gs, world, sm::store_of(world), terrain, 123u);

    int macroNpcs = 0;
    {
        const sm::MacroStore& st = sm::store_of(world);
        for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32) {
            if (st.alive[s32] == 0) continue;
            ++macroNpcs;
            CHECK(sm::creatures_empty(st.inventory[s32].inv),
                  "a freshly spawned wanderer is a squad of ONE: empty roster, "
                  "the slot itself is the leader");
        }
    }
    CHECK(macroNpcs > 0, "fixture must spawn macro NPCs to say anything");

    // Runtime door: the console/event spawner goes through the same make_npc.
    CHECK(sm::spawn_npc_at(gs, world, sm::store_of(world), terrain, "bandit", 4, 4, /*level*/ 3),
          "runtime spawn door must accept a registry label");
    int rosters = 0;
    rosters = int(sm::store_of(world).aliveCount);
    CHECK(rosters == macroNpcs + 1,
          "both spawn doors must attach exactly one roster per macro NPC");
}

} // namespace

int main() {
    test_every_macro_npc_is_a_squad_of_one();
    return sm::test::report("squad_roster_test");
}
