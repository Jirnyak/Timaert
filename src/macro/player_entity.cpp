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
#include "macro/store.h"
#include <algorithm>
#include <array>

namespace sm {

namespace {

// The registry-side cache of the answer below. Lives in the registry's own
// context (not a global — the world owns it, and dies with it), because
// landing 4 turned this lookup from "a handful of calls on a transition"
// into a per-frame door (bars, death check, HUD, spellbook, rest) and a
// linear scan of sixteen thousand squads per call stopped being free.
// Шаг 1г: носитель ссылки — хэндл {slot,gen}; entt-энтити при нём —
// мостовая производная (умирает в 1е вместе с MacroSlot), потому что
// ~20 звонящих player_squad_entity до 1е хотят энтити, а скан на каждый
// зов — ровно то, ради чего кэш родился.
struct PlayerSquadCache { entt::entity e = entt::null; MacroHandle h{}; };

// The player's macro squad, found by its reserved ordinal — through the
// cache, revalidated on every hit: a stale id must never be trusted
// (load rebuilds the world; leave() tears entities down), so a cached pair
// only answers while the handle is alive AND still wears the reserved
// ordinal (the generation kills reuse the ordinal check alone could miss).
entt::entity find_player_squad(ecs::World& world) {
    auto& cache = world.reg.ctx().emplace<PlayerSquadCache>();
    if (cache.e != entt::null && world.reg.valid(cache.e)) {
        MacroStore& st = store_of(world);
        if (st.valid(cache.h)
            && st.spawnId[cache.h.slot].index == ecs::kPlayerSquadOrdinal) {
            return cache.e;
        }
    }
    cache.e = macro_entity_by_spawn_id(world, ecs::kPlayerSquadOrdinal);
    cache.h = cache.e != entt::null ? handle_of(world.reg, cache.e)
                                    : MacroHandle{};
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
        MacroStore& st = store_of(world);
        const MacroHandle h = store_birth(st);
        if (!st.valid(h)) return;   // отказ капа уже прозвучал вслух
        squad = reg.create();
        reg.emplace<ecs::MacroSlot>(squad, h.slot);
        st.spawnId[h.slot] = ecs::MacroSpawnId{ecs::kPlayerSquadOrdinal};
        st.cell[h.slot]    = ecs::MacroCell{ecs::cell_index(sx, sy, gs.mapW)};
        st.visual[h.slot]  = ecs::MacroVisual{float(sx), float(sy), 0.0f};
        st.kind[h.slot]    = ecs::NPCKind{
            std::uint16_t(NPCType::Adventurer),
            std::uint16_t(faction_index(kPlayerFactionId))};
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
        st.sheet[h.slot] = birth;
        const CharacterSheet& sheet = st.sheet[h.slot];
        st.level[h.slot] = ecs::NpcLevel{
            std::int16_t(std::max(1, sheet.levelData.level))};
        ecs::Pools& pools = st.pools[h.slot];
        {
            Rng faceRng(ecs::kPlayerSquadOrdinal ^ 0x9E3779B9u);
            st.character[h.slot] = ecs::roll_npc_character(faceRng, 160);
        }
        // His book, born WITH the body like every squad's (v89) — with the
        // starter spell the old PlayerState default carried (state.cpp).
        // Память/ростер/сумка/черты уже обнулены рождением слота.
        spellbook_learn(st.spellBook[h.slot], spell_ordinal("magic_bolt"));
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
        st.runtime[h.slot] = rt;
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
    {
        MacroStore& st = store_of(world);
        const std::uint16_t slot = slot_of(reg, squad);
        st.level[slot] = ecs::NpcLevel{std::int16_t(
            std::max(1, st.sheet[slot].levelData.level))};
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

bool wake_player_in_original_body(ecs::World& world) {
    auto& reg = world.reg;
    const entt::entity flag = player_flag_entity(world);
    const entt::entity home = find_player_squad(world);
    // He was never wearing anyone — the man who died is himself.
    if (flag == entt::null || home == entt::null || flag == home) return false;
    // ВОЗВРАЩАТЬСЯ ЕСТЬ КУДА, ТОЛЬКО ПОКА ЖИВ ОРИГИНАЛ. Asked of the record the
    // same way the world asks it of every other body — the tag the reaper
    // stamps, and the bars themselves, because a body can be at zero for a tick
    // before anything marks it.
    MacroStore& st = store_of(world);
    const std::uint16_t homeSlot = slot_of(reg, home);
    if (st.dead[homeSlot] != 0) return false;
    if (st.pools[homeSlot].hp <= 0.0f) return false;
    // One displacement of one flag — вселение, проигранное назад. Exactly-one
    // holds by the move itself (sub/spawn.h possess_entity does the same).
    for (auto e : reg.view<ecs::PlayerTag>()) {
        if (e != home) reg.remove<ecs::PlayerTag>(e);
    }
    if (!reg.all_of<ecs::PlayerTag>(home)) reg.emplace<ecs::PlayerTag>(home);
    return true;
}

// ── ВСЁ НИЖЕ СПРАШИВАЕТ ФЛАЖОК, А НЕ ОРДИНАЛ (2026-09-14) ────────────────
//
// Девять дверей: лист, эффективный лист, ростер, сумка, spCarry, полосы,
// книга, память, refresh. До этого дня каждая звала `find_player_squad` —
// «запись с зарезервированным номером», то есть ТВОЁ РОДНОЕ ТЕЛО, всегда. Это
// и был корень: вопрос «чьи это полосы» имел ответ «мои», даже когда ты стоял
// в чужом теле, и потому выпитое в теле лорда зелье лечило оставленную
// оболочку (problems.md §48).
//
// Теперь они зовут `player_flag_entity` — «тот, на ком флажок». Ни одна из
// ~30 точек вызова не изменилась: они всегда спрашивали правильную вещь, это
// дверь отвечала не про того. Ты ПОЛНОСТЬЮ тот, в чьём теле стоишь: его лист,
// его люди, его сумка, его книга (вердикт владельца 2026-09-14 — «вся семья»).
// Твоя сумка не пропала, она на твоём теле, там, где ты его оставил стоять.
//
// `player_squad_entity` ВЫШЕ остаётся ординальной — и это не исключение, а
// другой вопрос: «КТО ОРИГИНАЛ», обратный адрес одержимости. Смотри
// `wake_player_in_original_body` ниже: именно потому третий тег и переживает
// эту правку — у него появилась собственная работа.
CharacterSheet* player_sheet(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).sheet[slot_of(world.reg, e)];
}

const CharacterSheet* player_sheet(const ecs::World& world) {
    return player_sheet(const_cast<ecs::World&>(world));
}

CharacterSheet player_effective_sheet(ecs::World& world) {
    // The universal effective door asked about his own squad (посадка Б) —
    // a missing world answers with the empty sheet a missing body IS.
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return CharacterSheet{};
    return effective_sheet_of(world, e);
}

Inventory* player_inventory(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).inventory[slot_of(world.reg, e)].inv;
}

const Inventory* player_inventory(const ecs::World& world) {
    return player_inventory(const_cast<ecs::World&>(world));
}

float* player_sp_carry(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).pools[slot_of(world.reg, e)].spCarry;
}

ecs::Pools* player_pools(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).pools[slot_of(world.reg, e)];
}

