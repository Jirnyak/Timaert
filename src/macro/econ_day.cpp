#include "macro/econ_day.h"
#include "macro/labour.h"   // souls_home / souls_flock — две двери душ места
#include "macro/roster_window.h"   // roster_bill — ОДИН счёт содержания (M-140)

#include "macro/economy.h"    // stock_price — ranking asks THE price law
#include "tables/faction.h"    // монетная семья фракции — ординалы номиналов
#include "macro/state.h"      // GameState/Landmark — ведомость пишется в место
#include "macro/characters.h" // landmark_sheet — анкета места судит спрос

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>   // обратный мост ординалов — одна таблица, один резолв

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
    // Строки номиналов — ОРДИНАЛАМИ, резолв один раз на фракцию (ЗАКОН
    // СЛОВАРЯ п.1: три хеш-поиска на КАЖДОЕ место-день были тиком, а не
    // проводкой). Таблица выводится из того же реестра фракций.
    const std::array<int, 3>& mintRows = faction_mint_rows(mintFactionIdx);
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
            season_demand_for(outIdx, needDebt, population, hands, &store),
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
            report(sink, user, EconFact::Kind::Minted,
                   commodity_of_item(int(item_parts(cd.outIdx)[0].def)),
                   units);
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
    // Проход по ВСЕМУ словарю, а не по списку нужд: дверь гашения не обязана
    // знать, кто и по какому закону выставил счёт — она платит по тому, что
    // выставлено (ЗАКОН АГНОСТИЧНОСТИ). Строка без счёта стоит нулём и
    // пропускается первой же проверкой.
    int paid = 0;
    for (int idx = 0; idx < kCommodityCount; ++idx) {
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
    if (population <= 0) {
        // Мёртвое место — не должник: счёт закрывается вместе с жизнью, и
        // закрывается ЦЕЛИКОМ — проход по словарю, а не по списку нужд.
        for (int c = 0; c < kCommodityCount; ++c) needDebt[c] = 0;
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
    // РАЦИОН СПРАШИВАЕТСЯ У СТРОКИ, А НЕ У ЧИСЛА ДУШ (вердикт владельца
    // 2026-09-30, дословно: «с чего это лошади не едят если в таблице
    // объектов они ещё как едят! убрать все хардкоды — агностичный сквад,
    // все кто в нём едят по таблице по системе; никаких особенностей
    // лошадей»). Счёт голода места есть ТОТ ЖЕ `roster_bill`, которым
    // судится отряд в поле, — этим закрыт наряд M-140 «свести рот места и
    // рот отряда в один»: раньше здесь стоял `population × kDaysPerSeason`,
    // то есть «один юнит на душу в день» ЛИТЕРАЛОМ, и колонка `boardPerDay`
    // всей таблицы существ молча не читалась.
    //
    // СЛЕДСТВИЕ, КОТОРОЕ ЭТИМ ПОКУПАЕТСЯ: предел ТАБУНА места стал
    // эмерджентным. Прежде его держал потолок `garrison_cap_` — ровно тот
    // костыль, что запрещён ЗАКОНОМ КЛАМПА («потолок численности МАСКИРУЕТ
    // отсутствие обратной связи»); теперь лишняя лошадь просто ЕСТ, счёт
    // растёт, и недоимка забирает головы. Предел наступает через мир.
    const int headsBefore = creature_heads(store);
    const std::int64_t boardSeason = roster_bill(store).board;
    // Сезонный рацион ОДНОЙ головы этого места — среднее по её же составу:
    // оба числа из одних данных, ни одной назначенной константы.
    const int perHeadSeason = headsBefore > 0
        ? std::max<int>(1, int(boardSeason / headsBefore))
        : kDaysPerSeason;
    int deaths = 0;
    // ДОЛЯ КОМФОРТА МЕРЯЕТСЯ СТОИМОСТЬЮ, А НЕ ШТУКАМИ (вердикт владельца
    // 2026-09-26, подтверждён прямо). Бюджет горожанина задан в стоимости,
    // значит и «сколько из него не покрыто» — стоимость: в штуках кирпич
    // весил бы столько же, сколько статуя, и рост судил бы не то.
    long long comfortValue = 0;
    long long unmetValue = 0;
    const int hungerOrd = hunger_commodity_ordinal();
    for (int c = 0; c < kCommodityCount; ++c) {
        const std::int32_t remaining = needDebt[c];
        if (c == hungerOrd) {
            // Сколько РТОВ не прокормлено: недоимка, делённая на сезонный
            // рацион рта. Смерть снимает ЛЮБУЮ голову — «никаких
            // особенностей лошадей» (вердикт): голодный двор теряет и
            // человека, и скотину, по своей же таблице.
            deaths = std::min(headsBefore, int(remaining / perHeadSeason));
            continue;
        }
        const int demand = season_comfort_units(population, c);
        if (demand <= 0) continue;
        const ItemDef* def = item_def_at(commodity_item_index(c));
        const long long unit = def ? def->value : 0;
        comfortValue += (long long)demand * unit;
        unmetValue += (long long)(remaining < demand ? remaining : demand)
                      * unit;
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
    // Доля прокормленных — по РТАМ этого места: счёт выставлен ртам, и
    // судить его людьми значило бы мерить оплату не тем, что оплачивалось.
    const float foodShare = headsBefore > 0
        ? float(headsBefore - deaths) / float(headsBefore)
        : 0.0f;
    const float comfortShare = comfortValue > 0
        ? 1.0f - float(unmetValue) / float(comfortValue)
        : 1.0f;
    out.wellbeing = foodShare * comfortShare;
    // 2. ВЗЫСКАНИЕ ИСПОЛНЯЕТСЯ ЗДЕСЬ — головы снимает та же дверь, что их
    // судила. Иначе новый счёт ниже пришлось бы выставлять по составу,
    // которого уже нет, — и он врал бы ровно на съеденных.
    if (deaths > 0) bleed_heads(store, deaths);
    // 3. НОВЫЙ СЧЁТ — по ФАКТИЧЕСКОМУ составу, перезаписью: старый долг не
    // переносится (взыскали — выставили новый). Голод спрашивает таблицу
    // (`roster_bill` по живым головам), комфорт — людей: бюджет горожанина
    // есть свойство человека, и лошадь его не тратит.
    const int popAfter = std::max(0, count_human_souls(store));
    const std::int64_t boardAfter = roster_bill(store).board;
    for (int c = 0; c < kCommodityCount; ++c) {
        needDebt[c] = std::int32_t(c == hungerOrd
            ? boardAfter
            : season_comfort_units(popAfter, c));
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

// Обратный конец того же моста, О(1) по каталожному ординалу. Резолв один раз
// за процесс (таблица каталога неизменна), дальше — чтение массива: строковый
// поиск товара по строке каталога был в топ-10 самых тяжёлых вызовов
// симуляции (замер PMU 2026-09-24).
const std::array<int, 3>& faction_mint_rows(int factionIdx) {
    static const std::vector<std::array<int, 3>> kRows = [] {
        // +1 слот: последний — имперская семья, ответ для индекса вне реестра
        // (ровно fallback `faction_coins`, не второй закон).
        std::vector<std::array<int, 3>> rows(std::size_t(kFactionCount) + 1);
        for (int f = 0; f <= kFactionCount; ++f) {
            const char* const* coins = faction_coins(f);
            for (int i = 0; i < 3; ++i) {
                rows[std::size_t(f)][std::size_t(i)] =
                    item_index(coins[2 - i]);
            }
        }
        return rows;
    }();
    static const std::array<int, 3> kNoMint{-1, -1, -1};
    if (factionIdx < 0) return kNoMint;
    const int slot = factionIdx < kFactionCount ? factionIdx : kFactionCount;
    return kRows[std::size_t(slot)];
}

// ── ФОРМУЛА НУЖДЫ: ОДНА ДВЕРЬ, ТРИ ВОПРОСА (M-137) ───────────────────────

bool commodity_is_comfort(int commodityIdx) {
    const ItemDef* d = item_def_at(commodity_item_index(commodityIdx));
    return d && d->type == ItemType::Goods && d->value > 0;
}

int comfort_row_count() {
    static const int kCount = [] {
        int n = 0;
        for (int c = 0; c < kCommodityCount; ++c) {
            if (commodity_is_comfort(c)) ++n;
        }
        return n;
    }();
    return kCount;
}

int season_comfort_units(int population, int commodityIdx) {
    if (population <= 0) return 0;
    if (!commodity_is_comfort(commodityIdx)) return 0;
    const int rows = comfort_row_count();
    if (rows <= 0) return 0;
    const ItemDef* d = item_def_at(commodity_item_index(commodityIdx));
    // ОДНО деление на всё: население × сезон × бюджет / (строк × стоимость).
    const long long units = (long long)population * kDaysPerSeason
                          * kComfortValuePerPopDay
                          / ((long long)rows * d->value);
    return int(units);
}

int hunger_commodity_ordinal() {
    static const int kOrd = [] {
        int found = -1, count = 0;
        for (int c = 0; c < kCommodityCount; ++c) {
            const ItemDef* d = item_def_at(commodity_item_index(c));
            if (!d || d->type != ItemType::Food) continue;
            if (found < 0) found = c;
            ++count;
        }
        // Ровно одна пищевая строка словаря — закон, прибитый static_assert'ом
        // в items.cpp (там каталог виден компилятору). Здесь fail-closed на
        // случай, если словарь и каталог разъедутся правкой одного из них.
        return count == 1 ? found : -1;
    }();
    return kOrd;
}

int hunger_item_index() { return commodity_item_index(hunger_commodity_ordinal()); }

int recipe_out_item(int recipeRow) {
    return (recipeRow >= 0 && recipeRow < kRecipeCount)
        ? resolved().recipes[recipeRow].outItem : -1;
}

int commodity_of_item(int itemIdx) {
    static const std::vector<std::int16_t> kBack = [] {
        std::vector<std::int16_t> b(item_catalog().size(), -1);
        for (int i = 0; i < kCommodityCount; ++i) {
            const int row = commodity_item_index(i);
            if (row >= 0 && row < int(b.size())) {
                b[std::size_t(row)] = std::int16_t(i);
            }
        }
        return b;
    }();
    return (itemIdx >= 0 && itemIdx < int(kBack.size()))
        ? int(kBack[std::size_t(itemIdx)]) : -1;
}

void seed_landmark_inventory(Inventory& inv, int population, bool isCity) {
    if (population <= 0) return;
    // Born MID-LIFE means born with LAST SEASON'S HARVEST IN THE BARN: the
    // first boundary (econ_debt_boundary) bills a whole season of harch and
    // the larder pays it on the spot — a place seeded with less starts life
    // in debt and must out-produce it or bury the shortfall a season later.
    // The larder IS a season.
    constexpr int kSeedVitalDays = kDaysPerSeason;
    static_assert(kSeedVitalDays == kDaysPerSeason,
                  "the seed larder must survive the first season window");
    const int needDays = isCity ? 32 : 8;   // a season / days
    const int hungerOrd = hunger_commodity_ordinal();
    for (int c = 0; c < kCommodityCount; ++c) {
        // ГОЛОД — своя система (сезон харча), комфорт — доля бюджета за
        // столько дней, сколько живёт амбар этого рода места. Долю считает
        // ОДНА дверь (season_comfort_units), поэтому «сколько дней» осталось
        // единственным числом редактора здесь.
        const int qty = c == hungerOrd
            ? population * kSeedVitalDays
            : (season_comfort_units(population, c) * needDays)
                  / kDaysPerSeason;
        if (qty > 0) inv.add_of(commodity_item_index(c), qty);
    }
    // Raw buffers per head — {commodity, units·population >> shift}. A
    // Village, whose whole business is raw, holds double.
    struct RawSeed { const char* id; int shift; };
    constexpr RawSeed kRawSeeds[] = {
        // ПИЩА УШЛА ИЗ СЫРЬЕВЫХ БУФЕРОВ (2026-09-20): она больше не материал,
        // а голодная строка лестницы — и та уже выдаёт новорождённому месту
        // СЕЗОН харча выше. Оставь её здесь — место родится с сезоном плюс
        // ещё одним днём, и закон «амбар рождения = ровно сезон» тихо врёт.
        {"wood", 0}, {"fibre", 1}, {"stone", 1}, {"clay", 2}, {"iron", 3},
    };
    const int siteMult = isCity ? 1 : 2;
    for (const RawSeed& r : kRawSeeds) {
        const int qty = (population >> r.shift) * siteMult;
        if (qty > 0) inv.add(r.id, qty);
    }
    // КАЗНА ПРИ РОЖДЕНИИ — ПУСТА (M-139, вердикт владельца 2026-09-26,
    // вариант «в»: «снести всё и держать дыру открытой до пула лута»). Здесь
    // печаталась казна «от популяции ± четверть разброса от мирового сида»
    // тремя номиналами фракции — вторая дверь появления стоимости рядом с
    // чеканкой. Новорождённое место выходит в мир со СКЛАДОМ и без монет:
    // деньги приходят чеканкой из серебра, а покупает место по плотности
    // стоимости (`currency.h densest_value_slot`) — ЧИСТЫЙ БАРТЕР, пока
    // кузнец не дорастёт до монетного ранга. Дыра названа в M-139 и ждёт
    // пула лута; параметры `factionIdx`/`seedSalt` ушли вместе с блоком —
    // читать их больше нечему.
}


} // namespace sm
