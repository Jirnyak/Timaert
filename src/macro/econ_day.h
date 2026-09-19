// The honest economy day-loop — костяк №1 (work_vector, owner-approved form).
//
// THE LAW this module exists to uphold: RESOURCE IS CONSERVED. Nothing is
// created from population formulas; every unit is gathered from a deposit by
// worker-days, transformed by a recipe, or consumed by a need. The self-play
// test (tests/econ_v1_test.cpp) proves the ledger balances to the unit —
// that test, run over years of game time, is the balancing arbiter.
//
// Anchor unit: the PERSON-DAY. 1 bread feeds 1 pop for 1 day; a gatherer
// pulls kGatherPerWorkerDay raw units a day; each recipe names its
// output-per-worker-day. Round po2-family numbers by house style.
//
// FACTS: every noteworthy happening is reported through EconFactSink — the
// «система обязана объявить свои факты» invariant. This is a POD callback,
// NOT the EventBus: macro is L1 and may not include events (L3); the app
// layer forwards the facts it wants onto the bus when this loop is wired in.
//
// NOT WIRED into world_tick yet — deliberately additive. Wiring replaces
// EconomyState's twin float arrays with Stockpile and is its own increment.
#pragma once
#include <array>
#include <cstdint>
#include "macro/attributes.h"   // SkillId/Skills — ремесло открывает рецепт
#include "macro/commodity.h"
#include "macro/seasons.h"
#include "macro/items.h"

