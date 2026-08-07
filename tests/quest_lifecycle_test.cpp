#include "content/quests/procedural.h"
#include "content/plot/chapter_1.h"
#include "content/plot/encounters.h"
#include "content/plot/intro.h"
#include "events/effect_applicator.h"
#include "events/event_bus.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
#include "events/quests/quest_engine.h"

#include "core/rng.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

// shuffled_order() has external linkage in procedural.cpp (deliberately kept out
// of its anonymous namespace) so this test can exercise the Fisher-Yates
// out-of-bounds guard directly. See src/content/quests/procedural.cpp.
namespace sm { std::vector<int> shuffled_order(Rng& rng); }

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

int count_completed_id(const sm::PlayerState& p, const std::string& id) {
    int count = 0;
    for (const auto& completed : p.completedQuestIds) {
        if (completed == id) ++count;
    }
    return count;
}

int count_failed_id(const sm::PlayerState& p, const std::string& id) {
    int count = 0;
    for (const auto& failed : p.failedQuestIds) {
        if (failed == id) ++count;
    }
    return count;
}

void apply_pending(sm::EventBus& bus, sm::GameState& gs, std::size_t& applied) {
    while (true) {
        const auto& events = bus.tick_events();
        if (applied >= events.size()) return;

        const std::size_t begin = applied;
        const std::size_t end = events.size();
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        sm::apply_events(pending, gs);
        applied = end;
    }
}

// Poll-side stand-ins for the deleted zero-caller EventBus query helpers:
// the consumer contract is scanning tick_events(), exactly like production.
bool has_tag(const sm::EventBus& bus, sm::EventTag tag) {
    for (const auto& ev : bus.tick_events())
        if (ev.tag == tag) return true;
    return false;
}

const sm::GameEvent* find_tag(const sm::EventBus& bus, sm::EventTag tag) {
    for (const auto& ev : bus.tick_events())
        if (ev.tag == tag) return &ev;
    return nullptr;
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
    if (subId == 0 || bus.subscription_count() != 1) {
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
    // The consumer contract is POLLING: scan tick_events() like production
    // does (the per-tick query helpers were deleted as zero-caller API).
    if (bus.tick_events().size() != 3
        || count_tag(bus, sm::EventTag::Custom) != 2) {
        return fail("EventBus tick buffer did not retain emitted events");
    }
    std::vector<const sm::GameEvent*> matches;
    for (const auto& ev : bus.tick_events())
        if (ev.tag == sm::EventTag::Custom) matches.push_back(&ev);
    if (matches.size() != 2
        || matches[0]->s1 != "first" || matches[0]->ix != 1
        || matches[1]->s1 != "second") {
        return fail("EventBus tick order does not match emit order");
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
    if (bus.subscription_count() != 0) {
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
    sm::EventBus addDuringEmitBus;
    int firstOrder = 0;
    int lateOrder = 0;
    int order = 0;
    addDuringEmitBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        firstOrder = ++order;
        addDuringEmitBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
            lateOrder = ++order;
        });
    });
    addDuringEmitBus.emit(first);
    if (firstOrder != 1
        || lateOrder != 2
        || addDuringEmitBus.subscription_count() != 2) {
        return fail("EventBus does not match TS live listener append order");
    }

    sm::EventBus removeDuringEmitBus;
    int removerSeen = 0;
    int removedSeen = 0;
    std::uint32_t removedSubId = 0;
    removeDuringEmitBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++removerSeen;
        removeDuringEmitBus.unsubscribe(removedSubId);
    });
    removedSubId = removeDuringEmitBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++removedSeen;
    });
    removeDuringEmitBus.emit(first);
    if (removerSeen != 1
        || removedSeen != 0
        || removeDuringEmitBus.subscription_count() != 1) {
        return fail("EventBus does not match TS listener removal during emit");
    }

    sm::EventBus unrelatedRemoveBus;
    int firstCustomSeen = 0;
    int secondCustomSeen = 0;
    const std::uint32_t playerMoveSubId =
        unrelatedRemoveBus.on(sm::EventTag::PlayerMove, [&](const sm::GameEvent&) {});
    unrelatedRemoveBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++firstCustomSeen;
        unrelatedRemoveBus.unsubscribe(playerMoveSubId);
    });
    unrelatedRemoveBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++secondCustomSeen;
    });
    unrelatedRemoveBus.emit(first);
    if (firstCustomSeen != 1
        || secondCustomSeen != 1
        || unrelatedRemoveBus.subscription_count() != 2) {
        return fail("EventBus unrelated-tag unsubscribe affected current tag dispatch");
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
    sm::GameEvent onAccept{sm::EventTag::SpawnEntity};
    onAccept.s1 = "bandit";
    q.onAccept.push_back(onAccept);
    sm::EventBus bus;
    sm::QuestEngine engine;
    sm::PlayerState player{};
    std::vector<sm::Quest> active;
    std::size_t activeDuringOnAccept = 0;
    std::size_t activeDuringQuestStart = 0;
    bus.on(sm::EventTag::SpawnEntity, [&](const sm::GameEvent&) {
        activeDuringOnAccept = active.size();
    });
    bus.on(sm::EventTag::QuestStart, [&](const sm::GameEvent&) {
        activeDuringQuestStart = active.size();
    });
    engine.accept(active, q, player, bus);
    if (count_tag(bus, sm::EventTag::QuestStart) == 0) {
        return fail("QuestEngine::accept did not emit QuestStart");
    }
    if (activeDuringOnAccept != 1 || activeDuringQuestStart != 1) {
        return fail("QuestEngine::accept emitted events before activeQuests push");
    }
    if (bus.tick_events().size() != 2
        || bus.tick_events()[0].tag != sm::EventTag::SpawnEntity
        || bus.tick_events()[1].tag != sm::EventTag::QuestStart
        || bus.tick_events()[1].s1 != q.id
        || bus.tick_events()[1].s2 != q.title) {
        return fail("QuestEngine::accept did not emit onAccept before QuestStart");
    }
    engine.accept(active, q, player, bus);
    if (active.size() != 2 || count_tag(bus, sm::EventTag::QuestStart) != 2) {
        return fail("QuestEngine::accept deduped a quest unlike TS accept()");
    }
    return true;
}

