// Faithful port of `src/game/items.ts` (catalog + loot tables + helpers).
//
// All numeric constants, ids, chances, min/max ranges, and gold formulas
// are copied verbatim from the TS source. Adding/removing entries here
// must mirror the TS file 1:1.

#include "macro/items.h"
#include "macro/anatomy.h"
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
    // Faction currencies (owner, W2d): money is a COMMODITY — every realm
    // mints its own light coin; all at value 1 until exchange rates arrive
    // (macro/currency.h owns the faction mapping and the wallet math).
    {"coin_empire",  "Imperial Crown",  ItemType::Misc,        1, 0.01f, "\xF0\x9F\xAA\x99",
        "Coin of the Empire of Light", {}},
    {"coin_magika",  "Magika Sigil",    ItemType::Misc,        1, 0.01f, "\xF0\x9F\xAA\x99",
        "Coin of the Magika realms", {}},
    {"coin_timaert", "Republic Mark",   ItemType::Misc,        1, 0.01f, "\xF0\x9F\xAA\x99",
        "Coin of the Republic of Timaert", {}},
    {"coin_barbar",  "Northern Ring",   ItemType::Misc,        1, 0.01f, "\xF0\x9F\xAA\x99",
        "Ring-money of the northern kingdoms", {}},

    // Consumables
    {"potion_hp",   "Health Potion",   ItemType::Potion,     50, 0.30f, "\xE2\x9D\xA4",
        "Restores 30 HP", {{std::uint8_t(BonusId::HealHp), 30}}},
    {"potion_mp",   "Mana Potion",     ItemType::Potion,     75, 0.30f, "\xE2\x9C\xA8",
        "Restores 15 MP", {{std::uint8_t(BonusId::HealMp), 15}}},
    // The economy's NOUNS live in THIS catalog too (owner's one-dictionary
    // ruling): the bread a city bakes and the bread in the player's bag are
    // one row. Ids and weights match macro/commodity.h verbatim — the link
    // law in econ_v1_test holds the two tables together.
    {"bread",  "Bread",           ItemType::Food,       10, 1.00f, "\xF0\x9F\x8D\x9E",
        "Restores 10 HP", {{std::uint8_t(BonusId::HealHp), 10}}},
    {"food_meat",   "Raw Meat",        ItemType::Food,       15, 0.50f, "\xF0\x9F\x8D\x96",
        "Restores 15 HP", {{std::uint8_t(BonusId::HealHp), 15}}},

    // Materials
    {"wood",    "Wood",            ItemType::Material,    5, 2.00f, "\xF0\x9F\xAA\xB5",
        "Building material", {}},
    {"iron",    "Iron Ore",        ItemType::Material,   15, 4.00f, "\xE2\x9B\x8F",
        "Smithing material", {}},
    // Value 32 IS the mint yield (CANON S10: «таблица цен = монетный двор»
    // — one unit of silver coins into 32 nominal-1 coins, po2): the whole
    // money supply of the world derives from this one number × geology.
    {"silver",  "Silver Ore",      ItemType::Material,   32, 4.00f, "\xE2\x9A\xAA",
        "Mint metal", {}},
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
    // The two rows that finally MEAN what they have always said. `slotMask`
    // names TYPES, so a dagger fits any body with a grip and the leather fits
    // any body with a torso — an octopus included, without either row knowing
    // an octopus exists.
    {"wpn_dagger",  "Rusty Dagger",    ItemType::Weapon,     30, 1.00f, "\xF0\x9F\x97\xA1",
        "+2 STR when equipped", {{std::uint8_t(BonusId::Str), 2}},
        /*slot*/part_bit(BodyPartId::Grip) | part_bit(BodyPartId::OffGrip),
        /*blocks*/0, /*armor*/{}},
    {"arm_leather", "Leather Armor",   ItemType::Armor,      60, 5.00f, "\xF0\x9F\x9B\xA1",
        "+2 END when equipped", {{std::uint8_t(BonusId::End), 2}},
        /*slot*/part_bit(BodyPartId::Torso), /*blocks*/0,
        // A leather coat is worth a third of a plain blow (kArmorHalving);
        // uniform across the nine types — the mechanical scalar-era
        // translation, until armour rows author their columns.
        /*armor*/uniform_armor(3)},

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
constexpr LootEntry kMinerLoot[] = {
    {"iron",  1.0f, 2, 7, 0},
    {"bread", 0.5f, 1, 2, 0},
};
constexpr LootEntry kQuarrymanLoot[] = {
    {"stone", 1.0f, 2, 7, 0},
    {"bread", 0.5f, 1, 2, 0},
};
constexpr LootEntry kClayDiggerLoot[] = {
    {"clay",  1.0f, 2, 7, 0},
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
// A wheat stand: rolls a couple of grain, then the harvest door scales by
// the stand's own height against the kind's reference (sub/map_data.h
// yieldRefHeightM) — so in practice a stalk pays a stalk's worth.
constexpr LootEntry kCropLoot[] = {
    {"grain", 1.00f, 1, 2, 0},
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
    SM_LOOT_PROFILE("miner", kMinerLoot),
    SM_LOOT_PROFILE("quarryman", kQuarrymanLoot),
    SM_LOOT_PROFILE("clay_digger", kClayDiggerLoot),
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
    SM_LOOT_PROFILE("crop",       kCropLoot),    // world prop, not an inhabitant
};
#undef SM_LOOT_PROFILE

// NPCType -> loot-profile id, one row per enum value with the enum as a
// COLUMN under the rows_in_enum_order guard (the kNpcPurse idiom): the old
// positional list was checked for COUNT only, so an insertion in the middle
// of NPCType would silently re-key every profile after it. There used to be
// a SECOND door beside this one — a kNpcLoot[] indexed by the same enum,
// with a comment asking that the two be kept in sync. A comment is not a
// mechanism (problems.md 18).
//
// The creature rows name their profile in their own `lootId` column and fall
// back to their faction's default, which is why they carry a nullptr rather
// than a made-up name: one row, one answer, and the column that already
// existed wins over a second list.
struct NpcLootRow { NPCType type; const char* id; };
constexpr NpcLootRow kNpcLootId[std::size_t(NPCType::Count)] = {
    {NPCType::Peasant,      "peasant"},
    {NPCType::Woodcutter,   "woodcutter"},
    {NPCType::Merchant,     "merchant"},
    {NPCType::Caravan,      "caravan"},
    {NPCType::Bandit,       "bandit"},
    {NPCType::Guard,        "guard"},
    {NPCType::Witch,        "witch"},
    {NPCType::Sorceress,    "sorceress"},
    {NPCType::Miner,        "miner"},
    {NPCType::Quarryman,    "quarryman"},
    {NPCType::ClayDigger,   "clay_digger"},
    // creatures — see npc.h `lootId` / `factionId`
    {NPCType::Rabbit,       nullptr},
    {NPCType::Deer,         nullptr},
    {NPCType::Fox,          nullptr},
    {NPCType::Wolf,         nullptr},
    {NPCType::Bear,         nullptr},
    {NPCType::Boar,         nullptr},
    {NPCType::Snake,        nullptr},
    {NPCType::Hawk,         nullptr},
    {NPCType::Frog,         nullptr},
    {NPCType::Goat,         nullptr},
    {NPCType::Eagle,        nullptr},
    {NPCType::Croc,         nullptr},
    {NPCType::Goblin,       nullptr},
    {NPCType::Skeleton,     nullptr},
    {NPCType::Troll,        nullptr},
    {NPCType::SwampThing,   nullptr},
    {NPCType::IceWraith,    nullptr},
    {NPCType::SandScorpion, nullptr},
    {NPCType::StoneGolem,   nullptr},
    // The player: his loot is the bag he carries, not a profile.
    {NPCType::Adventurer,   nullptr},
    {NPCType::Vendor,       nullptr},
    {NPCType::SilverMiner,  nullptr},
    {NPCType::TaxCollector, nullptr},
};
static_assert(rows_in_enum_order(kNpcLootId, &NpcLootRow::type),
              "kNpcLootId row order must mirror NPCType");

constexpr bool every_npc_loot_id_resolves() {
    for (const NpcLootRow& row : kNpcLootId) {
        // nullptr is an ANSWER, not a gap: this row defers to its own column
        // and its faction's default (the creature rows). What must not happen
        // is a row naming a profile the registry does not have.
        if (!row.id) continue;
        bool found = false;
        for (const LootProfile& p : kLootProfiles) {
            found = found || std::string_view(row.id) == std::string_view(p.id);
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

inline std::vector<ItemRef> roll_loot(const LootEntry* table, std::size_t n,
                                        int level, RngFn rng) {
    std::vector<ItemRef> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const LootEntry& e = table[i];
        if (e.minLevel > 0 && level < e.minLevel) continue;
        if (rng() < e.chance) {
            const int qty = e.min + static_cast<int>(rng() *
                static_cast<float>(e.max - e.min + 1));
            ItemRef r{};
            const int idx = item_index(e.item);
            if (idx < 0) continue;      // a profile naming an unknown row drops
            r.def = std::uint16_t(idx);
            r.count = qty;
            out.push_back(r);
        }
    }
    return out;
}

} // namespace

// ── Public API ────────────────────────────────────────────────

// THE ordinal of an authoring id. Resolved through the same map the def
// lookup uses, and cached by the caller — the runtime record carries the
// number, never the string.
int item_index(const char* id) noexcept {
    if (!id || id[0] == '\0') return -1;
    const auto& m = catalog_map();
    const auto it = m.find(id);
    return it == m.end() ? -1 : int(it->second - kCatalog);
}

int item_index(const std::string& id) noexcept { return item_index(id.c_str()); }

const ItemDef* item_def_at(int idx) noexcept {
    return (idx >= 0 && idx < int(std::size(kCatalog))) ? &kCatalog[idx]
                                                        : nullptr;
}

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
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        if (const ItemDef* d = item_def_at(int(s.def))) {
            total += d->weight * static_cast<float>(s.count);
        }
    }
    return total;
}

