// THE agreement between the two laws of battle (Session 15, owner ruling):
// the auto-resolve (macro/auto_battle.h) must agree with the fought version
// well enough that a player who fights by hand is not playing a different
// game from the one the world plays around him. So the core of this file is
// a PROPERTY, not an equality of numbers: the same lineup, run through the
// resolver AND through a hand-fought simulation — the shipping steering
// (sub/battle.cpp) navigating bodies whose stats come from the very same
// sheet law (make_character_sheet → project_combat, aura applied) with the
// engine's own strike model — must name the same winner and a compatible
// magnitude of losses.
//
// Everything else pins the resolver's own laws: strength grows with the
// sheet (a constant resolver cannot satisfy a comparison of two live runs),
// a clear advantage cannot be rolled away by fortune, ambush and fatigue
// move the outcome in the direction the world says they must, and the same
// inputs with the same seed resolve to the same battle.
#include "check.h"

#include "macro/auto_battle.h"
#include "sub/battle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace sm;
using namespace sm::sub;

constexpr float kWorld = 3072.0f;

float flat_height(void*, float, float) { return 100.0f; }

BattleTerrain flat_terrain() {
    BattleTerrain t{};
    t.heightAt = &flat_height;
    t.worldMax = kWorld;
    return t;
}

constexpr std::uint64_t mask_of(int faction) { return 1ull << faction; }

SoldierSquad roster_of(NPCType kind, int level, int n,
                       std::uint32_t idBase) {
    SoldierSquad r{};
    for (int i = 0; i < n; ++i) {
        r.push(make_soldier(std::uint8_t(kind), level,
                            idBase + std::uint32_t(i)));
    }
    return r;
}

AutoBattleSide side_of(NPCType leader, int leaderLevel,
                       const SoldierSquad* roster) {
    AutoBattleSide s{};
    s.leaderType = leader;
    s.leaderLevel = leaderLevel;
    s.leaderSeed = 0xABCDu ^ std::uint32_t(leader);
    s.roster = roster;
    return s;
}

// ── The fought version, headless ───────────────────────────────────────────
// Navigation is the SHIPPING battle module; per-body stats are the SAME
// sheet-projected numbers the resolver and the subworld births read; the
// strike model mirrors the engine loop (inReach + cooldown → hp -= damage),
// like tests/battle_ai_test.cpp's attrition harness does.
struct ManualOutcome {
    int winner = -1;             // -1 = did not conclude
    int start[2] = {0, 0};
    int survivors[2] = {0, 0};
};

