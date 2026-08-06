// The macro AI's eight behaviours, each asserted as the promise it makes:
// where the NPC decides to go, and what that decision costs it.
//
// This file was green for months while asserting nothing (`int fail()` returned
// into a `bool` — every failure read as PASS). It now goes through
// tests/check.h, where no function carries a verdict and a test that runs zero
// checks fails by counting. The bundled `if (a || b || c) fail(...)` conditions
// were split: one promise per check, so a red line names the promise it broke
// instead of the seven it was bundled with.
#include "check.h"

#include "macro/npc_ai.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

sm::Settlement settlement(int id, int x, int y) {
    sm::Settlement s{};
    s.id = id;
    s.name = "Test";
    s.x = x;
    s.y = y;
    s.population = 1000;
    s.mood = sm::SettlementMood::Stable;
    s.kingdomIdx = 0;
    s.economy = "trade";
    return s;
}

entt::entity spawn_ai(sm::ecs::World& world,
                      sm::NPCType type,
                      float x,
                      float y,
                      int homeId,
                      sm::NPCState state = sm::NPCState::Idle,
                      int timer = 0,
                      int sp = 100) {
    auto e = world.reg.create();
    world.reg.emplace<sm::ecs::Position>(e, x, y, 0.0f);
    world.reg.emplace<sm::ecs::VisualPos>(e, x, y, 0.0f);
    world.reg.emplace<sm::ecs::NPCKind>(e, std::uint16_t(type), std::uint16_t{0});

    sm::ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeId;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.stateTimer = std::int16_t(timer);
    rt.teleportCooldown = 0;
    rt.sp = std::int16_t(sp);
    rt.state = std::uint8_t(state);
    rt.visualSpeed = 0.0f;
    rt.tickAccum = 0.0f;
    world.reg.emplace<sm::ecs::MacroNpcRuntime>(e, rt);
    world.reg.emplace<sm::ecs::Health>(e, 50.0f, 50.0f);
    return e;
}

void tick_once(sm::GameState& gs,
               sm::ecs::World& world,
               sm::MacroNpcAiRuntime& runtime,
               const sm::TreeGrid* treeGrid = nullptr) {
    sm::tick_macro_npc_ai(gs, world, treeGrid, runtime, sm::kAiTicks);
}

bool close_enough(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

bool targets(const sm::ecs::MacroNpcRuntime& rt, float x, float y) {
    return close_enough(rt.targetX, x) && close_enough(rt.targetY, y);
}

bool in_state(const sm::ecs::MacroNpcRuntime& rt, sm::NPCState s) {
    return rt.state == std::uint8_t(s);
}

void test_home_wanderer_returns_when_far() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 50, 50));

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 80.0f, 50.0f, 1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 10u);
    tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Returning),
          "a HomeWanderer 30 cells from home enters Returning");
    CHECK(targets(rt, 50.0f, 50.0f),
          "a returning HomeWanderer aims at its OWN home settlement");
}

void test_woodcutter_targets_nearest_tree() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 20, 20));
    // Two trees: one within reach, one across the map. The near one must win —
    // "nearest", not "first in the grid".
    std::vector<sm::TreePoint> trees{{23, 20}, {80, 80}};
    sm::TreeGrid grid;
    sm::build_tree_grid(grid, trees, gs.mapW, gs.mapH, 32);

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Woodcutter, 21.0f, 20.0f, 1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 20u);
    tick_once(gs, world, runtime, &grid);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Traveling),
          "a Woodcutter with a tree in range travels to it");
    CHECK(targets(rt, 23.0f, 20.0f),
          "a Woodcutter picks the NEAREST tree, not the far one");
}

void test_trader_targets_other_settlement() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 10, 10));
    gs.settlements.push_back(settlement(2, 40, 10));

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Merchant, 10.0f, 10.0f, 1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 30u);
    tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Traveling),
          "a Trader standing at home sets out");
    CHECK(rt.targetSettlementId == 2,
          "a Trader trades AWAY from home: never its own settlement");
    CHECK(targets(rt, 40.0f, 10.0f),
          "the Trader's target cell is the chosen settlement's cell");
}

void test_nomad_excludes_current_target() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 10, 10));
    gs.settlements.push_back(settlement(2, 40, 10));

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Caravan, 40.0f, 10.0f, -1);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    rt.targetSettlementId = 2;

    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 40u);
    tick_once(gs, world, runtime);

    CHECK(in_state(rt, sm::NPCState::Traveling),
          "a Nomad that arrived picks a new leg immediately");
    CHECK(rt.targetSettlementId == 1,
          "a Nomad never re-picks the settlement it is already standing at");
    CHECK(targets(rt, 10.0f, 10.0f),
          "the Nomad's target cell follows the settlement it chose");
}

