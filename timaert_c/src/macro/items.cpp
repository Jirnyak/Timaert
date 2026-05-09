// Faithful port of `src/game/items.ts` (catalog + loot tables + helpers).
//
// All numeric constants, ids, chances, min/max ranges, and gold formulas
// are copied verbatim from the TS source. Adding/removing entries here
// must mirror the TS file 1:1.

#include "macro/items.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace sm {

namespace {

// ── Item catalog ───────────────────────────────────────────────
// Order copied verbatim from TS `ITEM_CATALOG`.

constexpr ItemDef kCatalog[] = {
    // Currency / Resources
    {"gold",        "Gold",            ItemType::Misc,        1, 0.01f, "\xF0\x9F\xAA\x99",
        "Universal currency", {}},

    // Consumables
    {"potion_hp",   "Health Potion",   ItemType::Potion,     50, 0.30f, "\xE2\x9D\xA4",
        "Restores 30 HP", {30, 0, 0, 0, 0, 0}},
    {"potion_mp",   "Mana Potion",     ItemType::Potion,     75, 0.30f, "\xE2\x9C\xA8",
        "Restores 15 MP", { 0,15, 0, 0, 0, 0}},
    {"food_bread",  "Bread",           ItemType::Food,       10, 0.10f, "\xF0\x9F\x8D\x9E",
        "Restores 10 HP", {10, 0, 0, 0, 0, 0}},
    {"food_meat",   "Raw Meat",        ItemType::Food,       15, 0.50f, "\xF0\x9F\x8D\x96",
        "Restores 15 HP", {15, 0, 0, 0, 0, 0}},

    // Materials
    {"mat_wood",    "Wood",            ItemType::Material,    5, 2.00f, "\xF0\x9F\xAA\xB5",
        "Building material", {}},
    {"mat_iron",    "Iron Ore",        ItemType::Material,   15, 3.00f, "\xE2\x9B\x8F",
        "Smithing material", {}},
    {"mat_bone",    "Bone",            ItemType::Material,    6, 0.50f, "\xF0\x9F\xA6\xB4",
        "Crafting material from monsters", {}},
    {"mat_hide",    "Hide",            ItemType::Material,   12, 1.00f, "\xF0\x9F\xA7\xB5",
        "Tanned animal hide", {}},
    {"mat_herb",    "Herb",            ItemType::Material,    8, 0.05f, "\xF0\x9F\x8C\xBF",
        "Alchemy ingredient", {}},

    // Equipment
    {"wpn_dagger",  "Rusty Dagger",    ItemType::Weapon,     30, 1.00f, "\xF0\x9F\x97\xA1",
        "+2 STR when equipped", {0, 0, 0, 2, 0, 0}},
    {"arm_leather", "Leather Armor",   ItemType::Armor,      60, 5.00f, "\xF0\x9F\x9B\xA1",
        "+2 END when equipped", {0, 0, 0, 0, 2, 0}},

    // Valuables
    {"misc_gem",    "Gemstone",        ItemType::Misc,      100, 0.05f, "\xF0\x9F\x92\x8E",
        "Valuable gem, can be sold", {}},
};

const std::unordered_map<std::string, const ItemDef*>& catalog_map() {
    static const std::unordered_map<std::string, const ItemDef*> m = []{
        std::unordered_map<std::string, const ItemDef*> r;
        for (const auto& d : kCatalog) r.emplace(d.id, &d);
        return r;
    }();
    return m;
}

// ── Loot tables (copied verbatim from TS) ──────────────────────

struct LootEntry {
    const char* item;
    float       chance;
    int         min;
    int         max;
    int         minLevel;  // 0 = unrestricted
};

// NPC_LOOT[npcType] — keys 0-7 mirror NPCType enum in `npc.h`:
// 0 Peasant, 1 Woodcutter, 2 Merchant, 3 Caravan, 4 Bandit, 5 Guard,
// 6 Witch, 7 Sorceress.

constexpr LootEntry kPeasantLoot[] = {
    {"food_bread", 0.6f, 1, 3, 0},
    {"mat_wood",   0.4f, 1, 4, 0},
    {"mat_herb",   0.2f, 1, 2, 0},
};
constexpr LootEntry kWoodcutterLoot[] = {
    {"mat_wood",   1.0f, 2, 7, 0},
    {"food_bread", 0.5f, 1, 2, 0},
};
constexpr LootEntry kMerchantLoot[] = {
    {"potion_hp",  0.7f, 1, 3, 0},
    {"food_bread", 0.6f, 2, 6, 0},
    {"potion_mp",  0.5f, 1, 2, 0},
    {"mat_iron",   0.4f, 1, 3, 0},
    {"misc_gem",   0.3f, 1, 1, 0},
    {"wpn_dagger", 0.2f, 1, 1, 0},
};
constexpr LootEntry kCaravanLoot[] = {
    {"food_bread", 1.0f, 3, 7, 0},
    {"potion_hp",  0.7f, 1, 3, 0},
    {"mat_iron",   0.6f, 2, 5, 0},
    {"misc_gem",   0.4f, 1, 2, 0},
};
constexpr LootEntry kBanditLoot[] = {
    {"potion_hp",  0.7f, 1, 2, 0},
    {"wpn_dagger", 0.5f, 1, 1, 3},
    {"misc_gem",   0.4f, 1, 2, 0},
};
constexpr LootEntry kGuardLoot[] = {
    {"food_bread",  0.6f, 1, 3, 0},
    {"potion_hp",   0.5f, 1, 1, 0},
    {"arm_leather", 0.3f, 1, 1, 3},
};
constexpr LootEntry kWitchLoot[] = {
    {"potion_mp",  1.0f, 1, 3, 0},
    {"mat_herb",   0.7f, 2, 5, 0},
    {"potion_hp",  0.5f, 1, 2, 0},
};
constexpr LootEntry kSorceressLoot[] = {
    {"potion_mp",  1.0f, 2, 5, 0},
    {"potion_hp",  1.0f, 1, 3, 0},
    {"mat_herb",   0.6f, 3, 7, 0},
    {"misc_gem",   0.4f, 1, 2, 0},
};

struct LootTable {
    const LootEntry* data;
    std::size_t      n;
};

// Index = NPCType integer.
constexpr LootTable kNpcLoot[] = {
    {kPeasantLoot,    sizeof(kPeasantLoot)    / sizeof(LootEntry)},
    {kWoodcutterLoot, sizeof(kWoodcutterLoot) / sizeof(LootEntry)},
    {kMerchantLoot,   sizeof(kMerchantLoot)   / sizeof(LootEntry)},
    {kCaravanLoot,    sizeof(kCaravanLoot)    / sizeof(LootEntry)},
    {kBanditLoot,     sizeof(kBanditLoot)     / sizeof(LootEntry)},
    {kGuardLoot,      sizeof(kGuardLoot)      / sizeof(LootEntry)},
    {kWitchLoot,      sizeof(kWitchLoot)      / sizeof(LootEntry)},
    {kSorceressLoot,  sizeof(kSorceressLoot)  / sizeof(LootEntry)},
};

// FAUNA_LOOT — keyed by faction id string.
constexpr LootEntry kWildlifeLoot[] = {
    {"food_meat", 0.85f, 1, 3, 0},
    {"mat_hide",  0.50f, 1, 2, 0},
};
constexpr LootEntry kDemonsLoot[] = {
    {"mat_bone",  0.70f, 1, 3, 0},
    {"mat_herb",  0.30f, 1, 2, 0},
    {"misc_gem",  0.15f, 1, 1, 3},
};

// Settlement loot.
constexpr LootEntry kSettlementBase[] = {
    {"food_bread", 1.0f,  5, 14, 0},
    {"potion_hp",  1.0f,  3,  9, 0},
};
constexpr LootEntry kEconFarming[] = {
    {"food_bread", 1.0f, 10, 24, 0},
    {"mat_herb",   1.0f,  5, 12, 0},
};
constexpr LootEntry kEconMining[] = {
    {"mat_iron",   1.0f, 5, 14, 0},
    {"misc_gem",   1.0f, 0,  2, 0},
};
constexpr LootEntry kEconTrade[] = {
    {"potion_hp",  1.0f, 5, 14, 0},
    {"potion_mp",  1.0f, 3,  9, 0},
    {"mat_iron",   1.0f, 3,  7, 0},
    {"misc_gem",   1.0f, 0,  3, 0},
};
constexpr LootEntry kEconFishing[] = {
    {"food_bread", 1.0f, 8, 19, 0},
    {"mat_herb",   1.0f, 3,  7, 0},
};
constexpr LootEntry kEconCrafting[] = {
    {"mat_wood",   1.0f, 5, 14, 0},
    {"mat_iron",   1.0f, 4, 11, 0},
};

inline std::vector<ItemStack> roll_loot(const LootEntry* table, std::size_t n,
                                        int level, RngFn rng) {
    std::vector<ItemStack> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const LootEntry& e = table[i];
        if (e.minLevel > 0 && level < e.minLevel) continue;
        if (rng() < e.chance) {
            const int qty = e.min + static_cast<int>(rng() *
                static_cast<float>(e.max - e.min + 1));
            out.push_back({std::string(e.item), qty});
        }
    }
    return out;
}

