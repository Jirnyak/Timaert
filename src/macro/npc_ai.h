// Macroworld NPC AI: behaviour dispatch for persistent macro NPCs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "ecs/world.h"
#include "macro/macro_stock.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "macro/spawners.h"
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
// (kSpRegenPctPerHour, kMacroWalkCellsPerHour). 24 × 32 / 8192 = 0.09375 h:
// 256 thinks make the day, exactly.
inline constexpr float kAiTickGameHours =
    24.0f * float(kAiTicks) / float(kTicksPerDay);

// A profession works ITS OWN village's ground: half the live worlds'
// village spacing (~21-23 cells, derive_city_spacing/2) rounded to po2 —
// a work trip stays inside the home hinterland, never the neighbour's.
// The spawn side reads the same number: ore inside this reach raises the
// profession, and the man it raises can actually walk to the ore.
inline constexpr int kGathererReach = 16;

// The automaton's CAMP threshold: legs below this fraction of the bar on
// campable ground pitch camp NOW, before any debt — an eighth is the margin
// a crew keeps to make camp at all. The BITE stays a law for whoever still
// chooses to march (the open sea, the player); without this decision the
// fleet lived camp-to-camp on chronic exhaustion and bled to death on the
// road — 447 caravans in 24 days, measured by the balance run 2026-08-30.
// Public so squad_travel_test derives its march anchor from the same number.
inline constexpr int kCampBarDivisor = 8;

// ── THE flat bucket grid ─────────────────────────────────────────────────
//
// Prefix sums plus one sorted item array — a counting sort, the shape
// `sub::UnitGrid` already uses for the same job in the battle. It replaces a
// `vector<vector<T>>`, which is the DOD defect CANON S26 names: a heap
// container PER CELL, so a 128×128 grid was sixteen thousand vector headers
// with sixteen thousand possible allocations, rebuilt from scratch at the top
// of every AI sweep.
//
// Items are u32 because both users address by one: a tree grid stores indices
// into the tree array, a squad grid stores entity bits. Two passes and, after
// the first build, ZERO allocations — the scatter cursors are a member for
// exactly that reason.
struct CellBuckets {
    int cellSize = 8;
    int cols = 0;
    int rows = 0;
    std::vector<std::uint32_t> begin;    // cols*rows + 1 prefix sums
    std::vector<std::uint32_t> items;    // bucket-sorted payload
    std::vector<std::uint32_t> cursor;   // scatter cursors; members = no churn

    std::size_t cell_of(int gx, int gy) const {
        return std::size_t(gy) * std::size_t(cols) + std::size_t(gx);
    }
    const std::uint32_t* cell_begin(int gx, int gy) const {
        return items.data() + begin[cell_of(gx, gy)];
    }
    const std::uint32_t* cell_end(int gx, int gy) const {
        return items.data() + begin[cell_of(gx, gy) + 1];
    }
};

// Size the grid and clear the counts. Call, then `bucket_count` once per item,
// then `bucket_prefix`, then `bucket_scatter` once per item — the counting
// sort's three steps, spelled out so a caller cannot do them out of order
// without noticing.
void bucket_reset(CellBuckets& g, int mapW, int mapH, int cellSize);
void bucket_count(CellBuckets& g, int gx, int gy);
void bucket_prefix(CellBuckets& g, std::size_t itemCount);
void bucket_scatter(CellBuckets& g, int gx, int gy, std::uint32_t item);

struct TreeGrid {
    CellBuckets grid;
    const std::vector<TreePoint>* trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

// Transient spatial index of macro SQUADS (Session 15) — the sibling of
// TreeGrid above, rebuilt from the live registry at the top of every AI
// drive, NEVER stored per map cell and never serialized (owner constraint:
// no per-cell NPC arrays). It is what lets a squad SEE another squad: the
// threat step scans the neighbouring buckets instead of every entity.
struct SquadIndex {
    CellBuckets grid;
};

void build_squad_index(SquadIndex& g, ecs::World& w, int mapW, int mapH,
                       int cellSize = 8);

struct MacroNpcAiRuntime {
    Rng           jitter{0xA1F0u};
    std::uint32_t sweepAccum = 0;   // world ticks toward the next sweep
    int         pendingSweeps = 0;
    std::size_t sweepCursor = 0;
    SquadIndex  squadIndex;         // rebuilt per drive; buckets reused
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
};

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
CaravanDeal trade_caravan_at_station(Inventory& hold, float capacityKg,
                                     Landmark& market,
                                     int charisma, int bargaining);
// The village crew selling at its nearest city: unload everything, then
// spend the WHOLE purse down the home's needs ladder («деревня не копит
// капитал», owner 2026-08-30) — each line up to a season's stock at home;
// what the market cannot supply leaves coin to ride home for the tax graph.
CaravanDeal trade_vendor_at_market(Inventory& bag, float capacityKg,
                                   Landmark& market,
                                   const MemoryEntry* homeSnapshot,
                                   int homePopulation, EconSite homeSite,
                                   int charisma, int bargaining);

// ── ПОРУЧЕНИЕ: цель сквада = {глагол, объект} ────────────────────────────
// (CANON S10 «универсальный ИИ сквадов», владелец 2026-09-02.) Один слой
// выбора целей на ВСЕ широкие классы: рефлексы прерывают, аукцион выбирает,
// машины-глаголы исполняют. Глагол — что делать; объект — над чем: строка
// kGathererDefs (Gather) или ординал рынка (Sell). Пара живёт в
// ecs::MacroNpcRuntime (errandVerb/errandObject, сейв v74) и выдаётся
// аукционом ротации; пере-аукцион ТОЛЬКО по завершении цели (артель
// растворилась — завтрашняя ротация решает заново). Будущие классы целей
// (патруль, охота, война, разведка) — новые глаголы-строки, не новая форма.
enum class ErrandVerb : std::uint8_t {
    None = 0,    // поручения нет: поведение по колонке ai строки типа
    Gather = 1,  // добыть: объект = строка таблицы целей kGathererDefs
    Sell = 2,    // рейс сбыта (излишки + дань-относ): объект = ординал рынка
    // Патруль горячей округи (CANON S10 «стража», 2026-09-02): объект =
    // ОРДИНАЛ ЛАНДМАРКА округи-цели, не её номер — номер округи живёт до
    // первого перепёка графа, а ординал вечен (v54) и едет в сейве; округа
    // цели восстанавливается nav_region_at по клетке ландмарка.
    Patrol = 3,
};

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
int rotate_worker_squads(MacroWorld& mw, int day);

// Squads EAT (owner 2026-08-30/31; CANON S10, реф M&B): bread is for the
// ROSTER only — the leader is a SUBJECT and needs nothing by himself («0
// бойцов = 0 хлеба и жалования», owner 2026-08-31 — the M&B law, and why a
// lone caravan is honestly immune to hunger). Each roster soul eats one
// bread a day out of the squad's OWN bag; a short day bleeds an eighth of
// the roster into the deserter pool AT ONCE (?34, one mechanic). Called
// once per game day beside the labour rotation. Returns souls deserted.
int feed_squads_daily(MacroWorld& mw);

// ── THE provisioning law of squad creation (owner 2026-08-31, CANON S10):
// «универсальная механика создания сквада — он должен быть загружен
// хлебом». The raising landmark loads the new squad's bag with bread for
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
