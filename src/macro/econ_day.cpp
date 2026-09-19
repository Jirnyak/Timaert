#include "macro/econ_day.h"

#include "macro/currency.h"   // add_value_in_coins — the treasury seed
#include "macro/economy.h"    // stock_price — ranking asks THE price law

#include <algorithm>
#include <bit>
#include <cstring>

namespace sm {

namespace {

// Recipe/need ids resolve once, lazily, into index tables — hot loops then
// touch integers only (the faction-registry pattern).
struct ResolvedRecipe {
    int output = -1;
    // КАТАЛОЖНЫЙ ординал выхода — и он, а не товарный, делает вещь. Рецепт
    // вправе выдавать строку, которой в товарной лестнице НЕТ: зелье — это
    // предмет со своим составом, а не ярус нужд (владелец 2026-09-18,
    // «поушоны будем варить»). Товарный индекс остаётся для ФАКТА и для
    // «сегодняшнего стола» — там, где речь о нуждах населения.
    int outItem = -1;
    // The mint row (econ_day.h kMintOutput): output resolves to the town's
    // faction coin at run time. NOTHING else about it is special (owner
    // verdict 2026-09-12, «единая система крафта; города производят как
    // экономика-ИИ, используя ту же систему»): its matter is the coin row's
    // own composition and the day makes it through the same craft door as
    // every other output — so no matter is stored here at all.
    bool isMint = false;
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
            rr.outItem = item_index(kRecipes[i].output);
            rr.isMint = std::strcmp(kRecipes[i].output, kMintOutput) == 0;
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

int econ_produce_day(Inventory& store, const std::int32_t* needDebt,
                     const Skills& hands, int workers,
                     int population, EconFactSink sink, void* user,
                     int mintFactionIdx) {
    if (workers <= 0) return 0;
    // The mint's outputs: the faction's own coin family, GOLD FIRST — the
    // same metal-per-day tempo strikes 100× the value when the store holds
    // gold, which is exactly the nominal's point. Each nominal consumes its
    // OWN metal through the one craft door; a metal the town lacks simply
    // runs zero batches.
    int mintRows[3] = {-1, -1, -1};
    if (mintFactionIdx >= 0) {
        const char* const* coins = faction_coins(mintFactionIdx);
        for (int i = 0; i < 3; ++i) mintRows[i] = item_index(coins[2 - i]);
    }
    const ResolvedTables& t = resolved();

    // РАНЖИРОВАНИЕ РУК (CANON S10, владелец 2026-09-18: «руки идут туда, где
    // выше стоимость выхода на рабочий день»). Ни порога «работать или нет»,
    // ни перевода единиц: каждый рабочий день уходит в рецепт с наибольшей
    // стоимостью выхода по ТЕКУЩЕЙ цене склада (стоимость/день против
    // стоимость/день). Цена пересчитывается после каждого назначения —
    // слиппедж производства: полка растёт → цена падает → руки сами
    // переходят к следующему рецепту; сезон проедает склад → цена растёт →
    // возвращаются. Потолок выпечки ВЫВОДИТСЯ из цены, а не назначается.
    // Три прохода «стол/честные доли/остатки» умерли вместе с «нуждой»
    // `1 << 30`: они были грубым ранжированием, где все рецепты равно ценны,
    // — ~300 из 315 городских рук пекли товар, которого никто не просил,
    // и мир стоял у печи (замер 2026-09-18: хлеба на 17 сезонов, металла
    // нет).
    //
    // Кандидат = выходная строка каталога: одна на товарный рецепт, по одной
    // на каждый номинал монетного (каждый номинал ест СВОЙ металл через ту
    // же дверь крафта — прежняя семантика слота минта, развёрнутая в строки).
    struct Cand {
        int outIdx;      // выходная строка каталога
        int commodity;   // товарный ординал для факта (-1 у монеты)
        bool isMint;
        int demand;      // сезонный спрос выхода ЗДЕСЬ — единый закон
                         // (season_demand_for: остаток счёта + производный)
        int perDay;      // партий на рабочий день (труд строки × КПД места)
        int yield;       // единиц в партии
        int base;        // ItemDef::value выхода
    };
    Cand cands[std::size_t(kRecipeCount) + 2];
    int candCount = 0;
    // THE population-efficiency law (owner 2026-08-30, CANON S10, ?31
    // closed): КПД = log2(популяции)/4 — «город вдвое больше работает на
    // четверть лучше». Integer log2 (bit width); темп = колонка труда
    // выходной строки (items.h item_labour — то же число, которым платит
    // рука в SP), в ПАРТИЯХ.
    const int popLog =
        population > 1 ? (std::bit_width(unsigned(population)) - 1) : 0;
    const auto push_cand = [&](int outIdx, int commodity, bool isMint) {
        // -1 = минт без монеты (не двор) или мёртвый id — fail closed.
        if (outIdx < 0) return;
        if (item_parts(outIdx).empty()) return;   // без состава нет партий
        const ItemDef* def = item_def_at(outIdx);
        if (!def || def->value <= 0) return;
        cands[std::size_t(candCount++)] = Cand{
            outIdx, commodity, isMint,
            season_demand_for(def->id, needDebt, population, hands, &store),
            std::max(1, item_labour(outIdx) * popLog / 4),
            item_yield(outIdx), def->value};
    };
    for (int i = 0; i < kRecipeCount; ++i) {
        if (!recipe_known(hands, kRecipes[i].craft, kRecipes[i].minRank))
            continue;
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.isMint) {
            for (int m = 0; m < 3; ++m) push_cand(mintRows[m], -1, true);
        } else {
            push_cand(rr.outItem, rr.output, false);
        }
    }

