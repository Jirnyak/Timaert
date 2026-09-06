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
    const float s = float((supply < 0 ? 0 : supply) + 1);
    const float d = float((demandPerDay < 0 ? 0 : demandPerDay) + 1);
    const float scarcity = d / s;
    return scarcity < 0.25f ? 0.25f : (scarcity > 4.0f ? 4.0f : scarcity);
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
int demand_for_(const char* itemId, int population, EconSite site,
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
        for (const RecipeDef& r : kRecipes) {
            // Derived demand exists only where the recipe CAN run: a
            // village that bakes nothing wants no grain beyond its own
            // needs, however hungry its future bakery would be — without
            // this gate the growers' own granaries priced at the scarcity
            // ceiling and the caravans' loans bought a quarter of the lot
            // (measured, balance_run 2026-08-30).
            if (!recipe_runs_at(r.site, site)) continue;
            for (const RecipeInput& in : r.inputs) {
                if (!in.id || std::strcmp(in.id, itemId) != 0) continue;
                demand += demand_for_(r.output, population, site, depth - 1)
                          * in.qty;
            }
        }
    }
    return demand;
}

}  // namespace

int daily_demand_for(const char* itemId, int population, EconSite site) {
    // Depth 4 covers chains far past today's one-step recipes (ore → metal
    // → part → tool) and caps any future accidental cycle.
    return demand_for_(itemId, population, site, 4);
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
    // The merchant's temperament: a greedy one charges more and pays less,
    // a generous one the reverse.
    auto has = [&](NPCTrait t) {
        if (!traits) return false;
        const auto raw = std::uint8_t(t);
        for (std::uint8_t i = 0; i < traits->count && i < 2; ++i) {
            if (traits->traits[i] == raw) return true;
        }
        return false;
    };
    float mult = 1.0f;
    if (has(NPCTrait::Greedy))   mult = buying ? 1.2f : 0.8f;
    if (has(NPCTrait::Generous)) mult = buying ? 0.9f : 1.2f;
    return mult;
}

} // namespace sm
