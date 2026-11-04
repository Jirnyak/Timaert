// ЗАКОН ДИКОГО РОЛЛА — КТО ВОДИТСЯ НА ЭТОЙ ЗЕМЛЕ И СКОЛЬКО ИХ.
//
// ПЕРЕЕХАЛ ИЗ `macro/fauna.h` ЛОМТЁМ 3 ЭПИКА ПАКЕТОВ v1, и переезд — по шву
// ТИП против ЭКЗЕМПЛЯРА (§11, M-181): здесь ничего, кроме СТРОК КАТАЛОГА и
// чистой арифметики над ними, — ни одного члена изменяемого состояния мира,
// ни одной подписи с `GameState` или слоем. А спрашивает этот закон ОБА
// мира: макромир им отращивает ряд Fauna, субмир — им заселяет логово
// (`spawn_dungeon_vermin@src/sub/spawn.cpp`) и дикую клетку, и пока он лежал
// под `macro/`, законное чтение КАТАЛОГА было НЕОТЛИЧИМО от незаконного
// чтения состояния — то есть стена канала была невозможна в принципе.
//
// Половина, которая НЕ переехала и не могла: `fauna_cell_capacity_at` (ей
// нужен конверт слоёв мира) и виды над каталогом существ — они остались в
// `macro/fauna.h`, который этот файл включает.
//
// Adding a creature: one row of THE body table (tables/npc.h) that states its
// ground in its habitat mask. Adding a biome: its bit and its counts row.
// No engine code changes ever needed.
#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include "tables/behaviour.h"
#include "tables/biomes.h"
#include "tables/npc.h"
#include "tables/sprite_rows.h"

