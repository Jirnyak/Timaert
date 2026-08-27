#include "macro/economy.h"
#include "macro/state.h"
#include "ecs/components.h"
#include "macro/econ_day.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm {

namespace {
inline float jround(float v) { return std::round(v); }
} // namespace

int player_buy_price(int basePrice, int charisma, int bargaining) {
    const float discount = 1.0f - (static_cast<float>(charisma) * 0.01f
                                  + static_cast<float>(bargaining) * 0.02f);
    const float scaled = static_cast<float>(basePrice) * std::max(0.5f, discount);
    return std::max(1, static_cast<int>(jround(scaled)));
}

int player_sell_price(int basePrice, int charisma, int bargaining) {
    const float bonus = 1.0f + (static_cast<float>(charisma) * 0.01f
                               + static_cast<float>(bargaining) * 0.02f);
    const float scaled = static_cast<float>(basePrice) * 0.7f * std::min(1.5f, bonus);
    return std::max(1, static_cast<int>(jround(scaled)));
}

int player_trade_price(int baseValue, int charisma, int bargaining,
                       float contextMult, bool buying) {
    const int scaledBase = std::max(1,
        static_cast<int>(jround(static_cast<float>(baseValue) * contextMult)));
    return buying ? player_buy_price(scaledBase, charisma, bargaining)
                  : player_sell_price(scaledBase, charisma, bargaining);
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

int daily_demand_for(const char* itemId, int population) {
    if (!itemId || population <= 0) return 0;
    for (int i = 0; i < kNeedCount; ++i) {
        if (std::strcmp(kNeeds[i].commodity, itemId) == 0) {
            return population / kNeeds[i].popPerUnitDay;
        }
    }
    return 0;
}


float mood_price_mult(SettlementMood mood, bool buying) {
    // A town's temper prices its market, and it prices BOTH sides of the deal
    // (the merchant-temperament column beside this one always did): a
    // prosperous town sells cheap and pays well because it has coin; a town in
    // revolt charges a risk premium and haggles the traveller down. Data, one
    // row per band, mirroring trait_price_mult's shape.
    switch (mood) {
        case SettlementMood::Prosperous: return buying ? 0.9f : 1.1f;
        case SettlementMood::Unrest:     return buying ? 1.2f : 0.85f;
        case SettlementMood::Revolt:     return buying ? 1.4f : 0.7f;
        default:                         return 1.0f;
    }
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