inline float gold_faction_mult(const char* factionId) {
    if (std::strcmp(factionId, "wildlife") == 0) return 0.1f;
    if (std::strcmp(factionId, "demons")   == 0) return 0.6f;
    if (std::strcmp(factionId, "bandits")  == 0) return 0.8f;
    return 1.0f;
}

} // namespace

// ── Public API ────────────────────────────────────────────────

const ItemDef* item_def(const std::string& id) noexcept {
    const auto& m = catalog_map();
    const auto it = m.find(id);
    return it == m.end() ? nullptr : it->second;
}

float inventory_weight(const Inventory& inv) noexcept {
    float total = 0.0f;
    for (const auto& s : inv.stacks) {
        if (const ItemDef* d = item_def(s.id)) {
            total += d->weight * static_cast<float>(s.count);
        }
    }
    return total;
}

std::vector<ItemStack> generate_npc_inventory(int npcType, int npcLevel, RngFn rng) {
    if (npcType < 0 ||
        npcType >= static_cast<int>(sizeof(kNpcLoot) / sizeof(LootTable))) {
        return {};
    }
    const LootTable& t = kNpcLoot[npcType];
    return roll_loot(t.data, t.n, npcLevel, rng);
}

std::vector<ItemStack> generate_fauna_loot(const char* factionId, int level, RngFn rng) {
    if (std::strcmp(factionId, "wildlife") == 0)
        return roll_loot(kWildlifeLoot, sizeof(kWildlifeLoot) / sizeof(LootEntry), level, rng);
    if (std::strcmp(factionId, "demons") == 0)
        return roll_loot(kDemonsLoot,   sizeof(kDemonsLoot)   / sizeof(LootEntry), level, rng);
    return {};
}

