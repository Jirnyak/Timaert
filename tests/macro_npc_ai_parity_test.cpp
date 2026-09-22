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
#include "macro/map_generator.h"
#include "macro/recovery.h"
#include "macro/resource_field.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

sm::Landmark settlement(int id, int x, int y) {
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = id;
    s.name = "Test";
    s.x = x;
    s.y = y;
    s.population = 1000;
    s.factionIdx = 0;
    return s;
}

entt::entity spawn_ai(sm::ecs::World& world,
                      sm::NPCType type,
                      float x,
                      float y,
                      int homeId,
                      sm::NPCState state = sm::NPCState::Idle,
                      int timer = 0,
                      int sp = 100,
                      int mapW = 128) {
    auto e = world.reg.create();
    world.reg.emplace<sm::ecs::MacroCell>(
        e, sm::ecs::cell_index(int(x), int(y), mapW));
    world.reg.emplace<sm::ecs::MacroVisual>(e, x, y, 0.0f);
    world.reg.emplace<sm::ecs::NPCKind>(e, std::uint16_t(type), std::uint16_t{0});

    sm::ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeId;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.stateTimer = std::int16_t(timer);
    rt.teleportCooldown = 0;
    rt.state = std::uint8_t(state);
    rt.visualSpeed = 0.0f;
    rt.tickAccum = 0.0f;
    world.reg.emplace<sm::ecs::MacroNpcRuntime>(e, rt);
    // The body's three bars live in one block since the pools landing; the
    // legs' bar is filled here beside the wound, not in the march runtime.
    sm::ecs::Pools pools{};
    pools.hp = pools.maxHp = 50;
    pools.sp = sp;
    pools.maxSp = 100;
    world.reg.emplace<sm::ecs::Pools>(e, pools);
    return e;
}