bool test_effect_applicator_ts_verbs() {
    // The player's standing lives in the relation matrix now, so the verb test
    // drives a whole GameState; `player` stays a reference for readability.
    sm::GameState verbState{};
    sm::PlayerState& player = verbState.player;
    player.gold = 10;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.levelData.exp = 0;
    player.sheet.levelData.expToNext = 100;
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

    sm::GameEvent debt{sm::EventTag::PlayerGoldChange};
    debt.ix = -30;
    events.push_back(debt);

    // A tag the applicator has no arm for (SpellCast is producer-side only):
    // it must pass through without touching the player.
    sm::GameEvent ignoredSpell{sm::EventTag::SpellCast};
    ignoredSpell.ix = 999;
    events.push_back(ignoredSpell);

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
    events.push_back(complete);

    sm::GameEvent failQuest{sm::EventTag::QuestFail};
    failQuest.s1 = "q_fail";
    events.push_back(failQuest);
    events.push_back(failQuest);

    sm::apply_events(events, verbState);

    if (player.gold != -15) {
        return fail("PlayerGoldChange did not mutate gold like TS");
    }
    if (player.combatStats.currentHp != 33
        || player.combatStats.currentMp != 25
        || player.combatStats.currentSp != 26) {
        return fail("ApplyEffect TS hp/mp/sp verbs produced wrong combat stats");
    }
    if (player.sheet.levelData.exp != 42 || player.sheet.levelData.level != 1) {
        return fail("grant_xp did not apply XP without direct level mutation");
    }
    if (sm::player_reputation(&verbState, "guild") != 3) {
        return fail("ReputationChange did not move the player's row in the matrix");
    }
    if (player.codexUnlocked.size() != 1 || player.codexUnlocked[0] != "entry.alpha") {
        return fail("CodexUnlock did not deduplicate entry like TS");
    }
    if (!contains_completed_id(player, "q_complete")
        || !contains_failed_id(player, "q_fail")
        || !contains_completed_id(player, "q_fail")) {
        return fail("quest completion/failure ledgers do not match TS done semantics");
    }
    if (count_completed_id(player, "q_complete") != 2
        || count_completed_id(player, "q_fail") != 2) {
        return fail("quest completed ledger deduped ids unlike TS push semantics");
    }
    if (count_failed_id(player, "q_fail") != 1) {
        return fail("quest failed ledger duplicated native classification");
    }

    sm::PlayerState negativeHp{};
    negativeHp.combatStats.currentHp = 7;
    negativeHp.combatStats.maxHp = 50;
    sm::GameEvent lethal{sm::EventTag::ApplyEffect};
    lethal.s1 = "damage_hp";
    lethal.ix = 10;
    sm::GameState lethalState{};
    lethalState.player = negativeHp;
    sm::apply_events(std::span<const sm::GameEvent>(&lethal, 1), lethalState);
    if (lethalState.player.combatStats.currentHp != -3) {
        return fail("damage_hp clamped HP unlike TS applyEffect_");
    }

    return true;
}

// Experience is granted and consumed by ONE path (award_exp), whoever pays it.
// This test used to demand the opposite — that a scripted grant_xp leave the
// player sitting on unspendable experience — which is the bug, not a contract:
// only the subworld kill path ever drained the pool, so a hero could finish ten
// contracts and stay level 1 until he stabbed a wolf.
//
// What is still true is that levelling is STATE, not an event: the applicator
// fabricates no level-up event at all (the PlayerLevelUp tag itself was deleted
// 2026-08-05 with the rest of the never-referenced tags, so the guarantee is
// now the type system's, not an assertion's).
bool test_grant_xp_levels_through_the_one_path() {
    sm::GameState xpState{};
    sm::PlayerState& player = xpState.player;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.attributes.wis = 0;  // isolate from the wis dividend (own test)
    player.combatStats = sm::calculate_combat_stats(player.sheet.attributes, player.sheet.skills);
    const int firstThreshold = player.sheet.levelData.expToNext;

    sm::EventBus bus;
    std::size_t applied = 0;
    sm::GameEvent xp1{sm::EventTag::ApplyEffect};
    xp1.s1 = "grant_xp";
    xp1.ix = firstThreshold - 10;      // just short of the level
    bus.emit(xp1);
    sm::GameEvent xp2{sm::EventTag::ApplyEffect};
    xp2.s1 = "grant_xp";
    xp2.ix = 20;                       // ...and over it
    bus.emit(xp2);

    apply_pending(bus, xpState, applied);
    if (player.sheet.levelData.level != 2) {
        return fail("grant_xp left the player below a threshold he had passed");
    }
    if (player.sheet.levelData.exp != 10) {
        return fail("the remainder past the threshold was not carried");
    }
    if (player.sheet.levelData.expToNext != sm::exp_to_next_level(2)) {
        return fail("the next threshold was not recomputed for the new level");
    }
    if (player.sheet.levelData.attributePoints
            != sm::default_level_data().attributePoints + 3
        || player.sheet.levelData.skillPoints
            != sm::default_level_data().skillPoints + 1) {
        return fail("levelling through grant_xp did not pay its points");
    }
    return true;
}

