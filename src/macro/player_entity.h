// THE player's squad on the macro map — an ordinary squad record of the one
// smooth store, told from every other squad by two things and two things
// only: a reserved ordinal (`ecs::kPlayerSquadOrdinal`) and the anketa's own
// column `playerFlag`.
//
// «ИГРОК = НПЦ» (CANON S4). «Кем я управляю сейчас» — a DIFFERENT question,
// and it MOVES: onto a possessed lord, onto a body underground. That is why
// it cannot be the thing that identifies his party, and why the two answers
// are two carriers. С 5б (вердикт владельца 2026-09-29) ИСТИНА обоих —
// анкета: колонка playerFlag («кем я на карте») и spawnId ==
// kPlayerSquadOrdinal («кто оригинал»); `GameState::playerFlagBits`/
// `playerSquadBits` — КЭШИ-хэндлы этих колонок, и вопрос «где флажок»
// остался распаковкой поля мира, скана нет. Перенос флажка — ОДНА дверь
// transfer_player_flag (store.h): колонка + кэш одним движением.
//
// This entity used to be a HUSK: `Position` + a tag, recreated every macro
// tick, deliberately invisible to render / proximity / AI, while the real
// party lived beside it in PlayerState as a creatures, a bag and a head of its
// own. Every consumer of those was a player-specific path — a second kind
// of squad with its own projection, its own battle side and its own casualty
// bookkeeping. The merge of 2026-08-27 collapsed them: the creatures is
// `ecs::SquadUpkeep`, the bag `ecs::NpcInventory`, the head `AgentMemory`, and
// all three ride the same macro-snapshot record every lord's do.
//
// EVERYTHING has moved. WHERE he stands moved HERE with подпосадка 4
// (2026-09-10, v88): his cell is the ordinary ecs::MacroCell of his record,
// stepped by the input walker and glided by the one MacroVisual integrator.
// How hurt and how tired he is moved HERE with landing 4 (2026-09-10): his
// three bars are the ordinary ecs::Pools of the record. WHO he built moved
// HERE with посадка Б (2026-09-10, v91): his sheet is the ordinary owned
// CharacterSheet every named character carries — player_sheet() below is
// the one door.
#pragma once
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/store.h"
#include "macro/anketa.h"
#include "macro/entry_context.h"
#include "macro/spell_book_state.h"
#include "macro/state.h"

namespace sm {

// Ensure exactly one player squad exists and both GameState handles are
// honest. Called once at world boot and at the top of every macro
// (non-subworld) tick: it creates the squad on first call (spawn cell
// derived from the world — the realm's first city), re-resolves handles a
// stale field cannot carry (boot after load resolves by the reserved
// ordinal), and keeps the sheet's derivatives honest. It does NOT touch the
// squad's MacroCell — where he stands is the record's own truth
// (подпосадка 4). Idempotent and cheap; never touches a live scene body
// (that lifecycle is owned by SubworldEngine).
void ensure_macro_player_entity(GameState& gs, MacroStore& st);

// ── БИТОВЫЕ ДВЕРИ (1е кластер 5): истина — два поля GameState ────────────
// «Кем я на карте» и «кто оригинал» — распаковка полей, ноль сканов.
// Валидность хэндла (поколение) спрашивается у store читателем, которому
// она нужна; сентинель «никого» распаковывается в kMacroNoSlot.
inline MacroHandle player_flag_handle(const GameState& gs) {
    return macro_handle_from_bits(gs.playerFlagBits);
}
inline MacroHandle player_squad_handle(const GameState& gs) {
    return macro_handle_from_bits(gs.playerSquadBits);
}

// The flag holder's cell and map-glide visual — the SAME columns every
// squad keeps (MacroCell = the one number that is his position's truth,
// MacroVisual = what the eye sees between cells). nullptr before the world
// exists. These replaced the gs.player.x/y scalars: the last duplicate
// store of «where he stands» died with подпосадка 4 (v88).
inline ecs::MacroCell* player_flag_cell(const GameState& gs, MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return nullptr;
    return &st.cell[h.slot];
}
inline const ecs::MacroCell* player_flag_cell(const GameState& gs,
                                              const MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return nullptr;
    return &st.cell[h.slot];
}
inline ecs::MacroVisual* player_flag_visual(const GameState& gs,
                                            MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return nullptr;
    return &st.visual[h.slot];
}

// THE macro jump (escape teleport, console goto, subworld exit door): set
// the flag holder's cell and erase the entry edge (a jump is not a walk —
// SubworldEngine::enter must fall back to the centre). The VISUAL is left
// to the one glide integrator on purpose (owner 2026-09-10: «универсально
// без игрокового кода») — a short hop glides, a far jump snaps via the
// integrator's own teleport backstop.
inline void player_jump_to_cell(GameState& gs, MacroStore& st, int x, int y) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, gs.mapW)};
    // A jump is not a walk: no entry edge for the next subworld enter, and
    // the think cadence restarts (the accumulator doubles as the player's
    // entry-tick clock — same kAiTicks law as every squad's think).
    auto& rt = st.runtime[h.slot];
    rt.entryDir = kEntryDirNone;
    rt.entryTicks = 0;
    rt.tickAccum = 0;
}

