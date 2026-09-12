#include "macro/econ_day.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace sm {

namespace {

// Recipe/need ids resolve once, lazily, into index tables — hot loops then
// touch integers only (the faction-registry pattern).
struct ResolvedRecipe {
    int output = -1;
    // The mint row (econ_day.h kMintOutput): output resolves to the town's
    // faction coin at run time. NOTHING else about it is special (owner
    // verdict 2026-09-12, «единая система крафта; города производят как
    // экономика-ИИ, используя ту же систему»): its matter is the coin row's
    // own composition and the day makes it through the same craft door as
    // every other output — so no matter is stored here at all.
    bool isMint = false;
    // popPerUnitDay of the need row this output serves, 0 = not a need —
    // production plans "today's table" against this before any surplus.
    int demandDivisor = 0;
};

struct ResolvedTables {
    ResolvedRecipe recipes[kRecipeCount];
    int needIdx[kNeedCount];
};

const ResolvedTables& resolved() {
    static const ResolvedTables t = [] {
        ResolvedTables r{};
        for (int i = 0; i < kRecipeCount; ++i) {
            ResolvedRecipe& rr = r.recipes[i];
            rr.output = commodity_index(kRecipes[i].output);
            rr.isMint = std::strcmp(kRecipes[i].output, kMintOutput) == 0;
            for (int n = 0; n < kNeedCount; ++n) {
                if (std::strcmp(kNeeds[n].commodity, kRecipes[i].output) == 0) {
                    rr.demandDivisor = kNeeds[n].popPerUnitDay;
                    break;
                }
            }
        }
        for (int i = 0; i < kNeedCount; ++i) {
            r.needIdx[i] = commodity_index(kNeeds[i].commodity);
        }
        return r;
    }();
    return t;
}

void report(EconFactSink sink, void* user, EconFact::Kind kind,
            int commodity, int amount) {
    if (!sink || amount == 0) return;
    EconFact f{};
    f.kind = kind;
    f.commodity = commodity;
    f.amount = amount;
    sink(user, f);
}

} // namespace

