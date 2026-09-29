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
//   * the spoils are the fallen's OWN goods, moved once: победителю
//     достаётся ровно то, что павший нёс, и ни строки сверх — бросок
//     хардкод-профиля роли и печать кошелька снесены (M-139, 2026-09-26);
//     a defeat pays the player nothing.

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
sm::Inventory& player_bag_of(const sm::GameState& gs, sm::ecs::World& w) {
    static sm::Inventory scratch{};
    sm::Inventory* bag = sm::player_inventory(gs, sm::store_of(w));
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
sm::MacroHandle squad(ecs::World& w, NPCType leaderType,
                      const char* factionId,
                      int level, int members, std::uint32_t spawnIndex) {
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
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
    return h;
}

// An outcome that kills the whole enemy side: every roster row plus the
// leader. Hand-built so the test states the law, not the resolver's dice.
AutoBattleOutcome wipe_of(ecs::World& w, sm::MacroHandle loser,
                          bool loserIsB) {
    AutoBattleOutcome o{};
    o.winner = loserIsB ? 0 : 1;
    auto& cas = loserIsB ? o.casualtiesB : o.casualtiesA;
    for (const CreatureHead r :
         creature_heads_range(sm::store_of(w).inventory[loser.slot].inv)) {
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

    const sm::MacroHandle a = 
squad(w, NPCType::Guard, "empire", 3, 0, 1u);
    const sm::MacroHandle b = 
squad(w, NPCType::Bandit, "bandits", 2, 3, 2u);
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
        CHECK(f.killer == sm::macro_handle_bits(a),
              "every fact names the victor as the killer — хэндлом записи");
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
    const sm::MacroHandle a = 
squad(w, NPCType::Guard, "empire", 3, 0, 1u);
    const sm::MacroHandle b = 
squad(w, NPCType::Bandit, "bandits", 2, 2, 2u);
    settle_auto_battle(mw, a, b, wipe_of(w, b, true));
    CHECK(sm::macro_dead(sm::store_of(w), b),
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
        const sm::MacroHandle enemy =
            
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
        const sm::MacroHandle enemy =
            
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
    // СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ (§8 п.11): чтобы спросить «дошла ли
    // добыча», у павшего должна БЫТЬ добыча. Раньше её рождал бросок
    // хардкод-профиля роли — и вопрос стоял «роняет ли ростерная запись то,
    // чего у неё нет»; вердикт владельца 2026-09-26 (M-139) снёс и профили,
    // и кошельки, поэтому закон теперь ОДИН и честный: победителю достаётся
    // РОВНО то, что павший нёс. Кладём купцу настоящий товар руками.
    const sm::MacroHandle enemy = 
squad(w, NPCType::Merchant, "empire", 4, 3, 7u);
    {
        Inventory& einv = sm::store_of(w).inventory[enemy.slot].inv;
        einv.add("misc_gem", 3);
        einv.add("coin_empire_silver", 5);
    }
    const int coinBefore = coin_census_value(player_bag_of(gs, w));
    settle_player_auto_battle(mw, enemy, wipe_of(w, enemy, true), true);

    // ПЕРЕНОС, А НЕ ПЕЧАТЬ: самоцветы и серебро павшего лежат у победителя, и
    // ровно в том счёте, в каком были у павшего — сверх этого не появляется
    // ничего (негативный контроль сноса выдачи, §8 п.6: вернись бросок
    // профиля или кошелёк — счёт перестанет совпадать).
    CHECK(player_bag_of(gs, w).count("misc_gem") == 3,
          "добыча павшего ПЕРЕНОСИТСЯ победителю, ровно своим счётом");
    CHECK(coin_census_value(player_bag_of(gs, w)) == coinBefore + 5 * 10,
          "монеты павшего переходят как товар, и НИ ОДНОЙ сверх — выдача "
          "монет из воздуха снесена (M-139)");
    {
        int goods = 0;
        for (const ItemRef& sl : player_bag_of(gs, w).slots) {
            if (!sl.empty() && world_row_is_item(sl.def)) ++goods;
        }
        CHECK(goods == 2,
              "в сумке победителя ровно две предметные строки — те, что нёс "
              "павший, и ни одной наброшенной профилем роли");
    }

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
    const sm::MacroHandle enemy2 =
        
squad(w2, NPCType::Merchant, "empire", 4, 3, 8u);
    AutoBattleOutcome loss{};
    loss.winner = 1;                    // the enemy (side B) won
    loss.leaderFractionA = 0.0f;        // the player fell
    loss.leaderFractionB = 0.8f;
    settle_player_auto_battle(mw2, enemy2, loss, /*playerIsA*/true);
    CHECK(coin_census_value(player_bag_of(gs, w2)) == 0,
          "a defeat pays the player nothing");
}

// ПАВШИЙ БЕЗ ДОБРА НЕ ПЛАТИТ НИЧЕМ — второй конец того же закона переноса.
// Прежде здесь стоял «у волчьей строки нет карманов» (колонки `purseMin`/
// `purseMax`), и он умер вместе с колонками: M-139. Свидетель остаётся, но
// охраняет уже перенос, а не кошелёк — волчья стая ничего не несёт, значит
// победителю не достаётся ни монеты, ни строки.
void test_empty_handed_fallen_pays_nothing() {
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
    const sm::MacroHandle pack = 
squad(w, NPCType::Wolf, "wildlife", 3, 0, 9u);
    settle_player_auto_battle(mw, pack, wipe_of(w, pack, true), true);
    CHECK(coin_census_value(player_bag_of(gs, w)) == 0,
          "стая, не нёсшая ничего, не платит ни монеты — ни кошелька строки, "
          "ни броска профиля больше нет (M-139)");
    int goods = 0;
    for (const ItemRef& sl : player_bag_of(gs, w).slots) {
        if (!sl.empty() && world_row_is_item(sl.def)) ++goods;
    }
    CHECK(goods == 0,
          "и ни одной предметной строки: добыча есть ПЕРЕНОС, а не выдача");
}

} // namespace

int main() {
    test_every_death_is_reported_once();
    test_no_facts_when_nobody_listens();
    test_kill_price_is_the_registry_column();
    test_spoils_are_rolled_not_scavenged();
    test_empty_handed_fallen_pays_nothing();
    return sm::test::report("auto_resolve_speaks_test");
}
