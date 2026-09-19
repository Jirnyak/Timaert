#include "macro/economy.h"
#include "macro/attributes.h"
#include "macro/state.h"
#include "ecs/components.h"
#include "macro/econ_day.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm {

namespace {
inline float jround(float v) { return std::round(v); }

// НАЦЕНКА = РАЗНИЦА ТОРГОВЫХ СИЛ, в долях (CANON S25). Положительная — моя
// сторона сильнее и наценивает в свою пользу; отрицательная — наценивает
// другая. Одно вычитание: никакого «кто здесь хозяин лавки».
inline float trade_edge(int myTradePct, int theirTradePct) {
    return float(myTradePct - theirTradePct) * 0.01f;
}
} // namespace

int trade_buy_price(int basePrice, int myTradePct, int theirTradePct) {
    // Покупаю Я: сильнее — сбиваю цену, слабее — переплачиваю.
    const float scaled = float(basePrice)
                       * (1.0f - trade_edge(myTradePct, theirTradePct));
    return std::max(1, static_cast<int>(jround(scaled)));
}

int trade_sell_price(int basePrice, int myTradePct, int theirTradePct) {
    // Продаю Я: та же разница, другой знак — и ни одного второго числа
    // (×0.7 спреда не существует: у сделки нет дома, который брал бы своё).
    const float scaled = float(basePrice)
                       * (1.0f + trade_edge(myTradePct, theirTradePct));
    return std::max(1, static_cast<int>(jround(scaled)));
}

int trade_price(int baseValue, int myTradePct, int theirTradePct,
                bool buying) {
    const int base = std::max(1, baseValue);
    return buying ? trade_buy_price(base, myTradePct, theirTradePct)
                  : trade_sell_price(base, myTradePct, theirTradePct);
}

float stock_scarcity(int supply, int demandSeason) {
    // ОДНА кривая цены (CANON S10, пять законов): `value × (спрос+1)/(запас+1)`,
    // КОРИДОРА НЕТ. Спрос приходит СЕЗОННЫМ ЧИСЛОМ, и календаря в этой
    // двери больше нет (CANON S10, долг, 2026-09-19): прямая часть спроса =
    // остаток СЧЁТА места — уже сезонная величина, тающая по мере оплаты, —
    // производная по рецептам считается той же меркой (season_demand_for),
    // так что горизонт у обеих половин один по построению (ловушка S10
    // «переводить обе половины» закрыта дверью, а не дисциплиной).
    // Равновесие читаемо: склад == непокрытая нужда ⇒ цена ровно по базе;
    // любое отклонение цены от базы означает «здесь не хватает» или «здесь
    // завал» — для каждого товара и места без исключений.
    // Коридор [0.25…4.0] умер вердиктом 2026-09-18: он не защищал от
    // «щедрого купца», а СОЗДАВАЛ его — в насыщении нет слиппеджа, и только
    // там прокрутка была прибыльной (price_law_test держит обратное).
    const long long s = (long long)(supply < 0 ? 0 : supply) + 1;
    const long long d = (long long)(demandSeason < 0 ? 0 : demandSeason) + 1;
    return float(d) / float(s);
}

int stock_price(int baseValue, int supply, int demandSeason) {
    const float p = float(baseValue) * stock_scarcity(supply, demandSeason);
    const int v = int(jround(p));
    return v < 1 ? 1 : v;
}