int generate_loot_gold(int level, const char* factionId, RngFn rng) {
    const float mult   = gold_faction_mult(factionId);
    const float base   = (3.0f + static_cast<float>(level) * 2.0f) * mult;
    const float jitter = rng() * base;
    const int v = static_cast<int>(std::floor(base + jitter));
    return v < 0 ? 0 : v;
}

Inventory generate_settlement_inventory(int population, const char* economy, RngFn rng) {
    Inventory inv;

    // Base
    for (const auto& e : kSettlementBase) {
        const int qty = e.min + static_cast<int>(rng() *
            static_cast<float>(e.max - e.min + 1));
        if (qty > 0) inv.add(e.item, qty);
    }

    // Economy-specific
    const LootEntry* econ = nullptr;
    std::size_t      econN = 0;
    if      (std::strcmp(economy, "farming") == 0)  { econ = kEconFarming;  econN = sizeof(kEconFarming) /sizeof(LootEntry); }
    else if (std::strcmp(economy, "mining") == 0)   { econ = kEconMining;   econN = sizeof(kEconMining)  /sizeof(LootEntry); }
    else if (std::strcmp(economy, "trade") == 0)    { econ = kEconTrade;    econN = sizeof(kEconTrade)   /sizeof(LootEntry); }
    else if (std::strcmp(economy, "fishing") == 0)  { econ = kEconFishing;  econN = sizeof(kEconFishing) /sizeof(LootEntry); }
    else if (std::strcmp(economy, "crafting") == 0) { econ = kEconCrafting; econN = sizeof(kEconCrafting)/sizeof(LootEntry); }

    if (econ) {
        for (std::size_t i = 0; i < econN; ++i) {
            const auto& e = econ[i];
            const int qty = e.min + static_cast<int>(rng() *
                static_cast<float>(e.max - e.min + 1));
            if (qty > 0) inv.add(e.item, qty);
        }
    }

    // Population bonus
    const int popTier = population / 200;
    if (popTier >= 1) inv.add("potion_mp", 1 + static_cast<int>(rng() * static_cast<float>(popTier)));
    if (popTier >= 2) inv.add("misc_gem",      static_cast<int>(rng() * static_cast<float>(popTier)));

    return inv;
}

std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc) {
    // Find stack
    ItemStack* stack = nullptr;
    for (auto& s : inv.stacks) if (s.id == itemId) { stack = &s; break; }
    if (!stack || stack->count <= 0) return {};

    const ItemDef* def = item_def(itemId);
    if (!def) return {};
    if (def->type != ItemType::Potion && def->type != ItemType::Food) return {};

    std::string msg;
    auto append = [&](const std::string& part) {
        if (!msg.empty()) msg += ", ";
        msg += part;
    };

    if (def->effect.hp != 0) {
        const int healed = std::min(def->effect.hp, pc.maxHp - pc.currentHp);
        pc.currentHp += healed;
        append("+" + std::to_string(healed) + " HP");
    }
    if (def->effect.mp != 0) {
        const int restored = std::min(def->effect.mp, pc.maxMp - pc.currentMp);
        pc.currentMp += restored;
        append("+" + std::to_string(restored) + " MP");
    }
    if (def->effect.sp != 0) {
        const int restored = std::min(def->effect.sp, pc.maxSp - pc.currentSp);
        pc.currentSp += restored;
        append("+" + std::to_string(restored) + " SP");
    }

    inv.remove(itemId, 1);

    std::string head = "Used ";
    head += def->name;
    if (msg.empty()) return head;
    return head + ": " + msg;
}

} // namespace sm
