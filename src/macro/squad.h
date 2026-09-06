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
#include "core/rng.h"
#include <cstdio>
#include "macro/currency.h"
#include "macro/items.h"
#include "macro/landmark_grid.h"
#include "macro/landmark_registry.h"
#include "macro/macro_stock.h"
#include "macro/player_entity.h"
#include "macro/state.h"
#include "macro/zones.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sm {

// Refresh the four cached scalars a macro leader's march reads
// (ecs::MacroNpcRuntime) from his sheet — THE one door, called by make_npc at
// birth and by award_leader_xp when a level changes the sheet. The old
// ceiling (`maxSp = 2×maxHp`) was the squads' own SP dialect, priced against
// nothing; now the leader's bar is calculate_combat_stats — the same formula
// the player's bar comes from — so "battle of lords" is literal: the lord's
// END is his squad's endurance, his travel skill its road discount, his
// marathon its recovery (owner ruling, Session 21).
// The KIND is required, not defaulted: the back this leader hauls with is a
// column of his row (npc.h NpcTypeDef::haulMult), and a default of "a person"
// would be silently wrong for exactly the rows that matter — a caravan given a
// man's shoulders is a caravan that can no longer afford to travel. A missing
// argument should be a compile error, not a stranded trade route.
inline void refresh_leader_travel_stats(ecs::MacroNpcRuntime& rt,
                                        const CharacterSheet& sheet,
                                        NPCType type) {
    const CombatStats cs =
        calculate_combat_stats(sheet.attributes, sheet.skills);
    rt.maxSp = std::int16_t(std::clamp(cs.maxSp, 1, 32767));
    rt.travelRank = std::uint8_t(
        std::clamp(sheet.skills.of(SkillId::Travel), 0, kMaxSkillRank));
    rt.marathonRank = std::uint8_t(
        std::clamp(sheet.skills.of(SkillId::Marathon), 0, kMaxSkillRank));
    // The cache is the walk's own float; the LAW is whole percent (4в).
    rt.moveMult = float(
        calculate_derived(sheet.attributes, sheet.skills).moveSpeedPct)
        / 100.0f;
    const float haul = npc_def(type).haulMult;
    rt.carryCap = get_carry_capacity(sheet.attributes, sheet.skills)
                  * (haul > 0.0f ? haul : 1.0f);
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
    // The player's own squad never deserts wholesale: he is not a leader whose
    // men wander off when he falls, and losing his roster into the pool would
    // be silent — the tag is the guard the ordinal check would be, for free.
    for (auto [e, roster] :
         w.reg.view<ecs::SquadRoster, ecs::Dead>(
             entt::exclude<ecs::PlayerSquadTag>).each()) {
        (void)e;
        if (roster.squad.empty()) continue;
        // The pool CAN refuse (its own kMaxSquadMembers ceiling): only the
        // men it actually took leave the roster; the rest STAY as the dead
        // lord's band and the next sweep tries again — nobody is destroyed
        // for standing past a cap (CANON S26).
        const int taken = add_squad(deserterPool, roster.squad);
        moved += taken;
        if (taken >= roster.squad.size()) {
            roster.squad.clear();
        } else {
            for (int i = taken; i < roster.squad.size(); ++i) {
                roster.squad[i - taken] = roster.squad[i];
            }
            roster.squad.count -= taken;
        }
    }
    return moved;
}

