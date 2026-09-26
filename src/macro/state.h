// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "macro/attributes.h"
#include "macro/character_sheet.h"
#include "macro/commodity.h"   // kCommodityCount — дань по позициям (v73)
#include "macro/items.h"
#include "macro/memory.h"   // WorldMemory — память мира с горизонтом сезона
#include "macro/agent_memory.h"
#include "macro/army.h"
#include "macro/roster.h"   // Roster — ОДИН ростер на место и на сквад (S4)
#include "macro/interests.h"   // Interests — ВСЕ связи субъекта одной таблицей
#include "macro/landmark_registry.h"
#include "macro/resource_field.h"
#include "macro/npc.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/relations.h"
#include "macro/knowledge.h"
#include "macro/chronicle.h"
#include "macro/scent_field.h"
#include "macro/markers.h"
#include "macro/spell_book_state.h"
#include "macro/map_generator.h"

namespace sm {

// v12: the faction registry unification (macro/faction.h) — one row per
// faction incl. the previously unregistered "magika" and the relation matrix
// re-sampled from the temperament bands in registry order, so a v11 faction
// set / reputation map no longer matches the world the code would regenerate.
// v13: sparse per-cell tree-count overrides (`treeOverrides`) — the persisted
// mutations of the derived TreeLayer (felled trees / future woodcutters).
// v14: forests are no longer a feature — FT_Tree removed (FT_DirtRoad byte
// 3 → 2) and the tree layer derives from the spawn_trees massif mask with
// small biome ambience, so a v13 world's derived layers (and hence its
// override baselines) no longer match what this code regenerates.
// v15: the player's entry-side context (PlayerState entryDir/entryTicks,
// macro/entry_context.h) — which side the player walked into the current macro
// cell from, persisted so a save made at a river bank re-enters the subworld
// with the same army-facing placement.
// v16: the player became an ordinary faction row ("player") and his reputation
// map left PlayerState — his standing is now his row in the ONE relation matrix
// (gs.factions), so the player block no longer carries a string→int map and the
// faction matrix carries one more row.
// v17: the `athletics` skill — training that multiplies the speed `spd` grants
// (attributes add, skills multiply). Skills are a POD block in the save, so a
// new field shifts it.
// v18: the world clock is ONE integer tick (core/time.h WorldTime), not a
// {day, hour, minute} triple with a float minute accumulator riding alongside
// it. The save now states the instant exactly, to 1/64 of a real second, and
// the block shrank from three ints to one u64.
// v19: rng.h next_f01() honest [0,1) (top-24-bit grid, the old /2^32 rounded
// the top codes to exactly 1.0f). Same seed now regenerates a different world,
// so every save that stores a worldSeed is invalidated.
// v20: EventTag renumbered densely after the 16 never-referenced tags were
// deleted — quest onAccept events persist tag VALUES, so the numbering is
// part of the save format.
// v21: TradeRoute carries origin/dest KIND (village vs settlement) — the two
// id spaces both start at zero and the old settlements-first arrival lookup
// credited village revenue to whatever city shared the number.
// v22: lastWorldRebakeDay joins GameState (Session 17) — the monthly re-bake/
// autosave phase used to live on App and reset on every load, so a load
// always pushed the next autosave a full season away.
// v23: the macro-ECS snapshot (Session 17, macro/macro_snapshot.h) — every
// persistent macro NPC rides the save as a record, load restores instead of
// re-spawning from the seed, and MacroSpawnId ordinals come from ONE
// persistent monotonic counter (nextMacroSpawnOrdinal) instead of a
// max-over-living scan that reissued dead men's identities (19.24).
// v24: the world's runtime rhythms (Session 17) — the daily-tick queue with
// its remainder and jitter RNG (WorldTickRuntime, now a GameState member)
// and the macro-AI sweep rhythm (MacroAiRhythm). Without them every load
// re-rolled the SAME jitter sequence, dropped queued days and reset the
// sweep phase.
// v25: story progress (Session 17) — which logic nodes still EXIST and which
// are ACTIVE. Loads used to skip register_intro_story_nodes entirely
// (3 nodes -> 1), so a loaded game lost the intro AND chapter 1.
// v26: sparse deposit overrides (W2a, macro/deposit_layer.h) — the drained
// remains of the world's clay/iron/stone cells, tree-override pattern.
// v27: MacroNpcRuntime grows homeIsVillage (W2b) — an agent's home names its
// id SPACE, so a village woodcutter is finally the village's man; the
// runtime rides the macro snapshot as a POD block, so the layout is format.
// v28: AgentMemory joins the macro record (W2b, macro/agent_memory.h) —
// what a leader remembers (a caravan's market snapshot, a raided village)
// survives the save, 136 padding-free bytes per agent.
// v29: the OLD economy is gone (W2b-4): EconomyState's float arrays, the
// abstract TradeRoute system and cityLastTradeDay leave the save; landmarks
// carry the honest day's readouts instead (starved/unmet/famine + the
// logistic population carry).
// v30: deposit overrides carry the KIND (packed u64, W2c) — a discovered
// iron vein (stone quarry struck iron) must survive a load.
// v31: faction CURRENCIES (owner, W2d): money is a commodity — four coin
// rows replace the "gold" item; treasuries hold the kingdom's coin, purses
// the agent's faction's (an extra make_npc RNG draw re-rolls worlds).
// v32: PlayerState::gold is GONE — the player's money is coin in his
// inventory like every other squad's; PlayerState gains AgentMemory (debt
// facts live there, summed by the fact arithmetic).
// v33: sparse fauna-count overrides (`faunaOverrides`, Session 16) — the
// wild headcount is an honest macro stock: cells the hunt has scarred
// persist, so a cleared pack stays cleared across a load.
// v34: sparse crop-harvest scars (`cropOverrides`, Field Inc F3) — the
// standing wheat is an honest macro stock: what the sickle took stays
// taken across a load and regrows on the world clock.
// v35: ONE resource-field container (Field Inc F7 / R1): fauna and wheat
// scars live in `resourceScars[ResourceFieldId]` — one dialect (the SCAR),
// one generic save block per field. The old fauna remaining-count override
// died with its dialect.
// v36: the forest is the Trees CARRIER row of the resource-field registry
// and the save carries the tree grid WHOLE (a living, growing field is not
// derivable from seed + sparse scars) — `treeOverrides` died with the
// derive-plus-overlay model.
// v37: deposits are three carrier rows (Clay/Iron/Stone) with one sparse map
// PER KIND — a cell may hold several kinds (iron found IN a stone mountain;
// nothing vanishes), so the single-kind packed override died and the save
// carries the deposit cells whole, like the tree grid.
// v38: Spire carries its spell's tier — the spire's whole context (zone gate
// at placement, tower storey count, guard site) derives from it, and the
// subworld may not reach up into the spell registry to recompute it.
// v39: the tier cache dies — the spell registry moved into the world layers
// (macro/spells.h, history/ARCHITECTURE.md Rule 13), so every consumer derives tier
// from spellId at the moment of reading. The save stops carrying a registry
// number as cargo.
// v40: the player's knowledge of the map (macro/knowledge.h) — the explored
// grid rides the save whole, one byte per cell. Visible (2) is a session
// projection of where the player stands and decays to Explored (1) on write;
// a load recomputes sight from the restored position. Terra incognita became
// a fact of the world, so a v39 save — a world the player "knew" entirely —
// no longer describes one.
// v41: spell cooldowns are STEPS, not float seconds (core/time.h). The field
// changed type as well as meaning, so a v40 slot's floats would be read as
// enormous step counts — a saved book would come back locked for hours.
// v42: a squad member's `kind` is 16 bits — the ONE id space bodies already
// share (humanoid ordinal below 0x100, monster catalog row at or above it).
// A beast could not stand in a roster while the field was a byte, so a wolf
// pack was not expressible as a squad; the macro snapshot refused monster
// entities for the same reason. Both are now open (CANON.md S4/S16).
// v44: the player is an ordinary squad, so his goods and his head stopped
// being fields of PlayerState. His bag is the NpcInventory and his memory the
// AgentMemory on his squad entity, both written by the macro-snapshot record
// that already carries every other leader's — two blocks left the player
// section of the file and no block replaced them.
// v45: a macro leader's runtime carries his back — `carryCap` (the sheet's
// carry capacity, cached beside maxSp/travelRank/marathonRank/moveMult) and
// `overloadCost`, the SP surcharge his current load is costing. The overload
// law was the player's alone, so a caravan hauling a ton marched like an
// empty scout; it is universal now (owner ruling), and MacroNpcRuntime rides
// the macro snapshot as a POD block, so its layout IS the format.
// v46: skill RANKS are a flat envelope of bytes and the meanings are rows
// (macro/attributes.h kSkillDefs). The block is the same 32 bytes it was as
// eight ints, which is exactly why the version had to move: a v45 slot would
// load with the same LENGTH and none of the same meaning — four ranks read out
// of one, and no reader the wiser.
// v47: attribute SCORES are a flat envelope of bytes too (macro/attributes.h
// kAttributeDefs), 16 slots for the 9 the game names — so naming the tenth is
// a row, not a format. The block shrank from nine ints to sixteen bytes, and a
// v46 slot would be read at the wrong length entirely.
// v48: an item's effect is rows of the ONE bonus registry (macro/bonus.h).
// `ItemEffect`'s six named ints are gone — three of them were fiction nothing
// read, and one named an attribute the sheet does not have — and `ItemAffix`
// is now literally the registry's `Bonus`. That last one is byte-identical, so
// no saved item moved; the version follows the catalog's meaning changing
// under the same bytes.
// v49: a body's WORN gear rides the macro record (macro/anatomy.h Equipment).
// The cells are a flat array with holes, so the file carries cell INDICES and
// the crowd — which wears nothing — costs a zero count. A two-hander's blocked
// cells are DERIVED on load rather than stored: a second copy of what the
// catalog already says could disagree with it after a retune.
// v50: a settlement's history is a fixed RING of a season (32 days), written
// oldest-first so the file carries a past rather than a ring's seam. It was
// two heap vectors per settlement capped by `erase(begin())` — an O(n) shift
// per town per game day — and the window was 30, a month from another
// calendar; it is kDaysPerSeason now, the span the whole world already grows
// by. The block's LENGTH changed, so the version had to move.
// v51: the world's own memory rides the save (macro/chronicle.h, CANON S20.1).
// Both tiers whole — the ring's LIVE facts (a fresh world costs four bytes,
// not two megabytes of zeroes) and the annals entire, because the annals are
// not a cache but part of the world and a legends mode will read exactly them
// (owner's ruling: «сейв обязательно нёс историю»). The per-cell chains are
// NOT written: a link is derived from the facts, and a stored derivative is a
// second truth waiting to disagree.
// v52: a squad carries RENOWN (ecs::MacroNpcRuntime). A band starts nameless —
// its deeds are weather the ring forgets in a season — and BECOMES a figure by
// doing enough, after which its deeds go into the annals for good (owner,
// 2026-08-27). One number, not a counter plus a flag: "is it named" is derived
// from it, so the two can never disagree about the same band. The runtime is a
// POD block of the macro record, so its layout is the format.
// v53: RENOWN belongs to every MACRO entity with an identity, not to squads
// alone (owner, 2026-08-27) — a band, a city, a people. Settlements and
// villages carry theirs, so the landmark blocks grew a field. And what a deed
// is worth became CONTEXTUAL: the base its row gives plus a share of what the
// victim was worth, which is a number the world already kept about them.
// v54: ONE landmark identity (owner, 2026-08-28). Cities, villages and spires
// used to be numbered from zero in three independent registers, so an id
// alone never named a place — the chronicle paid a village's renown to the
// city wearing the same number, and two crutch bits (MacroNpcRuntime
// homeIsVillage, MacroStockKey subjectIsVillage) existed only to disambiguate.
// Now every landmark draws its id from GameState::nextLandmarkOrdinal — the
// same monotonic-ordinal law MacroSpawnId already lives by — and both
// crutches are dead, which changes the NPC runtime POD's layout.
// v55: ANNIHILATION of worked-out veins (owner, 2026-08-28: «истощённая жила
// — это не существующая жила»). A deposit cell leaves the map when it runs
// dry.
// v56: the scarcity baseline is DERIVED, not stored (owner: world level vs
// «суммарно железа в мире»). DepositLayer::virginUnits is recomputed from
// terrain + seed every boot — the v55 drainedCells counter left the format
// the day it arrived.
// v57: the player's JOURNAL — his knowledge of the world's facts (owner,
// 2026-08-28: the player knows only what he took part in, what happened on
// his cell while he stood there, and — later — rumours; and his journal is
// the log of his WHOLE game, it never forgets). Copies of chronicle records,
// append-only, loud cap; rides in write_player.
// v58: the event log is GONE (owner, 2026-08-28: «чисти ивент лог»). Session
// messages are a fading HUD feed that dies with the moment (SessionFeed,
// never serialized); the player's past is his JOURNAL of chronicle records;
// two of its lines became honest facts instead (a struck vein = Discovered,
// a player's deal = Traded). 8192 saved std::strings leave the format.
// v59: the spellbook is FLAT (macro/spell_book_state.h) — ordinal-indexed
// rows over the append-only spell registry; the string ids and the three
// heap containers left the format.
// v60: dead-code sweep. LayerParameters (a save-prefix POD) dropped its
// never-read tempMin/tempMax columns.
// v61: the world seed is an INTEGER (S26 «всё дискретно»). LayerParameters
// (a save-prefix POD) carries `seed` as uint32_t where a float sat — same
// four bytes, different meaning, so the version moves. Every seed the game
// ever wrote was < 100000 (the UI decimation), exactly representable in
// both types: the worlds themselves are bit-identical.
// v62: ONE landmark roster (CANON S9, owner 2026-08-29). The three vectors
// (settlements / villages / spires) and their three serializers became one
// `std::vector<Landmark>` with a kind column and one serializer; every kind
// writes every column, unused ones ride at zero defaults. A S9 transition
// (village→city, spire→ruin) is now a column flip, not a record move.
// v63: quest identity is an ORDINAL (CANON S20.1, owner 2026-08-29). Quests
// carry nextQuestOrdinal numbers + a POD offer-provenance triple; the id
// string and its FNV event key are dead. The eternal completedQuestIds /
// failedQuestIds string vectors became settledQuestOffers (same-day dedup —
// their only living semantic) + two lifetime counters. The codex unlock
// state is a bit per article ordinal (macro/codex.h registry, was UI-owned
// string tables + a string vector).
// v64: bridges (FT_Bridge, owner 2026-08-29) — the DERIVED world changed.
// The road planner may now pay for a one-cell water crossing (square-on,
// stamped FT_Bridge, always stone), so road networks route differently and
// city pairs that used to component-prune connect. Nothing new is
// serialized — the feature grid is regenerated at boot — but an old slot's
// squads and knowledge would sit in a subtly different world.
// v71: built features (owner 2026-08-31, CANON S10 «фичи создаются
// сквадами») — the cells squads ploughed/built ride the save as their own
// world-field row and are re-stamped onto the seed-baked feature grid at
// load, BEFORE the rebaker reads it. The grid itself stays derived.
// v72: the FIELD law of geology (owner 2026-08-31) — deposits derive from
// noise-field nests, not per-cell hash rolls. Nothing new is serialized,
// but an old slot's veins would sit in a subtly different world.
// v73 (2026-09-02): дань по позициям — Landmark несёт titheOwedGoods[15] +
// titheOwedCoin вместо стоимостного titheOwed (вердикт «по 1/8 каждого
// запаса с округлением вниз»).
// v74 (2026-09-02): корабли через фичу — gs.shipsAtCell (счётчик кораблей
// по клеткам портов/брошенных) + MacroNpcRuntime.aboard/dockX/dockY.
// v75 (2026-09-03): хищник-жертва (CANON S10) — поля следов фракций едут в
// сейве (gs.scent, разреженно) + Robbed ВЫРЕЗАН из FactKind (вердикт
// «минимум систем»: сдвиг ординалов видов фактов).
// v77 (2026-09-03): восьмёрка атрибутов (CANON S14) — VIT вырезан (END = HP
// + SP/2, WILL = MP + SP/2), порядок AttributeId стал каноничным, ординалы
// атрибутов И BonusId сдвинулись; реген всех баров процентный.
// v78 (2026-09-03): скиллы-64 (CANON S14) — SkillId = канонные 32 строки
// (ординалы пересеклись, Fighter → Armsmaster), конверт Skills 32→64 байта,
// LevelData несёт learnPicks (создание = 5 атрибутов + 5 выучиваний),
// раздача 1:1; BonusId получил хвост из 24 скилл-строк (append).
// v79 (2026-09-05): Фаза 3 кубы — SkillId/BonusId получили Unarmed (append,
// ординалы навсегда), ecs::Combat/CombatTemplate несут Dice+flatAdd+LCK
// вместо float damage; сейв инвалидируется правилом репо №2.
// v80 (2026-09-06): Фаза 4г — ecs::Pools стал int{hp,maxHp} (float-хранилище
// умерло: все писатели целые с Фазы 3, дробный реген живёт в carry-
// аккумуляторах, не в баре); MacroNpcRecord.health едет тем же POD-ом.
// v81 (2026-09-06): Фаза 6 — MacroNpcRuntime получил scoutRank (кэш ранга
// Разведки лидера рядом с travelRank/marathonRank, та же дверь рефреша);
// рантайм едет в MacroNpcRecord целиком.
// v82 (2026-09-07): аффикс-трек — ItemRef несёт 8 аффиксов (было 4) двумя
// плоскими массивами rows[8]+values[8] (SoA, паддинг пар умер); сейв пишет
// оба массива вместо одного массива пар с дырками.
// v84 (2026-09-09): ОДИН ЗАКОН ВОССТАНОВЛЕНИЯ (CANON S14; владелец:
// «никакого особенного игрока и ущербных НПЦ»). `ecs::Pools` несёт дробный
// остаток отдыха рядом со своей полосой, и она едет в снимке макро-ECS
// сырыми байтами (save.cpp w.pod) — рост POD'а есть смена формата.
// v85 (2026-09-10): посадка 4 — PlayerState::combatStats УМЕР: полосы игрока
// живут в ecs::Pools его сквада и едут ТОЛЬКО снимком макро-ECS (36 байт
// p.combatStats выпали из write_player; второй комплект полос игрока на
// диске был дефектом). Дробные остатки HP/MP игрока впервые переживают
// загрузку — они в Pools, а App-аккумулятор умер.
// v86 (2026-09-10): МАСШТАБЫ — клетка сквада стала ОДНИМ ЧИСЛОМ
// (`ecs::MacroCell{u32}` вместо `ecs::Position{float×3}` у макро-сущностей;
// вердикт владельца: «мир — плоский массив, связный тор, у каждой клетки
// ровно одно число»). `MacroNpcRecord.pos` (12 байт float) → `cell`
// (4 байта u32) — смена формата снимка макро-ECS.
// v87 (2026-09-10): ЧЕСТНЫЙ ФЛАГ — `PlayerTag` едет байтом записи снапшота
// (`MacroNpcRecord.playerFlag`), вердикт владельца: «сейв честно хранит
// снимок всего мира… и потом честно просто смотрится у кого флажок игрок».
// `PlayerState::possessedMacroSpawnId` умер (второй склад «кем управляю»
// вне снапшота); генезис на пути загрузки больше не создаёт макро-тел —
// до этого каждая загрузка подсовывала дверям СВЕЖИЙ сквад игрока, а
// восстановленный ходил призраком (SAVE-5).
// v88 (2026-09-10): ИГРОК-КАК-СКВАД (подпосадка 4) — `PlayerState.x/y` и
// entry-байты умерли; позиция игрока = `MacroCell` его сквада в записи
// снапшота, entry-контекст = байты его `MacroNpcRuntime` там же. Блок
// игрока в сейве теряет 2 float + 2 байта; читателей у них больше нет.
// v89 (2026-09-10): КНИГА НА ТЕЛО (§41 корень 3) — `SpellBook` стал
// компонентом каждого макро-тела и битсетами конверта 256 (вердикт
// владельца); запись снапшота несёт книгу, блок игрока её теряет.
// v90 (2026-09-10): ЛИСТ НА ИМЕНОВАННОЕ ТЕЛО (§41 корень 1, посадка А;
// ММОРПГ-вердикт владельца: «у каждого персистентного персонажа свой лист
// и идёт в сейв») — CharacterSheet стал КОМПОНЕНТОМ именованных родов
// (npc.h kNamedKinds), рождается броском сида один раз и дальше владеем;
// запись снапшота несёт его опт-ин блоком (hasSheet). Транзиенты (ротация,
// караваны) деривируют генерик на лету и не хранят ничего.
// v91 (2026-09-10): ЛИСТ ИГРОКА = ТОТ ЖЕ КОМПОНЕНТ (посадка Б) —
// PlayerState::sheet мёртв, его лист едет в записи его сквада опт-ин
// блоком hasSheet как у любого именованного; блок игрока теряет три
// pod-поля листа (attributes/levelData/skills).
// v92 (2026-09-10): СТОЛ АНКЕТ — запись сквада несёт designOrdinal (int16,
// −1 = обычный сквад): строка стола дизайн-персонажей
// (macro/characters.h), восстанавливается тегом DesignCharacterTag.
// Смерть анкеты навсегда: генезис на загрузке не гоняется, погибшая
// просто отсутствует в снапшоте.
// v93 (2026-09-10): ПОЛЁТ + ЛОГОВО — MacroNpcRuntime вырос тремя полями
// (flying-кэш колонки cruiseM, lairX/lairY дом-клетка модели LairSorties);
// runtime едет в записи POD-ом, его раскладка = формат.
// v95 (2026-09-17): СЕЗОННОЕ ОКНО БАЛАНСОВ (CANON S19.2) — Landmark carries
// seasonWellbeing (the boundary window's verdict the daily population law
// lives off until the next boundary).
// v96 (2026-09-18, сессия В — снятие спецпутей): SettlementHistory вырезана
// из Landmark (вердикт №4 — одна память места, летопись S20.1); монета —
// просто товар (12 фракционных монет данными, ворота и список валют мертвы);
// слой разработки + счётчики кораблей полем (shipsAtCell/resourceScars
// умирают); металлы медь/золото. Старые сейвы ничего не стоят (закон P1).
// v97 (2026-09-19): РОСТЕР — ИНВЕНТАРЬ СУЩЕСТВ (CANON S4) — на диске
// СЛОТ-строки {kind, level, count, entityId}, не по-душам: гарнизон в 752
// души — два слота, не 752 записи, и int32-стак не разворачивается в стену.
// v98 (2026-09-19): ЛОШАДЬ — ЮНИТ (CANON S10) — ResourceFieldId вырос
// строкой Horses ПЕРЕД блоком жил (скар-блоки сейва сдвинулись), фича
// FT_Pasture = байт 14, таблица NPC выросла строкой Horse. Старые сейвы
// ничего не стоят (закон P1).
// v99 (2026-09-19): ПОТРЕБЛЕНИЕ — ДОЛГ (CANON S10) — Landmark несёт
// needDebt[kCommodityCount]: сезонный счёт лестницы нужд, гасится приходом.
// v100 (2026-09-19): ПЕРКИ — ТРЕТЬЯ ВАЛЮТА (CANON S14 5-5-5/1-1-1, заглушка
// по вердикту владельца: графа осознанно нет) — LevelData вырос perkPoints,
// CharacterSheet несёт 256-битную маску PerkMask (32 байта, S26); граф
// сядет в этот конверт без движения сейва.
// v101 (2026-09-19): ОДНА МЕРА ЖИЗНИ (CANON S10/S16, вердикт владельца
// «теперь только есть благополучие и оно даёт рост») — настроение, реестр
// его полос, восстания и флаг голода ВЫРЕЗАНЫ; у места остались
// seasonWellbeing и needDebt.
constexpr int kSaveVersion = 110;   // v110: слияние M-71 — существа в контейнере

// (SettlementHistory — the per-settlement population ring — died 2026-09-18,
// owner verdict №4 of the second canon audit: «сноси, есть уже единая система
// фактов и событий». One memory of a place exists — the chronicle, CANON
// S20.1; a second per-landmark diary was a parallel memory system.)

// ── THE landmark record (CANON S9, owner verdict 2026-08-29) ─────────────
// One struct, one vector, a KIND COLUMN. City, village and spire used to be
// three structs in three GameState vectors — which made "what stands on a
// cell" a switch, made a S9 transition (village→city, spire→ruin) a record
// MOVE between types, and made every new landmark kind a new vector plus
// save code. Now the kind is data: a transition flips `type` (plus a grid
// rebake), and a new kind is its registry row plus the columns it reads.
// Fields a kind does not use sit at their zero defaults — the zero
// contribution, CANON S6 — and cost nothing but bytes (S26: size is not an
// argument).
// ── КАРТА ОКРУГИ (владелец, 2026-09-18) ──────────────────────────────────
// Дословно: «можно каждый сезон вокруг каждого ландмарка сканировать все фичи
// и ландмарки — типа как карта округи, и тогда сквадам вообще не надо искать,
// а просто решать, куда идти».
//
// ЭТО УБИРАЕТ ПОИСК ИЗ ТИКА. Артель искала жилу сама, каждым аукционом, и
// граница поиска была назначенным числом клеток — из-за чего металла в мире не
// добывалось ВООБЩЕ (дубль-прогон: 0 железа/глины/серебра за 256 дней), а когда
// границу вывели из цены рейса, поиск стал стоить миллион чтений на артель.
// Теперь ищет МЕСТО, раз в сезон, и держит ответ строкой; артель читает.
//
// ГРАНИЦА — СВОЯ НАВ-ОКРУГА (S7, выбор владельца): выдуманного радиуса не
// существует вовсе, а закон «жила в чужой округе — сосед возьмёт» уже живёт в
// коде. Несколько деревень одной округи делят одну землю — они и конкурируют
// за неё честно.
//
// УНИВЕРСАЛЬНО ДЛЯ ВСЕХ ЛАНДМАРКОВ (владелец, 2026-09-18: «это не только для
// деревень, а универсально для всех ландмарков — такая система, да?»): карта
// живёт КОЛОНКОЙ единого реестра мест, и опись обходит его целиком — город,
// деревня, шпиль, руина, логово. Что с картой делать, решает строка вида (у
// деревни артели, у города свои дела, у логова — своя охота): никакой ветки
// «если деревня» здесь нет и быть не может.
//
// ПРОИЗВОДНОЕ, В СЕЙВ НЕ ЕДЕТ: это ответ, выводимый из полей + навигации, и он
// пересобирается на границе сезона и после загрузки (как и любой запечённый
// слой, S21).
struct SurveyRow {
    // Ближайшая клетка рода в своей округе; -1 = в округе такого рода нет.
    std::int16_t x = -1, y = -1;
    // Путевое расстояние по запечённому полю округи (distHome) — то самое
    // число, которым артель и ходит; 0xFFFF = недостижимо.
    std::uint16_t dist = 0xFFFFu;
    bool none() const { return x < 0 || y < 0; }
};
struct LandmarkSurvey {
    // По строке на каждый род реестра полей — рост родов не трогает код.
    SurveyRow rows[std::size_t(ResourceFieldId::Count)];
    // День последней описи: сезонная граница ставит его, читатель видит,
    // насколько карта свежа (0 = никогда не описывали).
    std::int32_t day = 0;
};

// ВЕДОМОСТЬ МЕСТА (CANON S10 «ЗНАНИЕ О ЦЕНЕ — ТРИ ЯРУСА», владелец
// 2026-09-20: «поле строим, если оно универсально, но лучше перепекать на
// фазе конца сезона — мы там всё перепекаем, а не каждый день»).
//
// ЧТО ПОЧЁМ у этого места — по строке на каждый товар, в единицах
// стоимости. Публикуется НА ГРАНИЦЕ СЕЗОНА тем же тактом, что опись округи,
// и рядом с ней: опись говорит месту, ЧТО у него рядом, ведомость — ЧТО
// ПОЧЁМ. Одна форма, один момент, ни накопления, ни вытеснения, ни крутилки.
//
// ПРОИЗВОДНОЕ, В СЕЙВ НЕ ЕДЕТ — как и опись: цена выводится из склада,
// счёта и анкеты места, и пересобирается на каждой границе.
//
// ЗАЧЕМ ОНА ПОНАДОБИЛАСЬ (дефект, найденный 2026-09-20): покупательская
// половина торговли читала запас ДОМА через 4-битный класс памяти крю с
// потолком «много = 4096» (дверь ВЫРЕЗАНА 2026-09-21 — вызовов не было). У города с
// 45 млн хлеба класс давал 4096 при сезонной нужде порядка 80 000 — крю
// заключало, что дома острый дефицит, и скупало хлеб везде, чтобы привезти
// ДОМОЙ. Торговля мира качала хлеб ВВЕРХ, в города, которые в нём тонут.
// Ведомость этого читателя ЗАМЕНЯЕТ, а не дополняет: память сквада
// (CANON S10, ярус 3) остаётся фановой системой и веса не несёт нигде.
struct LandmarkLedger {
    // Цена единицы у этого места на день публикации. 0 = не публиковалось
    // (мир до первой границы сезона) — читатель обязан это различать.
    std::int32_t price[std::size_t(kCommodityCount)]{};
    // Сезонная нужда места по этой строке — та же величина, которой
    // посчитана цена рядом. Публикуется вместе с ценой, потому что
    // «сколько дому вообще нужно» — второй вопрос всякого, кто решает,
    // что везти: цена говорит ПОЧЁМ, нужда — СКОЛЬКО влезет.
    std::int32_t demand[std::size_t(kCommodityCount)]{};
    std::int32_t day = 0;   // день публикации; 0 = ведомости ещё нет
    bool published() const { return day > 0; }
};

// МИРОВОЕ СРЕДНЕЕ — ЦЕНА МЕСТА ЗА ГОРИЗОНТОМ (CANON S10, ярус 2, владелец
// 2026-09-20: «За горизонтом место оценивается по МИРОВОМУ СРЕДНЕМУ из той
// же ведомости: число не назначено, а посчитано из самой таблицы»).
//
// ОДНА строка на мир, считается ТЕМ ЖЕ проходом, что публикует ведомости
// мест, из ИХ ЖЕ цен — ни одного назначенного числа (S26). Следствие,
// названное каноном: дальнее место выглядит «обычным рынком» — туда ездят,
// но без предпочтения; целенаправленный дальний рейс принадлежит ярусу 3.
//
// ПОЧЕМУ ЭТО НЕ КОЛОНКА В КАЖДОЙ ВЕДОМОСТИ: число одно на мир, и копия его
// в 1 880 местах была бы вторым ответом на тот же вопрос (S26).
// ПРОИЗВОДНОЕ, В СЕЙВ НЕ ЕДЕТ — как и сами ведомости.
struct WorldLedger {
    std::int32_t price[std::size_t(kCommodityCount)]{};
    std::int32_t day = 0;   // день публикации; 0 = среднего ещё нет
    bool published() const { return day > 0; }
};

struct Landmark {
    int id = -1;             // world-unique ordinal (nextLandmarkOrdinal, v54)
    LandmarkType type = LandmarkType::None;  // THE kind column (registry row)
    std::string name;        // "" where the kind carries none (spires derive)
    int x = 0, y = 0;
    int population = 0;
    // THE store (owner's ruling, W2): the landmark's universal Inventory is
    // its market, its granary and its warehouse in one — agents deliver into
    // it, the day-loop eats from it, the trade panel sells out of it.
    Inventory inventory;
    // ОПИСЬ СВОЕЙ ОКРУГИ — производная, в сейв не едет (см. LandmarkSurvey).
    LandmarkSurvey survey;
    LandmarkLedger ledger;       // ЧТО ПОЧЁМ здесь — тот же сезонный такт
    // РОСТЕР МЕСТА — ТОТ ЖЕ ТИП, ЧТО У СКВАДА (macro/roster.h, CANON S4:
    // «гарнизон = ростер ландмарка»). Инвентарь существ плюс его счёт
    // содержания одной записью; пуст, если строка реестра гарнизона не
    // держит. Слово «гарнизон» осталось ИМЕНЕМ РОЛИ, а не вторым видом
    // контейнера: судит его та же дверь, что артель и армию игрока
    // (macro/roster_window.h).
    Roster garrison;
    // WHOSE place this is — a faction registry index (owner 2026-09-11:
    // «королевств нет, только фракции — одна система»). -1 = nobody's,
    // which resolves to the free folk through faction_or_freefolk. It
    // replaced kingdomIdx, which named a row of a Kingdom vector that was
    // itself just a materialized copy of the registry.
    std::int16_t factionIdx = -1;
    // ── ИНТЕРЕСЫ МЕСТА — ВСЕ ЕГО СВЯЗИ В ОДНОЙ ТАБЛИЦЕ (владелец, 2026-09-21).
    // Феодальное ребро здесь — ЧАСТНЫЙ СЛУЧАЙ отношения, а не своя система
    // (вердикт: «тогда отдельная феодальная система не нужна, она будет
    // частным случаем отношений»). Форма и отвергнутые колонки — в
    // macro/interests.h.
    //
    // ЧТО ЭТО ПОЛЕ ЗАМЕНИЛО, ПОИМЁННО:
    //   `suzerainLandmarkId` — «кому я плачу», одна колонка на весь феодальный
    //      граф. Теперь это запись Stance::Suzerain;
    //   `vassalHead` / `vassalNext` — односвязный производный индекс «кто мои
    //      вассалы», построенный 2026-09-21 и НЕ ПОЛУЧИВШИЙ НИ ОДНОГО
    //      ЧИТАТЕЛЯ (его строитель ensure_vassal_edges не звался ниоткуда —
    //      problems §55). Теперь это записи Stance::Vassal, которые ставит
    //      та же дверь, что и обратную им: половин у S24 больше нет.
    Interests interests{};
    // The honest economy's daily readouts (v29): yesterday's hunger and
    // comfort shortfall (for the eye and the mood), the famine edge flag,
    // and the fractional carry of the LOGISTIC population law.
    std::uint16_t starvedYesterday = 0;
    // THE SEASON WINDOW'S VERDICT (v95, CANON S19.2): wellbeing quantized to
    // a byte, written on the boundary day by econ_debt_boundary's outcome
    // and read by the mood band + population law every day until the next
    // boundary. Born 255: a landmark seeded mid-life starts its life fed.
    std::uint8_t  seasonWellbeing = 255;
    float         popGrowthCarry = 0.0f;
    // WHAT THE WORLD THINKS OF THIS PLACE (macro/chronicle.h). Renown is not
    // a squad's private counter — it belongs to every MACRO entity that has an
    // identity (owner, 2026-08-27): a band, a city, a people. A famous city is
    // a harder prize and a louder loss, and beating it is worth more precisely
    // because it was famous. The microworld has none of this.
    std::uint32_t renown = 0;
    // Spire columns: kSpellDefs row ordinal (macro/spells.h, append-only) —
    // the spire's whole difficulty context (placement gate, tower storeys,
    // guard site) is the spell's tier, derived at the moment of reading —
    // and whether its orb has been drained.
    std::uint32_t spellId = 0;
    bool depleted = false;
    // ── The UNIVERSAL tribute, BY POSITION (owner 2026-09-02; v73) ───────
    // «Дают по 1/8 всего со склада, с округлением до меньшего»: on the
    // place's own seasonal pay-day an eighth of EACH commodity stack (floor)
    // and an eighth of the coin are ASSESSED into these per-position debts;
    // carriers (the vendor, the tax courier) deliver them IN KIND. The old
    // value-debt paid «coin, then the fattest stacks» — and the fattest was
    // grain, so the suzerain never saw a grain-rich vassal's silver and the
    // mint starved (measured, seed 7: 4308 silver parked in a village for
    // 100 days). A slice of every stack is a slice of everything the vassal
    // is rich in — the mint metal included. Missed seasons accumulate
    // honestly, per position.
    // ── ДОЛГ ДАНИ — ОДНА СТОИМОСТЬ (владелец 2026-09-22) ──────────────
    // Здесь лежали ДВА ответа на «сколько должен»: `titheOwedGoods[15]` по
    // строкам плюс `titheOwedCoin` отдельно. Оба умерли вместе с вердиктом
    // «всё в инвентаре — товар»: долг есть СТОИМОСТЬ, и платится он по
    // ПЛОТНОСТИ (currency.h transfer_value_dense — первым уходит самое
    // ценное). Это строже прежней защиты «доля каждого стака»: там вассал
    // отдавал по щепотке отовсюду, здесь — самое дорогое, что у него есть,
    // и «заплачу зерном, серебро оставлю» невозможно ни с какой стороны.
    // Минус 60 Б у места и минус одна из четырёх «вторых колонок» §56.
    std::int64_t titheOwedValue = 0;
    std::int32_t titheSeasonAssessed = -1;   // last season charged (-1 never)
    // v74: the assessment BASE is the season's AVERAGE store, not the
    // pay-day snapshot (owner 2026-09-02: «лучше среднего склада за месяц,
    // а то пустой склад случайно — и ничего не платит, или наоборот»).
    // Память с горизонтом СЕЗОНА, одна дверь на весь мир (macro/memory.h,
    // CANON S19.2) — день уплаты перестал быть лотереей «уехал ли вендор
    // этим утром».
    //
    // v104: ПРЕДМАСШТАБИРОВАНА. Здесь лежал std::int32_t со значением КАК
    // ЕСТЬ, и разностный шаг `(склад − avg) >> 5` обнулялся на всякой
    // разнице меньше 32: склад, ни разу не превысивший 31, держал среднее
    // РОВНО НОЛЬ вечно — то есть вся лестница комфорта не облагалась данью
    // никогда, а «1/8 со всего» было неправдой тем сильнее, чем место
    // мельче (0 % у мелкого, 12.1 % у крупного против обещанных 12.5 %).
    // Теперь поле держит значение × горизонт и читается memory_value();
    // ширина 64 бита не запас, а расчёт (см. ЗАКОН ТИПА в memory.h).
    // v108: ОДНА ПАМЯТЬ ВМЕСТО ШЕСТНАДЦАТИ. База начисления — среднее за
    // сезон СТОИМОСТИ СКЛАДА целиком (inventory_value), а не пятнадцать
    // средних по строкам плюс шестнадцатое по монете: долг стоимостный,
    // значит и база его стоимостная. Минус 120 Б у места.
    WorldMemory titheAvgValue = 0;
    // ── ПОТРЕБЛЕНИЕ — ДОЛГ (CANON S10, вердикт 2026-09-19; v99) ─────────
    // На границе сезона место получает СЧЁТ = сезонная нужда по каждой
    // строке лестницы (индекс — товарный ординал, зеркало titheOwedGoods;
    // не-лестничные ординалы всегда нули). Приход гасит долг СРАЗУ и
    // съедается — на складе лежит только ИЗЛИШЕК, всё видимое свободно.
    // Непогашенный хлеб на следующей границе уходит населением НАСМЕРТЬ
    // (доля = остаток / душевой сезон), прочие строки гасят рост.
    std::int32_t needDebt[kCommodityCount] = {};
};
// ── РАЗМЕР МЕСТА ЗАКРЕПЛЁН (AGENTS п.10; числа пересняты с.18) ────────────
// 42 400 Б × 32 768 мест (кап kWorldLandmarks, core/stacks.h) = 1.29 ГиБ по
// капу; в замеренном мире (~1 880 мест) — 76 МиБ. (Прежняя редакция этого
// комментария держала 13 912 Б / 435 МиБ / 25 МиБ — числа ТРЁХ ведомостей
// назад; ассерт ниже был прав, проза врала.) Из них 41 032 Б (96.8 %) —
// ОБЩЕЕ ЯДРО СУБЪЕКТА: inventory (40 960) + garrison-счёт (72). Ровно то же
// ядро несёт макро-сквад — это и есть «ландмарк есть неподвижный сквад»
// (CANON S4), уже выполненное в памяти.
//
// РАСХОДЯТСЯ ОНИ НА 1 368 Б. Три четверти — РЕЕСТР ИНТЕРЕСОВ (1 024 Б,
// 128 связей): он пришёл 2026-09-21 на место феодального ребра, и сквад
// получит ТОТ ЖЕ реестр, когда у отношений сквадов появится первый читатель
// (условие в interests.h: только после слияния пространств ординалов).
//
// ОСТАЛЬНОЕ РАСХОЖДЕНИЕ — СПИСОК НЕДОДЕЛОК, А НЕ ЗАМЫСЕЛ (problems §56):
//   name 24 Б      — std::string на структуре ×32768: AGENTS п.1 и п.3 прямым
//                    текстом; у сквада имя — ординал (NpcCharacter::nameIdx);
//   titheAvgValue 8 — ВТОРАЯ ПАМЯТЬ: у сквада память это AgentMemory
//                    (8 слотов). Было 128 Б — шестнадцать памятей по строкам;
//                    сжато до одной 2026-09-22 вместе со стоимостным долгом;
//   needDebt 60    — ВТОРОЙ ДОЛГ: рядом garrison.needDebt, оба в сейве;
//   population 4   — станет производным от ростера (переворот населения).
// Прочее честно своё: опись округи, прейскурант, дань, адрес, анкета.
// (Феод из этого списка ВЫШЕЛ 2026-09-21: три колонки — 12 Б — заменены
// записями реестра, и половина S24 закрыта.)
// 2026-09-22: место похудело на 184 Б (528 → 344) — стоимостный долг дани
// вместо пятнадцати колонок плюс монеты и ОДНА память вместо шестнадцати.
// Свидетель поймал обе правки компилятором, как и обещает AGENTS п.10:
// число живёт в коде, а не в прозе. (Моя прикидка «−180» была на 4 Б
// неверна — выравнивание; ЗАМЕР поправил, и это ровно то, зачем он тут.)
// 2026-09-24: слот вырос 36 → 40 Б (слот В эпика: level + entityId), ядро
// субъекта 12360 → 13384; замер поймал компилятором, как положено.
// 2026-09-24, шаг А слияния: ёмкость контейнера 256 → 1024 (32×32), ядро
// субъекта 13384 → 44104.
// 2026-09-24, шаг Б слияния: существа уехали В КОНТЕЙНЕР, ростер стал
// обвязкой счетов (72 Б) — ядро субъекта 44104 → 41032.
static_assert(sizeof(Landmark) == 42400,
              "место = ядро субъекта (41032) + реестр (1024) + 344 Б своего");
static_assert(sizeof(Landmark) == sizeof(Inventory) + sizeof(Roster)
                                      + sizeof(Interests) + 344,
              "ядро субъекта у места и у сквада ОДНО (CANON S4)");

enum class GameSubStateKind : std::uint8_t {
    Exploring, Paused, Trading, ViewingMap,
    // Forced pre-battle encounter (Session 15): a hostile squad on the map
    // stopped the player — the M&B screen is up and the world is paused.
    // Appended LAST and the highest live kind, which read_sub_state uses as
    // its refusal bound. The target
    // squad is runtime App state (an entt handle is not save material); a
    // loaded save that says PreBattle with no live target resets to
    // Exploring on the first frame — fail closed, no version bump.
    PreBattle,
};
struct GameSubState {
    GameSubStateKind kind = GameSubStateKind::Exploring;
    int settlementId = -1;
};
// (Four columns are gone with the random-encounter table: the `Event` sub-state
// kind, `eventId`, `enemyId` and `pendingEncounterIdx`. NOTHING in the project
// ever set that kind or wrote that index — the only writers were the modal's
// own resets and the save — so the modal could not open on any state the game
// could reach, and the three strings had no reader at all. The trigger behind
// them was removed by owner ruling 2026-08-05 (an unconditional random roll
// over a list is not a system); the table and modal were parked for a future
// context-driven trigger and parked furniture is not how this project waits —
// the наряд lives in the registry, not in dead columns (DOD п.9).)

// (struct Faction is gone. Its four identity columns — id, name, description,
// colour — were verbatim copies of the registry row that already declares them
// (macro/faction.h kFactionDefs), duplicated into every save; its `relations`
// map became the flat matrix in macro/relations.h. What a faction IS lives in
// the registry; how factions REGARD each other lives in the matrix; there is
// nothing a third structure could hold.)

// ── THE SESSION FEED: words that die with the moment ─────────────────────
// (owner, 2026-08-28: «это вообще не нужно хранить даже в сессии — пишется
// в UI как в Might & Magic и сразу забывается»). The old EventLogRing kept
// 8192 std::strings in the SAVE; but a session message ("Game saved.",
// "You have learned Fireball!") is not a fact of the world and not the
// player's journal — it is presentation. So the channel is a tiny POD ring
// the HUD fades out, NEVER serialized. What the world remembers is the
// chronicle; what the player learned is his journal; this is neither.
struct SessionFeed {
    static constexpr int kLines = 8;     // more than fits on screen anyway
    static constexpr int kTextCap = 112; // one HUD line, NUL included
    struct Line {
        char  text[kTextCap];
        float ttl = 0.0f;                // seconds of screen life left
    };
    Line lines[kLines]{};
    std::uint8_t head = 0;               // where the NEXT line goes
};

// A settled quest OFFER's provenance — the POD triple that names an offer
// uniquely (events/quests/quest_types.h Quest: each generator fires at most
// once per settlement per day). Defined here, not there, because the layer
// order runs macro → events: player state stores it, the quest engine reads
// it through this door.
struct SettledQuestOffer {
    std::int32_t giverSettlementId = -1;
    std::int32_t bornDay = -1;
    std::uint8_t offerSlot = 0;
};

struct PlayerState {
    std::string name;
    // The creation screen's "nature" pick (v78): 0 = male, 1 = female. A
    // byte, not an enum class — it indexes the authored choice table
    // (content/plot/intro.h creation_sex_choices) whose rows own the words.
    std::uint8_t sexIdx = 0;
    int ageDays = 1000;
    // (No `x`/`y` since v88. WHERE he stands is the ordinary ecs::MacroCell
    // on his squad entity — подпосадка 4: the input walker steps it, the one
    // glide integrator moves the eye, the snapshot restores it. The scalars
    // were four literal duplicates of ECS fields, re-projected every tick —
    // the anaesthesia-bridge of problems.md §41 root 4.)
    // (No gold FIELD: money is faction coin in `inventory` — macro/currency.h
    // wallet math. The player is a squad like any other, v32.)
    // (No `sheet` field since v91 — посадка Б. WHO he built is the ordinary
    // owned CharacterSheet component on his squad entity, the same block
    // every named character carries (squad.h owned_sheet, посадка А), read
    // through macro/player_entity.h player_sheet() and the universal
    // effective door (squad.h effective_sheet_of). It rides the save inside
    // his MacroNpcRecord like every named lord's (hasSheet, v90) — the
    // player block stopped writing a second copy. It was the LAST field
    // that made him a different kind of body from the squads around him.)
    // (No `combatStats` field. The player's three bars are the ordinary
    // ecs::Pools on his squad entity — macro/player_entity.h player_pools()
    // — refreshed from this sheet through the one door every leader's are
    // (squad.h refresh_body_from_sheet). It sat here as a nine-field private
    // store with three cached rest rates until 2026-09-10, and it was the
    // last field that made him a different kind of body from the squads
    // around him: landing 4 of the «полосы на тело» track.)
    // (No `inventory` field. The player's bag is the ordinary
    // ecs::NpcInventory on his squad entity — macro/player_entity.h
    // player_inventory(). It was the last large field that made him a
    // different kind of thing from the squads around him.)
    // NOTE. There is no `reputation` map here any more. The player's standing
    // with every faction IS his row in the one relation matrix
    // (gs.factions["player"].relations) — see player_reputation /
    // add_player_reputation below. Two stores for one number meant the battle
    // pass and the macro matrix could disagree about the same pair.
    // (No `army` field. The player's squad is an ORDINARY squad — his men
    // live in the creature area of his ONE container (M-71), reached through
    // macro/player_entity.h player_inventory(). It sat here as its own
    // roster until 2026-08-27, and every consumer of it was a
    // player-specific path CANON S4 forbids by name.)
    // Codex unlock state: one bit per article ordinal (macro/codex.h
    // CodexArticleId; the static_assert there is the loud cap). Replaced a
    // vector of id STRINGS (v63) — a string was doing an ordinal's job.
    std::uint64_t codexUnlockedBits = 0;
    // The player's log — a RING, not a vector that shifts. It was capped by
    // `erase(begin())`, which memmoves up to eight thousand std::strings on
    // every entry past the cap: the same defect the settlement history and the
    // event bus both had, and the same fix. The cap lives in the container, so
    // no caller can forget it and nothing shifts to enforce it.
    //
    // (No event log. Session messages die with the moment — GameState's
    // sessionFeed below; what the player LEARNED is the journal right here;
    // what the world remembers is the chronicle. Three questions, three
    // answers, no fourth store.)
    // Loud cap of the journal below — kChronicleAnnals' own size (owner,
    // 2026-08-28: «на века, не жалко»): the player's whole-game log gets no
    // less room than the world's eternal memory. 2^20 copies × 32 B = 32 MB
    // at the END of a long life (the vector grows as it fills — size is no
    // argument either way, CANON S26); at even 100 learned facts a day that
    // is 80+ game years. Hitting it flips journalFull, never a silent drop.
    static constexpr std::uint32_t kJournalFactsCap = 1u << 20;
    // The player's JOURNAL: his KNOWLEDGE of the world's facts (?27 half-
    // ruling, owner 2026-08-28). The chronicle is the world's one memory; the
    // journal is what of it the player LEARNED — by taking part, by standing
    // on the cell where it happened, and (later, through the same door) by
    // buying rumours. Two owner laws shape the container:
    //   · the journal NEVER forgets — it is the log of his whole game — so it
    //     is append-only with a LOUD cap, not a ring;
    //   · the world's ring DOES forget, so the journal holds COPIES of the
    //     32-byte records, not seq references that would dangle. A fact is
    //     immutable from the moment it is filed, so a copy of it is not a
    //     second truth — there is nothing for the two to disagree about.
    // Words are still derived at display time (fact_sentence): the journal
    // stays a VIEW on the chronicle; what it stores is WHOSE the knowledge is.
    std::vector<WorldFact>   journal;
    std::uint32_t            journalSeenSeq = 0;  // last chronicle seq scanned
    std::uint8_t             journalFull = 0;     // the loud cap flag
    // (No spellBook since v89. A body's knowledge is the SpellBook COMPONENT
    // on its macro entity — §41 root 3: the one-copy field here was literally
    // the class combatStats was before landing 4, and it is why only the
    // player could cast, drain or be taught. His book rides his squad's
    // MacroNpcRecord like every lord's.)
    // Truce clocks, one per faction SLOT (macro/relations.h): the day a
    // cease-fire with that faction runs out. It was the last string-keyed
    // faction map in the game — and it has no gameplay reader yet, so the
    // concept is kept (S24 politics will want truces) in the shape everything
    // else about factions now has: a flat array indexed by ordinal.
    std::array<std::int32_t, kMaxWorldFactions> factionPeaceUntilDay{};
    // Quest OFFERS the player has settled (completed or failed) — the POD
    // provenance triples the quest engine's is_known compares against, so a
    // settlement does not re-offer what was already done TODAY. An offer's
    // identity includes its bornDay (quest_types.h), so an entry whose day
    // has passed can never be generated again — the engine prunes stale
    // entries each tick, and the list stays a handful of records.
    //
    // v63: this replaced completedQuestIds/failedQuestIds — two ETERNAL
    // string vectors whose only living semantic was exactly this same-day
    // dedup (the day was baked into the id string, so an old entry never
    // matched anything again — dead weight growing in the save forever).
    std::vector<SettledQuestOffer> settledQuestOffers;
    // Lifetime tallies (the honest split: the old string lists filed a
    // failure into BOTH, a TS relic). Display/stats only — dedup is the
    // provenance list above; history will be the chronicle's job.
    std::uint32_t completedQuestCount = 0;
    std::uint32_t failedQuestCount = 0;
    // (No possessedMacroSpawnId since v87. "Whom do I control" has ONE store —
    // ecs::PlayerTag on the macro entity itself, riding the snapshot as an
    // honest byte (MacroNpcRecord.playerFlag). The field was the flag's
    // out-of-snapshot double, and the re-derivation it fed masked the load
    // raising a second player squad — SAVE-5.)
    // (No entry-side context since v88: the packed entry step and the
    // time-in-cell count are his squad's own MacroNpcRuntime bytes — the
    // same two every marcher stamps — and the accumulator toward the next
    // tick is that runtime's tickAccum, free on an input-driven squad the
    // AI sweep never thinks for.)
    // (No `memory` field. The player's head is the ordinary AgentMemory on
    // his squad entity — the same component every squad leader carries, saved
    // by the same macro record. It sat here as a second store with ZERO
    // readers in src/: everything that remembers anything about the player
    // was already going through the entity.)
};

// The ONE door into the session feed (drawn and faded by the HUD, never
// saved). Overlong lines are cut at the HUD's own width — a feed line is a
// glance, not a document.
inline void session_feed_push(SessionFeed& f, const char* text) {
    if (!text || text[0] == '\0') return;
    SessionFeed::Line& l = f.lines[f.head % SessionFeed::kLines];
    std::snprintf(l.text, sizeof l.text, "%s", text);
    l.ttl = 6.0f;   // seconds on screen — M&M's own unhurried fade
    f.head = std::uint8_t((f.head + 1) % SessionFeed::kLines);
}

// World-tick runtime (moved here from world_tick.h in v24, because it is
// STATE): the budgeted daily-simulation queue, the subworld step remainder
// and the jitter stream the daily economy rolls. All integers on purpose —
// the old float scale could not survive a save or a pause without losing a
// sliver of a day.
struct WorldTickRuntime {
    int pendingDailyTicks = 0;
    int nextDailyTickDay = 0;
    // Subworld only: the clock there advances one tick per
    // kSubworldTickDivisor simulation steps; this counts the steps not yet
    // spent.
    std::uint64_t subworldStepRemainder = 0;
    Rng jitter{0xC0FFEEu};
};

// The persistent half of the macro-AI runtime (npc_ai.h MacroNpcAiRuntime):
// the jitter stream and the sweep rhythm. The transient half (the squad
// index) is rebuilt every drive and stays out of the save. Synced with the
// live runtime at exactly two doors — staging before a save, applying after
// a load (src/app/main.cpp) — fixed-width fields because this is a save
// block.
struct MacroAiRhythm {
    Rng           jitter{0xA1F0u};
    std::uint32_t sweepAccum = 0;
    std::int32_t  pendingSweeps = 0;
    std::uint64_t sweepCursor = 0;
};

// One work of a squad's hands on the world's feature grid (v71; CANON S10
// «фичи создаются сквадами»): the cell and what now stands there. POD,
// append-only — the save row (world_fields.h Built) writes it verbatim and
// the load re-stamps it onto the seed-baked grid.
struct BuiltFeature {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t ft = 0;   // FeatureType byte (macro/features.h)
};

struct GameState {
    int version = kSaveVersion;
    std::string saveName;
    std::string savedAt;
    std::uint32_t worldSeed = 0;
    int mapW = 1024, mapH = 1024;
    LayerParameters mapParams{};
    int cityCountTarget = 0;

