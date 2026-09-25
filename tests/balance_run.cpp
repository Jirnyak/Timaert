// balance_run — the world plays itself, headless (owner track 2026-08-30;
// CANON S10 «Баланс экономики выводится ДУБЛЬ-ПРОГОНОМ», work_vector §1
// «Методика баланса»). NOT a ctest: it is the measuring instrument of the
// economy track — it raises the SAME world the game boots (macro/world_gen.h,
// one baker, CANON S26), runs it at the live loop's own cadence (256 AI
// sweeps + the daily tick per game day — the 8192 frame steps of the live
// loop are a rendering artifact and are skipped), and writes per-day TSV:
//
//   <out>/world_<seed>.tsv     one row per day: totals + per-commodity flows
//   <out>/landmarks_<seed>.tsv one row per landmark per day: pop/wellbeing/stocks
//
// Usage: balance_run [seedsCsv] [days] [outDir]
//        defaults:    12345      256    balance_out
// Exit 1 = a world LAW broke (printed as [law] ... FAIL); 0 otherwise.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/time.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/commodity.h"
#include "macro/currency.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/faction.h"
#include "macro/items.h"
#include "macro/landmark_grid.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/nav_field.h"
#include "macro/npc_ai.h"
#include "macro/world_row.h"
#include "macro/pathfinding.h"
#include "macro/spawners.h"
#include "macro/state.h"
#include "macro/store.h"
#include "macro/tree_layer.h"
#include "macro/world_gen.h"
#include "macro/world_tick.h"
#include "macro/zones.h"

namespace {

// One game day of econ facts, folded flat. World-aggregate on purpose: the
// per-landmark cut comes from sampling gs.landmarks directly at day end;
// facts carry the FLOWS the stocks alone cannot show.
struct DayAccum {
    long long gathered[sm::kCommodityCount] = {};
    long long produced[sm::kCommodityCount] = {};
    long long consumed[sm::kCommodityCount] = {};
    int starvedPops = 0;
    long long mintedCoins = 0;
    // ── ВЕДОМОСТЬ СКЛАДА ДУШ (econ_day.h, CANON S9) ──────────────────────
    // Уровни (кто где стоит) прибор снимает сам вечерним свипом; ПОТОКИ к
    // вечеру не видны — место, выплатившее сто душ в артель, и место,
    // потерявшее сто душ, выглядят одинаково. Эти четыре величины и есть
    // разница между «мир умирает» и «мир переливается».
    long long soulsBorn = 0;
    int crewsUnfed = 0;          // артелей провалило окно по ХАРЧУ...
    long long soulsUnfed = 0;    // ...и сколько душ из них ушло
    int crewsUnpaid = 0;         // то же по ПЛАТЕ
    long long soulsUnpaid = 0;