// ── The END of a dead squad's story (CANON S4, canon audit 2026-08-29) ────
// «Убили всех — сквада на карте нет». A tracked death marks the leader
// entity Dead (the subworld reaper, the auto-resolve, the exhaustion bite)
// and the drain above moves his men to the deserter pool — but nothing ever
// DESTROYED the entity: the corpse-row lived in the ECS and rode every save
// (macro_snapshot dead=1) forever. The squad's identity and deeds are already
// the chronicle's, by save-stable ordinal, so the entity itself has nothing
// left to carry.
//
// DEFERRED on purpose — called once per macro tick, at its end (npc_ai.cpp
// tick drivers), never inside a settle: the settle callers still hold the
// entities (the threat step re-reads `self` after settle_auto_battle, the
// pre-battle modal re-checks its foe), and every LONG-lived reference is
// ordinal-based or validity-guarded by construction (MacroOrigin backlinks
// check reg.valid; MacroDebt receipts and possession resolve spawn ordinals
// through scans that simply find nothing; find_roster fails to a no-op).
//
// A roster the pool REFUSED keeps its entity — the dead lord's band stands
// until the drain takes the men (its own contract: nobody is destroyed for
// standing past a cap, CANON S26). The player's squad is never swept: his
// death is a game-over screen, not a disappearance. Returns how many left
// the map.
inline int destroy_dead_macro_squads(ecs::World& w,
                                     std::int64_t* lootPoolValue = nullptr) {
    std::vector<entt::entity> doomed;
    auto view = w.reg.view<ecs::MacroSpawnId, ecs::SquadRoster, ecs::Dead>(
        entt::exclude<ecs::PlayerTag, ecs::PlayerSquadTag, ecs::SubworldTag>);
    for (auto e : view) {
        if (!view.get<ecs::SquadRoster>(e).squad.empty()) continue;
        doomed.push_back(e);
    }
    for (entt::entity e : doomed) {
        // THE loot pool (owner 2026-08-30, CANON S5): a squad that died
        // with no victor — exhaustion, drowning — FOLDS its belongings
        // into their catalog worth and pays the world's loot pool (one
        // number; a battle's loser was already emptied by
        // loot_fallen_owner, so what folds here is exactly the victorless
        // remainder). Ruins, dungeons and mob drops are the pool's future
        // contextual outflow: loot is ROLLED from tables with a budget
        // drawn off this value — variety by law, O(1) memory.
        if (lootPoolValue) {
            if (const auto* bag = w.reg.try_get<ecs::NpcInventory>(e)) {
                *lootPoolValue += inventory_value(bag->inv);
            }
        }
        w.reg.destroy(e);
    }
    return int(doomed.size());
}

// THE lookup by save-stable ordinal (ecs::MacroSpawnId): the one identity a
// macro entity keeps across a regeneration, so it is what a receipt names
// (ecs::MacroDebt.subject for a roster row) and what possession stores. The
// registry is never serialized, so this is a scan — of thousands, not of a
// hot loop: a death, a possession, a load.
inline entt::entity macro_entity_by_spawn_id(ecs::World& w,
                                             std::uint32_t index) {
    for (auto e : w.reg.view<ecs::MacroSpawnId>()) {
        if (w.reg.get<ecs::MacroSpawnId>(e).index == index) return e;
    }
    return entt::null;
}

// ── STANDING, FOR ANY MACRO PARTICIPANT (CANON S20.1) ─────────────────────
//
// Owner's ruling, 2026-08-27: renown is not a squad's private counter — every
// MACRO entity with an identity carries one. A band, a city, a people. Where
// it lives, by participant kind: a squad's rides its runtime component, a
// landmark's answers through the one landmark door (landmark_renown_slot,
// state.h). A CELL has none and never will — the land has no standing — and
// a FACTION's is the politics track's to add: one field, read through this
// same door. `renown_of` is what makes a deed contextual — killing a legend
// is worth a share of the legend — and `grant_renown` is what makes fame
// spread.
inline std::uint32_t* renown_slot(ecs::World& w, GameState& gs,
                                  std::uint8_t participantKind,
                                  std::uint32_t ordinal) {
    switch (fact_subject_kind(participantKind)) {
        case std::uint8_t(FactSubject::Squad): {
            const entt::entity e = macro_entity_by_spawn_id(w, ordinal);
            if (e == entt::null) return nullptr;
            auto* rt = w.reg.try_get<ecs::MacroNpcRuntime>(e);
            return rt ? &rt->renown : nullptr;
        }
        case std::uint8_t(FactSubject::Landmark):
            return landmark_renown_slot(gs, int(ordinal));
        default:
            return nullptr;
    }
}