// The wis dividend (owner ruling 2026-08-05): every XP grant scales by the
// recipient's expMult (+1% per wis point). Before the wiring, wis was a
// dead attribute — computed, displayed, consumed by nothing.
bool test_grant_xp_pays_the_wis_dividend() {
    sm::GameState wisState{};
    sm::PlayerState& player = wisState.player;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.attributes.wis = 10;  // expMult = 1.10

    sm::GameEvent grant{sm::EventTag::ApplyEffect};
    grant.s1 = "grant_xp";
    grant.ix = 100;
    sm::apply_events(std::span<const sm::GameEvent>(&grant, 1), wisState);
    if (player.sheet.levelData.exp != 110) {
        std::fprintf(stderr, "exp=%d (expected 110)\n",
                     player.sheet.levelData.exp);
        return fail("grant_xp ignored the wis expMult");
    }
    return true;
}

// A quest that pays experience must pay the LEVEL it is worth. This is the bug
// the audit named: the reward did a bare `exp +=`, and the only code that ever
// consumed the pool lived in the subworld kill path — so contract experience
// piled up unspendable, and a quest-only playthrough never levelled at all.
bool test_quest_xp_reward_levels_the_player() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.player.sheet.levelData = sm::default_level_data();
    gs.player.sheet.attributes.wis = 0;  // isolate from the wis dividend

    // Worth three thresholds at once — a chapter reward at low level does this,
    // and one level-up per grant would silently swallow the rest.
    const int l1 = sm::exp_to_next_level(1);
    const int l2 = sm::exp_to_next_level(2);
    const int l3 = sm::exp_to_next_level(3);

    sm::Quest q{};
    q.id = "q_xp_reward";
    q.title = "Paid in experience";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective done{};
    done.kind = sm::ObjectiveKind::VisitCell;
    done.ix = 0; done.iy = 0; done.radius = 4.0f;   // player starts at (0,0)
    q.objectives.push_back(done);
    sm::Reward xp{};
    xp.kind = sm::RewardKind::Xp;
    xp.amount = l1 + l2 + l3 + 7;
    q.rewards.push_back(xp);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    engine.tick(active, bus, gs);

    if (!active.empty()) return fail("the reward quest did not complete");
    if (gs.player.sheet.levelData.level != 4) {
        return fail("quest XP did not level the player (or stopped at one level)");
    }
    if (gs.player.sheet.levelData.exp != 7) {
        return fail("the remainder past the last threshold was not carried");
    }
    return true;
}

// Every builtin node is registered AND active. The name and the three
// assertions below used to measure this against the TypeScript registry, an
// implementation that no longer exists and that the owner has ruled we owe no
// parity to — so a node deleted in C++ for a C++ reason read as "drifted from
// TS". It now states the C++ contract on its own terms.
//
// sys_level_up is deliberately absent: it waited on EventTag::PlayerLevelUp,
// which nothing has ever emitted, so it could not fire (removed 2026-08-05).
bool test_builtin_nodes_are_registered_and_active() {
    sm::LogicNodeEngine logic;
    sm::register_builtin_nodes(logic);
    const char* kBuiltinIds[] = {"sys_settlement"};
    const int kBuiltinCount = int(sizeof(kBuiltinIds) / sizeof(kBuiltinIds[0]));
    if (logic.node_count() != kBuiltinCount
        || logic.active_count() != kBuiltinCount) {
        return fail("builtin logic node count is not the registered set");
    }
    for (const char* id : kBuiltinIds) {
        if (!logic.has(id)) return fail("a builtin logic node is missing");
        if (!logic.is_active(id)) return fail("a builtin logic node is inactive");
    }
    return true;
}

// (Was test_player_level_up_event_is_presentation_only, exercising the
// PlayerLevelUp tag.) That tag is DELETED now; the surviving contract is the
// general one: a tag the applicator has no arm for passes through without
// mutating the player. Custom is the permanent such tag.
bool test_unhandled_tag_is_inert_in_applicator() {
    sm::GameState levelState{};
    sm::PlayerState& player = levelState.player;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.levelData.exp =
        sm::exp_to_next_level(1) + sm::exp_to_next_level(2) + 5;
    player.sheet.levelData.attributePoints = 7;
    player.combatStats.currentHp = 7;
    player.combatStats.maxHp = 9;

    const int beforeLevel = player.sheet.levelData.level;
    const int beforeExp = player.sheet.levelData.exp;
    const int beforeExpToNext = player.sheet.levelData.expToNext;
    const int beforeAttributePoints = player.sheet.levelData.attributePoints;
    const int beforeHp = player.combatStats.currentHp;
    const int beforeMaxHp = player.combatStats.maxHp;

    sm::GameEvent unhandled{sm::EventTag::Custom};
    unhandled.ix = 99;
    sm::apply_events(std::span<const sm::GameEvent>(&unhandled, 1), levelState);
    if (player.sheet.levelData.level != beforeLevel
        || player.sheet.levelData.exp != beforeExp
        || player.sheet.levelData.expToNext != beforeExpToNext
        || player.sheet.levelData.attributePoints != beforeAttributePoints
        || player.combatStats.currentHp != beforeHp
        || player.combatStats.maxHp != beforeMaxHp) {
        return fail("an unhandled tag mutated player inside effect applicator");
    }
    return true;
}

