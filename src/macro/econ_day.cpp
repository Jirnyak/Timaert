#include "macro/econ_day.h"

#include <algorithm>
#include <cstring>

namespace sm {

namespace {

// Recipe/need ids resolve once, lazily, into index tables — hot loops then
// touch integers only (the faction-registry pattern).
struct ResolvedRecipe {
    int output = -1;
    int inputIdx[2] = {-1, -1};
    int inputQty[2] = {0, 0};
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
            r.recipes[i].output = commodity_index(kRecipes[i].output);
            for (int k = 0; k < 2; ++k) {
                if (kRecipes[i].inputs[k].id) {
                    r.recipes[i].inputIdx[k] =
                        commodity_index(kRecipes[i].inputs[k].id);
                    r.recipes[i].inputQty[k] = kRecipes[i].inputs[k].qty;
                }
            }
            for (int n = 0; n < kNeedCount; ++n) {
                if (std::strcmp(kNeeds[n].commodity, kRecipes[i].output) == 0) {
                    r.recipes[i].demandDivisor = kNeeds[n].popPerUnitDay;
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

int econ_gather_day(Stockpile& store, Deposit* deposits, int depositCount,
                    int workers, EconFactSink sink, void* user) {
    if (workers <= 0 || depositCount <= 0 || !deposits) return 0;
    int capacity = workers * kGatherPerWorkerDay;
    int total = 0;
    for (int d = 0; d < depositCount && capacity > 0; ++d) {
        Deposit& dep = deposits[d];
        if (dep.remaining <= 0) continue;
        if (dep.commodity < 0 || dep.commodity >= kRawCommodityCount) continue;
        const int take = std::min(dep.remaining, capacity);
        dep.remaining -= take;
        store.qty[std::size_t(dep.commodity)] += take;
        capacity -= take;
        total += take;
        report(sink, user, EconFact::Kind::Gathered, dep.commodity, take);
    }
    return total;
}

int econ_produce_day(Stockpile& store, EconSite site, int workers,
                     int population, EconFactSink sink, void* user) {
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

    auto run_recipe = [&](int i, int unitCap, int workerCap) {
        const ResolvedRecipe& rr = t.recipes[i];
        // Units this recipe could make from the store alone.
        int byInputs = unitCap;
        for (int k = 0; k < 2; ++k) {
            if (rr.inputIdx[k] < 0) continue;
            byInputs = std::min(
                byInputs,
                store.qty[std::size_t(rr.inputIdx[k])] / rr.inputQty[k]);
        }
        if (byInputs <= 0) return;
        const int perDay = kRecipes[i].outputPerWorkerDay;
        int wanted = (byInputs + perDay - 1) / perDay;
        wanted = std::min(wanted, workerCap);
        const int staffed = std::min(wanted, workersLeft);
        const int made = std::min(byInputs, staffed * perDay);
        if (made <= 0) return;
        for (int k = 0; k < 2; ++k) {
            if (rr.inputIdx[k] < 0) continue;
            store.qty[std::size_t(rr.inputIdx[k])] -= made * rr.inputQty[k];
        }
        store.qty[std::size_t(rr.output)] += made;
        workersLeft -= staffed;
        total += made;
        report(sink, user, EconFact::Kind::Produced, rr.output, made);
    };

    // Pass 0 — today's table, by demand.
    for (int i = 0; i < kRecipeCount && workersLeft > 0; ++i) {
        if (kRecipes[i].site != site) continue;
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.output < 0 || rr.demandDivisor <= 0) continue;
        const int demand = population / rr.demandDivisor;
        const int have = store.qty[std::size_t(rr.output)];
        if (demand <= have) continue;   // yesterday's surplus covers today
        run_recipe(i, demand - have, workersLeft);
    }

    // Fair shares are computed AFTER the table is served, over the recipes
    // that still have inputs.
    int liveRecipes = 0;
    for (int i = 0; i < kRecipeCount; ++i) {
        if (kRecipes[i].site != site) continue;
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.output < 0) continue;
        bool feedable = true;
        for (int k = 0; k < 2; ++k) {
            if (rr.inputIdx[k] < 0) continue;
            feedable = feedable
                && store.qty[std::size_t(rr.inputIdx[k])] >= rr.inputQty[k];
        }
        if (feedable) ++liveRecipes;
    }
    fairShare = liveRecipes > 0
        ? (workersLeft + liveRecipes - 1) / liveRecipes : 0;

    // Pass 1 — fair shares; pass 2 — leftovers in table order.
    for (int pass = 1; pass <= 2 && workersLeft > 0; ++pass) {
        for (int i = 0; i < kRecipeCount && workersLeft > 0; ++i) {
            if (kRecipes[i].site != site) continue;
            if (t.recipes[i].output < 0) continue;
            run_recipe(i, 1 << 30, pass == 1 ? fairShare : workersLeft);
        }
    }
    return total;
}

ConsumeOutcome econ_consume_day(Stockpile& store, int population,
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
        const int got = std::min(demand, store.qty[std::size_t(idx)]);
        store.qty[std::size_t(idx)] -= got;
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

Stockpile stockpile_from_inventory(const Inventory& inv) {
    Stockpile s{};
    for (int i = 0; i < kCommodityCount; ++i) {
        s.qty[std::size_t(i)] = inv.count(kCommodities[i].id);
    }
    return s;
}

void apply_stockpile_to_inventory(const Stockpile& s, Inventory& inv) {
    for (int i = 0; i < kCommodityCount; ++i) {
        const int have = inv.count(kCommodities[i].id);
        const int want = s.qty[std::size_t(i)];
        if (want > have) inv.add(kCommodities[i].id, want - have);
        else if (want < have) inv.remove(kCommodities[i].id, have - want);
    }
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
