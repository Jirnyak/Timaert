#include "check.h"

#include "content/quests/procedural.h"
#include "content/plot/chapter_1.h"
#include "content/plot/encounters.h"
#include "content/plot/intro.h"
#include "events/effect_applicator.h"
#include "events/event_bus.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
#include "events/quests/quest_engine.h"
#include "macro/agent_memory.h"
#include "macro/codex.h"
#include "macro/currency.h"

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

// THE fixture's container. The quest engine and the effect applicator are
// handed a bag now instead of reaching into PlayerState for one (the player's
// bag is an ordinary NpcInventory on his squad entity), so a headless test
// simply owns one. It is reset per scenario where a scenario cares.
sm::Inventory bag{};
// The player's head: an unpayable fine is written down as a debt fact, and the
// engine is handed the container instead of reaching into PlayerState — the
// same reason it is handed `bag`.
sm::AgentMemory head{};

const sm::Quest* find_delivery_quest(const std::vector<sm::Quest>& quests,
                                     const char* itemId) {
    for (const auto& q : quests) {
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

// Is this offer's provenance triple in the settled memory? (The engine's
// is_known checks active ∪ settled; this looks at the settled half alone.)
bool offer_settled(const sm::PlayerState& p, const sm::Quest& q) {
    for (const auto& so : p.settledQuestOffers) {
        if (so.giverSettlementId == q.giverSettlementId
            && so.offerSlot == q.offerSlot
            && so.bornDay == q.bornDay) return true;
    }
    return false;
}

void apply_pending(sm::EventBus& bus, sm::GameState& gs, std::size_t& applied) {
    while (true) {
        const auto& events = bus.tick_events();
        if (applied >= events.size()) return;

        const std::size_t begin = applied;
        const std::size_t end = events.size();
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        sm::apply_events(pending, gs, &bag);
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

void test_event_bus_contract_surface() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::EventBus bus;
    CHECK_OR_RETURN(!(bus.tick() != 0
        || bus.subscription_count() != 0
        || !bus.tick_events().empty()
        || !bus.last_tick_events().empty()),
        "EventBus did not start empty");

    int customSeen = 0;
    int customSum = 0;
    const std::uint32_t subId = bus.on(sm::EventTag::Custom, [&](const sm::GameEvent& ev) {
        ++customSeen;
        customSum += ev.ix;
    });
    CHECK_OR_RETURN(!(subId == 0 || bus.subscription_count() != 1),
        "EventBus subscription state is incorrect");

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

    CHECK_OR_RETURN(!(customSeen != 2 || customSum != 3),
        "EventBus listeners did not receive emit_all events");
    // The consumer contract is POLLING: scan tick_events() like production
    // does (the per-tick query helpers were deleted as zero-caller API).
    CHECK_OR_RETURN(!(bus.tick_events().size() != 3
        || count_tag(bus, sm::EventTag::Custom) != 2),
        "EventBus tick buffer did not retain emitted events");
    std::vector<const sm::GameEvent*> matches;
    for (const auto& ev : bus.tick_events())
        if (ev.tag == sm::EventTag::Custom) matches.push_back(&ev);
    CHECK_OR_RETURN(!(matches.size() != 2
        || matches[0]->s1 != "first" || matches[0]->ix != 1
        || matches[1]->s1 != "second"),
        "EventBus tick order does not match emit order");

    bus.flush();
    CHECK_OR_RETURN(!(bus.tick() != 1
        || !bus.tick_events().empty()
        || bus.last_tick_events().size() != 3),
        "EventBus flush did not promote tick events to last");

    bus.unsubscribe(subId);
    CHECK_OR_RETURN(!(bus.subscription_count() != 0),
        "EventBus unsubscribe did not remove listener");
    sm::GameEvent silent{sm::EventTag::Custom};
    silent.s1 = "silent";
    silent.ix = 7;
    bus.emit(silent);
    CHECK_OR_RETURN(!(customSeen != 2 || customSum != 3),
        "EventBus unsubscribed listener still received events");

    // No subscriber, still a fact of the frame: the poll side (tick_events /
    // last_tick_events promotion) must carry it. The bus's history ring is
    // gone — the past lives in the chronicle/journal (CANON S20.1).
    bus.flush();
    CHECK_OR_RETURN(!(bus.tick() != 2
        || bus.last_tick_events().size() != 1
        || bus.last_tick_events()[0].tag != sm::EventTag::Custom
        || bus.last_tick_events()[0].s1 != "silent"),
        "EventBus flush lost an event no subscriber was watching");
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
    CHECK_OR_RETURN(!(firstOrder != 1
        || lateOrder != 2
        || addDuringEmitBus.subscription_count() != 2),
        "EventBus does not match TS live listener append order");

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
    CHECK_OR_RETURN(!(removerSeen != 1
        || removedSeen != 0
        || removeDuringEmitBus.subscription_count() != 1),
        "EventBus does not match TS listener removal during emit");

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
    CHECK_OR_RETURN(!(firstCustomSeen != 1
        || secondCustomSeen != 1
        || unrelatedRemoveBus.subscription_count() != 2),
        "EventBus unrelated-tag unsubscribe affected current tag dispatch");

    sm::EventBus resetBus;
    int resetSeen = 0;
    resetBus.on(sm::EventTag::Custom, [&](const sm::GameEvent&) {
        ++resetSeen;
        resetBus.reset();
    });
    resetBus.emit(first);
    CHECK_OR_RETURN(!(resetSeen != 1
        || resetBus.tick() != 0
        || resetBus.subscription_count() != 0
        || !resetBus.tick_events().empty()
        || !resetBus.last_tick_events().empty()),
        "EventBus reset during listener dispatch did not leave clean state");

    bus.reset();
    CHECK_OR_RETURN(!(bus.tick() != 0
        || bus.subscription_count() != 0
        || !bus.tick_events().empty()
        || !bus.last_tick_events().empty()),
        "EventBus reset did not clear public state");
}

void test_quest_accept_event_order() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::Quest q{};
    q.title = "Alias";
    sm::GameEvent onAccept{sm::EventTag::SpawnEntity};
    onAccept.s1 = "bandit";
    q.onAccept.push_back(onAccept);
    sm::EventBus bus;
    sm::QuestEngine engine;
    sm::GameState gs{};
    std::vector<sm::Quest> active;
    std::size_t activeDuringOnAccept = 0;
    std::size_t activeDuringQuestStart = 0;
    bus.on(sm::EventTag::SpawnEntity, [&](const sm::GameEvent&) {
        activeDuringOnAccept = active.size();
    });
    bus.on(sm::EventTag::QuestStart, [&](const sm::GameEvent&) {
        activeDuringQuestStart = active.size();
    });
    engine.accept(active, q, gs, bus);
    CHECK_OR_RETURN(!(count_tag(bus, sm::EventTag::QuestStart) == 0),
        "QuestEngine::accept did not emit QuestStart");
    CHECK_OR_RETURN(!(activeDuringOnAccept != 1 || activeDuringQuestStart != 1),
        "QuestEngine::accept emitted events before activeQuests push");
    CHECK_OR_RETURN(!(bus.tick_events().size() != 2
        || bus.tick_events()[0].tag != sm::EventTag::SpawnEntity
        || bus.tick_events()[1].tag != sm::EventTag::QuestStart
        || bus.tick_events()[1].a != active[0].ordinal
        || bus.tick_events()[1].s2 != q.title),
        "QuestEngine::accept did not emit onAccept before QuestStart");
    // Accepting issues the identity: monotonic ordinals from the ONE issuer,
    // starting at 1 (0 stays reserved for "an offer not yet accepted").
    CHECK_OR_RETURN(!(active[0].ordinal != 1u),
        "the first accepted quest did not draw ordinal 1");
    engine.accept(active, q, gs, bus);
    CHECK_OR_RETURN(!(active.size() != 2 || count_tag(bus, sm::EventTag::QuestStart) != 2),
        "QuestEngine::accept deduped a quest unlike TS accept()");
    CHECK_OR_RETURN(!(active[1].ordinal != 2u
        || gs.nextQuestOrdinal != 3u),
        "quest ordinals are not monotonic from the one issuer");
}

void test_effect_applicator_ts_verbs() {
    bag.clear();
    head = sm::AgentMemory{};
    // The player's standing lives in the relation matrix now, so the verb test
    // drives a whole GameState; `player` stays a reference for readability.
    sm::GameState verbState{};
    sm::PlayerState& player = verbState.player;
    bag.add("coin_empire", 10);
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

    // `damage_hp` is GONE, by ruling: a verb that subtracted the player's
    // health from a plot file was a second damage system reachable by exactly
    // one body in the world. It is fired here anyway, as an unknown verb, to
    // prove the applicator ignores what it does not know instead of guessing.
    sm::GameEvent razed{sm::EventTag::ApplyEffect};
    razed.s1 = "damage_hp";
    razed.ix = 17;
    events.push_back(razed);

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
    codex.a = std::uint32_t(sm::CodexArticleId::Witches);
    events.push_back(codex);
    events.push_back(codex);
    // An out-of-range article ordinal is ignored, not a stray bit.
    sm::GameEvent codexBad{sm::EventTag::CodexUnlock};
    codexBad.a = 63u;
    events.push_back(codexBad);

    sm::GameEvent complete{sm::EventTag::QuestComplete};
    complete.a = 101u;
    events.push_back(complete);
    events.push_back(complete);

    sm::GameEvent failQuest{sm::EventTag::QuestFail};
    failQuest.a = 102u;
    events.push_back(failQuest);
    events.push_back(failQuest);

    sm::apply_events(events, verbState, &bag);

    // Money is coin now: the wallet drains to ZERO and cannot go negative —
    // the uncovered remainder of a penalty is a DEBT FACT, not a negative
    // number (owner's ruling; the event verb spends what the wallet holds).
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != 0),
        "PlayerGoldChange did not drain the wallet");
    // HP is 50 rather than 33: the razed verb took nothing, which is the
    // point of razing it.
    CHECK_OR_RETURN(!(player.combatStats.currentHp != 50
        || player.combatStats.currentMp != 25
        || player.combatStats.currentSp != 26),
        "ApplyEffect hp/mp/sp verbs produced wrong combat stats");
    CHECK_OR_RETURN(!(player.sheet.levelData.exp != 42 || player.sheet.levelData.level != 1),
        "grant_xp did not apply XP without direct level mutation");
    CHECK_OR_RETURN(!(sm::player_reputation(&verbState, "guild") != 3),
        "ReputationChange did not move the player's row in the matrix");
    // The unlock is a bit per ordinal: emitting twice sets it once, and the
    // out-of-range ordinal above set nothing.
    CHECK_OR_RETURN(!(player.codexUnlockedBits
            != sm::codex_bit(sm::CodexArticleId::Witches)),
        "CodexUnlock bits are not exactly the one emitted article");
    // Lifetime tallies: two external completions, two external failures —
    // the honest split (the old string ledgers filed a failure into BOTH).
    CHECK_OR_RETURN(!(player.completedQuestCount != 2u
        || player.failedQuestCount != 2u),
        "quest completion/failure tallies did not count external events");

    // NO PLOT FILE CAN WOUND ANYBODY (owner, 2026-08-27). The verb is gone,
    // and the road it used is closed too: an instant bonus may not drive HP
    // down, because a wound is a blow and blows have exactly one door.
    sm::PlayerState hurtMe{};
    hurtMe.combatStats.currentHp = 7;
    hurtMe.combatStats.maxHp = 50;
    sm::GameEvent lethal{sm::EventTag::ApplyEffect};
    lethal.s1 = "damage_hp";
    lethal.ix = 10;
    sm::GameState lethalState{};
    lethalState.player = hurtMe;
    sm::apply_events(std::span<const sm::GameEvent>(&lethal, 1), lethalState, &bag);
    CHECK_OR_RETURN(!(lethalState.player.combatStats.currentHp != 7),
        "a razed verb must do NOTHING, not something smaller");
    // ...and the registry itself refuses the same thing by the other road,
    // so restoring the verb would not restore the hole.
    int hp = 7;
    sm::PoolSlice pools{};
    pools.current[int(sm::PoolId::Hp)] = &hp;
    pools.maximum[int(sm::PoolId::Hp)] = 50;
    CHECK_OR_RETURN(!(sm::apply_instant(pools, {std::uint8_t(sm::BonusId::HealHp), -10}) != 0
        || hp != 7),
        "an instant bonus must not wound: blows have one door");
    // Negative control: the SAME row still heals, so the refusal above is a
    // direction and not a dead door.
    CHECK_OR_RETURN(!(sm::apply_instant(pools, {std::uint8_t(sm::BonusId::HealHp), 10}) != 10
        || hp != 17),
        "the healing direction of the row must still work");

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
void test_grant_xp_levels_through_the_one_path() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState xpState{};
    sm::PlayerState& player = xpState.player;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.attributes[sm::AttributeId::Wis] = 0;  // isolate from the wis dividend (own test)
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
    CHECK_OR_RETURN(!(player.sheet.levelData.level != 2),
        "grant_xp left the player below a threshold he had passed");
    CHECK_OR_RETURN(!(player.sheet.levelData.exp != 10),
        "the remainder past the threshold was not carried");
    CHECK_OR_RETURN(!(player.sheet.levelData.expToNext != sm::exp_to_next_level(2)),
        "the next threshold was not recomputed for the new level");
    CHECK_OR_RETURN(!(player.sheet.levelData.attributePoints
            != sm::default_level_data().attributePoints + 1
        || player.sheet.levelData.skillPoints
            != sm::default_level_data().skillPoints + 1),
        "levelling through grant_xp did not pay its points (1:1 per level)");
}

