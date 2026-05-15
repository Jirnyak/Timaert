#include "content/quests/procedural.h"
#include "content/plot/chapter_1.h"
#include "content/plot/encounters.h"
#include "content/plot/intro.h"
#include "events/effect_applicator.h"
#include "events/event_bus.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
#include "events/quests/quest_engine.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

bool fail(const char* msg) {
    std::fprintf(stderr, "FAIL quest_lifecycle_test: %s\n", msg);
    return false;
}

const sm::Quest* find_delivery_quest(const std::vector<sm::Quest>& quests,
                                     const char* itemId) {
    for (const auto& q : quests) {
        if (q.id.rfind("q_proc_deliver_", 0) != 0) continue;
        for (const auto& obj : q.objectives) {
            if (obj.kind == sm::ObjectiveKind::DeliverItems
                && obj.itemId == itemId) {
                return &q;
            }
        }
    }
    return nullptr;
}

const sm::Quest* find_wait_quest(const std::vector<sm::Quest>& quests) {
    for (const auto& q : quests) {
        for (const auto& obj : q.objectives) {
            if (obj.kind == sm::ObjectiveKind::WaitAt) return &q;
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

bool contains_failed_id(const sm::PlayerState& p, const std::string& id) {
    for (const auto& failed : p.failedQuestIds) {
        if (failed == id) {
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

bool test_event_bus_contract_surface() {
    sm::EventBus bus;
    if (bus.tick() != 0
        || bus.subscription_count() != 0
        || !bus.tick_events().empty()
        || !bus.last_tick_events().empty()
        || !bus.history().empty()) {
        return fail("EventBus did not start empty");
    }

    int customSeen = 0;
    int customSum = 0;
    const std::uint32_t subId = bus.on(sm::EventTag::Custom, [&](const sm::GameEvent& ev) {
        ++customSeen;
        customSum += ev.ix;
    });
    if (subId == 0
        || bus.subscription_count() != 1
        || !bus.has_subscribers(sm::EventTag::Custom)
        || bus.has_subscribers(sm::EventTag::PlayerMove)) {
        return fail("EventBus subscription state is incorrect");
    }

    sm::GameEvent first{sm::EventTag::Custom};
    first.s1 = "first";
    first.ix = 1;
    sm::GameEvent move{sm::EventTag::PlayerMove};
    move.ix = 3;
    sm::GameEvent second{sm::EventTag::Custom};
    second.s1 = "second";
    second.ix = 2;
    const std::vector<sm::GameEvent> batch{first, move, second};
    bus.emit_all(batch);

    if (customSeen != 2 || customSum != 3) {
        return fail("EventBus listeners did not receive emit_all events");
    }
    if (bus.tick_events().size() != 3 || !bus.has_tag(sm::EventTag::Custom)) {
        return fail("EventBus tick buffer did not retain emitted events");
    }
    const sm::GameEvent* found = bus.find(sm::EventTag::Custom);
    if (!found || found->s1 != "first" || found->ix != 1) {
        return fail("EventBus find did not return the first matching tick event");
    }
    const auto matches = bus.find_all(sm::EventTag::Custom);
    if (matches.size() != 2
        || matches[0]->s1 != "first"
        || matches[1]->s1 != "second") {
        return fail("EventBus find_all order does not match tick order");
    }

    bus.flush(4, 9);
    if (bus.tick() != 1
        || !bus.tick_events().empty()
        || bus.last_tick_events().size() != 3
        || bus.history().size() != 3) {
        return fail("EventBus flush did not promote tick events to last/history");
    }
    const auto customHistory = bus.query_history(sm::EventTag::Custom, 10);
    if (customHistory.size() != 2
        || customHistory[0].event.s1 != "second"
        || customHistory[0].tick != 0
        || customHistory[0].day != 4
        || customHistory[0].hour != 9
        || customHistory[1].event.s1 != "first") {
        return fail("EventBus query_history did not return newest-first entries");
    }

    bus.unsubscribe(subId);
    if (bus.subscription_count() != 0
        || bus.has_subscribers(sm::EventTag::Custom)) {
        return fail("EventBus unsubscribe did not remove listener");
    }
    sm::GameEvent silent{sm::EventTag::Custom};
    silent.s1 = "silent";
    silent.ix = 7;
    bus.emit(silent);
    if (customSeen != 2 || customSum != 3) {
        return fail("EventBus unsubscribed listener still received events");
    }

    bus.flush(4, 10);
    const auto limitedHistory = bus.query_history(sm::EventTag::Custom, 2);
    if (limitedHistory.size() != 2
        || limitedHistory[0].event.s1 != "silent"
        || limitedHistory[0].tick != 1
        || limitedHistory[1].event.s1 != "second") {
        return fail("EventBus query_history limit/newest order is incorrect");
    }
    bus.trim_history(2);
    if (bus.history().size() != 2) {
        return fail("EventBus trim_history did not keep the newest entries");
    }
    const auto trimmedHistory = bus.query_history(sm::EventTag::Custom, 10);
    if (trimmedHistory.size() != 2
        || trimmedHistory[0].event.s1 != "silent"
        || trimmedHistory[1].event.s1 != "second") {
        return fail("EventBus trim_history changed newest-first query semantics");
    }

    sm::EventBus mutationBus;
    int selfSeen = 0;
    int lateSeen = 0;
    std::uint32_t selfSubId = 0;
    selfSubId = mutationBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++selfSeen;
        mutationBus.unsubscribe(selfSubId);
        mutationBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
            ++lateSeen;
        });
    });
    mutationBus.emit(first);
    if (selfSeen != 1
        || lateSeen != 0
        || mutationBus.subscription_count() != 1) {
        return fail("EventBus did not defer listener mutation until after emit");
    }
    mutationBus.emit(second);
    if (selfSeen != 1
        || lateSeen != 1
        || mutationBus.subscription_count() != 1) {
        return fail("EventBus deferred listener was not active on the next emit");
    }

    sm::EventBus resetBus;
    int resetSeen = 0;
    resetBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++resetSeen;
        resetBus.reset();
    });
    resetBus.emit(first);
    if (resetSeen != 1
        || resetBus.tick() != 0
        || resetBus.subscription_count() != 0
        || !resetBus.tick_events().empty()
        || !resetBus.last_tick_events().empty()
        || !resetBus.history().empty()) {
        return fail("EventBus reset during listener dispatch did not leave clean state");
    }

    bus.reset();
    if (bus.tick() != 0
        || bus.subscription_count() != 0
        || !bus.tick_events().empty()
        || !bus.last_tick_events().empty()
        || !bus.history().empty()) {
        return fail("EventBus reset did not clear public state");
    }
    return true;
}