ManualOutcome fight_by_hand(const AutoBattleSide& a, const AutoBattleSide& b) {
    struct Body {
        float x, y, vx, vy, hp, dmg, cd, cdMax, reach, speed, radius;
        // The fraction of an incoming blow this body keeps — THE door's own
        // mitigation, kArmorHalving / (kArmorHalving + armor)
        // (sub/damage.cpp), so the fought harness softens every strike by
        // the same law the shipping door does.
        float mitigate;
        int side;
        bool alive;
        bool missile;
    };
    std::vector<Body> bodies;

    const auto add_fighter = [&bodies](NPCType type, int level,
                                       std::uint32_t seed,
                                       const BonusTotals* squadBonuses, float frac,
                                       int side) {
        CharacterSheet sheet = make_character_sheet(type, level, seed);
        if (squadBonuses) sheet = effective_sheet(sheet, *squadBonuses);
        const NpcTypeDef& def = npc_def(type);
        const CombatTemplate pc = project_combat(sheet, def.combat);
        Body f{};
        f.hp = std::max(1.0f, std::floor(pc.hp)) *
               std::clamp(frac, 0.0f, 1.0f);
        f.dmg = std::floor(pc.damage);
        f.cd = 0.0f;
        f.cdMax = std::max(0.1f, pc.cooldown);
        f.reach = pc.attackRange;
        f.speed = pc.speed;
        f.radius = npc_body_radius(def);
        f.mitigate = kArmorHalving
                   / (kArmorHalving + float(std::max(0, def.armor)));
        f.side = side;
        f.alive = true;
        f.missile = pc.attackKind == CombatTemplate::Missile;
        bodies.push_back(f);
    };

    const AutoBattleSide* sides[2] = {&a, &b};
    for (int side = 0; side < 2; ++side) {
        const AutoBattleSide& s = *sides[side];
        add_fighter(s.leaderType, s.leaderLevel, s.leaderSeed, nullptr,
                    s.leaderHealthFraction, side);
        if (s.roster) {
            for (const SoldierRecord& r : *s.roster) {
                if (!valid_npc_kind(r.kind)) continue;
                add_fighter(NPCType(r.kind), normalize_soldier_level(r.level),
                            auto_battle_detail::member_seed(r), &s.bonuses,
                            1.0f, side);
            }
        }
    }

    ManualOutcome out{};
    const float cx = kWorld * 0.5f, cy = kWorld * 0.5f;
    {
        int idx[2] = {0, 0};
        int per[2] = {0, 0};
        for (const Body& f : bodies) ++per[f.side];
        out.start[0] = per[0];
        out.start[1] = per[1];
        for (Body& f : bodies) {
            float pos[2] = {cx, cy};
            deploy_army_slot(cx, cy, f.side, idx[f.side]++, per[f.side], pos);
            f.x = pos[0];
            f.y = pos[1];
        }
    }

    const BattleTerrain terrain = flat_terrain();
    const BattleParams prm{};
    BattleUnits u{};
    UnitGrid fine{}, pick{};
    InfluenceField field{};
    std::vector<int> slot;
    const float dt = 1.0f / 30.0f;
    for (int t = 0; t < 18000; ++t) {
        u.clear();
        u.reserve(int(bodies.size()));
        slot.clear();
        int alive[2] = {0, 0};
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const Body& f = bodies[i];
            if (!f.alive) continue;
            ++alive[f.side];
            BattleUnitDesc d{};
            d.x = f.x; d.y = f.y;
            d.vx = f.vx; d.vy = f.vy;
            d.radius = f.radius;
            d.speed = f.speed;
            d.reach = f.reach;
            d.sight = 4000.0f;   // the meeting already happened on the map
            d.faction = std::int16_t(f.side);
            d.enemyMask = mask_of(1 - f.side);
            // The row's ranged kind steers as ranged, like the engine feed
            // (sub/engine.cpp: Combat::Missile ⇒ BU_Missile).
            if (f.missile) d.flags |= BU_Missile;
            u.add(d);
            slot.push_back(int(i));
        }
        out.survivors[0] = alive[0];
        out.survivors[1] = alive[1];
        if (alive[0] == 0 || alive[1] == 0) {
            out.winner = alive[0] > 0 ? 0 : 1;
            break;
        }

        build_unit_grid(fine, u, fine_cell_for(u, prm), 256);
        build_unit_grid(pick, u, pick_cell_for(u, prm), 256);
        build_influence_field(field, u, prm.influenceCell, kWorld);
        steer_battle(u, fine, pick, field, terrain, prm, dt, nullptr);

        for (int i = 0; i < u.count; ++i) {
            Body& f = bodies[std::size_t(slot[std::size_t(i)])];
            f.x = u.x[std::size_t(i)]; f.y = u.y[std::size_t(i)];
            f.vx = u.vx[std::size_t(i)]; f.vy = u.vy[std::size_t(i)];
            f.cd -= dt;
            if (u.inReach[std::size_t(i)] && u.target[std::size_t(i)] >= 0
                && f.cd <= 0.0f) {
                Body& v = bodies[std::size_t(
                    slot[std::size_t(u.target[std::size_t(i)])])];
                v.hp -= f.dmg * v.mitigate;
                if (v.hp <= 0.0f) v.alive = false;
                f.cd = f.cdMax;
            }
        }
    }
    return out;
}

// ── The resolver's own laws ────────────────────────────────────────────────

void test_power_grows_with_the_sheet() {
    using namespace sm;
    const auto few  = roster_of(NPCType::Guard, 3, 3, 100u);
    const auto more = roster_of(NPCType::Guard, 3, 9, 100u);
    const auto veteran = roster_of(NPCType::Guard, 9, 3, 100u);

    const float pFew  = squad_power(side_of(NPCType::Guard, 3, &few));
    const float pMore = squad_power(side_of(NPCType::Guard, 3, &more));
    const float pVet  = squad_power(side_of(NPCType::Guard, 3, &veteran));
    CHECK(pMore > pFew, "more men are more strength");
    CHECK(pVet > pFew, "higher levels are more strength - the sheet IS the scaling");

    // The aura reaches the resolver through the same door the births use.
    AutoBattleSide led = side_of(NPCType::Guard, 3, &few);
    CharacterSheet leaderSheet{};
    add_perk(leaderSheet.perks, PerkID::Leader);
    led.bonuses = squad_bonuses(leaderSheet);
    CHECK(squad_power(led) > pFew,
          "a Leader-perk lord commands a stronger squad by the same aura law");

    // Fatigue and terrain are context, not flavour.
    AutoBattleSide tired = side_of(NPCType::Guard, 3, &few);
    tired.fatigue = 0.5f;
    CHECK(squad_power(tired) < pFew, "an exhausted squad fights at a discount");
    AutoBattleSide uphill = side_of(NPCType::Guard, 3, &few);
    uphill.terrain = 1.5f;
    CHECK(squad_power(uphill) > pFew, "ground worth holding is strength");
}