// A test_level_up_show_dialog_node lived here and passed for as long as the
// sys_level_up node existed — because it EMITTED PlayerLevelUp onto the bus
// itself and then checked the node reacted. The node did react. Nothing in the
// game ever emitted that event, so the dialog never appeared in play, and this
// test's manufactured input is precisely what hid that for so long. Removed
// with the node (2026-08-05).
//
// Worth remembering when writing the replacement: a test that supplies the
// event under test proves the HANDLER works, never that the event is raised.

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

        const sm::GameEvent* dialog = find_tag(bus, sm::EventTag::ShowDialog);
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
        if (has_tag(bus, sm::EventTag::ShowDialog)) {
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
    if (has_tag(bus, sm::EventTag::Custom)) {
        return fail("inactive registered node fired before activate");
    }

    logic.activate("inactive");
    logic.tick(bus, player);
    const sm::GameEvent* ev = find_tag(bus, sm::EventTag::Custom);
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

bool test_logic_node_tick_order_matches_ts_set() {
    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;

    sm::LogicNode first{};
    first.id = "first";
    first.label = "First";
    first.effect = [](sm::NodeContext& ctx) {
        sm::GameEvent ev{sm::EventTag::Custom};
        ev.s1 = "first";
        ctx.bus->emit(ev);
    };
    logic.add(std::move(first));

    sm::LogicNode second{};
    second.id = "second";
    second.label = "Second";
    sm::ConditionSlot condition{};
    condition.isEvent = false;
    condition.check = [](const sm::EventBus& bus, const sm::PlayerState&) {
        return has_tag(bus, sm::EventTag::Custom);
    };
    second.conditions.push_back(std::move(condition));
    second.mask.push_back(1);
    second.effect = [](sm::NodeContext& ctx) {
        sm::GameEvent ev{sm::EventTag::Custom};
        ev.s1 = "second";
        ctx.bus->emit(ev);
    };
    logic.add(std::move(second));

    logic.activate("first");
    logic.activate("second");
    logic.tick(bus, player);

    if (bus.tick_events().size() != 2
        || bus.tick_events()[0].s1 != "first"
        || bus.tick_events()[1].s1 != "second") {
        return fail("LogicNodeEngine tick order/interleaving does not match TS Set semantics");
    }
    if (logic.active_count() != 0 || !logic.is_consistent()) {
        return fail("LogicNodeEngine active state after ordered tick is inconsistent");
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

    const sm::GameEvent* ev = find_tag(bus, sm::EventTag::Custom);
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

bool test_logic_node_self_reactivation_safe_cases() {
    {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;

        sm::LogicNode selfActivate{};
        selfActivate.id = "self_activate";
        selfActivate.label = "Self Activate";
        selfActivate.effect = [](sm::NodeContext& ctx) {
            sm::GameEvent ev{sm::EventTag::Custom};
            ev.s1 = "self_activate";
            ctx.bus->emit(ev);
            ctx.activate("self_activate");
        };
        logic.add(std::move(selfActivate));
        logic.activate("self_activate");

        logic.tick(bus, player);
        if (count_tag(bus, sm::EventTag::Custom) != 1
            || logic.active_count() != 0
            || logic.is_active("self_activate")) {
            return fail("LogicNodeEngine direct self-activate did not match TS Set no-op");
        }
    }

    {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;

        sm::LogicNode selfNext{};
        selfNext.id = "self_next";
        selfNext.label = "Self Next";
        selfNext.next.push_back("self_next");
        selfNext.effect = [](sm::NodeContext& ctx) {
            sm::GameEvent ev{sm::EventTag::Custom};
            ev.s1 = "self_next";
            ctx.bus->emit(ev);
        };
        logic.add(std::move(selfNext));
        logic.activate("self_next");

        logic.tick(bus, player);
        if (count_tag(bus, sm::EventTag::Custom) != 1
            || logic.active_count() != 1
            || !logic.is_active("self_next")) {
            return fail("LogicNodeEngine self next-link did not stay active for next tick");
        }

        bus.flush(0, 6);
        logic.tick(bus, player);
        if (count_tag(bus, sm::EventTag::Custom) != 1
            || !logic.is_active("self_next")) {
            return fail("LogicNodeEngine self next-link did not fire once per tick");
        }
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
    if (!logic.has("intro_main")
        || !logic.has(sm::content::kChapter1NodeId)
        || !logic.is_active("intro_main")
        || logic.is_active(sm::content::kChapter1NodeId)) {
        return fail("plot node ids/initial activation do not match TS plot registry");
    }

    logic.tick(bus, player);

    const sm::GameEvent* event = find_tag(bus, sm::EventTag::ShowStory);
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
    if (has_tag(bus, sm::EventTag::ShowStory)) {
        return fail("intro_main ShowStory fired more than once without reactivation");
    }

    logic.activate(sm::content::kChapter1NodeId);
    if (logic.active_count() != 1
        || !logic.is_active(sm::content::kChapter1NodeId)) {
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

// A test_random_encounter_logic_node lived here and drove the "enc_random"
// node: walk far enough, roll a private die, get a uniformly random list row.
// Removed with the node (owner ruling 2026-08-05: events come from game
// context and state, never an unconditional random roll over a list).

bool test_quest_failed_uses_failed_ledger() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(10, 6, 0);
    gs.player.x = 1.0f;
    gs.player.y = 1.0f;

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
    if (!has_tag(bus, sm::EventTag::QuestFailed)) {
        return fail("expired quest did not emit QuestFailed");
    }
    if (has_tag(bus, sm::EventTag::QuestCompleted)) {
        return fail("expired quest completed instead of failing first");
    }
    const sm::GameEvent* failed = find_tag(bus, sm::EventTag::QuestFail);
    if (!failed
        || failed->s1 != q.id
        || failed->s2 != "expired"
        || failed->b != sm::kEventEffectAlreadyApplied) {
        return fail("expired quest did not carry TS QuestFail reason");
    }
    if (count_completed_id(gs.player, q.id) != 1
        || count_failed_id(gs.player, q.id) != 1) {
        return fail("expired quest did not mutate done/failed ledgers directly like TS");
    }

    sm::apply_events(bus.tick_events(), gs);
    if (count_completed_id(gs.player, q.id) != 1) {
        return fail("QuestFailed duplicated TS completedQuestIds done ledger");
    }
    if (count_failed_id(gs.player, q.id) != 1) {
        return fail("QuestFailed duplicated failedQuestIds");
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
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;
    gs.player.inventory.add("wood", 3);

    sm::Settlement settlement{};
    settlement.id = 7;
    settlement.name = "Test Anchorage";
    settlement.x = 12;
    settlement.y = 18;
    settlement.population = 1000;
    settlement.mood = sm::SettlementMood::Stable;
    settlement.kingdomIdx = 0;
    gs.settlements.push_back(settlement);

    sm::Quest q{};
    q.id = "q_delivery_item";
    q.title = "Deliver Materials";
    q.description = "Test delivery";
    q.category = sm::QuestCategory::Procedural;
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::DeliverItems;
    objective.itemId = "wood";
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
    if (gs.player.inventory.count("wood") != 1) {
        return fail("delivery did not remove delivered items");
    }
    if (gs.player.inventory.count("misc_gem") != 2) {
        return fail("item reward did not grant item reward");
    }

    std::size_t applied = 0;
    apply_pending(bus, gs, applied);
    if (gs.player.inventory.count("wood") != 1
        || gs.player.inventory.count("misc_gem") != 2) {
        return fail("event application duplicated direct inventory mutation");
    }
    if (!contains_completed_id(gs.player, q.id)) {
        return fail("delivery completion was not applied");
    }
    return true;
}

bool test_quest_reward_dispatch_order_and_application() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 10.0f;
    gs.player.y = 10.0f;
    gs.player.gold = 20;
    gs.player.sheet.levelData = sm::default_level_data();
    gs.player.sheet.levelData.exp = 0;

    sm::Quest q{};
    q.id = "q_reward_order";
    q.title = "Reward Order";
    q.description = "Reward parity test";
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::VisitCell;
    objective.ix = 10;
    objective.iy = 10;
    objective.radius = 1.0f;
    q.objectives.push_back(objective);

    sm::Reward gold{};
    gold.kind = sm::RewardKind::Gold;
    gold.amount = 7;
    q.rewards.push_back(gold);

    sm::Reward xp{};
    xp.kind = sm::RewardKind::Xp;
    xp.amount = 11;
    q.rewards.push_back(xp);

    sm::Reward item{};
    item.kind = sm::RewardKind::Item;
    item.itemId = "misc_gem";
    item.amount = 2;
    q.rewards.push_back(item);

    sm::Reward rep{};
    rep.kind = sm::RewardKind::Reputation;
    rep.faction = "guild";
    rep.delta = 3;
    q.rewards.push_back(rep);

    sm::Reward custom{};
    custom.kind = sm::RewardKind::Event;
    custom.event.tag = sm::EventTag::Custom;
    custom.event.s1 = "reward_event";
    q.rewards.push_back(custom);

    sm::EventBus bus;
    int goldSeenByListener = -1;
    int completedDuringGold = -1;
    int reputationSeenByListener = -1;
    bus.on(sm::EventTag::PlayerGoldChange, [&](const sm::GameEvent&) {
        goldSeenByListener = gs.player.gold;
        completedDuringGold = count_completed_id(gs.player, q.id);
    });
    bus.on(sm::EventTag::ReputationChange, [&](const sm::GameEvent&) {
        reputationSeenByListener = sm::player_reputation(&gs, "guild");
    });
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    engine.tick(active, bus, gs);

    const auto& events = bus.tick_events();
    if (!active.empty() || events.size() != 4) {
        return fail("quest reward dispatch did not emit expected event count");
    }
    if (events[0].tag != sm::EventTag::PlayerGoldChange
        || events[0].ix != 7
        || events[0].iy != 27
        || events[0].b != sm::kEventEffectAlreadyApplied
        || events[1].tag != sm::EventTag::ReputationChange
        || events[1].s1 != "guild"
        || events[1].ix != 3
        || events[1].iy != 3
        || events[1].b != sm::kEventEffectAlreadyApplied
        || events[2].tag != sm::EventTag::Custom
        || events[2].s1 != "reward_event"
        || events[3].tag != sm::EventTag::QuestComplete
        || events[3].s1 != q.id
        || events[3].b != sm::kEventEffectAlreadyApplied) {
        return fail("quest reward dispatch order does not match TS reward-before-complete flow");
    }
    if (goldSeenByListener != 27
        || completedDuringGold != 1
        || reputationSeenByListener != 3) {
        return fail("quest reward listeners did not see TS direct state mutation");
    }
    if (gs.player.gold != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || gs.player.inventory.count("misc_gem") != 2
        || count_completed_id(gs.player, q.id) != 1) {
        return fail("quest rewards did not mutate state directly like TS");
    }

    std::size_t applied = 0;
    apply_pending(bus, gs, applied);
    if (gs.player.gold != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || gs.player.inventory.count("misc_gem") != 2
        || count_completed_id(gs.player, q.id) != 1) {
        return fail("quest reward events duplicated direct TS state");
    }

    apply_pending(bus, gs, applied);
    if (gs.player.gold != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || gs.player.inventory.count("misc_gem") != 2
        || count_completed_id(gs.player, q.id) != 1) {
        return fail("quest reward events reapplied after pending cursor advanced");
    }
    return true;
}

bool test_find_location_player_move_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

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
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
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
    gs.worldTime = sm::world_time_at(0, 6, 0);
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
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (!active.empty() || !has_tag(bus, sm::EventTag::QuestCompleted)) {
        return fail("VisitCell did not complete from player radius");
    }
    return true;
}

bool test_quest_completion_order_matches_ts_reverse_scan() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 10.0f;
    gs.player.y = 10.0f;

    auto make_visit = [](const char* id) {
        sm::Quest q{};
        q.id = id;
        q.title = id;
        q.description = "Reverse scan order test";
        q.category = sm::QuestCategory::Procedural;
        sm::Objective objective{};
        objective.kind = sm::ObjectiveKind::VisitCell;
        objective.ix = 10;
        objective.iy = 10;
        objective.radius = 1.0f;
        q.objectives.push_back(objective);
        return q;
    };

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(make_visit("q_low"));
    active.push_back(make_visit("q_high"));

    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);

    std::vector<std::string> completed;
    for (const auto& ev : bus.tick_events()) {
        if (ev.tag == sm::EventTag::QuestComplete) {
            completed.push_back(ev.s1);
        }
    }
    if (!active.empty()
        || completed.size() != 2
        || completed[0] != "q_high"
        || completed[1] != "q_low") {
        return fail("QuestEngine completion order does not match TS reverse scan");
    }
    return true;
}

bool test_wait_at_timeadvance_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
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

    sm::GameEvent compressedLegacy{sm::EventTag::TimeAdvance};
    compressedLegacy.ix = 2;
    bus.emit(compressedLegacy);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour() + 2);
    engine.tick(active, bus, gs);
    if (active.empty() || has_tag(bus, sm::EventTag::QuestCompleted)
        || active[0].objectives[0].hoursWaited != 1) {
        return fail("WaitAt treated one TimeAdvance event as more than one TS hour");
    }

    sm::GameEvent oneHour{sm::EventTag::TimeAdvance};
    oneHour.ix = 1;
    bus.emit(oneHour);
    sm::GameEvent anotherHour{sm::EventTag::TimeAdvance};
    anotherHour.ix = 1;
    bus.emit(anotherHour);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour() + 3);
    engine.tick(active, bus, gs);
    if (!active.empty() || !has_tag(bus, sm::EventTag::QuestCompleted)) {
        return fail("WaitAt did not complete after required TimeAdvance");
    }
    return true;
}

