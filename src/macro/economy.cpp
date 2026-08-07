#include "macro/economy.h"

#include <algorithm>
#include <cmath>

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

} // namespace sm
