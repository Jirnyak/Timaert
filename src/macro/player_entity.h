// THE player's squad on the macro map — an ordinary squad record of the one
// smooth store, told from every other squad by two things and two things
// only: the two bits of the anketa's own column `playerFlag` — `home` («кто
// оригинал») and `on` («кем я на карте»). His ORDINAL is an ordinary one from
// the single emitter, like every squad's: the reserved literal 0x7FFFFFFF died
// with M-255, and with it the last place where the player was a special kind
// of identity rather than a flag on an ordinary record.
//
// «ИГРОК = НПЦ» (CANON S4). «Кем я управляю сейчас» — a DIFFERENT question,
// and it MOVES: onto a possessed lord, onto a body underground. That is why
// it cannot be the thing that identifies his party, and why the two answers
// are two carriers. ЭТО БОЛЬШЕ НЕ ТАК, И ЭТО ВЕРДИКТ: носителей ОДИН (ломоть
// 7б M-421, владелец 2026-11-09 — «`.home` умирает совсем»). ИСТИНА — анкета:
// ОДИН бит колонки playerFlag, `on` («кем я на карте»); `playerFlagRef` — его
// КЭШ-хэндл, и вопрос «где флажок» остался чтением поля мира, скана нет.
// Перенос — ОДНА дверь transfer_player_flag (store.h): колонка + кэш одним
// движением. На вопрос «что у меня ещё есть» отвечает ФРАКЦИЯ
// (`eldest_controlled_squad` ниже), а не второй бит.
//
// This entity used to be a HUSK: `Position` + a tag, recreated every macro
// tick, deliberately invisible to render / proximity / AI, while the real
// party lived beside it in PlayerState as a creatures, a bag and a head of its
// own. Every consumer of those was a player-specific path — a second kind
// of squad with its own projection, its own battle side and its own casualty
// bookkeeping. The merge of 2026-08-27 collapsed them: the creatures is
// `ecs::SquadUpkeep`, the bag `ecs::NpcInventory`, and
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
#include "bus/store.h"
#include "bus/anketa.h"
#include "bus/entry_context.h"
#include "bus/spell_book_state.h"
#include "macro/state.h"

namespace sm {

// Ensure exactly one player squad exists and both GameState handles are
// honest. Called once at world boot and at the top of every macro
// (non-subworld) tick: it creates the squad on first call (spawn cell
// derived from the world — the realm's first city), re-resolves handles a
// stale field cannot carry (boot after load re-resolves by the home BIT of
// the playerFlag column — M-255 killed the reserved-ordinal scan), and keeps
// the sheet's derivatives honest. It does NOT touch the
// squad's MacroCell — where he stands is the record's own truth
// (подпосадка 4). Idempotent and cheap; never touches a live scene body
// (that lifecycle is owned by SubworldEngine).
void ensure_macro_player_entity(GameState& gs, MacroStore& st);

// ── ДВЕРИ ХЭНДЛОВ (1е кластер 5): истина — два поля GameState ────────────
// «Кем я на карте» и «кто оригинал» — чтение полей, ноль сканов (M-220:
// поля несут MacroHandle {слот, ID} напрямую, распаковка битов умерла).
// Валидность хэндла (совпадение ID) спрашивается у store читателем,
// которому она нужна; «никого» — id == 0, закон нуля-ординала.
inline MacroHandle player_flag_handle(const GameState& gs) {
    return gs.playerFlagRef;
}
// (`player_squad_handle` — «кто оригинал» — снесена ломтём 7б M-421 вместе
//  с кэшом и битом. Всякий вопрос «чей лист/сумка/сбруя/стояние/дела»
//  спрашивает АКТИВНОГО: «ты ПОЛНОСТЬЮ тот, в чьём теле стоишь» — вердикт
//  владельца 2026-09-14, и три сайта переехали на него как на ПОЧИНКУ, а не
//  как на миграцию: они читали дом, пока игрок носил лорда.)

// (`player_home_ordinal` — «под каким номером подшиты его дела» — снесён
//  ломтём 7б M-421: дела подшиваются под АКТИВНОГО, и ординал у него
//  спрашивает `player_flag_ordinal` ниже.)

// ОРДИНАЛ ТОГО, НА КОМ ФЛАЖОК — «ЧЬИМ ИМЕНЕМ СДЕЛАНО ДЕЛО». Пока он носит
// лорда, дела подшиваются под ЛОРДА, где бы ни случились (A2, 2026-09-17:
// «вся семья» — его люди платят свои смерти в ЕГО сквад, не в игрока).
inline std::uint32_t player_flag_ordinal(const GameState& gs,
                                         const MacroStore& st) {
    const MacroHandle flag = player_flag_handle(gs);
    return st.valid(flag) ? st.spawnId[flag.slot].index : 0u;
}

// The flag holder's cell — the SAME column every squad keeps (MacroCell =
// the one number that is his position's truth). nullptr before the world
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
// Заявленная точка флажконосца в КЛЕТОЧНЫХ единицах: клетка + целая доля
// (CellSub, кванты 1/256 — M-244a). Этим якорятся камера и всё, кому нужна
// правда мира; ГЛАЙД между клетками — кэш глаз карты
// (tick_macro_eyes@src/ui/macro_overlay.cpp), он представление, не мир.
inline bool player_flag_point(const GameState& gs, const MacroStore& st,
                              float& x, float& y) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return false;
    x = float(ecs::cell_x(st.cell[h.slot], gs.mapW))
        + float(st.runtime[h.slot].sub.x) / float(ecs::kCellSubQuantum);
    y = float(ecs::cell_y(st.cell[h.slot], gs.mapW))
        + float(st.runtime[h.slot].sub.y) / float(ecs::kCellSubQuantum);
    return true;
}