// ОЧНУТЬСЯ В СВОЁМ ТЕЛЕ — the one thing that makes possession an EFFECT rather
// than a bare flag swap (owner 2026-09-14: «если это эффект посессии,
// единственное отличие — что смерть это возвращение в оригинал»). Called the
// moment a possession ENDS: the worn body dying today, a spell expiring
// tomorrow. The flag moves home in one movement — the same one displacement
// that took the body, run backwards.
//
// Returns FALSE when there is nothing to wake up in, and that is the end of the
// game (owner, same day): «если оригинал жив то возвращает в него, если мёртв и
// ты умираешь в посессии то гейм овер». The original is known without storing
// anything — it is `gs.playerSquadBits`, the reserved-ordinal record.
//
// No-op returning false if he was never wearing anyone: then the man who died
// is himself, and there is no return to make.
bool wake_player_in_original_body(GameState& gs, MacroStore& st);

// ── ДВА ХЭНДЛА ИГРОКА В GameState (M-106 1е кластер 5) ───────────────────
// `gs.playerSquadBits` (родной сквад, зарезервированный ординал) и
// `gs.playerFlagBits` («кем я на карте») — packed-хэндлы store. Сентинель
// полей — литерал «все единицы» в state.h (state.h не включает store.h);
// ассерт держит согласие с законом распаковки:
static_assert(macro_handle_from_bits(0xFFFFFFFFu).slot == kMacroNoSlot,
              "сентинель полей игрока GameState (все единицы) обязан "
              "распаковываться в «никого» (macro_handle_from_bits, store.h)");

// Пересборка кэшей игрока — звать строго ПОСЛЕ restore_macro_ecs: истина
// «кто игрок» приехала колонкой playerFlag записей снапшота (5б), и оба
// поля GameState пересобираются из колонок ОДНИМ сканом на границе
// загрузки (home — spawnId == kPlayerSquadOrdinal, флаг — playerFlag == 1).
// Порченый файл (ноль или несколько носителей) не рождает ни безфлажного
// мира, ни двух игроков: колонка чистится, флаг честно складывается домой.
void resolve_player_handles_after_load(GameState& gs, MacroStore& st);

// «Я СЕЙЧАС НЕ В СЕБЕ» — the flag stands on somebody other than the original.
// THE one honest way to ask «вселён ли я»: the fact IS the two handles being
// different, and nothing else. Before this door the chronicle asked it by
// comparing a spawn ordinal to a magic number (macro/journal.h) — a second,
// hand-written answer to a question the flag already answers.
inline bool player_wears_another_body(const GameState& gs) {
    const MacroHandle flag = player_flag_handle(gs);
    const MacroHandle home = player_squad_handle(gs);
    return flag.slot != kMacroNoSlot && home.slot != kMacroNoSlot
        && gs.playerFlagBits != gs.playerSquadBits;
}

// (player_roster умер слиянием M-71: армия игрока — область существ его же
// контейнера, то есть ответ и на «армию», и на «сумку» — player_inventory.)