void test_aggressive_chases_visible_player() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.player.x = 12.0f;
    gs.player.y = 10.0f;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 50u);
    tick_once(gs, world, runtime);

    auto& p = world.reg.get<sm::ecs::Position>(e);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Chasing),
          "an Aggressive NPC that can see the player gives chase");
    CHECK(targets(rt, 12.0f, 10.0f),
          "the chase aims at where the player actually is");
    CHECK(close_enough(p.x, 11.0f) && close_enough(p.y, 10.0f),
          "one AI tick closes exactly one cell of the two-cell gap");
    CHECK(rt.sp == 90, "chasing spends stamina: a chase is not free");
    CHECK(close_enough(rt.visualSpeed, 2.0f),
          "the visual speed reports the pace the chase actually moved at");
}

void test_patrol_returns_when_far_from_home() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 50, 50));

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Guard, 70.0f, 50.0f, 1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 60u);
    tick_once(gs, world, runtime);

    auto& p = world.reg.get<sm::ecs::Position>(e);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Returning),
          "a Patrol that strayed past its leash turns back");
    CHECK(targets(rt, 50.0f, 50.0f),
          "the returning Patrol aims at the settlement it guards");
    CHECK(close_enough(p.x, 69.0f),
          "the Patrol actually MOVES homeward on the same tick it decides to");
}

void test_teleporter_cooldown_counts_down() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Witch, 20.0f, 20.0f, -1);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    rt.teleportCooldown = 2;

    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 70u);
    tick_once(gs, world, runtime);

    CHECK(rt.teleportCooldown == 1,
          "a Teleporter's cooldown burns down one AI tick at a time");
}

void test_wanderer_enters_wandering_state() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Sorceress, 30.0f, 30.0f, -1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 80u);
    tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Wandering),
          "a homeless Wanderer starts wandering rather than standing still");
    CHECK(!targets(rt, 30.0f, 30.0f),
          "wandering picks somewhere ELSE: its own cell is not a destination");
}

void test_resting_recovery_prevents_permanent_stall() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1,
                      sm::NPCState::Resting, 0, 0);

    // Resting exits at HALF the bar, and the regen law is a percent of the
    // bar per game hour (kSpRegenPctPerHour = 1/8): from empty that is 4 game
    // hours ≈ 43 thinks, whatever the bar's size (the old 10-tick loop was
    // calibrated to the retired 5%-per-think dialect). Tick until the state
    // flips — the NPC starts LIVING again right after, so a fixed overshoot
    // would measure wandering, not resting; 128 thinks (a half-day) is the
    // honesty bound.
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 90u);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    int thinks = 0;
    while (in_state(rt, sm::NPCState::Resting) && thinks < 128) {
        tick_once(gs, world, runtime);
        ++thinks;
    }
    CHECK(in_state(rt, sm::NPCState::Idle),
          "Resting is a state an NPC LEAVES: exhaustion is never permanent");
    CHECK(int(rt.sp) >= int(rt.maxSp) / 2,
          "leaving Resting means stamina actually came back");
}

void test_macro_visual_smoothing_and_snap() {
    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 12.0f, 10.0f, -1);
    auto& visual = world.reg.get<sm::ecs::VisualPos>(e);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    visual.vx = 10.0f;
    visual.vy = 10.0f;
    rt.visualSpeed = 4.0f;
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    CHECK(close_enough(visual.vx, 11.0f) && close_enough(visual.vy, 10.0f),
          "the render position glides toward the logical one at speed * dt");

    auto& p = world.reg.get<sm::ecs::Position>(e);
    p.x = 30.0f;
    p.y = 10.0f;
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    CHECK(close_enough(visual.vx, 30.0f) && close_enough(visual.vy, 10.0f),
          "a jump too far to glide SNAPS instead of sliding across the map");
    CHECK(close_enough(visual.speed, 0.0f),
          "a snapped body reports no travel speed: it did not walk there");
}

} // namespace

int main() {
    test_home_wanderer_returns_when_far();
    test_woodcutter_targets_nearest_tree();
    test_trader_targets_other_settlement();
    test_nomad_excludes_current_target();
    test_aggressive_chases_visible_player();
    test_patrol_returns_when_far_from_home();
    test_teleporter_cooldown_counts_down();
    test_wanderer_enters_wandering_state();
    test_resting_recovery_prevents_permanent_stall();
    test_macro_visual_smoothing_and_snap();
    return sm::test::report("macro_npc_ai_parity_test");
}