bool test_ts_quest_tag_aliases() {
    if (sm::EventTag::QuestAccepted != sm::EventTag::QuestStart
        || sm::EventTag::QuestObjectiveProgress != sm::EventTag::QuestUpdate
        || sm::EventTag::QuestCompleted != sm::EventTag::QuestComplete
        || sm::EventTag::QuestFailed != sm::EventTag::QuestFail) {
        return fail("legacy quest tags do not alias TS quest tags");
    }

    sm::Quest q{};
    q.id = "q_alias";
    q.title = "Alias";
    sm::EventBus bus;
    sm::QuestEngine engine;
    sm::PlayerState player{};
    std::vector<sm::Quest> active;
    engine.accept(active, q, player, bus);
    if (!bus.has_tag(sm::EventTag::QuestStart)
        || !bus.has_tag(sm::EventTag::QuestAccepted)) {
        return fail("QuestEngine::accept did not emit TS QuestStart alias");
    }
    return true;
}

bool test_effect_applicator_ts_verbs() {
    sm::PlayerState player{};
    player.gold = 10;
    player.levelData = sm::default_level_data();
    player.levelData.exp = 0;
    player.levelData.expToNext = 100;
    player.combatStats.currentHp = 20;
    player.combatStats.maxHp = 50;
    player.combatStats.currentMp = 5;
    player.combatStats.maxMp = 30;
    player.combatStats.currentSp = 20;
    player.combatStats.maxSp = 40;

    std::vector<sm::GameEvent> events;
    sm::GameEvent gold{sm::EventTag::PlayerGoldChange};
    gold.ix = 5;
    events.push_back(gold);

    sm::GameEvent heal{sm::EventTag::ApplyEffect};
    heal.s1 = "heal_hp";
    heal.ix = 10;
    events.push_back(heal);

    sm::GameEvent restoreHp{sm::EventTag::ApplyEffect};
    restoreHp.s1 = "restore_hp";
    restoreHp.ix = 99;
    events.push_back(restoreHp);

    sm::GameEvent damage{sm::EventTag::ApplyEffect};
    damage.s1 = "damage_hp";
    damage.ix = 17;
    events.push_back(damage);

    sm::GameEvent restoreMp{sm::EventTag::ApplyEffect};
    restoreMp.s1 = "restore_mp";
    restoreMp.ix = 20;
    events.push_back(restoreMp);

    sm::GameEvent restoreSp{sm::EventTag::ApplyEffect};
    restoreSp.s1 = "restore_sp";
    restoreSp.ix = 15;
    events.push_back(restoreSp);

    sm::GameEvent drainSp{sm::EventTag::ApplyEffect};
    drainSp.s1 = "drain_sp";
    drainSp.ix = 9;
    events.push_back(drainSp);

    sm::GameEvent xp{sm::EventTag::ApplyEffect};
    xp.s1 = "grant_xp";
    xp.ix = 42;
    events.push_back(xp);

    sm::GameEvent unknown{sm::EventTag::ApplyEffect};
    unknown.s1 = "unknown_effect";
    unknown.ix = 999;
    events.push_back(unknown);

    sm::GameEvent reputation{sm::EventTag::ReputationChange};
    reputation.s1 = "guild";
    reputation.ix = 3;
    events.push_back(reputation);

    sm::GameEvent codex{sm::EventTag::CodexUnlock};
    codex.s1 = "entry.alpha";
    events.push_back(codex);
    events.push_back(codex);

    sm::GameEvent complete{sm::EventTag::QuestComplete};
    complete.s1 = "q_complete";
    events.push_back(complete);

    sm::GameEvent failQuest{sm::EventTag::QuestFail};
    failQuest.s1 = "q_fail";
    events.push_back(failQuest);

    sm::apply_events(events, player);

    if (player.gold != 15) {
        return fail("PlayerGoldChange did not mutate gold like TS");
    }
    if (player.combatStats.currentHp != 33
        || player.combatStats.currentMp != 25
        || player.combatStats.currentSp != 26) {
        return fail("ApplyEffect TS hp/mp/sp verbs produced wrong combat stats");
    }
    if (player.levelData.exp != 42 || player.levelData.level != 1) {
        return fail("grant_xp did not apply XP without direct level mutation");
    }
    const auto repIt = player.reputation.find("guild");
    if (repIt == player.reputation.end() || repIt->second != 3) {
        return fail("ReputationChange did not mutate reputation like TS");
    }
    if (player.codexUnlocked.size() != 1 || player.codexUnlocked[0] != "entry.alpha") {
        return fail("CodexUnlock did not deduplicate entry like TS");
    }
    if (!contains_completed_id(player, "q_complete")
        || !contains_failed_id(player, "q_fail")
        || contains_completed_id(player, "q_fail")) {
        return fail("quest completion/failure ledgers were not separated");
    }
    return true;
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

bool test_settlement_show_dialog_node() {
    const auto run_case = [](sm::EventTag tag, const char* name) -> bool {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;
        sm::register_builtin_nodes(logic);

        sm::GameEvent visit{tag};
        visit.s1 = name;
        bus.emit(visit);
        bus.flush(1, 8);
        logic.tick(bus, player);

        const sm::GameEvent* dialog = bus.find(sm::EventTag::ShowDialog);
        if (!dialog) {
            return fail("settlement enter event did not emit ShowDialog through sys_settlement");
        }
        const std::string expectedTitle = std::string("Welcome to ") + name;
        if (dialog->s1 != expectedTitle
            || dialog->s2.find("The gates open before you") == std::string::npos
            || dialog->ix != 1) {
            return fail("sys_settlement ShowDialog payload does not match TS node");
        }
        return true;
    };

    if (!run_case(sm::EventTag::SettlementVisit, "Round City")) {
        return false;
    }
    if (!run_case(sm::EventTag::PlayerEnterSettlement, "TS City")) {
        return false;
    }
    {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;
        sm::register_builtin_nodes(logic);

        sm::GameEvent leave{sm::EventTag::PlayerLeaveSettlement};
        leave.s1 = "TS City";
        bus.emit(leave);
        bus.flush(1, 9);
        logic.tick(bus, player);
        if (bus.has_tag(sm::EventTag::ShowDialog)) {
            return fail("PlayerLeaveSettlement must not trigger sys_settlement dialog");
        }
    }
    return true;
}

bool test_logic_node_add_registers_inactive() {
    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;

    sm::LogicNode node{};
    node.id = "inactive";
    node.label = "Inactive";
    node.effect = [](sm::NodeContext& ctx) {
        sm::GameEvent ev{sm::EventTag::Custom};
        ev.s1 = "inactive";
        ctx.bus->emit(ev);
    };
    logic.add(std::move(node));

    if (logic.node_count() != 1 || logic.active_count() != 0) {
        return fail("LogicNodeEngine add did not register inactive node");
    }
    logic.tick(bus, player);
    if (bus.has_tag(sm::EventTag::Custom)) {
        return fail("inactive registered node fired before activate");
    }

    logic.activate("inactive");
    logic.tick(bus, player);
    const sm::GameEvent* ev = bus.find(sm::EventTag::Custom);
    if (!ev || ev->s1 != "inactive") {
        return fail("activated registered node did not fire");
    }
    return true;
}

bool test_logic_node_pending_ids_survive_node_add() {
    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;

    sm::LogicNode mutator{};
    mutator.id = "mutator";
    mutator.label = "Mutator";
    mutator.effect = [](sm::NodeContext& ctx) {
        for (int i = 0; i < 96; ++i) {
            sm::LogicNode child{};
            child.id = "child_" + std::to_string(i);
            child.label = "Child";
            ctx.add_node(std::move(child));
        }
    };
    logic.add(std::move(mutator));
    logic.activate("mutator");

    constexpr int kObserverCount = 8;
    for (int i = 0; i < kObserverCount; ++i) {
        const std::string id = "observer_" + std::to_string(i);
        sm::LogicNode observer{};
        observer.id = id;
        observer.label = "Observer";
        observer.effect = [](sm::NodeContext& ctx) {
            sm::GameEvent ev{sm::EventTag::Custom};
            ev.s1 = "observer";
            ctx.bus->emit(ev);
        };
        logic.add(std::move(observer));
        logic.activate(id);
    }

    logic.tick(bus, player);

    int fired = 0;
    for (const auto& ev : bus.tick_events()) {
        if (ev.tag == sm::EventTag::Custom && ev.s1 == "observer") {
            ++fired;
        }
    }
    if (fired != kObserverCount) {
        return fail("LogicNodeEngine skipped pending nodes after add_node rehash");
    }
    if (!logic.is_consistent()) {
        return fail("LogicNodeEngine active set became inconsistent after add_node");
    }
    if (logic.active_count() != 0) {
        return fail("add_node activated dynamically registered nodes");
    }
    return true;
}

bool test_logic_node_effect_can_remove_self() {
    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;

    sm::LogicNode selfRemove{};
    selfRemove.id = "self_remove";
    selfRemove.label = "Self Remove";
    selfRemove.effect = [](sm::NodeContext& ctx) {
        sm::GameEvent ev{sm::EventTag::Custom};
        ev.s1 = "self_remove";
        ctx.bus->emit(ev);
        ctx.remove_node("self_remove");
    };
    logic.add(std::move(selfRemove));
    logic.activate("self_remove");

    logic.tick(bus, player);

    const sm::GameEvent* ev = bus.find(sm::EventTag::Custom);
    if (!ev || ev->s1 != "self_remove") {
        return fail("LogicNodeEngine self-removing node did not fire");
    }
    if (logic.node_count() != 0 || logic.active_count() != 0) {
        return fail("LogicNodeEngine self-removing node stayed registered");
    }
    if (!logic.is_consistent()) {
        return fail("LogicNodeEngine inconsistent after self-removing node");
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
    if (logic.node_count() != 2 || logic.active_count() != 1) {
        return fail("plot node registration did not match TS plot index");
    }

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

    logic.activate(sm::content::kChapter1NodeId);
    if (logic.active_count() != 1) {
        return fail("chapter 1 placeholder did not activate after intro result");
    }
    bus.flush(0, 7);
    logic.tick(bus, player);
    if (!logic.is_consistent() || logic.active_count() != 1) {
        return fail("chapter 1 placeholder did not remain dormant and active");
    }
    if (!bus.tick_events().empty()) {
        return fail("chapter 1 placeholder emitted events despite false TS condition");
    }
    return true;
}

bool test_encounter_table_shape() {
    const auto& table = sm::content::encounters();
    if (table.size() != 15) {
        return fail("encounter table count does not match TS buildEncounterTable");
    }

    const auto& hidden = table[0];
    if (hidden.title != "Hidden Cache"
        || hidden.choices.size() != 2
        || hidden.choices[0].effects.size() != 1
        || hidden.choices[0].effects[0].tag != sm::EventTag::PlayerGoldChange
        || hidden.choices[0].effects[0].ix < 15
        || hidden.choices[0].effects[0].ix > 44) {
        return fail("Hidden Cache encounter does not match TS gold branch");
    }

    const auto& campfire = table[2];
    if (campfire.title != "Abandoned Campfire"
        || campfire.choices.size() != 2
        || campfire.choices[0].effects.size() != 2
        || campfire.choices[0].effects[0].s1 != "restore_sp"
        || campfire.choices[0].effects[1].s1 != "heal_hp"
        || campfire.choices[1].effects.size() != 1
        || campfire.choices[1].effects[0].tag != sm::EventTag::PlayerGoldChange
        || (campfire.choices[1].effects[0].ix != 0
            && campfire.choices[1].effects[0].ix != 25)) {
        return fail("Abandoned Campfire encounter does not match TS branches");
    }

    const auto& merchant = table[3];
    if (merchant.choices.size() != 3
        || merchant.choices[1].effects.size() != 1
        || merchant.choices[1].effects[0].tag != sm::EventTag::BattleStart
        || merchant.choices[1].effects[0].s1 != "Angry Merchant"
        || merchant.choices[1].effects[0].s2 != "merchant"
        || merchant.choices[1].effects[0].ix != 3) {
        return fail("Traveling Merchant battle branch does not match TS");
    }

    const auto& shrine = table[11];
    if (shrine.title != "Mysterious Shrine"
        || shrine.choices.size() != 2
        || shrine.choices[0].effects.size() != 2
        || shrine.choices[0].effects[0].s1 != "restore_hp"
        || shrine.choices[0].effects[1].s1 != "restore_mp"
        || shrine.choices[1].effects.size() != 1) {
        return fail("Mysterious Shrine structure does not match TS");
    }
    const sm::GameEvent& offering = shrine.choices[1].effects[0];
    const bool offeringGold =
        offering.tag == sm::EventTag::PlayerGoldChange && offering.ix == 50;
    const bool offeringDamage =
        offering.tag == sm::EventTag::ApplyEffect
        && offering.s1 == "damage_hp"
        && offering.ix == 25;
    if (!offeringGold && !offeringDamage) {
        return fail("Mysterious Shrine offering is not a legal TS branch");
    }

    const auto& monolith = table[12];
    if (monolith.choices.size() != 2
        || monolith.choices[0].effects.size() != 2
        || monolith.choices[0].effects[0].tag != sm::EventTag::CodexUnlock
        || monolith.choices[0].effects[0].s1 != "cosmology"
        || monolith.choices[1].effects.size() != 2
        || monolith.choices[1].effects[0].tag != sm::EventTag::ReputationChange
        || monolith.choices[1].effects[0].s1 != "empire"
        || monolith.choices[1].effects[1].s1 != "cults") {
        return fail("Black Monolith encounter does not match TS effects");
    }

    const auto& witch = table[14];
    if (witch.title != "Witch's Hut"
        || witch.choices.size() != 3
        || witch.choices[1].effects.size() != 1
        || witch.choices[1].effects[0].tag != sm::EventTag::BattleStart
        || witch.choices[1].effects[0].s1 != "Forest Witch"
        || witch.choices[1].effects[0].s2 != "witch"
        || witch.choices[1].effects[0].ix != 7) {
        return fail("Witch encounter battle branch does not match TS");
    }
    return true;
}

bool test_random_encounter_logic_node() {
    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;
    sm::register_builtin_nodes(logic);

    bool fired = false;
    for (int i = 0; i < 512 && !fired; ++i) {
        sm::GameEvent move{sm::EventTag::PlayerMove};
        move.a = 20000u;
        bus.emit(move);
        bus.flush(0, 8);
        logic.tick(bus, player);

        const sm::GameEvent* dialog = bus.find(sm::EventTag::ShowDialog);
        if (!dialog) continue;
        if (dialog->s1.empty()
            || dialog->s2.empty()
            || !dialog->dialogChoices
            || dialog->dialogChoices->empty()
            || dialog->ix != static_cast<int>(dialog->dialogChoices->size())) {
            return fail("enc_random ShowDialog payload is incomplete");
        }
        fired = true;
    }

    if (!fired) {
        return fail("enc_random did not fire from PlayerMove steps");
    }
    if (!logic.is_consistent()) {
        return fail("enc_random left LogicNodeEngine inconsistent");
    }
    return true;
}

bool test_quest_failed_uses_failed_ledger() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 10;
    gs.worldTime.hour = 6;

    sm::Quest q{};
    q.id = "q_expired";
    q.title = "Expired";
    q.description = "Expired quest";
    q.category = sm::QuestCategory::Procedural;
    q.expireDay = 9;
    sm::Objective o{};
    o.kind = sm::ObjectiveKind::VisitCell;
    o.ix = 1;
    o.iy = 1;
    o.radius = 1.0f;
    q.objectives.push_back(o);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    engine.tick(active, bus, gs);
    if (!active.empty()) {
        return fail("expired quest was not removed");
    }
    if (!bus.has_tag(sm::EventTag::QuestFailed)) {
        return fail("expired quest did not emit QuestFailed");
    }

    sm::apply_events(bus.tick_events(), gs.player);
    if (contains_completed_id(gs.player, q.id)) {
        return fail("QuestFailed appended to completedQuestIds");
    }
    if (!contains_failed_id(gs.player, q.id)) {
        return fail("QuestFailed did not append to failedQuestIds");
    }
    if (!engine.is_known(active, gs.player, q.id)) {
        return fail("failed quest is not treated as known");
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

bool test_visit_cell_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;
    gs.player.x = 40.0f;
    gs.player.y = 50.0f;

    sm::Quest q{};
    q.id = "q_visit_cell";
    q.title = "Visit Cell";
    q.description = "Test VisitCell objective";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::VisitCell;
    objective.ix = 41;
    objective.iy = 50;
    objective.radius = 2.0f;
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    if (!active.empty() || !bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("VisitCell did not complete from player radius");
    }
    return true;
}

bool test_wait_at_timeadvance_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;

    sm::Quest q{};
    q.id = "q_wait_at";
    q.title = "Wait At";
    q.description = "Test WaitAt objective";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::WaitAt;
    objective.ix = 12;
    objective.iy = 18;
    objective.radius = 1.5f;
    objective.hoursRequired = 3;
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);

    sm::GameEvent twoHours{sm::EventTag::TimeAdvance};
    twoHours.ix = 2;
    bus.emit(twoHours);
    bus.flush(gs.worldTime.day, gs.worldTime.hour + 2);
    engine.tick(active, bus, gs);
    if (active.empty() || bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("WaitAt completed before required hours");
    }

    sm::GameEvent oneHour{sm::EventTag::TimeAdvance};
    oneHour.ix = 1;
    bus.emit(oneHour);
    bus.flush(gs.worldTime.day, gs.worldTime.hour + 3);
    engine.tick(active, bus, gs);
    if (!active.empty() || !bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("WaitAt did not complete after required TimeAdvance");
    }
    return true;
}