// …his BAG, which is the ordinary ecs::NpcInventory every macro body
// carries. It was `PlayerState::inventory`: the last large field that made the
// player a different kind of thing from the squads around him.
Inventory* player_inventory(const GameState& gs, MacroStore& st);
const Inventory* player_inventory(const GameState& gs, const MacroStore& st);

// What the player REMEMBERS: the ordinary AgentMemory of the same record, the
// same column every squad leader carries. It sat on PlayerState as a second
// store until 2026-08-27; the macro record already saved the copy, so the
// field was a duplicate the save wrote twice and nothing read back.
// The player's ONE signed fractional stamina carry — `MacroNpcRuntime::spCarry`
// of his record, the very field every lord on the map keeps. It used to
// be two unsigned accumulators on App (a spend-only TravelStamina and a
// regen-only slot in PlayerRecoveryAccumulator), which between them could not
// even express the state his own bar was in: a debt with a fraction owed.
float* player_sp_carry(const GameState& gs, MacroStore& st);

// THE player's three bars — the ordinary ecs::Pools of his record, the
// very block every lord and every scene body keeps (landing 4, owner
// 2026-09-09/10: «полосы на тело, никакого особенного игрока»). It was
// PlayerState::combatStats: a nine-field private store with three cached rest
// rates, projected onto this block every tick and saved TWICE. Returns
// nullptr before the world exists; there are no bars to read then.
ecs::Pools* player_pools(const GameState& gs, MacroStore& st);
const ecs::Pools* player_pools(const GameState& gs, const MacroStore& st);

// THE player's spellbook — the ordinary SpellBook column of his record, the
// block every macro body is born with (§41 root 3, v89). It was
// PlayerState::spellBook: a one-copy store that made casting, sustained
// drains and spire-teaching player-only mechanics. Same family as
// player_pools/player_inventory: the book of the man THE FLAG IS ON. This line
// used to read «his own squad by the reserved ordinal — casting through a
// possessed body's book arrives the day NPC casting does»; that day was
// 2026-09-14, and it arrived not as a feature but as the removal of the wrong
// question. nullptr before the world exists.
SpellBook* player_spellbook(const GameState& gs, MacroStore& st);
const SpellBook* player_spellbook(const GameState& gs, const MacroStore& st);

// «His sheet changed» — the ONE call every such moment makes (creation,
// level-up, point spend, learning, gear on/off, console): ceilings and march
// caches follow the EFFECTIVE sheet through THE door every lord's do
// (squad.h refresh_body_from_sheet), each bar preserving its fraction
// («доля у всех», owner 2026-09-10). No-op before the world exists.
void refresh_player_body(const GameState& gs, MacroStore& st);

AgentMemory* player_head(const GameState& gs, MacroStore& st);

// THE player's OWNED base sheet — the ordinary CharacterSheet column of
// his record (посадка Б, v91): the writable store creation, level-up,
// point spends and learning mutate, the block the macro snapshot already
// carries for every named character (hasSheet). It was PlayerState::sheet —
// the LAST field that made him a different kind of body from the squads
// around him. nullptr before the world exists. Never hold the pointer
// across a simulated tick (ecs-ref grabla: a spawn reallocates storage).
CharacterSheet* player_sheet(const GameState& gs, MacroStore& st);
const CharacterSheet* player_sheet(const GameState& gs, const MacroStore& st);

// The sheet the world should actually ask about him — THE door (phase 4,
// owner 2026-09-06): «финальный лист после всех источников — прокачка,
// врождённое, предмет — и его везде использует». Every reader of his numbers
// (bars, damage, march, carry, prices, XP, UI) walks through here; writes
// (creation, level-up, learning) go to the BASE sheet (player_sheet), never
// to this copy. Since посадка Б this is the universal effective door
// (squad.h effective_sheet_of) asked about his own squad — what stood here
// as player_standing_bonuses dissolved into standing_bonuses_of: the player
// is that door's ordinary case.
CharacterSheet player_effective_sheet(const GameState& gs,
                                      const MacroStore& st);

} // namespace sm
