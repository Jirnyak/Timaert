// Items & inventory. Faithful port of `src/game/items.ts`.
//
// TS authoritative order:
//   ItemType { Weapon=0, Armor=1, Potion=2, Food=3, Material=4, Misc=5 }
// Stacking semantics: at most one entry per id (addItem stacks quantity).
//
// Catalog (24 ids): gold, potion_hp, potion_mp, food_meat, mat_bone,
// mat_hide, mat_herb, wpn_dagger, arm_leather, misc_gem — plus the economy's
// 14 commodity nouns (macro/commodity.h, owner's one-dictionary ruling):
// wood, stone, iron, clay, grain, bread, bricks, cloth, tools, furniture,
// wagon, jewelry, carving, statue. The bread a city bakes and the bread in
// the player's bag are ONE row; ids and weights match commodity.h verbatim
// (link law in econ_v1_test).

#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

namespace sm {

enum class ItemType : std::uint8_t {
    Weapon   = 0,
    Armor    = 1,
    Potion   = 2,
    Food     = 3,
    Material = 4,
    Misc     = 5,
};

// Optional stat bonus (zero = absent — mirrors TS `effect?: {...}`).
struct ItemEffect {
    int hp  = 0;
    int mp  = 0;
    int sp  = 0;
    int str = 0;
    int end = 0;
    int agi = 0;
};

// Static blueprint — `ItemDef` mirrors `ItemDef = Omit<Item, 'quantity'>`.
struct ItemDef {
    const char* id          = "";
    const char* name        = "";
    ItemType    type        = ItemType::Misc;
    int         value       = 0;     // gold price
    float       weight      = 0.0f;  // kg per single
    const char* icon        = "?";   // unicode glyph
    const char* description = "";
    ItemEffect  effect      = {};
};

// Inventory entry. TS `Item` carries the def fields too — in C++ we
// keep just (id, count) and look up the def from the catalog. The
// public API matches TS (`addItem`, `removeItem`, `hasItem`, etc.).
struct ItemStack {
    std::string id;
    int         count = 0;
};

struct Inventory {
    std::vector<ItemStack> stacks;

    int count(const std::string& id) const noexcept {
        for (const auto& s : stacks) if (s.id == id) return s.count;
        return 0;
    }
    bool has(const std::string& id) const noexcept { return count(id) > 0; }
    int  total() const noexcept {
        int n = 0; for (const auto& s : stacks) n += s.count; return n;
    }
    void add(const std::string& id, int n) {
        if (n <= 0) return;
        for (auto& s : stacks) if (s.id == id) { s.count += n; return; }
        stacks.push_back({id, n});
    }
    bool remove(const std::string& id, int n = 1) {
        for (auto it = stacks.begin(); it != stacks.end(); ++it) {
            if (it->id == id) {
                if (it->count < n) return false;
                it->count -= n;
                if (it->count == 0) stacks.erase(it);
                return true;
            }
        }
        return false;
    }
};

// Player combat slice consumed by `useItem` (mirrors TS inline type).
struct PlayerCombatSlice {
    int currentHp, maxHp;
    int currentMp, maxMp;
    int currentSp, maxSp;
};

// Catalog accessor. Returns nullptr if id is unknown (TS returns a
// dummy "Unknown item" — we expose the lookup so callers can decide).
const ItemDef* item_def(const std::string& id) noexcept;

// Enumerate the entire item catalog — the single source of truth for item
// ids. Callers (e.g. the dev console `give`) iterate this rather than
// re-listing ids, so a new catalog row is instantly available everywhere.
std::span<const ItemDef> item_catalog() noexcept;

// Total inventory weight in kg (sum of def.weight × count).
float inventory_weight(const Inventory& inv) noexcept;

// Loot generation. `rng()` returns float in [0, 1).
using RngFn = float (*)();

int                    generate_loot_gold(int level, const char* factionId, RngFn rng);
// (generate_settlement_inventory is gone: the unified-container moment it was
// kept for arrived — landmark stocks are seeded by the ECONOMY's own law,
// econ_day.h seed_landmark_inventory, from the one commodity dictionary.)

// ── Unified loot table ─────────────────────────────────────────
// ONE loot registry keyed by a stable string `lootId`. Every drop — NPC or
// monster — resolves through `roll_loot_profile`, replacing the old split
// (NPCType-int vs faction-string) with a single keyed path. Registered ids:
// the 8 NPC roles (peasant..sorceress), plus faction defaults wildlife /
// demons / bandits. Unknown / empty id => no items.
std::vector<ItemStack> roll_loot_profile(const char* lootId, int level, RngFn rng);

// NPCType integer -> its loot-profile id (npc.h enum order). "" if out of range.
const char* npc_loot_id(int npcType) noexcept;

// Apply consumable effect to player. Returns the player-visible message
// (empty string if item not found, not consumable, or out of stock).
std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc);

} // namespace sm