void test_a_clear_advantage_cannot_be_rolled_away() {
    using namespace sm;
    const auto strongR = roster_of(NPCType::Guard, 5, 8, 100u);
    const auto weakR   = roster_of(NPCType::Peasant, 1, 3, 200u);
    const AutoBattleSide strong = side_of(NPCType::Guard, 5, &strongR);
    const AutoBattleSide weak   = side_of(NPCType::Peasant, 1, &weakR);

    int strongWins = 0, loserBled = 0;
    for (std::uint32_t s = 0; s < 32; ++s) {
        Rng rng(1000u + s);
        const AutoBattleOutcome o =
            resolve_auto_battle(strong, weak, Ambush::None, rng);
        if (o.winner == 0) ++strongWins;
        if (int(o.casualtiesB.size()) * 2 >= int(weakR.size())) ++loserBled;
        CHECK(o.leaderFractionA > 0.0f,
              "a winner's leader limps out - only a broken side can lose its head");
    }
    CHECK(strongWins == 32, "bounded fortune cannot flip a clear advantage");
    CHECK(loserBled == 32, "a broken side loses most of its men, every time");

    // Determinism: the same battle from the same seed is the same battle.
    Rng r1(77u), r2(77u);
    const AutoBattleOutcome o1 =
        resolve_auto_battle(strong, weak, Ambush::None, r1);
    const AutoBattleOutcome o2 =
        resolve_auto_battle(strong, weak, Ambush::None, r2);
    CHECK(o1.winner == o2.winner
              && o1.casualtiesA == o2.casualtiesA
              && o1.casualtiesB == o2.casualtiesB
              && o1.leaderFractionA == o2.leaderFractionA
              && o1.leaderFractionB == o2.leaderFractionB,
          "same inputs, same seed, same battle - resumable and testable");
}

// Owner ruling (2026-08-06): a leader's head is never given to chance. He
// dies in an auto-battle ONLY when he lost AND his whole roster died with
// him; while one of his men still stands, defeat costs him HP — above zero.
void test_a_leaders_head_is_never_given_to_chance() {
    using namespace sm;
    const auto strongR = roster_of(NPCType::Guard, 5, 8, 100u);
    const auto weakR   = roster_of(NPCType::Peasant, 1, 6, 200u);
    const AutoBattleSide strong = side_of(NPCType::Guard, 5, &strongR);
    const AutoBattleSide weak   = side_of(NPCType::Peasant, 1, &weakR);

    int checkedAlive = 0, checkedDead = 0;
    for (std::uint32_t s = 0; s < 64; ++s) {
        Rng rng(5000u + s);
        const AutoBattleOutcome o =
            resolve_auto_battle(strong, weak, Ambush::None, rng);
        CHECK_OR_RETURN(o.winner == 0, "the fixture's strong side must win");
        const bool wiped = int(o.casualtiesB.size()) >= weakR.size();
        if (wiped) {
            CHECK(o.leaderFractionB == 0.0f,
                  "a loser with no men left falls with the last of them");
            ++checkedDead;
        } else {
            CHECK(o.leaderFractionB > 0.0f,
                  "while one of his men stands, defeat wounds the leader - "
                  "it never kills him");
            ++checkedAlive;
        }
    }
    CHECK(checkedAlive + checkedDead == 64,
          "every resolved battle judged the leader rule");

    // A squad of one is its own whole roster: a lone loser has nobody left
    // to stand between him and the field.
    Rng rng(9u);
    const AutoBattleSide lone = side_of(NPCType::Peasant, 1, nullptr);
    const AutoBattleOutcome o =
        resolve_auto_battle(strong, lone, Ambush::None, rng);
    CHECK(o.winner == 0 && o.leaderFractionB == 0.0f,
          "a beaten squad of one is gone from the map, by the same rule");
}

void test_context_tips_the_scales() {
    using namespace sm;
    const auto rosterA = roster_of(NPCType::Guard, 3, 5, 100u);
    const auto rosterB = roster_of(NPCType::Guard, 3, 5, 200u);
    const AutoBattleSide a = side_of(NPCType::Guard, 3, &rosterA);
    const AutoBattleSide b = side_of(NPCType::Guard, 3, &rosterB);

    // Identical squads: the ambusher's free volley outweighs bounded fortune.
    int ambusherWins = 0;
    for (std::uint32_t s = 0; s < 16; ++s) {
        Rng rng(2000u + s);
        if (resolve_auto_battle(a, b, Ambush::SideA, rng).winner == 0)
            ++ambusherWins;
    }
    CHECK(ambusherWins == 16, "who struck from ambush matters, every time");

    // Identical squads, one fresh, one spent: fatigue decides the same way.
    AutoBattleSide tired = b;
    tired.fatigue = 0.5f;
    int freshWins = 0;
    for (std::uint32_t s = 0; s < 16; ++s) {
        Rng rng(3000u + s);
        if (resolve_auto_battle(a, tired, Ambush::None, rng).winner == 0)
            ++freshWins;
    }
    CHECK(freshWins == 16, "a spent squad loses to its fresh twin");
}