// THE macro jump (escape teleport, console goto, subworld exit door): set
// the flag holder's cell and erase the entry edge (a jump is not a walk —
// SubworldEngine::enter must fall back to the centre). The map EYE is left
// to the one glide cache on purpose (owner 2026-09-10: «универсально
// без игрокового кода») — a short hop glides, a far jump snaps via the
// eye cache's own teleport backstop (tick_macro_eyes).
inline void player_jump_to_cell(GameState& gs, MacroStore& st, int x, int y) {
    // Тело переехало в squad_jump_to_cell@src/bus/store.h (ломоть 5 M-171):
    // прыжок — операция рода 2 по хэндлу. Здесь остались резолв флажка и
    // сторона мира — ровно то, чему нужен GameState.
    squad_jump_to_cell(st, player_flag_handle(gs), x, y, gs.mapW);
}

// (ЗДЕСЬ ЖИЛА `wake_player_in_original_body` — «ОЧНУТЬСЯ В СВОЁМ ТЕЛЕ», та
//  самая строка, которой вселение было ЭФФЕКТОМ, а не голой сменой флажка
//  (владелец 2026-09-14). СНЕСЕНА ломтём 7б M-421 по вердикту владельца
//  2026-11-09: «`.home` умирает совсем … возврат из вселения становится ТОЙ ЖЕ
//  дверью переключения активного, одна дверь на все переключения». Её закон
//  цел и живёт в `keep_active_squad` выше — изменился лишь ВОПРОС: не «жив ли
//  ОДИН конкретный сквад (дом)», а «есть ли под контролем хоть кто-то».
//  Прод-звонящий у неё был ровно один, и он умер вместе с гейтом смерти по
//  телу; держать дверь дальше значило бы хранить легаси, живое одним своим
//  свидетелем.)

// ── ЧТО У ИГРОКА ЕЩЁ ЕСТЬ — ОДНО АГНОСТИЧНОЕ УСЛОВИЕ НАД ФРАКЦИЕЙ ────────
// Вердикт владельца 2026-11-09 (ломоть 7б M-421), дословно: «нужно агностично
// системно условие геймовер это смерть сквада в макромире который игрока
// (также нюанс у игрока может быть несоклько сквадов под контролем то есть
// гейм овер ссмерть всех сквадом с флажком или что у нас игрока)»; и о
// носителе множества: «это н носитель это буквально типа как флажок … (и по
// замыслу именно аткивный один но между ними можно переключатьсчя) но все они
// считаются под контролем - можно просто через фаркцию игрока».
//
// Значит «ПОД КОНТРОЛЕМ» = ФРАКЦИЯ (колонка рода 2 == `kPlayerFactionSlot`), а
// «АКТИВНЫЙ» = бит `.on`, и это РАЗНЫЕ вопросы: активный ровно один, под
// контролем — сколько угодно. Носитель множества не стоит ни одного нового
// байта: фракция у сквада уже есть, как у всякого.
//
// ОДНА ДВЕРЬ НА ОБА ВОПРОСА, И ЭТО НЕ ЭКОНОМИЯ (DOD п.6): «есть ли хоть один»
// и «кто из них старейший» суть ОДИН вопрос о мире — «что у игрока осталось».
// Ответ `id == 0` ЕСТЬ game over; он же называет нового АКТИВНОГО, когда
// прежний пал (вердикт четвёртый того же дня: старейший живой ординал —
// ординал монотонен и не переиспользуется, поэтому правило детерминировано и
// не требует геометрии в момент, когда центра окна уже нет).
//
// HP НЕ СПРАШИВАЕТСЯ НИ РАЗУ, И ЭТО ЗАКОН, А НЕ ЗАБЫВЧИВОСТЬ (ЗАКОН ТОЧКИ):
// сквад мёртв ТОЛЬКО ПУСТЫМ, а ноль здоровья личности есть НОКАУТ — «анкета
// после боя с 0 или меньше хп и ждёт хилится». Сквад с лежащей личностью и
// живым ростером под контролем ОСТАЁТСЯ.
//
// Тотал НЕ ХРАНИТСЯ (DOD п.9: «хранить тотал без разряда А хуже, чем выводить
// каждый раз» — устаревший тотал врёт МОЛЧА). Цена вывода — один линейный
// проход по капу на звонок: 16384 байта колонки живости, в тик это ничто.
MacroHandle eldest_controlled_squad(const MacroStore& st);