bool test_destroy_npc_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

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

    // A kill of the wanted TYPE counts. NpcDeath carries the type in `ix`.
    sm::GameEvent real{sm::EventTag::NpcDeath};
    real.a = 41;          // entity handle — irrelevant to the tally
    real.ix = 2;          // type 2: this is the kill the contract is about
    // A different creature whose ENTITY HANDLE happens to equal the wanted type.
    // This is the bug: handles are small integers, so the second body ever
    // created dying used to complete a "slay two of type 2" contract by itself.
    sm::GameEvent impostor{sm::EventTag::NpcDeath};
    impostor.a = 2;       // handle 2
    impostor.ix = 7;      // but a type-7 body
    // And a body carrying no NPCKind at all: it reports kNoNpcType, never a
    // plausible-looking 0 that would be counted as a Peasant.
    sm::GameEvent kindless{sm::EventTag::NpcDeath};
    kindless.a = 55;
    kindless.ix = sm::kNoNpcType;
    bus.emit(real);
    bus.emit(impostor);
    bus.emit(kindless);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (active.size() != 1) {
        return fail("DestroyNpc counted an entity handle / a kindless body as a kill");
    }
    if (active[0].objectives[0].killed != 1) {
        return fail("DestroyNpc tally is not one kill after one real kill");
    }

    // The second real kill of that type completes it.
    sm::GameEvent secondReal{sm::EventTag::NpcDeath};
    secondReal.a = 77;
    secondReal.ix = 2;
    bus.emit(secondReal);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (!active.empty() || !has_tag(bus, sm::EventTag::QuestCompleted)) {
        return fail("DestroyNpc did not complete on kills of the wanted type");
    }
    return true;
}