namespace sm {

// AI hint for the spawned entity.
// (`FaunaAi` lived here until 2026-08-20 — a second behaviour vocabulary for
// beasts. It is `AIBehaviour` now, the one every row speaks: tables/behaviour.h.)

// Creature faction — the registry id string (tables/faction.h), the SAME key
// every other faction consumer uses. The old FaunaFaction enum was a fourth
// parallel vocabulary whose indices collided with the humanoid one (a bandit
// creature (2) and a bandit NPC (3) compared as enemies, a bandit NPC (3) and
// a DEMON creature (3) as brothers — the spell friendly-fire check used the
// raw index). One id space ends that class of bug. nullptr = factionless.

// A creature row IS an npc row (owner, 2026-08-20: one system). The struct
// that used to live here is `NpcTypeDef` in tables/npc.h, and every creature is
// a member of the ONE table there — this alias survives only so the spawn
// tables below read as what they are: lists of rows a place may roll.
using FaunaEntry = NpcTypeDef;

// ── THE spawn law (CANON S6/S12, owner 2026-08-24) ───────────────────
// Who a cell rolls = habitat × danger-match, over the ONE body table.
//
//   weight(row, cell) = row.weight × habitat(row, cell)
//                     × danger_match(creature_tier(row), cell danger)
//
// · habitat — a bitmask COLUMN of the body table (tables/npc.h, enum-ordered
//   beside the law that reads it): which biomes the row lives in, plus the
//   forest class and the den kinds. It replaced thirteen hand-built
//   list-tables and the switch ladder that chose between them
//   (canon-audit F5) — a new species states its ground in its own row.
// · ТИР — АВТОРСКАЯ КОЛОНКА СТРОКИ, И ЧИТАЕТСЯ ОН ОДНОЙ ДВЕРЬЮ
//   (`creature_tier@src/tables/npc.h` над колонкой `baseLevel`, M-382/M-384).
//   Здесь стояла ВЫВЕДЕННАЯ мощь `spawn_strength` — её некролог ниже, — и
//   она была ВТОРЫМ ответом на вопрос «насколько эта тварь высоко» рядом с
//   тиром, которым считается цена убитого (DOD п.6, наряд M-387).
// · danger_match — symmetric around tier == danger (the owner's word:
//   hell is peopled by demons, not by demons plus rabbits), HALVING per
//   kDangerHalfLife bytes of mismatch, floor 1 — a doom-tier horror in a
//   safe meadow is «исчезающе малая вероятность», literally, never zero.
//   No cutoffs, no bands: composition flows across the map.
//
// The zone therefore changes WHAT SPAWNS and nothing else — the negative
// control that no number is scaled on the body after the pick stands
// (subworld_spawn_parity_test; the autolevel stays dead).

// Биты ареала (kHabForest/kHabRuin/kHabSpire/kHabTown, hab(Biome)) переехали
// в tables/npc.h 2026-09-22 — к колонке строки, которую они описывают.
// (Страж КОЛОНКИ ТОЛПЫ умер 2026-11-02 вместе с колонкой: раскладка толпы
//  сцены уехала в данные субмира — `kCrowdLayout@src/sub/crowd_layout.h`, —
//  и авторится там ИМЕНАМИ битов kHab* из tables/npc.h, так что второго
//  правописания, которое этот страж сверял, больше не существует. Страж
//  колонки `faunaHabitat` умер тем же способом в M-371: род-развилки
//  place_def мертвы, полосу объявляет МОДУЛЬ клетки именами битов —
//  `FaunaBand@src/tables/npc.h`.)

// Halving distance of the match law. Derived, not tuned: ten halvings span
// the whole 0..255 continuum (a full-span mismatch lands at 2^-10 of the
// peak before the floor), so kDangerHalfLife = 256 / 10.
inline constexpr int kDangerHalfLife = 256 / 10;

// ── ЗДЕСЬ ЖИЛА `spawn_strength`, И ОНА УНИЧТОЖЕНА 2026-11-04 (M-387) ──────
//
// ЧЕМ ОНА БЫЛА: байт силы строки, ВЫВЕДЕННЫЙ из её же боевых колонок —
// `log₂(hp × урон/период)`, нормированный по КРАЙНИМ строкам каталога в
// 0..255, — и им судилось расселение.
//
// ПОЧЕМУ СНЕСЕНА, ПЕРВОЕ: ДВА ОТВЕТА НА ОДИН ВОПРОС МИРА (DOD п.6). После
// M-384 цена убитого считается АВТОРСКИМ тиром (`creature_tier`), а
// расселение судилось этой выведенной мощью, и разойтись им ничто не мешало:
// автор поднимал тир, а водиться тварь оставалась там же. Вопрос «насколько
// эта тварь высоко» один, значит и носитель один — колонка.
//
// ПОЧЕМУ СНЕСЕНА, ВТОРОЕ — ШКАЛА БЫЛА ПЛАВАЮЩЕЙ, И ЭТО БЫЛ ЖИВОЙ ДЕФЕКТ.
// Нормировка шла по `lo`/`hi` САМОГО каталога, поэтому добавление ОДНОЙ
// сильной строки пересчитывало байт у ВСЕХ сорока семи и сдвигало расселение
// ВСЕГО мира — правка контента меняла географию. На тире шкала АБСОЛЮТНА
// (лестница 0..255, `kMaxTier@src/tables/progression.h`): новая строка встаёт
// на свою ступень и никого не двигает. Замер сноса — в теле коммита M-387.
//
// ПОЧЕМУ ВЫВОД БЫЛ НЕВЕРЕН ПО СУЩЕСТВУ, А НЕ ТОЛЬКО ПО ФОРМЕ: он не видит
// половины строки. Тот же довод уже записан у лестницы
// (`src/tables/progression.h`, вердикт владельца 2026-11-03: «могут быть
// твари мало хп куча резистов и огромнвй урон не умничай где нюансы есть») —
// `hp × удар / период` не знает ни резистов, ни блока, ни дальнобойности,
// значит стеклянная пушка получала низкую ступень, а жирный безобидный мешок
// высокую. Тир есть ВЫСКАЗЫВАНИЕ автора, и формулой оно не заменяется.
//
// ВМЕСТЕ С НЕЙ УМЕРЛА `row_power` — её ЕДИНСТВЕННЫЙ звонящий был здесь
// (перепись M-387: 1 кодовый сайт в `src/`, 0 в `tests/`), а своя шапка
// утверждала «Второго счёта силы в игре нет» при живом
// `head_power@src/macro/auto_battle.h`, который считает ту же `hp × dps`
// ИНАЧЕ — с бронёй, с `flatAdd`/`multPct` и с полом периода 0.1 против 0.25.
// То есть шапка была свидетелем, который врёт (§0 п.2), и второй счёт силы
// пережил её на год.

// The symmetric never-zero match term, in 1/1024ths of the peak.
//
// СТУПЕНЬ, А НЕ «СИЛА», И ИМЯ ЗДЕСЬ НЕ УКРАШЕНИЕ: дверь ОДНА на двух
// потребителей, и каждый подаёт свою ступень ОДНОЙ лестницы 0..255 — спавн
// живности тир строки (`roll_spawns` ниже), режиссёр лута ценностную ступень
// вещи (`loot_issue@src/macro/loot_pool.cpp`). Параметр звался `strength`,
// когда у спавна была выведенная мощь; после M-387 это слово назвало бы
// величину, которой нет ни у одного звонящего (ЗАКОН ИМЕНИ п.3). Единицы у
// них общие не потому, что три вещи тождественны, а потому, что мир их
// СОПОСТАВЛЯЕТ (`src/tables/progression.h`: «сопоставление и есть причина
// единиц»).
std::uint32_t danger_match_weight(std::uint8_t rung, std::uint8_t danger);

// Everything the roll wants to know about the cell — assembled by the
// caller from its CellContext / CellFacts. Zero members are legal silence.
// РОДА МЕСТА ЗДЕСЬ БОЛЬШЕ НЕТ (M-371): фауна — поле дикой природы, полосу
// особой клетки объявляет её МОДУЛЬ (band), земля отвечает по лесу/биому.
struct SpawnContext {
    Biome        biome = Biome::Meadow;
    bool         forest = false;          // is_forest_cell(treeCount)
    FaunaBand    band{};                  // модульная полоса; bit 0 = земля
    std::uint8_t danger = 0;              // the cell's danger byte (zones)
};

// ОРДИНАЛ, А НЕ СТРОКА (ЗАКОН СЛОВАРЯ п.1-2, наряд M-187). Здесь стоял
// `const char* factionId`, и `faction_index()` звался НА КАЖДОЕ ТЕЛО — дважды
// в `src/sub/spawn.cpp`, то есть линейный скан 64 строк реестра со `strcmp`
// внутри цикла заселения. Имя переводится в число ОДИН раз, на границе ролла
// (`roll_spawns`), как велит закон: «граница переводит имя в ординал ОДИН
// раз». Ширина — та же, какой её берёт `BodySpec::faction`; «неизвестная
// строка» остаётся прежним значением `std::uint16_t(-1)`, которое звонящие
// уже получали от `faction_index`, — поведение не тронуто ни в одном бите.
struct FaunaPick { const FaunaEntry* entry; std::uint16_t faction; };

// Roll a cell's wild population by the law. Count comes from the land's
// counts row (or the module's band); faction comes from the species row's own
// wildFaction and from nowhere else — a wild roll belongs to the LAND (M-39
// killed the place-kind override that used to dress a spire's slope deer as
// demons).
std::vector<FaunaPick> roll_spawns(const SpawnContext& ctx,
                                   std::uint32_t& rngState);

// (pick_crowd_row — ролл рода толпы по полосе crowdHabitat — УНИЧТОЖЕН
// 2026-10-04, D-хвост: род, уровень и лицо жителя — ФАКТ ГОЛОВЫ инвентаря
// на всех трёх путях (улица, интерьер, дверь рассвета), и выдумывать род
// заново на каждом входе было побочной системой. Колонку crowdHabitat
// читают теперь гейты «держит ли род толпу» — все четыре в СУБМИРЕ — и
// пять `static_assert` ниже; род ДУШИ решается при поселении, не при
// воплощении.)

// (`row_power` — `hp × dps` строки в double — УНИЧТОЖЕНА 2026-11-04 вместе с
//  `spawn_strength`, своим единственным звонящим; разбор и причина — в её
//  некрологе выше. Мощь строки в бою по-прежнему считает
//  `head_power@src/macro/auto_battle.h`, и теперь это ЕДИНСТВЕННЫЙ счёт силы
//  в игре — ровно то, что ложно утверждала шапка умершей.)

// ── ЗДЕСЬ ЖИЛА `weakest_crowd_kind`, И ОНА УНИЧТОЖЕНА 2026-10-27 ──────────
//
// ЧЕМ ОНА БЫЛА: «слабейшая строка полосы crowdHabitat этого рода мест» —
// ВЫВОД рода души данжа (шпиль → Imp, руина и логово → CaveBat), чтобы вид
// стака не выдумывался при заселении (вердикт владельца 2026-09-30: «души
// данжей — тоже головами; вид стака НЕ выдумывать»).
//
// ПОЧЕМУ СНЕСЕНА: у вывода исчез ВОПРОС. Оба её звонка умерли одним
// коммитом `6ba675f1` (M-320, вердикт владельца «снести колонки целиком») —
// ветка `settle_souls@src/macro/labour.h` и проход
// `regrow_dungeon_populations`, — потому что данж перестал быть сквадом:
// руина и шпиль стали чистыми фичами, заселять и отращивать их будет ИХ
// СОБСТВЕННЫЙ модуль (CANON S28, механизм 2) и своими строками. Перепись
// перед резом: **НОЛЬ звонящих** в `src/` и в `tests/`, при трёх прозах,
// которые ещё утверждали обратное — и громче всех её собственная шапка:
// «ЕЁ ЗОВЁТ ДВЕРЬ ЗАСЕЛЕНИЯ (labour.h settle_souls)».
//
// ПОЧЕМУ ЭТОГО НЕ ВИДЕЛ НИКТО, И ЭТО УРОК, А НЕ ОПРАВДАНИЕ: `inline` в
// ЗАГОЛОВКЕ, поэтому `-Wunused-function` молчит по построению — мёртвая
// дверь в заголовке невидима компилятору, в отличие от статической в
// `.cpp` (ту `-Wunused-function` назвал сам же в M-320: так умерла
// `place_context_score`). Нашла её ПЕРЕПИСЬ КОЛОНКИ: `crowdHabitat`
// считалась «самой растянутой по слоям», а растяжку держал читатель внутри
// двери с нулём звонящих.
//
// ЧТО ОСТАЛОСЬ ВМЕСТО НЕЁ: ничего, и это правильный ответ (§1: временных
// костылей не ставим). Понадобится данжу население — его даст его модуль.

// ── The honest headcount (Session 16) ────────────────────────────────
// A cell's fauna CAPACITY — how many heads its own spawn table carries
// (the winning table's maxCount). ПРИРОДНАЯ ёмкость: биом + лес, рода места
// в ней нет (M-371). Она — базовая ряда Fauna: потолок роста и кламп выемки
// носителя (хранимое поле GameState::faunaField, заполняет генератор).
int fauna_cell_capacity(Biome biome, int treeCount);

// (The breeding step is the ONE growth law — resource_fields_daily_growth,
// macro/resource_field.h: beasts breed where beasts are, capped by this
// capacity — and lives with the rows: macro/macro_stock.cpp.)

} // namespace sm
