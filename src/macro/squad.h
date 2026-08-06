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

// The macro leader's stamina ceiling — one law, shared by the AI stamina
// regen (npc_ai.cpp) and the fatigue a squad brings into an auto-battle.
inline int macro_npc_max_sp(const ecs::Health& hp) {
    const int fromHealth =
        int(std::lround(std::max(1.0f, hp.maxHp) * 2.0f));
    return std::max(1, fromHealth);
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
        s.leaderSeed = sid->index * 2654435761u + 0x51ADu;
    }
    if (const auto* hp = reg.try_get<ecs::Health>(e)) {
        s.leaderHealthFraction = hp->maxHp > 0.0f
            ? std::clamp(hp->hp / hp->maxHp, 0.0f, 1.0f) : 1.0f;
        if (const auto* rt = reg.try_get<ecs::MacroNpcRuntime>(e)) {
            const int maxSp = macro_npc_max_sp(*hp);
            s.fatigue = std::clamp(float(rt->sp) / float(maxSp), 0.1f, 1.0f);
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
                    (sid ? sid->index : 0u) * 2654435761u + 0x51ADu;
                const float frac = hp->maxHp > 0.0f
                    ? std::clamp(hp->hp / hp->maxHp, 0.0f, 1.0f) : 1.0f;
                const CombatTemplate pc = project_combat(
                    make_character_sheet(type, lvl->value, seed),
                    npc_def(type).combat);
                hp->maxHp = std::max(1.0f, std::floor(pc.hp));
                hp->hp = std::clamp(std::floor(hp->maxHp * frac),
                                    1.0f, hp->maxHp);
            }
        }
    }
    return gained;
}

// Settle a resolved auto-battle into the world — through the SAME doors the
// fought version pays. Roster deaths go through the macro-stock roster row
// by name (never a direct vector edit); a dead leader is hp=0 + Dead,
// exactly the shape the subworld's tracked-death writeback leaves; the
// survivors of a dead leader fall into the deserter pool by the one drain;
// the loser's belongings pass to the victor when their owner fell (bandits
// ROB — that is the point of a caravan raid); the winner's leader is paid
// XP for every fallen enemy through the one reward law.
inline void settle_auto_battle(GameState& gs, ecs::World& w,
                               entt::entity ea, entt::entity eb,
                               const AutoBattleOutcome& o) {
    auto& reg = w.reg;
    MacroWorld mw{&gs, nullptr, &w};

    const entt::entity winner = o.winner == 0 ? ea : eb;
    const entt::entity loser  = o.winner == 0 ? eb : ea;
    const auto& loserCasualties = o.winner == 0 ? o.casualtiesB
                                                : o.casualtiesA;

    // XP is computed BEFORE the deaths are settled: the reward reads the
    // fallen rows (kind, level), and settling removes them.
    int xp = 0;
    if (const auto* roster = reg.try_get<ecs::SquadRoster>(loser)) {
        for (const SoldierRecord& r : roster->members) {
            if (!valid_npc_kind(r.kind)) continue;
            for (std::uint32_t id : loserCasualties) {
                if (id == r.entityId) {
                    xp += npc_xp_reward(NPCType(r.kind),
                                        normalize_soldier_level(r.level));
                    break;
                }
            }
        }
    }

    // Roster deaths, by name, through the ledger row.
    const auto settle_side = [&](entt::entity e,
                                 const std::vector<std::uint32_t>& ids) {
        const auto* sid = reg.try_get<ecs::MacroSpawnId>(e);
        const auto* pos = reg.try_get<ecs::Position>(e);
        if (!sid) return;
        MacroStockKey key{};
        key.subject = std::int32_t(sid->index);
        key.cellX = pos ? std::int16_t(int(pos->x)) : std::int16_t(0);
        key.cellY = pos ? std::int16_t(int(pos->y)) : std::int16_t(0);
        for (std::uint32_t id : ids) {
            key.detail = std::int32_t(id);
            macro_stock_apply(mw, MacroStock::Roster, key, -1);
        }
    };
    settle_side(ea, o.casualtiesA);
    settle_side(eb, o.casualtiesB);

    // Leaders: fractions land in the macro Health; zero is the tracked-death
    // shape (hp=0 + Dead), the same mark the subworld reaper leaves.
    const auto settle_leader = [&](entt::entity e, float fraction) {
        auto* hp = reg.try_get<ecs::Health>(e);
        if (!hp) return;
        if (fraction <= 0.0f) {
            hp->hp = 0.0f;
            reg.emplace_or_replace<ecs::Dead>(e);
            return;
        }
        hp->hp = std::clamp(std::floor(hp->maxHp * fraction),
                            1.0f, hp->maxHp);
    };
    settle_leader(ea, o.leaderFractionA);
    settle_leader(eb, o.leaderFractionB);

    // The spoils: a fallen owner's bag passes to the victor, stack by stack,
    // through the same Inventory the loot system fills.
    if (reg.all_of<ecs::Dead>(loser)) {
        auto* loserBag = reg.try_get<ecs::NpcInventory>(loser);
        auto* winnerBag = reg.try_get<ecs::NpcInventory>(winner);
        if (loserBag && winnerBag) {
            for (const auto& stack : loserBag->inv.stacks) {
                winnerBag->inv.add(stack.id, stack.count);
            }
            loserBag->inv.stacks.clear();
        }
        if (const auto* kind = reg.try_get<ecs::NPCKind>(loser);
            kind && kind->type < std::uint16_t(NPCType::Count)) {
            const auto* lvl = reg.try_get<ecs::NpcLevel>(loser);
            xp += npc_xp_reward(NPCType(std::uint8_t(kind->type)),
                                normalize_soldier_level(lvl ? lvl->value : 1));
        }
    }

    // A dead leader's survivors stop being a squad NOW — the auto-battle IS
    // the whole fight, so its end is here, not at a subworld exit.
    drain_dead_leader_squads(w, gs.deserterPool);

    award_leader_xp(w, winner, xp);
}

} // namespace sm
