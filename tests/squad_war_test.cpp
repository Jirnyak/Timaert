// Squad↔squad perception and war on the macro map (Session 15, Inc 5).
//
// Before this, macro NPCs were ghosts to each other: no behaviour could see
// another squad, and two armies could share a cell for years. What is pinned
// here is the owner's design made live:
//   · a hostile squad NEARBY is perceived through the transient SquadIndex
//     and answered by the strength law — the weak flee, fighters pursue;
//   · a hostile squad ON THE SAME CELL is a battle, resolved by the one
//     auto-battle law and settled through the one ledger: roster rows die by
//     name, a fallen leader is hp=0 + Dead, the victor takes the spoils and
//     the XP — and levels by the same curve the player climbs;
//   · neutral squads ignore each other, and the underground drive
//     (allowAutoBattle=false) perceives but does not resolve.
// Every fixture builds its relations explicitly: a world with no hostility
// rows must behave exactly as the world always did (the parity test guards
// that side).
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/squad.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "core/torus.h"

#include <cstdint>
#include <initializer_list>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Both leaders wear real registry factions; the fixture then writes the
// relation it wants between them — through the same ensure_faction_row door
// the game uses — so hostility is data here exactly as it is in the world.
GameState make_world(int relation) {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    ensure_faction_row(gs, "bandits").relations["timaert"] = relation;
    ensure_faction_row(gs, "timaert").relations["bandits"] = relation;
    return gs;
}

entt::entity make_squad_at(ecs::World& w, NPCType type, const char* faction,
                           int level, float x, float y, std::uint32_t ordinal,
                           std::initializer_list<std::uint32_t> memberIds,
                           NPCType memberKind, int memberLevel) {
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, x, y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(type),
                              std::uint16_t(faction_index(faction)));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = -1;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.sp = 500;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, ordinal);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(level));
    const CharacterSheet sheet = make_character_sheet(
        type, level, ordinal * 2654435761u + 0x51ADu);
    const float hp = std::max(
        1.0f, std::floor(project_combat(sheet, npc_def(type).combat).hp));
    reg.emplace<ecs::Health>(e, hp, hp);
    auto& roster = reg.emplace<ecs::SquadRoster>(e);
    for (std::uint32_t id : memberIds) {
        roster.members.push_back(
            make_soldier(std::uint8_t(memberKind), memberLevel, id));
    }
    reg.emplace<ecs::NpcInventory>(e);
    return e;
}

int roster_count(ecs::World& w, GameState& gs, std::uint32_t ordinal) {
    MacroWorld mw{&gs, nullptr, &w};
    return macro_stock_read(mw, MacroStock::Roster,
                            MacroStockKey{std::int32_t(ordinal), 0, 0});
}

float dist(ecs::World& w, entt::entity a, entt::entity b) {
    const auto& pa = w.reg.get<ecs::Position>(a);
    const auto& pb = w.reg.get<ecs::Position>(b);
    return std::sqrt(torus_dist_sq(pa.x, pa.y, pb.x, pb.y,
                                   float(kMap), float(kMap)));
}

void drive(GameState& gs, ecs::World& w, MacroNpcAiRuntime& rt, int thinks,
           bool allowAutoBattle = true) {
    for (int i = 0; i < thinks; ++i) {
        tick_macro_npc_ai(gs, w, nullptr, rt, kAiTicks, allowAutoBattle);
    }
}

// A geometric meeting of hostiles IS a battle, settled through the ledger.
void test_hostiles_on_one_cell_fight_and_the_ledger_pays() {
    GameState gs = make_world(-80);
    ecs::World w;
    // A strong bandit warband and a weak caravan, standing on the same cell.
    const auto bandit = make_squad_at(w, NPCType::Bandit, "bandits", 5,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 4);
    const auto caravan = make_squad_at(w, NPCType::Caravan, "timaert", 1,
                                       10.0f, 10.0f, 2u, {21u},
                                       NPCType::Peasant, 1);
    w.reg.get<ecs::NpcInventory>(caravan).inv.add("mat_wood", 5);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 42u);
    drive(gs, w, rt, 1);

    CHECK(roster_count(w, gs, 2u) == 0,
          "the caravan's roster died by name through the roster row");
    CHECK(w.reg.all_of<ecs::Dead>(caravan)
              && w.reg.get<ecs::Health>(caravan).hp == 0.0f,
          "a loser whose whole roster fell falls with it - the tracked-death shape");
    CHECK(!w.reg.all_of<ecs::Dead>(bandit),
          "the crushing winner survives");
    CHECK(w.reg.get<ecs::NpcInventory>(bandit).inv.count("mat_wood") == 5
              && w.reg.get<ecs::NpcInventory>(caravan).inv.stacks.empty(),
          "the raid PAYS: the fallen owner's goods pass to the victor");
    CHECK(w.reg.get<ecs::MacroNpcRuntime>(bandit).xp > 0
              || w.reg.get<ecs::NpcLevel>(bandit).value > 5,
          "victory pays experience through the one reward law");
}

