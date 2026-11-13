#include "macro/econ_day.h"
#include "macro/characters.h"  // kSquadBirth — амбар рождения рода
#include "macro/labour.h"   // souls_home / souls_flock — две двери душ места
#include "macro/upkeep_window.h"   // upkeep_bill — ОДИН счёт содержания (M-140)

#include "tables/faction.h"    // монетная семья фракции — ординалы номиналов
#include "macro/state.h"      // GameState — ведомость пишется в осёдлый сквад

#include <algorithm>
#include <cstring>
#include <vector>   // обратный мост ординалов — одна таблица, один резолв

namespace sm {

namespace {

// Recipe/need ids resolve once, lazily, into index tables — hot loops then
// touch integers only (the faction-registry pattern).
struct ResolvedRecipe {
    // КАТАЛОЖНЫЙ ординал выхода — и он, а не товарный, делает вещь. Рецепт
    // вправе выдавать строку, которой в товарной лестнице НЕТ: зелье — это
    // предмет со своим составом, а не ярус нужд (владелец 2026-09-18,
    // «поушоны будем варить»). (Товарный ординал выхода стоял здесь колонкой
    // без читателя и снят рядом M-486.)
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
            rr.outItem = item_index(kRecipes[i].output);
            rr.isMint = std::strcmp(kRecipes[i].output, kMintOutput) == 0;
        }
        return r;
    }();
    return t;
}

} // namespace

int craft_outputs(int mintFactionIdx, CraftOutput* out) {
    // Строки номиналов — ОРДИНАЛАМИ, резолв один раз на фракцию (ЗАКОН
    // СЛОВАРЯ п.1); золото первым.
    const std::array<int, 3>& mintRows = faction_mint_rows(mintFactionIdx);
    const ResolvedTables& t = resolved();
    int n = 0;
    const auto push = [&](int outIdx, std::uint8_t action) {
        // -1 = монетного двора нет или мёртвый id — fail closed.
        if (outIdx < 0) return;
        if (item_parts(outIdx).empty()) return;   // без состава нет партий
        const ItemDef* def = item_def_at(outIdx);
        if (!def || def->value <= 0) return;
        out[n++] = CraftOutput{outIdx, action};
    };
    for (int i = 0; i < kRecipeCount; ++i) {
        const ResolvedRecipe& rr = t.recipes[i];
        if (rr.isMint) {
            for (int m = 0; m < 3; ++m) push(mintRows[m], kRecipes[i].action);
        } else {
            push(rr.outItem, kRecipes[i].action);
        }
    }
    return n;
}

int econ_pay_debt(Inventory& store, std::int32_t* needDebt) {
    // ОДНА дверь гашения (CANON S10: «всё, что падает в него, идёт в уплату
    // долга»): склад платит по счёту, оплаченное СЪЕДЕНО — списано.
    // После вызова видимый склад — только излишек, и потому всё
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
    }
    return paid;
}

int econ_store_hygiene(Inventory& store) {
    // Slot hygiene stays DAILY while the balances went seasonal (CANON
    // «Крафт/Скрап»: авто-скрап ИИ по порогу >50%): a store clogged past the
    // half mark by non-fungible lut melts its cheapest pieces back to matter,
    // so bread and ore always have somewhere to land. Plain stacks are never
    // touched, and the PLAYER'S bag never passes through this function.
    return auto_scrap_overflow(store);
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
    return d && item_archetype(*d) == ObjectArchetype::Goods && d->value > 0;
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
            if (!d || item_archetype(*d) != ObjectArchetype::Food) continue;
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

// Строка каталога → ЕЁ рецепт, резолв один раз (тот же паттерн, что мост
// товаров ниже). Монетные строки отвечают минтом: его outItem = -1 («coin»
// не строка каталога), поэтому их вяжет архетип Currency, а не id.
int recipe_of_item(int itemIdx) {
    static const std::vector<std::int16_t> kBack = [] {
        std::vector<std::int16_t> b(item_catalog().size(), -1);
        int mintRow = -1;
        for (int ri = 0; ri < kRecipeCount; ++ri) {
            if (resolved().recipes[ri].isMint) { mintRow = ri; continue; }
            const int row = resolved().recipes[ri].outItem;
            if (row >= 0 && row < int(b.size())) {
                b[std::size_t(row)] = std::int16_t(ri);
            }
        }
        if (mintRow >= 0) {
            for (int i = 0; i < int(b.size()); ++i) {
                const ItemDef* d = item_def_at(i);
                if (d && item_archetype(*d) == ObjectArchetype::Currency) {
                    b[std::size_t(i)] = std::int16_t(mintRow);
                }
            }
        }
        return b;
    }();
    return (itemIdx >= 0 && itemIdx < int(kBack.size()))
        ? int(kBack[std::size_t(itemIdx)]) : -1;
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

void seed_rooted_inventory(Inventory& inv, int population,
                            const SquadBirthRow& row) {
    if (population <= 0) return;
    // Born MID-LIFE means born with LAST SEASON'S HARVEST IN THE BARN: the
    // first boundary (`upkeep_season_window`) bills a whole season of harch and
    // the larder pays it on the spot — a place seeded with less starts life
    // in debt and must out-produce it or bury the shortfall a season later.
    // The larder IS a season.
    constexpr int kSeedVitalDays = kDaysPerSeason;
    static_assert(kSeedVitalDays == kDaysPerSeason,
                  "the seed larder must survive the first season window");
    // ЧИСЛА — КОЛОНКИ СТРОКИ РОДА, А НЕ ВЕТКА ПО БУЛЕВУ РОДУ. Здесь стояло
    // `isCity ? 32 : 8`, то есть род, протащенный в фундаментальную дверь
    // аргументом, и дверь решала по нему сама (ЗАКОН СТРОКИ КАТАЛОГА).
    const int needDays = int(row.seedComfortDays);
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
    const int siteMult = int(row.seedRawMult);
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