int econ_produce_day(Inventory& store, EconSite site, int workers,
                     int population, EconFactSink sink, void* user,
                     const char* mintCurrencyId) {
    if (workers <= 0) return 0;
    const ResolvedTables& t = resolved();
    int total = 0;
    // v1 scheduler, three passes over the table. A recipe used to staff
    // itself against the ENTIRE stockpile — bread claimed ceil(grainStock/8)
    // workers and the whole town baked while bricks, cloth and tools never
    // saw a worker-day. Now:
    //   pass 0 — TODAY'S TABLE: each recipe whose output the town CONSUMES
    //            (a needs-ladder row) staffs up to today's demand, in table
    //            order — bread first by construction, never starved by a
    //            fair share;
    //   pass 1 — FAIR SHARES: the remaining workers split evenly across the
    //            recipes that still have inputs — the surplus/exports;
    //   pass 2 — LEFTOVERS flow in table order.
    int workersLeft = workers;
    int fairShare = 0;

    // The OUTPUT catalog row a recipe makes: the table's own commodity for
    // goods, THIS town's coin for the mint — the one datum the mint right
    // supplies. From that row on, everything (matter, yield, the making
    // itself) is THE craft system, verbatim.
    const auto output_row_of = [&](const ResolvedRecipe& rr) {
        if (rr.isMint) {
            return mintCurrencyId ? item_index(mintCurrencyId) : -1;
        }
        return commodity_item_index(rr.output);
    };

    auto run_recipe = [&](int i, int unitCap, int workerCap) {
        const ResolvedRecipe& rr = t.recipes[i];
        // -1 = a mint with no coin named (no right here) or a dead id —
        // fail closed like every unwired layer.
        const int outIdx = output_row_of(rr);
        if (outIdx < 0) return;
        const auto parts = item_parts(outIdx);
        const int yield = item_yield(outIdx);
        if (parts.empty()) return;   // no composition: nothing from nothing
        // BATCHES this recipe could run from the store alone. Parts are
        // catalog ordinals — the store is asked directly. unitCap arrives in
        // OUTPUT units (today's demand); one batch makes `yield` of them.
        int byInputs = unitCap >= (1 << 20)
            ? (1 << 20) : (unitCap + yield - 1) / yield;
        for (const ItemPart& part : parts) {
            byInputs = std::min(byInputs, store.count_of(int(part.def))
                                              / int(part.count));
        }
        if (byInputs <= 0) return;
        // THE population-efficiency law (owner 2026-08-30, CANON S10, ?31
        // closed): КПД = log2(популяции)/4 — «город вдвое больше работает
        // на четверть лучше». Integer log2 (bit width); a born village
        // (the owner's-scale hundred, kVillageBornBase) lands at ×1½,
        // a 512-soul city at ×2¼. One law prices why the city bakes
        // better — never a second recipe row, never a site wall. The
        // quarter is a balance-run tunable. Tempo = the OUTPUT ROW'S OWN
        // labour column (items.h item_labour — the same number the hand's
        // SP price divides by), in BATCHES: for the mint that is metal
        // units a day, exactly the old «4 металла».
        const int popLog =
            population > 1 ? (std::bit_width(unsigned(population)) - 1) : 0;
        const int perDay =
            std::max(1, item_labour(outIdx) * popLog / 4);
        int wanted = (byInputs + perDay - 1) / perDay;
        wanted = std::min(wanted, workerCap);
        const int staffed = std::min(wanted, workersLeft);
        const int made = std::min(byInputs, staffed * perDay);
        if (made <= 0) return;
        // THE craft door — the town makes goods and strikes coin exactly as
        // a hand at the bench does (owner 2026-09-12: «город делает монеты
        // через систему крафта по своему ИИ»). All-or-nothing lives in the
        // door: a full store is a day that did not happen, the inputs never
        // left, and no fact lies about goods that do not exist. (The hand
        // written debit-refund pair that stood here — and once double-debited
        // the mint's silver — died with it.)
        if (!craft_item(store, outIdx, made)) return;
        workersLeft -= staffed;
        total += made;
        if (rr.isMint) {
            // The fact names the METAL spent (part 0's commodity row) — the
            // money-supply counter balance_run watches.
            const ItemDef* metal = item_def_at(int(parts[0].def));
            report(sink, user, EconFact::Kind::Minted,
                   metal ? commodity_index(metal->id) : -1, made * yield);
        } else {
            report(sink, user, EconFact::Kind::Produced, rr.output,
                   made * yield);
        }
    };

    // Pass 0 — today's table, by demand.
    for (int i = 0; i < kRecipeCount && workersLeft > 0; ++i) {
        if (!recipe_runs_at(kRecipes[i].site, site)) continue;
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.output < 0 || rr.demandDivisor <= 0) continue;
        const int demand = population / rr.demandDivisor;
        const int have = store.count_of(commodity_item_index(rr.output));
        if (demand <= have) continue;   // yesterday's surplus covers today
        run_recipe(i, demand - have, workersLeft);
    }

    // Fair shares are computed AFTER the table is served, over the recipes
    // that still have inputs (one whole batch feedable).
    int liveRecipes = 0;
    for (int i = 0; i < kRecipeCount; ++i) {
        if (!recipe_runs_at(kRecipes[i].site, site)) continue;
        const int outIdx = output_row_of(t.recipes[i]);
        if (outIdx < 0) continue;
        bool feedable = true;
        for (const ItemPart& part : item_parts(outIdx)) {
            feedable = feedable
                && store.count_of(int(part.def)) >= int(part.count);
        }
        if (feedable) ++liveRecipes;
    }
    fairShare = liveRecipes > 0
        ? (workersLeft + liveRecipes - 1) / liveRecipes : 0;

    // Pass 1 — fair shares; pass 2 — leftovers in table order.
    for (int pass = 1; pass <= 2 && workersLeft > 0; ++pass) {
        for (int i = 0; i < kRecipeCount && workersLeft > 0; ++i) {
            if (!recipe_runs_at(kRecipes[i].site, site)) continue;
            if (t.recipes[i].output < 0 && !t.recipes[i].isMint) continue;
            run_recipe(i, 1 << 30, pass == 1 ? fairShare : workersLeft);
        }
    }
    return total;
}