    void reset() { *this = DayAccum{}; }
};

void econ_fact_sink(void* user, const sm::EconFact& f) {
    auto* a = static_cast<DayAccum*>(user);
    const bool hasCommodity =
        f.commodity >= 0 && f.commodity < sm::kCommodityCount;
    switch (f.kind) {
        case sm::EconFact::Kind::Gathered:
            if (hasCommodity) a->gathered[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Produced:
            if (hasCommodity) a->produced[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Consumed:
            if (hasCommodity) a->consumed[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Starved: a->starvedPops += f.amount; break;
        // amount = coins struck (commodity carries the INPUT silver row, so
        // this is its own counter, not a produced[] line).
        case sm::EconFact::Kind::Minted: a->mintedCoins += f.amount; break;
        // Slot hygiene, not a flow of goods: what the melt RETURNS is not
        // reported as Produced on purpose (entropy is a sink, not a source).
        case sm::EconFact::Kind::Scrapped: break;
        // Ведомость склада душ. Один факт дезертирства = одна артель,
        // поэтому здесь считаются ОБЕ величины: артели фактами, души суммой.
        case sm::EconFact::Kind::SoulsBorn:
            a->soulsBorn += f.amount;
            break;
        case sm::EconFact::Kind::SoulsDesertedUnfed:
            a->crewsUnfed += 1;
            a->soulsUnfed += f.amount;
            break;
        case sm::EconFact::Kind::SoulsDesertedUnpaid:
            a->crewsUnpaid += 1;
            a->soulsUnpaid += f.amount;
            break;
    }
}

// Every coin row of the faction registry's mint columns, counted by catalog
// ordinal × NOMINAL (its value column): with three nominals per realm
// (verdict №1, 2026-09-17) the money supply is the VALUE of the coin goods —
// a gold piece is a hundred coppers of supply, not one.
long long coins_in(const sm::Inventory& inv, const std::vector<int>& coinIdx) {
    long long total = 0;
    for (int idx : coinIdx) {
        const sm::ItemDef* def = sm::item_def_at(idx);
        total += (long long)inv.count_of(idx) * (def ? def->value : 1);
    }
    return total;
}

}  // namespace

int main(int argc, char** argv) {
    const char* seedsCsv = argc > 1 ? argv[1] : "12345";
    const int days = argc > 2 ? std::atoi(argv[2]) : 256;
    const std::string outDir = argc > 3 ? argv[3] : "balance_out";
    if (days <= 0) {
        std::fprintf(stderr, "balance_run: days must be positive\n");
        return 1;
    }

    std::vector<std::uint32_t> seeds;
    for (const char* p = seedsCsv; *p;) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(p, &end, 10);
        if (end == p) break;
        seeds.push_back(std::uint32_t(v));
        p = *end == ',' ? end + 1 : end;
    }
    if (seeds.empty()) {
        std::fprintf(stderr, "balance_run: no seeds in '%s'\n", seedsCsv);
        return 1;
    }

#ifdef _WIN32
    std::system(("mkdir \"" + outDir + "\" 2>NUL").c_str());
#else
    std::system(("mkdir -p '" + outDir + "'").c_str());
#endif

    // Coin catalog ordinals, resolved once (strings are authoring keys).
    // The coin rows are the faction registry's own mint columns (the
    // kCurrencyDefs list died 2026-09-18 with verdict №1) — dedup because
    // culture groups fold onto their realm's family.
    std::vector<int> coinIdx;
    for (const auto& f : sm::kFactionDefs) {
        for (const char* c : f.mint) {
            if (!c || !c[0]) continue;
            const int idx = sm::item_index(c);
            if (idx >= 0
                && std::find(coinIdx.begin(), coinIdx.end(), idx)
                       == coinIdx.end()) {
                coinIdx.push_back(idx);
            }
        }
    }

    bool lawsHold = true;

    for (const std::uint32_t seed : seeds) {
        const auto t0 = std::chrono::steady_clock::now();

        // ── Raise THE world (same baker as the game boot) ────────────────
        sm::GameState gs;
        sm::TerrainData terrain{};
        std::vector<sm::TreePoint> trees;
        sm::TreeLayer treeLayer;
        sm::DepositLayer deposits;
        sm::FeatureLayer features;
        sm::ZoneLayer zones;
        sm::TreeGrid treeGrid;
        sm::LandmarkGrid landmarkGrid;
        sm::PathCostData pathCost;
        sm::ecs::World ecs;
        auto macroStore = sm::make_macro_store();
        sm::store_attach(ecs, macroStore.get());

        sm::WorldGenParams gp{};
        gp.seed = seed;
        sm::WorldGenOut go{};
        go.gs = &gs;
        go.terrain = &terrain;
        go.trees = &trees;
        go.treeLayer = &treeLayer;
        go.deposits = &deposits;
        go.features = &features;
        go.zones = &zones;
        go.treeGrid = &treeGrid;
        go.landmarkGrid = &landmarkGrid;
        go.pathCost = &pathCost;
        go.world = &ecs;
        go.store = macroStore.get();
        sm::generate_macro_world(go, gp);

        // Muster law: nobody is BORN at sea.
        {
            int atSea = 0;
            for (auto [e, kind, p]
                 : ecs.reg.view<sm::ecs::NPCKind, sm::ecs::Position>()
                       .each()) {
                (void)e; (void)kind;
                const int wx = ((int(p.x) % gs.mapW) + gs.mapW) % gs.mapW;
                const int wy = ((int(p.y) % gs.mapH) + gs.mapH) % gs.mapH;
                if (!pathCost.water.empty()
                    && pathCost.water[std::size_t(wy) * gs.mapW + wx])
                    ++atSea;
            }
            std::fprintf(stderr, "[muster] spawned at sea: %d\n", atSea);
        }

        sm::MacroNpcAiRuntime ai;
        sm::reset_macro_npc_ai_runtime(ai, seed);

        DayAccum accum;
        sm::NavWorld nav;        // запечённая навигация (CANON S7), derived
        sm::MacroWorld mw{};
        mw.gs = &gs;
        mw.nav = &nav;
        mw.trees = &treeLayer;
        mw.world = &ecs;
        mw.store = macroStore.get();
        mw.terrain = &terrain;
        mw.deposits = &deposits;
        mw.features = &features;
        mw.zones = &zones;
        mw.pathCost = &pathCost;
        mw.treeGrid = &treeGrid;
        mw.landmarks = &landmarkGrid;
        mw.econFacts = &econ_fact_sink;
        mw.econFactsUser = &accum;

        // ── TSV writers ──────────────────────────────────────────────────
        char path[512];
        std::snprintf(path, sizeof path, "%s/world_%u.tsv", outDir.c_str(),
                      seed);
        FILE* fw = std::fopen(path, "w");
        std::snprintf(path, sizeof path, "%s/landmarks_%u.tsv",
                      outDir.c_str(), seed);
        FILE* fl = std::fopen(path, "w");
        if (!fw || !fl) {
            std::fprintf(stderr, "balance_run: cannot open outputs in %s\n",
                         outDir.c_str());
            return 1;
        }
        // ЗОНД РАСПРЕДЕЛЕНИЯ ПОРУЧЕНИЙ (2026-09-19): куда рулетка дела
        // артелей — счёт живых крестьянских крю по глаголу поручения, плюс
        // пул дезертиров (кровь закона 1/8). Ответ на «почему зерно не
        // едет»: мало рейсов или пустые сделки.
        std::fprintf(fw, "day\tpop\tcoinLandmarks\tcoinSquads\tcoinLootPool"
                         "\tstarvedPops\tminted\ttrades\ttradedValue\tfoodHolds"
                         "\tcrewsGather\tcrewsSell\tcrewsOther\tdeserters"
                         // ЛОШАДЬ-ЮНИТ (2026-09-19): свидетель контура —
                         // табуны в гарнизонах и спины в отрядах, миром.
                         "\thorsesGarr\thorsesSquads\tpastures"
                         // ПАРЦЕЛЛЫ — свидетель ЗАСЕЯННОЙ ЗЕМЛИ (M-96).
                         // Матушка-земля одна: поле фертильности не знает,
                         // кто на нём стоит, а фича сверху говорит, что с
                         // клетки снимают. Поэтому парцелла — ОДНО число:
                         // хлеб и лён качают одно поле (kGathererDefs
                         // Worksite::HomeField / HomeFlaxField). Читает
                         // разбор еды: «еда просела — пахать стало негде»
                         // отличается от «еда просела — некому пахать»
                         // ТОЛЬКО этой колонкой.
                         "\tparcels"
                         // ── БАЛАНС ДУШ МИРА (CANON S9, владелец
                         // 2026-09-21) ──────────────────────────────────
                         // Население — РЕСУРС места, которым оно платит за
                         // артель; значит «мир умирает» и «мир переливает
                         // осёдлых в вооружённых» — разные события, и
                         // различает их только полный счёт по контейнерам.
                         // Контейнеров пять: склады душ мест (по родам),
                         // сквады с домом, сквады без дома (банды),
                         // гарнизоны, пул дезертиров. soulsWorld — их
                         // сумма, и падать она может ТОЛЬКО от смерти.
                         "\tpopCity\tpopVil\tpopLair\tpopElse"
                         "\tsoulsHomed\tsoulsFree\tsoulsGarr\tsoulsPool"
                         "\tsoulsWorld"
                         // Потоки дня (ведомость склада душ, econ_day.h).
                         "\tsoulsBorn\tcrewsUnfed\tsoulsUnfed"
                         "\tcrewsUnpaid\tsoulsUnpaid");
        for (int c = 0; c < sm::kCommodityCount; ++c) {
            const char* id = sm::kCommodities[c].id;
            std::fprintf(fw, "\t%s_stock\t%s_gathered\t%s_produced"
                             "\t%s_consumed\t%s_city\t%s_vil",
                         id, id, id, id, id, id);
        }
        std::fprintf(fw, "\n");
        // ТРИ ПОТОКА МАТЕРИИ, ПО МЕСТАМ (CANON S10, 2026-09-20): гипотеза о
        // расщеплении горизонта — про МЕДИАНЫ по местам (еда топит пол
        // кривой, а рукотворные строки голодают у потолка), и мировой
        // агрегат медианы показать не может. По строке на поток: food =
        // ЕДА, cloth = БЛАГА, iron = РЕСУРС.
        //
        // ДВЕ КОЛОНКИ ДОЛГА ВМЕСТО ОДНОГО ЛИТЕРАЛЬНОГО НУЛЯ (починка прибора
        // 2026-09-21). Здесь печаталось `unmet` константой 0, а `bread` и
        // `grain` были ОДНИМ И ТЕМ ЖЕ ординалом — наследие сноса хлеба,
        // пережившее его на день. Прибор, который печатает одну величину
        // дважды под разными именами и назначенный ноль под третьим, не
        // меряет, а успокаивает: все числа сессий 4-5 про «хлеб в городах»
        // и «зерно» сняты с этих колонок. Долг — та самая величина, которой
        // МЕРЯЕТСЯ нужда с 2026-09-19 (CANON S10, потребление = долг):
        // голодный счёт хранится в житель-днях (popPerUnitDay == 1), и
        // непогашенный остаток на границе И ЕСТЬ то, чем место умрёт.
        std::fprintf(fl, "day\tid\ttype\tpop\twellbeing\tstarved"
                         "\tdebtFood\tdebtComfort\tfood\tcloth\tiron\tcoin\n");

        // Голодная строка — ДВЕРЬ (econ_day.h hunger_item_*), не литерал:
        // ровно та причина, по которой дверь и заведена — переименуй строку
        // лестницы, и литерал в приборе замолчит, не сломавшись.
        const int foodIdx = sm::hunger_item_index();
        const int clothIdx = sm::item_index("cloth");
        const int ironIdx = sm::item_index("iron");
        // needDebt индексируется ТОВАРНЫМ ординалом (state.h) — берём его у
        // той же таблицы лестницы, которую читает econ_debt_boundary, а не
        // переписываем её здесь второй копией (закон тестов §5).
        const int hungerOrd = sm::commodity_index(sm::hunger_item_id());
        int comfortOrd[sm::kNeedCount] = {};
        int comfortOrdCount = 0;
        for (int i = 0; i < sm::kNeedCount; ++i) {
            if (i == sm::kHungerNeedRow) continue;
            const int ord = sm::commodity_index(sm::kNeeds[i].commodity);
            if (ord >= 0) comfortOrd[comfortOrdCount++] = ord;
        }
        std::uint32_t ringCursor = gs.chronicle.nextSeq;

        // ── The days: the live loop's cadence without its frames ─────────
        // One game day = kTicksPerDay/kAiTicks = 256 iterations of
        // [advance 32 ticks → one AI sweep], daily tick at the rollover —
        // exactly the order the app's frame runs them (main.cpp).
        constexpr int kSweepsPerDay = int(sm::kTicksPerDay / sm::kAiTicks);
        for (int d = 0; d < days; ++d) {
            accum.reset();
            for (int s = 0; s < kSweepsPerDay; ++s) {
                sm::tick_world(gs, gs.worldTickRt, sm::kAiTicks,
                               /*max_daily_ticks=*/32, &mw);
                sm::tick_macro_npc_ai(mw, ai, sm::kAiTicks,
                                      /*allowAutoBattle=*/true);
            }

            // Day-end sampling: stocks from the landmarks, flows from facts.
            long long popTotal = 0, coinLm = 0;
            // Склады душ мест по родам. Логово выделено отдельной колонкой
            // не для красоты: по вердикту владельца «банды это население
            // ландмарка логово бандитов» — значит колонка обязана быть
            // ненулевой, а сегодня она ноль, и это измеряемая недостройка.
            long long popCity = 0, popVil = 0, popLair = 0, popElse = 0;
            long long stock[sm::kCommodityCount] = {};
            long long stockCity[sm::kCommodityCount] = {};
            long long stockVil[sm::kCommodityCount] = {};
            for (const auto& lm : gs.landmarks) {
                popTotal += lm.population;
                switch (lm.type) {
                    case sm::LandmarkType::City: popCity += lm.population;
                        break;
                    case sm::LandmarkType::Village: popVil += lm.population;
                        break;
                    case sm::LandmarkType::Lair: popLair += lm.population;
                        break;
                    default: popElse += lm.population; break;
                }
                coinLm += coins_in(lm.inventory, coinIdx);
                for (int c = 0; c < sm::kCommodityCount; ++c) {
                    const long long n = lm.inventory.count_of(
                        sm::commodity_item_index(c));
                    stock[c] += n;
                    if (lm.type == sm::LandmarkType::City) stockCity[c] += n;
                    else if (lm.type == sm::LandmarkType::Village)
                        stockVil[c] += n;
                }
                const bool settled = lm.type == sm::LandmarkType::City
                                  || lm.type == sm::LandmarkType::Village;
                if (settled) {
                    long long debtComfort = 0;
                    for (int k = 0; k < comfortOrdCount; ++k)
                        debtComfort += lm.needDebt[comfortOrd[k]];
                    std::fprintf(fl,
                                 "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d"
                                 "\t%d\t%lld\n",
                                 gs.worldTime.day(), lm.id, int(lm.type),
                                 lm.population, int(lm.seasonWellbeing),
                                 int(lm.starvedYesterday),
                                 hungerOrd >= 0 ? lm.needDebt[hungerOrd] : 0,
                                 debtComfort,
                                 lm.inventory.count_of(foodIdx),
                                 lm.inventory.count_of(clothIdx),
                                 lm.inventory.count_of(ironIdx),
                                 coins_in(lm.inventory, coinIdx));
                }
            }
            long long coinSquads = 0, foodHolds = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kMacroEntityCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& bag = macroStore->inventory[slot];
                coinSquads += coins_in(bag.inv, coinIdx);
                foodHolds += bag.inv.count_of(foodIdx);
            }
            long long horsesGarr = 0, soulsGarr = 0;
            for (const sm::Landmark& lm : gs.landmarks) {
                horsesGarr += sm::creature_heads_of(
                    lm.inventory, sm::NPCType::Horse);
                // Гарнизон — область существ ЕДИНОГО контейнера места
                // (M-71). Считаются ЛЮДИ: табун у места свой столбец, и
                // душой населения лошадь не была никогда.
                soulsGarr += sm::count_human_souls(lm.inventory);
            }
            long long horsesSquads = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kMacroEntityCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                horsesSquads += sm::creature_heads_of(
                    macroStore->inventory[slot].inv, sm::NPCType::Horse);
            }
            // ДУШИ В СКВАДАХ, разделённые ПО АДРЕСУ ДОМА. Лидер — такая же
            // душа, как любая в ростере (CANON S4: «одиночка = лидер с
            // пустым ростером»), поэтому он +1, а не особый случай.
            // Бездомный сквад — это банда: выплаченная кем-то душа, которую
            // не ждёт ни один склад. По вердикту владельца такого состояния
            // быть не должно вовсе («банды это население ландмарка логово
            // бандитов»), и эта колонка меряет, сколько мира сейчас живёт
            // мимо закона.
            long long soulsHomed = 0, soulsFree = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kMacroEntityCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& kind = macroStore->kind[slot];
                const auto& rt = macroStore->runtime[slot];
                long long souls = sm::is_folk_kind(kind.type) ? 1 : 0;
                souls += sm::count_human_souls(
                    macroStore->inventory[slot].inv);
                if (souls <= 0) continue;
                if (sm::landmark_by_id(gs, rt.homeSettlementId) != nullptr)
                    soulsHomed += souls;
                else
                    soulsFree += souls;
            }
            const long long soulsPool =
                sm::count_human_souls(gs.deserterPool);
            long long pastures = 0, parcels = 0;
            if (!features.data.empty()) {
                for (const std::uint8_t f : features.data) {
                    if (f == sm::FT_Pasture) ++pastures;
                    else if (f == sm::FT_Field || f == sm::FT_FlaxField)
                        ++parcels;
                }
            }
            int crewsGather = 0, crewsSell = 0, crewsOther = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kMacroEntityCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& kind = macroStore->kind[slot];
                const auto& crt = macroStore->runtime[slot];
                if (kind.type != std::uint16_t(sm::NPCType::Peasant))
                    continue;
                if (crt.squadType == std::uint8_t(sm::SquadType::Artel))
                    ++crewsGather;
                else if (crt.squadType
                         == std::uint8_t(sm::SquadType::Caravan))
                    ++crewsSell;
                else
                    ++crewsOther;
            }
            // The day's DEALS, read off the chronicle ring by sequence — the
            // same shop window the witcher asks (S20.1): every Traded fact
            // since yesterday's cursor.
            long long trades = 0, tradedValue = 0;
            for (const sm::WorldFact& f : gs.chronicle.ring) {
                if (f.seq < ringCursor || f.seq == 0) continue;
                if (f.kind != std::uint16_t(sm::FactKind::Traded)) continue;
                trades += 1;
                tradedValue += f.amount;
            }
            ringCursor = gs.chronicle.nextSeq;