void tick_once(sm::GameState& gs,
               sm::ecs::World& world,
               sm::MacroNpcAiRuntime& runtime,
               const sm::TreeGrid* treeGrid = nullptr) {
    sm::MacroWorld mw{.gs = &gs, .world = &world, .treeGrid = treeGrid};
    sm::tick_macro_npc_ai(mw, runtime, sm::kAiTicks);
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
    gs.landmarks.push_back(settlement(1, 50, 50));

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
    gs.landmarks.push_back(settlement(1, 20, 20));
    // Two trees: one within reach, one across the map. The near one must win —
    // "nearest", not "first in the grid".
    std::vector<sm::TreePoint> trees{{23, 20}, {80, 80}};
    sm::TreeGrid grid;
    sm::build_tree_grid(grid, trees, gs.mapW, gs.mapH, 32);

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 21.0f, 20.0f, 1);
    {
        // Работа именуется поручением (аукцион, CANON S10): рубка = Gather
        // над строкой целей Trees; тип — лишь лист и спина.
        auto& wrt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
        wrt.squadType = std::uint8_t(sm::SquadType::Artel);
        wrt.errandObject = std::uint32_t(
            sm::gather_goal_row(sm::ResourceFieldId::Trees));
    }
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
    gs.landmarks.push_back(settlement(1, 10, 10));
    gs.landmarks.push_back(settlement(2, 40, 10));

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
    gs.landmarks.push_back(settlement(1, 10, 10));
    gs.landmarks.push_back(settlement(2, 40, 10));

    sm::ecs::World world;
    // Бродяга — ТИП СКВАДА без дома: диспетчер спрашивает тип первым, а
    // корован без дома честно сваливается в ai_nomad. На роли тела этот
    // тест стоять больше не может — роль TaxCollector осталась без машины
    // 2026-09-22, и фикстура молча перестала ехать.
    auto e = spawn_ai(world, sm::NPCType::Peasant, 40.0f, 10.0f, -1);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    rt.squadType = std::uint8_t(sm::SquadType::Caravan);
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

    sm::ecs::World world;
    // The player the pursuit rule sees: a FLAGGED squad standing on its cell
    // (подпосадка 4 — the AI asks the world for the flag holder, no scalar).
    {
        const auto pe = world.reg.create();
        world.reg.emplace<sm::ecs::PlayerTag>(pe);
        world.reg.emplace<sm::ecs::MacroCell>(
            pe, sm::ecs::cell_index(12, 10, gs.mapW));
    }
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1);
    // Pursuit asks THE hostility rule now (damage-door Inc 3), so the fixture
    // must say WHO this bandit is and WHERE the pair stands — an aggressive
    // row chases nobody it is not at war with.
    world.reg.get<sm::ecs::NPCKind>(e).factionIdx =
        std::uint16_t(sm::faction_index("bandits"));
    sm::add_player_reputation(gs, "bandits", -100);
    // ONE law of sight (owner, 2026-08-29): the private player channel is
    // dead — the bandit perceives the player's SQUAD through the same
    // SquadIndex as any other squad, at the same kSquadSightCells. So the
    // fixture spawns that squad; a bare gs.player position is invisible now.
    auto player = spawn_ai(world, sm::NPCType::Adventurer, 12.0f, 10.0f, -1);
    world.reg.get<sm::ecs::NPCKind>(player).factionIdx =
        std::uint16_t(sm::faction_index(sm::kPlayerFactionId));
    world.reg.emplace<sm::ecs::PlayerSquadTag>(player);
    // And pursuit is the one STRENGTH law (squad_threat_step): a fighter
    // closes only fights it wins with margin. The player is wounded to 10%
    // so the chase is the law's own verdict, not a leftover reflex.
    world.reg.get<sm::ecs::Pools>(player).hp = 5.0f;
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 50u);
    // The march budget is DERIVED data now (kMacroWalkCellsPerHour ×
    // kAiTickGameHours per think — 2026-08-24 recalibration): run exactly
    // enough thinks to close the two-cell gap, never pinning a literal pace.
    const float perThink =
        sm::kMacroWalkCellsPerHour * sm::kAiTickGameHours;
    const int thinksToClose = int(std::ceil(2.0f / perThink));
    for (int i = 0; i < thinksToClose; ++i) tick_once(gs, world, runtime);

    const auto& pcell = world.reg.get<sm::ecs::MacroCell>(e);
    (void)pcell;
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(in_state(rt, sm::NPCState::Chasing),
          "an Aggressive NPC that can see the player gives chase");
    CHECK(targets(rt, 12.0f, 10.0f),
          "the chase aims at where the player actually is");
    // A multi-cell march never hops OVER the player — it stops ON the
    // meeting cell, where the forced-encounter door looks.
    CHECK(sm::ecs::cell_x(pcell, 128) == 12 && sm::ecs::cell_y(pcell, 128) == 10,
          "the chase closes the two-cell gap and stops on the player");
    // The march debt is the trip's true price: two featureless cells at
    // kStaminaPerCell each, part paid in whole SP, the rest in the carry.
    const auto& chaserPools = world.reg.get<sm::ecs::Pools>(e);
    const float paid = float(100 - chaserPools.sp) - chaserPools.spCarry;
    CHECK(std::fabs(paid - 2.0f * sm::kStaminaPerCell) < 0.01f,
          "chasing pays exactly the two cells' derived march debt");
    CHECK(rt.visualSpeed > 0.0f,
          "the visual speed reports that the chase actually moved");
}

