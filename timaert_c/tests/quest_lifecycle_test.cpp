#include "content/quests/procedural.h"
#include "content/plot/intro.h"
#include "events/effect_applicator.h"
#include "events/event_bus.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
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
    for (const auto& completed : p.completedQuestIds) {
        if (completed == id) {
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
    const sm::GameEvent* levelUp = bus.find(sm::EventTag::PlayerLevelUp);
    if (!levelUp || levelUp->ix != 2) {
        return fail("PlayerLevelUp did not carry the new level");
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

bool test_level_up_show_dialog_node() {
    sm::PlayerState player{};
    player.levelData = sm::default_level_data();
    player.combatStats = sm::calculate_combat_stats(player.attributes, player.skills);

    sm::EventBus bus;
    sm::LogicNodeEngine logic;
    sm::register_builtin_nodes(logic);

    std::size_t applied = 0;
    sm::GameEvent xp{sm::EventTag::ApplyEffect};
    xp.s1 = "grant_xp";
    xp.ix = player.levelData.expToNext + 5;
    bus.emit(xp);

    apply_pending(bus, player, applied);
    bus.flush(0, 6);
    logic.tick(bus, player);

    const sm::GameEvent* dialog = bus.find(sm::EventTag::ShowDialog);
    if (!dialog) {
        return fail("PlayerLevelUp did not emit ShowDialog through logic node");
    }
    if (dialog->s1 != "Level Up!"
        || dialog->s2.find("level 2") == std::string::npos
        || dialog->ix != 1) {
        return fail("ShowDialog level-up payload does not match TS node");
    }

    bus.flush(0, 7);
    logic.tick(bus, player);
    if (bus.has_tag(sm::EventTag::ShowDialog)) {
        return fail("level-up ShowDialog node fired without a new PlayerLevelUp");
    }
    return true;
}

bool test_intro_show_story_node() {
    const sm::content::StoryDef& story = sm::content::intro_story();
    if (std::string(story.id) != "intro"
        || std::string(story.sourceNodeId) != "intro_main"
        || story.phaseCount != 4) {
        return fail("intro story table identity does not match TS intro");
    }
    if (story.phases[0].kind != sm::content::StoryPhaseKind::Slides
        || story.phases[0].slideCount != 9) {
        return fail("intro story slide phase does not match TS intro");
    }
    if (story.phases[1].kind != sm::content::StoryPhaseKind::Choice
        || std::string(story.phases[1].id) != "sex"
        || story.phases[1].choiceCount != 2) {
        return fail("intro story sex choice phase does not match TS intro");
    }
    if (story.phases[2].kind != sm::content::StoryPhaseKind::Input
        || std::string(story.phases[2].id) != "name"
        || story.phases[2].maxLength != 24) {
        return fail("intro story input phase does not match TS intro");
    }
    if (story.phases[3].kind != sm::content::StoryPhaseKind::Choice
        || std::string(story.phases[3].id) != "realm"
        || story.phases[3].choiceCount != 3) {
        return fail("intro story realm choice phase does not match TS intro");
    }

    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;
    sm::content::register_intro_story_nodes(logic);

    logic.tick(bus, player);

    const sm::GameEvent* event = bus.find(sm::EventTag::ShowStory);
    if (!event) {
        return fail("intro_main did not emit ShowStory");
    }
    if (event->s1 != "intro_main"
        || event->s2 != "intro"
        || event->ix != 4
        || event->iy != 9
        || event->a != 5
        || event->b != 24) {
        return fail("ShowStory flat payload does not match TS intro shape");
    }

    bus.flush(0, 6);
    logic.tick(bus, player);
    if (bus.has_tag(sm::EventTag::ShowStory)) {
        return fail("intro_main ShowStory fired more than once without reactivation");
    }
    return true;
}

bool test_item_delivery_direct_path() {
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
    if (gs.player.inventory.count("mat_wood") != 1) {
        return fail("delivery did not remove delivered items");
    }
    if (gs.player.inventory.count("misc_gem") != 2) {
        return fail("item reward did not grant item reward");
    }

    std::size_t applied = 0;
    apply_pending(bus, gs.player, applied);
    if (gs.player.inventory.count("mat_wood") != 1
        || gs.player.inventory.count("misc_gem") != 2) {
        return fail("event application duplicated direct inventory mutation");
    }
    if (!contains_completed_id(gs.player, q.id)) {
        return fail("delivery completion was not applied");
    }
    return true;
}

bool test_find_location_player_move_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;

    sm::Quest q{};
    q.id = "q_find_location";
    q.title = "Find Location";
    q.description = "Test PlayerMove objective payload";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::FindLocation;
    objective.cellX = 22;
    objective.cellY = 33;
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    sm::GameEvent move{sm::EventTag::PlayerMove};
    move.ix = 22;
    move.iy = 33;
    bus.emit(move);
    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    if (!active.empty()) {
        return fail("PlayerMove did not complete FindLocation");
    }
    return true;
}

bool test_abandon_emits_and_removes() {
    sm::Quest q{};
    q.id = "q_abandon_test";
    q.title = "Abandon Test";
    q.description = "Test QuestEngine::abandon";
    q.category = sm::QuestCategory::Procedural;

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);

    engine.abandon(active, q.id, bus);
    if (!active.empty()) {
        return fail("abandon did not remove quest from active list");
    }
    const sm::GameEvent* ev = bus.find(sm::EventTag::QuestAbandoned);
    if (!ev || ev->s1 != q.id || ev->a != std::uint32_t(sm::quest_id_key(q.id))) {
        return fail("abandon did not emit QuestAbandoned payload");
    }

    engine.abandon(active, q.id, bus);
    if (count_tag(bus, sm::EventTag::QuestAbandoned) != 1) {
        return fail("abandon emitted duplicate event for missing quest");
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
    if (!test_level_up_show_dialog_node()) return 1;
    if (!test_intro_show_story_node()) return 1;
    if (!test_item_delivery_direct_path()) return 1;
    if (!test_find_location_player_move_objective()) return 1;
    if (!test_abandon_emits_and_removes()) return 1;

    std::printf("OK quest_lifecycle_test id=%s hours=%d reward_gold=%d completed=%zu xp_level=ok level_dialog=ok intro_story=ok item_direct=ok find_move=ok abandon=ok\n",
                selected.id.c_str(),
                timeAdvance.ix,
                rewardGold,
                gs.player.completedQuestIds.size());
    return 0;
}