// ── The agreement itself ───────────────────────────────────────────────────

void agreement_case(const char* what, const AutoBattleSide& a,
                    const AutoBattleSide& b) {
    using namespace sm;
    const ManualOutcome manual = fight_by_hand(a, b);
    CHECK_OR_RETURN(manual.winner >= 0, "the fought battle must conclude");

    int agree = 0;
    int loserBled = 0, winnerHeld = 0;
    constexpr int kSeeds = 8;
    for (std::uint32_t s = 0; s < kSeeds; ++s) {
        Rng rng(4000u + s);
        const AutoBattleOutcome o = resolve_auto_battle(a, b, Ambush::None, rng);
        if (o.winner == manual.winner) ++agree;

        const AutoBattleSide& loser = o.winner == 0 ? b : a;
        const auto& loserCas = o.winner == 0 ? o.casualtiesB : o.casualtiesA;
        const auto& winnerCas = o.winner == 0 ? o.casualtiesA : o.casualtiesB;
        const AutoBattleSide& winner = o.winner == 0 ? a : b;
        const int loserMen = loser.roster ? int(loser.roster->size()) : 0;
        const int winnerMen = winner.roster ? int(winner.roster->size()) : 0;
        // The fought loser is annihilated (the sim runs to conclusion); the
        // resolved loser must at least be BROKEN - the majority down - or the
        // two worlds tell different stories about the same defeat.
        if (loserMen == 0 || int(loserCas.size()) * 2 >= loserMen) ++loserBled;
        // And at this advantage the fought winner keeps most of his line, so
        // the resolved winner must too.
        if (winnerMen == 0 || int(winnerCas.size()) * 2 <= winnerMen)
            ++winnerHeld;
    }
    CHECK(agree == kSeeds, what);
    CHECK(loserBled == kSeeds,
          "both laws break the loser: the majority of his men are down");
    CHECK(winnerHeld == kSeeds,
          "both laws spare the winner: he keeps the majority of his line");

    // The fought version's own sanity: the winner side kept bodies standing.
    CHECK(manual.survivors[manual.winner] > 0,
          "the fought winner has survivors to keep");
}

void test_auto_and_fought_agree() {
    using namespace sm;
    // Guards over peasants: quality wins in both worlds.
    const auto guardsR   = roster_of(NPCType::Guard, 4, 6, 100u);
    const auto peasantsR = roster_of(NPCType::Peasant, 1, 6, 200u);
    agreement_case(
        "quality: the resolver names the same winner the fought battle does",
        side_of(NPCType::Guard, 4, &guardsR),
        side_of(NPCType::Peasant, 1, &peasantsR));

    // Numbers over a thin line: twenty bandits break four guards. (Twelve
    // used to be enough — until the resolver learned to credit the guards'
    // plate as the door does; a clear mass verdict now has to out-mass the
    // armour too.)
    const auto banditsR = roster_of(NPCType::Bandit, 3, 20, 300u);
    const auto thinR    = roster_of(NPCType::Guard, 3, 4, 400u);
    agreement_case(
        "mass: the resolver names the same winner the fought battle does",
        side_of(NPCType::Bandit, 3, &banditsR),
        side_of(NPCType::Guard, 3, &thinR));

    // ARMOUR decides: the bandit mob out-numbers the guards past their raw
    // hp × dps — only the guards' plate (npc_def armor 10, worth ×2
    // effective HP at kArmorHalving) turns the fight. A resolver that does
    // not read armour names the mob here; the door-mitigated fought battle
    // names the guards.
    const auto mobR   = roster_of(NPCType::Bandit, 3, 8, 500u);
    const auto plateR = roster_of(NPCType::Guard, 3, 6, 600u);
    agreement_case(
        "armour: the resolver credits plate exactly as the door mitigates it",
        side_of(NPCType::Bandit, 3, &mobR),
        side_of(NPCType::Guard, 3, &plateR));
}

} // namespace

int main() {
    test_power_grows_with_the_sheet();
    test_a_clear_advantage_cannot_be_rolled_away();
    test_a_leaders_head_is_never_given_to_chance();
    test_context_tips_the_scales();
    test_auto_and_fought_agree();
    return sm::test::report("auto_battle_test");
}
