// Economy. Faithful port of `src/game/economy.ts` (lines 1-516).
//
// Behavioural parity is the priority. Math operations mirror the
// JavaScript semantics: `Math.round` rounds half away from zero,
// `Math.floor`/`Math.ceil`/`Math.sqrt` are the IEEE equivalents.

#include "macro/economy.h"

#include <algorithm>
#include <cmath>

namespace sm {

namespace {

constexpr float kGatherBase         = 0.3f;
constexpr float kProductionRate     = 0.15f;
constexpr float kPriceDecay         = 0.05f;
constexpr float kPriceFloor         = 1.0f;
constexpr float kPriceCeiling       = 500.0f;
constexpr float kDemandPerPop       = 0.01f;
constexpr float kTaxRate            = 0.001f;
constexpr float kConsumeRate        = 0.02f;
constexpr float kDistanceCostFactor = 0.0001f;
constexpr int   kCaravanCapacity    = 20;
constexpr int   kTradeCooldown      = 3;

// Math.round in TS / JS: round half away from zero for positive
// values. std::round matches that behaviour.
inline float jround(float v) { return std::round(v); }

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

} // namespace

EconomyState create_economy_state(std::vector<ResourceId> localResources) {
    EconomyState eco;
    eco.resources.fill(0.0f);
    eco.goods.fill(0.0f);
    for (std::size_t i = 0; i < kNumResources; ++i) {
        eco.resourcePrices[i] = kResourceBasePrice[i];
    }
    for (std::size_t g = 0; g < kNumGoods; ++g) {
        eco.goodPrices[g] = kGoods[g].baseValue;
    }
    eco.wealth         = 0.0f;
    eco.happiness      = 0.5f;
    eco.localResources = std::move(localResources);
    return eco;
}

// ── Resource gathering (villages) ────────────────────────────
void gather_resources(EconomyState& eco, int population) {
    const float workers = std::sqrt(static_cast<float>(population)) * eco.happiness;
    for (ResourceId r : eco.localResources) {
        const auto idx = static_cast<std::size_t>(r);
        const float amount = workers * kGatherBase * kResourceRarity[idx];
        eco.resources[idx] += amount;
    }
}

// ── Goods production (cities) ────────────────────────────────
void produce_goods(EconomyState& eco, int population) {
    const float efficiency = std::sqrt(static_cast<float>(population)) * kProductionRate;
    for (std::size_t gi = 0; gi < kNumGoods; ++gi) {
        const GoodDef& g = kGoods[gi];
        const float have1 = eco.resources[g.r1];
        const float have2 = eco.resources[g.r2];
        if (have1 >= 1.0f && have2 >= 1.0f) {
            const float batch = std::min({have1, have2, efficiency});
            eco.resources[g.r1] -= batch;
            eco.resources[g.r2] -= batch;
            eco.goods[gi]       += batch;
        }
    }
}

// ── Price model (local supply & demand) ──────────────────────
void update_prices(EconomyState& eco, int population, bool isCity) {
    const float baseDemand = static_cast<float>(population) * kDemandPerPop;

    // Resource prices
    for (std::size_t i = 0; i < kNumResources; ++i) {
        const float supply = eco.resources[i];
        const float demand = isCity ? baseDemand * 2.0f : baseDemand * 0.3f;
        const float ratio  = supply > 0.01f ? demand / supply : 10.0f;
        const float target = kResourceBasePrice[i] * ratio;
        eco.resourcePrices[i] += (target - eco.resourcePrices[i]) * kPriceDecay;
        eco.resourcePrices[i]  = clampf(eco.resourcePrices[i], kPriceFloor, kPriceCeiling);
    }

    // Good prices
    for (std::size_t gi = 0; gi < kNumGoods; ++gi) {
        const GoodDef& g = kGoods[gi];
        const float supply = eco.goods[gi];
        const float demand = baseDemand;
        const float ratio  = supply > 0.01f ? demand / supply : 10.0f;
        const float target = g.baseValue * ratio;
        eco.goodPrices[gi] += (target - eco.goodPrices[gi]) * kPriceDecay;
        eco.goodPrices[gi]  = clampf(eco.goodPrices[gi], kPriceFloor, kPriceCeiling);
    }
}

// ── Goods satisfaction → growth / happiness / wealth ─────────
TickResult consume_and_grow(EconomyState& eco, int population) {
    float growthSignal    = 0.0f;
    float happinessSignal = 0.0f;
    float wealthSignal    = 0.0f;

    const float pop = static_cast<float>(population);

    for (std::size_t gi = 0; gi < kNumGoods; ++gi) {
        const GoodDef& g = kGoods[gi];
        const float stock   = eco.goods[gi];
        const float consume = std::min(stock, pop * kConsumeRate);
        if (consume > 0.0f) {
            eco.goods[gi]   -= consume;
            growthSignal    += consume * g.growthK;
            wealthSignal    += consume * g.wealthK * eco.goodPrices[gi];
            happinessSignal += consume * g.happinessK;
        }
    }

    // Raw grain consumption
    const auto grainIdx = static_cast<std::size_t>(ResourceId::Grain);
    const float grainConsume = std::min(eco.resources[grainIdx], pop * kConsumeRate * 0.5f);
    if (grainConsume > 0.0f) {
        eco.resources[grainIdx] -= grainConsume;
        growthSignal            += grainConsume * 0.5f;
    }

    // Wealth from trade surplus + local production, with tax drain.
    eco.wealth += wealthSignal;
    eco.wealth *= (1.0f - kTaxRate);

    // Happiness — slow convergence based on goods satisfaction
    const float targetHappiness = std::min(1.0f,
        0.3f + happinessSignal / std::max(1.0f, pop * 0.1f));
    eco.happiness += (targetHappiness - eco.happiness) * 0.1f;
    eco.happiness  = clampf(eco.happiness, 0.1f, 1.0f);

    // Logistic population growth
    const float popCap     = 100.0f + eco.wealth * 0.5f;
    const float growthRate = 0.01f * growthSignal / std::max(1.0f, pop * 0.1f);
    const float logistic   = growthRate * (1.0f - pop / popCap);
    const int   popDelta   = static_cast<int>(jround(logistic * pop));
    return TickResult{popDelta};
}

// ── Trade ────────────────────────────────────────────────────
namespace {

// Sum cargo qty (mirrors `cargo.reduce((s,c) => s + c.qty, 0)`).
int cargo_total_qty(const std::vector<CargoEntry>& cargo) {
    int s = 0;
    for (const auto& c : cargo) s += c.qty;
    return s;
}

// Look up dest price for a cargo entry.
float dest_price_for(const EconomyState& dest, const CargoEntry& c) {
    return c.kindIsResource
        ? dest.resourcePrices[c.key]
        : dest.goodPrices[c.key];
}

} // namespace

TradeRoute find_best_trade_route(const TradeOrigin& origin,
                                 const std::vector<TradeDest>& destinations,
                                 bool isVillage,
                                 int currentDay,
                                 int lastTrade) {
    TradeRoute none{};
    none.valid = false;

    if (currentDay - lastTrade < kTradeCooldown) return none;

    const TradeDest* bestDest = nullptr;
    float bestProfit          = 0.0f;
    std::vector<CargoEntry> bestCargo;

    for (const auto& dest : destinations) {
        const float dx          = origin.x - dest.x;
        const float dy          = origin.y - dest.y;
        const float distSq      = dx * dx + dy * dy;
        const float distanceCost = distSq * kDistanceCostFactor;

        std::vector<CargoEntry> cargo;
        float totalProfit = 0.0f;

        if (isVillage) {
            for (std::size_t i = 0; i < kNumResources; ++i) {
                const float stock = origin.eco->resources[i];
                if (stock < 2.0f) continue;
                const float margin = dest.eco->resourcePrices[i]
                                     - origin.eco->resourcePrices[i]
                                     - distanceCost;
                if (margin > 0.0f) {
                    const float remaining = static_cast<float>(kCaravanCapacity - cargo_total_qty(cargo));
                    const float qtyf = std::min(stock * 0.5f, remaining);
                    if (qtyf >= 1.0f) {
                        const int qty = static_cast<int>(std::floor(qtyf));
                        cargo.push_back(CargoEntry{
                            static_cast<std::uint8_t>(i), true, qty,
                            origin.eco->resourcePrices[i]});
                        totalProfit += margin * static_cast<float>(qty);
                    }
                }
            }
        } else {
            // City exports goods
            for (std::size_t gi = 0; gi < kNumGoods; ++gi) {
                const float stock = origin.eco->goods[gi];
                if (stock < 2.0f) continue;
                const float margin = dest.eco->goodPrices[gi]
                                     - origin.eco->goodPrices[gi]
                                     - distanceCost;
                if (margin > 0.0f) {
                    const float remaining = static_cast<float>(kCaravanCapacity - cargo_total_qty(cargo));
                    const float qtyf = std::min(stock * 0.5f, remaining);
                    if (qtyf >= 1.0f) {
                        const int qty = static_cast<int>(std::floor(qtyf));
                        cargo.push_back(CargoEntry{
                            static_cast<std::uint8_t>(gi), false, qty,
                            origin.eco->goodPrices[gi]});
                        totalProfit += margin * static_cast<float>(qty);
                    }
                }
            }

            // Cities can also send surplus resources
            for (std::size_t i = 0; i < kNumResources; ++i) {
                const float stock = origin.eco->resources[i];
                if (stock < 5.0f) continue;
                const float margin = dest.eco->resourcePrices[i]
                                     - origin.eco->resourcePrices[i]
                                     - distanceCost;
                if (margin > 0.0f) {
                    const float remaining = static_cast<float>(kCaravanCapacity - cargo_total_qty(cargo));
                    const float qtyf = std::min(stock * 0.3f, remaining);
                    if (qtyf >= 1.0f) {
                        const int qty = static_cast<int>(std::floor(qtyf));
                        cargo.push_back(CargoEntry{
                            static_cast<std::uint8_t>(i), true, qty,
                            origin.eco->resourcePrices[i]});
                        totalProfit += margin * static_cast<float>(qty);
                    }
                }
            }
        }

        // Sort cargo by margin (best first)
        std::sort(cargo.begin(), cargo.end(),
            [&dest](const CargoEntry& a, const CargoEntry& b) {
                const float mA = dest_price_for(*dest.eco, a) - a.buyPrice;
                const float mB = dest_price_for(*dest.eco, b) - b.buyPrice;
                return mB < mA;
            });

        if (totalProfit > bestProfit && !cargo.empty()) {
            bestProfit = totalProfit;
            bestDest   = &dest;
            bestCargo  = std::move(cargo);
        }
    }

    if (bestDest == nullptr || bestCargo.empty()) return none;

    // Deduct cargo from origin
    for (const auto& c : bestCargo) {
        if (c.kindIsResource) {
            origin.eco->resources[c.key] -= static_cast<float>(c.qty);
        } else {
            origin.eco->goods[c.key]     -= static_cast<float>(c.qty);
        }
    }

    const float dx = origin.x - bestDest->x;
    const float dy = origin.y - bestDest->y;
    const int travelDays = std::max(1,
        static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy) / 50.0f)));

    TradeRoute route;
    route.originId   = origin.id;
    route.destId     = bestDest->id;
    route.cargo      = std::move(bestCargo);
    route.arrivalDay = currentDay + travelDays;
    route.valid      = true;
    return route;
}

