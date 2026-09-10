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
    // 64 — the fixture map side every test in this file boots (gs.mapW).
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(int(x), int(y), 64));
    reg.emplace<ecs::MacroVisual>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Bandit),
                              std::uint16_t(faction_index("bandits")));
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    reg.emplace<ecs::Pools>(e, 50, 50);
    reg.emplace<ecs::MacroSpawnId>(e, ordinal);
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = -1;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
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
    // (No position scalar to seed since подпосадка 4: the creation door
    // derives the spawn cell from the world — map centre in a bare fixture.)
    ecs::World w;
    ensure_macro_player_entity(gs, w);

    const entt::entity e = player_squad_entity(w);
    CHECK(e != entt::null, "the player's squad exists after the door");

    // The snapshot's view (macro/macro_snapshot.cpp) names exactly this set —
    // if the player misses one, he is not saved, and a save that forgets the
    // player's own army is the loudest bug this merge could ship.
    CHECK((w.reg.all_of<ecs::MacroSpawnId, ecs::MacroCell, ecs::MacroVisual,
                        ecs::NPCKind, ecs::Pools, ecs::NpcLevel,
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
    // (No position scalar to seed since подпосадка 4: the creation door
    // derives the spawn cell from the world — map centre in a bare fixture.)
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
    // (No position scalar to seed since подпосадка 4: the creation door
    // derives the spawn cell from the world — map centre in a bare fixture.)
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);
    // Stand him on 20,20 the way any placement happens now: by writing the
    // squad's own cell (the jump door's core), not a scalar.
    w.reg.get<ecs::MacroCell>(mine).idx = ecs::cell_index(20, 20, 64);

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
    CHECK(ecs::cell_x(w.reg.get<ecs::MacroCell>(mine), 64) == 20 &&
          ecs::cell_y(w.reg.get<ecs::MacroCell>(mine), 64) == 20,
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
    ecs::World w;
    // The sheet is the OWNED component now (посадка Б): it exists only once
    // the body does, so the build is written through the door and the next
    // ensure walk (the per-tick refresh) moves the ceilings after it.
    ensure_macro_player_entity(gs, w);
    player_sheet(w)->attributes[sm::AttributeId::End] = 5;
    player_sheet(w)->levelData.level = 1;
    ensure_macro_player_entity(gs, w);
    const entt::entity e = player_squad_entity(w);

    const int bornMaxSp = w.reg.get<ecs::Pools>(e).maxSp;
    CHECK(bornMaxSp
              == bar_ceilings(player_sheet(w)->attributes,
                              player_sheet(w)->skills, 100, 100, 100).maxSp,
          "his squad is born with his own stamina bar — the sheet's ceiling");

    // He is wounded, he grows tired, he trains END and he levels. The Pools
    // on the entity ARE the store now (landing 4): the wound and the debt
    // are written straight into it, and the sheet change moves the ceilings
    // through the one door — each bar keeping its FRACTION («доля у всех»),
    // the debt surviving unclamped.
    {
        auto& pools = w.reg.get<ecs::Pools>(e);
        pools.hp = 17;
        pools.sp = -6;   // an honest exhaustion debt
    }
    const int oldMaxHp = w.reg.get<ecs::Pools>(e).maxHp;
    player_sheet(w)->attributes[sm::AttributeId::End] = 12;
    player_sheet(w)->levelData.level = 4;
    // He marched to 33,44 (a cell write — what the walker does); the heal
    // pass below must rescale his numbers WITHOUT touching where he stands.
    w.reg.get<ecs::MacroCell>(e).idx = ecs::cell_index(33, 44, 64);
    ensure_macro_player_entity(gs, w);

    const auto& hp = w.reg.get<ecs::Pools>(e);
    CHECK(hp.maxHp == bar_ceilings(player_sheet(w)->attributes,
                                   player_sheet(w)->skills, 100, 100, 100).maxHp,
          "the bigger bar the new END bought reached the entity");
    CHECK(hp.hp == int(float(hp.maxHp) * (17.0f / float(oldMaxHp))),
          "the wound rescaled by its FRACTION — no free heal, no theft");
    CHECK(hp.maxSp == bar_ceilings(player_sheet(w)->attributes,
                                   player_sheet(w)->skills, 100, 100, 100).maxSp,
          "the stamina ceiling followed the END he trained");
    CHECK(hp.maxSp > bornMaxSp,
          "negative control: that ceiling did MOVE — the check above is not "
          "comparing two copies of the same stale number");
    CHECK(hp.sp == -6, "the exhaustion DEBT survives the rescale, unclamped");
    CHECK(w.reg.get<ecs::NpcLevel>(e).value == 4, "and he is level 4 to the map");
    const auto& cellNow = w.reg.get<ecs::MacroCell>(e);
    CHECK(ecs::cell_x(cellNow, 64) == 33 && ecs::cell_y(cellNow, 64) == 44,
          "the entity stands where he stands");
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
// what this pins is that the one door says the PLAYER's numbers, not a
// generic adventurer's — read off the OWNED sheet component on his entity
// (посадка Б: the storedSheet parameter died, no caller can forget it).
void test_one_door_assembles_every_battle_side() {
    GameState gs{};
    gs.mapW = gs.mapH = 64;
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    player_sheet(w)->attributes[sm::AttributeId::Str] = 18;
    player_sheet(w)->attributes[sm::AttributeId::End] = 18;
    player_sheet(w)->levelData.level = 5;
    // The per-tick walk moves the ceilings after the build change.
    ensure_macro_player_entity(gs, w);
    const entt::entity e = player_squad_entity(w);
    {
        auto& pools = w.reg.get<ecs::Pools>(e);
        pools.hp = pools.maxHp / 2;
        pools.sp = pools.maxSp / 4;
    }

    // A transient squad owns no sheet — the same door, the derive path.
    // Spawned BEFORE `mine` is assembled: the side carries a pointer into
    // the roster pool, and a later spawn may reallocate it (ecs-ref grabla).
    const entt::entity transient = npc_squad(w, 30.0f, 30.0f, 9u, 0);
    const AutoBattleSide mine = auto_battle_side_of(w, e);
    const AutoBattleSide generic = auto_battle_side_of(w, transient);

    CHECK(mine.leaderHpOverride
              == float(std::max(1, bar_ceilings(player_sheet(w)->attributes,
                                                player_sheet(w)->skills, 100, 100, 100).maxHp)),
          "owning his sheet, the door states HIS ceiling");
    CHECK(generic.leaderHpOverride < 0.0f,
          "negative control: a transient with no owned sheet derives from "
          "the row — the component is what makes the difference");
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
    ecs::World w;
    ensure_macro_player_entity(gs, w);
    player_sheet(w)->attributes[sm::AttributeId::End] = 10;
    // The per-tick walk moves the ceilings after the build change.
    ensure_macro_player_entity(gs, w);
    const entt::entity mine = player_squad_entity(w);
    const int maxHp = w.reg.get<ecs::Pools>(mine).maxHp;

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

    const auto& hp = w.reg.get<ecs::Pools>(mine);
    CHECK(hp.hp == std::floor(hp.maxHp * 0.5f),
          "the door wrote his wound onto the entity, by the entity's ceiling");
    CHECK(hp.hp > 0 && hp.hp < maxHp,
          "negative control: he is HURT, not untouched and not dead — the "
          "fraction actually travelled");
    CHECK(!w.reg.all_of<ecs::Dead>(mine),
          "a survivor is not marked dead by the shared door");
}

// ── Phase 4: THE sheet door ──────────────────────────────────────────────
// player_effective_sheet is the ONE read of the player's numbers (owner,
// 2026-09-06: «финальное после всего — и его везде использует»). What it
// buys, pinned with its negative controls: a worn bonus is IN the answer,
// the BASE sheet never moves, and taking the item off leaves no residue.
void test_the_sheet_door_reads_what_is_standing() {
    ecs::World w;
    GameState gs;
    ensure_macro_player_entity(gs, w);
    player_sheet(w)->attributes[AttributeId::End] = 8;

    const CharacterSheet bare = player_effective_sheet(w);
    CHECK(bare.attributes.of(AttributeId::End) == 8,
          "nothing worn, nothing burning: the door answers the base sheet");

    const entt::entity squad = player_squad_entity(w);
    CHECK_OR_RETURN(squad != entt::null, "the player's squad exists");
    auto& eq = w.reg.emplace<ecs::BodyEquipment>(squad);
    ItemRef plate{};
    plate.count = 1;
    plate.set_affix(0, {std::uint8_t(BonusId::End), +2});
    eq.gear.worn[0] = plate;

    const CharacterSheet dressed = player_effective_sheet(w);
    CHECK(dressed.attributes.of(AttributeId::End) == 10,
          "a worn +2 END is IN the sheet the world asks about");
    // ...and the bar follows, because the bar is derived from the sheet —
    // phase 4's promised effect: the breastplate fattens the SP bar.
    CHECK(bar_ceilings(dressed.attributes, dressed.skills, 100, 100, 100).maxSp
              > bar_ceilings(bare.attributes, bare.skills, 100, 100, 100).maxSp,
          "a worn +END widens what a day of marching can hold");
    CHECK(player_sheet(w)->attributes.of(AttributeId::End) == 8,
          "the BASE sheet never moved — reads walk the door, writes never do");

    // Take it off: the door simply stops summing it.
    eq.gear.worn[0] = ItemRef{};
    CHECK(player_effective_sheet(w)
                  .attributes.of(AttributeId::End) == 8,
          "negative control: off the body, out of the answer — no residue");
}

} // namespace

int main() {
    test_player_carries_everything_a_squad_carries();
    test_the_sheet_door_reads_what_is_standing();
    test_the_mark_survives_losing_the_flag();
    test_ai_leaves_the_player_squad_standing();
    test_the_players_men_never_desert();
    test_the_entity_numbers_are_not_stale();
    test_the_head_is_on_the_entity();
    test_one_door_assembles_every_battle_side();
    test_the_players_wound_settles_through_the_one_door();
    return sm::test::report("player_is_a_squad_test");
}