    int total = 0;
    long long madeBatches[std::size_t(kRecipeCount) + 2] = {};
    for (int w = 0; w < workers; ++w) {
        // Верхний рецепт дня: стоимость выхода на рабочий день по текущей
        // полке = цена единицы × единиц за день. Пересчёт каждый раз —
        // O(рецептов) целых операций, партий за день не больше
        // workers × perDay (предохранитель переполнения жив арифметикой).
        long long bestScore = 0;
        int best = -1;
        int bestBatches = 0;
        for (int c = 0; c < candCount; ++c) {
            const Cand& cd = cands[std::size_t(c)];
            int batches = cd.perDay;
            for (const ItemPart& part : item_parts(cd.outIdx)) {
                batches = std::min(batches, store.count_of(int(part.def))
                                                / int(part.count));
            }
            if (batches <= 0) continue;
            const long long score =
                (long long)stock_price(cd.base, store.count_of(cd.outIdx),
                                       cd.demand)
                * batches * cd.yield;
            if (score > bestScore) {
                bestScore = score;
                best = c;
                bestBatches = batches;
            }
        }
        if (best < 0) break;   // ни один рецепт не кормим сырьём — руки без дела
        // THE craft door — the town makes goods and strikes coin exactly as
        // a hand at the bench does (owner 2026-09-12: «город делает монеты
        // через систему крафта по своему ИИ»). All-or-nothing lives in the
        // door: a full store is a day that did not happen, the inputs never
        // left, and no fact lies about goods that do not exist.
        if (!craft_item(store, cands[std::size_t(best)].outIdx,
                        bestBatches)) {
            // Склад не принял (переполнение слотов) — кандидат мёртв на
            // сегодня, иначе рука зациклится на несостоявшемся дне.
            cands[std::size_t(best)].perDay = 0;
            continue;
        }
        madeBatches[std::size_t(best)] += bestBatches;
        total += bestBatches;
    }
    for (int c = 0; c < candCount; ++c) {
        if (madeBatches[std::size_t(c)] <= 0) continue;
        const Cand& cd = cands[std::size_t(c)];
        const int units = int(madeBatches[std::size_t(c)] * cd.yield);
        if (cd.isMint) {
            // The fact names the METAL spent (part 0's commodity row) — the
            // money-supply counter balance_run watches.
            const ItemDef* metal =
                item_def_at(int(item_parts(cd.outIdx)[0].def));
            report(sink, user, EconFact::Kind::Minted,
                   metal ? commodity_index(metal->id) : -1, units);
        } else {
            report(sink, user, EconFact::Kind::Produced, cd.commodity,
                   units);
        }
    }
    return total;
}

int econ_pay_debt(Inventory& store, std::int32_t* needDebt,
                  EconFactSink sink, void* user) {
    // ОДНА дверь гашения (CANON S10: «всё, что падает в него, идёт в уплату
    // долга»): склад платит по счёту, оплаченное СЪЕДЕНО — списано с фактом
    // Consumed. После вызова видимый склад — только излишек, и потому всё
    // видимое свободно для погрузки и оплаты.
    const ResolvedTables& t = resolved();
    int paid = 0;
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = t.needIdx[i];
        if (idx < 0) continue;
        const std::int32_t debt = needDebt[idx];
        if (debt <= 0) continue;
        const int have = store.count_of(commodity_item_index(idx));
        const int eaten = have < debt ? have : int(debt);
        if (eaten <= 0) continue;
        store.remove_of(commodity_item_index(idx), eaten);
        needDebt[idx] -= eaten;
        paid += eaten;
        report(sink, user, EconFact::Kind::Consumed, idx, eaten);
    }
    return paid;
}

ConsumeOutcome econ_debt_boundary(Inventory& store, std::int32_t* needDebt,
                                  int population,
                                  EconFactSink sink, void* user) {
    ConsumeOutcome out{};
    const ResolvedTables& t = resolved();
    if (population <= 0) {
        // Мёртвое место — не должник: счёт закрывается вместе с жизнью.
        for (int i = 0; i < kNeedCount; ++i) {
            if (t.needIdx[i] >= 0) needDebt[t.needIdx[i]] = 0;
        }
        out.wellbeing = 0.0f;
        return out;
    }
    // 1. ВЗЫСКАНИЕ прошлого счёта. Хлеб: по душе за каждый непокрытый
    // душевой сезон — пропорция «доля долга × население» выходит сама,
    // хранить исходный счёт не нужно (счёт и был население × душевой
    // сезон); хвост меньше душевого сезона прощается — зеркало закона
    // «кусок меньше сезона не кормит никого». Прочие строки не убивают:
    // от нехватки ткани не умирают, она гасит РОСТ. Шкала комфорта — по
    // СЕГОДНЯШНЕМУ населению: с выставления счёта оно дрейфует ростом, но
    // доля читается на той же границе, где выставится новый счёт.
    int deaths = 0;
    int comfortDemand = 0;
    int unmetComfort = 0;
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = t.needIdx[i];
        if (idx < 0) continue;
        const std::int32_t remaining = needDebt[idx];
        if (i == kHungerNeedRow) {
            // Душевой сезон голодной строки = kDaysPerSeason юнитов
            // (popPerUnitDay == 1 по построению, static_assert в econ_day.h).
            deaths = std::min(population, int(remaining / kDaysPerSeason));
        } else {
            const int demand = (population / kNeeds[i].popPerUnitDay)
                             * kDaysPerSeason;
            if (demand <= 0) continue;
            comfortDemand += demand;
            unmetComfort += remaining < demand ? int(remaining) : demand;
        }
    }
    out.starvedPop = deaths;
    if (deaths > 0) {
        report(sink, user, EconFact::Kind::Starved, -1, deaths);
    }
    // БЛАГОПОЛУЧИЕ — ОДНА МЕРА (владелец 2026-09-19): доля оплаченной еды ×
    // доля оплаченного комфорта. Доля еды — это выжившие против населения
    // ДО взыскания: смертей ровно столько, сколько душевых сезонов не
    // оплачено, поэтому отношение И ЕСТЬ «оплачено / выставлено», без
    // второго хранимого числа. Голодавший сезон гасит рост сам, и это не
    // вторая кара: мёртвых уже не вернуть, а живые просто не плодятся,
    // пока не прокормятся.
    const float foodShare = float(population - deaths) / float(population);
    const float comfortShare = comfortDemand > 0
        ? 1.0f - float(unmetComfort) / float(comfortDemand)
        : 1.0f;
    out.wellbeing = foodShare * comfortShare;
    // 2. НОВЫЙ СЧЁТ — по населению ПОСЛЕ смертей, перезаписью: старый долг
    // не переносится (взыскали — выставили новый).
    const int popAfter = population - deaths;
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = t.needIdx[i];
        if (idx < 0) continue;
        needDebt[idx] = std::int32_t(
            (popAfter / kNeeds[i].popPerUnitDay) * kDaysPerSeason);
    }
    // 3. НЕМЕДЛЕННОЕ ГАШЕНИЕ: посевной амбар и прошлый излишек платят по
    // счёту в ту же минуту — та же дверь, что у прихода.
    econ_pay_debt(store, needDebt, sink, user);
    return out;
}

