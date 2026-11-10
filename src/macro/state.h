// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "core/torus.h"   // cell_of / world_shape_ok — адрес клетки мира
#include "bus/anketa.h"
#include "ecs/components.h"   // MacroHandle — кэши игрока {слот, ID} (M-220)
#include "tables/commodity.h"   // kCommodityCount — дань по позициям (v73)
#include "bus/memory.h"   // WorldMemory — память мира с горизонтом сезона
#include "bus/upkeep.h"   // Upkeep — ОДИН счёт содержания на место и на сквад (S4)
#include "bus/interests.h"   // Interests — ВСЕ связи субъекта одной таблицей
#include "tables/squad_type.h"   // SquadType — ось рода сквада (реестр мест снесён)
#include "macro/resource_field.h"
#include "tables/npc.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/factions.h"
#include "macro/knowledge.h"
#include "macro/tree_layer.h"   // TreeLayer — живое поле леса, род 3 (M-180)
#include "macro/features.h"     // FeatureLayer — поле фич клетки, род 3 (M-456)
#include "macro/chronicle.h"
#include "macro/scent_field.h"
#include "macro/markers.h"
#include "bus/spell_book_state.h"
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
// bus/entry_context.h) — which side the player walked into the current macro
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
// what a avatar remembers (a caravan's market snapshot, a raided village)
// survives the save, 136 padding-free bytes per agent.
// v29: the OLD economy is gone (W2b-4): EconomyState's float arrays, the
// abstract TradeRoute system and cityLastTradeDay leave the save; places
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
// A beast could not stand in a creatures while the field was a byte, so a wolf
// pack was not expressible as a squad; the macro snapshot refused monster
// entities for the same reason. Both are now open (CANON.md S4/S16).
// v44: the player is an ordinary squad, so his goods and his head stopped
// being fields of PlayerState. His bag is the NpcInventory and his memory the
// AgentMemory on his squad entity, both written by the macro-snapshot record
// that already carries every other avatar's — two blocks left the player
// section of the file and no block replaced them.
// v45: a macro avatar's runtime carries his back — `carryPerSoulG` (the
// sheet's carry capacity in WHOLE GRAMS, cached beside
// travelRank/marathonRank/moveMult). (v140, 2026-10-20: единица стала
// граммом, а вторая колонка `carryCap` — весь обоз — СНЕСЕНА: сумму спины и
// голов складывает дверь `carry_capacity_g@src/macro/squad.h` у места
// употребления, поэтому устареть тоталу нечем. v141, 2026-10-21, M-302:
// СНЕСЕНА и третья — `overloadCost`, цена перегруза: вердикт владельца
// оставил в мире РОВНО ДВА хранимых веса — сколько сквад может нести и
// сколько несёт, — а цена есть их разница у места употребления и едет
// параметром. Вместе с колонкой ушёл немой кламп по её ширине.) The overload
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
// not two megabytes of zeroes). (Второй ярус — АННАЛЫ — снесён M-184.)
// not a cache but part of the world and a legends mode will read exactly them
// (owner's ruling: «сейв обязательно нёс историю»). The per-cell chains are
// NOT written: a link is derived from the facts, and a stored derivative is a
// second truth waiting to disagree.
// v52: a squad carries RENOWN (ecs::MacroNpcRuntime). A band starts nameless —
// its deeds are weather the ring forgets in a season — and BECOMES a figure by
// doing enough (owner, 2026-08-27; прежде с этого дня дела шли в АННАЛЫ —
// ярус снесён M-184). One number, not a counter plus a flag: "is it named" is derived
// from it, so the two can never disagree about the same band. The runtime is a
// POD block of the macro record, so its layout is the format.
// v53: RENOWN belongs to every MACRO entity with an identity, not to squads
// alone (owner, 2026-08-27) — a band, a city, a people. Settlements and
// villages carry theirs, so the place blocks grew a field. And what a deed
// is worth became CONTEXTUAL: the base its row gives plus a share of what the
// victim was worth, which is a number the world already kept about them.
// v54: ONE place identity (owner, 2026-08-28). Cities, villages and spires
// used to be numbered from zero in three independent registers, so an id
// alone never named a place — the chronicle paid a village's renown to the
// city wearing the same number, and two crutch bits (MacroNpcRuntime
// homeIsVillage, MacroStockKey subjectIsVillage) existed only to disambiguate.
// Now every place draws its id from GameState::nextLandmarkOrdinal — the
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
// two of its lines became honest facts instead (a struck vein — тот род
// факта снесён 2026-10-11 вместе со своим единственным писателем,
// a player's deal = Traded). 8192 saved std::strings leave the format.
// v59: the spellbook is FLAT (bus/spell_book_state.h) — ordinal-indexed
// rows over the append-only spell registry; the string ids and the three
// heap containers left the format.
// v60: dead-code sweep. LayerParameters (a save-prefix POD) dropped its
// never-read tempMin/tempMax columns.
// v61: the world seed is an INTEGER (S26 «всё дискретно»). LayerParameters
// (a save-prefix POD) carries `seed` as uint32_t where a float sat — same
// four bytes, different meaning, so the version moves. Every seed the game
// ever wrote was < 100000 (the UI decimation), exactly representable in
// both types: the worlds themselves are bit-identical.
// v62: ONE place list (CANON S9, owner 2026-08-29). The three vectors
// (settlements / villages / spires) and their three serializers became one
// `std::vector<Landmark>` with a kind column and one serializer; every kind
// writes every column, unused ones ride at zero defaults. A S9 transition
// (village→city, spire→ruin) is now a column flip, not a record move.
// v63: quest identity is an ORDINAL (CANON S20.1, owner 2026-08-29). Quests
// carry nextQuestOrdinal numbers + a POD offer-provenance triple; the id
// string and its FNV event key are dead. The eternal completedQuestIds /
// failedQuestIds string vectors became settledQuestOffers (same-day dedup —
// their only living semantic) + two lifetime counters. The codex unlock
// state is a bit per article ordinal (tables/codex.h registry, was UI-owned
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
// v73 (2026-09-02): дань по позициям — Place несёт titheOwedGoods[15] +
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
// v95 (2026-09-17): СЕЗОННОЕ ОКНО БАЛАНСОВ (CANON S19.2) — Place carries
// seasonWellbeing (the boundary window's verdict the daily population law
// lives off until the next boundary).
// v96 (2026-09-18, сессия В — снятие спецпутей): SettlementHistory вырезана
// из Place (вердикт №4 — одна память места, летопись S20.1); монета —
// просто товар (12 фракционных монет данными, ворота и список валют мертвы);
// слой разработки + счётчики кораблей полем (shipsAtCell/resourceScars
// умирают); металлы медь/золото. Старые сейвы ничего не стоят (закон P1).
// v97 (2026-09-19): КОНТЕЙНЕР — ИНВЕНТАРЬ СУЩЕСТВ (CANON S4) — на диске
// СЛОТ-строки {kind, level, count, entityId}, не по-душам: гарнизон в 752
// души — два слота, не 752 записи, и int32-стак не разворачивается в стену.
// v98 (2026-09-19): ЛОШАДЬ — ЮНИТ (CANON S10) — ResourceFieldId вырос
// строкой Horses ПЕРЕД блоком жил (скар-блоки сейва сдвинулись), фича
// FT_Pasture = байт 14, таблица NPC выросла строкой Horse. Старые сейвы
// ничего не стоят (закон P1). (Собственного ПОЛЯ у этого ряда не стало в
// v137: стадо — число фичи, см. хвост списка.)
// v99 (2026-09-19): ПОТРЕБЛЕНИЕ — ДОЛГ (CANON S10) — Place несёт
// needDebt[kCommodityCount]: сезонный счёт лестницы нужд, гасится приходом.
// v100 (2026-09-19): ПЕРКИ — ТРЕТЬЯ ВАЛЮТА (CANON S14 5-5-5/1-1-1, заглушка
// по вердикту владельца: графа осознанно нет) — LevelData вырос perkPoints,
// CharacterSheet несёт 256-битную маску PerkMask (32 байта, S26); граф
// сядет в этот конверт без движения сейва.
// v101 (2026-09-19): ОДНА МЕРА ЖИЗНИ (CANON S10/S16, вердикт владельца
// «теперь только есть благополучие и оно даёт рост») — настроение, реестр
// его полос, восстания и флаг голода ВЫРЕЗАНЫ; у места остались
// seasonWellbeing и needDebt.
// v116 (M-106 1е кластер 5): игрок в GameState двумя хэндлами; на проводе —
// ординал носителя флажка (одно u32 в скалярах мира); байт playerFlag записи
// снапшота умирает вместе с PlayerTag/PlayerSquadTag.
// v117 (кластер 5б): истина «кто игрок» — колонка playerFlag анкеты (род 2);
// флаг снова едет байтом записи снапшота — в новом роде (колонка анкеты, не
// спутник entt-тега); скаляр v116 в скалярах мира умер, биты GameState — кэши.
// v118 (M-37): ЕДИНОЕ ПРОСТРАНСТВО ОРДИНАЛОВ — места тянут id из
// nextMacroSpawnOrdinal (выдача с 1, 0 навсегда «никто» — закон нуля-ординала,
// AGENTS ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.6); nextLandmarkOrdinal покидает формат;
// сентинели «нет места» у носителей сводятся с -1 к 0.
// v119 (ход 2, вердикт 3): имя анкеты — колонка char[32] КАЖДОГО сквада
// (ecs::SquadName в MacroStore и в записи снапшота); индекс имени остался только
// дефолтом генерации.
// v120 (ход 2, ломтик B): спелл шпиля — число фичи в слое чисел фич под
// FT_Spire (ординал kSpellDefs + 1; 0 = выкачан — закон нуля-ординала);
// колонки Landmark::spellId/depleted покидают формат. Выкачанный шпиль
// забывает спелл, как истощённая жила (вердикт «выкачанность = 0»).
// v121 (ход 2, ломтик C): РОД 6 — строки фракций (macro/factions.h): id
// плоскими чарами, цвет, отношения колонкой строки, феодальные рёбра дани
// отрезками общего пула; RelationMatrix и рантайм-имена хвоста уничтожены;
// блок сейва — один pod FactionState.
// v122 (ход 2, ломтик D — ПЕРЕВОРОТ НАСЕЛЕНИЯ, M-8/M-93): паства поселения
// — ЧИСЛО ФИЧИ (`featureNumber` под FT_City/FT_Village), домашние души — ГОЛОВЫ в
// инвентаре места (один вид — один стак), души данжа — головы его толпы
// (слабейшая строка полосы crowdHabitat: шпиль → Imp). Колонки
// Landmark::population и Landmark::garrison (гарнизон как отдельный
// контейнер, M-8) покидают формат.
// v123 (ход 2, ломтик E шаг 1): имя места — ПЛОСКИЕ ЧАРЫ char[32], раскладка
// колонки анкеты ecs::SquadName; std::string покинул структуру ×32768 (ЗАКОН
// СЛОВАРЯ, DOD п.1), а с ним и переменная длина на проводе: имя едет pod'ом,
// как у сквада.
// v125: ДВЕ независимые ломки формата сошлись в одном слиянии, и обе названы,
// потому что версия обязана остаться САМОСОГЛАСОВАННОЙ (§ФОРМАТ СЕЙВА: ломать
// молча, но не врать):
//  · ось типа ОДНА — `SquadType` слился в `SquadType`, подвижные роды
//    перенумерованы (Artel 1 → 9, Caravan 2 → 10, Collector 3 → 11), чтобы
//    места сохранили значения 1..8 и три таблицы по этому ключу не
//    переразложились. Раскладка байт не изменилась НИ НА ЕДИНИЦУ — изменился
//    СМЫСЛ значений 1..3 в колонке `squadType`, и старый файл прочитался бы
//    артелью-городом молча;
//  · восемь типов урона вместо девяти (M-193): `DamageType::Slash` и
//    `BonusId::ArmorSlash` умерли, ординалы после них сдвинулись, а ординал
//    бонуса есть байт в каждом сохранённом аффиксе.
// Обе ветки подняли версию до 124 независимо; 125 — то число, которое честно
// называет, что в файле лежит И то, И другое.
// 129 — единый кап 16384 (`kUnifiedCap@src/core/caps.h`): кап записей макро,
// пул рёбер дани и `sizeof(FactionState)` в payload ужались вдвое.
// 133 — ЧЕТЫРЕ НАВЫКА ДОБЫЧИ И ХОЗЯЙСТВА СНЕСЕНЫ (M-144, 2026-10-12; имена и
// разбор — на месте реза в `tables/attributes.h`): они ушли из `SkillId`, и
// ординалы после них сдвинулись на четыре. РАСКЛАДКА НЕ ДВИНУЛАСЬ НИ НА БАЙТ — ранги лежат
// конвертом `kMaxSkills` = 64 независимо от числа названных строк, — поэтому
// версия обязана была пойти ровно по той причине, что названа у v46 выше:
// слот v132 прочитался бы с ТОЙ ЖЕ ДЛИНОЙ и НЕ ТЕМ СМЫСЛОМ, то есть ранг
// фуражирования лёг бы портняжным делом, и ни один читатель бы не заметил.
// Тот же ход у `BonusId`: четыре парные строки умерли, ординалы хвоста
// сдвинулись, а ординал бонуса есть байт в каждом сохранённом аффиксе.
// 134 — ТРИ СИРОТЫ И ДВЕ КОЛОНКИ-ПОЛОВИНЫ СНЕСЕНЫ (M-252, 2026-10-12,
// вердикт владельца). Здесь версия идёт по ОБЫЧНОЙ причине, а не по редкой:
// раскладка реально поехала, и поехала в трёх местах разом.
//   · `MacroNpcRuntime` 92 → 84 Б: ушли `stationsLeft` и `taxCarried`, у
//     обеих было НОЛЬ кодовых сайтов в дереве. Названных байт пять,
//     освободилось восемь — три добрала дыра выравнивания за u8.
//   · `CharacterSheet` 144 → 108 Б: ушла `PerkMask` (32 Б + 4 Б
//     выравнивания), сирота по дверям — прод-вызовов `has_perk`/`set_perk`
//     ноль. Из сейва ушли три строки записи маски.
//   · `Interest` терял `term` и `value` тем же днём и ПОЛУЧИЛ ИХ НАЗАД
//     (вердикт владельца «вернуть обе»): это две из четырёх колонок формы,
//     согласованной построчно 2026-09-21, то есть стройка, а не сирота —
//     разбор у `Interest@src/bus/interests.h`. На файл это не влияет ни в
//     ту, ни в другую сторону: sizeof был и остался 8 Б, а три байта, пока
//     они были выравниванием, лежали нулями — то есть ровно тем «бессрочно,
//     сила 0», что единственно и писала игра. Версию двигают первые две
//     строки списка.
// `sizeof(MacroStore)` 713 621 512 → 712 900 616 (−704 КиБ), и попутно
// компилятор назвал место, которого не знал ни один греп: `CharacterSheet`
// стоит колонкой и у сцены — `sizeof(SubObjects)` 649 → 613 Б/слот.
// 135 — ПРИБОР ЛЕТОПИСИ ПОЛУЧИЛ ПИК (M-254, 2026-10-12): в блок летописи
// добавлены 4 Б `Chronicle::factsPeak`, то есть ФАЙЛ СТАЛ ДЛИННЕЕ — обычная
// причина бампа. Поле несёт величину формулы размера кольца («самый длинный
// interestDays × факты мира ЗА ДЕНЬ»): `factsToday` обнуляется на смене
// суток и отвечал на другой вопрос, поэтому калибровать `kChronicleFacts` им
// было нельзя. Замер сразу дал число: пик 211 фактов в день × горизонт 64 дня
// (теперь ВЫВЕДЕННЫЙ из таблицы, `kChronicleLongestInterestDays`) = 13 504
// против капа 65 536 — кольцо с запасом 4.9×, и это теперь ЗАМЕР.
// (Кольцо, его кап и все три счётчика темпа дня сняты M-423, 2026-11-10:
// летопись стала дописываемым логом под капом эпохи. Версию это не двигало —
// сменилась РАСКЛАДКА, её ловит заголовок файла сам.)
// 137 — ПОЛЕ ЛОШАДЕЙ СНЕСЕНО (M-258, 2026-10-12, вердикт владельца «сноси
// поле лошадей это бред»). Файл стал КОРОЧЕ, и это тот же род причины: ряд
// `Horses` → `Livestock` перестал быть шрамовым и стал НОСИТЕЛЕМ на поле
// чисел фич, поэтому его скар-блок теперь всегда пуст, а поголовье едет в
// блоке `featureNumber`, который и так писался. Ординалы рядов НЕ двинулись (ряд
// остался на своём месте перед жилами), поэтому сдвига блоков нет — двигает
// версию ровно смена ЖИЛЬЦА блока.
// 149 — ПОЛЕ ФАУНЫ (M-371, 2026-11-02): ряд Fauna стал НОСИТЕЛЕМ на хранимое
// поле `GameState::faunaField` — его блок добавился в провод полей
// (world_fields.cpp), а скар-блок ряда Fauna теперь всегда пуст (носитель
// шрама не держит). Файл стал длиннее на блок и другой по смыслу ряда.
// 150 — M-379: долг свипов умер вместе с бюджетным драйвером — блок ритма
// макро-ИИ усох до одного джиттера (sweepAccum/pendingSweeps/sweepCursor
// покинули провод): ритм живёт пер-сквадно в rt.tickAccum строк сквадов.
// 157 — ЛОМОТЬ 1 СЕЙВ-ЭПИКА (M-439 / M-180 / M-190, 2026-11-10): файл стал
// КОРОЧЕ на семь мёртвых блоков. Ушли: строка `saveName` (прод-писателей не
// было ни одного), блок `subState` (режим оболочки — не мир; он переехал на
// `App` и загрузкой всегда встаёт в Exploring), оба списка logic-узлов,
// `factionPeaceUntilDay` (256 Б без единого прод-читателя) и четыре вечно
// пустых ряда шрамов (Fauna/Trees/Livestock/Vein — ряды-носители, шрама не
// держат). Метка времени `savedAt` на проводе ОСТАЛАСЬ — её читает экран
// загрузки; умер только её член в `GameState`.
// 158 — ЛОМОТЬ 3 СЕЙВ-ЭПИКА (M-439 / M-180, 2026-11-10): сменился ЖИЛЕЦ блока
// меток карты. Было: на метку два префикса длины с телами строк (ключ и
// подпись), байт стиля и две координаты. Стало: плоская запись
// `Marker@src/macro/markers.h` целиком, 104 Б, без дыр. Блок леса байтами
// НЕ изменился — сменился только его источник: клетки берутся из поля
// состояния `GameState::treeLayer`, а не из параметра провода.
// 159 — ЛОМОТЬ 4 СЕЙВ-ЭПИКА (M-180 / M-439, 2026-11-10): строк состояния мира
// на проводе не осталось. Имя игрока едет плоской ячейкой 32 Б вместо
// префикса длины с телом; запись метки карты — клетка, стиль, подпись (68 Б;
// строковый ключ и пара float-координат умерли); шапка блока следов потеряла
// число планов (оно — константа сборки `kFactionCount`), а индекс клетки
// следа на проводе стал адресом внутри канала под капом клеток.
// 160 — СЕЙВ ЕСТЬ ПРЯМОЙ ДАМП (M-439 / M-456, 2026-11-10): у файла больше нет
// провода. Он — заголовок и два куска памяти мира байт в байт: это состояние
// (`GameState`) и гладкая память сквадов (`MacroStore@src/bus/store.h`);
// порядок и ширина величин в файле суть раскладка этих двух типов, и ничего
// кроме неё (`save_game@src/macro/save.h`). С этого числа запись «что поехало
// в сейв» вести незачем: поехало ВСЁ, что стало членом состояния или
// колонкой стора, в тот же день. ВЕРСИЯ ОСТАЁТСЯ ДЛЯ ОДНОГО: смены СМЫСЛА при
// тех же байтах (перенумерованный ординал, другой закон той же колонки) —
// смену раскладки заголовок файла ловит сам, сравнением `sizeof`.
// Этим же шагом: поле фич клетки легло в состояние (руина переживает
// загрузку), сессионные члены уехали на оболочку, ячейка имени игрока и член
// `version` снесены, дыры выравнивания убраны под стену компилятора.
// 161 — ДВИЖЕНИЕ ЕСТЬ СОГЛАШЕНИЕ (M-2 / M-307, 2026-11-10): байты те же, смысл
// другой — ровно тот случай, ради которого версия оставлена. Хвост
// выравнивания колонки цели стал байтом «в пути» (`SquadGoal@src/bus/squad_row.h`),
// и только его читают ноги свипа: сквад из файла 160 нёс бы там ноль, то
// есть стоял бы посреди дороги, числясь идущим. В байте флагов марша
// прибавился исход прошлой думки — «прибыл».
constexpr int kSaveVersion = 161;