// The negative twin: the SAME aggressive row, the SAME two-cell distance —
// but the pair stands above the hostility line, so the one rule vetoes the
// chase. This is the law that killed the last private resolver (a befriended
// band used to follow the player forever while the forced-encounter door,
// reading the real rule, kept vetoing the meeting).
void test_aggressive_spares_a_friend() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    sm::ecs::World world;
    // The player the pursuit rule sees: a FLAGGED squad standing on its cell
    // (подпосадка 4 — the AI asks the world for the flag holder, no scalar).
    {
        const auto pe = world.reg.create();
        world.reg.emplace<sm::ecs::PlayerTag>(pe);
        world.reg.emplace<sm::ecs::MacroCell>(
            pe, sm::ecs::cell_index(12, 10, gs.mapW));
    }
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1);
    world.reg.get<sm::ecs::NPCKind>(e).factionIdx =
        std::uint16_t(sm::faction_index("bandits"));
    sm::add_player_reputation(gs, "bandits", 60);   // above kHostileThreshold
    // The same visible, beatable player squad as the positive twin — the
    // ONLY difference between the tests is the standing, so a broken
    // hostility check (not a broken perception) is what would turn this red.
    auto player = spawn_ai(world, sm::NPCType::Adventurer, 12.0f, 10.0f, -1);
    world.reg.get<sm::ecs::NPCKind>(player).factionIdx =
        std::uint16_t(sm::faction_index(sm::kPlayerFactionId));
    world.reg.emplace<sm::ecs::PlayerSquadTag>(player);
    world.reg.get<sm::ecs::Pools>(player).hp = 5.0f;
    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 50u);
    const float perThink =
        sm::kMacroWalkCellsPerHour * sm::kAiTickGameHours;
    const int thinks = int(std::ceil(2.0f / perThink));
    for (int i = 0; i < thinks; ++i) tick_once(gs, world, runtime);

    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    CHECK(!in_state(rt, sm::NPCState::Chasing),
          "an aggressive row does not chase a faction it is not at war with");
}

// (ПАТРУЛЬ ВЫРЕЗАН 2026-09-22 вместе со своим свидетелем: `ai_patrol`,
// значение `AIBehaviour::Patrol` и состояние `NPCState::Patrolling` снесены
// курсом «идти от минимума системы». Патруль вернётся ростером сквада-
// ландмарка после слияния гарнизона (CANON S10, наряд Б-1) — и свидетеля
// тогда писать ПО НОВОЙ ФОРМЕ, а не воскрешать этот.)

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
    // bar per game hour (kRestRegenPctPerHour = 1/8): from empty that is 4 game
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
    const auto& restedPools = world.reg.get<sm::ecs::Pools>(e);
    CHECK(restedPools.sp >= restedPools.maxSp / 2,
          "leaving Resting means stamina actually came back");
}

void test_macro_visual_smoothing_and_snap() {
    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 12.0f, 10.0f, -1);
    auto& visual = world.reg.get<sm::ecs::MacroVisual>(e);
    auto& rt = world.reg.get<sm::ecs::MacroNpcRuntime>(e);
    visual.vx = 10.0f;
    visual.vy = 10.0f;
    rt.visualSpeed = 4.0f;
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    CHECK(close_enough(visual.vx, 11.0f) && close_enough(visual.vy, 10.0f),
          "the render position glides toward the logical one at speed * dt");

    world.reg.get<sm::ecs::MacroCell>(e).idx =
        sm::ecs::cell_index(30, 10, 128);
    sm::tick_macro_npc_visuals(world, 128, 128, 0.25f);
    CHECK(close_enough(visual.vx, 30.0f) && close_enough(visual.vy, 10.0f),
          "a jump too far to glide SNAPS instead of sliding across the map");
    CHECK(close_enough(visual.speed, 0.0f),
          "a snapped body reports no travel speed: it did not walk there");
}