// The wis dividend (owner ruling 2026-08-05): every XP grant scales by the
// recipient's expMult (+1% per wis point). Before the wiring, wis was a
// dead attribute — computed, displayed, consumed by nothing.
void test_grant_xp_pays_the_wis_dividend() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState wisState{};
    sm::PlayerState& player = wisState.player;
    player.sheet.levelData = sm::default_level_data();
    player.sheet.attributes[sm::AttributeId::Wis] = 10;  // expMult = 1.10

    sm::GameEvent grant{sm::EventTag::ApplyEffect};
    grant.s1 = "grant_xp";
    grant.ix = 100;
    sm::apply_events(std::span<const sm::GameEvent>(&grant, 1), wisState, &bag);
    if (player.sheet.levelData.exp != 110) {
        std::fprintf(stderr, "exp=%d (expected 110)\n",
                     player.sheet.levelData.exp);
    }
    CHECK(player.sheet.levelData.exp == 110,
          "grant_xp ignored the wis expMult");
}

// A quest that pays experience must pay the LEVEL it is worth. This is the bug
// the audit named: the reward did a bare `exp +=`, and the only code that ever
// consumed the pool lived in the subworld kill path — so contract experience
// piled up unspendable, and a quest-only playthrough never levelled at all.
void test_quest_xp_reward_levels_the_player() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.player.sheet.levelData = sm::default_level_data();
    gs.player.sheet.attributes[sm::AttributeId::Wis] = 0;  // isolate from the wis dividend

    // Worth three thresholds at once — a chapter reward at low level does this,
    // and one level-up per grant would silently swallow the rest.
    const int l1 = sm::exp_to_next_level(1);
    const int l2 = sm::exp_to_next_level(2);
    const int l3 = sm::exp_to_next_level(3);

    sm::Quest q{};
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
    engine.tick(active, bus, gs, &bag, &head);

    CHECK_OR_RETURN(active.empty(), "the reward quest did not complete");
    CHECK_OR_RETURN(!(gs.player.sheet.levelData.level != 4),
        "quest XP did not level the player (or stopped at one level)");
    CHECK_OR_RETURN(!(gs.player.sheet.levelData.exp != 7),
        "the remainder past the last threshold was not carried");
}