// (SettlementHistory — the per-settlement population ring — died 2026-09-18,
// owner verdict №4 of the second canon audit: «сноси, есть уже единая система
// фактов и событий». One memory of a place exists — the chronicle, CANON
// S20.1; a second per-place diary was a parallel memory system.)

// ── THE place record (CANON S9, owner verdict 2026-08-29) ─────────────
// One struct, one vector, a KIND COLUMN. City, village and spire used to be
// three structs in three GameState vectors — which made "what stands on a
// cell" a switch, made a S9 transition (village→city, spire→ruin) a record
// MOVE between types, and made every new place kind a new vector plus
// save code. Now the kind is data: a transition flips `type` (plus a grid
// rebake), and a new kind is its registry row plus the columns it reads.
// Fields a kind does not use sit at their zero defaults — the zero
// contribution, CANON S6 — and cost nothing but bytes (S26: size is not an
// argument).
// (ОПИСЬ ОКРУГИ — SurveyRow/LandmarkSurvey и колонка Landmark::survey —
//  УНИЧТОЖЕНА 2026-09-30, ломтик E шаг 2: она была КОЛОНКОЙ МЕСТА, а места
//  больше нет, есть сквад. ЗАМЫСЕЛ ЦЕЛ и ждёт эконом-эпика — CANON S10,
//  «КАРТА ОКРУГИ: ИЩЕТ МЕСТО, А НЕ СКВАД», там же четыре его инварианта и
//  цена сноса числом.)