float settle_trade_route(const TradeRoute& route,
                         EconomyState& dest,
                         EconomyState& origin) {
    float revenue = 0.0f;
    for (const auto& c : route.cargo) {
        const float sellPrice = c.kindIsResource
            ? dest.resourcePrices[c.key]
            : dest.goodPrices[c.key];
        revenue += sellPrice * static_cast<float>(c.qty);

        if (c.kindIsResource) {
            dest.resources[c.key] += static_cast<float>(c.qty);
        } else {
            dest.goods[c.key]     += static_cast<float>(c.qty);
        }
    }

    float cost = 0.0f;
    for (const auto& c : route.cargo) cost += c.buyPrice * static_cast<float>(c.qty);
    origin.wealth += std::max(0.0f, revenue - cost);
    return revenue;
}

// ── Player trade helpers ─────────────────────────────────────
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

// ── Biome → resource mapping ─────────────────────────────────
std::vector<ResourceId> resources_for_terrain(float heightNorm, int terrainIndex) {
    std::vector<ResourceId> result;

    int ti;
    if (terrainIndex >= 0) {
        ti = terrainIndex;
    } else if (heightNorm > 0.75f) {
        ti = 4;
    } else if (heightNorm > 0.5f) {
        ti = 3;
    } else if (heightNorm < 0.25f) {
        ti = 1;
    } else {
        ti = 2;
    }

    for (std::size_t i = 0; i < kNumResources; ++i) {
        for (int t : kResourceTerrain[i]) {
            if (t < 0) break;
            if (t == ti) {
                result.push_back(static_cast<ResourceId>(i));
                break;
            }
        }
    }

    // Fertile fallback — at least grain.
    if (result.empty() && (ti == 2 || ti == 3)) {
        result.push_back(ResourceId::Grain);
    }

    // Mountains above 0.7 height get Iron bonus.
    if (heightNorm > 0.7f) {
        const auto ironId = ResourceId::Iron;
        if (std::find(result.begin(), result.end(), ironId) == result.end()) {
            result.push_back(ironId);
        }
    }

    return result;
}

} // namespace sm