// The weak run from strength they can see; fighters close in on prey.
void test_the_weak_flee_and_fighters_pursue() {
    GameState gs = make_world(-80);
    ecs::World w;
    const auto bandit = make_squad_at(w, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 5);
    const auto caravan = make_squad_at(w, NPCType::Caravan, "timaert", 1,
                                       14.0f, 10.0f, 2u, {},
                                       NPCType::Peasant, 1);
    // Freeze the bandits: resting with empty stamina, so the caravan's
    // flight is measured against a fixed threat.
    {
        auto& brt = w.reg.get<ecs::MacroNpcRuntime>(bandit);
        brt.sp = 0;
        brt.state = std::uint8_t(NPCState::Resting);
    }
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 43u);
    const float before = dist(w, bandit, caravan);
    drive(gs, w, rt, 3);
    CHECK(w.reg.get<ecs::MacroNpcRuntime>(caravan).state
              == std::uint8_t(NPCState::Fleeing),
          "a squad that cannot win runs - the strength law says so");
    CHECK(dist(w, bandit, caravan) > before,
          "and running away actually opens distance");

    // Now the mirror: freeze the caravan, wake the bandits - the fighter row
    // pursues the prey the same law says it beats.
    GameState gs2 = make_world(-80);
    ecs::World w2;
    const auto hunter = make_squad_at(w2, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 5);
    const auto prey = make_squad_at(w2, NPCType::Caravan, "timaert", 1,
                                    15.0f, 10.0f, 2u, {},
                                    NPCType::Peasant, 1);
    {
        auto& crt = w2.reg.get<ecs::MacroNpcRuntime>(prey);
        crt.sp = 0;
        crt.state = std::uint8_t(NPCState::Resting);
    }
    MacroNpcAiRuntime rt2{};
    reset_macro_npc_ai_runtime(rt2, 44u);
    const float before2 = dist(w2, hunter, prey);
    drive(gs2, w2, rt2, 3);
    CHECK(w2.reg.get<ecs::MacroNpcRuntime>(hunter).state
              == std::uint8_t(NPCState::Chasing),
          "a fighter row pursues prey the law says it beats");
    CHECK(dist(w2, hunter, prey) < before2,
          "and pursuit actually closes distance");
}

// Neutral squads pass each other exactly as they always did.
void test_neutral_squads_ignore_each_other() {
    GameState gs = make_world(0);
    ecs::World w;
    const auto a = make_squad_at(w, NPCType::Bandit, "bandits", 5,
                                 10.0f, 10.0f, 1u, {11u, 12u},
                                 NPCType::Bandit, 4);
    const auto b = make_squad_at(w, NPCType::Caravan, "timaert", 1,
                                 10.0f, 10.0f, 2u, {21u},
                                 NPCType::Peasant, 1);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 45u);
    drive(gs, w, rt, 3);
    CHECK(!w.reg.all_of<ecs::Dead>(a) && !w.reg.all_of<ecs::Dead>(b),
          "no relation below the line, no war - hostility is data");
    CHECK(roster_count(w, gs, 1u) == 2 && roster_count(w, gs, 2u) == 1,
          "nobody's roster paid for a meeting of neutrals");
}

// The underground drive perceives but does not resolve (live projected
// bodies own their own fight down there).
void test_no_auto_battle_when_the_ground_owns_the_fight() {
    GameState gs = make_world(-80);
    ecs::World w;
    const auto a = make_squad_at(w, NPCType::Bandit, "bandits", 5,
                                 10.0f, 10.0f, 1u, {11u, 12u},
                                 NPCType::Bandit, 4);
    const auto b = make_squad_at(w, NPCType::Caravan, "timaert", 1,
                                 10.0f, 10.0f, 2u, {21u},
                                 NPCType::Peasant, 1);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 46u);
    drive(gs, w, rt, 3, /*allowAutoBattle*/false);
    CHECK(!w.reg.all_of<ecs::Dead>(a) && !w.reg.all_of<ecs::Dead>(b)
              && roster_count(w, gs, 2u) == 1,
          "with the resolver gated off, a meeting resolves nothing");
}