// (ВЕДОМОСТЬ МЕСТА и МИРОВОЕ СРЕДНЕЕ — LandmarkLedger/WorldLedger, колонка
//  Landmark::ledger и поле GameState::worldLedger — УНИЧТОЖЕНЫ 2026-09-30,
//  ломтик E шаг 2. Ведомость была КЭШЕМ двух функций (season_demand_for +
//  stock_price) в колонке МЕСТА, а места больше нет, есть сквад. Свой дом
//  считается теперь живьём теми же двумя дверьми, чужой рынок —
//  АБСОЛЮТНОЙ СТОИМОСТЬЮ строки (market_price_seen@src/macro/npc_ai.cpp).
//  Ярусы 1 и 3 живы. Замысел и цена сноса — CANON S10, «ЗНАНИЕ О ЦЕНЕ —
//  ТРИ ЯРУСА».)

// (struct Landmark УНИЧТОЖЕНА ломтиком F: место есть неподвижный сквад, вся
// его идентичность — колонки ТЕЛА в MacroStore: spawnId, runtime.squadType,
// name, cell, kind.factionIdx. Рождение — birth_squad@src/macro/birth_squad.h,
// резолв по ординалу — macro_handle_by_spawn_id@src/macro/squad.h, обход —
// for_each_rooted@src/macro/rooted_iter.h.)

// (`GameSubStateKind` / `GameSubState` ЗДЕСЬ БОЛЬШЕ НЕ ЖИВУТ — M-190,
// kSaveVersion 157: режим оболочки не есть состояние мира. Тип и поле
// переехали в `src/app/app_state.h` (`App::subState`), в сейв не едут.)
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
// map became the flat matrix, а матрица 2026-09-30 умерла в СТРОКИ фракций
// (macro/factions.h, род 6): отношения — колонка строки, феод — рёбра пула.)

// (`SessionFeed` и её дверь `session_feed_push` ЗДЕСЬ БОЛЬШЕ НЕ ЖИВУТ — вердикт
// владельца 2026-11-10, вопрос В1 сейв-эпика: лента HUD не есть состояние мира.
// Она переехала в сессию (`Session@src/bus/session.h`), экземпляр — член `App`.)

