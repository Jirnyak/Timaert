// The auto-resolve is NOT MUTE (damage-door track Inc 6).
//
// CANON S13 says there is one law of battle at both scales, and work_vector §1
// says a system that emits no facts is invisible to the story layer. The
// auto-resolve broke both: it settled deaths in silence — no facts, so quest
// kill-tallies never counted an auto-resolved kill; no kill reputation, so a
// massacre by button cost nothing; and no loot roll, so the spoils were only
// whatever bag the loser happened to carry (a roster's dead dropped nothing at
// all). The same deaths underfoot paid all three.
//
// Pinned here, each with its negative control:
//   * every death is REPORTED once — roster rows by their record ids, the
//     leader by his entity — and a survivor is never reported;
//   * the kill price is the registry's column: a lawful faction charges
//     kKillRepPenalty per body, an outlaw charges nothing (killIsNoCrime);
//   * the spoils are ROLLED through the one loot registry, so a fallen
//     merchant pays coin of his own realm even though his record carried no
//     bag; a defeat pays the player nothing.

#include "check.h"
#include "macro/squad.h"
#include "macro/world_row.h"
#include "macro/player_entity.h"
#include "macro/npc_spawn.h"
#include "macro/currency.h"
#include "macro/store.h"

#include <cstdio>
#include <vector>

namespace {

// The player's bag is an ordinary NpcInventory on his squad entity now; a
// fixture raises that entity and reads the spoils from it.
sm::Inventory& player_bag_of(sm::ecs::World& w) {
    static sm::Inventory scratch{};
    sm::Inventory* bag = sm::player_inventory(w);
    return bag ? *bag : scratch;
}

using namespace sm;

struct FactLog {
    std::vector<BattleFact> facts;
};

void collect(void* user, const BattleFact& f) {
    static_cast<FactLog*>(user)->facts.push_back(f);
}

// A macro squad: leader entity + roster records, the shape every macro body
// has (squad == leader, CANON S4).
entt::entity squad(ecs::World& w, NPCType leaderType, const char* factionId,
                   int level, int members, std::uint32_t spawnIndex) {
    auto& reg = w.reg;
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    const entt::entity e = reg.create();
    reg.emplace<ecs::MacroSlot>(e, h.slot);
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(leaderType),
                                   std::uint16_t(faction_index(factionId))};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(level)};
    st.pools[h.slot] = ecs::Pools{100, 100};
    st.spawnId[h.slot] = ecs::MacroSpawnId{spawnIndex};
    auto& bag = st.inventory[h.slot];
    for (int i = 0; i < members; ++i) {
        creatures_push(bag.inv,
            make_soldier(std::uint16_t(NPCType::Merchant), level,
                         std::uint32_t(spawnIndex * 100u + std::uint32_t(i))));
    }
    return e;
}

// An outcome that kills the whole enemy side: every roster row plus the
// leader. Hand-built so the test states the law, not the resolver's dice.
AutoBattleOutcome wipe_of(ecs::World& w, entt::entity loser, bool loserIsB) {
    AutoBattleOutcome o{};
    o.winner = loserIsB ? 0 : 1;
    auto& cas = loserIsB ? o.casualtiesB : o.casualtiesA;
    for (const CreatureHead r :
         creature_heads_range((*sm::body_state<ecs::NpcInventory>(w.reg, loser)).inv)) {
        cas.push_back(make_soldier(r.kind, r.level, r.entityId));
    }
    (loserIsB ? o.leaderFractionB : o.leaderFractionA) = 0.0f;
    (loserIsB ? o.leaderFractionA : o.leaderFractionB) = 0.75f;
    return o;
}

void test_every_death_is_reported_once() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    FactLog log{};
    MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &w;
    ensure_macro_player_entity(gs, w);
    mw.facts = &collect;
    mw.factsUser = &log;

    const entt::entity a = squad(w, NPCType::Guard, "empire", 3, 0, 1u);
    const entt::entity b = squad(w, NPCType::Bandit, "bandits", 2, 3, 2u);
    settle_auto_battle(mw, a, b, wipe_of(w, b, /*loserIsB*/true));

    CHECK(log.facts.size() == 4,
          "three roster rows and the leader — four deaths, four facts");
    int leaderFacts = 0, rosterFacts = 0;
    for (const BattleFact& f : log.facts) {
        CHECK(f.kind == BattleFact::Kind::Death, "the fact is a death");
        if (f.detail < 0) ++leaderFacts; else ++rosterFacts;
    }
    CHECK(leaderFacts == 1, "the fallen leader is reported by his entity");
    CHECK(rosterFacts == 3, "each roster row is reported by its record id");
    CHECK(log.facts.back().npcType == std::uint16_t(NPCType::Bandit),
          "the leader's fact carries HIS row, not a member's");

    // The negative control: the victor lost nobody, so the victor says
    // nothing. A settle that reported both sides regardless would double the
    // world's dead.
    for (const BattleFact& f : log.facts) {
        CHECK(f.killer == std::uint32_t(entt::to_integral(a)),
              "every fact names the victor as the killer");
    }
}