ConsumeOutcome econ_consume_day(Inventory& store, int population,
                                bool famineWasActive,
                                EconFactSink sink, void* user) {
    ConsumeOutcome out{};
    if (population <= 0) {
        out.famineActive = false;
        if (famineWasActive) {
            report(sink, user, EconFact::Kind::FamineEnded, -1, 1);
        }
        return out;
    }
    const ResolvedTables& t = resolved();
    // A person is fed only if EVERY daily-vital need was met — with one bread
    // row this is the old number, with a second daily food it is the min, not
    // whichever row happened to run last (the overwrite bug).
    int fed = population;
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = t.needIdx[i];
        if (idx < 0) continue;
        const int demand = population / kNeeds[i].popPerUnitDay;
        if (demand <= 0) continue;
        const int got = std::min(demand,
                                 store.count_of(commodity_item_index(idx)));
        store.remove_of(commodity_item_index(idx), got);
        if (got > 0) {
            report(sink, user, EconFact::Kind::Consumed, idx, got);
        }
        const bool vital = kCommodities[idx].tier == CommodityTier::Vital;
        if (kNeeds[i].popPerUnitDay == 1 && vital) {
            // The hunger row: shortfall is people unfed today.
            fed = std::min(fed, got);
        } else {
            // EVERY other shortfall is counted — vital maintenance (cloth,
            // bricks) included. The old `!vital` guard sent exactly those
            // two rows' deficits into the void: neither hunger nor unmet.
            out.unmetComfort += demand - got;
            out.comfortDemand += demand;
        }
    }
    // Slot hygiene rides the same daily tick (CANON «Крафт/Скрап»: авто-скрап
    // ИИ по порогу >50%): a store clogged past the half mark by non-fungible
    // lut melts its cheapest pieces back to matter, so bread and ore always
    // have somewhere to land. Plain stacks are never touched, and the
    // PLAYER'S bag never passes through this function at all.
    report(sink, user, EconFact::Kind::Scrapped, -1,
           auto_scrap_overflow(store));

    out.fedPop = fed;
    out.starvedPop = population - fed;
    out.famineActive = out.starvedPop > 0;
    if (out.starvedPop > 0) {
        report(sink, user, EconFact::Kind::Starved, -1, out.starvedPop);
    }
    if (out.famineActive && !famineWasActive) {
        report(sink, user, EconFact::Kind::FamineStarted, -1, out.starvedPop);
    } else if (!out.famineActive && famineWasActive) {
        report(sink, user, EconFact::Kind::FamineEnded, -1, 1);
    }
    return out;
}

// A commodity's CATALOG ordinal. The two id spaces are one now; this is the
// bridge between the economy's own row order and the catalog's, resolved once
// per process instead of a string lookup per access.
int commodity_item_index(int commodityIdx) {
    static const std::array<int, std::size_t(kCommodityCount)> kMap = [] {
        std::array<int, std::size_t(kCommodityCount)> m{};
        for (int i = 0; i < kCommodityCount; ++i) {
            m[std::size_t(i)] = item_index(kCommodities[i].id);
        }
        return m;
    }();
    return (commodityIdx >= 0 && commodityIdx < kCommodityCount)
        ? kMap[std::size_t(commodityIdx)] : -1;
}

void seed_landmark_inventory(Inventory& inv, int population, EconSite site,
                             const char* currencyId) {
    if (population <= 0) return;
    constexpr int kSeedVitalDays = 4;          // the larder
    const int needDays = site == EconSite::City ? 32 : 8;   // a season / days
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = commodity_index(kNeeds[i].commodity);
        if (idx < 0) continue;
        const bool dailyVital = kNeeds[i].popPerUnitDay == 1
            && kCommodities[idx].tier == CommodityTier::Vital;
        const int qty = dailyVital
            ? population * kSeedVitalDays
            : (population / kNeeds[i].popPerUnitDay) * needDays;
        if (qty > 0) inv.add(kNeeds[i].commodity, qty);
    }
    // Raw buffers per head — {commodity, units·population >> shift}. A
    // Village, whose whole business is raw, holds double.
    struct RawSeed { const char* id; int shift; };
    constexpr RawSeed kRawSeeds[] = {
        {"grain", 0}, {"wood", 0}, {"stone", 1}, {"clay", 2}, {"iron", 3},
    };
    const int siteMult = site == EconSite::Village ? 2 : 1;
    for (const RawSeed& r : kRawSeeds) {
        const int qty = (population >> r.shift) * siteMult;
        if (qty > 0) inv.add(r.id, qty);
    }
    // The TREASURY (owner, W2d): money is the KINGDOM'S OWN COIN, minted
    // "from the population" and living in the same container as the goods.
    // A city's capital is deep (8 a head); a village keeps a modest chest.
    // The treasury is what the market PAYS FROM: a town that runs dry stops
    // buying — the arbitrage-killer's other half.
    if (currencyId && currencyId[0]) {
        inv.add(currencyId, population * (site == EconSite::City ? 8 : 2));
    }
}

} // namespace sm