// ── ONE RECOVERY LAW, ONE BODY (CANON S14; owner, 2026-09-09) ────────────
//
// «Три ресурса, один закон восстановления» — and the player is a flag on an
// active squad, not a second kind of creature. So a wounded lord standing in
// camp must mend at EXACTLY the rate a wounded player standing in camp mends:
// a percent of his own bar per game hour of REST (attributes.h
// kRestRegenPctPerHour), through the same fractional carry.
//
// The two clocks are made to meet on a whole number: one think is
// kAiTickGameHours = 0.09375 h, so SIXTEEN thinks are 1.5 game hours are the
// player's 90 minutes. Nothing here restates the rate — both sides are asked
// for their own answer and the answers must be the same number.
void test_a_resting_lord_mends_at_the_players_rate() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;

    // Resting with an empty bar: the body stays in camp for the whole window
    // (Resting is left at HALF the bar, ~43 thinks away), so all sixteen
    // thinks are honest rest and none of them is a march.
    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Bandit, 10.0f, 10.0f, -1,
                      sm::NPCState::Resting, 0, 0);
    auto& hp = world.reg.get<sm::ecs::Pools>(e);
    hp.maxHp = 50;
    hp.hp = 10;
    hp.maxMp = 50;
    hp.mp = 10;

    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 90u);
    constexpr int kThinks = 16;
    for (int i = 0; i < kThinks; ++i) tick_once(gs, world, runtime);

    CHECK(in_state(world.reg.get<sm::ecs::MacroNpcRuntime>(e),
                   sm::NPCState::Resting),
          "the fixture is honest: the lord spent the whole window in camp");
    CHECK(hp.hp > 10,
          "a wounded lord at rest MENDS — a wound is not permanent for an NPC");

    // The same wound, the same bar, the same game time — asked of the
    // player's own rest path: 90 minutes of standing in camp is one call of
    // THE rest law over an identical Pools block (main.cpp's macro rest
    // branch does exactly this). Since landing 4 the parity is held by
    // construction — one function — and this test guards that the AI's
    // per-think slicing of the same hours does not drift from one whole
    // slice (the fractional carry is what makes them identical).
    sm::ecs::Pools player{};
    player.maxHp = 50;
    player.hp = 10;
    player.maxMp = 50;
    player.mp = 10;
    sm::rest_pools(player, 90.0f / 60.0f, 0);

    CHECK(hp.hp == player.hp,
          "one recovery law: lord and player mend the SAME points per hour");
    // And the bar that did not exist for a lord until this landing. A lord's
    // well refills in camp exactly as a player's does — mana is a property of
    // a BODY, not a privilege of the one the camera follows.
    CHECK(hp.mp == player.mp,
          "the lord's MANA returns at the player's rate: every body has three "
          "bars, and one law fills them");
}

// The other half of the law, and the half a careless fix deletes: rest is
// PAID FOR BY STANDING STILL. `restRate` gates the player's three bars the
// moment his legs move (a marching body never calls the rest law); the squads' words for the same
// gate are `stopped && !moved`. A marching body that mends would heal the
// world's every wound for free, and no other check in this file would notice.
void test_a_marching_body_does_not_mend() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.landmarks.push_back(settlement(1, 50, 50));

    sm::ecs::World world;
    auto e = spawn_ai(world, sm::NPCType::Peasant, 80.0f, 50.0f, 1);
    auto& hp = world.reg.get<sm::ecs::Pools>(e);
    hp.maxHp = 50;
    hp.hp = 10;
    const float startX = float(sm::ecs::cell_x(
        world.reg.get<sm::ecs::MacroCell>(e), 128));

    sm::MacroNpcAiRuntime runtime;
    sm::reset_macro_npc_ai_runtime(runtime, 11u);
    for (int i = 0; i < 16; ++i) tick_once(gs, world, runtime);

    // Not a tolerance — a precondition. If the body never walked, the wound
    // check below would pass for the wrong reason, so it fails out loud.
    CHECK(!close_enough(float(sm::ecs::cell_x(
              world.reg.get<sm::ecs::MacroCell>(e), 128)), startX),
          "the fixture is honest: the body actually MARCHED these sixteen thinks");
    CHECK(hp.hp == 10,
          "the road does not heal: a body on the move mends nothing");
}

} // namespace

// ── AI-2: свидетель «полный пул разгружается на месте» ВЫРЕЗАН 2026-09-21
// вместе с самой механикой (raise_deserter_bands): бандитов в мире больше не
// рождают. Тест, переживший свою механику, сторожил бы дефект, а не закон
// (AGENTS, закон тестов §7).

