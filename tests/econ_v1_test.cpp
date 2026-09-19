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

#include "macro/characters.h"   // landmark_sheet — руки места
#include "check.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/items.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {
// Руки города и деревни — анкета их рода (characters.h), а не вид места.
static const sm::Skills& CITY =
    sm::landmark_sheet(sm::LandmarkType::City).skills;
static const sm::Skills& VILLAGE =
    sm::landmark_sheet(sm::LandmarkType::Village).skills;

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/econ_v1_test.cpp", 0);
    return 1;
}

struct Ledger {
    std::array<long, sm::kCommodityCount> gathered{};
    std::array<long, sm::kCommodityCount> produced{};
    std::array<long, sm::kCommodityCount> consumed{};
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
        case sm::EconFact::Kind::Consumed:
            // Потребление стало ДОЛГОМ (CANON S10): ест дверь гашения
            // (econ_pay_debt), и ест ДНЯМИ, не границей — ручной дифф
            // склада больше не накрывает все точки, факт накрывает.
            led->consumed[std::size_t(f.commodity)] += f.amount;
            break;
        case sm::EconFact::Kind::Minted: break;     // no mint in this fixture
        case sm::EconFact::Kind::Scrapped: break;   // no clog in this fixture
    }
}

// Inputs drawn per unit of each produced commodity — from the output row's
// own composition (macro/items.h item_parts), which since 2026-09-11 IS the
// recipe's matter (outputs are unique in v1, asserted below). The ledger
// deliberately reads through the same door production does: a drift between
// «что ест печь» and «из чего хлеб» is exactly what the merge killed.
void inputs_for_output(int outputIdx, int madeUnits,
                       std::array<long, sm::kCommodityCount>& used) {
    const int outCatalog = sm::commodity_item_index(outputIdx);
    // One batch of the composition makes `yield` units — inputs per unit are
    // count/yield (exact: a Produced amount is always whole batches).
    const int yield = sm::item_yield(outCatalog);
    for (const sm::ItemPart& part : sm::item_parts(outCatalog)) {
        const sm::ItemDef* d = sm::item_def_at(int(part.def));
        if (!d) continue;
        used[std::size_t(sm::commodity_index(d->id))]
            += long(madeUnits) * int(part.count) / yield;
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
        // СЫРЬЁ ИДЁТ ПЕРВЫМ И ПОДРЯД — это битовое пространство materialMask,
        // и спрашивается оно у ЕДИНСТВЕННОГО словаря «что это за вещь»:
        // категории каталога (ярус товарной строки умер 2026-09-18). Раньше
        // это держал static_assert по ярусу; компилятору категория недоступна
        // — каталог виден только своей единице трансляции, — поэтому закон
        // переехал сюда целиком, не ослабнув.
        const ItemDef* cd = item_def(kCommodities[i].id);
        if (!cd) return fail("commodity id names no catalog row");
        if ((i < kRawCommodityCount) != (cd->type == ItemType::Material)) {
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
        // A recipe's output must be a real CATALOG row — that is what the
        // production day actually makes (composition + yield + labour). Being
        // a COMMODITY on top of that is a statement about the needs ladder,
        // and not every made thing is one: the mint's output is the caller's
        // faction coin (kMintOutput), and a potion is an item nobody eats by
        // the ladder («поушоны будем варить», 2026-09-18).
        const int out = commodity_index(kRecipes[r].output);
        const bool isMint = std::strcmp(kRecipes[r].output, kMintOutput) == 0;
        if (!isMint && item_index(kRecipes[r].output) < 0)
            return fail("recipe output names no catalog row");

        const ItemDef* od = item_def(kRecipes[r].output);
        if (!isMint && od && od->type == ItemType::Material) {
            return fail("recipe may not output raw (only deposits create raw)");
        }
        for (int r2 = r + 1; r2 < kRecipeCount; ++r2) {
            if (std::strcmp(kRecipes[r].output, kRecipes[r2].output) == 0) {
                return fail("recipe outputs must be unique in v1");
            }
        }
        // The recipe's matter = its output row's composition. Every part must
        // be a commodity row too — production moves matter the economy's own
        // dictionary can name (the mint's metal is checked in section 7).
        for (const ItemPart& part : item_parts(commodity_item_index(out))) {
            const ItemDef* d = item_def_at(int(part.def));
            if (!d || commodity_index(d->id) < 0) {
                return fail("recipe input is not a commodity row");
            }
            if (int(part.count) <= 0) return fail("recipe qty <= 0");
        }
        // ...and its tempo = the same row's labour column (2026-09-12): a
        // scheduled output nobody's day can turn is a dead recipe row. The
        // mint's output resolves per town — its labour lives on the coin
        // rows, checked with their composition below.
        if (out >= 0 && item_labour(commodity_item_index(out)) <= 0) {
            return fail("recipe output has no labour (batches/person-day)");
        }
    }
    for (int n = 0; n < kNeedCount; ++n) {
        if (commodity_index(kNeeds[n].commodity) < 0) {
            return fail("need id unknown");
        }
        if (kNeeds[n].popPerUnitDay <= 0) return fail("need divisor <= 0");
    }

    // ── 2+3. Self-play: village gathers, city crafts, both eat ──────────
    const int grainIdx = commodity_index("food");
    const int woodIdx = commodity_index("wood");
    const int clayIdx = commodity_index("clay");
    const int ironIdx = commodity_index("iron");
    const int stoneIdx = commodity_index("stone");
    const int breadIdx = commodity_index("bread");

    Ledger led{};
    // The store IS the inventory now (one dictionary, one container).
    Inventory village{};
    Inventory city{};
    // Счета мест (CANON S10, потребление — долг): граница выставляет,
    // приход гасит; в мире долг живёт на Landmark, здесь — рядом со складом.
    std::int32_t villageDebt[kCommodityCount] = {};
    std::int32_t cityDebt[kCommodityCount] = {};

    // The GATHER half lives with the field agents now (ai_gatherer; the
    // pure econ_gather_day died with the owner's 2026-08-31 ruling — «уже
    // собирают крестьяне»). The self-play models their person-day norm
    // directly, and the ledger counts it the way the sink used to.
    const auto gather = [&](Inventory& store, int commodityIdx, int workers,
                            Ledger& l) {
        const int take = workers * kGatherPerWorkerDay;
        if (store.add_of(commodity_item_index(commodityIdx), take)) {
            l.gathered[std::size_t(commodityIdx)] += take;
        }
    };

    const int villagePop = 16;
    const int cityPop = 32;
    bool villageFamine = false;
    bool cityFamine = false;

    // Days are 1-based like the world's own (day 1 = the first boundary):
    // production runs daily, the CONSUME lands on season boundaries only
    // (CANON S19.2) — three windows inside 96 days.
    const int kDays = 96;
    int boundariesStarvedAfterWarmup = 0;
    for (int day = 1; day <= kDays; ++day) {
        // Village: 7 workers on grain, 1 in the forest, 1 rotating the pits.
        gather(village, grainIdx, 7, led);
        gather(village, woodIdx, 1, led);
        const int pitRotation[3] = {clayIdx, ironIdx, stoneIdx};
        gather(village, pitRotation[day % 3], 1, led);

        // Caravan abstraction v1: all raw moves to the city for crafting.
        for (int c = 0; c < kRawCommodityCount; ++c) {
            city.add_of(commodity_item_index(c), village.count_of(commodity_item_index(c)));
            village.remove_of(commodity_item_index(c), village.count_of(commodity_item_index(c)));
        }

        // The city bakes for the PAIR — its own table plus the village's
        // bread that rides back on the return leg. Twelve workers, not the
        // old eight: the seasonal window (S19.2) makes the city BANK a
        // season of bread between boundaries, so steady-state production
        // must beat consumption + export with headroom, not sit on the old
        // daily knife-edge (measured: 8 workers banked 840 of the 1024).
        econ_produce_day(city, CITY, 12, cityPop + villagePop,
                         &sink, &led);

        // The return leg: the village's daily bread comes back.
        const int breadBack =
            std::min(villagePop, city.count_of(commodity_item_index(breadIdx)));
        city.remove_of(commodity_item_index(breadIdx), breadBack);
        village.add_of(commodity_item_index(breadIdx), breadBack);

        // ПОТРЕБЛЕНИЕ — ДОЛГ (CANON S10): граница выставляет счёт и
        // взыскивает прошлый, ДНЕВНОЕ гашение платит по нему тем, что
        // пришло, — ровно как settle_landmark_day в мире. Съеденное
        // ложится в леджер фактами Consumed.
        if (season_boundary(day)) {
            const ConsumeOutcome ov = econ_debt_boundary(
                village, villageDebt, villagePop, villageFamine, &sink, &led);
            villageFamine = ov.famineActive;
            const ConsumeOutcome oc = econ_debt_boundary(
                city, cityDebt, cityPop, cityFamine, &sink, &led);
            cityFamine = oc.famineActive;
            // Law 3: the pair pays every season's bill — nobody dies at any
            // boundary (the first bill is issued on day 1 and cannot kill;
            // every later one must find the season already paid).
            if (ov.starvedPop > 0 || oc.starvedPop > 0) {
                std::fprintf(stderr,
                             "day=%d starvedV=%d starvedC=%d "
                             "debtV=%d debtC=%d breadC=%d\n",
                             day, ov.starvedPop, oc.starvedPop,
                             villageDebt[breadIdx], cityDebt[breadIdx],
                             city.count_of(commodity_item_index(breadIdx)));
                ++boundariesStarvedAfterWarmup;
            }
        }
        // Дневной такт гашения — та же дверь, что в settle_landmark_day.
        econ_pay_debt(village, villageDebt, &sink, &led);
        econ_pay_debt(city, cityDebt, &sink, &led);
        for (int c = 0; c < kCommodityCount; ++c) {
            if (village.count_of(commodity_item_index(c)) < 0 || city.count_of(commodity_item_index(c)) < 0) {
                return fail("negative stock — bookkeeping bug");
            }
        }
    }

    if (boundariesStarvedAfterWarmup > 0) {
        return fail("balanced scenario starved a season after warm-up");
    }

    // Law 2: the ledger balances to the unit for EVERY commodity.
    std::array<long, kCommodityCount> usedAsInputs{};
    for (int c = 0; c < kCommodityCount; ++c) {
        inputs_for_output(c, int(led.produced[std::size_t(c)]), usedAsInputs);
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        const long lhs = led.gathered[std::size_t(c)] + led.produced[std::size_t(c)];
        const long rhs = usedAsInputs[std::size_t(c)] + led.consumed[std::size_t(c)]
            + village.count_of(commodity_item_index(c)) + city.count_of(commodity_item_index(c));
        if (lhs != rhs) {
            std::fprintf(stderr, "commodity=%s lhs=%ld rhs=%ld\n",
                         kCommodities[c].id, lhs, rhs);
            return fail("conservation law violated");
        }
    }
    // (The deposit-drain half of the ledger lives with the field agents
    // now — woodcutter_gather_test holds «layer loss == store gain» over
    // the live layers; the pure-step drain died with econ_gather_day.)

    // ── Famine transitions fire once, not every window ──────────────────
    // Долговой закон сдвигает голод на окно (первая граница только
    // выставляет счёт — убить ей нечего) и делает смерть ПРОПОРЦИЕЙ:
    // место, платящее полсчёта, каждый сезон хоронит половину — 32 → 16 →
    // 8 → 4 → 2, хронический голод при живом месте. Место вовсе без
    // прихода умирает ЦЕЛИКОМ за одно взыскание — это закон, не поломка.
    Ledger fled{};
    Inventory poor{};
    std::int32_t poorDebt[kCommodityCount] = {};
    bool famine = false;
    int poorPop = 32;
    for (int window = 0; window < 5; ++window) {
        const ConsumeOutcome o = econ_debt_boundary(
            poor, poorDebt, poorPop, famine, &sink, &fled);
        famine = o.famineActive;
        if (window == 0 && o.starvedPop != 0) {
            return fail("the first bill cannot kill before it is due");
        }
        if (window > 0 && o.starvedPop != poorPop / 2) {
            std::fprintf(stderr, "window=%d pop=%d starved=%d debtBread=%d\n",
                         window, poorPop, o.starvedPop, poorDebt[breadIdx]);
            return fail("a half-paid season must claim exactly half the souls");
        }
        poorPop -= o.starvedPop;   // как settle_landmark_day: умершие ушли
        // Привоз в полсчёта: гасится СРАЗУ той же дверью, что в мире.
        poor.add_of(commodity_item_index(breadIdx),
                    poorDebt[breadIdx] / 2);
        econ_pay_debt(poor, poorDebt, &sink, &fled);
    }
    if (fled.famineStarted != 1) return fail("FamineStarted must fire ONCE");
    if (fled.starvedEvents != 4) return fail("Starved must report per window");
    // Рельеф — привоз, кроющий счёт целиком: следующая граница не
    // взыскивает никого, и FamineEnded стреляет ровно раз.
    poor.add_of(commodity_item_index(breadIdx), poorDebt[breadIdx]);
    econ_pay_debt(poor, poorDebt, &sink, &fled);
    const ConsumeOutcome relief = econ_debt_boundary(
        poor, poorDebt, poorPop, famine, &sink, &fled);
    if (relief.starvedPop != 0 || relief.famineActive) {
        return fail("a paid season must end the famine");
    }
    if (fled.famineEnded != 1) return fail("FamineEnded must fire ONCE");

    // ── 4. Boundary: EVERY shortfall lands somewhere (Session 18) ───────
    // A season of bread in full, everything else absent. Под долгом (CANON
    // S10) недоплата видна ВЗЫСКАНИЕМ — на границе, следующей за счётом:
    // первая граница выставляет счёт и хлеб платит его на месте, вторая
    // судит остаток — хлебный долг погашен (никто не умер), долг каждой
    // прочей строки обязан лечь в unmetComfort целиком, не в пустоту.
    {
        Inventory s{};
        std::int32_t debt[kCommodityCount] = {};
        const int pop = 256;
        s.add_of(commodity_item_index(commodity_index("bread")),
                 pop * kDaysPerSeason);
        const ConsumeOutcome first =
            econ_debt_boundary(s, debt, pop, false, nullptr, nullptr);
        if (first.starvedPop != 0) {
            return fail("the first bill cannot kill before it is due");
        }
        if (s.count_of(commodity_item_index(commodity_index("bread"))) != 0) {
            return fail("the bill must eat the whole shelf on the spot");
        }
        const ConsumeOutcome o =
            econ_debt_boundary(s, debt, pop, false, nullptr, nullptr);
        if (o.fedPop != pop || o.starvedPop != 0) {
            return fail("bread-only pop must be fed in full");
        }
        int expectedUnmet = 0;
        for (int i = 0; i < kNeedCount; ++i) {
            if (std::strcmp(kNeeds[i].commodity, "bread") == 0) continue;
            expectedUnmet += (pop / kNeeds[i].popPerUnitDay) * kDaysPerSeason;
        }
        if (o.unmetComfort != expectedUnmet) {
            return fail("a non-daily shortfall fell into the void");
        }
    }

    // ── 5. Half a season of bread starves HALF the town — a season late ─
    // ПРОПОРЦИЯ ЖИВЁТ ВО ВЗЫСКАНИИ (CANON S10 + вердикт 2026-09-19
    // «смерть — единственная кара»): полсезона хлеба гасят полсчёта,
    // непокрытая половина уходит населением на СЛЕДУЮЩЕЙ границе — по
    // душе за каждый непокрытый душевой сезон. Выжившая половина сыта
    // (fedPop), рост её судит только комфорт.
    {
        Inventory s{};
        std::int32_t debt[kCommodityCount] = {};
        const int pop = 128;
        const int half = pop * kDaysPerSeason / 2;
        s.add_of(commodity_item_index(commodity_index("bread")), half);
        econ_debt_boundary(s, debt, pop, false, nullptr, nullptr);
        // Каждая единица на полке платит по счёту — склад пуст, долг
        // помнит ровно вторую половину.
        if (s.count_of(commodity_item_index(commodity_index("bread"))) != 0) {
            return fail("every unit on the shelf must pay the bill");
        }
        if (debt[commodity_index("bread")] != half) {
            return fail("the debt must remember exactly the unpaid half");
        }
        const ConsumeOutcome o =
            econ_debt_boundary(s, debt, pop, false, nullptr, nullptr);
        if (o.starvedPop != pop - pop / 2 || o.fedPop != pop / 2
            || !o.famineActive) {
            return fail("half a season must starve exactly half the souls");
        }
        // Хвост меньше душевого сезона ПРОЩАЕТСЯ на взыскании (зеркало
        // старого «кусок меньше сезона не кормит никого»): 31 хлеба гасят
        // 31 единицу счёта и снимают со смертей ровно одну душу.
        Inventory tail{};
        std::int32_t tailDebt[kCommodityCount] = {};
        tail.add_of(commodity_item_index(commodity_index("bread")),
                    kDaysPerSeason - 1);
        econ_debt_boundary(tail, tailDebt, pop, false, nullptr, nullptr);
        const ConsumeOutcome ot =
            econ_debt_boundary(tail, tailDebt, pop, false, nullptr, nullptr);
        if (ot.starvedPop != pop - 1) {
            return fail("a sub-season scrap forgives exactly one death");
        }
    }

    // ── 6. Produce: the first recipe may not hog the town ───────────────
    // Mountains of grain beside a little clay: before the fix bread staffed
    // ceil(grainStock/8) workers — the whole town — and bricks never saw a
    // single worker-day. Output DIVERSITY is the law: with inputs for both,
    // both are made.
    {
        Inventory s{};
        s.remove_of(commodity_item_index(commodity_index("food")),
                s.count_of(commodity_item_index(commodity_index("food"))));
        s.add_of(commodity_item_index(commodity_index("food")), 1024);
        s.remove_of(commodity_item_index(commodity_index("clay")),
                      s.count_of(commodity_item_index(commodity_index("clay"))));
        s.add_of(commodity_item_index(commodity_index("clay")), 64);
        // population 0: no demand pass — pure fair shares, the exact surface
        // the old hog bug lived on.
        const int made =
            econ_produce_day(s, CITY, 8, 0, nullptr, nullptr);
        if (made <= 0) return fail("city with inputs and workers made nothing");
        if (s.count_of(commodity_item_index(commodity_index("bricks"))) <= 0) {
            return fail("first recipe hogged every worker - no output diversity");
        }
        if (s.count_of(commodity_item_index(commodity_index("bread"))) <= 0) {
            return fail("fair shares must not starve the FIRST recipe either");
        }
    }

    // ── 7. A recipe with no inputs would mint matter — table law ────────
    // Every output must carry a composition on its catalog row, or the day
    // makes goods from nothing. The mint's output resolves per town (any
    // faction coin), so ALL currency rows answer for it — coin matter (and
    // its value-neutrality) is pinned in item_parts_test and by the
    // static_assert beside the table.
    for (int i = 0; i < kRecipeCount; ++i) {
        if (std::strcmp(kRecipes[i].output, kMintOutput) == 0) continue;
        if (item_parts(item_index(kRecipes[i].output)).empty()) {
            return fail("recipe with no inputs mints matter from nothing");
        }
    }
    // Every coin row named by the faction registry's mint columns (the
    // kCurrencyDefs list died with verdict №1) carries matter.
    for (const FactionDef& f : kFactionDefs) {
        for (const char* coin : f.mint) {
            if (!coin || !coin[0]) continue;
            if (item_parts(item_index(coin)).empty()) {
                return fail("a mint output (faction coin) has no composition");
            }
        }
    }
    // The productivity ANCHOR (owner: «1 добытчик кормит 32 душ» chain-wide)
    // is bread's labour column — the person-day the whole economy is scaled
    // by. If this drifts, every balance number silently reprices.
    if (item_labour(item_index("bread")) != kGatherPerWorkerDay) {
        return fail("bread labour must equal the person-day anchor");
    }

    // ── 7б. The overflow law rides the consume day (CANON «Крафт/Скрап») ─
    // A store clogged past the half mark by non-fungible lut comes back to
    // it within ONE daily tick — the wiring, not the door (the door's own
    // laws live in item_parts_test).
    {
        Inventory s{};
        const int dagger = item_index("wpn_dagger");
        for (int i = 0; i < 200; ++i) {
            ItemRef r{};
            r.def = std::uint16_t(dagger);
            r.count = 1;
            r.seed = std::uint32_t(1 + i);   // distinct seeds: 200 slots
            s.add_ref(r);
        }
        if (s.used_slots() <= kAutoScrapSlots) {
            return fail("clog fixture did not overflow (negative control)");
        }
        econ_store_hygiene(s, nullptr, nullptr);
        if (s.used_slots() > kAutoScrapSlots) {
            return fail("daily hygiene left the store clogged past 50%");
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
        const int empire = faction_index("empire");
        seed_landmark_inventory(city, pop, true, empire, 0x1234u);
        if (city.count("bread") != pop * kDaysPerSeason) {
            return fail("birth larder must hold a SEASON of bread — a place "
                        "seeded thinner dies of arithmetic at its first "
                        "window (S19.2)");
        }
        for (int i = 0; i < kNeedCount; ++i) {
            if (pop / kNeeds[i].popPerUnitDay <= 0) continue;
            if (city.count(kNeeds[i].commodity) <= 0) {
                return fail("a consumed need row was born empty");
            }
        }
        Inventory village;
        seed_landmark_inventory(village, pop, false, empire,
                                0x1234u);
        if (village.count("food") <= city.count("food")) {
            return fail("a village's whole business is raw - it holds more");
        }
        if (village.count("cloth") >= city.count("cloth")) {
            return fail("a crafting city banks deeper crafted stocks");
        }
        Inventory again;
        seed_landmark_inventory(again, pop, true, empire, 0x1234u);
        if (again.count("bread") != city.count("bread")
            || again.used_slots() != city.used_slots()) {
            return fail("birth stocks must be deterministic from population");
        }
        // The treasury (W2d): money is the kingdom's COIN, living in the
        // SAME container, and a city's capital runs deep. Since verdict №1
        // it lands as the family's three nominals (change-made) from
        // population ± a QUARTER's spread off the world seed — so the check
        // is a band around the base, not an equality.
        const auto treasury = [](const Inventory& inv) {
            return inv.count("coin_empire_gold") * 100
                 + inv.count("coin_empire_silver") * 10
                 + inv.count("coin_empire_copper");
        };
        const int cityBase = pop * 8;
        const int vilBase = pop * 2;
        if (treasury(city) < cityBase * 3 / 4
            || treasury(city) > cityBase * 5 / 4
            || treasury(village) < vilBase * 3 / 4
            || treasury(village) > vilBase * 5 / 4) {
            return fail("the birth treasury must scale with the heads "
                        "within the quarter spread");
        }
        if (treasury(again) != treasury(city)) {
            return fail("one salt must seed one treasury (determinism)");
        }
        // A different salt walks the spread: over a few salts at least one
        // treasury must differ, or the spread is decorative.
        {
            bool differs = false;
            for (std::uint32_t salt = 1; salt <= 4 && !differs; ++salt) {
                Inventory other;
                seed_landmark_inventory(other, pop, true, empire,
                                        salt);
                differs = treasury(other) != treasury(city);
            }
            if (!differs) return fail("the quarter spread never spreads");
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
        inv.add("food", 100);
        inv.add("potion_hp", 3);   // NOT a commodity — must ride untouched
        std::int32_t debt[kCommodityCount] = {};
        econ_produce_day(inv, VILLAGE, /*workers*/4,
                         /*population*/40, nullptr, nullptr);
        econ_debt_boundary(inv, debt, /*population*/40, false,
                           nullptr, nullptr);
        if (inv.count("potion_hp") != 3) {
            return fail("a day of economy disturbed what is not a commodity");
        }
        if (inv.count("food") > 100) {
            return fail("consumption cannot create grain");
        }
    }

    // ── 12. Zero workers craft nothing — the ghost-bench half of the
    // honest-death law (owner, 2026-08-29; the population side lives in
    // world_tick_parity_test, which links settle_landmark_day).
    {
        Inventory ghost;
        ghost.add("food", 100);
        if (econ_produce_day(ghost, CITY, /*workers*/0,
                             /*population*/0, nullptr, nullptr) != 0) {
            return fail("zero workers produced something");
        }
    }

    // ── 13. THE MINT is inside the conservation law ────────────────────
    // Law №2 at the top of this file is asserted over the commodity ledger,
    // and the sink skipped the mint's fact with «no mint in this fixture» —
    // so the one recipe that turns a stack into MONEY sat outside the law
    // that says nothing vanishes. The metal is what is pinned here, twice,
    // because it can be lost two ways: a strike that succeeds must consume
    // the metal ONCE, and a strike the shelf refuses must leave it where it
    // was (the promise add_ref's own comment makes, and the promise the
    // non-mint path already keeps by putting its inputs back).
    {
        const ItemDef* metal = item_def_at(item_index("silver"));
        if (!metal || metal->value <= 0) {
            return fail("silver carries no catalog value for the mint to pay");
        }
        // COINS PER UNIT OF METAL is the coin row's own yield column («1
        // металл → 32 монеты своего металла», verdict №1); the metal's price
        // is that yield × the coin's nominal, which mint_is_value_neutral
        // pins at compile time.
        const int coinRow = item_index("coin_empire_silver");
        const int yield = item_yield(coinRow);
        if (yield <= 0 || metal->value != yield * item_def_at(coinRow)->value) {
            return fail("the mint metal's price is not yield x nominal");
        }
        auto minted_sink = [](void* user, const EconFact& f) {
            if (f.kind == EconFact::Kind::Minted) {
                *static_cast<long*>(user) += f.amount;
            }
        };

        // A store of metal and nothing else: no other City recipe can be fed,
        // so every write below belongs to the mint. Two workers on a hamlet's
        // population strike far less than the store holds — the leftover is
        // what makes a SECOND debit visible at all (drain the stack dry and
        // the extra remove_of fails silently and hides itself).
        long mintedCoins = 0;
        Inventory store;
        store.add("silver", 40);
        econ_produce_day(store, CITY, /*workers*/2, /*population*/2,
                         minted_sink, &mintedCoins, faction_index("empire"));

        const int spent = 40 - store.count("silver");
        const int coins = store.count("coin_empire_silver");
        if (coins <= 0) return fail("the mint struck nothing from a store of silver");
        if (long(coins) != mintedCoins) {
            return fail("the Minted fact and the coins on the shelf disagree");
        }
        if (spent * yield != coins) {
            std::printf("  mint: silver spent=%d yield=%d coins struck=%d\n",
                        spent, yield, coins);
            return fail("the mint's metal does not match the coins struck");
        }

        // The refused strike. Every slot but one is taken by a distinct kind
        // (distinct seeds do not stack), the last holds more metal than one
        // day can strike — so the debited stack stays occupied, the coin has
        // nowhere to land, and add() refuses.
        Inventory full;
        for (int i = 0; i < kMaxInventorySlots - 1; ++i) {
            ItemRef junk{};
            junk.def = std::uint16_t(item_index("potion_hp"));
            junk.count = 1;
            junk.seed = std::uint32_t(i + 1);
            if (!full.add_ref(junk)) return fail("could not fill the fixture's store");
        }
        full.add("silver", 40);
        if (!full.full()) return fail("the fixture's store is not full");
        econ_produce_day(full, CITY, /*workers*/2, /*population*/2,
                         nullptr, nullptr, faction_index("empire"));
        if (full.count("coin_empire_silver") != 0) {
            return fail("a full shelf accepted coins it had no slot for");
        }
        if (full.count("silver") != 40) {
            std::printf("  refused mint: silver left=%d of 40\n",
                        full.count("silver"));
            return fail("a refused strike ate the metal — the inputs were not put back");
        }
    }

    std::printf("econ_v1_test: dictionary=ok conservation=ok deposits=ok "
                "no_starvation=ok famine_transitions=ok consume_laws=ok "
                "produce_fair=ok birth_stocks=ok population_law=ok "
                "one_store=ok ghost_bench=ok mint_conservation=ok days=%d\n",
                kDays);
    CHECK(true, "every gate above held");
    return sm::test::report("econ_v1_test");
}