// A settled quest OFFER's provenance — the POD triple that names an offer
// uniquely (events/quests/quest_types.h Quest: each generator fires at most
// once per settlement per day). Defined here, not there, because the layer
// order runs macro → events: player state stores it, the quest engine reads
// it through this door.
// (SettledQuestOffer умерла 2026-10-23, ломоть 3 M-72 вариант А: квест-машина
//  снесена целиком, задание игрока вернётся конструктором задач на форме цели
//  рода 2 — ломоть 6.)

struct PlayerState {
    // (ЯЧЕЙКА ИМЕНИ `name` УНИЧТОЖЕНА — вердикт владельца 2026-11-10, вопрос В3
    // сейв-эпика, дословно: «игрок ничем не особенный имена через анекты
    // сквадо … также имена могут менятьс имя игрока частныйц случац». Имя
    // живёт в ОДНОМ месте — колонке `name` анкеты сквада в сторе
    // (`SquadName@src/bus/squad_row.h`), у игрока это колонка АКТИВНОГО сквада,
    // и читает её та же дверь, что имя всякого (`squad_name@src/bus/store.h`).
    // Здесь стояла вторая ячейка той же ширины — второй носитель одного рода:
    // при вселении панели показывали имя оставленной оболочки. Экран создания
    // пишет имя прямо в колонку сквада той же дверью `set_squad_name`.)
    // (Колонка `sexIdx` УНИЧТОЖЕНА в v130, перепись сирот 2026-10-11: она
    // была КОПИЕЙ экранного поля `CreationState::sexIdx` и не имела ни одного
    // прод-читателя. Очко атрибута за «female» считается ТАМ, где стоит
    // авторская таблица выбора — `apply_creation@src/app/main.cpp` читает
    // `cs.sexIdx`, то есть экран, а не мир. Колонка состояния мира, которую
    // пишут и не читают, есть колонка-сирота, DOD п.9.)
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
    // — refreshed from this sheet through the one door every avatar's are
    // (squad.h refresh_body_from_sheet). It sat here as a nine-field private
    // store with three cached rest rates until 2026-09-10, and it was the
    // last field that made him a different kind of body from the squads
    // around him: landing 4 of the «полосы на тело» track.)
    // (No `inventory` field. The player's bag is the ordinary
    // ecs::NpcInventory on his squad entity — macro/player_entity.h
    // player_inventory(). It was the last large field that made him a
    // different kind of thing from the squads around him.)
    // NOTE. There is no `reputation` map here any more. The player's standing
    // with every faction IS his faction's row column (macro/factions.h
    // FactionRow::rel) — see player_reputation /
    // add_player_reputation below. Two stores for one number meant the battle
    // pass and the macro matrix could disagree about the same pair.
    // (No `army` field. The player's squad is an ORDINARY squad — his men
    // live in the creature area of his ONE container (M-71), reached through
    // macro/player_entity.h player_inventory(). It sat here as its own
    // creatures until 2026-08-27, and every consumer of it was a
    // player-specific path CANON S4 forbids by name.)
    // Codex unlock state: one bit per article ordinal (tables/codex.h
    // CodexArticleId; the static_assert there is the loud cap). Replaced a
    // vector of id STRINGS (v63) — a string was doing an ordinal's job.
    std::uint64_t codexUnlockedBits = 0;
    // Возраст стоит ПОСЛЕ кодекса, а не перед ним (сейв-эпик, вердикт В2):
    // восьмибайтовое слово первым — и четыре четырёхбайтовых следом ложатся
    // без единой дыры выравнивания.
    int ageDays = 1000;
    // (ЖУРНАЛА ИГРОКА ЗДЕСЬ БОЛЬШЕ НЕТ — M-423, 2026-11-10. Здесь стояли 2^20
    // копий записей летописи (32 МиБ в теле состояния), счёт, курсор по
    // номеру факта и флаг громкого капа: «что игрок узнал». Копии были платой
    // за забывающее кольцо — ссылка на вытесненную запись повисала бы. Летопись
    // стала дописываемым логом, который не забывает, и хранить стало нечего:
    // ИСТОРИЯ ИГРОКА ЕСТЬ ЦЕПОЧКА ТОГО СКВАДА, КОТОРЫМ ОН ИГРАЕТ, —
    // `chronicle_about@src/macro/chronicle.h`, тот же вопрос, что задаёт о себе
    // любой сквад мира. Своего хранилища, своего капа и своей двери захвата у
    // игрока нет: он ничем не особенный.
    //
    // ЧТО ПРИ ЭТОМ ПЕРЕСТАЛО БЫТЬ ВЫРАЗИМО, СКАЗАНО ВСЛУХ: половина «случилось
    // ЗДЕСЬ, пока он стоял на клетке» — чужой факт, в котором сквад игрока не
    // назван ни субъектом, ни объектом. Траектории сквадов лог не хранит, и
    // цепочкой участника «был свидетелем» не спросить. Её носитель назначен
    // рядом M-423 реестра (ломоть 4): род факта «оказался рядом».)
    // Четыре названных байта выравнивания (сейв-эпик, вердикт В2): состояние
    // игрока выровнено по восьмибайтовому слову кодекса.
    std::uint8_t             pad[4] = {};
    // (No spellBook since v89. A body's knowledge is the SpellBook COMPONENT
    // on its macro entity — §41 root 3: the one-copy field here was literally
    // the class combatStats was before landing 4, and it is why only the
    // player could cast, drain or be taught. His book rides his squad's
    // MacroNpcRecord like every lord's.)
    // (`factionPeaceUntilDay` УНИЧТОЖЕНА в v157 — ломоть 1 сейв-эпика: 256 Б
    // в каждом сейве, прод-читателей и прод-писателей ноль. Перемирия S24
    // придут рядом рода 6 в `FactionState`, а не колонкой игрока.)
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
    // (settledQuestOffers и оба счётчика квестов умерли с квест-машиной —
    //  ломоть 3 M-72, вариант А.)
    // (No possessedMacroSpawnId since v87. "Whom do I control" has ONE store —
    // колонка playerFlag анкеты (5б), riding the snapshot as an
    // honest byte (MacroNpcRecord.playerFlag). The field was the flag's
    // out-of-snapshot double, and the re-derivation it fed masked the load
    // raising a second player squad — SAVE-5.)
    // (No entry-side context since v88: the packed entry step and the
    // time-in-cell count are his squad's own MacroNpcRuntime bytes — the
    // same two every marcher stamps — and the accumulator toward the next
    // tick is that runtime's tickAccum, free on an input-driven squad the
    // AI sweep never thinks for.)
    // (No `memory` field. The player's head is the ordinary AgentMemory on
    // his squad entity — the same component every squad avatar carries, saved
    // by the same macro record. It sat here as a second store with ZERO
    // readers in src/: everything that remembers anything about the player
    // was already going through the entity.)
};

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
    // ШИРИНА — ПО ДОСТИЖИМОМУ ДИАПАЗОНУ (ЗАКОН ТИПА): остаток всегда строго
    // меньше `kSubworldTickDivisor@src/core/time.h` (64) — его кладёт туда
    // взятие по модулю. Здесь стояло u64, и вместе с четырёхбайтовым потоком
    // ниже оно оставляло структуре 4 Б хвостовой дыры (вердикт В2).
    std::uint32_t subworldStepRemainder = 0;
    Rng jitter{0xC0FFEEu};
};

// The persistent half of the macro-AI runtime (npc_ai.h MacroNpcAiRuntime):
// the jitter stream, and nothing else. The transient half (the squad index,
// the thinksDone witness meter) is rebuilt/re-counted every run and stays out
// of the save; the per-squad think phase rides each squad's own rt.tickAccum
// row. Synced with the live runtime at exactly two doors — staging before a
// save, applying after a load (src/app/main.cpp). (Колонки долга
// sweepAccum/pendingSweeps/sweepCursor умерли с M-379: долга думок больше не
// существует — единый драйвер платит каждый тик на месте.)
struct MacroAiRhythm {
    Rng           jitter{0xA1F0u};
};