// Every builtin node is registered AND active. The name and the three
// assertions below used to measure this against the TypeScript registry, an
// implementation that no longer exists and that the owner has ruled we owe no
// parity to — so a node deleted in C++ for a C++ reason read as "drifted from
// TS". It now states the C++ contract on its own terms.
//
// sys_level_up is deliberately absent: it waited on EventTag::PlayerLevelUp,
// which nothing has ever emitted, so it could not fire (removed 2026-08-05).
void test_builtin_nodes_are_registered_and_active() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::LogicNodeEngine logic;
    sm::register_builtin_nodes(logic);
    const char* kBuiltinIds[] = {"sys_settlement"};
    const int kBuiltinCount = int(sizeof(kBuiltinIds) / sizeof(kBuiltinIds[0]));
    CHECK_OR_RETURN(!(logic.node_count() != kBuiltinCount
        || logic.active_count() != kBuiltinCount),
        "builtin logic node count is not the registered set");
    for (const char* id : kBuiltinIds) {
        CHECK_OR_RETURN(logic.has(id), "a builtin logic node is missing");
        CHECK_OR_RETURN(logic.is_active(id), "a builtin logic node is inactive");
    }
}

// (Was test_player_level_up_event_is_presentation_only, exercising the
// PlayerLevelUp tag.) That tag is DELETED now; the surviving contract is the
// general one: a tag the applicator has no arm for passes through without
// mutating the player. Custom is the permanent such tag.
void test_unhandled_tag_is_inert_in_applicator() {
    bag.clear();
    head = sm::AgentMemory{};
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
    sm::apply_events(std::span<const sm::GameEvent>(&unhandled, 1), levelState, &bag);
    CHECK_OR_RETURN(!(player.sheet.levelData.level != beforeLevel
        || player.sheet.levelData.exp != beforeExp
        || player.sheet.levelData.expToNext != beforeExpToNext
        || player.sheet.levelData.attributePoints != beforeAttributePoints
        || player.combatStats.currentHp != beforeHp
        || player.combatStats.maxHp != beforeMaxHp),
        "an unhandled tag mutated player inside effect applicator");
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

void test_settlement_show_dialog_node() {
    bag.clear();
    head = sm::AgentMemory{};
    const auto run_case = [](sm::EventTag tag, const char* name) {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;
        sm::register_builtin_nodes(logic);

        sm::GameEvent visit{tag};
        visit.s1 = name;
        bus.emit(visit);
        bus.flush();
        logic.tick(bus, player);

        const sm::GameEvent* dialog = find_tag(bus, sm::EventTag::ShowDialog);
        CHECK_OR_RETURN(!(!dialog),
            "settlement enter event did not emit ShowDialog through sys_settlement");
        const std::string expectedTitle = std::string("Welcome to ") + name;
        CHECK_OR_RETURN(!(dialog->s1 != expectedTitle
            || dialog->s2.find("The gates open before you") == std::string::npos
            || dialog->ix != 1),
            "sys_settlement ShowDialog payload does not match TS node");
    };

    run_case(sm::EventTag::SettlementVisit, "Round City");
    run_case(sm::EventTag::PlayerEnterSettlement, "TS City");
    {
        sm::PlayerState player{};
        sm::EventBus bus;
        sm::LogicNodeEngine logic;
        sm::register_builtin_nodes(logic);

        sm::GameEvent leave{sm::EventTag::PlayerLeaveSettlement};
        leave.s1 = "TS City";
        bus.emit(leave);
        bus.flush();
        logic.tick(bus, player);
        CHECK_OR_RETURN(!(has_tag(bus, sm::EventTag::ShowDialog)),
            "PlayerLeaveSettlement must not trigger sys_settlement dialog");
    }
}

void test_logic_node_add_registers_inactive() {
    bag.clear();
    head = sm::AgentMemory{};
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

    CHECK_OR_RETURN(!(logic.node_count() != 1 || logic.active_count() != 0),
        "LogicNodeEngine add did not register inactive node");
    logic.tick(bus, player);
    CHECK_OR_RETURN(!(has_tag(bus, sm::EventTag::Custom)),
        "inactive registered node fired before activate");

    logic.activate("inactive");
    logic.tick(bus, player);
    const sm::GameEvent* ev = find_tag(bus, sm::EventTag::Custom);
    CHECK_OR_RETURN(!(!ev || ev->s1 != "inactive"),
        "activated registered node did not fire");
}

void test_logic_node_pending_ids_survive_node_add() {
    bag.clear();
    head = sm::AgentMemory{};
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
    CHECK_OR_RETURN(!(fired != kObserverCount),
        "LogicNodeEngine skipped pending nodes after add_node rehash");
    CHECK_OR_RETURN(!(!logic.is_consistent()),
        "LogicNodeEngine active set became inconsistent after add_node");
    CHECK_OR_RETURN(!(logic.active_count() != 0),
        "add_node activated dynamically registered nodes");
}

void test_logic_node_tick_order_matches_ts_set() {
    bag.clear();
    head = sm::AgentMemory{};
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

    CHECK_OR_RETURN(!(bus.tick_events().size() != 2
        || bus.tick_events()[0].s1 != "first"
        || bus.tick_events()[1].s1 != "second"),
        "LogicNodeEngine tick order/interleaving does not match TS Set semantics");
    CHECK_OR_RETURN(!(logic.active_count() != 0 || !logic.is_consistent()),
        "LogicNodeEngine active state after ordered tick is inconsistent");
}

void test_logic_node_effect_can_remove_self() {
    bag.clear();
    head = sm::AgentMemory{};
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
    CHECK_OR_RETURN(!(!ev || ev->s1 != "self_remove"),
        "LogicNodeEngine self-removing node did not fire");
    CHECK_OR_RETURN(!(logic.node_count() != 0 || logic.active_count() != 0),
        "LogicNodeEngine self-removing node stayed registered");
    CHECK_OR_RETURN(!(!logic.is_consistent()),
        "LogicNodeEngine inconsistent after self-removing node");
}

void test_logic_node_self_reactivation_safe_cases() {
    bag.clear();
    head = sm::AgentMemory{};
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
        CHECK_OR_RETURN(!(count_tag(bus, sm::EventTag::Custom) != 1
            || logic.active_count() != 0
            || logic.is_active("self_activate")),
            "LogicNodeEngine direct self-activate did not match TS Set no-op");
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
        CHECK_OR_RETURN(!(count_tag(bus, sm::EventTag::Custom) != 1
            || logic.active_count() != 1
            || !logic.is_active("self_next")),
            "LogicNodeEngine self next-link did not stay active for next tick");

        bus.flush();
        logic.tick(bus, player);
        CHECK_OR_RETURN(!(count_tag(bus, sm::EventTag::Custom) != 1
            || !logic.is_active("self_next")),
            "LogicNodeEngine self next-link did not fire once per tick");
    }

}