// The rotation half of the same defect: the dissolve view took any Idle crew
// at home — dead included — and paid its souls back to the landmark (or, for
// a garrison row, pushed dead records into the garrison). A dead crew is not
// a crew coming home; it is a corpse-row awaiting the drain.
void test_rotation_does_not_dissolve_the_dead() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.landmarks.push_back(settlement(1, 50, 50));
    sm::TerrainData terrain;
    terrain.width = 8;
    terrain.height = 8;
    terrain.rgba.assign(8u * 8u * 4u, 255u);
    for (std::size_t i = 0; i < 8u * 8u; ++i) terrain.rgba[i * 4u] = 180u;

    sm::ecs::World world;
    // A DEAD crew standing at its home city, Idle — the exact state the
    // dissolve used to swallow. Род взят ЖИВОЙ строкой ростера города
    // (Peasant, артель горожан): патрульная строка Guard вырезана
    // 2026-09-21, и свидетель на ней проверял бы уже не закон, а пустоту —
    // rotate_worker_squads не считает крю то, чего место не поднимает.
    const auto dead = spawn_ai(world, sm::NPCType::Peasant, 50.0f, 50.0f, 1);
    world.reg.emplace<sm::ecs::MacroSpawnId>(dead, 77u);
    auto& deadRoster = world.reg.emplace<sm::ecs::SquadRoster>(dead);
    deadRoster.squad.push(
        sm::make_soldier(std::uint16_t(sm::NPCType::Peasant), 2, 200u));
    world.reg.emplace<sm::ecs::Dead>(dead);

    const int popBefore = gs.landmarks[0].population;
    const int garrisonBefore = gs.landmarks[0].garrison.souls();
    sm::MacroWorld mw{.gs = &gs, .world = &world, .terrain = &terrain};
    sm::rotate_worker_squads(mw, /*day=*/3);

    CHECK(gs.landmarks[0].population == popBefore,
          "a dead crew's souls never return to the population");
    CHECK(gs.landmarks[0].garrison.souls() == garrisonBefore,
          "and dead records never march into the garrison");
    CHECK(world.reg.valid(dead),
          "the corpse-row is the drain's business, not the rotation's");

    // Negative control: ЖИВЫЕ артели той же строки этот же проход РАСПУСКАЕТ
    // — значит исключение выше про СМЕРТЬ, а не про мёртвую дверь. Город
    // держит ОДНУ крестьянскую строку, поэтому из двух стоящих дома артелей
    // одна занимает её, а лишняя распускается (суд границы, S19.2) —
    // наблюдаем это прямо по смерти сущности, а не по арифметике населения,
    // которую та же граница двигает ещё и набором.
    for (std::uint32_t i = 0; i < 2u; ++i) {
        const auto alive =
            spawn_ai(world, sm::NPCType::Peasant, 50.0f, 50.0f, 1);
        world.reg.emplace<sm::ecs::MacroSpawnId>(alive, 78u + i);
        auto& aliveRoster = world.reg.emplace<sm::ecs::SquadRoster>(alive);
        aliveRoster.squad.push(
            sm::make_soldier(std::uint16_t(sm::NPCType::Peasant), 2,
                             201u + i));
    }
    sm::rotate_worker_squads(mw, /*day=*/33);   // граница сезона
    int livingLeft = 0;
    for (auto [e2, k2] : world.reg.view<sm::ecs::NPCKind>().each()) {
        if (k2.type == std::uint16_t(sm::NPCType::Peasant)
            && !world.reg.all_of<sm::ecs::Dead>(e2))
            ++livingLeft;
    }
    CHECK(livingLeft < 2,
          "negative control: лишняя ЖИВАЯ артель распущена тем же проходом");
    CHECK(world.reg.valid(dead),
          "и труп пережил границу — растворение его по-прежнему не трогает");
}

int main() {
    test_home_wanderer_returns_when_far();
    test_woodcutter_targets_nearest_tree();
    test_trader_targets_other_settlement();
    test_nomad_excludes_current_target();
    test_aggressive_chases_visible_player();
    test_aggressive_spares_a_friend();
    test_teleporter_cooldown_counts_down();
    test_wanderer_enters_wandering_state();
    test_resting_recovery_prevents_permanent_stall();
    test_a_resting_lord_mends_at_the_players_rate();
    test_a_marching_body_does_not_mend();
    test_macro_visual_smoothing_and_snap();
    test_rotation_does_not_dissolve_the_dead();
    return sm::test::report("macro_npc_ai_parity_test");
}
