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

#include "check.h"
#include "macro/econ_day.h"
#include "macro/items.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/econ_v1_test.cpp", 0);
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
        if (kCommodities[i].weightKg <= 0.0f) {
            return fail("commodity weight must be positive");
        }
        // The PRICE is not a column here: the one anchor is the same row's
        // ItemDef.value, pinned positive by the link law of section 8 below.
    }
    for (int r = 0; r < kRecipeCount; ++r) {
        const int out = commodity_index(kRecipes[r].output);
        // The mint row's output is the caller's faction COIN, not a
        // commodity (econ_day.h kMintOutput) — the one sanctioned exception.
        if (out < 0 && std::strcmp(kRecipes[r].output, kMintOutput) != 0)
            return fail("recipe output id unknown");
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
    // The store IS the inventory now (one dictionary, one container).
    Inventory village{};
    Inventory city{};
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
            city.add_of(commodity_item_index(c), village.count_of(commodity_item_index(c)));
            village.remove_of(commodity_item_index(c), village.count_of(commodity_item_index(c)));
        }

        // The city bakes for the PAIR — its own table plus the village's
        // bread that rides back on the return leg.
        econ_produce_day(city, EconSite::City, 8, cityPop + villagePop,
                         &sink, &led);

        // The return leg: the village's daily bread comes back.
        const int breadBack =
            std::min(villagePop, city.count_of(commodity_item_index(breadIdx)));
        city.remove_of(commodity_item_index(breadIdx), breadBack);
        village.add_of(commodity_item_index(breadIdx), breadBack);

        // Consumption, ledgered by store diffs.
        Inventory beforeV = village;
        const ConsumeOutcome ov =
            econ_consume_day(village, villagePop, villageFamine, &sink, &led);
        villageFamine = ov.famineActive;
        Inventory beforeC = city;
        const ConsumeOutcome oc =
            econ_consume_day(city, cityPop, cityFamine, &sink, &led);
        cityFamine = oc.famineActive;
        for (int c = 0; c < kCommodityCount; ++c) {
            consumed[std::size_t(c)] +=
                (beforeV.count_of(commodity_item_index(c)) - village.count_of(commodity_item_index(c)))
                + (beforeC.count_of(commodity_item_index(c)) - city.count_of(commodity_item_index(c)));
        }

        // Law 3: after a two-day warm-up the pair feeds everyone, every day.
        if (day >= 2 && (ov.starvedPop > 0 || oc.starvedPop > 0)) {
            std::fprintf(stderr, "day=%d starvedV=%d starvedC=%d\n",
                         day, ov.starvedPop, oc.starvedPop);
            return fail("balanced scenario starved after warm-up");
        }
        for (int c = 0; c < kCommodityCount; ++c) {
            if (village.count_of(commodity_item_index(c)) < 0 || city.count_of(commodity_item_index(c)) < 0) {
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
            + village.count_of(commodity_item_index(c)) + city.count_of(commodity_item_index(c));
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
    Inventory poor{};
    bool famine = false;
    for (int day = 0; day < 4; ++day) {
        const ConsumeOutcome o = econ_consume_day(poor, 8, famine, &sink, &fled);
        famine = o.famineActive;
        if (o.starvedPop != 8) return fail("empty store must starve everyone");
    }
    if (fled.famineStarted != 1) return fail("FamineStarted must fire ONCE");
    if (fled.starvedEvents != 4) return fail("Starved must report daily");
    poor.remove_of(commodity_item_index(breadIdx),
                  poor.count_of(commodity_item_index(breadIdx)));
    poor.add_of(commodity_item_index(breadIdx), 64);
    const ConsumeOutcome relief = econ_consume_day(poor, 8, famine, &sink, &fled);
    if (relief.starvedPop != 0 || relief.famineActive) {
        return fail("bread must end the famine");
    }
    if (fled.famineEnded != 1) return fail("FamineEnded must fire ONCE");

    // ── 4. Consume: EVERY shortfall lands somewhere (Session 18) ────────
    // Bread in full, everything else absent. The daily-vital row feeds; every
    // other row's shortfall must be COUNTED — before the fix the two
    // non-daily Vital rows (cloth, bricks) matched neither branch and fell
    // into the void.
    {
        Inventory s{};
        const int pop = 256;
        s.remove_of(commodity_item_index(commodity_index("bread")),
                s.count_of(commodity_item_index(commodity_index("bread"))));
        s.add_of(commodity_item_index(commodity_index("bread")), pop);
        const ConsumeOutcome o = econ_consume_day(s, pop, false, nullptr, nullptr);
        if (o.fedPop != pop || o.starvedPop != 0) {
            return fail("bread-only pop must be fed in full");
        }
        int expectedUnmet = 0;
        for (int i = 0; i < kNeedCount; ++i) {
            if (std::strcmp(kNeeds[i].commodity, "bread") == 0) continue;
            expectedUnmet += pop / kNeeds[i].popPerUnitDay;
        }
        if (o.unmetComfort != expectedUnmet) {
            return fail("a non-daily shortfall fell into the void");
        }
    }

    // ── 5. Half bread: fed + starved PARTITION the town ─────────────────
    {
        Inventory s{};
        const int pop = 128;
        s.remove_of(commodity_item_index(commodity_index("bread")),
                s.count_of(commodity_item_index(commodity_index("bread"))));
        s.add_of(commodity_item_index(commodity_index("bread")), pop / 2);
        const ConsumeOutcome o = econ_consume_day(s, pop, false, nullptr, nullptr);
        if (o.fedPop != pop / 2 || o.starvedPop != pop - pop / 2
            || o.fedPop + o.starvedPop != pop || !o.famineActive) {
            return fail("fed + starved must partition the population");
        }
    }

    // ── 6. Produce: the first recipe may not hog the town ───────────────
    // Mountains of grain beside a little clay: before the fix bread staffed
    // ceil(grainStock/8) workers — the whole town — and bricks never saw a
    // single worker-day. Output DIVERSITY is the law: with inputs for both,
    // both are made.
    {
        Inventory s{};
        s.remove_of(commodity_item_index(commodity_index("grain")),
                s.count_of(commodity_item_index(commodity_index("grain"))));
        s.add_of(commodity_item_index(commodity_index("grain")), 1024);
        s.remove_of(commodity_item_index(commodity_index("clay")),
                      s.count_of(commodity_item_index(commodity_index("clay"))));
        s.add_of(commodity_item_index(commodity_index("clay")), 64);
        // population 0: no demand pass — pure fair shares, the exact surface
        // the old hog bug lived on.
        const int made =
            econ_produce_day(s, EconSite::City, 8, 0, nullptr, nullptr);
        if (made <= 0) return fail("city with inputs and workers made nothing");
        if (s.count_of(commodity_item_index(commodity_index("bricks"))) <= 0) {
            return fail("first recipe hogged every worker - no output diversity");
        }
        if (s.count_of(commodity_item_index(commodity_index("bread"))) <= 0) {
            return fail("fair shares must not starve the FIRST recipe either");
        }
    }

    // ── 7. A recipe with no inputs would mint matter — table law ────────
    for (int i = 0; i < kRecipeCount; ++i) {
        if (!kRecipes[i].inputs[0].id && !kRecipes[i].inputs[1].id) {
            return fail("recipe with no inputs mints matter from nothing");
        }
    }

    // ── 8. ONE dictionary (owner's ruling): every commodity is an item ──
    // The bread a city bakes and the bread in the player's bag are the same
    // row — a commodity id must resolve in the item catalog, and the two
    // tables must agree on MASS (there is one truth of weight).
    for (int i = 0; i < kCommodityCount; ++i) {
        const ItemDef* item = item_def(kCommodities[i].id);
        if (!item) return fail("commodity id missing from the item catalog");
        const float dw = item->weight - kCommodities[i].weightKg;
        if (dw > 0.001f || dw < -0.001f) {
            return fail("commodity and item disagree on weight");
        }
        if (item->value <= 0) return fail("commodity item has no value");
    }

    // ── 9. Birth stocks: a landmark is born mid-life (W2a) ──────────────
    {
        const int pop = 640;
        Inventory city;
        seed_landmark_inventory(city, pop, EconSite::City, "coin_empire");
        if (city.count("bread") != pop * 4) {
            return fail("birth larder must hold kSeedVitalDays of bread");
        }
        for (int i = 0; i < kNeedCount; ++i) {
            if (pop / kNeeds[i].popPerUnitDay <= 0) continue;
            if (city.count(kNeeds[i].commodity) <= 0) {
                return fail("a consumed need row was born empty");
            }
        }
        Inventory village;
        seed_landmark_inventory(village, pop, EconSite::Village, "coin_empire");
        if (village.count("grain") <= city.count("grain")) {
            return fail("a village's whole business is raw - it holds more");
        }
        if (village.count("cloth") >= city.count("cloth")) {
            return fail("a crafting city banks deeper crafted stocks");
        }
        Inventory again;
        seed_landmark_inventory(again, pop, EconSite::City, "coin_empire");
        if (again.count("bread") != city.count("bread")
            || again.used_slots() != city.used_slots()) {
            return fail("birth stocks must be deterministic from population");
        }
        // The treasury (W2d): money is the kingdom's COIN, living in the
        // SAME container, and a city's capital runs deep.
        if (city.count("coin_empire") != pop * 8
            || village.count("coin_empire") != pop * 2) {
            return fail("the birth treasury must scale with the heads");
        }
    }

    // ── 10. The SUPPLY-CEILING population law (owner, CANON S25 +
    //        2026-08-24: «все росты раз в сезон», no carrying cap) ────────
    {
        // Well-fed towns grow; nothing caps plenty — supply is the only
        // ceiling (wellbeing already turns growth around when the fields and
        // the trade fall short), and the rate is QUOTED PER SEASON: a month
        // is a season here, and in a town of a thousand souls a birth is an
        // event, not daily noise.
        const float small = population_delta_per_day(100, 1.0f);
        if (!(small > 0.0f)) return fail("a well-fed hamlet must grow");
        // A hamlet gains less than a head a day — the season quote spread
        // over kDaysPerSeason keeps daily change fractional.
        if (!(small < 1.0f)) {
            return fail("a hamlet cannot gain a whole head a day");
        }
        // A fully fed town gains its season quote over one season of days.
        const float aSeason =
            population_delta_per_day(1000, 1.0f) * float(kDaysPerSeason);
        const float quote = 1000.0f * kPopGrowthPerSeason;
        if (!(aSeason > quote * 0.99f && aSeason < quote * 1.01f)) {
            return fail("a season of full feeding pays the season quote");
        }
        // No cap: growth scales with heads all the way up (S25 — the town on
        // the crossroads may outgrow the black earth without roads).
        if (!(population_delta_per_day(20000, 1.0f)
              > population_delta_per_day(1000, 1.0f))) {
            return fail("no lid: a bigger fed town grows by more heads");
        }
        // Starvation bites in proportion to the mouths.
        const float starveBig = population_delta_per_day(16000, 0.0f);
        const float starveSmallTown = population_delta_per_day(1024, 0.0f);
        if (!(starveBig < starveSmallTown)) {
            return fail("a big starving town loses more heads than a small one");
        }
        // Wellbeing at the waterline holds steady.
        if (population_delta_per_day(1000, 0.5f) != 0.0f) {
            return fail("0.5 wellbeing is the waterline - no drift");
        }
        // Mood is the SAME wellbeing banded.
        if (mood_band_from_wellbeing(0.9f) != 0
            || mood_band_from_wellbeing(0.5f) != 2
            || mood_band_from_wellbeing(0.05f) != 4) {
            return fail("mood bands do not follow wellbeing");
        }
    }

    // ── 11. The economy works the ONE store, and only its own rows ─────
    // There is no adapter any more: the second container (a flat Stockpile of
    // 14 commodity counts) and the twice-a-day conversion to and from it died
    // with the second index space they bridged. What the law still owes is
    // the same promise the adapter used to make — a day of economy must not
    // disturb what is not the economy's.
    {
        Inventory inv;
        inv.add("grain", 100);
        inv.add("potion_hp", 3);   // NOT a commodity — must ride untouched
        econ_produce_day(inv, EconSite::Village, /*workers*/4,
                         /*population*/40, nullptr, nullptr);
        econ_consume_day(inv, /*population*/40, false, nullptr, nullptr);
        if (inv.count("potion_hp") != 3) {
            return fail("a day of economy disturbed what is not a commodity");
        }
        if (inv.count("grain") > 100) {
            return fail("consumption cannot create grain");
        }
    }

    // ── 12. Zero workers craft nothing — the ghost-bench half of the
    // honest-death law (owner, 2026-08-29; the population side lives in
    // world_tick_parity_test, which links settle_landmark_day).
    {
        Inventory ghost;
        ghost.add("grain", 100);
        if (econ_produce_day(ghost, EconSite::City, /*workers*/0,
                             /*population*/0, nullptr, nullptr) != 0) {
            return fail("zero workers produced something");
        }
    }

    std::printf("econ_v1_test: dictionary=ok conservation=ok deposits=ok "
                "no_starvation=ok famine_transitions=ok consume_laws=ok "
                "produce_fair=ok birth_stocks=ok population_law=ok "
                "one_store=ok ghost_bench=ok days=%d\n", kDays);
    CHECK(true, "every gate above held");
    return sm::test::report("econ_v1_test");
}