void test_intro_show_story_node() {
    bag.clear();
    head = sm::AgentMemory{};
    // The intro is PURE SLIDES since 2026-09-03: the asking (sex, name,
    // homeland) moved to the pre-world creation screen, which renders the
    // same authored choice tables through creation_*_choices — pinned here so
    // the screen cannot lose a row the story used to offer.
    const sm::content::StoryDef& story = sm::content::intro_story();
    CHECK_OR_RETURN(!(std::string(story.id) != "intro"
        || std::string(story.sourceNodeId) != "intro_main"
        || story.slideCount != 9
        || story.slides == nullptr),
        "intro story table identity does not match the slides-only intro");
    // The world's own opening: one arrival slide through the same channel
    // (the nine intro slides play pre-world on the IntroSlides screen).
    const sm::content::StoryDef& arrival = sm::content::arrival_story();
    CHECK_OR_RETURN(!(std::string(arrival.id) != "arrival"
        || std::string(arrival.sourceNodeId) != "intro_main"
        || arrival.slideCount != 1
        || arrival.slides == nullptr),
        "arrival story table identity does not match the one-slide opening");
    std::size_t sexCount = 0;
    (void)sm::content::creation_sex_choices(sexCount);
    CHECK_OR_RETURN(sexCount == 2,
        "the creation screen's sex table lost the authored rows");
    std::size_t realmCount = 0;
    (void)sm::content::creation_realm_choices(realmCount);
    CHECK_OR_RETURN(realmCount == 3,
        "the creation screen's homeland table lost the authored rows");

    sm::PlayerState player{};
    sm::EventBus bus;
    sm::LogicNodeEngine logic;
    sm::content::register_intro_story_nodes(logic);
    CHECK_OR_RETURN(!(logic.node_count() != 2 || logic.active_count() != 1),
        "plot node registration did not match TS plot index");
    CHECK_OR_RETURN(!(!logic.has("intro_main")
        || !logic.has(sm::content::kChapter1NodeId)
        || !logic.is_active("intro_main")
        || logic.is_active(sm::content::kChapter1NodeId)),
        "plot node ids/initial activation do not match TS plot registry");

    logic.tick(bus, player);

    const sm::GameEvent* event = find_tag(bus, sm::EventTag::ShowStory);
    CHECK_OR_RETURN(!(!event),
        "intro_main did not emit ShowStory");
    CHECK_OR_RETURN(!(event->s1 != "intro_main"
        || event->s2 != "arrival"
        || event->ix != 1),   // the slide count — the story IS its slides now
        "ShowStory flat payload does not match the one-slide arrival");

    bus.flush();
    logic.tick(bus, player);
    CHECK_OR_RETURN(!(has_tag(bus, sm::EventTag::ShowStory)),
        "intro_main ShowStory fired more than once without reactivation");

    logic.activate(sm::content::kChapter1NodeId);
    CHECK_OR_RETURN(!(logic.active_count() != 1
        || !logic.is_active(sm::content::kChapter1NodeId)),
        "chapter 1 placeholder did not activate after intro result");
    bus.flush();
    logic.tick(bus, player);
    CHECK_OR_RETURN(!(!logic.is_consistent() || logic.active_count() != 1),
        "chapter 1 placeholder did not remain dormant and active");
    CHECK_OR_RETURN(!(!bus.tick_events().empty()),
        "chapter 1 placeholder emitted events despite false TS condition");
}

