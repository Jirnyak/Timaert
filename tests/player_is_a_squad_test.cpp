// ИГРОК = НПЦ, and his party is an ORDINARY macro squad (CANON S4, owner's
// ruling 2026-08-27: «сквад игрока — обычный сквад, просто с флажком»).
//
// Making that true has a price the merge had to pay in full: the player's
// entity now carries the SAME components every macro squad carries — a roster,
// a bag, a level, a runtime, a face. So every scan on the map that used to
// mean "an NPC" now finds him too, and the ones that mean "somebody OTHER than
// me" have to say so. They used to say it with `exclude<PlayerTag>`, which is
// a different sentence: PlayerTag answers «кем я управляю сейчас» and MOVES —
// onto a possessed lord, onto a body underground. The moment it moves, the
// player's own squad becomes an ordinary NPC to every one of those loops: prey
// for the threat step, a party to trade with, a walker for the AI to drive.
//
// ecs::PlayerSquadTag is the second sentence — «чей это отряд» — and it never
// moves. This file pins what it buys, each with its negative control: an
// ordinary NPC in the very same state, which MUST be picked up by the loop
// that skips the player.
#include "check.h"

#include "macro/player_entity.h"
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/macro_snapshot.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdio>

namespace {

using namespace sm;

// An ordinary macro squad, spawned the shape the AI expects to drive.
entt::entity npc_squad(ecs::World& w, float x, float y, std::uint32_t ordinal,
                       int members) {
    auto& reg = w.reg;
    const entt::entity e = reg.create();
    reg.emplace<ecs::Position>(e, x, y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Bandit),
                              std::uint16_t(faction_index("bandits")));
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    reg.emplace<ecs::Health>(e, 50.0f, 50.0f);
    reg.emplace<ecs::MacroSpawnId>(e, ordinal);
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = -1;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.sp = 100;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    auto& roster = reg.emplace<ecs::SquadRoster>(e);
    for (int i = 0; i < members; ++i) {
        roster.squad.push(make_soldier(std::uint16_t(NPCType::Bandit), 2,
                                       ordinal * 100u + std::uint32_t(i)));
    }
    return e;
}

// ── 1. He is a whole squad, not a husk ───────────────────────────────────
void test_player_carries_everything_a_squad_carries() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.x = 20.0f;
    gs.player.y = 20.0f;
    ecs::World w;
    ensure_macro_player_entity(gs, w);

    const entt::entity e = player_squad_entity(w);
    CHECK(e != entt::null, "the player's squad exists after the door");

    // The snapshot's view (macro/macro_snapshot.cpp) names exactly this set —
    // if the player misses one, he is not saved, and a save that forgets the
    // player's own army is the loudest bug this merge could ship.
    CHECK((w.reg.all_of<ecs::MacroSpawnId, ecs::Position, ecs::VisualPos,
                        ecs::NPCKind, ecs::Health, ecs::NpcLevel,
                        ecs::MacroNpcRuntime, ecs::NpcTraits,
                        ecs::NpcCharacter, ecs::NpcInventory,
                        ecs::SquadRoster>(e)),
          "the player's squad matches the macro-snapshot view whole");
    CHECK(w.reg.get<ecs::MacroSpawnId>(e).index == ecs::kPlayerSquadOrdinal,
          "he is found by his reserved ordinal");
    CHECK(player_roster(w) != nullptr, "his roster is reachable");
    CHECK(player_inventory(w) != nullptr, "his bag is reachable");

    // And he is proved to be IN the snapshot, not merely shaped like it.
    const std::vector<MacroNpcRecord> snap = snapshot_macro_ecs(w);
    int mine = 0;
    for (const MacroNpcRecord& r : snap) {
        if (r.spawnId.index == ecs::kPlayerSquadOrdinal) ++mine;
    }
    CHECK(mine == 1, "the snapshot carries the player's squad exactly once");
}