inline std::uint32_t renown_of(ecs::World& w, GameState& gs,
                               std::uint8_t participantKind,
                               std::uint32_t ordinal) {
    const std::uint32_t* slot = renown_slot(w, gs, participantKind, ordinal);
    return slot ? *slot : 0u;
}

// Saturating, though the ceiling is a formality: the greatest single deed
// against a nobody is worth twenty, so reaching it takes two hundred million
// of them. What the clamp really buys is that ADDITION can never be the thing
// that wraps a legend into a nobody.
inline void grant_renown(ecs::World& w, GameState& gs,
                         std::uint8_t participantKind, std::uint32_t ordinal,
                         std::uint32_t gain) {
    if (gain == 0u) return;
    std::uint32_t* slot = renown_slot(w, gs, participantKind, ordinal);
    if (!slot) return;
    const std::uint64_t sum = std::uint64_t(*slot) + gain;
    *slot = std::uint32_t(std::min<std::uint64_t>(sum, 0xFFFFFFFFull));
}

// ── THE door into the world's memory: file the fact AND pay the doer ──────
//
// ONE door because the two halves must never come apart (S20.1). A writer
// that filed a deed and forgot the renown would be a world where nobody ever
// becomes somebody; a writer that paid renown without filing would be a
// legend nobody can read. Here neither is expressible: you hand over the
// sentence and the doer, and both happen. It lives on the MACRO layer beside
// its landmark twin (record_landmark_fact, state.h) so the app, the UI and
// the subworld engine all knock on the same door — the one-door law had
// drifted across two layers, and the drift was writers that filed for free.
//
// `subject` is the ENTITY that did it, when the caller holds one: the door
// resolves it to its save-stable ordinal (MacroSpawnId). Figure-ness is
// DERIVED from renown for EVERYONE, and the bit is the participant's standing
// AT THE MOMENT of the deed — marked from PRE-deed renown («с этого дня её
// дела идут в анналы»), subject and object alike. No writer hand-marks it: a
// hand-written `true` was how the player's deals filed him as a figure he had
// not yet become.
inline std::uint32_t record_deed(ecs::World& w, GameState& gs, WorldFact fact,
                                 entt::entity subject = entt::null) {
    if (subject != entt::null && w.reg.valid(subject)) {
        const auto* id = w.reg.try_get<ecs::MacroSpawnId>(subject);
        if (id && id->index != 0u) {
            fact.subjectKind = std::uint8_t(FactSubject::Squad);
            fact.subject = id->index;
        }
    }
    fact.subjectKind = fact_subject(
        FactSubject(fact_subject_kind(fact.subjectKind)),
        renown_is_named(renown_of(w, gs, fact.subjectKind, fact.subject)));
    if (fact_subject_kind(fact.objectKind)
        != std::uint8_t(FactSubject::None)) {
        fact.objectKind = fact_subject(
            FactSubject(fact_subject_kind(fact.objectKind)),
            renown_is_named(renown_of(w, gs, fact.objectKind, fact.object)));
    }
    const std::uint32_t seq = chronicle_record(gs.chronicle, fact);
    if (seq != 0u) {
        // WHAT THE DEED WAS WORTH, asked of the world: the base its row gives
        // for a deed against nobody, plus a share of whatever the OBJECT was
        // worth. That is the whole of "fame is made of fame" — no second rule
        // for famous victims, because their fame is already a number the
        // world keeps about them.
        const std::uint32_t gain = renown_for_deed(
            FactKind(fact.kind),
            renown_of(w, gs, fact.objectKind, fact.object));
        grant_renown(w, gs, fact.subjectKind, fact.subject, gain);
    }
    return seq;
}

// ── Auto-battle glue: entity ⇄ the pure resolver ──────────────────────────