void test_encounter_table_shape() {
    bag.clear();
    head = sm::AgentMemory{};
    const auto& table = sm::content::encounters();
    CHECK_OR_RETURN(!(table.size() != 15),
        "encounter table count does not match TS buildEncounterTable");

    const auto& hidden = table[0];
    CHECK_OR_RETURN(!(hidden.title != "Hidden Cache"
        || hidden.choices.size() != 2
        || hidden.choices[0].effects.size() != 1
        || hidden.choices[0].effects[0].tag != sm::EventTag::PlayerGoldChange
        || hidden.choices[0].effects[0].ix < 15
        || hidden.choices[0].effects[0].ix > 44),
        "Hidden Cache encounter does not match TS gold branch");

    const auto& campfire = table[2];
    CHECK_OR_RETURN(!(campfire.title != "Abandoned Campfire"
        || campfire.choices.size() != 2
        || campfire.choices[0].effects.size() != 2
        || campfire.choices[0].effects[0].s1 != "restore_sp"
        || campfire.choices[0].effects[1].s1 != "heal_hp"
        || campfire.choices[1].effects.size() != 1
        || campfire.choices[1].effects[0].tag != sm::EventTag::PlayerGoldChange
        || (campfire.choices[1].effects[0].ix != 0
            && campfire.choices[1].effects[0].ix != 25)),
        "Abandoned Campfire encounter does not match TS branches");

    const auto& merchant = table[3];
    CHECK_OR_RETURN(!(merchant.choices.size() != 3
        || merchant.choices[1].effects.size() != 1
        || merchant.choices[1].effects[0].tag != sm::EventTag::BattleStart
        || merchant.choices[1].effects[0].s1 != "Angry Merchant"
        || merchant.choices[1].effects[0].s2 != "merchant"
        || merchant.choices[1].effects[0].ix != 3),
        "Traveling Merchant battle branch does not match TS");

    const auto& shrine = table[11];
    CHECK_OR_RETURN(!(shrine.title != "Mysterious Shrine"
        || shrine.choices.size() != 2
        || shrine.choices[0].effects.size() != 2
        || shrine.choices[0].effects[0].s1 != "restore_hp"
        || shrine.choices[0].effects[1].s1 != "restore_mp"
        || shrine.choices[1].effects.size() != 1),
        "Mysterious Shrine structure does not match TS");
    const sm::GameEvent& offering = shrine.choices[1].effects[0];
    const bool offeringGold =
        offering.tag == sm::EventTag::PlayerGoldChange && offering.ix == 50;
    const bool offeringDamage =
        offering.tag == sm::EventTag::ApplyEffect
        && offering.s1 == "damage_hp"
        && offering.ix == 25;
    CHECK_OR_RETURN(!(!offeringGold && !offeringDamage),
        "Mysterious Shrine offering is not a legal TS branch");

    const auto& monolith = table[12];
    CHECK_OR_RETURN(!(monolith.choices.size() != 2
        || monolith.choices[0].effects.size() != 2
        || monolith.choices[0].effects[0].tag != sm::EventTag::CodexUnlock
        || monolith.choices[0].effects[0].a
            != std::uint32_t(sm::CodexArticleId::Cosmology)
        || monolith.choices[1].effects.size() != 2
        || monolith.choices[1].effects[0].tag != sm::EventTag::ReputationChange
        || monolith.choices[1].effects[0].s1 != "empire"
        || monolith.choices[1].effects[1].s1 != "cults"),
        "Black Monolith encounter does not match TS effects");

    const auto& witch = table[14];
    CHECK_OR_RETURN(!(witch.title != "Witch's Hut"
        || witch.choices.size() != 3
        || witch.choices[1].effects.size() != 1
        || witch.choices[1].effects[0].tag != sm::EventTag::BattleStart
        || witch.choices[1].effects[0].s1 != "Forest Witch"
        || witch.choices[1].effects[0].s2 != "witch"
        || witch.choices[1].effects[0].ix != 7),
        "Witch encounter battle branch does not match TS");
}

// A test_random_encounter_logic_node lived here and drove the "enc_random"
// node: walk far enough, roll a private die, get a uniformly random list row.
// Removed with the node (owner ruling 2026-08-05: events come from game
// context and state, never an unconditional random roll over a list).