    // THE landmark roster (CANON S9, 2026-08-29): every placed landmark of
    // every kind, one vector, kind = the record's `type` column. Ownership
    // priority for a contested cell is for_each_landmark's yield order
    // (landmark_iter.h), not storage order.
    std::vector<Landmark>   landmarks;
    // ЦЕНА ЗА ГОРИЗОНТОМ (CANON S10, ярус 2). Пересобирается тем же тактом,
    // что ведомости мест, из их же цен; в сейв не едет.
    WorldLedger             worldLedger;
    std::vector<Marker>     markers;
    // The player's map knowledge (v40): Unknown / Explored / Visible per cell.
    // Explored persists; Visible is re-derived from the player's position
    // (update_player_sight) — save.cpp clamps it away on write.
    KnowledgeLayer knowledge;
    // WHAT HAPPENED (macro/chronicle.h, CANON S20.1). The world's own memory,
    // in two tiers: a ring the world is ASKED (indexed by cell — this is what
    // lets a witcher find a monster by the traces it left) and annals the
    // world REMEMBERS. Both ride the save whole: the annals are not a cache,
    // they are part of the world, and a legends mode will read exactly them
    // (owner, 2026-08-27).
    Chronicle chronicle;
    // ПОЛЯ СЛЕДОВ ФРАКЦИЙ (macro/scent_field.h, CANON S10 «хищник-жертва»,
    // v75): два клеточных канала на фракцию — сила (squad_power) и цена
    // (души + груз). Едут в сейве целиком (вердикт владельца: движение не
    // фактируется, реплеить след не из чего).
    ScentField scent;
    // The session feed (see SessionFeed above): presentation, NEVER saved.
    SessionFeed sessionFeed;
    // THE relation matrix — flat, by ordinal (macro/relations.h). The
    // string-keyed map of string-keyed maps it replaced cost two temporaries,
    // two hashes and two strcmps per question, and the battle asks K² of them
    // per tick.
    RelationMatrix relations{};