// The wandering-tsar seed: any leader that wins fights levels by the same
// curve the player climbs - a data row, not a branch. Battle XP inflow is
// pinned by the first test; here the CONSUMPTION law: enough experience
// turns into a level and a bigger macro ceiling, wounds preserved as the
// fraction they always travel in.
void test_a_victorious_leader_levels() {
    GameState gs = make_world(-80);
    ecs::World w;
    const auto tsar = make_squad_at(w, NPCType::Peasant, "timaert", 1,
                                    10.0f, 10.0f, 1u, {}, NPCType::Peasant, 1);
    auto& hp = w.reg.get<ecs::Health>(tsar);
    const float maxHp0 = hp.maxHp;
    hp.hp = std::floor(hp.maxHp * 0.5f);   // walks in wounded
    const float frac0 = hp.hp / hp.maxHp;

    CHECK(award_leader_xp(w, tsar, exp_to_next_level(1) / 2) == 0,
          "half a bar is not a level");
    CHECK(award_leader_xp(w, tsar, exp_to_next_level(1)) >= 1,
          "a full bar turns into a level by the player's own curve");
    CHECK(w.reg.get<ecs::NpcLevel>(tsar).value >= 2,
          "the level landed on the leader");
    const auto& hp1 = w.reg.get<ecs::Health>(tsar);
    CHECK(hp1.maxHp > maxHp0,
          "the macro ceiling grows with the level");
    const float frac1 = hp1.hp / hp1.maxHp;
    CHECK(std::fabs(frac1 - frac0) < 0.1f,
          "the wound crossed the level-up as a fraction, not a free heal");
}

// The player's auto-resolve settles through the SAME halves (Inc 6): the
// enemy through the ledger and the tracked-death shape, the player where his
// truth lives — army rows by name, the wound fraction into combatStats, XP
// by award_exp. Outcomes are CRAFTED here: this pins the settling law, not
// the resolver's dice (those have their own tests).
void test_player_auto_resolve_settles_through_the_same_doors() {
    GameState gs = make_world(-80);
    gs.player.army.members.push_back(
        make_soldier(std::uint8_t(NPCType::Guard), 3, 501u));
    gs.player.army.members.push_back(
        make_soldier(std::uint8_t(NPCType::Guard), 3, 502u));
    gs.player.combatStats.maxHp = 100;
    gs.player.combatStats.currentHp = 100;
    const int level0 = gs.player.sheet.levelData.level;
    const int exp0 = gs.player.sheet.levelData.exp;

    ecs::World w;
    const auto enemy = make_squad_at(w, NPCType::Bandit, "bandits", 3,
                                     10.0f, 10.0f, 9u, {31u, 32u},
                                     NPCType::Bandit, 2);
    w.reg.get<ecs::NpcInventory>(enemy).inv.add("mat_wood", 4);

    // The player WINS: one of his men fell, he limps out at 60%; the enemy
    // is wiped — roster and leader both.
    AutoBattleOutcome win{};
    win.winner = 0;                       // player is side A
    win.casualtiesA = {501u};
    win.casualtiesB = {31u, 32u};
    win.leaderFractionA = 0.6f;
    win.leaderFractionB = 0.0f;
    const int xp = settle_player_auto_battle(gs, w, enemy, win,
                                             /*playerIsA*/true);
    CHECK(total_soldiers(gs.player.army) == 1
              && gs.player.army.members[0].entityId == 502u,
          "the player's fallen soldier left the army by name");
    CHECK(gs.player.combatStats.currentHp == 60,
          "the player's wound landed as the fraction, on the macro scalar");
    CHECK(roster_count(w, gs, 9u) == 0 && w.reg.all_of<ecs::Dead>(enemy),
          "the enemy died through the ledger and the tracked-death shape");
    CHECK(gs.player.inventory.count("mat_wood") == 4,
          "the fallen owner's goods landed in the player's own bag");
    CHECK(xp > 0
              && (gs.player.sheet.levelData.exp > exp0
                  || gs.player.sheet.levelData.level > level0),
          "victory paid the player experience through award_exp");

    // The player LOSES with a man still standing: wounded, never dead — the
    // leader rule holds for the player exactly as for any lord.
    GameState gs2 = make_world(-80);
    gs2.player.army.members.push_back(
        make_soldier(std::uint8_t(NPCType::Guard), 3, 601u));
    gs2.player.army.members.push_back(
        make_soldier(std::uint8_t(NPCType::Guard), 3, 602u));
    gs2.player.combatStats.maxHp = 100;
    gs2.player.combatStats.currentHp = 100;
    ecs::World w2;
    const auto victor = make_squad_at(w2, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 9u, {41u},
                                      NPCType::Bandit, 5);
    AutoBattleOutcome loss{};
    loss.winner = 1;
    loss.casualtiesA = {601u};            // one of two fell — not a wipe
    loss.leaderFractionA = 0.15f;
    loss.leaderFractionB = 0.9f;
    settle_player_auto_battle(gs2, w2, victor, loss, /*playerIsA*/true);
    CHECK(total_soldiers(gs2.player.army) == 1,
          "defeat took the fallen and left the survivor");
    CHECK(gs2.player.combatStats.currentHp >= 1,
          "while one of his men stands, defeat wounds the player - "
          "never kills him");
    CHECK(!w2.reg.all_of<ecs::Dead>(victor),
          "the victor rides on");
}