bool test_destroy_npc_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;

    sm::Quest q{};
    q.id = "q_destroy_npc";
    q.title = "Destroy Npc";
    q.description = "Test DestroyNpc objective";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::DestroyNpc;
    objective.npcType = 2;
    objective.count = 2;
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);

    sm::GameEvent first{sm::EventTag::NpcDeath};
    first.ix = 2;
    sm::GameEvent second{sm::EventTag::NpcDeath};
    second.a = 2;
    bus.emit(first);
    bus.emit(second);
    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    if (!active.empty() || !bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("DestroyNpc did not count NpcDeath payloads");
    }
    return true;
}

bool test_interact_cell_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 0;
    gs.worldTime.hour = 6;

    sm::Quest q{};
    q.id = "q_interact_cell";
    q.title = "Interact Cell";
    q.description = "Test InteractCell objective";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::InteractCell;
    objective.ix = 9;
    objective.iy = 11;
    q.objectives.push_back(objective);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);

    sm::GameEvent edit{sm::EventTag::WorldCellChange};
    edit.ix = 9;
    edit.iy = 11;
    bus.emit(edit);
    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    engine.tick(active, bus, gs);
    if (!active.empty() || !bus.has_tag(sm::EventTag::QuestCompleted)) {
        return fail("InteractCell did not consume WorldCellChange payload");
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

bool test_village_protect_generator_spawn_event() {
    sm::GameState gs{};
    gs.worldSeed = 0x51515151u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 3;

    sm::Settlement city{};
    city.id = 1;
    city.name = "Anchor";
    city.x = 20;
    city.y = 20;
    city.population = 500;
    city.mood = sm::SettlementMood::Stable;
    gs.settlements.push_back(city);

    sm::Village village{};
    village.id = 44;
    village.name = "Tense Hamlet";
    village.x = 24;
    village.y = 22;
    village.population = 80;
    village.mood = sm::SettlementMood::Tense;
    village.nearestCityId = city.id;
    gs.villages.push_back(village);

    sm::Quest selected{};
    bool found = false;
    for (int day = 0; day < 256 && !found; ++day) {
        gs.worldTime.day = day;
        const auto generated =
            sm::generate_quests_for_village(village, gs, gs.worldSeed);
        if (const sm::Quest* q = find_wait_quest(generated)) {
            selected = *q;
            found = true;
        }
    }
    if (!found) {
        return fail("village generator did not produce protect WaitAt quest");
    }
    if (selected.onAccept.empty()
        || selected.onAccept[0].tag != sm::EventTag::SpawnEntity
        || selected.onAccept[0].s1 != "bandit"
        || selected.onAccept[0].a < 2u) {
        return fail("protect quest did not carry SpawnEntity onAccept payload");
    }
    return true;
}

bool test_village_quest_ids_are_collision_safe() {
    sm::GameState gs{};
    gs.worldSeed = 0x71477147u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime.day = 5;

    sm::Settlement city{};
    city.id = 7;
    city.name = "Same Id City";
    city.x = 20;
    city.y = 20;
    city.population = 500;
    city.mood = sm::SettlementMood::Stable;
    city.eco.resourcePrices[std::size_t(sm::ResourceId::Iron)] = 99.0f;
    gs.settlements.push_back(city);

    sm::Village village{};
    village.id = 7;
    village.name = "Same Id Village";
    village.x = 24;
    village.y = 22;
    village.population = 80;
    village.mood = sm::SettlementMood::Tense;
    village.nearestCityId = city.id;
    gs.villages.push_back(village);

    const auto cityQuests =
        sm::generate_quests_for_settlement(city, gs, gs.worldSeed);
    const auto villageQuests =
        sm::generate_quests_for_village(village, gs, gs.worldSeed);
    if (cityQuests.empty() || villageQuests.empty()) {
        return fail("collision-safe quest id test did not generate quests");
    }
    for (const auto& vq : villageQuests) {
        if (vq.giverSettlementId != village.id) {
            return fail("village quest changed gameplay giver id");
        }
        if (vq.id.find("_v7_") == std::string::npos) {
            return fail("village quest id does not include village-safe segment");
        }
        for (const auto& cq : cityQuests) {
            if (cq.id == vq.id) {
                return fail("city and village quests collided by id");
            }
        }
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
    settlement.eco.resourcePrices[std::size_t(sm::ResourceId::Iron)] = 99.0f;
    gs.settlements.push_back(settlement);

    sm::Quest selected{};
    bool found = false;
    for (int day = 0; day < 256 && !found; ++day) {
        gs.worldTime.day = day;
        const auto generated =
            sm::generate_quests_for_settlement(settlement, gs, gs.worldSeed);
        if (const sm::Quest* q = find_delivery_quest(generated, "mat_iron")) {
            selected = *q;
            found = true;
        }
    }
    if (!found) {
        return fail("procedural generator did not produce economy-driven delivery quest") ? 0 : 1;
    }
    if (selected.objectives.empty()
        || selected.objectives.front().kind != sm::ObjectiveKind::DeliverItems
        || selected.objectives.front().itemId != "mat_iron"
        || selected.objectives.front().quantity <= 0
        || selected.objectives.front().targetSettlementId != settlement.id) {
        return fail("selected delivery quest does not follow economy resource demand") ? 0 : 1;
    }

    const int startGold = gs.player.gold;
    const int rewardGold = gold_reward(selected);
    if (rewardGold <= 0) {
        return fail("selected generated delivery quest has no gold reward") ? 0 : 1;
    }
    gs.player.inventory.add("mat_iron", selected.objectives.front().quantity);

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
    if (!active.empty()) {
        return fail("delivery items did not complete generated quest") ? 0 : 1;
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

    bus.flush(gs.worldTime.day, gs.worldTime.hour);
    sm::apply_events(bus.tick_events(), gs.player);
    if (gs.player.gold != startGold + rewardGold) {
        return fail("empty post-flush tick reapplied reward") ? 0 : 1;
    }

    if (!test_event_bus_contract_surface()) return 1;
    if (!test_ts_quest_tag_aliases()) return 1;
    if (!test_effect_applicator_ts_verbs()) return 1;
    if (!test_xp_level_up_producer()) return 1;
    if (!test_level_up_show_dialog_node()) return 1;
    if (!test_settlement_show_dialog_node()) return 1;
    if (!test_logic_node_add_registers_inactive()) return 1;
    if (!test_logic_node_pending_ids_survive_node_add()) return 1;
    if (!test_logic_node_effect_can_remove_self()) return 1;
    if (!test_intro_show_story_node()) return 1;
    if (!test_encounter_table_shape()) return 1;
    if (!test_random_encounter_logic_node()) return 1;
    if (!test_quest_failed_uses_failed_ledger()) return 1;
    if (!test_item_delivery_direct_path()) return 1;
    if (!test_find_location_player_move_objective()) return 1;
    if (!test_visit_cell_objective()) return 1;
    if (!test_wait_at_timeadvance_objective()) return 1;
    if (!test_destroy_npc_objective()) return 1;
    if (!test_interact_cell_objective()) return 1;
    if (!test_abandon_emits_and_removes()) return 1;
    if (!test_village_protect_generator_spawn_event()) return 1;
    if (!test_village_quest_ids_are_collision_safe()) return 1;

    std::printf("OK quest_lifecycle_test id=%s item=%s qty=%d reward_gold=%d completed=%zu failed=%zu event_bus=ok quest_tags=ok effects=ok xp_level=ok level_dialog=ok settlement_dialog=ok settlement_enter=ok settlement_leave=ok logic_register=ok logic_rehash=ok logic_self_remove=ok intro_story=ok chapter_placeholder=ok encounter_table=ok enc_random=ok quest_failed=ok item_direct=ok find_move=ok visit_cell=ok wait_at=ok destroy_npc=ok interact_cell=ok abandon=ok village_protect=ok quest_id_scope=ok\n",
                selected.id.c_str(),
                selected.objectives.front().itemId.c_str(),
                selected.objectives.front().quantity,
                rewardGold,
                gs.player.completedQuestIds.size(),
                gs.player.failedQuestIds.size());
    return 0;
}