struct GameState {
    // (ЧЛЕН `version` УНИЧТОЖЕН — сейв-эпик, 2026-11-10. Он нёс копию
    // `kSaveVersion`, ставился рождением состояния и читался НИКЕМ: версию
    // формата судит заголовок файла (`SaveHeader@src/macro/save.cpp`), а мир
    // о формате своего дампа знать не обязан. Колонка без читателя (DOD п.9),
    // и заодно — те 4 Б, из-за которых у состояния оставалась хвостовая дыра
    // выравнивания.)
    // (`saveName` и `savedAt` УНИЧТОЖЕНЫ в v157 — M-180: у обеих строк
    // прод-писателей не было. Метка времени слота живёт в файле и в
    // `SaveSummary::savedAt@src/macro/save.h`, мир её не носит.)
    std::uint32_t worldSeed = 0;
    int mapW = 1024, mapH = 1024;
    LayerParameters mapParams{};
    int cityCountTarget = 0;
    // The day the slow world last re-baked (path-cost grid) and autosaved —
    // once a season, together (Session 21). Lives HERE, not on App, so a load
    // keeps the phase instead of pushing the next autosave a season away (v22).
    int lastWorldRebakeDay = 0;
    // (Стоит здесь, среди скаляров головы, а не рядом с часами мира: шесть
    // четырёхбайтовых слов головы с этим седьмым дают границу, кратную 8, и
    // восьмибайтовые члены ниже ложатся без дыр — вердикт В2 сейв-эпика.)

    // МЕТКИ ИГРОКА НА КАРТЕ — и только они. Здесь стояла шапка «THE place
    // list … every placed place of every kind, one vector», описывавшая
    // ВЕКТОР МЕСТ; он умер 2026-10-04 (неподвижный сквад — слот `MacroStore`),
    // а проза осталась и читалась как документация к меткам. Ровно тот род
    // лжи шапки, из-за которого §0 п.2 запрещает ей верить.
    // Плоский список под именованным капом (`MarkerList@src/macro/markers.h`,
    // ломоть 3 сейв-эпика): счёт + записи фиксированной длины в теле
    // состояния, heap-члена нет. Двери — `add_marker` / `remove_marker`.
    MarkerList markers;
    // The player's map knowledge (v40): Unknown / Explored / Visible per cell.
    // Explored persists; Visible is re-derived from the player's position
    // (update_player_sight) — the load door dims it back to Explored
    // (`knowledge_dim_sight@src/macro/knowledge.h`).
    KnowledgeLayer knowledge;
    // WHAT HAPPENED (macro/chronicle.h, CANON S20.1). The world's own memory,
    // in ONE tier: a ring the world is ASKED (indexed by cell — this is what
    // lets a witcher find a monster by the traces it left), and it FORGETS.
    // Второй ярус — АННАЛЫ, «что мир помнит навсегда», — снесён целиком
    // (M-184). Значит режим легенд сегодня читать НЕЧЕГО, и это названо
    // вслух, а не прикрыто заглушкой (§1).
    Chronicle chronicle;
    // ПОЛЯ СЛЕДОВ ФРАКЦИЙ (macro/scent_field.h, CANON S10 «хищник-жертва»,
    // v75): два клеточных канала на фракцию — сила (squad_power) и цена
    // (души + груз). Едут в сейве целиком (вердикт владельца: движение не
    // фактируется, реплеить след не из чего).
    ScentField scent;
    // РОД 6 ФРЕЙМА — гладкий массив строк фракций (macro/factions.h): id
    // плоскими чарами, цвет, отношения КОЛОНКОЙ строки, феодальные рёбра
    // дани отрезками общего пула. Пришёл 2026-09-30 (ход 2 ломтик C) на
    // место RelationMatrix; рантайм-имена хвоста уничтожены вердиктом.
    FactionState factions{};

    // ПОЛЕ ВЛАДЕНИЯ ЗЕМЛЁЙ (род 3, M-90): байт на клетку тора — 0 = дикие
    // земли, k ≥ 1 = земля фракции со строкой реестра k − 1. Плоский массив
    // под капом клеток (`CellOwnerField@src/macro/politik.h`, ломоть 3
    // сейв-эпика): нулевая память и есть «ничья земля». Одна дверь чтения
    // (`faction_index_for_cell@src/macro/politik.h`), один писатель
    // (`finalize_politik` на генезисе), размер — сторона мира.
    //
    // Поле жило внутри `struct Politik` вместе со списком городов и со своей
    // копией размера карты; обёртка и была тем, что перепись родов фрейма
    // звала РАСХОЖДЕНИЕМ. Список городов оказался планом ГЕНЕРАТОРА и уехал
    // локальным буфером генезиса, копия размера умерла как второй ответ на
    // «какого размера мир» (DOD п.9), а поле встало здесь, где у поля мира
    // единая форма — рядом со `knowledge` и `scent`.
    CellOwnerField cellOwner;
    // ── ЛЕС — ЖИВОЕ ПОЛЕ НАД ТОРОМ (род 3), И ЕГО ДОМ ЗДЕСЬ (ломоть 3 сейв-
    // эпика, M-180 / M-439, 2026-11-10) ─────────────────────────────────
    // Лес жил отдельным членом оболочки приложения, пока остальные поля мира
    // лежали в этом агрегате. Расщепление было историческим, и платил за него
    // провод сейва: живые клетки леса ехали в `save_game` ОТДЕЛЬНЫМ параметром
    // рядом с состоянием, чтение возвращало их отдельным вектором, а свёртка
    // загрузки накатывала их своей дверью позже всех — поэтому свидетель
    // свёртки сравнивал лес сам с собой. Теперь поле лежит рядом со `knowledge`
    // и полями ряда, провод берёт его из состояния, а свёртка несёт его даром.
    // Носитель ряда Trees реестра полей; писатель клетки один —
    // `set_tree_count@src/macro/tree_layer.h` (игра ходит к нему через дверь
    // поля `field_move`). Второго адреса у леса нет: конверт `MacroWorld`
    // своего указателя на лес больше не несёт.
    TreeLayer treeLayer;
    // ── ПОЛЕ ФИЧ КЛЕТКИ — РОД ФИЧИ НА КЛЕТКЕ, БАЙТ НА КЛЕТКУ (род 3; M-456,
    // 2026-11-10) ────────────────────────────────────────────────────────
    // «Что построено на этой клетке»: дорога, пашня, шахта, город, руина.
    // Поле жило членом оболочки приложения и приходило в симуляцию указателем
    // конверта, хотя живой мир его ПИШЕТ — смерть города кладёт на клетку
    // руину, смерть деревни снимает фичу (глагол пульта `reshape_feature`). В
    // сейв оно не ехало и на загрузке перевыводилось из сида, то есть руина
    // после загрузки снова становилась городом. Теперь поле лежит здесь, рядом
    // с лесом и полем чисел фич, и едет с состоянием целиком. Писатель клетки
    // один — `feature_put@src/macro/features.h`; число под фичей — соседнее
    // поле `featureNumber` ниже. Второго адреса у поля нет: конверт
    // `MacroWorld` своего указателя на него больше не несёт.
    FeatureLayer features;
    PlayerState player;
    // (КЭШ НОСИТЕЛЯ ФЛАЖКА `playerFlagRef` ЗДЕСЬ БОЛЬШЕ НЕ ЖИВЁТ — вердикт
    // владельца 2026-11-10, вопрос В1 сейв-эпика. Истина «кто игрок» — колонка
    // `playerFlag` анкеты в сторе (род 2); кэш её носителя есть сессия
    // (`Session::playerFlagRef@src/bus/session.h`), и после загрузки его
    // пересобирает скан колонки. Рядом когда-то жил и второй кэш — «кто
    // оригинал», снесённый ломтём 7б M-421 вместе с битом `playerFlag.home`.)
    WorldTime   worldTime = world_time_at(0, 6, 0);
    // (ЕДИНЫЙ эмитент ординалов макро-субъектов — M-37 — ПЕРЕЕХАЛ в
    // MacroStore.nextSpawnId с M-220: идентичность выдаёт сама дверь
    // рождения store_birth, и валидность ссылки {слот, ID} судит она же.
    // Здесь он при новой игре стартовал бы с 1 и оживлял хэндлы прошлого
    // мира; у стора он монотонен через перерождение мира, как nextId арены
    // субмира. В сейве едет по-прежнему — полем стора.)
    // (СЧЁТЧИК СОБЫТИЯ СОСТАВА `navEpoch` ЗДЕСЬ БОЛЬШЕ НЕ ЖИВЁТ — вердикт
    // владельца 2026-11-10, В1: это производное сессии, а не величина мира.
    // Носитель — `Session::navEpoch@src/bus/session.h`; двигают его дверь
    // рождения осёдлого сквада и дверь загрузки.)
    // (Эмитент квестовых ординалов nextQuestOrdinal умер с квест-машиной —
    //  ломоть 3 M-72: цель игрока — род 2, своей идентичности ей не нужно.)
    // The world's runtime rhythms (v24). worldTickRt is the LIVE runtime —
    // world_tick.cpp mutates it in place; macroAiRhythm is the staged image
    // of App::npcAi's persistent half (see the two sync doors in main.cpp).
    WorldTickRuntime worldTickRt;
    MacroAiRhythm    macroAiRhythm;
    // ПОТОК БРОСКОВ ИГРОКА — ОТДЕЛЬНО ОТ ПОТОКА МИРА (M-378). Модалка встречи
    // бросала кости ТЕМ ЖЕ генератором, которым думают все сквады мира
    // (`MacroAiRhythm::jitter`): нажал «Talk» — сдвинул поток, и дальше мир
    // эволюционировал иначе. То есть «внутрисборочная воспроизводимость одного
    // сида» (§6) была ложью, как только игрок трогал меню, а ЗАКОН
    // АГНОСТИЧНОСТИ прямо запрещает миру знать, что игрок нажал.
    //
    // Соль выведена, а не взята с потолка: 0xA1F0 — соль думки мира
    // (`reset_macro_npc_ai_runtime@src/macro/npc_ai.cpp`), 0xC0FFEE — соль
    // дневного тика (`world_tick.cpp`); третья обязана отличаться от обеих,
    // иначе два потока пошли бы одной последовательностью от одного сида.
    // 0x50A1 («player») этому удовлетворяет и ни с чем в дереве не совпадает.
    Rng              playerRoll{0x50A1u};
    // (`logicNodesRegistered` / `logicNodesActive` и `subState` УНИЧТОЖЕНЫ в
    // v157 — ломоть 1 сейв-эпика. Список «существующих» узлов был равен
    // реестру кода ВСЕГДА (прод-звонящих `LogicNodeEngine::remove` ноль);
    // активный набор восстанавливается регистрацией на буте. Режим оболочки
    // живёт на `App::subState` — M-190.)
    // (ПУЛ ДЕЗЕРТИРОВ `deserterPool` и КАЗНА ПУЛА ЛУТА `lootPoolValue`
    // УНИЧТОЖЕНЫ в v131 — перепись сирот 2026-10-11, вердикт владельца
    // «снести». Обе были ПОЛОВИНОЙ КОНТУРА: писатель живой (уход за
    // невыплату складывал душу в пул; уплаченная плата и добро павших без
    // победителя складывались в казну), а ЧИТАТЕЛЕЙ — НИ ОДНОГО. Из пула
    // никто никогда не нанимал, казну никто никогда не тратил, то есть
    // «оседает в пуле» и «сгорает в казну» были словами о числе, которое
    // некому прочесть. Второй конец обоих контуров — АУКЦИОН НАЙМА и ПУЛ
    // ЛУТА как система (M-17); когда их построят, приёмники заведут заново,
    // и заведут ТАМ, где их читают. Времянки на освободившееся место не
    // поставлено — дыра названа нарядом M-206, AGENTS §1.)
    // (АРЕНДА ОКНА `leasedWindow` ЗДЕСЬ БОЛЬШЕ НЕ ЖИВЁТ — вердикт владельца
    // 2026-11-10, В1. На макро-слое аренды нет по построению, а сохраняться
    // можно только на нём, то есть состоянию мира о ней сказать нечего.
    // Носитель — `Session::leasedWindow@src/bus/session.h`, двери сдачи и
    // снятия там же; во фрейм она по-прежнему едет колонкой рода 4.)
    // (The abstract TradeRoute system is GONE (v29): trade is caravan
    // AGENTS carrying real cargo between real inventories — macro/npc_ai.cpp
    // ai_caravan.)