// ── КТО ВЕДЁТСЯ СЕЙЧАС — ОДНА ДВЕРЬ ВСЕХ ПЕРЕКЛЮЧЕНИЙ АКТИВНОГО ──────────
// Активный цел — НИЧЕГО не делает. Активный пал (слот протух или на нём байт
// судьбы) — флажок переезжает на СТАРЕЙШЕГО ПОД КОНТРОЛЕМ той же дверью
// `transfer_player_flag`, какой ходят вселение и резолв загрузки. Переезжать
// НЕКОМУ — возвращает FALSE, и это game over: «есть ли сквады под контролем
// игрока хоть один».
//
// НОКАУТ ПЕРЕКЛЮЧЕНИЕМ НЕ ЯВЛЯЕТСЯ: судятся те же два байта, что у двери
// выше, — hp не спрашивается, потому что лежащая личность при живом ростере
// есть сквад под контролем, а точку ведения внутри сцены переставляет
// перенавес (`rehook_squad_point_@src/sub/engine.cpp`), не эта дверь.
//
// ОНА ЖЕ ЕСТЬ ВОЗВРАТ ИЗ ВСЕЛЕНИЯ (вердикт владельца 2026-11-09): носимый
// сквад умер — активным становится старейший свой, и отдельной двери
// «очнуться в оригинале» для этого не нужно. Чинить осиротевший флажок эта
// дверь тоже умеет и по тому же вопросу — «кто ведётся», — поэтому второго
// написания починки в мире быть не должно.
bool keep_active_squad(GameState& gs, MacroStore& st);

// ── ОДИН ХЭНДЛ ИГРОКА В GameState (M-106; второй снесён ломтём 7б M-421) ─
// `gs.playerFlagRef` («кем я на карте») — MacroHandle {слот, ID} напрямую
// (M-220: упаковка в биты и её сентинель «все единицы» умерли; «никого» —
// дефолтный хэндл, id == 0).

// Пересборка кэшей игрока — звать строго ПОСЛЕ restore_macro_ecs: истина
// «кто игрок» приехала колонкой playerFlag записей снапшота (5б), и оба
// поля GameState пересобираются из колонок ОДНИМ сканом на границе
// загрузки ОДНИМ проходом: home — playerFlag.home == 1, флаг — playerFlag.on
// == 1 (M-255 убил второй проход, искавший дом по литералу-ординалу).
// Порченый файл (ноль или несколько носителей) не рождает ни безфлажного
// мира, ни двух игроков: колонка чистится, флаг честно складывается домой.
void resolve_player_handles_after_load(GameState& gs, MacroStore& st);

// (`player_wears_another_body` — «вселён ли я» — снесён ломтём 7б M-421:
//  он был РАЗНОСТЬЮ двух хэндлов, и со смертью второго вопрос перестал
//  быть выразимым. Его единственный прод-читатель, летопись, спрашивает
//  теперь ОДИН ординал активного.)

// (player_roster умер слиянием M-71: армия игрока — область существ его же
// контейнера, то есть ответ и на «армию», и на «сумку» — player_inventory.)

// …his BAG, which is the ordinary ecs::NpcInventory every macro body
// carries. It was `PlayerState::inventory`: the last large field that made the
// player a different kind of thing from the squads around him.
Inventory* player_inventory(const GameState& gs, MacroStore& st);
const Inventory* player_inventory(const GameState& gs, const MacroStore& st);

// (Дверь «что игрок помнит» — `player_head` — и вся колонка `AgentMemory`
// УНИЧТОЖЕНЫ 2026-10-11, перепись сирот: `recall` имел НОЛЬ прод-вызовов,
// то есть никто никогда не вспоминал, а писала память одна недоплата штрафа.
// 2.125 МиБ по капу — 99 % всех мёртвых байт макро-состояния.)
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

// (`refresh_player_body` СНЕСЕНА — M-301. Она была игроко-особой обёрткой
//  над дверью сквада: читала флажок и звала `refresh_body_from_sheet`. Её
//  шапка обещала «ОДИН звонок, который делает каждый такой момент», и это
//  было неправдой по трём классам изменений сразу — надевание, квестовый
//  опыт и гашение сустейна не звали её ни разу, а держали их две страховки.
//  Теперь пересчёт зовёт не МОМЕНТ, а сама ДВЕРЬ изменения, и её имя —
//  `refresh_squad_body@src/bus/squad_sheet.h`: у игрока здесь нет своего пути,
//  как не было и своего закона.)

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