void test_quest_failed_settles_its_offer() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(10, 6, 0);
    gs.player.x = 1.0f;
    gs.player.y = 1.0f;

    sm::Quest q{};
    q.ordinal = 42u;
    q.giverSettlementId = 5;
    q.offerSlot = 2;
    q.bornDay = 10;          // offered TODAY, expired yesterday
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
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty()),
        "expired quest was not removed");
    CHECK_OR_RETURN(!(!has_tag(bus, sm::EventTag::QuestFail)),
        "expired quest did not emit QuestFail");
    CHECK_OR_RETURN(!(has_tag(bus, sm::EventTag::QuestComplete)),
        "expired quest completed instead of failing first");
    const sm::GameEvent* failed = find_tag(bus, sm::EventTag::QuestFail);
    CHECK_OR_RETURN(!(!failed
        || failed->a != q.ordinal
        || failed->s2 != "expired"
        || failed->b != sm::kEventEffectAlreadyApplied),
        "expired quest did not carry ordinal + reason");
    CHECK_OR_RETURN(!(gs.player.failedQuestCount != 1u
        || gs.player.completedQuestCount != 0u
        || !offer_settled(gs.player, q)),
        "expiry did not settle the offer / count the failure honestly");

    sm::apply_events(bus.tick_events(), gs, &bag);
    CHECK_OR_RETURN(!(gs.player.failedQuestCount != 1u),
        "an already-applied QuestFail was double-counted");
    CHECK_OR_RETURN(!(!engine.is_known(active, gs.player, q)),
        "the settled offer is not treated as known");
    // NEGATIVE CONTROL: a different provenance is NOT known — the dedup
    // really compares the triple, not "anything settled today".
    sm::Quest other = q;
    other.offerSlot = 3;
    CHECK_OR_RETURN(!(engine.is_known(active, gs.player, other)),
        "a different offer slot reads as known: dedup is not by provenance");

    // The day turns: this offer can never be generated again (its bornDay is
    // part of its identity), so the settled memory prunes itself.
    gs.worldTime = sm::world_time_at(11, 6, 0);
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!gs.player.settledQuestOffers.empty()),
        "yesterday's settled offer was not pruned with its day");
}

void test_item_delivery_direct_path() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;
    bag.add("wood", 3);

    sm::Landmark settlement{};
    settlement.type = sm::LandmarkType::City;
    settlement.id = 7;
    settlement.name = "Test Anchorage";
    settlement.x = 12;
    settlement.y = 18;
    settlement.population = 1000;
    settlement.mood = sm::SettlementMood::Stable;
    settlement.kingdomIdx = 0;
    gs.landmarks.push_back(settlement);

    sm::Quest q{};
    q.ordinal = 7u;
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
    engine.tick(active, bus, gs, &bag, &head);

    CHECK_OR_RETURN(!(!active.empty()),
        "delivery quest did not complete from inventory condition");
    CHECK_OR_RETURN(!(bag.count("wood") != 1),
        "delivery did not remove delivered items");
    CHECK_OR_RETURN(!(bag.count("misc_gem") != 2),
        "item reward did not grant item reward");

    std::size_t applied = 0;
    apply_pending(bus, gs, applied);
    CHECK_OR_RETURN(!(bag.count("wood") != 1
        || bag.count("misc_gem") != 2),
        "event application duplicated direct inventory mutation");
    CHECK_OR_RETURN(!(gs.player.completedQuestCount != 1u),
        "delivery completion was not applied");
}

void test_quest_reward_dispatch_order_and_application() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 10.0f;
    gs.player.y = 10.0f;
    bag.add("coin_empire", 20);
    gs.player.sheet.levelData = sm::default_level_data();
    gs.player.sheet.levelData.exp = 0;

    sm::Quest q{};
    q.ordinal = 9u;
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
        goldSeenByListener = sm::wallet_value(bag);
        completedDuringGold = int(gs.player.completedQuestCount);
    });
    bus.on(sm::EventTag::ReputationChange, [&](const sm::GameEvent&) {
        reputationSeenByListener = sm::player_reputation(&gs, "guild");
    });
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);
    engine.tick(active, bus, gs, &bag, &head);

    const auto& events = bus.tick_events();
    CHECK_OR_RETURN(!(!active.empty() || events.size() != 4),
        "quest reward dispatch did not emit expected event count");
    CHECK_OR_RETURN(!(events[0].tag != sm::EventTag::PlayerGoldChange
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
        || events[3].a != q.ordinal
        || events[3].b != sm::kEventEffectAlreadyApplied),
        "quest reward dispatch order does not match TS reward-before-complete flow");
    CHECK_OR_RETURN(!(goldSeenByListener != 27
        || completedDuringGold != 1
        || reputationSeenByListener != 3),
        "quest reward listeners did not see TS direct state mutation");
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || bag.count("misc_gem") != 2
        || gs.player.completedQuestCount != 1u),
        "quest rewards did not mutate state directly like TS");

    std::size_t applied = 0;
    apply_pending(bus, gs, applied);
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || bag.count("misc_gem") != 2
        || gs.player.completedQuestCount != 1u),
        "quest reward events duplicated direct TS state");

    apply_pending(bus, gs, applied);
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != 27
        || gs.player.sheet.levelData.exp != 11
        || sm::player_reputation(&gs, "guild") != 3
        || bag.count("misc_gem") != 2
        || gs.player.completedQuestCount != 1u),
        "quest reward events reapplied after pending cursor advanced");
}

void test_find_location_player_move_objective() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

    sm::Quest q{};
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
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty()),
        "PlayerMove did not complete FindLocation");
}

void test_visit_cell_objective() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 40.0f;
    gs.player.y = 50.0f;

    sm::Quest q{};
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
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty() || !has_tag(bus, sm::EventTag::QuestComplete)),
        "VisitCell did not complete from player radius");
}

void test_quest_completion_order_matches_ts_reverse_scan() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 10.0f;
    gs.player.y = 10.0f;

    auto make_visit = [](std::uint32_t ordinal, const char* title) {
        sm::Quest q{};
        q.ordinal = ordinal;
        q.title = title;
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
    active.push_back(make_visit(1u, "q_low"));
    active.push_back(make_visit(2u, "q_high"));

    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);

    std::vector<std::uint32_t> completed;
    for (const auto& ev : bus.tick_events()) {
        if (ev.tag == sm::EventTag::QuestComplete) {
            completed.push_back(ev.a);
        }
    }
    CHECK_OR_RETURN(!(!active.empty()
        || completed.size() != 2
        || completed[0] != 2u
        || completed[1] != 1u),
        "QuestEngine completion order does not match TS reverse scan");
}

