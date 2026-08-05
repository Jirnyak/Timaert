#include "macro/econ_day.h"

#include <algorithm>

namespace sm {

namespace {

// Recipe/need ids resolve once, lazily, into index tables — hot loops then
// touch integers only (the faction-registry pattern).
struct ResolvedRecipe {
    int output = -1;
    int inputIdx[2] = {-1, -1};
    int inputQty[2] = {0, 0};
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
                     EconFactSink sink, void* user) {
    if (workers <= 0) return 0;
    const ResolvedTables& t = resolved();
    int total = 0;
    // v1 scheduler: walk the table in order, each recipe takes the workers it
    // can feed with inputs, leftovers move on. Table ORDER is the priority —
    // vital crafts sit first by construction.
    int workersLeft = workers;
    for (int i = 0; i < kRecipeCount && workersLeft > 0; ++i) {
        if (kRecipes[i].site != site) continue;
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.output < 0) continue;
        // Units this recipe could make from the store alone.
        int byInputs = 1 << 30;
        for (int k = 0; k < 2; ++k) {
            if (rr.inputIdx[k] < 0) continue;
            byInputs = std::min(
                byInputs, store.qty[std::size_t(rr.inputIdx[k])] / rr.inputQty[k]);
        }
        if (byInputs <= 0) continue;
        // Workers it takes to make them, capped by who is still idle.
        const int perDay = kRecipes[i].outputPerWorkerDay;
        const int wanted = (byInputs + perDay - 1) / perDay;
        const int staffed = std::min(wanted, workersLeft);
        const int made = std::min(byInputs, staffed * perDay);
        if (made <= 0) continue;
        for (int k = 0; k < 2; ++k) {
            if (rr.inputIdx[k] < 0) continue;
            store.qty[std::size_t(rr.inputIdx[k])] -= made * rr.inputQty[k];
        }
        store.qty[std::size_t(rr.output)] += made;
        workersLeft -= staffed;
        total += made;
        report(sink, user, EconFact::Kind::Produced, rr.output, made);
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
            out.fedPop = got;
            out.starvedPop = demand - got;
        } else if (!vital ) {
            out.unmetComfort += demand - got;
        }
    }
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

} // namespace sm
