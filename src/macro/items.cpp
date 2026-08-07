// Faithful port of `src/game/items.ts` (catalog + loot tables + helpers).
//
// All numeric constants, ids, chances, min/max ranges, and gold formulas
// are copied verbatim from the TS source. Adding/removing entries here
// must mirror the TS file 1:1.

#include "macro/items.h"
#include "macro/npc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

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
    // The economy's NOUNS live in THIS catalog too (owner's one-dictionary
    // ruling): the bread a city bakes and the bread in the player's bag are
    // one row. Ids and weights match macro/commodity.h verbatim — the link
    // law in econ_v1_test holds the two tables together.
    {"bread",  "Bread",           ItemType::Food,       10, 1.00f, "\xF0\x9F\x8D\x9E",
        "Restores 10 HP", {10, 0, 0, 0, 0, 0}},
    {"food_meat",   "Raw Meat",        ItemType::Food,       15, 0.50f, "\xF0\x9F\x8D\x96",
        "Restores 15 HP", {15, 0, 0, 0, 0, 0}},

    // Materials
    {"wood",    "Wood",            ItemType::Material,    5, 2.00f, "\xF0\x9F\xAA\xB5",
        "Building material", {}},
    {"iron",    "Iron Ore",        ItemType::Material,   15, 4.00f, "\xE2\x9B\x8F",
        "Smithing material", {}},
    {"grain",   "Grain",           ItemType::Material,    5, 1.00f, "\xF0\x9F\x8C\xBE",
        "Raw grain, milled and baked into bread", {}},
    {"stone",   "Stone",           ItemType::Material,    5, 4.00f, "\xF0\x9F\xAA\xA8",
        "Quarried stone", {}},
    {"clay",    "Clay",            ItemType::Material,    5, 2.00f, "\xF0\x9F\xBA",
        "River clay for bricks", {}},
    {"cloth",   "Cloth",           ItemType::Misc,       20, 1.00f, "\xF0\x9F\xA7\xB6",
        "Woven clothing", {}},
    {"bricks",  "Bricks",          ItemType::Misc,       10, 4.00f, "\xF0\x9F\xA7\xB1",
        "Fired bricks for housing", {}},
    {"tools",   "Tools",           ItemType::Misc,       40, 2.00f, "\xF0\x9F\x94\xA8",
        "Iron tools of the trades", {}},
    {"furniture","Furniture",      ItemType::Misc,       40, 8.00f, "\xF0\x9F\xAA\x91",
        "Carpented furniture", {}},
    {"wagon",   "Wagon",           ItemType::Misc,       80, 32.00f, "\xF0\x9F\x9B\x9E",
        "A cart for hauling goods", {}},
    {"jewelry", "Jewelry",         ItemType::Misc,      160, 1.00f, "\xF0\x9F\x92\x8D",
        "Fine ornaments", {}},
    {"carving", "Carving",         ItemType::Misc,       80, 2.00f, "\xF0\x9F\xAA\x86",
        "Ornamental woodwork", {}},
    {"statue",  "Statue",          ItemType::Misc,      320, 64.00f, "\xF0\x9F\x97\xBF",
        "A sculpted stone statue", {}},
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
    {"bread", 0.6f, 1, 3, 0},
    {"wood",   0.4f, 1, 4, 0},
    {"mat_herb",   0.2f, 1, 2, 0},
};
constexpr LootEntry kWoodcutterLoot[] = {
    {"wood",   1.0f, 2, 7, 0},
    {"bread", 0.5f, 1, 2, 0},
};
constexpr LootEntry kMerchantLoot[] = {
    {"potion_hp",  0.7f, 1, 3, 0},
    {"bread", 0.6f, 2, 6, 0},
    {"potion_mp",  0.5f, 1, 2, 0},
    {"iron",   0.4f, 1, 3, 0},
    {"misc_gem",   0.3f, 1, 1, 0},
    {"wpn_dagger", 0.2f, 1, 1, 0},
};
constexpr LootEntry kCaravanLoot[] = {
    {"bread", 1.0f, 3, 7, 0},
    {"potion_hp",  0.7f, 1, 3, 0},
    {"iron",   0.6f, 2, 5, 0},
    {"misc_gem",   0.4f, 1, 2, 0},
};
constexpr LootEntry kBanditLoot[] = {
    {"potion_hp",  0.7f, 1, 2, 0},
    {"wpn_dagger", 0.5f, 1, 1, 3},
    {"misc_gem",   0.4f, 1, 2, 0},
};
constexpr LootEntry kGuardLoot[] = {
    {"bread",  0.6f, 1, 3, 0},
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

// PROP_LOOT — the world's own things, not its inhabitants. A felled tree, and
// later a broken boulder or a rifled cairn, resolves through the SAME registry
// a kill does: the prop's kind names a loot profile (`structure_loot_id()` in
// sub/map_data.h), the profile rolls items. Nothing about breaking a prop is
// code — add a row here and a name there and the thing drops.
constexpr LootEntry kTreeLoot[] = {
    {"wood",  1.00f, 2, 5, 0},
};

// ── Unified loot registry ──────────────────────────────────────
// ONE table keyed by stable string `lootId`. Every drop resolves through
// here. The 8 NPC roles reuse the kNpcLoot tables above; factions map to the
// fauna tables. `"bandits"` reuses the bandit NPC table so a Bandits-faction
// creature drops real items instead of nothing (the old zero-loot bug).
struct LootProfile {
    const char*      id;
    const LootEntry* data;
    std::size_t      n;
};

#define SM_LOOT_PROFILE(id_, tbl) {id_, tbl, sizeof(tbl) / sizeof(LootEntry)}
constexpr LootProfile kLootProfiles[] = {
    SM_LOOT_PROFILE("peasant",    kPeasantLoot),
    SM_LOOT_PROFILE("woodcutter", kWoodcutterLoot),
    SM_LOOT_PROFILE("merchant",   kMerchantLoot),
    SM_LOOT_PROFILE("caravan",    kCaravanLoot),
    SM_LOOT_PROFILE("bandit",     kBanditLoot),
    SM_LOOT_PROFILE("guard",      kGuardLoot),
    SM_LOOT_PROFILE("witch",      kWitchLoot),
    SM_LOOT_PROFILE("sorceress",  kSorceressLoot),
    SM_LOOT_PROFILE("wildlife",   kWildlifeLoot),
    SM_LOOT_PROFILE("demons",     kDemonsLoot),
    SM_LOOT_PROFILE("bandits",    kBanditLoot),  // Bandits-faction fauna default
    SM_LOOT_PROFILE("tree",       kTreeLoot),    // world prop, not an inhabitant
};
#undef SM_LOOT_PROFILE

// NPCType integer -> loot-profile id, in npc.h enum order. There used to be a
// SECOND door beside this one — a kNpcLoot[] indexed by the same enum, with a
// comment asking that the two be kept in sync. A comment is not a mechanism
// (problems.md 18): the project has already lost two pairs kept that way. The
// second table is gone, and what is left is checked by the compiler.
constexpr const char* kNpcLootId[] = {
    "peasant", "woodcutter", "merchant", "caravan",
    "bandit", "guard", "witch", "sorceress",
};

// Every humanoid row names a profile, and every profile it names exists. A new
// NPCType that forgets its loot does not silently drop nothing — it does not
// build.
static_assert(std::size(kNpcLootId) == std::size_t(NPCType::Count),
              "every humanoid row must name a loot profile");

constexpr bool every_npc_loot_id_resolves() {
    for (const char* id : kNpcLootId) {
        bool found = false;
        for (const LootProfile& p : kLootProfiles) {
            found = found || std::string_view(id) == std::string_view(p.id);
        }
        if (!found) return false;
    }
    return true;
}
static_assert(every_npc_loot_id_resolves(),
              "an NPC row names a loot profile that is not in the registry");

const LootProfile* loot_profile(const char* lootId) noexcept {
    if (!lootId || !lootId[0]) return nullptr;
    for (const auto& p : kLootProfiles) {
        if (std::strcmp(lootId, p.id) == 0) return &p;
    }
    return nullptr;
}

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

std::span<const ItemDef> item_catalog() noexcept {
    return std::span<const ItemDef>(kCatalog, std::size(kCatalog));
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

std::vector<ItemStack> roll_loot_profile(const char* lootId, int level, RngFn rng) {
    const LootProfile* p = loot_profile(lootId);
    if (!p) return {};
    return roll_loot(p->data, p->n, level, rng);
}

const char* npc_loot_id(int npcType) noexcept {
    if (npcType < 0 || npcType >= static_cast<int>(std::size(kNpcLootId))) return "";
    return kNpcLootId[npcType];
}

int generate_loot_gold(int level, const char* factionId, RngFn rng) {
    const float mult   = gold_faction_mult(factionId);
    const float base   = (3.0f + static_cast<float>(level) * 2.0f) * mult;
    const float jitter = rng() * base;
    const int v = static_cast<int>(std::floor(base + jitter));
    return v < 0 ? 0 : v;
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
