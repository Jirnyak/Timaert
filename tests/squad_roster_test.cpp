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

namespace {

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
    gs.settlements.push_back(make_settlement(7, 8, 8));

    const sm::TerrainData terrain = absent_terrain();
    sm::ecs::World world;
    sm::spawn_macro_npcs(gs, world, terrain, 123u);

    int macroNpcs = 0;
    for (auto [e, rt] : world.reg.view<sm::ecs::MacroNpcRuntime>().each()) {
        (void)rt;
        ++macroNpcs;
        CHECK(world.reg.all_of<sm::ecs::SquadRoster>(e),
              "every macro NPC must carry a SquadRoster - the entity IS a squad");
        if (const auto* roster = world.reg.try_get<sm::ecs::SquadRoster>(e)) {
            CHECK(roster->members.empty(),
                  "a freshly spawned wanderer is a squad of ONE: empty roster, "
                  "the entity itself is the leader");
        }
    }
    CHECK(macroNpcs > 0, "fixture must spawn macro NPCs to say anything");

    // Runtime door: the console/event spawner goes through the same make_npc.
    CHECK(sm::spawn_npc_at(gs, world, terrain, "bandit", 4, 4, /*level*/ 3),
          "runtime spawn door must accept a registry label");
    int rosters = 0;
    for (auto [e, roster] : world.reg.view<sm::ecs::SquadRoster>().each()) {
        (void)roster;
        ++rosters;
        CHECK(world.reg.all_of<sm::ecs::MacroNpcRuntime>(e),
              "a SquadRoster may only ride a macro entity - no orphan squads");
    }
    CHECK(rosters == macroNpcs + 1,
          "both spawn doors must attach exactly one roster per macro NPC");
}

} // namespace

int main() {
    test_every_macro_npc_is_a_squad_of_one();
    return sm::test::report("squad_roster_test");
}