    // (treeOverrides and depositOverrides are GONE (v36/v37): forests and
    // deposits are carrier rows of the resource-field registry; the forest
    // lives in this state and rides the save with it, the vein field is a
    // pure function of terrain and seed and is not saved at all.)

    // ── THE TWO LAYERS OF S5, AND NEITHER IS A HASH (v96) ────────────────
    // «Разрежённого хранения больше нет» (CANON S5, поправка владельца
    // 2026-09-16): both of these are FIELDS over the connected world, dense
    // in memory and sparse only on the wire, because that is a file format
    // decision and not a model one. The two unordered_maps that lived here
    // (resourceScars, and shipsAtCell below them) were the world's last
    // hashes and the last thing that could only answer «сколько здесь» by
    // scanning to answer «есть ли рядом» (problems.md §52).
    //
    // NATURE's own row, as a scar: cell → units play has taken and regrowth
    // has not yet returned (the baseline is derived from terrain/climate, the
    // scar is the only storage, a healed cell zeroes itself). ОДНО поле, а не
    // массив по рядам реестра: шрам держит ровно один ряд — пашня (Wheat).
    // Остальные четыре — НОСИТЕЛИ (лес, жила, стадо, зверь): их живое
    // состояние лежит в их собственном носителе, и четыре заголовка, что
    // стояли здесь вечно пустыми (`resourceScarCells[Count]`), снесены в v157
    // вместе с их блоками на проводе. Что ряд-шрам один, прибито
    // `static_assert` у таблицы `kResourceFields@src/macro/macro_stock.cpp`.
    ResourceGrid wheatScars;
    // THE FEATURE-NUMBER LAYER — ONE field for the whole world («1 шахта в
    // клетке — одно поле в клетке! в том и замысел!»): a number in it
    // means whatever the FEATURE standing on that cell says it means,
    // which is unambiguous because a cell is worked exactly one way. Its
    // first honest tenant is the ships counter of a harbour or a beached
    // hull («у поля урожай, у шахты залежи, у порта корабли —
    // элегантно»); the standing crop of a parcel and the consolidated seam
    // under a mine move here as their laws are rebuilt (CANON S5
    // «постройка — это перенос»). Tenants today: the
    // harbour's ships counter and — v120 — the SPIRE'S SPELL under FT_Spire
    // (kSpellDefs ordinal + 1; 0 = orb drained, закон нуля-ординала), паства
    // поселения (v122) и — v137, M-258 — ПОГОЛОВЬЕ ЗАГОНА.
    //
    // ЧТО ИМЕННО ЗНАЧИТ ЧИСЛО, ГОВОРИТ КОЛОНКА СТРОКИ ФИЧИ
    // `FeatureDef::numberRow` (features.h), а не ветка у читателя: стадо стало
    // рядом-НОСИТЕЛЕМ на это поле, и своего массива над миром у него больше
    // нет вовсе (вердикт владельца 2026-10-12: «сноси поле лошадей это бред»).
    ResourceGrid featureNumber;
    // ── ПОЛЕ ФАУНЫ — ЖИВОЕ ПОГОЛОВЬЕ ДИКОЙ ПРИРОДЫ (M-371) ──────────────
    // Полноправное хранимое поле над тором (вердикты владельца 2026-11-02:
    // «а фауна это отдельное поле»; «это может быть полномппрваное поле -
    // фауна точно с фиачами не связана»; «его пишет СВОЙ ГЕНЕРАТОР один раз
    // при рождении мира (биом + лес…) … оно меняется по сезонам как и
    // другие ресурсы это норм»). Ряд Fauna реестра — НОСИТЕЛЬ на это поле
    // (форма Trees→TreeLayer, Livestock→featureNumber): прежний шрам ряда
    // мёртв, живое читается отсюда, рост лечит к природной ёмкости
    // (fauna_cell_capacity_at — биом + лес, БЕЗ рода: род-развилки умерли
    // тем же нарядом). Основание поселения вычищает клетку одной записью
    // через дверь ниже — событие мира, не связь с фичей в живом мире.
    ResourceGrid faunaField;
    // ── ПОЛЕ СВЕТА — ЭМИССИЯ НОЧНОГО СВЕЧЕНИЯ КЛЕТКИ (M-370) ────────────
    // Вердикт владельца 2026-11-02: «свет — своё поле … по сути это клеточка
    // светит (ну может через сквад или объект но суть в том что свет
    // генерится)»; форма А подтверждена им же («вроде А лучше»), рефреш —
    // «типа просто свет можно менять от числа на поле фичи при ребейке у
    // нас всё по сезонам». Клетка несёт u16-упаковку «палитра + сила»
    // (`tables/light_palette.h`). Писатели: рефреш фич на границе ребейка
    // (`refresh_feature_lights@src/macro/macro_lighting.cpp` — пишет ТОЛЬКО
    // клетки светящих фич, чужие огни не трогает), впредь сквады/объекты/
    // события той же дверью. Читатель один — сбор эмиттеров бейка
    // (`collect_macro_lights`); живой мир свечением не управляется (свет —
    // представление, ЗАКОН ТУПИКА РЕНДЕРА держит направление). В СЕЙВ ЕДЕТ,
    // как всякое поле состояния (вердикт владельца 2026-11-10: «все поля над
    // тором, лежащие в состоянии мира (Род 3), едут дампом единообразно и
    // агностично»); загрузочный ребейк всё равно переписывает клетки светящих
    // фич от их же чисел, так что файл и рефреш сойтись обязаны.
    ResourceGrid lightEmission;