    Politik politik;
    PlayerState player;
    WorldTime   worldTime = world_time_at(0, 6, 0);
    // The day the slow world last re-baked (path-cost grid) and autosaved —
    // once a season, together (Session 21). Lives HERE, not on App, so a load
    // keeps the phase instead of pushing the next autosave a season away (v22).
    int lastWorldRebakeDay = 0;
    // The ONE issuer of MacroSpawnId ordinals (v23): monotonic, never reused,
    // survives the save. Every creation path (boot spawn, quest spawn, console
    // squads) draws from here — the old max-over-living scan reissued a dead
    // NPC's ordinal, and a load could wake the player in a stranger's body
    // (problems.md 19.24).
    std::uint32_t nextMacroSpawnOrdinal = 0;
    // The ONE issuer of LANDMARK ids (v54): cities, villages, spires — every
    // named place draws from this counter at generation, so an id names ONE
    // place across all landmark kinds. 0 is reserved for "no landmark" (the
    // chronicle already files unknown subjects as 0), so issuance starts at 1.
    // Same monotonic-ordinal law as nextMacroSpawnOrdinal above: a hash or a
    // per-kind register is not an identity (CANON S20.1).
    std::uint32_t nextLandmarkOrdinal = 1;
    // СМЕНА СОСТАВА МЕСТ — СОБЫТИЕ, А НЕ СОСТОЯНИЕ (CANON S9, владелец
    // 2026-09-20: «рождение-смерть ландмарка это конкретные события, и
    // должна быть единая система-дверь, никаких проверщиков»). Счётчик
    // растёт в дверях рождения и смерти места и в тех немногих событиях,
    // что меняют проходимость мира (мост). Всё, что запечено ОТ СОСТАВА
    // (навигация S7), сравнивает одно число вместо прохода по ростеру.
    //
    // Производное состояние сессии, В СЕЙВ НЕ ЕДЕТ: загрузка поднимает
    // места через ту же дверь и тем самым честно взводит счётчик заново.
    std::uint32_t navEpoch = 0;
    // The ONE issuer of QUEST ordinals (v63), same law again. Issued at
    // ACCEPT (QuestEngine::accept) — the moment an offer stops being a
    // seed-regenerated projection and becomes an object the world stores;
    // 0 is reserved for "an offer not yet accepted". The FNV hash of the
    // quest id string that used to ride events (quest_id_key) is dead.
    std::uint32_t nextQuestOrdinal = 1;
    // The world's runtime rhythms (v24). worldTickRt is the LIVE runtime —
    // world_tick.cpp mutates it in place; macroAiRhythm is the staged image
    // of App::npcAi's persistent half (see the two sync doors in main.cpp).
    WorldTickRuntime worldTickRt;
    MacroAiRhythm    macroAiRhythm;
    // Story progress (v25): the ids of logic nodes that still EXIST (a
    // consumed one-shot stays consumed) and of those ACTIVE. Definitions are
    // code, re-registered on every boot; these two lists replay the
    // progress. Staged/applied at the same two doors as macroAiRhythm.
    std::vector<std::string> logicNodesRegistered;
    std::vector<std::string> logicNodesActive;
    GameSubState subState;
    // ПУЛ ДЕЗЕРТИРОВ — тот же ЕДИНЫЙ контейнер (M-71): существа строками
    // мира в области сверху, предметной областью пул не пользуется.
    Inventory deserterPool;
    // THE WORLD'S LOOT POOL — one VALUE, not a warehouse (owner 2026-08-30,
    // CANON S5/S10): the belongings of every squad that died with NO victor
    // (exhaustion, drowning) fold into their catalog worth and add here.
    // Future consumers (ruin & dungeon loot, mob drops) ROLL loot from
    // their own tables with a budget drawn off this number — many dead
    // caravans = richer, MORE VARIED loot, O(1) memory; storing the actual
    // items would have given a warehouse of identical sacks (rejected by
    // the owner). Signed: the ledger must show a bookkeeping bug as a
    // negative, not a wrap. Rides the save (v67).
    std::int64_t lootPoolValue = 0;
    // (The abstract TradeRoute system is GONE (v29): trade is caravan
    // AGENTS carrying real cargo between real inventories — macro/npc_ai.cpp
    // ai_caravan.)

