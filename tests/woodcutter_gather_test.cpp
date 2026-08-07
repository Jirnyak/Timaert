// The first honest work-loop (W2b): a village woodcutter CHOPS the world and
// HAULS it home. Pinned:
//   · the chop leaves through the ONE set_tree_count door — the layer count
//     really falls and the override is recorded (a save remembers the stump);
//   · the haul rides in the woodcutter's OWN bag and lands in his HOME
//     village's universal inventory (the home-link fix: a village man works
//     for the village, not for the nearest city);
//   · CONSERVATION — wood gained by the store == wood lost by the layer,
//     and the bag is empty after delivery: nothing minted, nothing dropped.
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/econ_day.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/squad.h"
#include "macro/tree_layer.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 32;

entt::entity make_woodcutter(ecs::World& w, float x, float y,
                             int homeVillageId) {
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, x, y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Woodcutter),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeVillageId;
    rt.homeIsVillage = 1;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    const CharacterSheet sheet = make_character_sheet(
        NPCType::Woodcutter, 3, leader_sheet_seed(11u));
    refresh_leader_travel_stats(rt, sheet);
    rt.sp = rt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 11u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    reg.emplace<ecs::Health>(e, 30.0f, 30.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);
    return e;
}

void test_the_chop_is_real_and_the_haul_comes_home() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = 40;
    gs.villages.push_back(vil);

    // A little forest cell four cells east of the village.
    TreeLayer layer;
    layer.width = kMap;
    layer.height = kMap;
    layer.data.assign(std::size_t(kMap) * kMap, 0);
    layer.data[10 * kMap + 14] = 100;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);

    ecs::World w;
    const entt::entity wc = make_woodcutter(w, 10.0f, 10.0f, vil.id);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // Live a while: idle -> travel -> WORK (the chop) -> return (the haul).
    for (int i = 0; i < 400; ++i) {
        tick_macro_npc_ai(gs, w, &grid, rt, kAiTicks,
                          /*allowAutoBattle=*/true, /*pathCost=*/nullptr,
                          &layer);
    }

    const int layerLost = 100 - int(layer.at(14, 10));
    const int storeGained = gs.villages[0].inventory.count("wood");
    const int inBag =
        w.reg.get<ecs::NpcInventory>(wc).inv.count("wood");

    CHECK(layerLost > 0, "the chop really fell trees in the layer");
    CHECK(storeGained > 0, "the haul reached the village store");
    CHECK(layerLost == storeGained + inBag,
          "CONSERVATION: layer loss == store gain + what still rides the bag");
    CHECK(!gs.treeOverrides.empty(),
          "the chop left an override - a save remembers the stump");
    CHECK(gs.villages[0].inventory.count("wood") > 0
              && gs.settlements.empty(),
          "the village man hauls for the VILLAGE (no city even exists here)");
}

void test_the_farmer_works_the_field() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);

    // A field two cells east — where stamp_field_features would put one.
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.homeIsVillage = 1;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    refresh_leader_travel_stats(
        prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)));
    prt.sp = prt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 12u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    reg.emplace<ecs::Health>(e, 20.0f, 20.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 400; ++i) {
        tick_macro_npc_ai(gs, w, nullptr, rt, kAiTicks,
                          /*allowAutoBattle=*/true, nullptr, nullptr,
                          &features);
    }
    const int grain = gs.villages[0].inventory.count("grain");
    CHECK(grain > 0, "the farmer's grain reached the village store");
    CHECK(grain % kGatherPerWorkerDay == 0,
          "grain arrives in whole trip-yields - one law of labour");
}

void test_no_layer_no_chop() {
    // Without a live tree layer the behaviour must not invent wood — the
    // old walk-to-forest-and-back pantomime, unchanged.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);
    ecs::World w;
    make_woodcutter(w, 10.0f, 10.0f, vil.id);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    for (int i = 0; i < 200; ++i) {
        tick_macro_npc_ai(gs, w, &grid, rt, kAiTicks);
    }
    CHECK(gs.villages[0].inventory.count("wood") == 0,
          "no layer => no honest wood, and none minted from nothing");
}

} // namespace

int main() {
    test_the_chop_is_real_and_the_haul_comes_home();
    test_the_farmer_works_the_field();
    test_no_layer_no_chop();
    return sm::test::report("woodcutter_gather_test");
}