bool test_interact_cell_objective() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

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

    // Same x, different y: an event on ANOTHER cell must not satisfy an
    // objective about this one (the LandmarkChangeOwner arm used to accept it).
    sm::GameEvent elsewhere{sm::EventTag::WorldCellChange};
    elsewhere.ix = 9;
    elsewhere.iy = 12;
    bus.emit(elsewhere);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (active.size() != 1) {
        return fail("InteractCell completed on an event from a different cell");
    }

    sm::GameEvent edit{sm::EventTag::WorldCellChange};
    edit.ix = 9;
    edit.iy = 11;
    bus.emit(edit);
    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (!active.empty() || !has_tag(bus, sm::EventTag::QuestCompleted)) {
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
    const sm::GameEvent* ev = find_tag(bus, sm::EventTag::QuestFail);
    if (!ev || ev->s1 != q.id || ev->s2 != "abandoned"
        || ev->a != std::uint32_t(sm::quest_id_key(q.id))) {
        return fail("abandon did not emit TS QuestFail(abandoned) payload");
    }

    sm::GameState abandonState{};
    sm::PlayerState& player = abandonState.player;
    sm::apply_events(bus.tick_events(), abandonState);
    if (!player.completedQuestIds.empty()
        || !player.failedQuestIds.empty()
        || engine.is_known(active, player, q.id)) {
        return fail("abandoned quest was recorded as done unlike TS screen abandon");
    }

    engine.abandon(active, q.id, bus);
    if (count_tag(bus, sm::EventTag::QuestFail) != 1) {
        return fail("abandon emitted duplicate event for missing quest");
    }
    return true;
}