int econ_store_hygiene(Inventory& store, EconFactSink sink, void* user) {
    // Slot hygiene stays DAILY while the balances went seasonal (CANON
    // «Крафт/Скрап»: авто-скрап ИИ по порогу >50%): a store clogged past the
    // half mark by non-fungible lut melts its cheapest pieces back to matter,
    // so bread and ore always have somewhere to land. Plain stacks are never
    // touched, and the PLAYER'S bag never passes through this function.
    const int scrapped = auto_scrap_overflow(store);
    if (scrapped > 0) {
        report(sink, user, EconFact::Kind::Scrapped, -1, scrapped);
    }
    return scrapped;
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

void seed_landmark_inventory(Inventory& inv, int population, bool isCity,
                             int factionIdx, std::uint32_t seedSalt) {
    if (population <= 0) return;
    // Born MID-LIFE means born with LAST SEASON'S HARVEST IN THE BARN: the
    // first boundary (econ_debt_boundary) bills a whole season of bread and
    // the larder pays it on the spot — a place seeded with less starts life
    // in debt and must out-produce it or bury the shortfall a season later.
    // The larder IS a season.
    constexpr int kSeedVitalDays = kDaysPerSeason;
    static_assert(kSeedVitalDays == kDaysPerSeason,
                  "the seed larder must survive the first season window");
    const int needDays = isCity ? 32 : 8;   // a season / days
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = commodity_index(kNeeds[i].commodity);
        if (idx < 0) continue;
        // ГОЛОДНАЯ строка — одной дверью (kHungerNeedRow), а не вторым
        // выводом того же предиката.
        const int qty = (i == kHungerNeedRow)
            ? population * kSeedVitalDays
            : (population / kNeeds[i].popPerUnitDay) * needDays;
        if (qty > 0) inv.add(kNeeds[i].commodity, qty);
    }
    // Raw buffers per head — {commodity, units·population >> shift}. A
    // Village, whose whole business is raw, holds double.
    struct RawSeed { const char* id; int shift; };
    constexpr RawSeed kRawSeeds[] = {
        {"food", 0}, {"wood", 0}, {"stone", 1}, {"clay", 2}, {"iron", 3},
    };
    const int siteMult = isCity ? 1 : 2;
    for (const RawSeed& r : kRawSeeds) {
        const int qty = (population >> r.shift) * siteMult;
        if (qty > 0) inv.add(r.id, qty);
    }
    // The TREASURY (owner, W2d): money is the KINGDOM'S OWN COIN, minted
    // "from the population" and living in the same container as the goods.
    // A city's capital is deep (8 a head); a village keeps a modest chest.
    // The treasury is what the market PAYS FROM: a town that runs dry stops
    // buying — the arbitrage-killer's other half.
    //
    // ± a QUARTER's spread off the world seed (owner verdict S10, посев
    // капитала «от популяции ± четверть разброса от мирового сида»): the
    // factor walks 768..1279 over 1024 — 3/4..5/4 in po2 arithmetic — so
    // two towns of one size are born organically unequal, деterministically
    // per world. The value lands as the faction's own three coins,
    // change-made largest-first (add_value_in_coins).
    {
        const int base = population * (isCity ? 8 : 2);
        // One xorshift step spreads consecutive salts before the mask.
        std::uint32_t h = seedSalt;
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        const int seeded = int(std::int64_t(base) * (768 + (h & 511)) / 1024);
        add_value_in_coins(inv, factionIdx, seeded);
    }
}

} // namespace sm