            std::fprintf(fw,
                         "%d\t%lld\t%lld\t%lld\t%lld\t%d\t%lld\t%lld"
                         "\t%lld\t%lld\t%d\t%d\t%d\t%d",
                         gs.worldTime.day(), popTotal, coinLm, coinSquads,
                         (long long)gs.lootPoolValue,
                         accum.starvedPops,
                         accum.mintedCoins, trades, tradedValue, foodHolds,
                         crewsGather, crewsSell, crewsOther,
                         int(sm::creature_heads(gs.deserterPool)));
            std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld",
                         horsesGarr, horsesSquads, pastures, parcels);
            // Баланс душ мира. soulsWorld печатается суммой, а не считается
            // читателем TSV, ровно по той же причине, по какой прибор вообще
            // существует: величина, которую каждый читатель складывает сам,
            // рано или поздно складывается по-разному.
            std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld"
                             "\t%lld\t%lld\t%lld\t%lld\t%lld"
                             "\t%lld\t%d\t%lld\t%d\t%lld",
                         popCity, popVil, popLair, popElse,
                         soulsHomed, soulsFree, soulsGarr, soulsPool,
                         popTotal + soulsHomed + soulsFree + soulsGarr
                             + soulsPool,
                         accum.soulsBorn, accum.crewsUnfed, accum.soulsUnfed,
                         accum.crewsUnpaid, accum.soulsUnpaid);
            for (int c = 0; c < sm::kCommodityCount; ++c) {
                std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld",
                             stock[c], accum.gathered[c], accum.produced[c],
                             accum.consumed[c], stockCity[c], stockVil[c]);
            }
            std::fprintf(fw, "\n");
        }

        {
            int atSea = 0;
            for (auto [e, kind, p]
                 : ecs.reg.view<sm::ecs::NPCKind, sm::ecs::Position>()
                       .each()) {
                (void)e; (void)kind;
                const int wx = ((int(p.x) % gs.mapW) + gs.mapW) % gs.mapW;
                const int wy = ((int(p.y) % gs.mapH) + gs.mapH) % gs.mapH;
                if (!pathCost.water.empty()
                    && pathCost.water[std::size_t(wy) * gs.mapW + wx])
                    ++atSea;
            }
            std::fprintf(stderr, "[muster] AT SEA at run end: %d\n", atSea);
        }
        // Closing muster: the trade fleet is this track's working part, and
        // its health must be readable without a debugger.
        {
            int caravans = 0, vendors = 0, vIdle = 0, vAway = 0;
            long long vendorLoad = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kMacroEntityCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& crt = macroStore->runtime[slot];
                const auto& bag = macroStore->inventory[slot];
                // Рейс сбыта — поручение крестьян (аукцион, CANON S10):
                // «вендор» смотра = артель с errand=Sell, тип умер.
                if (crt.squadType == std::uint8_t(sm::SquadType::Caravan)) {
                    ++vendors;
                    if (crt.state == 0) ++vIdle; else ++vAway;
                    vendorLoad += (long long)sm::inventory_weight(bag.inv);
                    continue;
                }
                // ЗОНД КАРАВАНОВ СНЯТ 2026-09-21: род NPCType::Merchant
                // снесён — караван был сквадом, притворившимся видом.

            }
            std::fprintf(stderr,
                         "[balance] caravans=%d vendors=%d (idle=%d away=%d "
                         "load=%lldkg)\n",
                         caravans, vendors, vIdle, vAway, vendorLoad);
        }

        // ── Laws (the invariants of work_vector §1, v0 set) ──────────────
        // Assert relations, not retuned literals: the world must still be
        // inhabited after the run — a dead world is a broken economy, not a
        // tuning choice. Finer laws (conservation to the unit, price
        // corridors) land with the mechanics that make them checkable.
        long long popEnd = 0;
        int alive = 0;
        for (const auto& lm : gs.landmarks) {
            if (lm.type != sm::LandmarkType::City
                && lm.type != sm::LandmarkType::Village) continue;
            popEnd += lm.population;
            alive += lm.population > 0 ? 1 : 0;
        }
        const bool populated = popEnd > 0 && alive > 0;
        lawsHold = lawsHold && populated;

        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now()
                                          - t0).count();
        std::fprintf(stderr,
                     "[balance] seed=%u days=%d pop=%lld aliveSettlements=%d "
                     "secs=%.1f\n",
                     seed, days, popEnd, alive, secs);
        std::fprintf(stderr, "[law] world still populated: %s\n",
                     populated ? "OK" : "FAIL");
        std::fclose(fw);
        std::fclose(fl);
    }

    return lawsHold ? 0 : 1;
}
