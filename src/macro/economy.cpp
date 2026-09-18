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

// The bargaining edge is the Trade ROW's percent (kSkillDefs), not a number
// of this file's own: the skill tooltip quotes the column, so the till must
// charge the column. An inline 0.02f lived here — a second truth the panel
// never promised (owner ruling 2026-09-06: торговля = 1%/ранг, таблица).
inline float bargaining_edge(int bargaining) {
    return skill_mult_of(SkillId::Trade, bargaining) - 1.0f;
}
} // namespace

int trade_buy_price(int basePrice, int charisma, int bargaining) {
    const float discount = 1.0f - (cha_trade_discount(charisma)
                                  + bargaining_edge(bargaining));
    const float scaled = static_cast<float>(basePrice) * std::max(0.5f, discount);
    return std::max(1, static_cast<int>(jround(scaled)));
}

int trade_sell_price(int basePrice, int charisma, int bargaining) {
    const float bonus = 1.0f + (cha_trade_discount(charisma)
                               + bargaining_edge(bargaining));
    const float scaled = static_cast<float>(basePrice) * 0.7f * std::min(1.5f, bonus);
    return std::max(1, static_cast<int>(jround(scaled)));
}

int trade_price(int baseValue, int charisma, int bargaining,
                       float contextMult, bool buying) {
    const int scaledBase = std::max(1,
        static_cast<int>(jround(static_cast<float>(baseValue) * contextMult)));
    return buying ? trade_buy_price(scaledBase, charisma, bargaining)
                  : trade_sell_price(scaledBase, charisma, bargaining);
}

float stock_scarcity(int supply, int demandPerDay) {
    // ОДНА кривая цены (CANON S10, пять законов): `value × (спрос+1)/(запас+1)`,
    // КОРИДОРА НЕТ. Спрос — СЕЗОННЫЙ: мир ест раз в сезон (S19.2), поэтому
    // мерка дефицита обязана совпасть с ритмом еды — дневная мерка читала
    // сезонный амбар как «завались» и опаздывала ровно на сезон (хутор с 400
    // хлеба при нужде 960 уходил в шахту и умирал). Обе половины спроса —
    // прямая лестница И производная по рецептам — сидят в demandPerDay
    // (daily_demand_for), так что горизонт у них один по построению (ловушка
    // S10 «переводить обе половины» закрыта дверью, а не дисциплиной).
    // Равновесие читаемо: склад == сезонная нужда ⇒ цена ровно по базе;
    // любое отклонение цены от базы означает «здесь не хватает» или «здесь
    // завал» — для каждого товара и места без исключений.
    // Коридор [0.25…4.0] умер вердиктом 2026-09-18: он не защищал от
    // «щедрого купца», а СОЗДАВАЛ его — в насыщении нет слиппеджа, и только
    // там прокрутка была прибыльной (price_law_test держит обратное).
    const long long s = (long long)(supply < 0 ? 0 : supply) + 1;
    const long long d =
        (long long)(demandPerDay < 0 ? 0 : demandPerDay) * kDaysPerSeason + 1;
    return float(d) / float(s);
}

int stock_price(int baseValue, int supply, int demandPerDay) {
    const float p = float(baseValue) * stock_scarcity(supply, demandPerDay);
    const int v = int(jround(p));
    return v < 1 ? 1 : v;
}

namespace {

// Demand is DIRECT (the needs ladder) plus DERIVED (owner track 2026-08-30):
// a town that eats bread demands grain, because bread is MADE of it — the
// demand of every recipe output flows down to its inputs × qty. Without
// this a starving city priced grain at base (nobody "eats" grain), its
// caravans saw no profit in hauling it, and stone outbid food (measured,
// balance_run). Recursive over the recipe table with a small depth cap:
// chains are data and may grow (ore → metal → tool), cycles must not hang.
int demand_for_(const char* itemId, int population, const Skills& hands,
                int depth) {
    if (!itemId || population <= 0) return 0;
    int demand = 0;
    for (int i = 0; i < kNeedCount; ++i) {
        if (std::strcmp(kNeeds[i].commodity, itemId) == 0) {
            demand += population / kNeeds[i].popPerUnitDay;
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
            for (const ItemPart& part : item_parts(item_index(r.output))) {
                if (int(part.def) != target) continue;
                demand += demand_for_(r.output, population, hands, depth - 1)
                          * int(part.count);
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

int daily_demand_for(const char* itemId, int population,
                     const Skills& hands) {
    // Depth 4 covers chains far past today's one-step recipes (ore → metal
    // → part → tool) and caps any future accidental cycle.
    const int direct = demand_for_(itemId, population, hands, 4);
    if (population <= 0) return direct;
    const int floorDemand = population / weakest_need_per_unit_day();
    return direct > floorDemand ? direct : floorDemand;
}


float mood_price_mult(SettlementMood mood, bool buying) {
    // A town's temper prices its market, and it prices BOTH sides of the deal
    // (the merchant-temperament column beside this one always did). The
    // numbers are columns of THE mood registry (state.h kMoodRows), beside
    // the band's label and everything else said about it.
    const MoodRow& r = mood_row(mood);
    return buying ? r.buyMul : r.sellMul;
}

float trait_price_mult(const ecs::NpcTraits* traits, bool buying) {
    // The merchant's temperament prices HIS side of the deal, and the numbers
    // are columns of THE trait registry (npc.h kTraitPriceRows), beside the
    // temper's name — the same shape as the mood registry above. A greedy man
    // charges more and pays less; a generous one the reverse; every other
    // temper has no opinion about money and its row says so.
    auto has = [&](NPCTrait t) {
        if (!traits) return false;
        const auto raw = std::uint8_t(t);
        for (std::uint8_t i = 0; i < traits->count && i < 2; ++i) {
            if (traits->traits[i] == raw) return true;
        }
        return false;
    };
    float mult = 1.0f;
    for (const TraitPriceRow& r : kTraitPriceRows) {
        if (!r.pricesMarket || !has(r.trait)) continue;
        mult = buying ? r.buyMul : r.sellMul;
    }
    return mult;
}

} // namespace sm