// Assemble one resolver side from a live macro squad entity. Everything the
// side needs is read from the entity — the same components every other
// consumer reads — so the resolver and the subworld can never disagree about
// what a squad IS. The leader's sheet seed is derived from his save-stable
// ordinal: the macro layer never stored his birth sheet seed, and both body
// births already re-derive sheets from their own context seeds, so the
// fraction-based wound law is what keeps the layers agreeing (sub/spawn.h).
// `storedSheet`: a leader whose sheet was AUTHORED rather than rolled — today
// that is the player and only the player, tomorrow it is every named lord.
// Pass it and his own build fights the battle; pass nothing and the row is
// derived from (type, level, seed) as a generic leader's always was.
//
// This parameter is what killed the twin. The app used to assemble the
// player's side by hand in `player_auto_battle_side` — twenty lines restating
// health-as-a-fraction, fatigue-as-sp-over-max and roster lookup, beside the
// twenty lines here that said the same things about everyone else. Two
// answers to one question about the world, which CANON S26 forbids by name:
// they had already drifted (the hand-built one read PlayerState while this one
// read the entity), and the merge that made the player an ordinary squad is
// exactly what made one function able to answer for both.
inline AutoBattleSide auto_battle_side_of(ecs::World& w, entt::entity e,
                                          const CharacterSheet* storedSheet
                                              = nullptr) {
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
        s.leaderHealthFraction = hp->maxHp > 0
            ? std::clamp(float(hp->hp) / float(hp->maxHp), 0.0f, 1.0f) : 1.0f;
        if (const auto* rt = reg.try_get<ecs::MacroNpcRuntime>(e)) {
            // sp may be a NEGATIVE debt (exhaustion); the 0.1 floor already
            // says "a squad never fights at literal zero".
            s.fatigue = std::clamp(
                float(rt->sp) / float(std::max<int>(1, rt->maxSp)),
                0.1f, 1.0f);
        }
    }
    if (const auto* roster = reg.try_get<ecs::SquadRoster>(e)) {
        s.roster = &roster->squad;
    }
    if (storedSheet) {
        // His hp ceiling is his OWN sheet's, not a roll of his row — and his
        // aura is what his perks and skills actually say. `leaderDpsOverride`
        // is deliberately NOT set here: a swing is priced by the subworld's
        // melee identity (sub/engine.h), and macro is L1 — it may not reach
        // up. The caller that knows both worlds states that one number.
        const CombatStats cs = calculate_combat_stats(storedSheet->attributes,
                                                      storedSheet->skills);
        s.leaderHpOverride = float(std::max(1, cs.maxHp));
        s.bonuses = squad_bonuses(*storedSheet);
    }
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
                const float frac = hp->maxHp > 0
                    ? std::clamp(float(hp->hp) / float(hp->maxHp), 0.0f, 1.0f)
                    : 1.0f;
                const CharacterSheet sheet =
                    make_character_sheet(type, lvl->value, seed);
                const CombatTemplate pc =
                    project_combat(sheet, npc_def(type).combat);
                hp->maxHp = std::max(1, int(pc.hp));
                hp->hp = std::clamp(int(float(hp->maxHp) * frac),
                                    1, hp->maxHp);
                // The march caches follow the sheet through the same door,
                // preserving the SP fraction like the wound above — a level
                // is not a free rest. An exhaustion DEBT (sp < 0) survives
                // as-is: levelling mid-collapse does not forgive it.
                const float spFrac = float(rt->sp)
                    / float(std::max<int>(1, rt->maxSp));
                refresh_leader_travel_stats(*rt, sheet, type);
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

// ── The fallen SPEAK (damage-door track Inc 6) ────────────────────────────
// An auto-resolved fight used to be MUTE: no facts, so quest kill-tallies
// never counted it; no kill reputation, so a massacre by auto-resolve cost
// nothing; and the spoils were whatever bag the loser happened to carry —
// the loot registry was never rolled. Below ground the same death paid all
// three. CANON S13 says there is ONE law of battle at both scales, and
// «система обязана объявить, какие факты она эмитит» (work_vector §1); these
// three helpers are that law, spelled once and shared by the AI↔AI settle and
// the player's own auto-resolve.

