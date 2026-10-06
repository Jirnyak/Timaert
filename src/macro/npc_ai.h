// Macroworld NPC AI: behaviour dispatch for persistent macro NPCs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "ecs/world.h"
#include "tables/behaviour.h"
#include "macro/macro_stock.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "macro/spawners.h"
#include "tables/squad_type.h"
#include "macro/squad_index.h"
#include "macro/squad_walk.h"
#include "macro/tree_layer.h"

namespace sm {

struct DepositLayer;

// How often a macro NPC thinks, in WORLD TICKS (core/time.h) — not in wall
// seconds. On the map that is 32 ticks = half a real second, exactly what it
// always was. Underground it is half a second of WORLD time, which is eight
// real seconds, because the AI now rides the same clock as the economy, the
// calendar and the sun instead of keeping a timer of its own. A lord no longer
// crosses the continent while you clear one room, and the macro world costs
// kSubworldTickDivisor times less to simulate while you are down there.
inline constexpr std::uint32_t kAiTicks = 32;
static_assert(kTicksPerDay % kAiTicks == 0,
              "the AI cadence must divide the day");

// Real seconds one AI period lasts ON THE MACRO MAP — the only place these
// interpolated positions are ever seen. Presentation only; the AI itself never
// reads it.
inline constexpr float kAiPeriodSeconds =
    float(kAiTicks) / float(kTicksPerRealSecond);

// GAME HOURS one AI think covers — the exchange rate that lets a squad pay
// and recover through the same per-game-hour laws the player uses
// (kRestRegenPctPerHour, kMacroWalkCellsPerHour). 24 × 32 / 8192 = 0.09375 h:
// 256 thinks make the day, exactly.
inline constexpr float kAiTickGameHours =
    24.0f * float(kAiTicks) / float(kTicksPerDay);

// kGathererReach moved to macro/resource_field.h (2026-09-16): the reach
// field the deposit layer stamps needs the SAME number, and deposit_layer.cpp
// cannot include this header — it drags EnTT into targets that do not link it.
// A constant two modules must agree on lives below both of them, never inside
// one of them.

// The automaton's CAMP threshold: legs below this fraction of the bar on
// campable ground pitch camp NOW, before any debt — an eighth is the margin
// a crew keeps to make camp at all. The BITE stays a law for whoever still
// chooses to march (the open sea, the player); without this decision the
// fleet lived camp-to-camp on chronic exhaustion and bled to death on the
// road — 447 caravans in 24 days, measured by the balance run 2026-08-30.
// Public so squad_travel_test derives its march anchor from the same number.
inline constexpr int kCampBarDivisor = 8;

struct TreeGrid {
    CellBuckets grid;
    const std::vector<TreePoint>* trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

struct MacroNpcAiRuntime {
    Rng           jitter{0xA1F0u};
    std::uint32_t sweepAccum = 0;   // world ticks toward the next sweep
    int         pendingSweeps = 0;
    std::size_t sweepCursor = 0;
    SquadIndex  squadIndex;         // rebuilt per drive; buckets reused
    // Порядок свипа по ординалу (macro/squad_walk.h) — скрэтч драйверов,
    // ноль аллокаций после прогрева.
    std::vector<SquadWalkEntry> sweepOrder;
};

struct MacroNpcAiSliceResult {
    int  npcsProcessed = 0;
    int  sweepsCompleted = 0;
    bool backlog = false;
};

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime, std::uint32_t seed);

// The AI think's view of the world: THE layer envelope (macro_stock.h
// MacroWorld, CANON S6) plus the drive-state that is not a layer — the RNG,
// the player's position, the transient squad index and the resolution gate.
// Before the door (2026-08-24) this struct carried its own parallel copy of
// eight layer pointers, and the two drivers assembled those copies line by
// line, twice (canon-audit H2) — one of them once forgot `deposits` and every
// miner in the world stopped digging while the player was underground. The
// envelope is embedded, not copied: a layer exists here because it exists in
// the world, and a null layer reads as "no contribution" (fail-closed), so
// tests without a world stay as they were.
struct TickContext {
    MacroWorld      mw{};
    int             mapW = 0;
    int             mapH = 0;
    Rng*            rng = nullptr;
    // NOT a perception channel (owner, 2026-08-29: «игрок ничем не особенен»
    // — the player's squad sits in the SquadIndex like anyone's). The one
    // consumer left is try_move's stop-ON-the-meeting-cell law: a multi-cell
    // march must not hop OVER the player, because the forced-encounter door
    // (Inc 6) is geometric and looks at his cell.
    float           playerX = 0.0f;
    float           playerY = 0.0f;
    // Squad↔squad perception (Session 15). `mw.world` + `squads` let a
    // behaviour see the OTHER squads; `allowAutoBattle` gates the meeting's
    // resolution — the map path fights, the underground drive only perceives,
    // because squads standing in the player's 3×3 window may have LIVE
    // projected bodies whose fight belongs to the ground, not to the resolver.
    const SquadIndex* squads = nullptr;
    bool              allowAutoBattle = true;
    // СЛОТЫ ИГРОКА, резолв один раз на свип (1е): «кем я на карте» (флажок,
    // вселение двигает) и родной сквад (зарезервированный ординал). Читатели
    // — гейты встреч и исключение свипа; до смерти тегов заполняются из
    // тегов (make_tick_context), после — из GameState. kMacroNoSlot = нет.
    std::uint16_t playerFlagSlot  = kMacroNoSlot;
    std::uint16_t playerSquadSlot = kMacroNoSlot;
    // Враждебность реестра, запечённая на свип (CANON S10 «хищник-жертва»):
    // бит g в [f] = фракция f враждебна фракции g — ОДИН порог над ОДНОЙ
    // матрицей (factions_hostile), прожёванный заранее, чтобы охота по следу
    // не гоняла строковые слоты отношений на каждый think. uint64 хватает по
    // построению: лимит мира = kMaxFactions = 64 (faction.h).
    std::uint64_t factionHostileMask[kMaxFactions] = {};
};

// ── След и охота (scent_field.h, CANON S10 «хищник-жертва», 2026-09-03) ──
// Публично по прецеденту trade_caravan_at_station: тест водит один think.
// Смелость охоты: преследуем след силы ≤ моя сила × 2^shift. Величина следа
// = сила × время присутствия, не сила хозяина — остывший след занижает,
// ошибки консервативны, их ловит визуальный рефлекс при контакте (закон
// боя). Обе — крутилки дубль-прогона.
inline constexpr int kHuntBoldShift = 1;
// Пол запаха в единицах поля (квант kScentQuantShift): след беднее — не
// стоит и шага, иначе боец дёргается на каждую пылинку диффузии.
inline constexpr std::uint32_t kHuntScentFloor = 8u;
// Писатель полей следов — вклад сквада в свою клетку (сила = squad_power,
// цена = души по строкам найма + ценность груза); зовётся из dispatch на
// каждый think, для игрока — свипом (scent_player_deposit внутри драйверов).
// Дробный СКРЕТЧ марша внутри одного think (transition of the scale split,
// 2026-09-10): ХРАНЕНИЕ клетки сквада — ecs::MacroCell (одно число, S2), а
// эта пара float живёт только на стеке думки — драйвер декодирует клетку на
// входе и кодирует обратно после settle. Никогда не компонент: полклетки в
// хранилище невыразимы по построению. (Хвост: перевод внутренней арифметики
// марша на int убьёт и этот скретч.)
struct MacroPos { float x = 0.0f; float y = 0.0f; };

void scent_squad_deposit(MacroHandle self, const MacroPos& p,
                         const ecs::NPCKind& kind, const TickContext& ctx);
// Рефлекс охоты: незанятый боем combatant идёт ВВЕРХ по градиенту чужой
// ЦЕНЫ под фильтром СИЛЫ (след силы ≤ моя сила × 2^kHuntBoldShift); true =
// think съеден охотой, макроцель в rt не тронута (пауза, не амнезия).
bool scent_hunt_step(MacroHandle self, MacroPos& p,
                     const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                     ecs::Pools& pools, const TickContext& ctx);

// ── Trading at a market (owner, 2026-08-30; CANON S10/S25) ───────────────
// Locality is the law: every decision reads the market the squad STANDS ON
// — no omniscience, no rumours. Two strategies over the ONE price law
// (economy.h stock_price at post-trade supply; transfer_value moves coin —
// nothing minted, nothing confiscated). At namespace scope (the
// settle_landmark_day pattern) so caravan_deal_test can drive one deal.
struct MemoryEntry;
struct CaravanDeal {
    int boughtValue = 0;      // coin paid INTO the market for goods taken
    int soldValue   = 0;      // coin the market paid for goods delivered
    int movedTableValue = 0;  // TABLE value of all goods that changed hands
                              //   (the chronicle's dealValue, as before)
};
// The caravan's STATION stop: sell into the market's shortage (up to its
// daily demand), buy its surplus (above its daily demand) — the bounds ARE
// the price law's own break-even, so no threshold constants exist.
// charisma/bargaining are the trader's SHEET (the one trade-price law of
// economy.h modulates both halves): a caravan out-trades a peasant because
// its row rolls better numbers — never a hardcode (owner 2026-08-30).
// `sink`/`user` — канал фактов мира (Consumed при гашении долга рынка,
// CANON S10): проданное в место по счёту съедается СРАЗУ. Тесты водят
// сделку без канала — гашение то же, факты молчат.
CaravanDeal trade_caravan_at_station(MacroStore& st, Inventory& hold,
                                     float capacityKg,
                                     std::uint16_t marketSlot,
                                     int myTradePct, int theirTradePct,
                                     EconFactSink sink = nullptr,
                                     void* user = nullptr);
// The village crew selling at its nearest city: unload everything, then
// spend the WHOLE purse down the home's needs ladder («деревня не копит
// капитал», owner 2026-08-30) — each line up to a season's stock at home;
// what the market cannot supply leaves coin to ride home for the tax graph.
//
// `home` — ТЕЛО дома (невалидный хэндл = у крю нет дома). Что почём и чего не хватает,
// считается здесь же живьём — теми же двумя дверьми (`season_demand_for` +
// `stock_price`), которыми это считала снесённая ведомость. Она и решает,
// что стоит везти: товар берут, если дома за него дают больше, чем просят
// здесь. nullptr (у крю нет дома) — крю НИЧЕГО НЕ ПОКУПАЕТ и уезжает с
// выручкой: без дома честнее не гадать, а продать и вернуться (CANON S10,
// ярус 1 — торговля стоит и без знания).
//
// ЧТО ЗДЕСЬ УМЕРЛО 2026-09-30 (ломтик E шаг 2): параметр
// `const LandmarkLedger* homeLedger` вместе с самой ведомостью — она была
// КЭШЕМ этих двух функций в колонке МЕСТА, а места больше нет, есть сквад.
// Закон не изменился ни на строку; исчезла копия и сезонный проход, её
// писавший.
//
// ЧТО ЗДЕСЬ УМЕРЛО 2026-09-20: `homeSnapshot` (4-битный класс памяти крю с
// потолком «много = 4096» — из-за него город с 45 млн хлеба выглядел
// голодным и мир качал хлеб ВВЕРХ) вместе с `homeDebt`, `homePopulation` и
// `homeSite`, которые существовали только чтобы пересчитать домашний спрос
// по этому огрублённому снимку. Память сквада (ярус 3) веса не несёт нигде.
CaravanDeal trade_vendor_at_market(MacroStore& st, Inventory& bag,
                                   float capacityKg,
                                   std::uint16_t marketSlot,
                                   MacroHandle home,
                                   int myTradePct, int theirTradePct,
                                   EconFactSink sink = nullptr,
                                   void* user = nullptr);

// ТИП СКВАДА живёт в tables/squad_type.h — его называют двое: сквад
// (колонкой) и СТРОКА РЕЕСТРА МЕСТА (объявляя, кого поднимает).
// Строка таблицы целей, добывающая ресурс `row` — объект поручения Gather;
// -1 = такой ресурс артелями не добывается. Порядок таблицы — деталь
// npc_ai.cpp: снаружи цель называют РЕСУРСОМ, не индексом.
enum class ResourceFieldId : std::uint8_t;
int gather_goal_row(ResourceFieldId row);

// ── The daily labour rotation (owner 2026-08-30; CANON S10) ──────────────
// «Поселение поднимает рабочий сквад → сквад идёт к полю → возвращается,
// кладёт на склад, растворяется в населении.» Souls are the stock every
// working crew draws from and returns to; the roster multiplies the take at
// the squad's one SP price (ai_gatherer). Called once per game day:
// yesterday's crews standing home in Idle DISSOLVE first (souls + leftovers
// back to the landmark), then every settled landmark raises a fresh crew for
// each ROW of its registry crew list with no crew already out — sized to
// TODAY'S population. Genesis seeds no eternal gatherers any more: a crew
// cut down on the road stays dead, and the town raises fewer souls
// tomorrow.
//
// АУКЦИОН ЦЕЛЕЙ (CANON S10, владелец 2026-09-02): у рабочего сквада НЕТ
// специализации — крестьянская артель строки CrewGate::Auction получает
// поручение аукционом. Кандидаты = все строки таблицы целей (find_worksite)
// + рейс сбыта; скор ДЕНЬГАМИ по закону цены: ценность дома (stock_price —
// нужда сама дорожает дефицитом) × ожидаемый дневной тейк / (1 + маршрут).
// РУЛЕТКА по скору — артели диверсифицируются без координации; нет цели с
// положительным скором = вывод аукциона, артель не поднимается. Терм
// опасности (Died-факты у маршрута) — добавить ПОСЛЕ, вес дубль-прогоном.
// Returns crews raised.
// ── ДВЕРЬ СТАНЦИИ: куда течёт товар отсюда ───────────────────────────────
// Рулетка по весу 1/(1+дни пути) среди жилых мест соседних округ (CANON S7,
// закон тора — расстояние меряется по кратчайшему пути на торе, а не по
// разнице координат). Объявлена здесь, потому что ЗАКОН обязан иметь
// свидетеля напрямую: прежде его пинали через ИИ каравана, и когда род
// каравана умер (2026-09-21), закон остался бы без свидетеля вовсе.
// prevId = -1 — станции «откуда пришли» нет.
int pick_next_station_(const TickContext& ctx, const MacroPos& p,
                       int currentId, int prevId, float& outX, float& outY);

// ── ЦЕНА ЧУЖОГО РЫНКА = АБСОЛЮТНАЯ СТОИМОСТЬ СТРОКИ (CANON S10) ─────────
// Вердикт владельца 2026-09-30, дословно: «у нас есть абсолютная стоимость /
// локальная цена от спроав предлложения контекста и / цена/массу для как раз
// транзакукий и что брать с собой - эмерджентные деньги».
//
// Эта дверь отвечает на первый из трёх вопросов и только на него: СКОЛЬКО
// СТРОКА СТОИТ САМА ПО СЕБЕ. Ярус 2 (ведомость места и мировое среднее из
// неё) уничтожен 2026-09-30 вместе с колонками места; на его ДАЛЬНЮЮ
// половину встало то же по смыслу число — «дальнее место выглядит обычным
// рынком», — но лежащее строкой каталога, а не пересчитываемое сезонным
// проходом по всем местам. Рассинхронизировать его теперь неоткуда.
//
// БЛИЖНЕЙ ПОЛОВИНЫ ЯРУСА 2 НЕТ, и это названная дыра: локальный дефицит и
// завал у соседа спрашивающему не видны, пока эконом-эпик не построит цену
// от спроса и предложения. СВОЙ дом — не ярус и никогда им не был: его
// склад и счёт читаются живьём там, где принимается решение.
//
// Возвращает 0 у строки, товаром не являющейся.
int market_price_seen(int commodityIdx);

int rotate_worker_squads(MacroWorld& mw, int day);

// КЕМ ДУМАЕТ СКВАД БЕЗ ТИПА — лестница приоритетов, ЗАКОН (владелец,
// 2026-09-10): приказ (маршрут в SquadOrders — его наличие И ЕСТЬ приказ)
// > строка стола анкет по ординалу тега > строка КАТАЛОГА ТЕЛ. Поведение не
// хранится — выводится каждый think из данных на сущности; новая ступень =
// данные + одна строка в определении (npc_ai.cpp), никогда ветка в
// диспетче. Объявлена здесь ради свидетелей и будущих читателей лестницы.
//
// ЗВАЛАСЬ `effective_behaviour` и была ЕДИНСТВЕННЫМ ответом на «что делает
// этот сквад». С 2026-09-21 первый ответ даёт ТИП СКВАДА (SquadType выше), а
// эта лестница осталась ветке `None` — сквадам без макро-источника
// поведения. Имя сменилось, потому что прежнее врало о её роли: «effective»
// значило «окончательное», а окончательное теперь тип.
//
// ПОСЛЕДНЯЯ СТУПЕНЬ — ТЕЧЬ ГРАНИЦЫ МИРОВ (CANON S2): каталог тел принадлежит
// микромиру. Ступень умирает вместе с генезисными одиночками (порция Б-6),
// и тогда вся функция уходит целиком.
AIBehaviour untyped_squad_behaviour(const MacroStore& st, MacroHandle h,
                                    const ecs::NPCKind& kind);

// THE SQUAD SEASON WINDOW (owner 2026-08-30/31 + 2026-09-17; CANON S10,
// S19.2, реф M&B): board and pay are for the ROSTER only — the leader is a
// SUBJECT and needs nothing by himself («0 бойцов = 0 хлеба и жалования» —
// why a lone rider is honestly immune to hunger). On the season BOUNDARY
// every squad with a roster settles BOTH needs a season ahead, out of its
// OWN bag:
//   · board — РАЦИОН СВОЕЙ СТРОКИ в день на каждую душу ростера, чей бы он
//     ни был (npc.h `npc_board_per_day`, 2026-09-22). Здесь стояло «одна
//     душа, чья строка на содержании (upkeepGoldPerDay >= 0; звери и
//     монстры — нет), со скидкой на Foraging ведущего» — и то и другое
//     снесено: рот теперь объявляет колонка рациона, а не колонка платы, и
//     скидки на счёт больше нет («один закон без исключений»);
//   · pay — the one upkeep law × the season, and the paid coin BURNS into
//     the world loot pool («жалованье сгорает»).
// Each need is covered WHOLE or not debited at all; ANY miss bleeds an
// eighth of the roster into the deserter pool ONCE per window («ВСЕ НУЖДЫ
// ДОЛЖНЫ БЫТЬ ПОКРЫТЫ, иначе потеря 1/8»). The judge is the SOLDIER'S own
// row, never the leader's — the old leader-typed gate was the player-special
// Adventurer.upkeep=0 door, dead by «игрок == нпц»: the player's squad pays
// here through the very same loop. Returns souls deserted.
int squad_season_window(MacroWorld& mw, int day);

// Daily bag hygiene — the auto-scrap half of the old daily feed loop (CANON
// «Крафт/Скрап»: авто-скрап ИИ по порогу >50%; the player's bag is NEVER
// touched — his scrap is a manual act). Returns stacks melted.
int squad_bags_hygiene_daily(MacroWorld& mw);

// A roster's SEASON of needs — the one arithmetic the boundary window bills
// by and the landmark's crew-loading fills by (drift between the two would
// be a second truth of содержание). Bread is per soul whose own row is on
// upkeep, foraging-scaled by the leader's effective sheet; wage is the one
// upkeep law × the season.
// БОРД и ПЛАТА — ровно две нужды армии, и это вердикт, а не упрощение
// (владелец 2026-09-18): горожанин судится по ВСЕЙ лестнице нужд, солдат —
// по харчу и жалованью. Поле звалось `bread` и тем самым делало литерал
// "bread" на месте списания «правильным на вид»; борд — это ГОЛОДНАЯ СТРОКА
// лестницы, какой бы она ни была (econ_day.h hunger_item_index).
//
// ЗДЕСЬ СТОЯЛИ `SquadSeasonNeeds` И `squad_season_needs` — снесены
// 2026-09-22: счёт ростера стал ОДНИМ на весь мир и переехал к суду,
// которому служит (macro/roster_window.h `roster_bill`). Разбор — в теле,
// на месте бывшего определения (npc_ai.cpp).

// ── THE provisioning law of squad creation (owner 2026-08-31, CANON S10):
// «универсальная механика создания сквада — он должен быть загружен
// хлебом». SINCE 2026-09-17 (S19.2) this is the GARRISON SORTIE'S law only:
// a population crew is perpetual, its содержание is the boundary window's
// balance (season loaded at creation/boundary — rotate's load_season_upkeep),
// and the trip loaf died with the daily feed. A patrol still carries bread
// for its march+dwell days.
// The raising landmark loads the new squad's bag with bread for
// its ROSTER (the leader eats nothing — the M&B law above):
//   portion = soldiers × (roundtrip days to the destination + the work day)
// Days derive from the squad's own errand — the same march that will walk
// it — so a far vein honestly demands a bigger loaf. A store that cannot
// fill the portion loads what it has (credit-before-debit): a starving
// village cannot outfit a far expedition, and that is the truth of it.
// Returns bread actually loaded.
int provision_squad(Inventory& store, Inventory& bag, int soldiers,
                    float roundtripCells, float freeCarryKg);

// Macro-view path: scans all macro NPCs each step and dispatches those whose
// per-NPC tick accumulator matured. `ticks` is world ticks elapsed. The
// envelope must carry at least `gs` and `world`; every other layer is an
// optional contribution (see TickContext above).
void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle = true);

// Smooth macro NPC render positions toward their logical cell positions.
// Mirrors TS `visualX/Y` interpolation and snaps long seam/teleport jumps.
void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt);

// Subworld path: queues one AI sweep per kAiTicks of WORLD time, then
// dispatches at most `max_npc_ticks` entities this step. Because the clock
// crawls underground, so do the sweeps — the macro world keeps thinking, at the
// pace of the day it is actually living through.
MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    MacroWorld& mw,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle = false);

} // namespace sm
