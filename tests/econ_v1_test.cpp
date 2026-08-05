// The self-play harness for the v1 economy day-loop — the BALANCING ARBITER
// (work_vector №1, owner-approved methodology): the economy is not balanced
// by eye, it is balanced by laws asserted over a world that plays itself.
//
// Laws pinned here:
//   1. DICTIONARY INTEGRITY — unique ids, raw rows first, recipes and needs
//      reference real rows, recipe outputs are never raw, material masks only
//      address raw rows.
//   2. CONSERVATION — over 64 simulated days of a village+city pair, every
//      commodity's ledger balances to the unit:
//        gathered + produced == used_as_inputs + consumed + stock_remaining
//      Nothing is created from population, nothing vanishes.
//   3. NO SILENT STARVATION — the balanced scenario feeds everyone every day;
//      the famine scenario starves, emits FamineStarted exactly once, and
//      emits FamineEnded exactly once when bread arrives (facts fire on
//      TRANSITIONS, not every day).

#include "macro/econ_day.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL econ_v1_test: %s\n", msg);
    return 1;
}

struct Ledger {
    std::array<long, sm::kCommodityCount> gathered{};
    std::array<long, sm::kCommodityCount> produced{};
    int starvedEvents = 0;
    int famineStarted = 0;
    int famineEnded = 0;
};

void sink(void* user, const sm::EconFact& f) {
    auto* led = static_cast<Ledger*>(user);
    switch (f.kind) {
        case sm::EconFact::Kind::Gathered:
            led->gathered[std::size_t(f.commodity)] += f.amount;
            break;
        case sm::EconFact::Kind::Produced:
            led->produced[std::size_t(f.commodity)] += f.amount;
            break;
        case sm::EconFact::Kind::Starved: ++led->starvedEvents; break;
        case sm::EconFact::Kind::FamineStarted: ++led->famineStarted; break;
        case sm::EconFact::Kind::FamineEnded: ++led->famineEnded; break;
    }
}

