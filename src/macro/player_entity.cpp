#include "macro/player_entity.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/agent_memory.h"
#include "macro/anatomy.h"
#include "macro/entry_context.h"
#include "macro/character_sheet.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/spell_book_state.h"
#include "macro/spells.h"
#include "macro/squad.h"
#include <algorithm>
#include <array>

namespace sm {

namespace {

// The registry-side cache of the answer below. Lives in the registry's own
// context (not a global — the world owns it, and dies with it), because
// landing 4 turned this lookup from "a handful of calls on a transition"
// into a per-frame door (bars, death check, HUD, spellbook, rest) and a
// linear scan of sixteen thousand squads per call stopped being free.
struct PlayerSquadCache { entt::entity e = entt::null; };

// The player's macro squad, found by its reserved ordinal — through the
// cache, revalidated on every hit: a stale entity id must never be trusted
// (load rebuilds the world; leave() tears entities down), so a cached id
// only answers while it is alive AND still wears the reserved ordinal.
entt::entity find_player_squad(ecs::World& world) {
    auto& cache = world.reg.ctx().emplace<PlayerSquadCache>();
    if (cache.e != entt::null && world.reg.valid(cache.e)) {
        if (const auto* sid = world.reg.try_get<ecs::MacroSpawnId>(cache.e);
            sid && sid->index == ecs::kPlayerSquadOrdinal) {
            return cache.e;
        }
    }
    cache.e = macro_entity_by_spawn_id(world, ecs::kPlayerSquadOrdinal);
    return cache.e;
}

} // namespace

void ensure_macro_player_entity(GameState& gs, ecs::World& world) {
    auto& reg = world.reg;

    // ── The player's squad: an ORDINARY macro squad ────────────────────────
    // Owner's ruling, 2026-08-27: «игрок = обычный сквад, просто с флажком
    // игрока». It used to be a bare husk (Position + PlayerTag) recreated
    // every macro tick, while the real squad lived beside it as
    // PlayerState::army — a second kind of squad with its own projection into
    // the subworld, its own auto-battle side, its own casualty path and its
    // own (absent) cap. Four player-specific paths, which CANON S4 forbids by
    // name.
    //
    // Now it is one entity carrying exactly what any squad carries, and it
    // PERSISTS: `clear_player_entity` (sub/engine.cpp) already spares anything
    // with MacroNpcRuntime — that is the rule that lets a possessed lord
    // survive losing the flag — so the player's own squad survives entering a
    // subworld by the same rule, with no case for itself.
    entt::entity squad = find_player_squad(world);
    if (squad == entt::null) {
        // THE spawn cell, derived from the world itself (подпосадка 4 — no
        // position scalar exists to seed from): the realm's first city, the
        // map centre when the world has none. A LOADED world never reaches
        // this branch — the snapshot restores his squad whole.
        int sx = gs.mapW / 2, sy = gs.mapH / 2;
        if (!gs.politik.cities.empty()) {
            sx = gs.politik.cities[0].x;
            sy = gs.politik.cities[0].y;
        }
        squad = reg.create();
        reg.emplace<ecs::MacroSpawnId>(squad, ecs::kPlayerSquadOrdinal);
        reg.emplace<ecs::MacroCell>(squad,
                                    ecs::cell_index(sx, sy, gs.mapW));
        reg.emplace<ecs::MacroVisual>(squad, float(sx), float(sy), 0.0f);
        reg.emplace<ecs::NPCKind>(
            squad, std::uint16_t(NPCType::Adventurer),
            std::uint16_t(faction_index(kPlayerFactionId)));
        // His OWNED sheet, born WITH the body like every named character's
        // (посадка Б, v91) — the default creation-screen build (the same
        // default_* trio default_player used to copy into PlayerState);
        // apply_creation overwrites it through player_sheet() right after
        // boot. On a LOADED world this branch is never reached: the snapshot
        // restores the component inside his record (hasSheet, v90).
        CharacterSheet birth{};
        birth.attributes = default_attributes();
        birth.skills     = default_skills();
        birth.levelData  = default_level_data();
        const CharacterSheet& sheet = reg.emplace<CharacterSheet>(squad, birth);
        reg.emplace<ecs::NpcLevel>(
            squad, std::int16_t(std::max(1, sheet.levelData.level)));
        ecs::Pools& pools = reg.emplace<ecs::Pools>(squad, ecs::Pools{});
        reg.emplace<ecs::NpcTraits>(squad, ecs::NpcTraits{});
        {
            Rng faceRng(ecs::kPlayerSquadOrdinal ^ 0x9E3779B9u);
            reg.emplace<ecs::NpcCharacter>(
                squad, ecs::roll_npc_character(faceRng, 160));
        }
        reg.emplace<AgentMemory>(squad);
        // His book, born WITH the body like every squad's (v89) — with the
        // starter spell the old PlayerState default carried (state.cpp).
        {
            SpellBook book{};
            spellbook_learn(book, spell_ordinal("magic_bolt"));
            reg.emplace<SpellBook>(squad, book);
        }
        // The snapshot's view names every component make_npc emplaces, and the
        // player's squad is saved BY IT now — neither his roster nor his bag
        // nor his head is a field of PlayerState any more.
        reg.emplace<ecs::NpcInventory>(squad, ecs::NpcInventory{});
        // A squad of one, its own leader — the same empty roster every macro
        // squad is born with (macro/npc_spawn.cpp make_npc).
        reg.emplace<ecs::SquadRoster>(squad);
        // The march caches come from the player's OWN sheet, through the same
        // door every leader's do.
        ecs::MacroNpcRuntime rt{};
        rt.homeSettlementId = -1;
        rt.targetSettlementId = -1;
        rt.targetX = float(sx);
        rt.targetY = float(sy);
        rt.state = std::uint8_t(NPCState::Idle);
        {
            // One assembly of what stands on him, used for both halves: the
            // sheet copy (attr/skill cells) and the derived cells the cache
            // door reads past it (MovePct/CarryKg). Through the universal
            // doors (squad.h) — he is their ordinary case.
            const BonusTotals st = standing_bonuses_of(world, squad);
            refresh_body_from_sheet(pools, &rt,
                                    effective_sheet(sheet, st),
                                    NPCType::Adventurer, &st);
        }
        // Born whole — creation is a moment that SAYS it heals. Every bar,
        // not a subset: a subset is exactly how mana stayed private property.
        pools.hp = pools.maxHp;
        pools.mp = pools.maxMp;
        pools.sp = pools.maxSp;
        reg.emplace<ecs::MacroNpcRuntime>(squad, rt);
    }

    // «Чей это отряд» — emplaced OUTSIDE the creation branch on purpose: a
    // loaded game restores the squad from the snapshot (which carries no tags),
    // so the mark has to be re-stamped every time this door is walked through.
    reg.emplace_or_replace<ecs::PlayerSquadTag>(squad);

    // ── No position projection any more ──────────────────────────────────
    // The MacroCell on this entity IS where he stands (подпосадка 4): the
    // walker steps it, the jump door writes it, the snapshot restores it.
    // The scalar mirror this block used to re-project died with v88 — the
    // anaesthesia-bridge of §41 root 4.
    if (const auto* own = reg.try_get<CharacterSheet>(squad)) {
        reg.emplace_or_replace<ecs::NpcLevel>(
            squad, std::int16_t(std::max(1, own->levelData.level)));
    }
    // The SAME door every lord's numbers go through (squad.h) — the sheet is
    // the law, ceilings and march caches are its cache, and there is one
    // refresh. The EFFECTIVE sheet (phase 4): a worn +END breastplate
    // carries and marches like the body actually wearing it. The ceilings
    // self-gate («доля у всех» rescale only when a ceiling actually moved),
    // so this every-tick walk is an identity while nothing stands or falls
    // off him — and a sheet-change moment anywhere that forgets its own
    // refresh_player_body call heals within one macro tick instead of
    // drifting forever.
    refresh_player_body(world);

    // ── The flag ──────────────────────────────────────────────────────────
    // Exactly one PlayerTag exists at a time, and it is MACRO ONLY since the
    // scale split (2026-09-10): it rides the player's own squad by default
    // and a possessed lord while he wears one — the scene body carries
    // AvatarTag, a different question. The old "is the flag on a subworld
    // body" guard fell away with the possibility it guarded.
    entt::entity flagHolder = entt::null;
    for (auto e : reg.view<ecs::PlayerTag>()) {
        flagHolder = e;
        break;
    }
    if (flagHolder == entt::null) {
        reg.emplace<ecs::PlayerTag>(squad);
    }
}

entt::entity player_squad_entity(ecs::World& world) {
    return find_player_squad(world);
}

CharacterSheet* player_sheet(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<CharacterSheet>(e);
}

const CharacterSheet* player_sheet(const ecs::World& world) {
    return player_sheet(const_cast<ecs::World&>(world));
}

CharacterSheet player_effective_sheet(ecs::World& world) {
    // The universal effective door asked about his own squad (посадка Б) —
    // a missing world answers with the empty sheet a missing body IS.
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return CharacterSheet{};
    return effective_sheet_of(world, e);
}

SoldierSquad* player_roster(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    auto* roster = world.reg.try_get<ecs::SquadRoster>(e);
    return roster ? &roster->squad : nullptr;
}

const SoldierSquad* player_roster(const ecs::World& world) {
    return player_roster(const_cast<ecs::World&>(world));
}

Inventory* player_inventory(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    auto* bag = world.reg.try_get<ecs::NpcInventory>(e);
    return bag ? &bag->inv : nullptr;
}

const Inventory* player_inventory(const ecs::World& world) {
    return player_inventory(const_cast<ecs::World&>(world));
}

float* player_sp_carry(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    auto* pools = world.reg.try_get<ecs::Pools>(e);
    return pools ? &pools->spCarry : nullptr;
}

ecs::Pools* player_pools(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<ecs::Pools>(e);
}

const ecs::Pools* player_pools(const ecs::World& world) {
    return player_pools(const_cast<ecs::World&>(world));
}

void refresh_player_body(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return;
    auto* pools = world.reg.try_get<ecs::Pools>(e);
    if (!pools) return;
    const auto* own = world.reg.try_get<CharacterSheet>(e);
    if (!own) return;
    auto* rt = world.reg.try_get<ecs::MacroNpcRuntime>(e);
    const BonusTotals st = standing_bonuses_of(world, e);
    refresh_body_from_sheet(*pools, rt, effective_sheet(*own, st),
                            NPCType::Adventurer, &st);
}

SpellBook* player_spellbook(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<SpellBook>(e);
}

const SpellBook* player_spellbook(const ecs::World& world) {
    return player_spellbook(const_cast<ecs::World&>(world));
}

AgentMemory* player_head(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<AgentMemory>(e);
}

const AgentMemory* player_head(const ecs::World& world) {
    return player_head(const_cast<ecs::World&>(world));
}

} // namespace sm