    // (treeOverrides and depositOverrides are GONE (v36/v37): forests and
    // deposits are carrier rows of the resource-field registry, and the save
    // carries their live state whole — save.cpp takes the carriers alongside
    // the state.)

    // ── THE TWO LAYERS OF S5, AND NEITHER IS A HASH (v96) ────────────────
    // «Разрежённого хранения больше нет» (CANON S5, поправка владельца
    // 2026-09-16): both of these are FIELDS over the connected world, dense
    // in memory and sparse only on the wire, because that is a file format
    // decision and not a model one. The two unordered_maps that lived here
    // (resourceScars, and shipsAtCell below them) were the world's last
    // hashes and the last thing that could only answer «сколько здесь» by
    // scanning to answer «есть ли рядом» (problems.md §52).
    //
    // NATURE's own rows, as scars: cell → units play has taken and regrowth
    // has not yet returned, one grid per sparse-dialect row (wheat, fauna;
    // the baseline is derived from terrain/climate, the scar is the only
    // storage, a healed cell zeroes itself). Carrier rows (trees, the vein
    // kinds) keep their grid EMPTY: their live state is their own carrier.
    ResourceGrid resourceScarCells[std::size_t(ResourceFieldId::Count)];
    // THE WORKED LAYER — ONE field for the whole world («1 шахта в клетке —
    // одно поле в клетке! в том и замысел!»): a number in it means whatever
    // the FEATURE standing on that cell says it means, which is unambiguous
    // because a cell is worked exactly one way. Its first honest tenant is
    // the ships counter of a harbour or a beached hull («у поля урожай, у
    // шахты залежи, у порта корабли — элегантно»); the standing crop of a
    // parcel and the consolidated seam under a mine move here as their laws
    // are rebuilt (CANON S5 «постройка — это перенос»).
    ResourceGrid worked;