// Squad creation as data (Inc 7): one spec, one door. The leader comes out
// of make_npc whole (the ONE creation path), the roster and the route are
// fields of the spec — and the ROUTE'S PRESENCE overrides the type row's ai
// (owner's ruling: one knob), which is what makes a player patrol a data
// row and not a code path.
void test_spawn_squad_is_one_spec_one_door() {
    GameState gs = make_world(0);
    gs.worldSeed = 777u;
    TerrainData absent{};
    absent.width = 0;
    absent.height = 0;
    ecs::World w;

    SquadSpec spec{};
    // A Peasant row's ai is HomeWanderer with no home (homeId -1 does
    // nothing) — so any march this squad makes is the ROUTE acting.
    spec.leaderType = NPCType::Peasant;
    spec.leaderLevel = 4;
    spec.x = 20;
    spec.y = 20;
    spec.factionIndex = faction_index("timaert");
    spec.members.push_back(make_soldier(
        std::uint8_t(NPCType::Guard), 3, 0x40000001u));
    spec.members.push_back(make_soldier(
        std::uint8_t(NPCType::Guard), 3, 0x40000002u));
    spec.waypointCount = 2;
    spec.waypoints[0] = 24; spec.waypoints[1] = 20;   // 4 cells east
    spec.waypoints[2] = 20; spec.waypoints[3] = 20;   // and back

    const entt::entity leader = spawn_squad(gs, w, absent, spec);
    CHECK_OR_RETURN(leader != entt::null && w.reg.valid(leader),
                    "the spec became a squad");
    CHECK((w.reg.all_of<ecs::MacroNpcRuntime, ecs::MacroSpawnId,
                        ecs::Health, ecs::NpcLevel, ecs::NpcCharacter,
                        ecs::NpcInventory, ecs::SquadRoster>(leader)),
          "the leader came out of the ONE creation door, whole");
    CHECK(w.reg.get<ecs::NpcLevel>(leader).value == 4,
          "the spec's level pinned the leader's level");
    CHECK(w.reg.get<ecs::SquadRoster>(leader).members.size() == 2,
          "the roster rows are the spec's rows");
    const auto* orders = w.reg.try_get<ecs::SquadOrders>(leader);
    CHECK(orders != nullptr && orders->waypointCount == 2,
          "the route landed as data on the squad");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    const float x0 = w.reg.get<ecs::Position>(leader).x;
    drive(gs, w, rt, 3);
    const auto& p1 = w.reg.get<ecs::Position>(leader);
    CHECK(p1.x > x0,
          "waypoint orders MARCH the squad east toward its route - the "
          "override, not the row, is steering");

    // Reaching a waypoint advances the route.
    for (int i = 0; i < 20; ++i) drive(gs, w, rt, 1);
    CHECK(w.reg.get<ecs::SquadOrders>(leader).currentWaypoint != 0
              || w.reg.get<ecs::Position>(leader).x < 23.0f,
          "the route advances at a reached waypoint (or is already homing "
          "back on the second leg)");

    // A second squad continues the ordinal line — two squads, two names.
    SquadSpec other = spec;
    other.x = 40;
    const entt::entity second = spawn_squad(gs, w, absent, other);
    CHECK_OR_RETURN(second != entt::null, "the second squad spawned");
    CHECK(w.reg.get<ecs::MacroSpawnId>(second).index
              != w.reg.get<ecs::MacroSpawnId>(leader).index,
          "each created squad gets its own save-stable ordinal");
}

} // namespace

int main() {
    test_hostiles_on_one_cell_fight_and_the_ledger_pays();
    test_the_weak_flee_and_fighters_pursue();
    test_neutral_squads_ignore_each_other();
    test_no_auto_battle_when_the_ground_owns_the_fight();
    test_a_victorious_leader_levels();
    test_player_auto_resolve_settles_through_the_same_doors();
    test_spawn_squad_is_one_spec_one_door();
    return sm::test::report("squad_war_test");
}