const ecs::Pools* player_pools(const ecs::World& world) {
    return player_pools(const_cast<ecs::World&>(world));
}

void refresh_player_body(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return;
    MacroStore& ms = store_of(world);
    const std::uint16_t slot = slot_of(world.reg, e);
    ecs::Pools* pools = &ms.pools[slot];
    const CharacterSheet* own = &ms.sheet[slot];
    auto* rt = &ms.runtime[slot];
    const BonusTotals st = standing_bonuses_of(world, e);
    // The ROW is the record's own (A1, 2026-09-17): the door follows the
    // flag, so its row must too — a worn lord's ceilings and haul are his
    // row's, not the Adventurer's. His own squad IS an Adventurer by birth
    // (ensure above), so the fallback for a record without a row says the
    // same thing the old literal did — for exactly the body it was true of.
    const auto& kind = ms.kind[slot];
    refresh_body_from_sheet(*pools, rt, effective_sheet(*own, st),
                            NPCType(kind.type), &st);
}

SpellBook* player_spellbook(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).spellBook[slot_of(world.reg, e)];
}

const SpellBook* player_spellbook(const ecs::World& world) {
    return player_spellbook(const_cast<ecs::World&>(world));
}

AgentMemory* player_head(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return &store_of(world).memory[slot_of(world.reg, e)];
}

const AgentMemory* player_head(const ecs::World& world) {
    return player_head(const_cast<ecs::World&>(world));
}

} // namespace sm
