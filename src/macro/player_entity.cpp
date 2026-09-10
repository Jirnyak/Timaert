#include "macro/player_entity.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/agent_memory.h"
#include "macro/anatomy.h"
#include "macro/character_sheet.h"
#include "macro/faction.h"
#include "macro/npc.h"
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
        squad = reg.create();
        reg.emplace<ecs::MacroSpawnId>(squad, ecs::kPlayerSquadOrdinal);
        reg.emplace<ecs::Position>(squad, gs.player.x, gs.player.y, 0.0f);
        reg.emplace<ecs::VisualPos>(squad, gs.player.x, gs.player.y, 0.0f);
        reg.emplace<ecs::NPCKind>(
            squad, std::uint16_t(NPCType::Adventurer),
            std::uint16_t(faction_index(kPlayerFactionId)));
        reg.emplace<ecs::NpcLevel>(
            squad, std::int16_t(std::max(1, gs.player.sheet.levelData.level)));
        ecs::Pools& pools = reg.emplace<ecs::Pools>(squad, ecs::Pools{});
        reg.emplace<ecs::NpcTraits>(squad, ecs::NpcTraits{});
        {
            Rng faceRng(ecs::kPlayerSquadOrdinal ^ 0x9E3779B9u);
            reg.emplace<ecs::NpcCharacter>(
                squad, ecs::roll_npc_character(faceRng, 160));
        }
        reg.emplace<AgentMemory>(squad);
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
        rt.targetX = gs.player.x;
        rt.targetY = gs.player.y;
        rt.state = std::uint8_t(NPCState::Idle);
        {
            // One assembly of what stands on him, used for both halves: the
            // sheet copy (attr/skill cells) and the derived cells the cache
            // door reads past it (MovePct/CarryKg).
            const BonusTotals st = player_standing_bonuses(world, gs.player);
            refresh_body_from_sheet(pools, &rt,
                                    effective_sheet(gs.player.sheet, st),
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

    // ── Position, projected EVERY walk ────────────────────────────────────
    // PlayerState is still the authoritative store for WHERE he stands (the
    // last scalar of the merge). The bars are NOT projected any more — the
    // Pools on this entity IS the store (landing 4), and the block below only
    // keeps the sheet's derivatives honest.
    //
    // No +0.5 on the position — Position is the raw cell coordinate, and the
    // overlay applies the render centring.
    reg.emplace_or_replace<ecs::Position>(squad, gs.player.x, gs.player.y, 0.0f);
    reg.emplace_or_replace<ecs::NpcLevel>(
        squad, std::int16_t(std::max(1, gs.player.sheet.levelData.level)));
    // The SAME door every lord's numbers go through (squad.h) — the sheet is
    // the law, ceilings and march caches are its cache, and there is one
    // refresh. The EFFECTIVE sheet (phase 4): a worn +END breastplate
    // carries and marches like the body actually wearing it. The ceilings
    // self-gate («доля у всех» rescale only when a ceiling actually moved),
    // so this every-tick walk is an identity while nothing stands or falls
    // off him — and a sheet-change moment anywhere that forgets its own
    // refresh_player_body call heals within one macro tick instead of
    // drifting forever.
    refresh_player_body(gs.player, world);

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

BonusTotals player_standing_bonuses(ecs::World& world,
                                    const PlayerState& player) {
    BonusTotals t{};
    // What he wears. Opt-in: a player who has equipped nothing has no
    // component, and the limiting case costs a lookup.
    if (const entt::entity e = find_player_squad(world); e != entt::null) {
        if (const auto* eq = world.reg.try_get<ecs::BodyEquipment>(e)) {
            t += worn_bonuses(eq->gear);
        }
    }
    // ...and what is burning on him. A sustained spell contributes while it
    // burns and stops the moment it does not — no bookkeeping, because nothing
    // was ever written down. Scaled by his BASE training on purpose: the
    // standing sum cannot read the sheet it is itself a term of.
    for (int ord = 0; ord < kSpellCount; ++ord) {
        if (!player.spellBook.sustained[ord]) continue;
        const SpellDef* def = &kSpellDefs[ord];
        for (const Bonus& b : def->effects) {
            accumulate(t, spell_bonus(b, player.sheet.skills,
                                      spell_school(*def)));
        }
    }
    return t;
}

CharacterSheet player_effective_sheet(ecs::World& world,
                                      const PlayerState& player) {
    return effective_sheet(player.sheet, player_standing_bonuses(world, player));
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

void refresh_player_body(PlayerState& player, ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return;
    auto* pools = world.reg.try_get<ecs::Pools>(e);
    if (!pools) return;
    auto* rt = world.reg.try_get<ecs::MacroNpcRuntime>(e);
    const BonusTotals st = player_standing_bonuses(world, player);
    refresh_body_from_sheet(*pools, rt, effective_sheet(player.sheet, st),
                            NPCType::Adventurer, &st);
}

AgentMemory* player_head(ecs::World& world) {
    const entt::entity e = find_player_squad(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<AgentMemory>(e);
}

const AgentMemory* player_head(const ecs::World& world) {
    return player_head(const_cast<ecs::World&>(world));
}

bool reattach_player_to_macro_spawn(ecs::World& world, int id, float px, float py) {
    if (id < 0) return false;
    auto& reg = world.reg;

    // Find the regenerated macro NPC that carries this deterministic ordinal.
    // spawn_macro_npcs recreated the whole population from `worldSeed` in the same
    // order, so the ordinal that was possessed at save time names the same NPC.
    entt::entity target = entt::null;
    for (auto e : reg.view<ecs::MacroSpawnId, ecs::MacroNpcRuntime>()) {
        if (int(reg.get<ecs::MacroSpawnId>(e).index) == id) { target = e; break; }
    }
    if (target == entt::null) return false;  // died before save / seed changed

    // Collect prior flag holders first — never mutate a pool while iterating it.
    std::array<entt::entity, 8> prior{};
    int n = 0;
    for (auto e : reg.view<ecs::PlayerTag>()) {
        if (e == target) continue;
        if (n >= int(prior.size())) break;
        prior[std::size_t(n++)] = e;
    }
    for (int i = 0; i < n; ++i) {
        const entt::entity e = prior[std::size_t(i)];
        if (!reg.valid(e)) continue;
        // Strip only: the player's OWN squad and any possessed lord are real
        // macro entities that must outlive losing the flag. Nothing here is a
        // husk any more — the husk was the thing this merge deleted.
        reg.remove<ecs::PlayerTag>(e);
        if (!reg.all_of<ecs::MacroNpcRuntime>(e)) reg.destroy(e);
    }

    if (!reg.all_of<ecs::PlayerTag>(target)) reg.emplace<ecs::PlayerTag>(target);
    // The loaded scalar is authoritative for WHERE the player is; the ordinal is
    // authoritative for WHO. Snap the adopted body to the saved cell.
    reg.emplace_or_replace<ecs::Position>(target, px, py, 0.0f);
    return true;
}

} // namespace sm