// Report one death through the envelope's channel (null = nobody listening).
inline void report_death(const MacroWorld& mw, std::uint16_t npcType,
                         entt::entity victim, entt::entity killer,
                         std::int32_t detail, int level,
                         const char* factionId) {
    if (!mw.facts) return;
    BattleFact f{};
    f.kind = BattleFact::Kind::Death;
    f.npcType = npcType;
    f.victim = victim == entt::null
        ? 0u : std::uint32_t(entt::to_integral(victim));
    f.killer = killer == entt::null
        ? 0u : std::uint32_t(entt::to_integral(killer));
    f.detail = detail;
    f.level = level;
    f.factionId = factionId ? factionId : "";
    mw.facts(mw.factsUser, f);
}

// The faction a macro body wears — its INSTANCE colours (Inc 2), not its row.
inline const char* squad_faction_id(ecs::World& w, entt::entity e) {
    const auto* kind = w.reg.try_get<ecs::NPCKind>(e);
    return kind ? faction_id_for_index(kind->factionIdx) : "";
}

// The RNG adapter THE loot registry asks for (RngFn is a bare float(*)()).
// Same shape the macro spawner and the subworld reaper each keep privately.
inline thread_local Rng* gSquadLootRng = nullptr;
inline float squad_loot_rng_f01() {
    return gSquadLootRng ? gSquadLootRng->next_f01() : 0.0f;
}

// What a fallen body of `kind` at `level` was carrying, rolled through THE
// loot registry with the SAME context the subworld reaper builds (the row's
// purse × the cell's danger × the wealth of the place, damage-door Inc 5) —
// so a merchant robbed on the map and a merchant robbed underfoot pay out by
// one law. Coin is minted in the fallen's own realm (W2d: an NPC's purse is
// his faction's currency).
inline void roll_fallen_spoils(const MacroWorld& mw, std::uint16_t kind,
                               int level, int cellX, int cellY,
                               const char* factionId, Rng& rng,
                               Inventory& into) {
    if (!valid_npc_kind(kind)) return;
    const NPCType type = NPCType(std::uint8_t(kind));
    CorpseLootContext ctx{};
    if (mw.zones) ctx.danger = mw.zones->at(cellX, cellY);
    if (mw.landmarks) {
        ctx.wealthMul =
            landmark_def(mw.landmarks->at(cellX, cellY).type).wealthMul;
    }
    gSquadLootRng = &rng;
    const char* lootId = npc_def(type).lootId;
    if (!lootId || !lootId[0]) lootId = npc_loot_id(int(type));
    if (!lootId || !lootId[0]) lootId = factionId;
    for (const ItemRef& s :
         roll_loot_profile(lootId, level, &squad_loot_rng_f01)) {
        into.add_ref(s);
    }
    const int coins =
        generate_loot_gold(int(type), level, ctx, &squad_loot_rng_f01);
    gSquadLootRng = nullptr;
    if (coins > 0) into.add(currency_for_faction_id(factionId), coins);
}

// Every death this side suffered, told once: the roster rows by their record
// ids and the leader by his entity. Read BEFORE the settle removes the rows.
inline void report_battle_deaths(const MacroWorld& mw, entt::entity side,
                                 const std::vector<std::uint32_t>& casualties,
                                 bool leaderFell, entt::entity killer) {
    if (!mw.facts || !mw.world) return;
    ecs::World& w = *mw.world;
    auto& reg = w.reg;
    const char* factionId = squad_faction_id(w, side);
    if (const auto* roster = reg.try_get<ecs::SquadRoster>(side)) {
        for (const SoldierRecord& r : roster->squad) {
            for (std::uint32_t id : casualties) {
                if (id != r.entityId) continue;
                report_death(mw, r.kind, entt::null, killer,
                             std::int32_t(id),
                             normalize_soldier_level(r.level), factionId);
                break;
            }
        }
    }
    if (leaderFell) {
        const auto* kind = reg.try_get<ecs::NPCKind>(side);
        const auto* lvl = reg.try_get<ecs::NpcLevel>(side);
        report_death(mw, kind ? kind->type : std::uint16_t(0), side, killer,
                     -1, normalize_soldier_level(lvl ? lvl->value : 1),
                     factionId);
    }
}

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
        hp->hp = 0;
        w.reg.emplace_or_replace<ecs::Dead>(e);
        return;
    }
    hp->hp = std::clamp(int(float(hp->maxHp) * fraction), 1, hp->maxHp);
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
        for (const SoldierRecord& r : roster->squad) {
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
    for (ItemRef& stack : bag->inv.slots) {
        if (stack.empty()) continue;
        // Credit first: a stack the victor's bag refuses (full) STAYS on the
        // fallen — a refused pickup leaves the corpse holding it (items.h's
        // conservation ruling), never a cleared slot over an evaporated good.
        if (into.add_ref(stack)) stack = ItemRef{};
    }
}

