// The macro-ECS snapshot (Session 17): the living map survives the save.
//
// Before this, a load cleared the registry and re-spawned lords from the
// seed: a killed squad rose again, a levelled leader forgot his campaigns,
// and the runtime ordinal issuer (max-over-living) could hand a dead man's
// identity to a stranger (problems.md 19.24). What is pinned here:
//   · snapshot -> save -> load -> restore round-trips the ENTITIES — wounds,
//     debt, xp, orders, roster, death — not just their scalars;
//   · the killed lord STAYS dead across the save;
//   · MacroSpawnId ordinals are for LIFE: destroy the highest-ordinal squad,
//     save, load, spawn anew — the dead man's ordinal is never reissued
//     (with the old max-over-living scan that exact sequence reissued it).
#include "check.h"

#include "macro/macro_snapshot.h"
#include "macro/npc_spawn.h"
#include "macro/save.h"
#include "macro/state.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "events/quests/quest_types.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace sm;

std::string temp_path(const char* name) {
    const char* dir = std::getenv("TEMP");
    if (!dir || dir[0] == '\0') dir = std::getenv("TMP");
    if (!dir || dir[0] == '\0') dir = ".";
    std::string p(dir);
    const char last = p.empty() ? '\0' : p[p.size() - 1u];
    if (last != '/' && last != '\\') p += '/';
    return p + name;
}

entt::entity find_by_ordinal(ecs::World& w, std::uint32_t ordinal) {
    for (auto [e, sid] : w.reg.view<ecs::MacroSpawnId>().each()) {
        if (sid.index == ordinal) return e;
    }
    return entt::null;
}

void test_snapshot_round_trips_the_living_map() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    TerrainData absent{};
    absent.width = 0;
    absent.height = 0;
    ecs::World w;

    // Squad A: a bandit lord with two guards and a route.
    SquadSpec specA{};
    specA.leaderType = NPCType::Bandit;
    specA.leaderLevel = 5;
    specA.x = 20;
    specA.y = 20;
    specA.factionIndex = faction_index("bandits");
    specA.members.push_back(make_soldier(std::uint8_t(NPCType::Guard), 3, 1001u));
    specA.members.push_back(make_soldier(std::uint8_t(NPCType::Guard), 4, 1002u));
    specA.waypointCount = 2;
    specA.waypoints[0] = 24; specA.waypoints[1] = 20;
    specA.waypoints[2] = 20; specA.waypoints[3] = 20;
    const entt::entity a = spawn_squad(gs, w, absent, specA);
    CHECK_OR_RETURN(a != entt::null, "squad A spawned");

    // Squad B: a lone peasant — a squad of one, its own leader.
    SquadSpec specB{};
    specB.leaderType = NPCType::Peasant;
    specB.leaderLevel = 2;
    specB.x = 40;
    specB.y = 40;
    specB.factionIndex = faction_index("timaert");
    const entt::entity b = spawn_squad(gs, w, absent, specB);
    CHECK_OR_RETURN(b != entt::null, "squad B spawned");

    const std::uint32_t ordinalA = w.reg.get<ecs::MacroSpawnId>(a).index;
    const std::uint32_t ordinalB = w.reg.get<ecs::MacroSpawnId>(b).index;

    // Life happened: A campaigned (xp, debt, a march), B was killed through
    // the tracked-death shape (hp=0 + Dead) the whole game uses.
    auto& rtA = w.reg.get<ecs::MacroNpcRuntime>(a);
    rtA.xp = 777;
    rtA.sp = -15;
    auto& posA = w.reg.get<ecs::Position>(a);
    posA.x = 25.0f;
    posA.y = 21.0f;
    w.reg.get<ecs::Health>(b).hp = 0.0f;
    w.reg.emplace<ecs::Dead>(b);

    // Snapshot -> save -> load -> restore, through the REAL save file.
    const std::string path = temp_path("timaert_macro_snapshot_test.bin");
    std::remove(path.c_str());
    const std::vector<Quest> noQuests;
    CHECK_OR_RETURN(save_game(gs, noQuests, snapshot_macro_ecs(w), path),
                    "the snapshot saved");

    GameState gs2{};
    std::vector<Quest> quests2;
    std::vector<MacroNpcRecord> records2;
    CHECK_OR_RETURN(load_game(gs2, quests2, records2, path),
                    "the snapshot loaded");
    CHECK(gs2.nextMacroSpawnOrdinal == gs.nextMacroSpawnOrdinal,
          "the identity issuer survives the save");

    ecs::World w2;
    restore_macro_ecs(records2, w2, gs2);

    const entt::entity a2 = find_by_ordinal(w2, ordinalA);
    CHECK_OR_RETURN(a2 != entt::null, "leader A restored under his ordinal");
    CHECK(w2.reg.get<ecs::MacroNpcRuntime>(a2).xp == 777,
          "the leader's campaigns (xp) survive the save");
    CHECK(w2.reg.get<ecs::MacroNpcRuntime>(a2).sp == -15,
          "the leader's exhaustion debt survives the save");
    CHECK(w2.reg.get<ecs::Position>(a2).x == 25.0f
              && w2.reg.get<ecs::Position>(a2).y == 21.0f,
          "the march stands - position is the saved one, not the spawn one");
    CHECK(w2.reg.get<ecs::NpcLevel>(a2).value == 5, "the level survives");
    CHECK(w2.reg.get<ecs::SquadRoster>(a2).members.size() == 2,
          "the roster rows survive");
    const auto* orders2 = w2.reg.try_get<ecs::SquadOrders>(a2);
    CHECK(orders2 != nullptr && orders2->waypointCount == 2,
          "the squad's route survives");
    CHECK(!w2.reg.all_of<ecs::Dead>(a2), "the living leader is not dead");

    const entt::entity b2 = find_by_ordinal(w2, ordinalB);
    CHECK_OR_RETURN(b2 != entt::null, "the dead leader is still ON the map");
    CHECK(w2.reg.all_of<ecs::Dead>(b2)
              && w2.reg.get<ecs::Health>(b2).hp == 0.0f,
          "the KILLED lord stays dead across the save");

    // ── Ordinals are for life (19.24). Remove the HIGHEST-ordinal squad
    // entirely — under the old max-over-living issuer the next spawn re-took
    // exactly that ordinal; the persistent counter must never.
    const std::uint32_t highest = ordinalA > ordinalB ? ordinalA : ordinalB;
    const entt::entity doomed = find_by_ordinal(w2, highest);
    CHECK_OR_RETURN(doomed != entt::null, "the highest-ordinal squad exists");
    w2.reg.destroy(doomed);

    SquadSpec specC{};
    specC.leaderType = NPCType::Bandit;
    specC.leaderLevel = 1;
    specC.x = 10;
    specC.y = 10;
    specC.factionIndex = faction_index("bandits");
    const entt::entity c = spawn_squad(gs2, w2, absent, specC);
    CHECK_OR_RETURN(c != entt::null, "a new squad spawned after the load");
    CHECK(w2.reg.get<ecs::MacroSpawnId>(c).index > highest,
          "a dead man's ordinal is NEVER reissued - identity is for life");

    std::remove(path.c_str());
}

} // namespace

int main() {
    test_snapshot_round_trips_the_living_map();
    return sm::test::report("macro_snapshot_test");
}