std::vector<ItemRef> roll_loot_profile(const char* lootId, int level, RngFn rng) {
    const LootProfile* p = loot_profile(lootId);
    if (!p) return {};
    return roll_loot(p->data, p->n, level, rng);
}

const char* npc_loot_id(int npcType) noexcept {
    if (npcType < 0 || npcType >= static_cast<int>(std::size(kNpcLootId))) return "";
    // A row that names no profile of its own answers with the empty string, not
    // with a null the caller has to remember to check. The creature rows are
    // that case: they defer to their own column and their faction's default.
    const char* id = kNpcLootId[npcType].id;
    return id ? id : "";
}

int generate_loot_gold(int npcType, int level, const CorpseLootContext& ctx,
                       RngFn rng) {
    if (npcType < 0 || npcType >= int(NPCType::Count)) return 0;
    const NpcPurseRow& purse = npc_purse(NPCType(std::uint8_t(npcType)));
    if (purse.max <= 0) return 0;          // no pockets: a beast, honestly
    // 1. The BODY: its row's span, rolled, grown by its own level (a veteran
    //    bandit has robbed more than a fresh recruit; level 1 is the row's
    //    authored number, unscaled).
    const float span = float(purse.max - purse.min);
    const float rolled = float(purse.min) + rng() * span;
    const float body = rolled
        * (1.0f + kLootLevelGain * float(level > 0 ? level - 1 : 0));
    // 2. The WORLD: one factor per system, each 1.0 when its system is silent.
    //    A new system is a new line here and a new field there — no caller
    //    learns about it.
    const float danger =
        1.0f + kDangerLootGain * (float(ctx.danger) / 255.0f);
    const float place = ctx.wealthMul > 0.0f ? ctx.wealthMul : 1.0f;
    const int v = int(std::floor(body * danger * place));
    return v < 0 ? 0 : v;
}