// ── 2. The mark is «чей отряд», not «где флажок» ─────────────────────────
void test_the_mark_survives_losing_the_flag() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.x = 20.0f;
    gs.player.y = 20.0f;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);
    CHECK(w.reg.all_of<ecs::PlayerSquadTag>(mine), "his squad is marked");
    CHECK(w.reg.all_of<ecs::PlayerTag>(mine), "and holds the flag by default");

    // Possession: the flag walks onto a lord. Walking the door again must NOT
    // hand it back — and must not un-mark the squad it left behind.
    const entt::entity lord = npc_squad(w, 30.0f, 30.0f, 7u, 2);
    w.reg.remove<ecs::PlayerTag>(mine);
    w.reg.emplace<ecs::PlayerTag>(lord);
    ensure_macro_player_entity(gs, w);

    CHECK(w.reg.all_of<ecs::PlayerTag>(lord), "the possessed lord keeps the flag");
    CHECK(!w.reg.all_of<ecs::PlayerTag>(mine),
          "the door does not steal the flag back mid-possession");
    CHECK(w.reg.all_of<ecs::PlayerSquadTag>(mine),
          "his squad is still HIS squad while he wears another man's face");
    CHECK(!w.reg.all_of<ecs::PlayerSquadTag>(lord),
          "and the borrowed body is not");
}

// ── 3. The AI never drives the player's squad ────────────────────────────
void test_ai_leaves_the_player_squad_standing() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.x = 20.0f;
    gs.player.y = 20.0f;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);

    // Possession, so PlayerTag is NOT on his squad — the exact state in which
    // the old `exclude<PlayerTag>` guard let the AI take the wheel.
    const entt::entity lord = npc_squad(w, 30.0f, 30.0f, 7u, 0);
    w.reg.remove<ecs::PlayerTag>(mine);
    w.reg.emplace<ecs::PlayerTag>(lord);

    // The negative control: an idle NPC standing where the player stands. If
    // the sweep is a no-op for everyone, this one's runtime never moves either
    // and the test proves nothing.
    const entt::entity other = npc_squad(w, 20.0f, 20.0f, 8u, 0);

    const auto before = w.reg.get<ecs::MacroNpcRuntime>(mine);
    MacroNpcAiRuntime runtime;
    MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &w;
    for (int i = 0; i < 8; ++i) tick_macro_npc_ai(mw, runtime, kAiTicks);

    const auto& after = w.reg.get<ecs::MacroNpcRuntime>(mine);
    CHECK(after.state == before.state,
          "the player's squad kept its state through eight AI sweeps");
    CHECK(after.targetX == before.targetX && after.targetY == before.targetY,
          "nothing gave the player's squad somewhere to be");
    CHECK(w.reg.get<ecs::Position>(mine).x == 20.0f &&
          w.reg.get<ecs::Position>(mine).y == 20.0f,
          "and it did not walk off his cell");
    // The sharpest witness: the per-NPC accumulator the sweep advances on
    // everyone it visits. Untouched means never visited, not "visited and
    // decided to stand".
    CHECK(after.tickAccum == 0.0f,
          "the sweep never so much as counted the player's squad");

    const auto& drove = w.reg.get<ecs::MacroNpcRuntime>(other);
    CHECK(drove.tickAccum != 0.0f || drove.state != std::uint8_t(NPCState::Idle),
          "negative control: the same eight sweeps DID reach the squad beside him");
}

// ── 4. His men never desert into the pool ────────────────────────────────
void test_the_players_men_never_desert() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);
    SoldierSquad* roster = player_roster(w);
    CHECK(roster != nullptr, "his roster is there to lose");
    for (int i = 0; i < 4; ++i) {
        roster->push(make_soldier(std::uint16_t(NPCType::Guard), 1,
                                  9000u + std::uint32_t(i)));
    }

    // `Dead` on the player's squad is not supposed to happen — but the drain
    // is called unconditionally at the end of every auto-battle, and a rule
    // that holds only because nothing has broken yet is not a rule.
    w.reg.emplace<ecs::Dead>(mine);
    const entt::entity fallen = npc_squad(w, 30.0f, 30.0f, 7u, 3);
    w.reg.emplace<ecs::Dead>(fallen);

    SoldierSquad pool{};
    const int moved = drain_dead_leader_squads(w, pool);

    CHECK(moved == 3, "only the fallen NPC leader's three men walked away");
    CHECK(pool.size() == 3, "and only they landed in the pool");
    CHECK(player_roster(w)->size() == 4,
          "the player's four are still his, dead flag or not");
    CHECK(w.reg.get<ecs::SquadRoster>(fallen).squad.empty(),
          "negative control: the NPC's roster WAS emptied by the same call");
}