void test_no_facts_when_nobody_listens() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    MacroWorld mw{};       // no sink: a headless fixture
    mw.gs = &gs;
    mw.world = &w;
    const entt::entity a = squad(w, NPCType::Guard, "empire", 3, 0, 1u);
    const entt::entity b = squad(w, NPCType::Bandit, "bandits", 2, 2, 2u);
    settle_auto_battle(mw, a, b, wipe_of(w, b, true));
    CHECK(sm::macro_dead(w.reg, b),
          "the battle still settles with nobody listening — a null channel "
          "is the zero contribution, not a broken path");
}

void test_kill_price_is_the_registry_column() {
    // A lawful realm: every body costs kKillRepPenalty.
    {
        GameState gs{};
        gs.mapW = gs.mapH = 64;
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        sm::store_attach(w, wStore_.get());
        MacroWorld mw{};
        mw.gs = &gs;
        mw.world = &w;
        const entt::entity enemy =
            squad(w, NPCType::Guard, "empire", 2, 2, 5u);
        const int before = player_reputation(&gs, "empire");
        settle_player_auto_battle(mw, enemy, wipe_of(w, enemy, true), true);
        const int after = player_reputation(&gs, "empire");
        CHECK(after == before + 3 * kKillRepPenalty,
              "three imperial dead cost three times the one kill price");
    }
    // An outlaw clan: killIsNoCrime, so nothing is charged at all.
    {
        GameState gs{};
        gs.mapW = gs.mapH = 64;
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        sm::store_attach(w, wStore_.get());
        MacroWorld mw{};
        mw.gs = &gs;
        mw.world = &w;
        const entt::entity enemy =
            squad(w, NPCType::Bandit, "bandits", 2, 2, 6u);
        const int before = player_reputation(&gs, "bandits");
        settle_player_auto_battle(mw, enemy, wipe_of(w, enemy, true), true);
        CHECK(player_reputation(&gs, "bandits") == before,
              "killing outlaws is no crime — the column says so, not the code");
    }
}

void test_spoils_are_rolled_not_scavenged() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.worldSeed = 4242u;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &w;
    ensure_macro_player_entity(gs, w);
    // A merchant band: rich rows, and NOT ONE of them carries a bag — roster
    // members are records, not entities. Before the roll they dropped nothing.
    const entt::entity enemy = squad(w, NPCType::Merchant, "empire", 4, 3, 7u);
    // Слияние M-71: контейнер у сквада ЕСТЬ (в нём живут его люди), но
    // ПРЕДМЕТНАЯ область пуста — до броска ронять по-прежнему нечего.
    {
        const Inventory& einv = (*sm::body_state<ecs::NpcInventory>(w.reg, enemy)).inv;
        int goods = 0;
        for (const ItemRef& sl : einv.slots) {
            if (!sl.empty() && world_row_is_item(sl.def)) ++goods;
        }
        CHECK(goods == 0,
              "the fixture's premise: nobody here carries goods");
    }
    settle_player_auto_battle(mw, enemy, wipe_of(w, enemy, true), true);

    // The realm's whole coin FAMILY (three nominals since verdict №1): a
    // purse change-makes into whichever rows fit its value.
    const auto realm_coin_value = [](const Inventory& inv, const char* id) {
        return coin_census_value(inv) > 0
            && faction_coins(faction_index(id))[0] != nullptr;
    };
    CHECK(realm_coin_value(player_bag_of(w), "empire"),
          "the fallen merchants pay coin of their own realm");

    // The negative control: a DEFEAT pays nothing. Loot is the victor's.
    GameState gs2{};
    gs2.mapW = gs2.mapH = 64;
    gs2.worldSeed = 4242u;
    ecs::World w2;
    auto w2Store_ = sm::make_macro_store();
    sm::store_attach(w2, w2Store_.get());
    MacroWorld mw2{};
    mw2.gs = &gs2;
    mw2.world = &w2;
    ensure_macro_player_entity(gs2, w2);
    const entt::entity enemy2 =
        squad(w2, NPCType::Merchant, "empire", 4, 3, 8u);
    AutoBattleOutcome loss{};
    loss.winner = 1;                    // the enemy (side B) won
    loss.leaderFractionA = 0.0f;        // the player fell
    loss.leaderFractionB = 0.8f;
    settle_player_auto_battle(mw2, enemy2, loss, /*playerIsA*/true);
    CHECK(coin_census_value(player_bag_of(w2)) == 0,
          "a defeat pays the player nothing");
}

// A beast squad is the other end of the purse law reaching the map: wolves
// carry no coin however the fight was resolved.
void test_beasts_pay_no_coin() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.worldSeed = 99u;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &w;
    ensure_macro_player_entity(gs, w);
    const entt::entity pack = squad(w, NPCType::Wolf, "wildlife", 3, 0, 9u);
    settle_player_auto_battle(mw, pack, wipe_of(w, pack, true), true);
    CHECK(coin_census_value(player_bag_of(w)) == 0,
          "a wolf pack pays no coin — the row has no pockets, on the map as "
          "underfoot");
}

} // namespace

int main() {
    test_every_death_is_reported_once();
    test_no_facts_when_nobody_listens();
    test_kill_price_is_the_registry_column();
    test_spoils_are_rolled_not_scavenged();
    test_beasts_pay_no_coin();
    return sm::test::report("auto_resolve_speaks_test");
}