bool test_village_protect_generator_spawn_event() {
    sm::GameState gs{};
    gs.worldSeed = 0x51515151u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(3, 0, 0);

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
        gs.worldTime = sm::world_time_at(day, 0, 0);
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
    gs.worldTime = sm::world_time_at(5, 0, 0);

    sm::Settlement city{};
    city.id = 7;
    city.name = "Same Id City";
    city.x = 20;
    city.y = 20;
    city.population = 500;
    city.mood = sm::SettlementMood::Stable;
    // Steer gen_delivery through the honest surface: tools are the town's
    // SCARCEST consumed good (bread plentiful, everything else stocked).
    city.inventory.add("bread", 2048);
    city.inventory.add("cloth", 128);
    city.inventory.add("bricks", 128);
    city.inventory.add("furniture", 128);
    city.inventory.add("jewelry", 128);
    city.inventory.add("carving", 128);
    city.inventory.add("statue", 128);
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

// Regression: Fisher-Yates OOB in shuffled_order (procedural.cpp). next_f01() is
// documented [0,1), but float(0xFFFFFFFF)/2^32 rounds up to exactly 1.0f, so
// int(f * (i + 1)) could equal i + 1 and swap with order[i + 1] -- one past the
// end of the 7-slot vector. The guard clamps j to i. Whatever the RNG yields,
// the result must remain a valid permutation of {0,1,2,3,4,5,6}.
//
// States 1584200935 and 22372349 are xorshift32 states whose FIRST draw makes
// next_f01() == 1.0f (1584200935 is the exact preimage of 0xFFFFFFFF), i.e. they
// drive i=6, j=7 before the clamp. Under a sanitizer the unpatched code aborts
// on these; otherwise the corrupted order[6] fails the permutation check.
bool test_shuffled_order_guards_rng_upper_bound() {
    auto is_permutation_0_6 = [](const std::vector<int>& order) -> bool {
        if (order.size() != 7) return false;
        bool seen[7] = {false, false, false, false, false, false, false};
        for (int v : order) {
            if (v < 0 || v > 6 || seen[v]) return false;
            seen[v] = true;
        }
        return true;
    };

    const std::uint32_t triggerStates[] = {1584200935u, 22372349u};
    for (std::uint32_t st : triggerStates) {
        sm::Rng rng(st);
        if (!is_permutation_0_6(sm::shuffled_order(rng))) {
            return fail("shuffled_order broke the {0..6} permutation at RNG max draw (OOB)");
        }
    }
    for (std::uint32_t seed = 1; seed <= 4096u; ++seed) {
        sm::Rng rng(seed);
        if (!is_permutation_0_6(sm::shuffled_order(rng))) {
            return fail("shuffled_order broke the {0..6} permutation on a normal seed");
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
    gs.worldTime = sm::world_time_at(0, 6, 0);
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
    settlement.inventory.add("bread", 2048);
    settlement.inventory.add("cloth", 128);
    settlement.inventory.add("bricks", 128);
    settlement.inventory.add("furniture", 128);
    settlement.inventory.add("jewelry", 128);
    settlement.inventory.add("carving", 128);
    settlement.inventory.add("statue", 128);
    gs.settlements.push_back(settlement);

    sm::Quest selected{};
    bool found = false;
    for (int day = 0; day < 256 && !found; ++day) {
        gs.worldTime = sm::world_time_at(day, 0, 0);
        const auto generated =
            sm::generate_quests_for_settlement(settlement, gs, gs.worldSeed);
        if (const sm::Quest* q = find_delivery_quest(generated, "tools")) {
            selected = *q;
            found = true;
        }
    }
    if (!found) {
        return fail("procedural generator did not produce economy-driven delivery quest") ? 0 : 1;
    }
    if (selected.objectives.empty()
        || selected.objectives.front().kind != sm::ObjectiveKind::DeliverItems
        || selected.objectives.front().itemId != "tools"
        || selected.objectives.front().quantity <= 0
        || selected.objectives.front().targetSettlementId != settlement.id) {
        return fail("selected delivery quest does not follow economy resource demand") ? 0 : 1;
    }

    const int startGold = gs.player.gold;
    const int rewardGold = gold_reward(selected);
    if (rewardGold <= 0) {
        return fail("selected generated delivery quest has no gold reward") ? 0 : 1;
    }
    gs.player.inventory.add("tools", selected.objectives.front().quantity);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;

    engine.accept(active, selected, gs.player, bus);
    if (active.size() != 1) {
        return fail("accept did not add quest to active list") ? 0 : 1;
    }
    if (!has_tag(bus, sm::EventTag::QuestAccepted)) {
        return fail("accept did not emit QuestAccepted") ? 0 : 1;
    }
    if (gs.player.gold != startGold) {
        return fail("accept applied reward before completion") ? 0 : 1;
    }

    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    engine.tick(active, bus, gs);
    if (!active.empty()) {
        return fail("delivery items did not complete generated quest") ? 0 : 1;
    }
    if (!has_tag(bus, sm::EventTag::QuestCompleted)) {
        return fail("completion did not emit QuestCompleted") ? 0 : 1;
    }

    sm::apply_events(bus.tick_events(), gs);
    if (!contains_completed_id(gs.player, selected.id)) {
        return fail("QuestCompleted was not applied to player completion state") ? 0 : 1;
    }
    if (gs.player.gold != startGold + rewardGold) {
        return fail("gold reward was not applied exactly once") ? 0 : 1;
    }

    bus.flush(gs.worldTime.day(), gs.worldTime.hour());
    sm::apply_events(bus.tick_events(), gs);
    if (gs.player.gold != startGold + rewardGold) {
        return fail("empty post-flush tick reapplied reward") ? 0 : 1;
    }

    if (!test_event_bus_contract_surface()) return 1;
    if (!test_ts_quest_tag_aliases()) return 1;
    if (!test_effect_applicator_ts_verbs()) return 1;
    if (!test_grant_xp_levels_through_the_one_path()) return 1;
    if (!test_grant_xp_pays_the_wis_dividend()) return 1;
    if (!test_quest_xp_reward_levels_the_player()) return 1;
    if (!test_builtin_nodes_are_registered_and_active()) return 1;
    if (!test_unhandled_tag_is_inert_in_applicator()) return 1;
    if (!test_settlement_show_dialog_node()) return 1;
    if (!test_logic_node_add_registers_inactive()) return 1;
    if (!test_logic_node_pending_ids_survive_node_add()) return 1;
    if (!test_logic_node_tick_order_matches_ts_set()) return 1;
    if (!test_logic_node_effect_can_remove_self()) return 1;
    if (!test_logic_node_self_reactivation_safe_cases()) return 1;
    if (!test_intro_show_story_node()) return 1;
    if (!test_encounter_table_shape()) return 1;
    if (!test_quest_failed_uses_failed_ledger()) return 1;
    if (!test_item_delivery_direct_path()) return 1;
    if (!test_quest_reward_dispatch_order_and_application()) return 1;
    if (!test_find_location_player_move_objective()) return 1;
    if (!test_visit_cell_objective()) return 1;
    if (!test_quest_completion_order_matches_ts_reverse_scan()) return 1;
    if (!test_wait_at_timeadvance_objective()) return 1;
    if (!test_destroy_npc_objective()) return 1;
    if (!test_interact_cell_objective()) return 1;
    if (!test_abandon_emits_and_removes()) return 1;
    if (!test_village_protect_generator_spawn_event()) return 1;
    if (!test_village_quest_ids_are_collision_safe()) return 1;
    if (!test_shuffled_order_guards_rng_upper_bound()) return 1;

    std::printf("OK quest_lifecycle_test id=%s item=%s qty=%d reward_gold=%d completed=%zu failed=%zu event_bus=ok quest_tags=ok effects=ok xp_effect=ok node_ids=ok level_event=ok level_dialog=ok settlement_dialog=ok settlement_enter=ok settlement_leave=ok logic_register=ok logic_rehash=ok logic_order=ok logic_self_remove=ok logic_self_reactivate=ok intro_story=ok chapter_placeholder=ok encounter_table=ok quest_failed=ok item_direct=ok reward_order=ok find_move=ok visit_cell=ok quest_order=ok wait_at=ok destroy_npc=ok interact_cell=ok abandon=ok village_protect=ok quest_id_scope=ok shuffle_guard=ok\n",
                selected.id.c_str(),
                selected.objectives.front().itemId.c_str(),
                selected.objectives.front().quantity,
                rewardGold,
                gs.player.completedQuestIds.size(),
                gs.player.failedQuestIds.size());
    return 0;
}