    // Features BUILT BY SQUADS (v71; owner 2026-08-31, CANON S10 «фичи
    // создаются сквадами»): the ploughed field today, the crew-laid road or
    // bridge tomorrow — appended when a squad's work stamps the feature
    // grid, re-stamped onto the seed-baked grid at load (world_fields.h row
    // Built). The grid itself stays DERIVED; this list is the truth.
    std::vector<BuiltFeature> builtFeatures;
    // (gs.shipsAtCell — третий хеш мира со сканом — УМЕР 2026-09-18: счётчик
    // пришвартованных стоит числом ФИЧИ в слое разработки `worked` выше,
    // ровно как обещал канон S10, а «ближайший корпус округи» читается
    // обходом builtFeatures вместо скана хеша.)
};

// ── The landmark-fact door: file the deed AND pay the fame ───────────────
//
// Where a place's standing lives, by the ONE landmark id space (v54). A
// spire has no standing (yet) and answers nullptr — its deeds are recorded,
// nothing is paid.
inline std::uint32_t* landmark_renown_slot(GameState& gs, int id) {
    if (id <= 0) return nullptr;
    // Same law as landmark_by_id below: the ordinal IS the address.
    const std::size_t i = std::size_t(id - 1);
    if (i < gs.landmarks.size() && gs.landmarks[i].id == id)
        return &gs.landmarks[i].renown;
    for (auto& lm : gs.landmarks) if (lm.id == id) return &lm.renown;
    return nullptr;
}

// ── ДВЕРЬ РОЖДЕНИЯ МЕСТА (CANON S9, владелец 2026-09-20) ────────────────
// Единственный способ, которым место попадает в ростер: и генезис, и
// загрузка, и всякая будущая основа деревни идут сюда. Дверь делает ДВЕ
// вещи — кладёт место и объявляет СОБЫТИЕ (navEpoch), по которому
// поднимается всё запечённое от состава. Опросов («пройти по всем и
// посмотреть, не изменилось ли») больше не существует: событие редкое,
// проверка была ежедневной, и она вдобавок сравнивала ЧИСЛО живых мест —
// смерть одного и рождение другого в одном окне гасили друг друга молча.
inline Landmark& add_landmark(GameState& gs, Landmark&& lm) {
    gs.landmarks.push_back(std::move(lm));
    ++gs.navEpoch;
    return gs.landmarks.back();
}

// ДВЕРЬ ПЕРЕХОДА — второе событие места, и оно же его СМЕРТЬ (владелец
// 2026-09-20: «уничтожение ландмарка и рождение будет как механика; сейчас
// можно менять ландмарк деревня на руины / мёртвую деревню»). Смерть места
// в этом мире не вычёркивает строку из ростера, а МЕНЯЕТ ВИД: деревня
// становится руиной и остаётся стоять следом (CANON S9 «уничтожен — и это
// оставляет след, а не пустое место»). Тем же ходом идут и рост
// (деревня → город), и любой будущий контекстный переход: переходы — данные.
//
// Вид места — строка реестра, от которой зависит всё запечённое от состава
// (кто сеет округу, кто держит рынок), поэтому дверь объявляет событие.
// Вызывателей пока нет: механика перехода не построена, дверь названа, чтобы
// у неё было ОДНО место и никто не завёл второй способ сменить вид.
inline void set_landmark_type(GameState& gs, Landmark& lm, LandmarkType t) {
    if (lm.type == t) return;
    lm.type = t;
    ++gs.navEpoch;
}

// THE by-id find over the one landmark roster. Ids are world-unique (v54's
// single ordinal issuer), so no kind is needed to resolve one.
//
// ОРДИНАЛ И ЕСТЬ АДРЕС (2026-09-20). The issuer is monotone
// (GameState::nextLandmarkOrdinal, first id = 1) and the roster is
// APPEND-ONLY — a place dies by turning LandmarkType::None, never by leaving
// the vector — so `landmarks[id - 1].id == id` holds by construction, the
// load path included (save.cpp restores in file order under the same
// issuer). The arithmetic hit IS the law; the scan under it is the honest
// fallback, kept because correctness must not rest on an invariant no
// static_assert can hold. It is not a second table: nothing is stored and
// nothing can drift out of sync.
//
// WHY IT MATTERS (numbers, AGENTS 8): sizeof(Landmark) is ~12.5 KB — the
// 256-slot inventory alone is 9 KiB and the garrison 3 KiB — so the roster
// is ~23 MB. One linear scan touched up to 1882 cache lines scattered across
// it with no locality, and a single trade decision paid up to NINE of them:
// the by-id find, not the route table, was the hot loop of that path.
inline Landmark* landmark_by_id(GameState& gs, int id) {
    if (id < 0) return nullptr;
    const std::size_t i = std::size_t(id - 1);
    if (i < gs.landmarks.size() && gs.landmarks[i].id == id)
        return &gs.landmarks[i];
    for (auto& lm : gs.landmarks) if (lm.id == id) return &lm;
    return nullptr;
}

// ── ФЕОДАЛЬНОЕ РЕБРО — ОДНА ДВЕРЬ НА ОБА КОНЦА (владелец, 2026-09-21) ─────
// CANON S24 требует, чтобы узел знал И сюзерена, И прямых подчинённых. Год
// эта пара жила как ДВЕ ПОЛОВИНЫ: колонка `suzerainLandmarkId` у вассала и
// производный индекс `vassalHead`/`vassalNext`, который никто не собирал и
// никто не читал (problems §55). Половины разъезжаются молча — поэтому концы
// ставятся ОДНИМ вызовом и снимаются одним, ровно как RelationMatrix держит
// свою симметрию одним set_relation.
//
// Старого сюзерена дверь снимает САМА: у места ровно один сюзерен, и смена
// его без снятия прежнего оставила бы вассала, платящего двоим.
inline void set_suzerain(GameState& gs, int vassalId, int suzerainId,
                         int value = 0, int term = 0) {
    Landmark* v = landmark_by_id(gs, vassalId);
    if (!v || vassalId == suzerainId) return;
    // Прежний сюзерен теряет этого вассала — с обоих концов.
    for (int i = 0; i < kMaxInterests; ++i) {
        Interest& it = v->interests.slots[i];
        if (it.stance == std::uint8_t(Stance::None)) break;
        if (it.stance != std::uint8_t(Stance::Suzerain)) continue;
        if (Landmark* old = landmark_by_id(gs, it.object))
            interest_clear(old->interests, vassalId);
        interest_clear(v->interests, it.object);
        break;                     // сюзерен у места ровно один
    }
    if (suzerainId < 0) return;    // «стал ничьим» — это и есть весь вызов
    Landmark* s = landmark_by_id(gs, suzerainId);
    if (!s) return;                // висячего ребра не заводим
    interest_set(v->interests, suzerainId, Stance::Suzerain, value, term);
    interest_set(s->interests, vassalId, Stance::Vassal, value, term);
}

// Кому это место платит дань; -1 — никому (столица, бесхозное место).
inline int suzerain_of(const Landmark& lm) {
    for (int i = 0; i < kMaxInterests; ++i) {
        const Interest& it = lm.interests.slots[i];
        if (it.stance == std::uint8_t(Stance::None)) break;
        if (it.stance == std::uint8_t(Stance::Suzerain)) return it.object;
    }
    return -1;
}

// ДОЛЖЕН ЛИ ЭТОТ ВАССАЛ ХОТЬ ЧТО-НИБУДЬ. Долг по позициям — он же ведомость
// «с кого собрано»: собранный вассал отвечает «нет» по построению, и второго
// признака («посещён в этом сезоне») в мире не заводится (S26).
inline bool owes_tithe(const Landmark& lm) {
    return lm.titheOwedValue > 0;
}
inline const Landmark* landmark_by_id(const GameState& gs, int id) {
    if (id < 0) return nullptr;
    const std::size_t i = std::size_t(id - 1);
    if (i < gs.landmarks.size() && gs.landmarks[i].id == id)
        return &gs.landmarks[i];
    for (const auto& lm : gs.landmarks) if (lm.id == id) return &lm;
    return nullptr;
}

// ── THE WORKED LAYER'S DOOR (CANON S5, v96) ──────────────────────────────
// The number under the feature standing on this cell: hulls moored at a
// harbour or a beached hull today; a mine's consolidated seam and a parcel's
// standing crop as their laws move here. It lives beside the field itself so
// every layer of the game can read it — the load path cannot link the field
// REGISTRY (that table drags the ECS behind it), and the worked layer needs
// no registry: there is one of it, and the feature on the cell says what its
// number means.
//
// The grid sizes itself from the world's own dimensions on the first write,
// so no caller has to remember to allocate and a read off an unborn world
// answers 0 — fail-closed by construction.
inline int worked_read(const GameState& gs, int x, int y) {
    return int(gs.worked.at(x, y));
}
inline void worked_write(GameState& gs, int x, int y, int value) {
    if (!gs.worked.live()) {
        if (gs.mapW <= 0 || gs.mapH <= 0) return;   // no world, no field
        gs.worked.allocate(gs.mapW, gs.mapH, 0);
    }
    gs.worked.write(x, y, std::int32_t(value < 0 ? 0 : value));
}
inline void worked_add(GameState& gs, int x, int y, int delta) {
    worked_write(gs, x, y, worked_read(gs, x, y) + delta);
}

// ONE action (S20.1: a writer that filed without paying would give a world
// where no place ever becomes somewhere; paying without filing, a legend
// nobody can read). The landmark twin of the app-side `record_deed`, and the
// same order: figure-ness is marked from the PRE-deed renown — «с этого дня
// её дела идут в анналы» — then the fact is filed, then the deed is paid
// (base + a tenth of what the OBJECT was worth: fame is made of fame for
// places exactly as for bands). Figure-ness itself is DERIVED, never stored
// (owner, 2026-08-28): a name is a word; historical weight is renown.
inline std::uint32_t record_landmark_fact(GameState& gs, FactKind kind,
                                          int landmarkId, int x, int y,
                                          int amount,
                                          int objectLandmarkId = 0) {
    std::uint32_t* subjSlot = landmark_renown_slot(gs, landmarkId);
    const std::uint32_t* objSlot =
        landmark_renown_slot(gs, objectLandmarkId);
    const std::uint32_t bar = std::uint32_t(renown_to_be_named());
    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(kind);
    f.subjectKind = fact_subject(FactSubject::Landmark,
                                 subjSlot && *subjSlot >= bar);
    f.subject = std::uint32_t(landmarkId < 0 ? 0 : landmarkId);
    if (objectLandmarkId > 0) {
        f.objectKind = fact_subject(FactSubject::Landmark,
                                    objSlot && *objSlot >= bar);
        f.object = std::uint32_t(objectLandmarkId);
    }
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = amount;
    const std::uint32_t seq = chronicle_record(gs.chronicle, f);
    if (seq != 0u && subjSlot) {
        *subjSlot += renown_for_deed(kind, objSlot ? *objSlot : 0u);
    }
    return seq;
}

// ── Relations, including the player's ────────────────────────
//
// ONE storage for "how does A regard B": gs.factions[A].relations[B]. The player
// is a row in it like anyone else (macro/faction.h "player"), so his standing —
// what the game calls reputation — is not a second map living on PlayerState.
// It used to be, and that meant two sources of truth for the same number: the
// battle pass asked reputation while the macro matrix held its own stale answer
// for the very same pair.
//
// Writes are SYMMETRIC, mirroring create_factions: a change to how the player
// regards a faction is the same change to how it regards him. That is what lets
// every consumer — combat masks, dialogue, quests, the diplomacy panel — ask one
// function about any pair without caring whether the player is on either side.

// Relation of `a` toward `b`, degrading SAFELY to 0 (neutral) for null/empty ids,
// unknown ids, or an absent matrix entry. Same faction → 100.
inline int faction_relation(const GameState* gs, const char* a, const char* b) {
    if (!gs || !a || !b || a[0] == '\0' || b[0] == '\0') return 0;
    if (std::strcmp(a, b) == 0) return 100;
    return relation_of(gs->relations, faction_slot(gs->relations, a),
                       faction_slot(gs->relations, b));
}

// The player's standing with `factionId` — a plain relation lookup on his row.
inline int player_reputation(const GameState* gs, const char* factionId) {
    return faction_relation(gs, kPlayerFactionId, factionId);
}

// ── THE binary hostility rule (damage-door track Inc 3) ────────────────────
// ONE threshold over the ONE matrix, spelled ONCE. Everything else is a form
// of this answer, never a second formula:
//   • the battle masks are its BAKED form — build_faction_masks applies the
//     same threshold over the same matrix once per tick and hostility becomes
//     a shift-and-AND (sub/movement.h);
//   • the per-entity subworld door (hostile_to_player_entity) adds only
//     SESSION state on top: the player's own side is never hostile, and a
//     TempHostileToPlayer grudge overrides the matrix until the body dies or
//     the scene ends;
//   • the stance colours (player_stance) are its continuous projection for
//     the eye — they may soften the answer, never contradict it.
// Six hand-spelled `relation < threshold` comparisons converged here; the
// macro side (squad threat, aggressive pursuit, the forced encounter) asks
// these functions, so the map and the ground read one law.
inline bool factions_hostile(const GameState* gs, const char* a,
                             const char* b) {
    return faction_relation(gs, a, b) < kHostileThreshold;
}

// Is this faction the PLAYER's enemy — the pair every player-facing consumer
// asks about (melee oracle, flee brain, dev cheat, forced encounters).
inline bool player_hostile_to(const GameState* gs, const char* factionId) {
    return factions_hostile(gs, kPlayerFactionId, factionId);
}

// Fetch a faction's row, creating it WITH ITS IDENTITY if this is the first
// mention of it in this world. Never insert a bare row: save.cpp re-keys the
// whole map by Faction::id on load, so a row written with an empty id comes back
// under the empty key — and takes every other bare row down with it.
// A faction's SLOT, claimed if this is the first the world hears of it. The
// map form created a phantom row keyed by a bare id here, and save.cpp re-keyed
// the whole map by Faction::id on load — so a row written with an empty id came
// back under the empty key and took every other bare row with it. A slot cannot
// be bare: it is a number, and an unknown id claims a reserved one.
inline FactionSlot ensure_faction_slot(GameState& gs, const char* id) {
    return claim_faction_slot(gs.relations, id);
}

// Move that standing by `delta`, writing both directions of the pair.
inline void add_player_reputation(GameState& gs, const char* factionId,
                                  int delta) {
    if (!factionId || factionId[0] == '\0' || delta == 0) return;
    if (std::strcmp(factionId, kPlayerFactionId) == 0) return;  // no self-standing
    const FactionSlot me = ensure_faction_slot(gs, kPlayerFactionId);
    const FactionSlot them = ensure_faction_slot(gs, factionId);
    if (me == kNoFactionSlot || them == kNoFactionSlot) return;
    set_relation(gs.relations, me, them,
                 relation_of(gs.relations, me, them) + delta);
}

// ── Factories ────────────────────────────────────────────────
// Mirror `defaultPlayer` / `createGameState` / `createRandomGameState`
// from state.ts. Faction relations are sampled deterministically from
// `seed` using the band system in state.ts.
PlayerState default_player();
void       create_factions(GameState& gs, std::uint32_t seed);
GameState  default_game_state(std::uint32_t seed, int mapW, int mapH,
                              const LayerParameters& mapParams = LayerParameters{},
                              int cityCountTarget = 0);

// Bridge politik → landmark lists. After `generate_politik` (and the
// `snap_cities_to_land` post-pass) the `gs.politik.cities` array holds
// the world's capitals and major cities. This populates the gameplay-
// facing `gs.settlements` (one per politik city) and settles villages
// on the best-scoring cells of each city's hinterland (R2: resources
// are primary, settlement is derived — macro/settlement_score.h), so
// the tree and deposit layers must exist BEFORE this runs. Idempotent —
// clears prior landmarks before populating.
struct TerrainData;  // fwd
struct TreeLayer;
struct DepositLayer;
void populate_landmarks_from_politik(GameState& gs,
                                     const TerrainData& terrain,
                                     std::uint8_t seaLevel8,
                                     TreeLayer& trees,
                                     DepositLayer& deposits);

} // namespace sm
