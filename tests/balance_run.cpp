// balance_run — the world plays itself, headless (owner track 2026-08-30;
// CANON S10 «Баланс экономики выводится ДУБЛЬ-ПРОГОНОМ», work_vector §1
// «Методика баланса»). NOT a ctest: it is the measuring instrument of the
// economy track — it raises the SAME world the game boots (macro/world_gen.h,
// one baker, CANON S26), runs it at the live loop's own cadence (256 AI
// sweeps + the daily tick per game day — the 8192 frame steps of the live
// loop are a rendering artifact and are skipped), and writes per-day TSV:
//
//   <out>/world_<seed>.tsv     one row per day: totals + per-commodity flows
//   <out>/landmarks_<seed>.tsv one row per landmark per day: pop/mood/stocks
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
#include "macro/items.h"
#include "macro/landmark_grid.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/pathfinding.h"
#include "macro/spawners.h"
#include "macro/state.h"
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
    int famineStarts = 0;
    int starvedPops = 0;

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
        case sm::EconFact::Kind::FamineStarted: a->famineStarts += 1; break;
        case sm::EconFact::Kind::Starved: a->starvedPops += f.amount; break;
        case sm::EconFact::Kind::FamineEnded: break;
    }
}

// Every coin row of the currency registry, counted by catalog ordinal — the
// money supply is a COUNT of coin goods (CANON S10: монета — товар), never a
// valuation.
long long coins_in(const sm::Inventory& inv, const std::vector<int>& coinIdx) {
    long long total = 0;
    for (int idx : coinIdx) total += inv.count_of(idx);
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
    std::vector<int> coinIdx;
    for (const auto& c : sm::kCurrencyDefs) {
        const int idx = sm::item_index(c.itemId);
        if (idx >= 0) coinIdx.push_back(idx);
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
        sm::MacroWorld mw{};
        mw.gs = &gs;
        mw.trees = &treeLayer;
        mw.world = &ecs;
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
        std::fprintf(fw, "day\tpop\tcoinLandmarks\tcoinSquads\tcoinLootPool\tfamineStarts"
                         "\tstarvedPops\ttrades\ttradedValue\tgrainHolds");
        for (int c = 0; c < sm::kCommodityCount; ++c) {
            const char* id = sm::kCommodities[c].id;
            std::fprintf(fw, "\t%s_stock\t%s_gathered\t%s_produced"
                             "\t%s_consumed\t%s_city\t%s_vil",
                         id, id, id, id, id, id);
        }
        std::fprintf(fw, "\n");
        std::fprintf(fl, "day\tid\ttype\tpop\tmood\tstarved\tunmet\tbread"
                         "\tgrain\tcoin\n");

        const int breadIdx = sm::item_index("bread");
        const int grainIdx = sm::item_index("grain");
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
            long long stock[sm::kCommodityCount] = {};
            long long stockCity[sm::kCommodityCount] = {};
            long long stockVil[sm::kCommodityCount] = {};
            for (const auto& lm : gs.landmarks) {
                popTotal += lm.population;
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
                    std::fprintf(fl,
                                 "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\n",
                                 gs.worldTime.day(), lm.id, int(lm.type),
                                 lm.population, int(lm.mood),
                                 int(lm.starvedYesterday),
                                 int(lm.unmetYesterday),
                                 lm.inventory.count_of(breadIdx),
                                 lm.inventory.count_of(grainIdx),
                                 coins_in(lm.inventory, coinIdx));
                }
            }
            long long coinSquads = 0, grainHolds = 0;
            for (auto [e, bag]
                 : ecs.reg.view<sm::ecs::NpcInventory>().each()) {
                (void)e;
                coinSquads += coins_in(bag.inv, coinIdx);
                grainHolds += bag.inv.count(grainIdx >= 0 ? "grain" : "");
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
                         "%d\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%lld\t%lld"
                         "\t%lld",
                         gs.worldTime.day(), popTotal, coinLm, coinSquads,
                         (long long)gs.lootPoolValue,
                         accum.famineStarts, accum.starvedPops,
                         trades, tradedValue, grainHolds);
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
            for (auto [e, kind, crt, bag]
                 : ecs.reg.view<sm::ecs::NPCKind, sm::ecs::MacroNpcRuntime,
                                sm::ecs::NpcInventory>().each()) {
                (void)e;
                if (kind.type == std::uint16_t(sm::NPCType::Vendor)) {
                    ++vendors;
                    if (crt.state == 0) ++vIdle; else ++vAway;
                    vendorLoad += (long long)sm::inventory_weight(bag.inv);
                    continue;
                }
                if (kind.type != std::uint16_t(sm::NPCType::Caravan))
                    continue;
                ++caravans;
                std::fprintf(stderr,
                             "[caravan] home=%d state=%d sp=%d/%d cap=%.0f "
                             "load=%.0f coin=%lld grain=%d wood=%d clay=%d\n",
                             crt.homeSettlementId, int(crt.state),
                             int(crt.sp), int(crt.maxSp), crt.carryCap,
                             sm::inventory_weight(bag.inv),
                             coins_in(bag.inv, coinIdx),
                             bag.inv.count("grain"), bag.inv.count("wood"),
                             bag.inv.count("clay"));
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
