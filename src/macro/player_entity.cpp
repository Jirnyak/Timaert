#include "macro/player_entity.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/agent_memory.h"
#include "macro/entry_context.h"
#include "macro/landmark_iter.h"   // for_each_place — места по слотам
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/spell_book_state.h"
#include "macro/anketa.h"
#include "macro/squad.h"
#include "macro/store.h"
#include <algorithm>
#include <array>

namespace sm {

// (PlayerSquadCache и find_player_squad умерли в 1е кластере 5: ответ «кто
// оригинал» стал распаковкой `gs.playerSquadBits` — кэшировать распаковку
// поля нечего, а скан по ординалу остался ровно один, на границе резолва
// ниже, где биты ещё не назначены.)

void ensure_macro_player_entity(GameState& gs, MacroStore& st) {

    // ── The player's squad: an ORDINARY macro squad ────────────────────────
    // Owner's ruling, 2026-08-27: «игрок = обычный сквад, просто с флажком
    // игрока». It used to be a bare husk recreated every macro tick, while
    // the real squad lived beside it as PlayerState::army — a second kind of
    // squad with its own projection into the subworld, its own auto-battle
    // side, its own casualty path and its own (absent) cap. Four
    // player-specific paths, which CANON S4 forbids by name.
    //
    // «Кто оригинал» — биты GameState; протухшие после загрузки или пустые
    // на буте биты резолвятся зарезервированным ординалом ОДИН раз (граница,
    // не тик: валидные биты — распаковка без скана).
    MacroHandle home = player_squad_handle(gs);
    if (!st.valid(home)
        || st.spawnId[home.slot].index != ecs::kPlayerSquadOrdinal) {
        home = macro_handle_by_spawn_id(st, ecs::kPlayerSquadOrdinal);
    }
    if (home.slot == kMacroNoSlot) {
        // THE spawn cell, derived from the world itself (подпосадка 4 — no
        // position scalar exists to seed from): the realm's first city, the
        // map centre when the world has none. A LOADED world never reaches
        // this branch — the snapshot restores his squad whole.
        int sx = gs.mapW / 2, sy = gs.mapH / 2;
        // ПЕРВЫЙ ГОРОД ПОПУЛЯЦИИ (ломтик F): места рождаются в порядке плана
        // генератора, и слоты выдаются по возрастанию, поэтому первый
        // City-слот и есть тот город, который здесь стоял списком
        // `politik.cities[0]`. Второго списка городов у мира больше нет —
        // план умер вместе с генезисом.
        bool cityFound = false;
        for_each_place(st, [&](std::uint16_t slot) {
            if (cityFound) return;
            if (SquadType(st.runtime[slot].squadType) != SquadType::City)
                return;
            sx = ecs::cell_x(st.cell[slot], gs.mapW);
            sy = ecs::cell_y(st.cell[slot], gs.mapW);
            cityFound = true;
        });
        const MacroHandle h = store_birth(st);
        if (!st.valid(h)) return;   // отказ капа уже прозвучал вслух
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
        // restores the record whole (hasSheet, v90).
        CharacterSheet birth{};
        birth.attributes = default_attributes();
        birth.skills     = default_skills();
        birth.levelData  = default_level_data();
        st.sheet[h.slot] = birth;
        // Тело игрока — маска строки Adventurer (M-183).
        gear_init(st.gear[h.slot].gear, npc_def(NPCType::Adventurer).slots);
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
        rt.homeSettlementId = 0;
        rt.targetSettlementId = 0;
        rt.targetX = float(sx);
        rt.targetY = float(sy);
        rt.state = std::uint8_t(NPCState::Idle);
        {
            // One assembly of what stands on him, used for both halves: the
            // sheet copy (attr/skill cells) and the derived cells the cache
            // door reads past it (MovePct/CarryKg). Through the universal
            // doors (squad.h) — he is their ordinary case.
            const BonusTotals bt = standing_bonuses_of(st, h);
            refresh_body_from_sheet(pools, &rt,
                                    effective_sheet(sheet, bt),
                                    NPCType::Adventurer, &bt);
        }
        // Born whole — creation is a moment that SAYS it heals. Every bar,
        // not a subset: a subset is exactly how mana stayed private property.
        pools.hp = pools.maxHp;
        pools.mp = pools.maxMp;
        pools.sp = pools.maxSp;
        st.runtime[h.slot] = rt;
        home = h;
    }
    gs.playerSquadBits = macro_handle_bits(home);

    // ── No position projection any more ──────────────────────────────────
    // The MacroCell of this record IS where he stands (подпосадка 4): the
    // walker steps it, the jump door writes it, the snapshot restores it.
    st.level[home.slot] = ecs::NpcLevel{std::int16_t(
        std::max(1, st.sheet[home.slot].levelData.level))};

    // ── The flag ──────────────────────────────────────────────────────────
    // Exactly one flag holder exists at a time — «кем я на карте» — and it
    // is MACRO ONLY since the scale split (2026-09-10): his own squad by
    // default, a possessed lord while he wears one (the scene body carries
    // AvatarTag, a different question). Истина — колонка playerFlag анкеты
    // (5б); носитель, не переживший мир (невалидные биты), честно
    // складывается домой ДВЕРЬЮ переноса — мира без флага не бывает.
    if (!st.valid(player_flag_handle(gs))) {
        transfer_player_flag(st, gs.playerFlagBits, home);
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
    refresh_player_body(gs, st);
}

bool wake_player_in_original_body(GameState& gs, MacroStore& st) {
    const MacroHandle flag = player_flag_handle(gs);
    const MacroHandle home = player_squad_handle(gs);
    // He was never wearing anyone — the man who died is himself. Оба хэндла
    // обязаны быть живыми: осиротевший флаг чинит ensure, не wake.
    if (!st.valid(flag) || !st.valid(home)
        || gs.playerFlagBits == gs.playerSquadBits) return false;
    // ВОЗВРАЩАТЬСЯ ЕСТЬ КУДА, ТОЛЬКО ПОКА ЖИВ ОРИГИНАЛ. Asked of the record
    // the same way the world asks it of every other body — the dead column
    // the reaper stamps, and the bars themselves, because a body can be at
    // zero for a tick before anything marks it.
    if (st.dead[home.slot] != 0) return false;
    if (st.pools[home.slot].hp <= 0.0f) return false;
    // One displacement of one flag — вселение, проигранное назад, той же
    // дверью (колонка + кэш одним движением; sub/possess.h ходит ею же).
    transfer_player_flag(st, gs.playerFlagBits, home);
    return true;
}

void resolve_player_handles_after_load(GameState& gs, MacroStore& st) {
    const MacroHandle home =
        macro_handle_by_spawn_id(st, ecs::kPlayerSquadOrdinal);
    // Один скан колонок на границе загрузки (5б): истина приехала колонкой
    // playerFlag записей снапшота, кэши GameState пересобираются из неё.
    MacroHandle flag{};
    std::uint32_t holders = 0;
    for (std::size_t s = 0; s < kMacroEntityCap; ++s) {
        if (!st.alive[s] || st.playerFlag[s].on == 0) continue;
        ++holders;
        flag = handle_at(st, std::uint16_t(s));
    }
    if (holders != 1) {
        // Порченый файл не рождает ни безфлажного мира, ни двух игроков:
        // колонку вычистить, флаг честно домой (мира без флага не бывает).
        for (std::size_t s = 0; s < kMacroEntityCap; ++s)
            st.playerFlag[s].on = 0;
        flag = home;
        if (st.valid(home)) st.playerFlag[home.slot].on = 1;
    }
    gs.playerSquadBits = macro_handle_bits(home);
    gs.playerFlagBits  = macro_handle_bits(flag);
}

// ── ВСЁ НИЖЕ СПРАШИВАЕТ ФЛАЖОК, А НЕ ОРДИНАЛ (2026-09-14) ────────────────
//
// Девять дверей: лист, эффективный лист, сумка, spCarry, полосы, книга,
// память, refresh. До этого дня каждая шла к «записи с зарезервированным
// номером», то есть к ТВОЕМУ РОДНОМУ ТЕЛУ, всегда. Это и был корень: вопрос
// «чьи это полосы» имел ответ «мои», даже когда ты стоял в чужом теле, и
// потому выпитое в теле лорда зелье лечило оставленную оболочку
// (problems.md §48).
//
// Теперь они распаковывают `gs.playerFlagBits` — «тот, на ком флажок». Ты
// ПОЛНОСТЬЮ тот, в чьём теле стоишь: его лист, его люди, его сумка, его
// книга (вердикт владельца 2026-09-14 — «вся семья»).
//
// `player_squad_handle` остаётся ординальной семантикой — и это не
// исключение, а другой вопрос: «КТО ОРИГИНАЛ», обратный адрес одержимости
// (wake_player_in_original_body выше).

namespace {
// Один резолв на все колоночные двери: валидный слот носителя флажка или
// kMacroNoSlot — «мира нет», и дверь честно отвечает nullptr.
inline std::uint16_t flag_slot_or_none(const GameState& gs,
                                       const MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    return st.valid(h) ? h.slot : kMacroNoSlot;
}
} // namespace

CharacterSheet* player_sheet(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.sheet[slot];
}
const CharacterSheet* player_sheet(const GameState& gs,
                                   const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.sheet[slot];
}

CharacterSheet player_effective_sheet(const GameState& gs,
                                      const MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return CharacterSheet{};
    return effective_sheet_of(st, h);
}

Inventory* player_inventory(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.inventory[slot].inv;
}
const Inventory* player_inventory(const GameState& gs,
                                  const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.inventory[slot].inv;
}

float* player_sp_carry(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot].spCarry;
}

ecs::Pools* player_pools(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot];
}
const ecs::Pools* player_pools(const GameState& gs, const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot];
}

void refresh_player_body(const GameState& gs, MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return;
    // The ROW is the record's own (A1, 2026-09-17): the door follows the
    // flag, so its row must too — a worn lord's ceilings and haul are his
    // row's, not the Adventurer's.
    const BonusTotals bt = standing_bonuses_of(st, h);
    const auto& kind = st.kind[h.slot];
    refresh_body_from_sheet(st.pools[h.slot], &st.runtime[h.slot],
                            effective_sheet(st.sheet[h.slot], bt),
                            NPCType(kind.type), &bt);
}

SpellBook* player_spellbook(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.spellBook[slot];
}
const SpellBook* player_spellbook(const GameState& gs,
                                  const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.spellBook[slot];
}

AgentMemory* player_head(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.memory[slot];
}

} // namespace sm