// ── СМЕРТЬ ГОВОРИТ В ЛЕТОПИСЬ (CANON S20.1, владелец 2026-09-02) ──────────
// The verdict door is the ONLY place the killer is still known, so the
// world's memory is written here and nowhere else — ONE door for EVERY
// auto-resolve, the AI↔AI settle and the player's own button alike (owner
// 2026-09-02, хвост 2б: «игрок ничем не особенен» — his massacres feed the
// same hatred, danger price and threat field as any lord's). Two records,
// each true from its author's side:
//   · Killed — the winner over the fallen loser. Renown flows by the one
//     deed law (record_deed), and «who kills near this village» becomes
//     a chronicle_near_kind question with no new storage.
//   · Died — each bereaved HOME landmark's own loss, object = the killer
//     (a NAMED band by its ordinal, a nameless one by its faction —
//     owner's ruling): the record hatred, revenge quests and the danger
//     term of the refusal price all READ instead of keeping counters.
inline std::int32_t battle_dead(const std::vector<std::uint32_t>& casualties,
                                float leaderFraction) {
    return std::int32_t(casualties.size())
           + (leaderFraction <= 0.0f ? 1 : 0);
}

inline void record_battle_facts(const MacroWorld& mw,
                                entt::entity winner, entt::entity loser,
                                std::int32_t loserDead,
                                std::int32_t winnerDead) {
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    auto& reg = w.reg;
    const auto* battlePos = reg.try_get<ecs::Position>(winner);
    const std::int16_t bx =
        std::int16_t(battlePos ? int(battlePos->x) : 0);
    const std::int16_t by =
        std::int16_t(battlePos ? int(battlePos->y) : 0);
    if (loserDead > 0) {
        WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(FactKind::Killed);
        f.objectKind = std::uint8_t(FactSubject::Squad);
        const auto* lid = reg.try_get<ecs::MacroSpawnId>(loser);
        f.object = lid ? lid->index : 0u;
        f.x = bx;
        f.y = by;
        f.amount = loserDead;
        record_deed(w, gs, f, winner);
    }
    const auto bereave = [&](entt::entity side, entt::entity foe,
                             std::int32_t dead) {
        if (dead <= 0) return;
        const auto* srt = reg.try_get<ecs::MacroNpcRuntime>(side);
        if (!srt || landmark_by_id(gs, srt->homeSettlementId) == nullptr)
            return;   // the homeless bereave nobody — Killed already spoke
        WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(FactKind::Died);
        f.subjectKind = std::uint8_t(FactSubject::Landmark);
        f.subject = std::uint32_t(srt->homeSettlementId);
        const auto* fid = reg.try_get<ecs::MacroSpawnId>(foe);
        const std::uint32_t foeOrd = fid ? fid->index : 0u;
        if (foeOrd != 0u
            && renown_is_named(renown_of(
                   w, gs, std::uint8_t(FactSubject::Squad), foeOrd))) {
            f.objectKind = std::uint8_t(FactSubject::Squad);
            f.object = foeOrd;
        } else {
            const auto* fkind = reg.try_get<ecs::NPCKind>(foe);
            f.objectKind = std::uint8_t(FactSubject::Faction);
            f.object = fkind ? fkind->factionIdx : 0u;
        }
        f.x = bx;
        f.y = by;
        f.amount = dead;
        record_deed(w, gs, f);
    };
    bereave(loser, winner, loserDead);
    bereave(winner, loser, winnerDead);
}

