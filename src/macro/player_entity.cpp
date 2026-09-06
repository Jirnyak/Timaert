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

// The player's macro squad, found by its reserved ordinal. A scan of the
// squads, which is what every other by-ordinal lookup does (a handful of
// thousands, on a transition — never in a loop).
entt::entity find_player_squad(ecs::World& world) {
    return macro_entity_by_spawn_id(world, ecs::kPlayerSquadOrdinal);
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
        const float hp = float(std::max(1, gs.player.combatStats.maxHp));
        reg.emplace<ecs::Health>(squad, hp, hp);
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
        refresh_leader_travel_stats(rt,
                                    player_effective_sheet(world, gs.player),
                                    NPCType::Adventurer);
        rt.sp = rt.maxSp;
        reg.emplace<ecs::MacroNpcRuntime>(squad, rt);
    }

    // «Чей это отряд» — emplaced OUTSIDE the creation branch on purpose: a
    // loaded game restores the squad from the snapshot (which carries no tags),
    // so the mark has to be re-stamped every time this door is walked through.
    reg.emplace_or_replace<ecs::PlayerSquadTag>(squad);

    // ── The numbers, projected EVERY walk ─────────────────────────────────
    // PlayerState is still the authoritative store for where he stands, how
    // hurt he is and how tired (those collapse onto the entity in the next
    // step of this merge). Until they do, the entity's copies are projections
    // — and a projection written ONCE at birth is not a projection, it is a
    // lie with a long fuse. All three used to be exactly that: `Health` was
    // stamped from maxHp at creation and never touched again, so the save
    // persisted a full-health player who was dying, and the subworld projector
    // would have raised his squad as an unwounded body; `maxSp` and the march
    // caches were stamped from the sheet at creation, so spending a single END
    // point left them behind forever.
    //
    // This door is walked on EVERY macro tick, and it is the only place these
    // numbers are written, so the projection cannot drift by more than the
    // tick it is refreshed on.
    //
    // No +0.5 on the position — Position is the raw cell coordinate, and the
    // overlay applies the render centring.
    reg.emplace_or_replace<ecs::Position>(squad, gs.player.x, gs.player.y, 0.0f);
    reg.emplace_or_replace<ecs::Health>(
        squad,
        float(std::max(0, gs.player.combatStats.currentHp)),
        float(std::max(1, gs.player.combatStats.maxHp)));
    reg.emplace_or_replace<ecs::NpcLevel>(
        squad, std::int16_t(std::max(1, gs.player.sheet.levelData.level)));
    if (auto* rt = reg.try_get<ecs::MacroNpcRuntime>(squad)) {
        // The SAME door every lord's caches go through (squad.h) — the sheet
        // is the law, these four are its cache, and there is one refresh.
        // The EFFECTIVE sheet (phase 4): a worn +END breastplate carries and
        // marches like the body actually wearing it.
        refresh_leader_travel_stats(*rt,
                                    player_effective_sheet(world, gs.player),
                                    NPCType::Adventurer);
        rt->sp = std::int16_t(std::clamp(gs.player.combatStats.currentSp,
                                         -32768, 32767));
    }

    // ── The flag ──────────────────────────────────────────────────────────
    // Exactly one PlayerTag exists at a time. It rides the player's own squad
    // by default — and rides SOMEONE ELSE while he possesses them, which is
    // why this only claims the flag when nobody macro-side holds it.
    entt::entity flagHolder = entt::null;
    for (auto e : reg.view<ecs::PlayerTag>()) {
        // Never touch a live subworld combat flag: that lifecycle belongs to
        // SubworldEngine, and during a subworld session the flag is on a body.
        if (reg.any_of<ecs::SubworldTag>(e)) return;
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
            const BonusTotals worn = worn_bonuses(eq->gear);
            for (int i = 0; i < kMaxAttributes; ++i)
                t.attr[std::size_t(i)] += worn.attr[std::size_t(i)];
            for (int i = 0; i < kMaxSkills; ++i)
                t.skill[std::size_t(i)] += worn.skill[std::size_t(i)];
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
            accumulate(t, spell_bonus(b, player.sheet.skills));
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
    auto* rt = world.reg.try_get<ecs::MacroNpcRuntime>(e);
    return rt ? &rt->spCarry : nullptr;
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