// ── 5. Его числа на сущности не врут ─────────────────────────────────────
void test_the_entity_numbers_are_not_stale() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.x = 20.0f;
    gs.player.y = 20.0f;
    gs.player.sheet.attributes[sm::AttributeId::End] = 5;
    gs.player.sheet.levelData.level = 1;
    recompute_combat_maxima(gs.player.combatStats,
                            gs.player.sheet.attributes,
                            gs.player.sheet.skills);
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity e = player_squad_entity(w);

    const int bornMaxSp = w.reg.get<ecs::MacroNpcRuntime>(e).maxSp;
    CHECK(bornMaxSp == gs.player.combatStats.maxSp,
          "his squad is born with his own stamina bar");

    // He is wounded, he grows tired, he trains END and he levels — every one
    // of these used to leave the entity behind FOREVER, because Health and the
    // march caches were written once at creation. The save then persisted a
    // hale, rested, level-1 player over a dying, exhausted, level-4 one.
    gs.player.combatStats.currentHp = 17;
    gs.player.combatStats.currentSp = -6;   // an honest exhaustion debt
    gs.player.sheet.attributes[sm::AttributeId::End] = 12;
    gs.player.sheet.levelData.level = 4;
    recompute_combat_maxima(gs.player.combatStats,
                            gs.player.sheet.attributes,
                            gs.player.sheet.skills);
    gs.player.x = 33.0f;
    gs.player.y = 44.0f;
    ensure_macro_player_entity(gs, w);

    const auto& hp = w.reg.get<ecs::Health>(e);
    CHECK(hp.hp == 17.0f, "the wound reached the entity");
    CHECK(hp.maxHp == float(gs.player.combatStats.maxHp),
          "and so did the bigger bar the new VIT bought");
    const auto& rt = w.reg.get<ecs::MacroNpcRuntime>(e);
    CHECK(rt.maxSp == gs.player.combatStats.maxSp,
          "the stamina ceiling followed the END he trained");
    CHECK(rt.maxSp > bornMaxSp,
          "negative control: that ceiling did MOVE — the check above is not "
          "comparing two copies of the same stale number");
    CHECK(rt.sp == -6, "the exhaustion DEBT survives the projection, unclamped");
    CHECK(w.reg.get<ecs::NpcLevel>(e).value == 4, "and he is level 4 to the map");
    const auto& pos = w.reg.get<ecs::Position>(e);
    CHECK(pos.x == 33.0f && pos.y == 44.0f, "the entity stands where he stands");
}

// ── 6. Его голова — компонент, а не поле ─────────────────────────────────
void test_the_head_is_on_the_entity() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    ecs::World w;
    ensure_macro_player_entity(gs, w);

    AgentMemory* head = player_head(w);
    CHECK(head != nullptr, "the player's head is reachable through one door");
    remember(*head, make_debt_fact(kDebtToSettlement, 7, 15, 3));

    // It rides the SAME record every leader's memory rides — proving it is not
    // saved twice and not saved never.
    const std::vector<MacroNpcRecord> snap = snapshot_macro_ecs(w);
    const MacroNpcRecord* mine = nullptr;
    for (const MacroNpcRecord& r : snap) {
        if (r.spawnId.index == ecs::kPlayerSquadOrdinal) mine = &r;
    }
    CHECK(mine != nullptr, "his record is in the snapshot");
    const MemoryEntry* debt = mine
        ? recall(mine->memory, AgentMemoryKind::Debt, 7, kDebtToSettlement)
        : nullptr;
    CHECK(debt != nullptr, "and it carries what he remembers");
    CHECK(debt && memory_amount(*debt) == 15, "with the amount intact");
}

