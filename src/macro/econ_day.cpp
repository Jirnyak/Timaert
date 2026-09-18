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

int econ_produce_day(Inventory& store, const Skills& hands, int workers,
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
        int demand;      // дневной спрос выхода ЗДЕСЬ — единый закон
                         // (daily_demand_for: лестница + производный)
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
            daily_demand_for(def->id, population, hands, &store),
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

ConsumeOutcome econ_consume_season(Inventory& store, int population,
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
    // СЫТОСТЬ ПРОПОРЦИОНАЛЬНА (владелец 2026-09-18, CANON S25): накормлено
    // столько, на сколько хватило. Кромка «всё или ничего» была про МЕСТО и
    // била по малым квадратично: хутору на 30 душ нужда ложилась одним
    // куском 960, и промах на единицу давал ПОЛНЫЙ голод при почти полных
    // закромах (измерено: 94 645 душ-дней за 64 дня). Теперь кромка — про
    // ДУШУ: душа сыта, если ЕЁ сезон покрыт целиком; остаток меньше одного
    // душевого сезона честно лежит до следующего окна. Этим же движением
    // «богато добывает → хорошо растёт» впервые становится измеримым — и
    // недоед гасит рост в той же точке, что пустые полки комфорта
    // (0.5 × 1.0 == 1.0 × 0.5, арифметика закона роста S25).
    int fed = population;
    for (int i = 0; i < kNeedCount; ++i) {
        const int idx = t.needIdx[i];
        if (idx < 0) continue;
        const int demand = (population / kNeeds[i].popPerUnitDay)
                         * kDaysPerSeason;
        if (demand <= 0) continue;
        const int have = store.count_of(commodity_item_index(idx));
        // THE hunger row, asked of the one door that knows which it is
        // (econ_day.h kHungerNeedRow — the same two columns this branch used
        // to re-derive inline). One definition, three eaters: the population
        // here, the garrison in world_tick, the squad in npc_ai.
        if (i == kHungerNeedRow) {
            // Душ, чей сезон склад кроет целиком (голодная строка — 1 юнит
            // на душу-день по построению, static_assert в econ_day.h).
            const int fedHere =
                std::min(population, have / kDaysPerSeason);
            const int eaten = fedHere * kDaysPerSeason;
            if (eaten > 0) {
                store.remove_of(commodity_item_index(idx), eaten);
                report(sink, user, EconFact::Kind::Consumed, idx, eaten);
            }
            fed = std::min(fed, fedHere);
        } else {
            // Комфорт тем же законом: съедено сколько есть, недостача
            // считается пропорционально — благополучие читает доли.
            const int eaten = have < demand ? have : demand;
            if (eaten > 0) {
                store.remove_of(commodity_item_index(idx), eaten);
                report(sink, user, EconFact::Kind::Consumed, idx, eaten);
            }
            out.comfortDemand += demand;
            out.unmetComfort += demand - eaten;
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
    // season window (econ_consume_season) debits a whole season of bread on
    // the boundary day — a place seeded with less dies of arithmetic at its
    // first window, before it has lived a day. The larder IS a season.
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