void test_wait_at_timeadvance_objective() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;

    sm::Quest q{};
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
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(active.empty() || has_tag(bus, sm::EventTag::QuestComplete)
        || active[0].objectives[0].hoursWaited != 1),
        "WaitAt treated one TimeAdvance event as more than one TS hour");

    sm::GameEvent oneHour{sm::EventTag::TimeAdvance};
    oneHour.ix = 1;
    bus.emit(oneHour);
    sm::GameEvent anotherHour{sm::EventTag::TimeAdvance};
    anotherHour.ix = 1;
    bus.emit(anotherHour);
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty() || !has_tag(bus, sm::EventTag::QuestComplete)),
        "WaitAt did not complete after required TimeAdvance");
}

void test_destroy_npc_objective() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

    sm::Quest q{};
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
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(active.size() != 1),
        "DestroyNpc counted an entity handle / a kindless body as a kill");
    CHECK_OR_RETURN(!(active[0].objectives[0].killed != 1),
        "DestroyNpc tally is not one kill after one real kill");

    // The second real kill of that type completes it.
    sm::GameEvent secondReal{sm::EventTag::NpcDeath};
    secondReal.a = 77;
    secondReal.ix = 2;
    bus.emit(secondReal);
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty() || !has_tag(bus, sm::EventTag::QuestComplete)),
        "DestroyNpc did not complete on kills of the wanted type");
}

void test_interact_cell_objective() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);

    sm::Quest q{};
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
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(active.size() != 1),
        "InteractCell completed on an event from a different cell");

    sm::GameEvent edit{sm::EventTag::WorldCellChange};
    edit.ix = 9;
    edit.iy = 11;
    bus.emit(edit);
    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty() || !has_tag(bus, sm::EventTag::QuestComplete)),
        "InteractCell did not consume WorldCellChange payload");
}

void test_abandon_emits_and_removes() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::Quest q{};
    q.ordinal = 13u;
    q.giverSettlementId = 4;
    q.offerSlot = 1;
    q.bornDay = 3;
    q.title = "Abandon Test";
    q.description = "Test QuestEngine::abandon";
    q.category = sm::QuestCategory::Procedural;

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;
    active.push_back(q);

    engine.abandon(active, q.ordinal, bus);
    CHECK_OR_RETURN(!(!active.empty()),
        "abandon did not remove quest from active list");
    const sm::GameEvent* ev = find_tag(bus, sm::EventTag::QuestFail);
    CHECK_OR_RETURN(!(!ev || ev->a != q.ordinal || ev->s2 != "abandoned"),
        "abandon did not emit QuestFail(abandoned) with the ordinal");

    // Abandoning neither settles the offer nor counts a failure: the
    // settlement may re-offer it the same day, exactly as before.
    sm::GameState abandonState{};
    sm::PlayerState& player = abandonState.player;
    sm::apply_events(bus.tick_events(), abandonState, &bag);
    CHECK_OR_RETURN(!(player.completedQuestCount != 0u
        || player.failedQuestCount != 0u
        || engine.is_known(active, player, q)),
        "abandoned quest was recorded as done unlike TS screen abandon");

    engine.abandon(active, q.ordinal, bus);
    CHECK_OR_RETURN(!(count_tag(bus, sm::EventTag::QuestFail) != 1),
        "abandon emitted duplicate event for missing quest");
}

void test_village_protect_generator_spawn_event() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.worldSeed = 0x51515151u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(3, 0, 0);

    sm::Landmark city{};
    city.type = sm::LandmarkType::City;
    city.id = 1;
    city.name = "Anchor";
    city.x = 20;
    city.y = 20;
    city.population = 500;
    city.mood = sm::SettlementMood::Stable;
    gs.landmarks.push_back(city);

    sm::Landmark village{};
    village.type = sm::LandmarkType::Village;
    village.id = 44;
    village.name = "Tense Hamlet";
    village.x = 24;
    village.y = 22;
    village.population = 80;
    village.mood = sm::SettlementMood::Tense;
    village.nearestCityId = city.id;
    gs.landmarks.push_back(village);

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
    CHECK_OR_RETURN(!(!found),
        "village generator did not produce protect WaitAt quest");
    CHECK_OR_RETURN(!(selected.onAccept.empty()
        || selected.onAccept[0].tag != sm::EventTag::SpawnEntity
        || selected.onAccept[0].s1 != "bandit"
        || selected.onAccept[0].a < 2u),
        "protect quest did not carry SpawnEntity onAccept payload");
}

// (Was test_village_quest_ids_are_collision_safe, guarding the "_v7_" id
// segment: two landmark kinds could share a numeric id then, and only the id
// STRING kept their quests apart. One landmark ordinal issuer (v54) ended
// same-id landmarks; what offers need now is the provenance contract below.)
void test_offer_provenance_is_unique_per_slot_and_day() {
    bag.clear();
    head = sm::AgentMemory{};
    sm::GameState gs{};
    gs.worldSeed = 0x71477147u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(5, 0, 0);

    sm::Landmark city{};
    city.type = sm::LandmarkType::City;
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
    gs.landmarks.push_back(city);

    sm::Landmark village{};
    village.type = sm::LandmarkType::Village;
    village.id = 7;
    village.name = "Same Id Village";
    village.x = 24;
    village.y = 22;
    village.population = 80;
    village.mood = sm::SettlementMood::Tense;
    village.nearestCityId = city.id;
    gs.landmarks.push_back(village);

    const auto cityQuests =
        sm::generate_quests_for_settlement(city, gs, gs.worldSeed);
    CHECK_OR_RETURN(!(cityQuests.empty()),
        "offer provenance test did not generate quests");
    // The dedup triple {giver, slot, bornDay} names an offer uniquely: every
    // generator fires at most once per settlement per day, so no two offers
    // of one day share a slot, and all carry the giver and the day.
    for (std::size_t i = 0; i < cityQuests.size(); ++i) {
        const sm::Quest& a = cityQuests[i];
        CHECK_OR_RETURN(!(a.giverSettlementId != city.id
            || a.bornDay != gs.worldTime.day()
            || a.ordinal != 0u),
            "an offer's provenance is not {giver, slot, TODAY} + no ordinal");
        for (std::size_t j = i + 1; j < cityQuests.size(); ++j) {
            CHECK_OR_RETURN(!(sm::same_offer(a, cityQuests[j])),
                "two offers of one settlement-day share a provenance triple");
        }
    }
    // Tomorrow's offers are new identities by construction.
    gs.worldTime = sm::world_time_at(6, 0, 0);
    const auto tomorrow =
        sm::generate_quests_for_settlement(city, gs, gs.worldSeed);
    CHECK_OR_RETURN(!(tomorrow.empty()),
        "offer provenance test did not generate tomorrow's quests");
    for (const auto& tq : tomorrow) {
        for (const auto& cq : cityQuests) {
            CHECK_OR_RETURN(!(sm::same_offer(tq, cq)),
                "a new day's offer collided with yesterday's provenance");
        }
    }
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
void test_shuffled_order_guards_rng_upper_bound() {
    bag.clear();
    head = sm::AgentMemory{};
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
        CHECK_OR_RETURN(!(!is_permutation_0_6(sm::shuffled_order(rng))),
            "shuffled_order broke the {0..6} permutation at RNG max draw (OOB)");
    }
    for (std::uint32_t seed = 1; seed <= 4096u; ++seed) {
        sm::Rng rng(seed);
        CHECK_OR_RETURN(!(!is_permutation_0_6(sm::shuffled_order(rng))),
            "shuffled_order broke the {0..6} permutation on a normal seed");
    }
}