// Settle a resolved AI↔AI auto-battle — the composition of the halves
// above: deaths by name, leader fractions, spoils to the victor when the
// owner fell (a caravan raid PAYS), survivors of a dead leader into the
// deserter pool (the auto-battle IS the whole fight, so its end is here),
// and the winner's leader paid XP through the one reward law.
inline void settle_auto_battle(const MacroWorld& mw,
                               entt::entity ea, entt::entity eb,
                               const AutoBattleOutcome& o) {
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    auto& reg = w.reg;
    const entt::entity winner = o.winner == 0 ? ea : eb;
    const entt::entity loser  = o.winner == 0 ? eb : ea;
    const auto& loserCasualties = o.winner == 0 ? o.casualtiesB
                                                : o.casualtiesA;
    const float loserFraction = o.winner == 0 ? o.leaderFractionB
                                              : o.leaderFractionA;

    int xp = xp_for_fallen(w, loser, loserCasualties, loserFraction <= 0.0f);
    report_battle_deaths(mw, ea, o.casualtiesA,
                         o.leaderFractionA <= 0.0f, eb);
    report_battle_deaths(mw, eb, o.casualtiesB,
                         o.leaderFractionB <= 0.0f, ea);

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

    record_battle_facts(mw, winner, loser,
                        battle_dead(loserCasualties, loserFraction),
                        battle_dead(o.winner == 0 ? o.casualtiesA
                                                  : o.casualtiesB,
                                    o.winner == 0 ? o.leaderFractionA
                                                  : o.leaderFractionB));
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
inline int settle_player_auto_battle(const MacroWorld& mw,
                                     entt::entity enemy,
                                     const AutoBattleOutcome& o,
                                     bool playerIsA) {
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    const auto& playerCas = playerIsA ? o.casualtiesA : o.casualtiesB;
    const auto& enemyCas  = playerIsA ? o.casualtiesB : o.casualtiesA;
    const float playerFraction =
        playerIsA ? o.leaderFractionA : o.leaderFractionB;
    const float enemyFraction =
        playerIsA ? o.leaderFractionB : o.leaderFractionA;
    const bool playerWon = (o.winner == 0) == playerIsA;
    // The player's spoils land in HIS bag — the ordinary NpcInventory on his
    // squad entity (macro/player_entity.h), the same container an enemy
    // lord's goods came out of.
    Inventory* playerBag = player_inventory(w);
    Inventory scratch{};
    if (!playerBag) playerBag = &scratch;   // headless fixture: nowhere to put

    int xp = playerWon
        ? xp_for_fallen(w, enemy, enemyCas, enemyFraction <= 0.0f)
        : 0;

    // The player's fallen leave his roster by the SAME door every squad's do
    // — his squad is an ordinary squad entity now, so this is
    // settle_squad_casualties over his own entity, ledger and all. The
    // hand-written removal that used to stand here was one of the four
    // player-specific paths.
    if (const entt::entity playerSquad = player_squad_entity(w);
        playerSquad != entt::null) {
        settle_squad_casualties(gs, w, playerSquad, playerCas);
    }
    // His wound settles through THE door every leader's does
    // (settle_leader_fraction), not through a second copy of the same three
    // lines of arithmetic. That copy was the last of the four player-specific
    // battle paths: it read PlayerState's ceiling while the door read the
    // entity's, so the two could round differently about one man.
    //
    // PlayerState still OWNS his hp (the store moves onto the entity when the
    // rest of stage 4 lands), so the pool follows the entity the door just
    // wrote — one writer, one direction, and the projection in
    // ensure_macro_player_entity has nothing to fight with.
    if (const entt::entity playerSquad = player_squad_entity(w);
        playerSquad != entt::null) {
        settle_leader_fraction(w, playerSquad,
                               std::clamp(playerFraction, 0.0f, 1.0f));
        if (const auto* hp = w.reg.try_get<ecs::Health>(playerSquad)) {
            gs.player.combatStats.currentHp = int(hp->hp);
        }
    }

    // The enemy's dead are FACTS, and killing them has a PRICE — the same two
    // the fought version pays through the reaper (damage-door Inc 6). The
    // crime is the registry's column, so a bandit costs nothing and a
    // peasant costs the same here as underfoot.
    report_battle_deaths(mw, enemy, enemyCas, enemyFraction <= 0.0f,
                         entt::null);
    const char* enemyFaction = squad_faction_id(w, enemy);
    if (!kill_is_no_crime(enemyFaction)) {
        const int fallen = int(enemyCas.size())
            + (enemyFraction <= 0.0f ? 1 : 0);
        for (int i = 0; i < fallen; ++i) {
            add_player_reputation(gs, enemyFaction, kKillRepPenalty);
        }
    }

    // Spoils: what the fallen CARRIED (their bags) plus what the loot
    // registry says a body of that row is worth where it fell — the same
    // roll the subworld reaper makes, so auto-resolving a caravan and
    // butchering it underfoot pay out by one law. The roster's dead have no
    // bags of their own (they are records, not entities), and this is where
    // they stop dropping nothing at all.
    if (playerWon) {
        const auto* epos = w.reg.try_get<ecs::Position>(enemy);
        const int cx = epos ? int(epos->x) : 0;
        const int cy = epos ? int(epos->y) : 0;
        Rng lootRng(hash3(std::uint32_t(entt::to_integral(enemy)),
                          std::uint32_t(enemyCas.size()),
                          gs.worldSeed));
        if (const auto* roster = w.reg.try_get<ecs::SquadRoster>(enemy)) {
            for (const SoldierRecord& r : roster->squad) {
                for (std::uint32_t id : enemyCas) {
                    if (id != r.entityId) continue;
                    roll_fallen_spoils(mw, r.kind,
                                       normalize_soldier_level(r.level),
                                       cx, cy, enemyFaction, lootRng,
                                       *playerBag);
                    break;
                }
            }
        }
        if (enemyFraction <= 0.0f) {
            const auto* kind = w.reg.try_get<ecs::NPCKind>(enemy);
            const auto* lvl = w.reg.try_get<ecs::NpcLevel>(enemy);
            roll_fallen_spoils(mw, kind ? kind->type : std::uint16_t(0),
                               normalize_soldier_level(lvl ? lvl->value : 1),
                               cx, cy, enemyFaction, lootRng,
                               *playerBag);
        }
    }

    settle_squad_casualties(gs, w, enemy, enemyCas);
    settle_leader_fraction(w, enemy, enemyFraction);
    if (playerWon && w.reg.all_of<ecs::Dead>(enemy)) {
        loot_fallen_owner(w, enemy, *playerBag);
    }
    drain_dead_leader_squads(w, gs.deserterPool);

    // Пара Killed+Died — ТА ЖЕ дверь, что у ИИ↔ИИ (хвост 2б, владелец
    // 2026-09-02): осиротевшие дома жертв игрока получают Died, и
    // ненависть/цена опасности/поле угрозы видят игрока-мясника так же,
    // как любого лорда.
    if (const entt::entity playerSquad = player_squad_entity(w);
        playerSquad != entt::null) {
        const entt::entity pw = playerWon ? playerSquad : enemy;
        const entt::entity pl = playerWon ? enemy : playerSquad;
        record_battle_facts(mw, pw, pl,
                            battle_dead(playerWon ? enemyCas : playerCas,
                                        playerWon ? enemyFraction
                                                  : playerFraction),
                            battle_dead(playerWon ? playerCas : enemyCas,
                                        playerWon ? playerFraction
                                                  : enemyFraction));
    }

    if (xp > 0) {
        // The EFFECTIVE sheet's WIS scales the take (phase 4, the one door —
        // macro/player_entity.h); the exp lands in the BASE levelData.
        const CharacterSheet eff = player_effective_sheet(w, gs.player);
        const DerivedBonuses d = calculate_derived(eff.attributes, eff.skills);
        award_exp(gs.player.sheet.levelData, xp, d.expMultPct);
    }
    return xp;
}

} // namespace sm