// Inputs drawn per unit of each produced commodity, from the recipe table
// (outputs are unique in v1, asserted below).
void inputs_for_output(int outputIdx, int madeUnits,
                       std::array<long, sm::kCommodityCount>& used) {
    for (int r = 0; r < sm::kRecipeCount; ++r) {
        if (sm::commodity_index(sm::kRecipes[r].output) != outputIdx) continue;
        for (int k = 0; k < 2; ++k) {
            if (!sm::kRecipes[r].inputs[k].id) continue;
            used[std::size_t(sm::commodity_index(sm::kRecipes[r].inputs[k].id))]
                += long(madeUnits) * sm::kRecipes[r].inputs[k].qty;
        }
        return;
    }
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. Dictionary integrity ─────────────────────────────────────────
    for (int i = 0; i < kCommodityCount; ++i) {
        for (int j = i + 1; j < kCommodityCount; ++j) {
            if (std::strcmp(kCommodities[i].id, kCommodities[j].id) == 0) {
                return fail("duplicate commodity id");
            }
        }
        if ((i < kRawCommodityCount)
            != (kCommodities[i].tier == CommodityTier::Raw)) {
            return fail("raw rows must be exactly the first kRawCommodityCount");
        }
        if (kCommodities[i].materialMask >> kRawCommodityCount) {
            return fail("materialMask addresses a non-raw row");
        }
        if (kCommodities[i].baseValue <= 0 || kCommodities[i].weightKg <= 0.0f) {
            return fail("commodity value/weight must be positive");
        }
    }
    for (int r = 0; r < kRecipeCount; ++r) {
        const int out = commodity_index(kRecipes[r].output);
        if (out < 0) return fail("recipe output id unknown");
        if (kCommodities[out].tier == CommodityTier::Raw) {
            return fail("recipe may not output raw (only deposits create raw)");
        }
        for (int r2 = r + 1; r2 < kRecipeCount; ++r2) {
            if (std::strcmp(kRecipes[r].output, kRecipes[r2].output) == 0) {
                return fail("recipe outputs must be unique in v1");
            }
        }
        for (int k = 0; k < 2; ++k) {
            if (!kRecipes[r].inputs[k].id) continue;
            if (commodity_index(kRecipes[r].inputs[k].id) < 0) {
                return fail("recipe input id unknown");
            }
            if (kRecipes[r].inputs[k].qty <= 0) return fail("recipe qty <= 0");
        }
        if (kRecipes[r].outputPerWorkerDay <= 0) {
            return fail("outputPerWorkerDay <= 0");
        }
    }
    for (int n = 0; n < kNeedCount; ++n) {
        if (commodity_index(kNeeds[n].commodity) < 0) {
            return fail("need id unknown");
        }
        if (kNeeds[n].popPerUnitDay <= 0) return fail("need divisor <= 0");
    }

    // ── 2+3. Self-play: village gathers, city crafts, both eat ──────────
    const int grainIdx = commodity_index("grain");
    const int woodIdx = commodity_index("wood");
    const int clayIdx = commodity_index("clay");
    const int ironIdx = commodity_index("iron");
    const int stoneIdx = commodity_index("stone");
    const int breadIdx = commodity_index("bread");

    Ledger led{};
    Stockpile village{};
    Stockpile city{};
    std::array<long, kCommodityCount> consumed{};

    Deposit fields[]{{grainIdx, 1 << 20}};
    Deposit forest[]{{woodIdx, 1 << 20}};
    Deposit pits[]{{clayIdx, 1 << 20}, {ironIdx, 1 << 20}, {stoneIdx, 1 << 20}};

    const int villagePop = 16;
    const int cityPop = 32;
    bool villageFamine = false;
    bool cityFamine = false;

    const int kDays = 64;
    for (int day = 0; day < kDays; ++day) {
        // Village: 7 workers on grain, 1 in the forest, 1 rotating the pits.
        econ_gather_day(village, fields, 1, 7, &sink, &led);
        econ_gather_day(village, forest, 1, 1, &sink, &led);
        econ_gather_day(village, &pits[day % 3], 1, 1, &sink, &led);

        // Caravan abstraction v1: all raw moves to the city for crafting.
        for (int c = 0; c < kRawCommodityCount; ++c) {
            city.qty[std::size_t(c)] += village.qty[std::size_t(c)];
            village.qty[std::size_t(c)] = 0;
        }

        econ_produce_day(city, EconSite::City, 8, &sink, &led);

        // The return leg: the village's daily bread comes back.
        const int breadBack =
            std::min(villagePop, city.qty[std::size_t(breadIdx)]);
        city.qty[std::size_t(breadIdx)] -= breadBack;
        village.qty[std::size_t(breadIdx)] += breadBack;

        // Consumption, ledgered by store diffs.
        Stockpile beforeV = village;
        const ConsumeOutcome ov =
            econ_consume_day(village, villagePop, villageFamine, &sink, &led);
        villageFamine = ov.famineActive;
        Stockpile beforeC = city;
        const ConsumeOutcome oc =
            econ_consume_day(city, cityPop, cityFamine, &sink, &led);
        cityFamine = oc.famineActive;
        for (int c = 0; c < kCommodityCount; ++c) {
            consumed[std::size_t(c)] +=
                (beforeV.qty[std::size_t(c)] - village.qty[std::size_t(c)])
                + (beforeC.qty[std::size_t(c)] - city.qty[std::size_t(c)]);
        }

        // Law 3: after a two-day warm-up the pair feeds everyone, every day.
        if (day >= 2 && (ov.starvedPop > 0 || oc.starvedPop > 0)) {
            std::fprintf(stderr, "day=%d starvedV=%d starvedC=%d\n",
                         day, ov.starvedPop, oc.starvedPop);
            return fail("balanced scenario starved after warm-up");
        }
        for (int c = 0; c < kCommodityCount; ++c) {
            if (village.qty[std::size_t(c)] < 0 || city.qty[std::size_t(c)] < 0) {
                return fail("negative stock — bookkeeping bug");
            }
        }
    }

    // Law 2: the ledger balances to the unit for EVERY commodity.
    std::array<long, kCommodityCount> usedAsInputs{};
    for (int c = 0; c < kCommodityCount; ++c) {
        inputs_for_output(c, int(led.produced[std::size_t(c)]), usedAsInputs);
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        const long lhs = led.gathered[std::size_t(c)] + led.produced[std::size_t(c)];
        const long rhs = usedAsInputs[std::size_t(c)] + consumed[std::size_t(c)]
            + village.qty[std::size_t(c)] + city.qty[std::size_t(c)];
        if (lhs != rhs) {
            std::fprintf(stderr, "commodity=%s lhs=%ld rhs=%ld\n",
                         kCommodities[c].id, lhs, rhs);
            return fail("conservation law violated");
        }
    }
    // Deposits drained exactly what was gathered.
    long drained = (long(1) << 20) * 5 - fields[0].remaining - forest[0].remaining
        - pits[0].remaining - pits[1].remaining - pits[2].remaining;
    long gatheredTotal = 0;
    for (int c = 0; c < kCommodityCount; ++c)
        gatheredTotal += led.gathered[std::size_t(c)];
    if (drained != gatheredTotal) return fail("deposits leaked");

    // ── Famine transitions fire once, not daily ─────────────────────────
    Ledger fled{};
    Stockpile poor{};
    bool famine = false;
    for (int day = 0; day < 4; ++day) {
        const ConsumeOutcome o = econ_consume_day(poor, 8, famine, &sink, &fled);
        famine = o.famineActive;
        if (o.starvedPop != 8) return fail("empty store must starve everyone");
    }
    if (fled.famineStarted != 1) return fail("FamineStarted must fire ONCE");
    if (fled.starvedEvents != 4) return fail("Starved must report daily");
    poor.qty[std::size_t(breadIdx)] = 64;
    const ConsumeOutcome relief = econ_consume_day(poor, 8, famine, &sink, &fled);
    if (relief.starvedPop != 0 || relief.famineActive) {
        return fail("bread must end the famine");
    }
    if (fled.famineEnded != 1) return fail("FamineEnded must fire ONCE");

    std::printf("econ_v1_test: dictionary=ok conservation=ok deposits=ok "
                "no_starvation=ok famine_transitions=ok days=%d\n", kDays);
    return 0;
}