namespace {

// Demand is DIRECT (the place's DEBT, CANON S10 2026-09-19: «спрос кривой
// дефицита читается из ДОЛГА» — непогашенный остаток счёта И ЕСТЬ
// непокрытая нужда, сезонной меркой по построению) plus DERIVED (owner
// track 2026-08-30): a town that eats bread demands grain, because bread
// is MADE of it — the demand of every recipe output flows down to its
// inputs × qty. Without this a starving city priced grain at base (nobody
// "eats" grain), its caravans saw no profit in hauling it, and stone
// outbid food (measured, balance_run). Recursive over the recipe table
// with a small depth cap: chains are data and may grow (ore → metal →
// tool), cycles must not hang.
// Без счёта (needDebt == nullptr — снимок чужого дома, фикстура) прямая
// часть честно падает на лестницу населения × сезон.
int demand_for_(const char* itemId, const std::int32_t* needDebt,
                int population, const Skills& hands,
                const Inventory* store, int depth) {
    if (!itemId || population <= 0) return 0;
    int demand = 0;
    for (int i = 0; i < kNeedCount; ++i) {
        if (std::strcmp(kNeeds[i].commodity, itemId) == 0) {
            demand += needDebt
                ? int(needDebt[commodity_index(itemId)])
                : (population / kNeeds[i].popPerUnitDay) * kDaysPerSeason;
            break;
        }
    }
    if (depth > 0) {
        const int target = item_index(itemId);
        for (const RecipeDef& r : kRecipes) {
            // Derived demand exists only where the recipe CAN run: hands
            // that bake nothing want no grain beyond their own needs,
            // however hungry their future bakery would be — without this
            // gate the growers' own granaries priced at the scarcity
            // ceiling and the caravans' loans bought a quarter of the lot
            // (measured, balance_run 2026-08-30).
            if (!recipe_known(hands, r.craft, r.minRank)) continue;
            // The recipe's matter = its output row's composition (the one
            // matter table, items.h). The mint's output is no catalog row
            // (-1 → empty span), and its silver demand was always zero:
            // nothing NEEDS coin down the needs ladder.
            const int outIdx = item_index(r.output);
            for (const ItemPart& part : item_parts(outIdx)) {
                if (int(part.def) != target) continue;
                // НЕТТИНГ СКЛАДОМ ВЫХОДА (владелец 2026-09-18, «смотреть
                // и на сезон, и на склад текущий»): вход нужен только на
                // НЕДОПЕЧЁННЫЙ остаток нужды выхода. Полный амбар хлеба не
                // хочет зерна — его пустая зерновая полка больше не
                // «дефицит» и не взрывает ни цену, ни скор рейса; пустой
                // амбар хочет в полную силу. С долгом обе величины —
                // СЕЗОННЫЕ ЧИСЛА по построению, дробь «туда-обратно» через
                // дневную мерку умерла вместе с календарём кривой.
                long long outSeason = demand_for_(r.output, needDebt,
                                                  population, hands,
                                                  store, depth - 1);
                if (store) {
                    outSeason -= store->count_of(outIdx);
                    if (outSeason < 0) outSeason = 0;
                }
                demand += int(outSeason) * int(part.count);
            }
        }
    }
    return demand;
}

}  // namespace

// НУЛЕВОГО СПРОСА НЕ БЫВАЕТ (владелец, 2026-09-18, дословно: «надо сделать,
// чтобы не было нулевого спроса — все ресурсы имеют спрос, универсальная
// система ресурсов; другой вопрос — большой или малый. И естественно спрос
// падает, если на складе много, но это уже должно быть тот же закон, что с
// ценой — единый»).
//
// ПОЧЕМУ ЭТО БЫЛО ДЕФЕКТОМ, И ЧИСЛОМ: спрос 0 у всего, что здесь не едят,
// ронял цену такого товара на ПОЛ кривой дефицита (0.25 × базы), а по этой
// цене аукцион артелей оценивает рейс. Поэтому деревня, СТОЯЩАЯ НА ЖЕЛЕЗЕ,
// никогда его не копала: зерно, которое она ест, всегда било металл, которым
// она не питается. Мир не добывал ни железа, ни серебра за 256 дней.
//
// ПОЛ СПРОСА ВЫВЕДЕН, А НЕ НАЗНАЧЕН: он равен самой СЛАБОЙ нужде лестницы
// (у статуи 1 на 512 жителей-дней) — «то, что никому не нужно, нужно так же
// редко, как самое редкое из нужного». Растёт лестница — двигается и пол;
// новой константы не рождается. Дальше всё делает ОДНА уже живущая кривая:
// склад полон — цена падает, склад пуст — растёт.
constexpr int weakest_need_per_unit_day() {
    int weakest = 1;
    for (const NeedDef& n : kNeeds) {
        if (n.popPerUnitDay > weakest) weakest = n.popPerUnitDay;
    }
    return weakest;
}

int season_demand_for(const char* itemId, const std::int32_t* needDebt,
                      int population, const Skills& hands,
                      const Inventory* store) {
    // Depth 4 covers chains far past today's one-step recipes (ore → metal
    // → part → tool) and caps any future accidental cycle.
    const int direct = demand_for_(itemId, needDebt, population, hands,
                                   store, 4);
    if (population <= 0) return direct;
    // Пол спроса — та же слабейшая нужда лестницы, сезонной меркой.
    const int floorDemand =
        (population / weakest_need_per_unit_day()) * kDaysPerSeason;
    return direct > floorDemand ? direct : floorDemand;
}


// (mood_price_mult и trait_price_mult вырезаны 2026-09-19 вместе со своими
// реестрами: цена не умножается на характер — она выводится из кривой и
// разницы торговых сил.)

} // namespace sm