// The end-to-end flow that used to live in main()'s preamble: an
// economy-driven delivery quest is generated, accepted, completed and
// rewarded exactly once.
void test_generated_delivery_quest_flow() {
    sm::GameState gs{};
    gs.worldSeed = 0x5eed1234u;
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldTime = sm::world_time_at(0, 6, 0);
    gs.player.x = 12.0f;
    gs.player.y = 18.0f;
    bag.add("coin_empire", 100);

    sm::Landmark settlement{};
    settlement.type = sm::LandmarkType::City;
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
    gs.landmarks.push_back(settlement);

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
    CHECK_OR_RETURN(!(!found),
        "procedural generator did not produce economy-driven delivery quest");
    CHECK_OR_RETURN(!(selected.objectives.empty()
        || selected.objectives.front().kind != sm::ObjectiveKind::DeliverItems
        || selected.objectives.front().itemId != "tools"
        || selected.objectives.front().quantity <= 0
        || selected.objectives.front().targetSettlementId != settlement.id),
        "selected delivery quest does not follow economy resource demand");

    const int startGold = sm::wallet_value(bag);
    const int rewardGold = gold_reward(selected);
    CHECK_OR_RETURN(!(rewardGold <= 0),
        "selected generated delivery quest has no gold reward");
    // The reward is PAID OFF THE GIVER'S STORE now (owner 2026-08-31,
    // canon-audit B3 closed) — fund the treasury, or the town honestly
    // pays nothing and this test would measure a thin purse, not the law.
    {
        sm::Landmark* giver = sm::landmark_by_id(gs, settlement.id);
        CHECK_OR_RETURN(!(giver == nullptr), "fixture: giver landmark");
        giver->inventory.add("coin_empire", rewardGold * 4);
    }
    bag.add("tools", selected.objectives.front().quantity);

    sm::EventBus bus;
    sm::QuestEngine engine;
    std::vector<sm::Quest> active;

    engine.accept(active, selected, gs, bus);
    CHECK_OR_RETURN(!(active.size() != 1),
        "accept did not add quest to active list");
    CHECK_OR_RETURN(!(!has_tag(bus, sm::EventTag::QuestStart)),
        "accept did not emit QuestStart");
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != startGold),
        "accept applied reward before completion");

    bus.flush();
    engine.tick(active, bus, gs, &bag, &head);
    CHECK_OR_RETURN(!(!active.empty()),
        "delivery items did not complete generated quest");
    CHECK_OR_RETURN(!(!has_tag(bus, sm::EventTag::QuestComplete)),
        "completion did not emit QuestComplete");

    sm::apply_events(bus.tick_events(), gs, &bag);
    CHECK_OR_RETURN(!(gs.player.completedQuestCount != 1u),
        "QuestComplete was not applied to player completion state");
    CHECK_OR_RETURN(!(active.empty() && gs.nextQuestOrdinal != 2u),
        "accepting the generated quest did not draw from the one issuer");
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != startGold + rewardGold),
        "gold reward was not applied exactly once");

    bus.flush();
    sm::apply_events(bus.tick_events(), gs, &bag);
    CHECK_OR_RETURN(!(sm::wallet_value(bag) != startGold + rewardGold),
        "empty post-flush tick reapplied reward");
}

} // namespace

int main() {
    test_generated_delivery_quest_flow();
    test_event_bus_contract_surface();
    test_quest_accept_event_order();
    test_effect_applicator_ts_verbs();
    test_grant_xp_levels_through_the_one_path();
    test_grant_xp_pays_the_wis_dividend();
    test_quest_xp_reward_levels_the_player();
    test_builtin_nodes_are_registered_and_active();
    test_unhandled_tag_is_inert_in_applicator();
    test_settlement_show_dialog_node();
    test_logic_node_add_registers_inactive();
    test_logic_node_pending_ids_survive_node_add();
    test_logic_node_tick_order_matches_ts_set();
    test_logic_node_effect_can_remove_self();
    test_logic_node_self_reactivation_safe_cases();
    test_intro_show_story_node();
    test_encounter_table_shape();
    test_quest_failed_settles_its_offer();
    test_item_delivery_direct_path();
    test_quest_reward_dispatch_order_and_application();
    test_find_location_player_move_objective();
    test_visit_cell_objective();
    test_quest_completion_order_matches_ts_reverse_scan();
    test_wait_at_timeadvance_objective();
    test_destroy_npc_objective();
    test_interact_cell_objective();
    test_abandon_emits_and_removes();
    test_village_protect_generator_spawn_event();
    test_offer_provenance_is_unique_per_slot_and_day();
    test_shuffled_order_guards_rng_upper_bound();
    return sm::test::report("quest_lifecycle_test");
}
