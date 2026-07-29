// Economy. Faithful port of `src/game/economy.ts`.
//
// Six resources × fifteen good-pairs (i<j combinations).
// Prices are floats; stocks are floats (TS uses Number for both).
// All constants and formulas mirror the TS module 1:1.

#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace sm {

// ── Resources ────────────────────────────────────────────────
// Order is authoritative — used for indexing every per-resource
// array in this module and in saves.
enum class ResourceId : std::uint8_t {
    Grain = 0, Wood, Iron, Clay, Silver, Gems, Count
};

inline constexpr std::size_t kNumResources = static_cast<std::size_t>(ResourceId::Count);

inline constexpr const char* kResourceNames[kNumResources] = {
    "Grain", "Wood", "Iron", "Clay", "Silver", "Gems"
};

// Rarity 0-1 (lower = rarer). TS `RESOURCE_RARITIES`.
inline constexpr float kResourceRarity[kNumResources] = {
    0.5f, 0.4f, 0.3f, 0.3f, 0.2f, 0.1f
};

// Base price = round(1 / (rarity × 2)). TS `buildBasePrice()`.
inline constexpr float kResourceBasePrice[kNumResources] = {
    1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 5.0f
};

// Terrain affinity. terrainIndex 0=water 1=sand 2=grass 3=dirt 4=mount
// 5=snow 6=jungle 7=swamp 8=tundra. -1 = list terminator.
inline constexpr int kResourceTerrain[kNumResources][4] = {
    {2, 3, -1, -1},  // Grain
    {2, 6,  8, -1},  // Wood
    {4, 3, -1, -1},  // Iron
    {1, 7, -1, -1},  // Clay
    {4, 5, -1, -1},  // Silver
    {4,-1, -1, -1},  // Gems
};

// ── Goods ────────────────────────────────────────────────────
// Auto-generated from resource pairs (i<j) — 15 entries in TS order.
inline constexpr std::size_t kNumGoods = 15;

struct GoodDef {
    const char* id;        // "Grain+Wood", etc.
    const char* name;      // hand-tuned label
    std::uint8_t r1;       // ResourceId
    std::uint8_t r2;
    float baseValue;       // round(1 / (rarity_r1 × rarity_r2 × 10))
    float growthK;
    float wealthK;
    float happinessK;
};

// Order matches the nested loop `for i { for j>i { ... } }` in TS.
inline constexpr GoodDef kGoods[kNumGoods] = {
    {"Grain+Wood",    "Bread",      0, 1, 1.0f, 0.8f, 0.1f, 0.1f},
    {"Grain+Iron",    "Tools",      0, 2, 1.0f, 0.6f, 0.3f, 0.1f},
    {"Grain+Clay",    "Bricks",     0, 3, 1.0f, 0.5f, 0.4f, 0.1f},
    {"Grain+Silver",  "Coins",      0, 4, 1.0f, 0.2f, 0.7f, 0.1f},
    {"Grain+Gems",    "Jewelry",    0, 5, 2.0f, 0.1f, 0.3f, 0.6f},
    {"Wood+Iron",     "Weapons",    1, 2, 1.0f, 0.1f, 0.6f, 0.3f},
    {"Wood+Clay",     "Furniture",  1, 3, 1.0f, 0.4f, 0.5f, 0.1f},
    {"Wood+Silver",   "Silverware", 1, 4, 1.0f, 0.1f, 0.5f, 0.4f},
    {"Wood+Gems",     "Ornaments",  1, 5, 3.0f, 0.1f, 0.4f, 0.5f},
    {"Iron+Clay",     "Tiles",      2, 3, 1.0f, 0.3f, 0.5f, 0.2f},
    {"Iron+Silver",   "Armor",      2, 4, 2.0f, 0.1f, 0.5f, 0.4f},
    {"Iron+Gems",     "Crowns",     2, 5, 3.0f, 0.0f, 0.3f, 0.7f},
    {"Clay+Silver",   "Pottery",    3, 4, 2.0f, 0.2f, 0.4f, 0.4f},
    {"Clay+Gems",     "Sculptures", 3, 5, 3.0f, 0.1f, 0.3f, 0.6f},
    {"Silver+Gems",   "Regalia",    4, 5, 5.0f, 0.0f, 0.2f, 0.8f},
};

// ── EconomyState (per settlement) ────────────────────────────
struct EconomyState {
    std::array<float, kNumResources> resources{}; // stock per resource
    std::array<float, kNumGoods>     goods{};     // stock per good
    std::array<float, kNumResources> resourcePrices{};
    std::array<float, kNumGoods>     goodPrices{};
    float wealth    = 0.0f;
    float happiness = 0.5f;
    // Resources this settlement can gather (villages). Cities = empty.
    std::vector<ResourceId> localResources;
};

EconomyState create_economy_state(std::vector<ResourceId> localResources = {});

// ── Per-tick simulation ──────────────────────────────────────
void  gather_resources(EconomyState& eco, int population);
void  produce_goods   (EconomyState& eco, int population);
void  update_prices   (EconomyState& eco, int population, bool isCity);

struct TickResult { int popDelta; };
TickResult consume_and_grow(EconomyState& eco, int population);

// ── Trade ────────────────────────────────────────────────────
struct CargoEntry {
    // key is either a ResourceId (kindIsResource=true) or a good index.
    std::uint8_t key;
    bool         kindIsResource;
    int          qty;
    float        buyPrice;
};

struct TradeRoute {
    int  originId;
    int  destId;
    std::vector<CargoEntry> cargo;
    int  arrivalDay;
    bool valid = false;
};

struct TradeOrigin {
    int   id;
    float x;
    float y;
    EconomyState* eco;
};

struct TradeDest {
    int   id;
    float x;
    float y;
    EconomyState* eco;
};

// Returns route with `valid=false` when no profitable route or cooldown
// not yet elapsed (TS returns undefined).
TradeRoute find_best_trade_route(const TradeOrigin& origin,
                                 const std::vector<TradeDest>& destinations,
                                 bool isVillage,
                                 int currentDay,
                                 int lastTrade);

// Deliver cargo at destination, return revenue to origin wealth.
float settle_trade_route(const TradeRoute& route,
                         EconomyState& dest,
                         EconomyState& origin);

// ── Player trade helpers ─────────────────────────────────────
int player_buy_price (int basePrice, int charisma, int bargaining);
int player_sell_price(int basePrice, int charisma, int bargaining);

// ── Biome → resource mapping for village placement ───────────
// `terrainIndex < 0` means "derive from heightNorm". Returns sorted
// list of gatherable resources.
std::vector<ResourceId> resources_for_terrain(float heightNorm, int terrainIndex = -1);

} // namespace sm
