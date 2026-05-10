#include "content/quests/procedural.h"
#include "events/effect_applicator.h"
#include "events/event_bus.h"
#include "events/quests/quest_engine.h"

#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

bool fail(const char* msg) {
    std::fprintf(stderr, "FAIL quest_lifecycle_test: %s\n", msg);
    return false;
}

const sm::Quest* find_wait_quest(const std::vector<sm::Quest>& quests) {
    for (const auto& q : quests) {
        for (const auto& obj : q.objectives) {
            if (obj.kind == sm::ObjectiveKind::WaitAt) {
                return &q;
            }
        }
    }
    return nullptr;
}

int gold_reward(const sm::Quest& q) {
    int total = 0;
    for (const auto& r : q.rewards) {
        if (r.kind == sm::RewardKind::Gold) {
            total += r.amount;
        }
    }
    return total;
}

bool contains_completed_id(const sm::PlayerState& p, const std::string& id) {
    const int key = sm::quest_id_key(id);
    for (int completed : p.completedQuestIds) {
        if (completed == key) {
            return true;
        }
    }
    return false;
}

void apply_pending(sm::EventBus& bus, sm::PlayerState& player, std::size_t& applied) {
    while (true) {
        const auto& events = bus.tick_events();
        if (applied >= events.size()) return;

        const std::size_t begin = applied;
        const std::size_t end = events.size();
        const sm::LevelData before = player.levelData;
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        sm::apply_events(pending, player);
        const sm::LevelData after = player.levelData;
        applied = end;
        sm::queue_player_level_up_if_needed(bus, pending, before, after);
    }
}

int count_tag(const sm::EventBus& bus, sm::EventTag tag) {
    int count = 0;
    for (const auto& ev : bus.tick_events()) {
        if (ev.tag == tag) ++count;
    }
    return count;
}

bool test_xp_level_up_producer() {
    sm::PlayerState player{};
    player.levelData = sm::default_level_data();
    player.combatStats = sm::calculate_combat_stats(player.attributes, player.skills);

    sm::EventBus bus;
    std::size_t applied = 0;
    sm::GameEvent xp1{sm::EventTag::ApplyEffect};
    xp1.s1 = "grant_xp";
    xp1.ix = player.levelData.expToNext - 10;
    bus.emit(xp1);
    sm::GameEvent xp2{sm::EventTag::ApplyEffect};
    xp2.s1 = "grant_xp";
    xp2.ix = 20;
    bus.emit(xp2);

    apply_pending(bus, player, applied);
    if (count_tag(bus, sm::EventTag::PlayerLevelUp) != 1) {
        return fail("XP threshold did not queue exactly one PlayerLevelUp");
    }
    if (player.levelData.level != 2 || player.levelData.exp != 10) {
        return fail("PlayerLevelUp did not apply the expected level state");
    }
    apply_pending(bus, player, applied);
    if (count_tag(bus, sm::EventTag::PlayerLevelUp) != 1) {
        return fail("second apply pass queued duplicate PlayerLevelUp");
    }
    return true;
}

bool test_item_delivery_event_path() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;
    gs.player.inventory.add("mat_wood", 3);

    sm::Settlement settlement{};
    settlement.id = 7;
    settlement.name = "Test Anchorage";
    settlement.x = 12;
    settlement.y = 18;
    settlement.population = 1000;
    settlement.mood = sm::SettlementMood::Stable;
    settlement.kingdomIdx = 0;
    settlement.economy = "trade";
    gs.settlements.push_back(settlement);

    sm::Quest q{};
    q.id = "q_delivery_item";
    q.title = "Deliver Materials";
    q.description = "Test delivery";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::DeliverItems;
    objective.itemId = "mat_wood";
    objective.quantity = 2;
    objective.targetSettlementId = settlement.id;
    q.objectives.push_back(objective);
    sm::Reward reward{};
    reward.kind = sm::RewardKind::Item;
    reward.itemId = "misc_gem";
    reward.amount = 2;
    q.rewards.push_back(reward);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    engine.tick(active, bus, gs);

    if (!active.empty()) {
        return fail("delivery quest did not complete from inventory condition");
    }
    if (gs.player.inventory.count("mat_wood") != 3
        || gs.player.inventory.count("misc_gem") != 0) {
        return fail("delivery mutated inventory before effect application");
    }

    std::size_t applied = 0;
    apply_pending(bus, gs.player, applied);
    if (gs.player.inventory.count("mat_wood") != 1) {
        return fail("delivery consume_item effect did not remove delivered items");
    }
    if (gs.player.inventory.count("misc_gem") != 2) {
        return fail("item reward effect did not grant item reward");
    }
    if (!contains_completed_id(gs.player, q.id)) {
        return fail("delivery completion was not applied");
    }
    return true;
}