    // (Вектор `builtFeatures` и структура `BuiltFeature` УНИЧТОЖЕНЫ в v130,
    // перепись сирот 2026-10-11. Шапка на этом месте обещала «appended when
    // a squad's work stamps the feature grid» — и была ЛЖЕЦОМ: `push_back` из
    // геймплея в дереве НОЛЬ, наполняло вектор только чтение сейва. Фичи
    // сквадами сегодня не строятся вовсе; когда будут — свой факт заведут
    // заново. Дыра названа нарядом M-206.
    // gs.shipsAtCell — третий хеш мира со сканом — умер 2026-09-18: счётчик
    // пришвартованных стоит числом ФИЧИ в слое разработки `featureNumber` выше.)
};

// ── СТЕНА: СОСТОЯНИЕ МИРА ТРИВИАЛЬНО КОПИРУЕМО (ломоть 4 сейв-эпика, M-180,
// 2026-11-10) ────────────────────────────────────────────────────────────
// Последние heap-члены состояния умерли: имя игрока и его журнал, оба канала
// поля следов, кольцо летописи и её головы цепочек лежат в теле структуры.
// С этой строки состояние мира есть ОДИН кусок памяти известного размера —
// ровно то, чего требует от пакета ЗАКОН КАНОНИЧЕСКОГО ФРЕЙМА п.7 и без чего
// сейв не может быть прямым дампом (CRAFT §10). Контейнер, строка, ссылка или
// владеющий указатель членом состояния (на любой глубине) больше НЕ
// СОБИРАЮТСЯ: запрет стоит под компилятором, а не под переписью.
static_assert(std::is_trivially_copyable_v<GameState>,
              "состояние мира обязано быть тривиально копируемым: heap-член "
              "на любой глубине ломает дамп байт-в-байт (M-180 / M-439)");
static_assert(std::is_standard_layout_v<GameState>,
              "состояние мира — плоская раскладка: без виртуальности и без "
              "смешанного доступа, адрес члена выводится из типа");

// (landmark_index_by_id / landmark_by_id / add_landmark / set_landmark_type
// УНИЧТОЖЕНЫ ломтиком F вместе со строкой и штабелем мест: резолв ординала —
// macro_handle_by_spawn_id@src/macro/squad.h, рождение — birth_squad,
// (смена вида места дверью НЕ делается: она умерла 2026-10-18 с нулём
//  прод-вызовов; событие состава поднимает рождение)
// navEpoch.)

// ── THE FEATURE-NUMBER LAYER'S DOOR (CANON S5, v96) ─────────────────────
// The number under the feature standing on this cell: hulls moored at a
// harbour or a beached hull today; a mine's consolidated seam and a parcel's
// standing crop as their laws move here. It lives beside the field itself so
// every layer of the game can read it — the load path cannot link the field
// REGISTRY (that table drags the ECS behind it), and the featureNumber layer needs
// no registry: there is one of it, and the feature on the cell says what its
// number means.
//
// The grid sizes itself from the world's own dimensions on the first write,
// so no caller has to remember to allocate and a read off an unborn world
// answers 0 — fail-closed by construction.
inline int feature_number_read(const GameState& gs, int x, int y) {
    return int(gs.featureNumber.at(x, y));
}
inline void feature_number_write(GameState& gs, int x, int y, int value) {
    if (!gs.featureNumber.live()) {
        if (gs.mapW <= 0 || gs.mapH <= 0) return;   // no world, no field
        gs.featureNumber.allocate(gs.mapW, gs.mapH);
    }
    gs.featureNumber.write(x, y, std::int32_t(value < 0 ? 0 : value));
}
inline void feature_number_add(GameState& gs, int x, int y, int delta) {
    feature_number_write(gs, x, y, feature_number_read(gs, x, y) + delta);
}

// ── Двери поля фауны — та же идиома, что у поля чисел фич выше ──────────
// Живое поголовье клетки. Писатели: генератор рождения мира (один раз),
// пульт полей через носителя ряда Fauna (fauna_apply@macro_stock.cpp — рост
// и охота), рождение поселения (вычищает свою клетку). Читает всё остальное
// пульт resource_field_read — прямых читателей у грида не заводить.
inline int fauna_field_read(const GameState& gs, int x, int y) {
    return int(gs.faunaField.at(x, y));
}
inline void fauna_field_write(GameState& gs, int x, int y, int value) {
    if (!gs.faunaField.live()) {
        if (gs.mapW <= 0 || gs.mapH <= 0) return;   // no world, no field
        gs.faunaField.allocate(gs.mapW, gs.mapH);
    }
    gs.faunaField.write(x, y, std::int32_t(value < 0 ? 0 : value));
}

// ── Дверь поля света — та же идиома (M-370) ─────────────────────────────
// Значение — упаковка «палитра + сила» (`tables/light_palette.h`); 0 = тьма.
// (read-дверь умерла 2026-11-06: единственный читатель поля —
// macro_lighting.cpp — идёт for_each_live по живым клеткам, поклеточного
// вопроса никто не задаёт.)
inline void light_emission_write(GameState& gs, int x, int y, int value) {
    if (!gs.lightEmission.live()) {
        if (gs.mapW <= 0 || gs.mapH <= 0) return;   // no world, no field
        gs.lightEmission.allocate(gs.mapW, gs.mapH);
    }
    gs.lightEmission.write(x, y, std::int32_t(value < 0 ? 0 : value));
}

// (record_rooted_fact УЕХАЛА в macro/squad.h флипом M-90 шаг 5: слава
// места — колонка runtime.renown его ТЕЛА, то есть двери нужен MacroStore,
// которого state.h не видит и видеть не должен.)

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
    // Сам себе — ВЕРХ ШКАЛЫ (тот же закон, что диагональ строк): здесь
    // стояло круглое 100 — второй ответ на один вопрос рядом с relation_of.
    if (std::strcmp(a, b) == 0) return kRelationMax;
    return relation_of(gs->factions, faction_index(a), faction_index(b));
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
//   • the per-slot subworld door (hostile_to_player_slot) adds only SCENE
//     state on top, and it adds it over the SAME faction pair: the player's own
//     side is never hostile, and a scene FEUD (`feud@src/sub/objects.h`,
//     M-422) overrides the matrix until the scene ends. Here stood a dead
//     name (`hostile_to_player_entity`) and a dead carrier (a per-body
//     TempHostileToPlayer bit that died with the BODY, not the scene);
//   • the stance colours (player_stance) are its continuous projection for
//     the eye — they may soften the answer, never contradict it.
// Six hand-spelled `relation < threshold` comparisons converged here; the
// macro side (squad threat, aggressive pursuit, the forced encounter) asks
// these functions, so the map and the ground read one law.
// СТРОКОВАЯ форма — ГРАНИЦА МИРА: переводит имена в ординалы и делегирует
// ординальной двери (`factions_hostile@src/macro/factions.h`), где порог и
// живёт. Своего сравнения с порогом здесь больше нет: он выписан в дереве
// ОДНАЖДЫ, иначе два ответа на один вопрос разъехались бы молча.
inline bool factions_hostile(const GameState* gs, const char* a,
                             const char* b) {
    if (!gs) return false;   // нет мира — нет отношений, значит и вражды
    return factions_hostile(gs->factions, faction_index(a), faction_index(b));
}

