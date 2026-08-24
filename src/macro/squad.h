// Squad lifecycle helpers — the macro side of "THE macro entity is a squad"
// (macrosim.md, ecs::SquadRoster doctrine). The squad IS its leader entity;
// what lives here is what happens to the roster around the leader's own
// life and death. Header-only: pure ECS + army.h record moves, no engine,
// no renderer, so every layer (subworld leave, the coming auto-resolve,
// tests) settles squads through the same functions.
#pragma once

#include "ecs/world.h"
#include "macro/army.h"
#include "macro/auto_battle.h"
#include "macro/macro_stock.h"
#include "macro/state.h"

#include <algorithm>
#include <cmath>

namespace sm {

// Refresh the four cached scalars a macro leader's march reads
// (ecs::MacroNpcRuntime) from his sheet — THE one door, called by make_npc at
// birth and by award_leader_xp when a level changes the sheet. The old
// ceiling (`maxSp = 2×maxHp`) was the squads' own SP dialect, priced against
// nothing; now the leader's bar is calculate_combat_stats — the same formula
// the player's bar comes from — so "battle of lords" is literal: the lord's
// END is his squad's endurance, his travel skill its road discount, his
// marathon its recovery (owner ruling, Session 21).
inline void refresh_leader_travel_stats(ecs::MacroNpcRuntime& rt,
                                        const CharacterSheet& sheet) {
    const CombatStats cs =
        calculate_combat_stats(sheet.attributes, sheet.skills);
    rt.maxSp = std::int16_t(std::clamp(cs.maxSp, 1, 32767));
    rt.travelRank = std::uint8_t(
        std::clamp(sheet.skills.travel, 0, kMaxSkillRank));
    rt.marathonRank = std::uint8_t(
        std::clamp(sheet.skills.marathon, 0, kMaxSkillRank));
    rt.moveMult =
        calculate_derived(sheet.attributes, sheet.skills).moveSpeedMult;
}

// Owner ruling 3 (macrosim.md): kill the leader and the squad lives on,
// FACELESS, until the fight ends — only then do the survivors stop being a
// squad and fall into the deserter pool, out of which the macro sim later
// raises deserter and bandit bands. "The fight ends" is the caller's word:
// the subworld says it on leave(), the auto-resolve will say it when its
// battle settles. Sweeps every squad whose leader is Dead but whose roster
// still holds members; a live leader's squad is never touched, and a swept
// roster is emptied so the pool can never be paid twice for the same men.
// Returns how many soldiers walked away.
inline int drain_dead_leader_squads(ecs::World& w, SoldierSquad& deserterPool) {
    int moved = 0;
    for (auto [e, roster] :
         w.reg.view<ecs::SquadRoster, ecs::Dead>().each()) {
        (void)e;
        if (roster.members.empty()) continue;
        add_soldiers(deserterPool, roster.members);
        moved += int(roster.members.size());
        roster.members.clear();
    }
    return moved;
}

// ── Auto-battle glue: entity ⇄ the pure resolver ──────────────────────────

// Assemble one resolver side from a live macro squad entity. Everything the
// side needs is read from the entity — the same components every other
// consumer reads — so the resolver and the subworld can never disagree about
// what a squad IS. The leader's sheet seed is derived from his save-stable
// ordinal: the macro layer never stored his birth sheet seed, and both body
// births already re-derive sheets from their own context seeds, so the
// fraction-based wound law is what keeps the layers agreeing (sub/spawn.h).
inline AutoBattleSide auto_battle_side_of(ecs::World& w, entt::entity e) {
    AutoBattleSide s{};
    auto& reg = w.reg;
    if (const auto* kind = reg.try_get<ecs::NPCKind>(e)) {
        if (kind->type < std::uint16_t(NPCType::Count)) {
            s.leaderType = NPCType(std::uint8_t(kind->type));
        }
    }
    if (const auto* lvl = reg.try_get<ecs::NpcLevel>(e)) {
        s.leaderLevel = normalize_soldier_level(lvl->value);
    }
    if (const auto* sid = reg.try_get<ecs::MacroSpawnId>(e)) {
        s.leaderSeed = leader_sheet_seed(sid->index);
    }
    if (const auto* hp = reg.try_get<ecs::Health>(e)) {
        s.leaderHealthFraction = hp->maxHp > 0.0f
            ? std::clamp(hp->hp / hp->maxHp, 0.0f, 1.0f) : 1.0f;
        if (const auto* rt = reg.try_get<ecs::MacroNpcRuntime>(e)) {
            // sp may be a NEGATIVE debt (exhaustion); the 0.1 floor already
            // says "a squad never fights at literal zero".
            s.fatigue = std::clamp(
                float(rt->sp) / float(std::max<int>(1, rt->maxSp)),
                0.1f, 1.0f);
        }
    }
    if (const auto* roster = reg.try_get<ecs::SquadRoster>(e)) {
        s.roster = &roster->members;
    }
    // A generic macro leader has no persistent sheet yet, so no aura sources;
    // the day leaders carry sheets (S17 snapshot / plot lords), collect here.
    return s;
}

// Pay a leader's victory. XP flows through the ONE reward law
// (npc_xp_reward) and is consumed by the SAME curve the player climbs
// (exp_to_next_level); a level gained recomputes the leader's macro ceiling
// from a sheet of the new level while PRESERVING the wound fraction — the
// currency wounds already travel in. This is what makes the "wandering tsar"
// a data row: any leader that wins fights, levels.
inline int award_leader_xp(ecs::World& w, entt::entity e, int xp) {
    if (xp <= 0) return 0;
    auto& reg = w.reg;
    auto* rt = reg.try_get<ecs::MacroNpcRuntime>(e);
    auto* lvl = reg.try_get<ecs::NpcLevel>(e);
    if (!rt || !lvl) return 0;
    rt->xp += xp;
    int gained = 0;
    while (rt->xp >= exp_to_next_level(lvl->value)) {
        rt->xp -= exp_to_next_level(lvl->value);
        lvl->value = std::int16_t(
            std::min<int>(kMaxSoldierLevel, lvl->value + 1));
        ++gained;
    }
    if (gained > 0) {
        if (auto* hp = reg.try_get<ecs::Health>(e)) {
            if (const auto* kind = reg.try_get<ecs::NPCKind>(e);
                kind && kind->type < std::uint16_t(NPCType::Count)) {
                const NPCType type = NPCType(std::uint8_t(kind->type));
                const auto* sid = reg.try_get<ecs::MacroSpawnId>(e);
                const std::uint32_t seed =
                    leader_sheet_seed(sid ? sid->index : 0u);
                const float frac = hp->maxHp > 0.0f
                    ? std::clamp(hp->hp / hp->maxHp, 0.0f, 1.0f) : 1.0f;
                const CharacterSheet sheet =
                    make_character_sheet(type, lvl->value, seed);
                const CombatTemplate pc =
                    project_combat(sheet, npc_def(type).combat);
                hp->maxHp = std::max(1.0f, std::floor(pc.hp));
                hp->hp = std::clamp(std::floor(hp->maxHp * frac),
                                    1.0f, hp->maxHp);
                // The march caches follow the sheet through the same door,
                // preserving the SP fraction like the wound above — a level
                // is not a free rest. An exhaustion DEBT (sp < 0) survives
                // as-is: levelling mid-collapse does not forgive it.
                const float spFrac = float(rt->sp)
                    / float(std::max<int>(1, rt->maxSp));
                refresh_leader_travel_stats(*rt, sheet);
                if (rt->sp > 0) {
                    rt->sp = std::int16_t(std::clamp(
                        int(std::lround(spFrac * float(rt->maxSp))),
                        1, int(rt->maxSp)));
                }
            }
        }
    }
    return gained;
}

// ── The settling halves — one set of doors for EVERY consumer ──────────────
// An auto-battle's outcome lands in the world through exactly the pieces
// below, whether the caller is the AI threat step (two macro entities) or
// the player's own auto-resolve button (Inc 6). No consumer edits a roster
// vector or a health bar directly.

// Roster deaths, by name, through the ledger row.
inline void settle_squad_casualties(GameState& gs, ecs::World& w,
                                    entt::entity e,
                                    const std::vector<std::uint32_t>& ids) {
    auto& reg = w.reg;
    const auto* sid = reg.try_get<ecs::MacroSpawnId>(e);
    const auto* pos = reg.try_get<ecs::Position>(e);
    if (!sid) return;
    MacroWorld mw{.gs = &gs, .world = &w};  // named, not positional — the
                                            // envelope grows, positions rot
    MacroStockKey key{};
    key.subject = std::int32_t(sid->index);
    key.cellX = pos ? std::int16_t(int(pos->x)) : std::int16_t(0);
    key.cellY = pos ? std::int16_t(int(pos->y)) : std::int16_t(0);
    for (std::uint32_t id : ids) {
        key.detail = std::int32_t(id);
        macro_stock_apply(mw, MacroStock::Roster, key, -1);
    }
}

// A leader's post-battle fraction lands in the macro Health; zero is the
// tracked-death shape (hp=0 + Dead), the same mark the subworld reaper
// leaves — so an auto-battle death and a fought death are indistinguishable
// to everything upstream.
inline void settle_leader_fraction(ecs::World& w, entt::entity e,
                                   float fraction) {
    auto* hp = w.reg.try_get<ecs::Health>(e);
    if (!hp) return;
    if (fraction <= 0.0f) {
        hp->hp = 0.0f;
        w.reg.emplace_or_replace<ecs::Dead>(e);
        return;
    }
    hp->hp = std::clamp(std::floor(hp->maxHp * fraction), 1.0f, hp->maxHp);
}

// What the fallen of `loser` are worth, through the ONE reward law. Read
// BEFORE the deaths settle — the reward needs the rows, settling removes
// them. Includes the leader's own worth when the outcome killed him.
inline int xp_for_fallen(ecs::World& w, entt::entity loser,
                         const std::vector<std::uint32_t>& casualties,
                         bool leaderFell) {
    auto& reg = w.reg;
    int xp = 0;
    if (const auto* roster = reg.try_get<ecs::SquadRoster>(loser)) {
        for (const SoldierRecord& r : roster->members) {
            if (!valid_npc_kind(r.kind)) continue;
            for (std::uint32_t id : casualties) {
                if (id == r.entityId) {
                    xp += npc_xp_reward(NPCType(r.kind),
                                        normalize_soldier_level(r.level));
                    break;
                }
            }
        }
    }
    if (leaderFell) {
        if (const auto* kind = reg.try_get<ecs::NPCKind>(loser);
            kind && kind->type < std::uint16_t(NPCType::Count)) {
            const auto* lvl = reg.try_get<ecs::NpcLevel>(loser);
            xp += npc_xp_reward(NPCType(std::uint8_t(kind->type)),
                                normalize_soldier_level(lvl ? lvl->value : 1));
        }
    }
    return xp;
}

// A fallen owner's bag, stack by stack, into any Inventory — the victor's
// macro bag or the player's own.
inline void loot_fallen_owner(ecs::World& w, entt::entity fallen,
                              Inventory& into) {
    auto* bag = w.reg.try_get<ecs::NpcInventory>(fallen);
    if (!bag) return;
    for (const auto& stack : bag->inv.stacks) into.add(stack.id, stack.count);
    bag->inv.stacks.clear();
}

// Settle a resolved AI↔AI auto-battle — the composition of the halves
// above: deaths by name, leader fractions, spoils to the victor when the
// owner fell (a caravan raid PAYS), survivors of a dead leader into the
// deserter pool (the auto-battle IS the whole fight, so its end is here),
// and the winner's leader paid XP through the one reward law.
inline void settle_auto_battle(GameState& gs, ecs::World& w,
                               entt::entity ea, entt::entity eb,
                               const AutoBattleOutcome& o) {
    auto& reg = w.reg;
    const entt::entity winner = o.winner == 0 ? ea : eb;
    const entt::entity loser  = o.winner == 0 ? eb : ea;
    const auto& loserCasualties = o.winner == 0 ? o.casualtiesB
                                                : o.casualtiesA;
    const float loserFraction = o.winner == 0 ? o.leaderFractionB
                                              : o.leaderFractionA;

    int xp = xp_for_fallen(w, loser, loserCasualties, loserFraction <= 0.0f);

    settle_squad_casualties(gs, w, ea, o.casualtiesA);
    settle_squad_casualties(gs, w, eb, o.casualtiesB);
    settle_leader_fraction(w, ea, o.leaderFractionA);
    settle_leader_fraction(w, eb, o.leaderFractionB);

    if (reg.all_of<ecs::Dead>(loser)) {
        if (auto* winnerBag = reg.try_get<ecs::NpcInventory>(winner)) {
            loot_fallen_owner(w, loser, winnerBag->inv);
        }
    }

    drain_dead_leader_squads(w, gs.deserterPool);
    award_leader_xp(w, winner, xp);
}

// Settle the PLAYER's auto-resolve against a macro squad (Inc 6 — the M&B
// button). The player is the same shape as any leader (his entity is the
// leader, PlayerState::army is his roster), so the enemy half goes through
// exactly the halves above; the player half lands where the player's truth
// lives — army rows removed by name, the wound fraction into combatStats
// (the macro scalar his subworld body mirrors), XP through award_exp with
// the wis dividend. By the resolver's own law his head is never diced: he
// reaches 0 only when his whole army died with him — and 0 currentHp is the
// same game-over the fought version ends in. Returns the XP awarded.
inline int settle_player_auto_battle(GameState& gs, ecs::World& w,
                                     entt::entity enemy,
                                     const AutoBattleOutcome& o,
                                     bool playerIsA) {
    const auto& playerCas = playerIsA ? o.casualtiesA : o.casualtiesB;
    const auto& enemyCas  = playerIsA ? o.casualtiesB : o.casualtiesA;
    const float playerFraction =
        playerIsA ? o.leaderFractionA : o.leaderFractionB;
    const float enemyFraction =
        playerIsA ? o.leaderFractionB : o.leaderFractionA;
    const bool playerWon = (o.winner == 0) == playerIsA;

    int xp = playerWon
        ? xp_for_fallen(w, enemy, enemyCas, enemyFraction <= 0.0f)
        : 0;

    for (std::uint32_t id : playerCas) {
        remove_one_soldier_by_entity_id(gs.player.army, id);
    }
    {
        auto& cs = gs.player.combatStats;
        const float frac = std::clamp(playerFraction, 0.0f, 1.0f);
        cs.currentHp = int(std::floor(float(cs.maxHp) * frac));
        if (frac > 0.0f && cs.currentHp < 1) cs.currentHp = 1;
    }

    settle_squad_casualties(gs, w, enemy, enemyCas);
    settle_leader_fraction(w, enemy, enemyFraction);
    if (playerWon && w.reg.all_of<ecs::Dead>(enemy)) {
        loot_fallen_owner(w, enemy, gs.player.inventory);
    }
    drain_dead_leader_squads(w, gs.deserterPool);

    if (xp > 0) {
        const DerivedBonuses d = calculate_derived(gs.player.sheet.attributes,
                                                   gs.player.sheet.skills);
        award_exp(gs.player.sheet.levelData, xp, d.expMult);
    }
    return xp;
}

} // namespace sm