bool test_world_cell_change_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;

    sm::Quest q{};
    q.id = "q_interact_cell";
    q.title = "Enter Cell";
    q.description = "Test world cell producer payload";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::InteractCell;
    objective.ix = 22;
    objective.iy = 33;
    objective.action = "enter_cell";
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    bus.emit(sm::make_world_cell_change_event(22, 33, "enter_cell"));
    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    if (!active.empty()) {
        return fail("WorldCellChange enter_cell did not complete InteractCell");
    }
    return true;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.worldSeed = 0x5eed1234u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;
    gs.player.gold = 100;

    sm::Settlement settlement{};
    settlement.id = 7;
    settlement.name = "Test Anchorage";
    settlement.x = 12;
    settlement.y = 18;
    settlement.population = 1000;
    settlement.mood = sm::SettlementMood::Stable;
    settlement.kingdomIdx = 0;
    settlement.economy = "trade";
    gs.settlements.push_back(settlement);

    sm::Quest selected{};
    bool found = false;
    for (int day = 0; day < 256 && !found; ++day) {
        gs.worldTime.day = day;
        const auto generated =
            sm::generate_quests_for_settlement(settlement, gs, gs.worldSeed);
        if (const sm::Quest* q = find_wait_quest(generated)) {
            selected = *q;
            found = true;
        }
    }
    if (!found) {
        return fail("procedural generator did not produce a WaitAt quest") ? 0 : 1;
    }
    if (selected.objectives.empty()
        || selected.objectives.front().kind != sm::ObjectiveKind::WaitAt
        || selected.objectives.front().hoursRequired <= 0) {
        return fail("selected quest has invalid WaitAt objective") ? 0 : 1;
    }

    const int startGold = gs.player.gold;
    const int rewardGold = gold_reward(selected);
    if (rewardGold <= 0) {
        return fail("selected generated WaitAt quest has no gold reward") ? 0 : 1;
    }

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;

    engine.accept(active, selected, gs.player, bus);
    if (active.size() != 1) {
        return fail("accept did not add quest to active list") ? 0 : 1;
    }
    if (!bus.has_tag(sm::EventTag::QuestAccepted)) {
        return fail("accept did not emit QuestAccepted") ? 0 : 1;
    }
    if (gs.player.gold != startGold) {
        return fail("accept applied reward before completion") ? 0 : 1;
    }

    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    sm::apply_events(bus.tick_events(), gs.player);
    if (active.size() != 1) {
        return fail("quest completed without a TimeAdvance event") ? 0 : 1;
    }

    sm::GameEvent timeAdvance{sm::EventTag::TimeAdvance};
    timeAdvance.ix = active.front().objectives.front().hoursRequired;
    bus.emit(timeAdvance);
    bus.flush(gs.worldTime.day, gs.worldTime.hour + timeAdvance.ix);

    engine.tick(active, bus, gs);
    if (!active.empty()) {
        return fail("TimeAdvance did not complete generated WaitAt quest") ? 0 : 1;
    }
    if (!bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("completion did not emit QuestCompleted") ? 0 : 1;
    }

    sm::apply_events(bus.tick_events(), gs.player);
    if (!contains_completed_id(gs.player, selected.id)) {
        return fail("QuestCompleted was not applied to player completion state") ? 0 : 1;
    }
    if (gs.player.gold != startGold + rewardGold) {
        return fail("gold reward was not applied exactly once") ? 0 : 1;
    }

    bus.flush(gs.worldTime.day, gs.worldTime.hour + timeAdvance.ix);
    sm::apply_events(bus.tick_events(), gs.player);
    if (gs.player.gold != startGold + rewardGold) {
        return fail("empty post-flush tick reapplied reward") ? 0 : 1;
    }

    if (!test_xp_level_up_producer()) return 1;
    if (!test_item_delivery_event_path()) return 1;
    if (!test_world_cell_change_objective()) return 1;

    std::printf("OK quest_lifecycle_test id=%s hours=%d reward_gold=%d completed=%zu xp_level=ok item_path=ok world_cell=ok\n",
                selected.id.c_str(),
                timeAdvance.ix,
                rewardGold,
                gs.player.completedQuestIds.size());
    return 0;
}