// ── 7. Одна дверь сборки стороны боя ─────────────────────────────────────
// The app used to build the player's AutoBattleSide by hand beside the door
// every other squad went through — two answers to one question, reading two
// different stores. They can no longer disagree because there is one of them;
// what this pins is that the one door still says the PLAYER's numbers, not a
// generic adventurer's, when it is handed his authored sheet.
void test_one_door_assembles_every_battle_side() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.sheet.attributes[sm::AttributeId::Str] = 18;
    gs.player.sheet.attributes[sm::AttributeId::Vit] = 18;
    gs.player.sheet.attributes[sm::AttributeId::End] = 14;
    gs.player.sheet.levelData.level = 5;
    recompute_combat_maxima(gs.player.combatStats,
                            gs.player.sheet.attributes,
                            gs.player.sheet.skills);
    gs.player.combatStats.currentHp = gs.player.combatStats.maxHp / 2;
    gs.player.combatStats.currentSp = gs.player.combatStats.maxSp / 4;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity e = player_squad_entity(w);

    const AutoBattleSide mine = auto_battle_side_of(w, e, &gs.player.sheet);
    const AutoBattleSide generic = auto_battle_side_of(w, e);

    CHECK(mine.leaderHpOverride == float(gs.player.combatStats.maxHp),
          "handed his sheet, the door states HIS ceiling");
    CHECK(generic.leaderHpOverride < 0.0f,
          "negative control: handed none, the same door derives from the row "
          "— the sheet is what makes the difference, not the entity");
    CHECK(mine.leaderHealthFraction > 0.45f && mine.leaderHealthFraction < 0.55f,
          "his wound walks in with him, read off the entity the merge made "
          "honest");
    CHECK(mine.fatigue > 0.2f && mine.fatigue < 0.3f,
          "and so does his tiredness");
    CHECK(mine.roster == player_roster(w),
          "his men are his roster — the same lookup, not a second one");
}

// ── 8. Его рана оседает через ту же дверь ────────────────────────────────
// The last of the four player-specific battle paths: `settle_player_auto_battle`
// used to do the leader-wound arithmetic itself, in three lines that read
// PlayerState's ceiling while `settle_leader_fraction` read the entity's — two
// copies of one sum, free to round differently about one man.
void test_the_players_wound_settles_through_the_one_door() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    gs.player.sheet.attributes[sm::AttributeId::Vit] = 10;
    recompute_combat_maxima(gs.player.combatStats,
                            gs.player.sheet.attributes,
                            gs.player.sheet.skills);
    gs.player.combatStats.currentHp = gs.player.combatStats.maxHp;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);
    const int maxHp = gs.player.combatStats.maxHp;

    MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &w;
    const entt::entity foe = npc_squad(w, 10.0f, 10.0f, 7u, 2);

    AutoBattleOutcome o{};
    o.winner = 0;                 // the player's side takes it
    o.leaderFractionA = 0.5f;     // ...limping
    o.leaderFractionB = 0.0f;
    for (const SoldierRecord& r : w.reg.get<ecs::SquadRoster>(foe).squad) {
        o.casualtiesB.push_back(r.entityId);
    }
    settle_player_auto_battle(mw, foe, o, /*playerIsA*/true);

    const auto& hp = w.reg.get<ecs::Health>(mine);
    CHECK(hp.hp == std::floor(hp.maxHp * 0.5f),
          "the door wrote his wound onto the entity, by the entity's ceiling");
    CHECK(gs.player.combatStats.currentHp == int(hp.hp),
          "and his pool followed the entity — one writer, one direction");
    CHECK(gs.player.combatStats.currentHp > 0
          && gs.player.combatStats.currentHp < maxHp,
          "negative control: he is HURT, not untouched and not dead — the "
          "fraction actually travelled");
    CHECK(!w.reg.all_of<ecs::Dead>(mine),
          "a survivor is not marked dead by the shared door");
}

} // namespace

int main() {
    test_player_carries_everything_a_squad_carries();
    test_the_mark_survives_losing_the_flag();
    test_ai_leaves_the_player_squad_standing();
    test_the_players_men_never_desert();
    test_the_entity_numbers_are_not_stale();
    test_the_head_is_on_the_entity();
    test_one_door_assembles_every_battle_side();
    test_the_players_wound_settles_through_the_one_door();
    return sm::test::report("player_is_a_squad_test");
}