std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc) {
    // Find stack
    ItemRef* stack = nullptr;
    const int idx = item_index(itemId);
    for (ItemRef& s : inv.slots) {
        if (!s.empty() && s.def == std::uint16_t(idx)) { stack = &s; break; }
    }
    if (!stack || stack->count <= 0) return {};

    const ItemDef* def = item_def(itemId);
    if (!def) return {};
    if (!item_type_consumable(def->type)) return {};

    std::string msg;
    auto append = [&](const std::string& part) {
        if (!msg.empty()) msg += ", ";
        msg += part;
    };

    // Every cell of the row goes through the ONE instant door (macro/bonus.h),
    // which reports what actually MOVED rather than what was asked for — so
    // the line the player reads says 5 when only 5 fitted. The three
    // hand-written clamp-and-append blocks that stood here were the same
    // arithmetic three times, and they could only ever speak about the three
    // pools they happened to name.
    PoolSlice pools{};
    pools.current[int(PoolId::Hp)] = &pc.currentHp;
    pools.maximum[int(PoolId::Hp)] = pc.maxHp;
    pools.current[int(PoolId::Mp)] = &pc.currentMp;
    pools.maximum[int(PoolId::Mp)] = pc.maxMp;
    pools.current[int(PoolId::Sp)] = &pc.currentSp;
    pools.maximum[int(PoolId::Sp)] = pc.maxSp;

    for (const Bonus& b : def->bonus) {
        if (b.row == 0) continue;
        const int moved = apply_instant(pools, b);
        // A zero move is REPORTED, not swallowed: "+0 HP" is how the player
        // learns the potion he just drank on a full bar was wasted.
        std::string part = moved >= 0 ? "+" : "";
        part += std::to_string(moved);
        part += " ";
        part += bonus_def(BonusId(b.row)).label;
        append(part);
    }

    inv.remove(itemId, 1);

    std::string head = "Used ";
    head += def->name;
    if (msg.empty()) return head;
    return head + ": " + msg;
}

} // namespace sm