// Is this faction the PLAYER's enemy — the pair every player-facing consumer
// asks about (melee oracle, flee brain, dev cheat, forced encounters).
inline bool player_hostile_to(const GameState* gs, const char* factionId) {
    return factions_hostile(gs, kPlayerFactionId, factionId);
}

// (ensure_faction_slot умерла 2026-09-30 вместе с рантайм-именами хвоста:
// фракция мира — строка РЕЕСТРА, слот отвечает faction_index; будущей
// рождённой в игре фракции строку выдаст её собственный закон.)

// Move that standing by `delta`, writing both directions of the pair.
//
// ИГРОК — ОБЫЧНАЯ СТРОКА МАТРИЦЫ, А ЭТА ДВЕРЬ — ЕГО ЧАСТНЫЙ СЛУЧАЙ, И ТОЛЬКО
// (вердикт владельца 2026-11-03: «да это надо обобщить … потому что игрок
// ничем не особенен»). Поступок судит ОРДИНАЛЬНАЯ дверь
// `add_faction_standing@src/macro/factions.h` — одна на весь мир; здесь
// остаётся ГРАНИЦА, переводящая имя в ординал ОДИН раз (ЗАКОН СЛОВАРЯ) и
// подставляющая строку игрока актором. Своей арифметики у неё больше нет, и
// это важнее краткости: пока она сама звала `set_relation`, «игрок ударил» и
// «аватар ударил» были ДВУМЯ законами одной величины.
//
// Все четыре прежних отказа целы и ни одного не осталось ЗДЕСЬ: пустое имя и
// незнакомое дают -1 (`faction_index`), нулевой сдвиг и «сам себе» (ординал
// игрока равен ординалу игрока) отвергает сама дверь. Поэтому `strcmp` с
// именем игрока ушёл — сравнение имён заменено равенством чисел.
//
// Звонящие — ПО ПРИРОДЕ про игрока, и потому дверь жива: эффект выбора
// диалога (`src/events/effect_applicator.cpp`), выбор родины на старте и
// консоль (`src/app/main.cpp`).
//
// А ЕГО АВТОРЕЗОЛВ ОТСЮДА УШЁЛ 2026-11-03, и это был ПОСЛЕДНИЙ звонящий,
// который про игрока НЕ был: `settle_player_auto_battle@src/macro/squad.h`
// платил за убитых строкой ИГРОКА, а платить обязана фракция сквада-участника
// (вердикт владельца «Обобщить: платит фракция сквада-участника»). Признак,
// по которому дверь жива у остальных трёх: у них актор игрок ПО ПРЕДМЕТУ —
// диалог ведёт он, родину выбирает он, консоль правит его. Новому звонящему
// сюда нужен тот же признак, иначе ему нужна ординальная дверь.
inline void add_player_reputation(GameState& gs, const char* factionId,
                                  int delta) {
    add_faction_standing(gs.factions, kPlayerFactionSlot,
                         faction_index(factionId), delta);
}

// ── Factories ────────────────────────────────────────────────
// Mirror `defaultPlayer` / `createGameState` / `createRandomGameState`
// from state.ts. Faction relations are sampled deterministically from
// `seed` using the band system in state.ts.
void       default_player(PlayerState& out);
void       create_factions(GameState& gs, std::uint32_t seed);
// ── РОЖДЕНИЕ И СБРОС СОСТОЯНИЯ — НА МЕСТЕ, НЕ ЗНАЧЕНИЕМ (M-180) ─────────
// `GameState` несёт слои мира плоскими массивами под капом клеток
// (`kWorldCellCap@src/core/caps.h`) и весит мегабайты: на стеке он не живёт
// НИГДЕ, включая временные. Отсюда три двери вместо прежних «вернуть
// значением» и `gs = GameState{}` (обе клали состояние на стек звонящего):
//   · `make_game_state` — состояние рождается в куче, как `MacroStore`;
//   · `game_state_reset` — возврат к рождённому на том же адресе;
//   · `default_game_state` — сброс плюс поля новой игры, в `out`.
// Стена — обход `test_world_layers_never_by_value` в
// `tests/arch_guard_test.cpp` (объявление значением законно только у
// поимённых хозяев); вторая сеть — флаг `-Wframe-larger-than` в
// `timaert_build_flags`, называющий функцию с раздутым кадром.
inline std::unique_ptr<GameState> make_game_state() {
    return std::unique_ptr<GameState>(new GameState());
}
inline void game_state_reset(GameState& gs) {
    std::destroy_at(&gs);
    std::construct_at(&gs);
}
void       default_game_state(GameState& out,
                              std::uint32_t seed, int mapW, int mapH,
                              const LayerParameters& mapParams = LayerParameters{},
                              int cityCountTarget = 0);

// Bridge the politik PLAN → the place list. After `generate_politik`
// (and the `snap_cities_to_land` post-pass) the `cities` plan holds the
// world's capitals and major cities. This turns each plan row into a place
// of the one list and settles villages on the best-scoring cells of each
// city's hinterland (R2: resources are primary, settlement is derived —
// macro/settlement_score.h), so the tree and deposit layers must exist BEFORE
// this runs. Idempotent — clears prior places before populating.
//
// The plan arrives as a PARAMETER and not off `gs` (M-90): it is the
// generator's scratch, the caller owns it for the length of genesis, and the
// living world has no city list of its own to disagree with the creatures.
struct TerrainData;  // fwd
struct DepositLayer;
struct MacroStore;   // fwd — тела мест рождаются в store (M-90 шаг 5)
struct Session;      // fwd — рождение осёдлых объявляет событие состава
void populate_rooted_from_politik(GameState& gs, Session* ses,
                                     MacroStore& st,
                                     const std::vector<City>& cities,
                                     const TerrainData& terrain,
                                     DepositLayer& deposits);

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-180, ломоть 4 сейв-эпика) ───────────────────
// Судит КОМПИЛЯТОР, а не ревью: структура состояния мира, в которую заложили
// вектор, строку, карту, умный указатель, функтор или виртуальный метод, отсюда
// НЕ СОБЕРЁТСЯ, и сообщение назовёт тип поимённо (`core/row_law.h`, наряд
// M-169). Новая структура в этом файле обязана появиться и в этом списке — за
// полнотой списка следит `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::PlayerState);
TIMAERT_ROW(sm::WorldTickRuntime);
TIMAERT_ROW(sm::MacroAiRhythm);
TIMAERT_ROW(sm::GameState);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::PlayerState, &sm::PlayerState::codexUnlockedBits,
    &sm::PlayerState::ageDays, &sm::PlayerState::pad);
TIMAERT_PACKED(sm::WorldTickRuntime, &sm::WorldTickRuntime::pendingDailyTicks,
    &sm::WorldTickRuntime::nextDailyTickDay,
    &sm::WorldTickRuntime::subworldStepRemainder,
    &sm::WorldTickRuntime::jitter);
TIMAERT_PACKED(sm::MacroAiRhythm, &sm::MacroAiRhythm::jitter);
TIMAERT_PACKED(sm::GameState, &sm::GameState::worldSeed, &sm::GameState::mapW,
    &sm::GameState::mapH, &sm::GameState::mapParams,
    &sm::GameState::cityCountTarget, &sm::GameState::lastWorldRebakeDay,
    &sm::GameState::markers, &sm::GameState::knowledge,
    &sm::GameState::chronicle, &sm::GameState::scent,
    &sm::GameState::factions, &sm::GameState::cellOwner,
    &sm::GameState::treeLayer, &sm::GameState::features,
    &sm::GameState::player, &sm::GameState::worldTime,
    &sm::GameState::worldTickRt, &sm::GameState::macroAiRhythm,
    &sm::GameState::playerRoll, &sm::GameState::wheatScars,
    &sm::GameState::featureNumber, &sm::GameState::faunaField,
    &sm::GameState::lightEmission);
