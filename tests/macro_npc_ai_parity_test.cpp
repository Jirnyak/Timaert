#include "macro/npc_ai.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "macro_npc_ai_parity_test FAIL: %s\n", msg);
    return 1;
}

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
    world.reg.emplace<sm::ecs::Position>(e, x, y);
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
    sm::tick_macro_npc_ai(gs, world, treeGrid, runtime, sm::kAiTickSec);
}

bool close_enough(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

bool test_home_wanderer_returns_when_far() {
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
    if (rt.state != std::uint8_t(sm::NPCState::Returning)
        || !close_enough(rt.targetX, 50.0f)
        || !close_enough(rt.targetY, 50.0f)) {
        return fail("HomeWanderer did not target home when beyond 20 cells");
    }
    return true;
}

bool test_woodcutter_targets_nearest_tree() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.settlements.push_back(settlement(1, 20, 20));
    std::vector<sm::TreePoint> trees{{23, 20}, {80, 80}};
    sm::TreeGrid grid;
    sm::build_tree_grid(grid, trees, gs.mapW, gs.mapH, 32);

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Woodcutter, 21.0f, 20.0f, 1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 20u);
    tick_once(gs, world, runtime, &grid);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    if (rt.state != std::uint8_t(sm::NPCState::Traveling)
        || !close_enough(rt.targetX, 23.0f)
        || !close_enough(rt.targetY, 20.0f)) {
        return fail("Woodcutter did not target the nearest 30-cell tree");
    }
    return true;
}

bool test_trader_targets_other_settlement() {
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
    if (rt.state != std::uint8_t(sm::NPCState::Traveling)
        || rt.targetSettlementId != 2
        || !close_enough(rt.targetX, 40.0f)
        || !close_enough(rt.targetY, 10.0f)) {
        return fail("Trader did not pick a non-home settlement");
    }
    return true;
}

bool test_nomad_excludes_current_target() {
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

    if (rt.state != std::uint8_t(sm::NPCState::Traveling)
        || rt.targetSettlementId != 1
        || !close_enough(rt.targetX, 10.0f)
        || !close_enough(rt.targetY, 10.0f)) {
        return fail("Nomad did not exclude the current target settlement");
    }
    return true;
}

bool test_aggressive_chases_visible_player() {
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
    if (rt.state != std::uint8_t(sm::NPCState::Chasing)
        || !close_enough(rt.targetX, 12.0f)
        || !close_enough(rt.targetY, 10.0f)
        || !close_enough(p.x, 11.0f)
        || !close_enough(p.y, 10.0f)
        || rt.sp != 90
        || !close_enough(rt.visualSpeed, 2.0f)) {
        return fail("Aggressive NPC did not chase and step toward nearby player");
    }
    return true;
}

bool test_patrol_returns_when_far_from_home() {
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
    if (rt.state != std::uint8_t(sm::NPCState::Returning)
        || !close_enough(rt.targetX, 50.0f)
        || !close_enough(rt.targetY, 50.0f)
        || !close_enough(p.x, 69.0f)) {
        return fail("Patrol did not return toward home after straying beyond 12 cells");
    }
    return true;
}

bool test_teleporter_cooldown_counts_down() {
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

    if (rt.teleportCooldown != 1) {
        return fail("Teleporter did not decrement cooldown before wander logic");
    }
    return true;
}

bool test_wanderer_enters_wandering_state() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Sorceress, 30.0f, 30.0f, -1);
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 80u);
    tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    if (rt.state != std::uint8_t(sm::NPCState::Wandering)
        || (close_enough(rt.targetX, 30.0f)
            && close_enough(rt.targetY, 30.0f))) {
        return fail("Wanderer failed to pick a non-trivial wandering target");
    }
    return true;
}

bool test_resting_recovery_prevents_permanent_stall() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1,
                      sm::NPCState::Resting, 0, 0);
    auto& hp = world.reg.get<sm::ecs::Health>(e);
    hp.maxHp = 10.0f;
    hp.hp = 10.0f;

    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 90u);
    for (int i = 0; i < 10; ++i) tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    if (rt.state != std::uint8_t(sm::NPCState::Idle) || rt.sp < 10) {
        return fail("Resting NPC did not recover to idle at half stamina");
    }
    return true;
}

bool test_macro_visual_smoothing_and_snap() {
    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 12.0f, 10.0f, -1);
    auto& visual = world.reg.get<sm::ecs::VisualPos>(e);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    visual.vx = 10.0f;
    visual.vy = 10.0f;
    rt.visualSpeed = 4.0f;
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    if (!close_enough(visual.vx, 11.0f) || !close_enough(visual.vy, 10.0f)) {
        return fail("macro visual interpolation did not advance by speed * dt");
    }

    auto& p = world.reg.get<sm::ecs::Position>(e);
    p.x = 30.0f;
    p.y = 10.0f;
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    if (!close_enough(visual.vx, 30.0f) || !close_enough(visual.vy, 10.0f)
        || !close_enough(visual.speed, 0.0f)) {
        return fail("macro visual interpolation did not snap long jumps");
    }
    return true;
}

} // namespace

int main() {
    if (!test_home_wanderer_returns_when_far()) return 1;
    if (!test_woodcutter_targets_nearest_tree()) return 1;
    if (!test_trader_targets_other_settlement()) return 1;
    if (!test_nomad_excludes_current_target()) return 1;
    if (!test_aggressive_chases_visible_player()) return 1;
    if (!test_patrol_returns_when_far_from_home()) return 1;
    if (!test_teleporter_cooldown_counts_down()) return 1;
    if (!test_wanderer_enters_wandering_state()) return 1;
    if (!test_resting_recovery_prevents_permanent_stall()) return 1;
    if (!test_macro_visual_smoothing_and_snap()) return 1;

    std::printf("OK macro_npc_ai_parity_test behaviours=8 resting=ok visual=ok\n");
    return 0;
}