namespace sm {

// ── State ────────────────────────────────────────────────────────────────

// Integer units per commodity — discrete by house style; signed so a
// bookkeeping bug shows up as a negative number, not a silent wrap.
// THE store of a place is its INVENTORY — there is no second container and no
// second index space (owner's ruling, 2026-08-27: полное слияние). A flat
// `Stockpile` of 14 commodity counts used to live here and be converted to and
// from the landmark's inventory TWICE PER GAME DAY per landmark, through a
// string lookup each way, because the same noun was addressed by two different
// ordinals. `bread` is one row of one catalog now.
//
// The day's steps stay PURE (owner: «шаги остаются ЧИСТЫМИ»): they take data
// and tables, never the world.
//
// A commodity's CATALOG ordinal, resolved once and cached — the day loop asks
// this instead of carrying a second numbering of the same things.
int commodity_item_index(int commodityIdx);

// ── Data tables ──────────────────────────────────────────────────────────

// v1: buildings are ABSTRACT (owner ruling) — a recipe names the KIND of
// place it runs in, not a building.
// Any (owner 2026-08-30, CANON S10): a recipe every settled place can run —
// the bread row moved here, because «деревня печёт хуже уже потому, что её
// меньше»: the population-efficiency law below prices the difference, not a
// second recipe and not a site wall.
// (EconSite УМЕР 2026-09-18, вердикт владельца «сносим сайт». Он был стеной
// ПО ВИДУ: деревне открыт хлеб, всё прочее — City-only, потому что City. Что
// место УМЕЕТ, теперь говорит его анкета — те же скиллы, тот же лист, что у
// всех в макромире, — а рецепт называет ремесло и ранг. Деревня не чеканит не
// потому, что она деревня, а потому что её кузнечный ранг ноль.)

// Рецепт открыт этим рукам? Один вопрос, один ответ, ноль веток по виду.
inline bool recipe_known(const Skills& sk, SkillId craft, int minRank) {
    return sk.of(craft) >= minRank;
}

// A recipe names only THE SCHEDULE — what this kind of place works on, and
// where. Its MATTER became the catalog row's composition on 2026-09-11, and
// its TEMPO followed on 2026-09-12 (owner: «единая SP-система труда»):
// batches-per-person-day is the item's own labour column (macro/items.h
// item_labour), the same number the hand's SP price divides by. Two tables
// of «из чего хлеб» — and then two of «сколько труда в хлебе» — each
// drifted apart exactly once before they were merged.
struct RecipeDef {
    const char*  output;    // commodity id
    SkillId      craft;     // ЧЬИ руки это делают
    std::uint8_t minRank;   // и с какого ранга умеют
};

// The MINT output marker: not a commodity row — the produce scheduler
// resolves it into the town's own faction coin (CANON S10: чеканка = рецепт;
// выход монет из единицы металла = стоимость металла по единой таблице цен,
// «таблица цен и есть монетный двор»; сеньораж эмерджентен из рыночного
// спреда серебра).
inline constexpr const char* kMintOutput = "coin";
// The mint's matter is NOT stated here (owner verdict 2026-09-12, «привести
// к единой системе»): it is the COIN ROW'S OWN composition — {silver 1} →
// yield 32 (items.cpp kPartsAuthoring, «состав монеты и есть монетный
// двор») — so striking and MELTING coin are the one reaction every other
// item runs. What this row keeps is the RIGHT and the TEMPO: only a place
// with a coin named runs it, at outputPerWorkerDay BATCHES (= metal units)
// a day.

// THE productivity anchor (owner 2026-08-30/31, CANON S10): one worker at ANY
// link of the chain covers the needs of ~32 souls — «1 добытчик кормит 32
// душ» extended through the whole chain (поле → печь → рот) on 2026-08-31.
// Declared above the recipe table because the bread row derives from it.
inline constexpr int kGatherPerWorkerDay = 32;

// РЕМЕСЛО И РАНГ вместо вида места. Ранги расставлены редко и без претензии:
// это РАННЯЯ ДЕМКА (владелец, 2026-09-18) — закладывается СИСТЕМА, а рецептов
// у ремесла со временем станет много, и тогда шаг между ними станет узким сам
// собой. Ремесло названо по МАТЕРИИ, с которой работают руки.
//
// ДЕРЕВООБРАБОТКИ отдельной строкой пока нет (пять ремёсел — слово владельца),
// поэтому мебель и резьба квартируют у masonry как «что сложено и сколочено».
// Переселить их = поменять одну колонку, кода это не касается.
inline constexpr RecipeDef kRecipes[] = {
    // BREAD = the anchor made chain-wide: a baker's day turns exactly one
    // farmer's gather-day of grain (32) into 32 bread, so the farmer+baker
    // pair feeds 16 and the slack pays for crafts and the road.
    // (What each output CONSUMES — and HOW FAST it turns — lives on its
    // catalog row: items.cpp kPartsAuthoring composition + labour columns.)
    {"bread",     SkillId::Cooking,     1},
    // ЧЕКАНКА — кузнечное дело высокого ранга. Права чеканки КОЛОНКОЙ не
    // существует (канон, вердикт №12): чеканит тот, чьи руки умеют.
    {kMintOutput, SkillId::Blacksmith, 30},
    {"bricks",    SkillId::Masonry,     1},
    {"cloth",     SkillId::Tailoring,   1},
    {"tools",     SkillId::Blacksmith,  1},
    {"furniture", SkillId::Masonry,    10},
    {"wagon",     SkillId::Blacksmith, 15},
    {"jewelry",   SkillId::Blacksmith, 20},
    {"carving",   SkillId::Masonry,     5},
    {"statue",    SkillId::Masonry,    25},
    // ЗЕЛЬЯ ВАРЯТ (владелец, 2026-09-18). Состав у них в каталоге был всегда
    // ({mat_herb 2} → 8), в производственном дне — не было. Пока травы в мир
    // не приходят, строка честно варит ноль партий: у рецепта нет материи, а
    // не нет рецепта.
    {"potion_hp", SkillId::Alchemy,     1},
    {"potion_mp", SkillId::Alchemy,    10},
};
inline constexpr int kRecipeCount = int(sizeof(kRecipes) / sizeof(kRecipes[0]));

// The needs ladder (потребности по ярусам). popPerUnitDay: one unit serves
// that many pop-days (po2 → the daily demand is a shift). Vital shortfall
// STARVES; Instrument/Luxury shortfall only goes unmet (mood/growth math
// consumes these counts at wiring time).
struct NeedDef {
    const char* commodity;
    int popPerUnitDay;
};
inline constexpr NeedDef kNeeds[] = {
    {"bread",     1},    // 1 хлеб = 1 житель-день; ЕДИНСТВЕННАЯ голодная нужда v1
    {"cloth",     32},   // одежда изнашивается: 1 на 32 жителе-дня
    {"bricks",    64},   // поддержание жилья
    {"tools",     16},
    {"furniture", 64},
    {"jewelry",   32},
    {"carving",   32},
    {"statue",    512},
};
inline constexpr int kNeedCount = int(sizeof(kNeeds) / sizeof(kNeeds[0]));

// ── ГОЛОДНАЯ СТРОКА ЛЕСТНИЦЫ — одна дверь на всех, кто ест ────────────────
//
// Одна строка отвечает не за неуют, а за ГОЛОД. Она НЕ НАЗВАНА словом — она
// УЗНАЁТСЯ, ровно по тем двум колонкам, по которым её и судит
// econ_debt_boundary: нужда, у которой одна единица кроет один житель-день
// (popPerUnitDay == 1). Что она из категории ЕДА — закон, который держит
// свидетель: ярус товара (CommodityTier) умер 2026-09-18, «что это за вещь»
// отвечает ОДНА категория каталога (items.h ItemType).
//
// ЗАЧЕМ ДВЕРЬ, А НЕ СЛОВО. Тот же харч списывают ТРОЕ, и до 2026-09-18 они
// спрашивали его тремя способами: население — лестницу (честно), гарнизон
// места (world_tick garrison_upkeep_) и сквад в поле (npc_ai
// squad_season_window) — литералом "bread". Литерал здесь хуже обычного
// дрейфа: он держит в руках ответ на вопрос, который УМЕЕТ меняться. Сделай
// голодной другую строку или переименуй эту — лестница поедет, а две армии
// молча продолжат искать в закромах слово, которого там больше нет. И это
// не выглядит как поломка: голод не наступит, наступит НЕсписание — армия
// перестанет есть, останется сытой и бесплатной, а зелёная суита ничего не
// заметит, потому что «съедено 0 из 0» ни одному инварианту не противоречит.
//
// АРМИЯ ЕСТ ТОЛЬКО ЭТУ СТРОКУ, И ЭТО НАМЕРЕННО (вердикт владельца
// 2026-09-18, спрошено прямо): горожанин судится по ВСЕЙ лестнице (хлеб
// голодит, остальное копится неудовлетворённым), а солдат — по харчу и
// плате, и только. «Армия хочет БОРД и ПЛАТУ» — это разница замысла, а не
// недоделка, и дверь названа «голодная строка» именно поэтому: она отдаёт
// ту единственную нужду, которую обязан покрыть всякий, кто кормит людей.
constexpr int hunger_need_row_() {
    int found = -1, count = 0;
    for (int i = 0; i < kNeedCount; ++i) {
        if (kNeeds[i].popPerUnitDay != 1) continue;
        if (found < 0) found = i;
        ++count;
    }
    return count == 1 ? found : -1;
}
// Строка лестницы, а не каталога: индекс в kNeeds.
inline constexpr int kHungerNeedRow = hunger_need_row_();
static_assert(kHungerNeedRow >= 0,
              "the needs ladder must carry EXACTLY ONE hunger row — the need "
              "served one unit per pop-day. Zero of them means nobody can "
              "starve; two means the code that feeds the world has to pick, "
              "and every eater would pick differently");
// Что эта строка — ЕДА, проверяет свидетель (econ_v1_test): каталог виден
// только своей единице трансляции, и врать компилятору про это нечем.

// Голодная строка авторским КЛЮЧОМ — для дверей, которые берут строку
// (haul_between). Пространства id лестницы и каталога едины, ключ один.
inline constexpr const char* hunger_item_id() {
    return kNeeds[kHungerNeedRow].commodity;
}

// Она же каталожным ОРДИНАЛОМ — то, ЧЕМ её списывают из закромов. Строка
// остаётся ключом авторским: резолв один раз за процесс, не поиск на доступ
// (тот же контракт, что у commodity_item_index — и та же арифметика, потому
// что мост между пространствами id ровно это и делает).
inline int hunger_item_index() {
    static const int idx = item_index(hunger_item_id());
    return idx;
}

// One worker per this many heads (po2) — the BENCH quota: the share of
// hands staffing the recipes (econ_produce_day). The daily CREWS are the
// landmark's own law since 2026-08-31 (landmark_registry: crews rows +
// labourShift — «какие сквады кто спавнит — таблично»); the city's
// labourShift = log2 of this number, so the two laws meet at the same
// eighth of a town.
inline constexpr int kHeadsPerCityWorker = 8;

// (THE WORKING RHYTHM died here, 2026-09-16. `kWorkCyclesPerBar` = 4 declared
// how many hauls a rested worker made in a day, and `kGatherPerCycle` = 32/4
// derived how much one haul brought — a number honestly derived from a number
// nobody could derive. Both are gone: a worker now takes ONE object per act at
// the player's own price, an ARTEL takes one per hand, and the trip home
// happens when the backs are full, the bar is spent, or the ground gives
// nothing. How many trips that makes is a consequence of carrying capacity and
// stamina — a property of the world instead of a knob. CANON S14.1.)

// ── THE PRICE OF AN ACTION (CANON S14.1) ─────────────────────────────────
// Every action costs SP, and the price is one sentence: THE BAR DIVIDED BY
// THE RATE — how many such actions the body does in a day. Marching, felling,
// mining, ploughing, building, crafting: one purse, one formula, no second
// labour law.
//
// It is a DOOR because the sentence was written five times in three dialects
// — `maxSp / kWorkCyclesPerBar` (three copies in npc_ai.cpp), `maxSp /
// kGatherPerWorkerDay` (the player's harvest), `maxSp / item_labour` (a craft
// batch) — each with its own `max(1, …)`. The numbers agreed; the spelling did
// not, and the sixth author would have got the divisor wrong in silence.
//
// The floor of 1 is the law, not a guard: an action a body can perform is an
// action it can be tired by, so no rate however generous makes work free.
//
// `ratePerDay` is a COLUMN of whatever is acting — cycles in a bar, objects in
// a day, batches in a person-day. A new action names its rate; it does not
// rewrite this.
inline constexpr int sp_price(int maxSp, int ratePerDay) {
    return ratePerDay > 0 ? (maxSp / ratePerDay > 1 ? maxSp / ratePerDay : 1)
                          : maxSp;
}

// HOW MANY HANDS a production has. Hands multiply the YIELD and never the
// price: the bar belongs to the SQUAD (it is the leader body's own bar and
// does not grow with the roster), so N souls do N workers' work for one
// action's price. Owner, verbatim: «каждый работник рубит по дереву, SP
// тратится как у игрока, просто деревьев в число людей больше».
//
// The leader is a hand too — that is the +1, and it is why a lone walker is
// not a special case of anything.
inline constexpr int production_hands(int rosterSize) {
    return 1 + (rosterSize > 0 ? rosterSize : 0);
}

// ── Facts ────────────────────────────────────────────────────────────────

struct EconFact {
    enum class Kind : std::uint8_t {
        Gathered = 0,       // commodity, amount
        Produced = 1,       // commodity, amount
        FamineStarted = 2,  // amount = starved pops today
        FamineEnded = 3,
        Starved = 4,        // amount = pops that went unfed today
        Consumed = 5,       // commodity, amount — the needs ladder's take
        Minted = 6,         // amount = coins struck (commodity = silver row)
        Scrapped = 7,       // amount = non-fungible stacks melted to matter
                            // by the overflow law (items.h auto_scrap_overflow)
    };
    Kind kind{};
    int commodity = -1;
    int amount = 0;
    // Which landmark's day this was. The pure steps are landmark-BLIND (they
    // see one Inventory); the id is stamped by the relay in world_tick.cpp —
    // the one caller that knows whose store it handed in. -1 = unattributed
    // (a direct pure-step call, e.g. econ_v1_test).
    int landmarkId = -1;
};
using EconFactSink = void (*)(void* user, const EconFact& fact);

// ── СТОРОНА-ПРИЁМНИК с долгом места (CANON S10, вердикт 2026-09-19) ──────
//
// «Любая смена инвентаря, которая добавляет предметы» — одна форма для
// дверей прихода (haul_between, transfer_value_dense): сумка проходит
// голым инвентарём (needDebt == nullptr, неявная конверсия), МЕСТО отдаёт
// свой счёт и канал фактов — и дверь гасит долг СРАЗУ тем, что упало
// (econ_pay_debt после кредита). Дневной такт settle_landmark_day остаётся
// СТРАХОВКОЙ для путей мимо дверей (торговая панель игрока).
struct Depot {
    Inventory& inv;
    std::int32_t* needDebt = nullptr;  // счёт места или null (сумка)
    EconFactSink sink = nullptr;       // куда докладывать Consumed
    void* user = nullptr;
    Depot(Inventory& i) : inv(i) {}
    Depot(Inventory& i, std::int32_t* debt, EconFactSink s, void* u)
        : inv(i), needDebt(debt), sink(s), user(u) {}
};

// ── The day, in three pure steps ─────────────────────────────────────────

// Workers run the site's recipes in three passes: today's TABLE first (each
// consumed output staffed up to the town's daily demand, table order — bread
// can never be starved by a fair share), then FAIR SHARES of the remaining
// workers across recipes with inputs (the surplus), then leftovers in table
// order. Returns total units produced. Conservation: inputs leave the store
// as outputs enter.
// `mintFactionIdx`: whose coin FAMILY the kMintOutput recipe strikes — the
// town's faction (its three nominals run gold-first, each off its own metal;
// faction_coins resolves the free folk to the imperial family). -1 = this
// place has no mint and the row simply does not run.
// `needDebt` — счёт места (CANON S10): спрос, по которому ранжируются
// руки, читает ОСТАТОК ДОЛГА (season_demand_for); nullptr — фикстура без
// счёта, прямая часть спроса падает на лестницу населения.
int econ_produce_day(Inventory& store, const std::int32_t* needDebt,
                     const Skills& hands, int workers,
                     int population, EconFactSink sink, void* user,
                     int mintFactionIdx = -1);

struct ConsumeOutcome {
    int fedPop = 0;         // souls who lived through the boundary fed
    int starvedPop = 0;     // souls the unpaid hunger debt KILLED (caller
                            //   subtracts them from the population)
    int unmetComfort = 0;   // non-hunger debt left unpaid (gates growth)
    int comfortDemand = 0;  // total non-hunger season demand (unmet's scale)
    bool famineActive = false;
};

// ПОТРЕБЛЕНИЕ — ЭТО ДОЛГ, А НЕ СПИСАНИЕ (CANON S10, владелец 2026-09-19:
// «вот новый сезон — тебе новый долг… если не полностью погашен, то
// отнимается популяция не вся, а ПРОПОРЦИОНАЛЬНО долгу»). Границей сезона
// (season_boundary — единое окно S19.2) место проходит три шага:
//   1. ВЗЫСКАНИЕ прошлого счёта: непогашенный ХЛЕБ уходит населением
//      насмерть — по душе за каждый непокрытый душевой сезон (остаток /
//      kDaysPerSeason; хвост меньше душевого сезона прощается — зеркало
//      закона «кусок меньше сезона не кормит никого»). Смерть —
//      ЕДИНСТВЕННАЯ кара голода (вердикт 2026-09-19): выжившие сыты,
//      fedPop == population − starvedPop, и рост судит только комфорт.
//      Непогашенные ПРОЧИЕ строки гасят РОСТ через unmetComfort — от
//      нехватки ткани не умирают. Старый долг не переносится.
//   2. НОВЫЙ СЧЁТ: сезонная нужда лестницы по населению ПОСЛЕ смертей,
//      перезаписью в needDebt (товарный ординал — зеркало titheOwedGoods).
//   3. НЕМЕДЛЕННОЕ ГАШЕНИЕ из склада (econ_pay_debt) — посевной амбар и
//      прошлый излишек платят по счёту в ту же минуту.
// `famineWasActive` carries last season's state so FamineStarted/FamineEnded
// fire exactly on the transitions. Смерти применяет ВЫЗЫВАЮЩИЙ (дверь одна —
// settle_landmark_day); мёртвому месту счёт закрывается.
ConsumeOutcome econ_debt_boundary(Inventory& store, std::int32_t* needDebt,
                                  int population, bool famineWasActive,
                                  EconFactSink sink, void* user);

// ГАШЕНИЕ ДОЛГА — одна дверь «склад платит по счёту» (CANON S10: «всё, что
// падает в него, идёт в уплату долга»). Съеденное СПИСЫВАЕТСЯ со склада
// (факт Consumed) — после вызова на складе лежит только излишек. Зовётся
// границей (шаг 3 выше) и днём места как страховочный такт; порция Б
// проведёт её через двери прихода (haul/craft/transfer) — «сразу» без лага.
// Возвращает единиц погашено.
int econ_pay_debt(Inventory& store, std::int32_t* needDebt,
                  EconFactSink sink, void* user);

// Daily slot hygiene, split out of the old daily consume (CANON «Крафт/
// Скрап»): the store past half occupancy melts its cheapest non-fungible
// stacks back to matter. Reports a Scrapped fact when anything melted.
// Returns stacks melted.
int econ_store_hygiene(Inventory& store, EconFactSink sink, void* user);

// ── Population and mood (owner's law, W2b-4) ─────────────────────────────
//
// LOGISTIC growth, not flat heads per day: dP = r·P·(1−P/K)·drive, where
// drive ∈ [−1, +1] comes from continuous WELLBEING (fed fraction, softened
// by comfort shortfall).
//
// NO CARRYING CAP (CANON S25, the owner's word): «ни константы-потолка, ни
// ёмкости как крышки быть не должно». The ceiling EMERGES from supply — a
// town that outgrows its fields and its trade sees wellbeing fall and stops;
// famine turns it around. A town on a crossroads may outgrow one on black
// earth with no roads, and that is the right world. (The old kPopCarryingCap
// = 16384 was one lid for every town on the map — canon-audit III.6.)
//
// The rate is quoted PER SEASON (owner 2026-08-24): a month IS a season here
// — 32 days, the same epoch the forest grows by (kGrowthEpochDays) — and in
// a fantasy town of a thousand souls a birth is an event, not daily noise.
// The daily tick only ACCRUES the season rate into the fractional carry;
// whole people appear when the carry crosses one.
inline constexpr float kPopGrowthPerSeason = 1.0f / 8.0f;  // po2: +12.5%/season fully fed
inline constexpr float kPopGrowthRatePerDay =
    kPopGrowthPerSeason / float(kDaysPerSeason);

// Continuous wellbeing in [0, 1]: the fed fraction, softened by how much of
// the comfort ladder went unmet. 0.5 is the waterline — above it the town
// grows, below it shrinks.
inline float settlement_wellbeing(const ConsumeOutcome& o, int population) {
    if (population <= 0) return 0.5f;
    const float fedFrac = float(o.fedPop) / float(population);
    const float comfortFrac = o.comfortDemand > 0
        ? 1.0f - float(o.unmetComfort) / float(o.comfortDemand)
        : 1.0f;
    return fedFrac * (0.5f + 0.5f * comfortFrac);
}

inline float population_delta_per_day(int population, float wellbeing) {
    if (population <= 0) return 0.0f;
    const float drive = (wellbeing - 0.5f) * 2.0f;   // [-1, +1]
    // Growth and decline ride the same season-quoted rate; nothing damps
    // starvation and nothing caps plenty — supply is the only ceiling (S25).
    return kPopGrowthRatePerDay * float(population) * drive;
}

// Mood is the SAME wellbeing banded for the eye (0 = Prosperous …
// 4 = Revolt, matching SettlementMood's order) — context, not a second law.
inline int mood_band_from_wellbeing(float wellbeing) {
    if (wellbeing >= 0.8f) return 0;
    if (wellbeing >= 0.6f) return 1;
    if (wellbeing >= 0.4f) return 2;
    if (wellbeing >= 0.2f) return 3;
    return 4;
}


// ── Birth stocks ─────────────────────────────────────────────────────────

// A landmark is born MID-LIFE, not at hour zero (owner's ruling): worldgen
// seeds its universal Inventory as if it had been living for years, so the
// market has wares on day one and nobody starves while the first caravans
// find their legs. One law, deterministic from population:
//   · the daily-vital row (bread) holds kSeedVitalDays of the table — a
//     larder, not a warehouse;
//   · every other need row holds a stretch of its daily demand — a season
//     in a crafting City, days in a gathering Village;
//   · raw stocks are a production buffer per head — doubled in a Village,
//     whose whole business is raw.
//   · the treasury seeds as the faction's own three coins (change-making,
//     add_value_in_coins), from population ± a quarter's spread off the
//     world seed (`seedSalt` — world seed ⊕ the landmark's identity), so
//     twin towns are born organically unequal (CANON S10, посев капитала).
void seed_landmark_inventory(Inventory& inv, int population, bool isCity,
                             int factionIdx, std::uint32_t seedSalt);

} // namespace sm
