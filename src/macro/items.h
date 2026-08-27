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
#include "macro/bonus.h"
#include <array>
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

inline constexpr int kMaxItemAffixes = 4;

// What a row DOES, as rows of the one bonus registry (macro/bonus.h).
//
// It was a struct of six named ints — `hp, mp, sp, str, end, agi` — and half
// of it was fiction: nothing anywhere read `str`, `end` or `agi`, so the
// dagger's authored "+2 STR when equipped" and the leather's "+2 END" did
// nothing at all, and `agi` named an attribute the sheet does not even have
// (it is `spd`). Four cells because that is what an item INSTANCE already
// carries (kMaxItemAffixes): a catalog row and a rolled affix say the same
// kind of thing, so they say it in the same words.
inline constexpr int kMaxItemBonuses = kMaxItemAffixes;

// Static blueprint — `ItemDef` mirrors `ItemDef = Omit<Item, 'quantity'>`.
struct ItemDef {
    const char* id          = "";
    const char* name        = "";
    ItemType    type        = ItemType::Misc;
    int         value       = 0;     // gold price
    float       weight      = 0.0f;  // kg per single
    const char* icon        = "?";   // unicode glyph
    const char* description = "";
    // Zeroed cells are empty: BonusId::None is row 0, so a row that grants
    // nothing writes nothing.
    Bonus       bonus[kMaxItemBonuses] = {};

    // ── Where it goes, and what it costs to wear ──────────────────────────
    // `slotMask` is an OR of `part_bit(BodyPartId::X)` (macro/anatomy.h): the
    // TYPES of body part this can sit on. A ring says "Finger" ONCE and fits
    // an octopus with twenty of them; that is the whole reason the mask names
    // types and not indices. 0 = cannot be worn at all, which is every potion
    // and every sack of grain.
    std::uint64_t slotMask   = 0;
    // ...and what wearing it ALSO occupies. A two-hander is the case this
    // exists for: it sits in the main grip and takes the off hand with it.
    // 0 = takes only its own cell.
    std::uint64_t blocksMask = 0;
    // What it stops. Same units as damage and as a creature row's own armour,
    // because all three meet in one formula (sub/damage.cpp).
    int           armor      = 0;
};

// ── THE item instance, and THE container ───────────────────────────────────
//
// One record shape for every container in the world (owner, 2026-08-27): the
// player's bag, a landmark's stock, a chest, a corpse's spoils, a caravan's
// load. It was `std::vector<ItemStack>` where a stack was `{std::string id;
// int count;}` — 32 bytes and a heap block per entry, a linear scan by STRING
// for every question, and a vector header on every entity that carries goods.
//
// The record is flat and carries what an instance IS:
//   * `def`      — the catalog row, resolved ONCE (strings stay the AUTHORING
//                  key in tables; the runtime carries the ordinal, the
//                  faction_index idiom);
//   * `count`    — signed on purpose (owner: «стака с нулём не бывает, так что
//                  int можно») — a negative count is a visible accounting bug,
//                  where an unsigned one would wrap into billions;
//   * `seed`/`material`/`quality`/`affix[]` — what makes a PROCEDURAL item
//                  itself (owner's Diablo-but-simpler design). No producer
//                  fills them yet — the bonus registry is the RPG core's work
//                  — but they ride the format from day one so the save is
//                  bumped once, not twice.
//
// STACKING is one sentence: two records merge only when everything except
// `count` is equal. Bread merges with bread; two procedurally rolled swords
// never merge, because their seeds differ. No second rule, no second table.
inline constexpr int kMaxInventorySlots = 256;   // 16×16, the player's grid

// An affix IS a bonus (macro/bonus.h `Bonus`): a row of the one registry and
// how much of it. The name stays because "affix" is what a rolled modifier on
// an item is CALLED, but the type is the same one a perk and an aura carry —
// byte-identical to the `{uint8 row, int16 value}` this format already wrote,
// so naming it did not move a single saved item.
using ItemAffix = Bonus;

struct ItemRef {
    std::uint16_t def = 0;         // catalog ordinal
    std::uint8_t  material = 0;    // a row of the raw tier; 0 = the row's own
    std::uint8_t  quality = 0;     // 0 = ordinary
    std::int32_t  count = 0;       // 0 = THIS SLOT IS EMPTY
    std::uint32_t seed = 0;        // 0 = plain, not procedurally rolled
    ItemAffix     affix[kMaxItemAffixes]{};

    bool empty() const { return count == 0; }
    // Everything except the count — the whole stacking rule.
    bool same_kind_as(const ItemRef& o) const {
        if (def != o.def || material != o.material || quality != o.quality
            || seed != o.seed) {
            return false;
        }
        for (int i = 0; i < kMaxItemAffixes; ++i) {
            if (affix[i].row != o.affix[i].row
                || affix[i].value != o.affix[i].value) {
                return false;
            }
        }
        return true;
    }
};

// The catalog ordinal of an authoring id, or -1. Strings name rows in tables;
// nothing compares them per tick.
int item_index(const char* id) noexcept;
int item_index(const std::string& id) noexcept;
// The row an ordinal names; nullptr when the ordinal is out of the catalog.
const ItemDef* item_def_at(int idx) noexcept;

struct Inventory {
    std::array<ItemRef, kMaxInventorySlots> slots{};

    // ── Reading ───────────────────────────────────────────────────────────
    int count_of(int defIdx) const noexcept {
        if (defIdx < 0) return 0;
        int n = 0;
        for (const ItemRef& s : slots) {
            if (!s.empty() && s.def == std::uint16_t(defIdx)) n += s.count;
        }
        return n;
    }
    int count(const std::string& id) const noexcept {
        return count_of(item_index(id));
    }
    bool has(const std::string& id) const noexcept { return count(id) > 0; }
    int total() const noexcept {
        int n = 0;
        for (const ItemRef& s : slots) n += s.count;
        return n;
    }
    int used_slots() const noexcept {
        int n = 0;
        for (const ItemRef& s : slots) if (!s.empty()) ++n;
        return n;
    }
    bool full() const noexcept { return used_slots() >= kMaxInventorySlots; }

    // ── Writing ───────────────────────────────────────────────────────────
    // Returns FALSE when the container has no room (owner's ruling: the thing
    // stays where it was — a refused pickup leaves the corpse holding it, a
    // refused trade rolls back whole, a town whose store is full stops
    // producing. Goods never evaporate; that is the economy's conservation
    // law).
    bool add_ref(const ItemRef& what) {
        if (what.count <= 0) return true;          // nothing to add
        for (ItemRef& s : slots) {
            if (!s.empty() && s.same_kind_as(what)) {
                s.count += what.count;
                return true;
            }
        }
        for (ItemRef& s : slots) {
            if (s.empty()) { s = what; return true; }
        }
        return false;
    }
    // By ORDINAL — what a system that already knows the row uses (the economy
    // day, the loot roll). The string forms below are the authoring-facing
    // convenience over exactly these.
    bool add_of(int defIdx, int n) {
        if (defIdx < 0 || n <= 0) return true;
        ItemRef r{};
        r.def = std::uint16_t(defIdx);
        r.count = n;
        return add_ref(r);
    }
    bool remove_of(int defIdx, int n) {
        if (defIdx < 0 || n <= 0 || count_of(defIdx) < n) return false;
        int left = n;
        for (ItemRef& s : slots) {
            if (s.empty() || s.def != std::uint16_t(defIdx)) continue;
            const int take = s.count < left ? s.count : left;
            s.count -= take;
            left -= take;
            if (s.empty()) s = ItemRef{};
            if (left == 0) return true;
        }
        return left == 0;
    }
    bool add(const std::string& id, int n) {
        const int idx = item_index(id);
        if (n <= 0) return true;
        // An id the catalog does not know is a FAILURE, not a silent no-op:
        // with string ids a fabricated name used to land in the bag and only
        // reveal itself as "Unknown item" in the UI much later.
        if (idx < 0) return false;
        ItemRef r{};
        r.def = std::uint16_t(idx);
        r.count = n;
        return add_ref(r);
    }
    bool remove(const std::string& id, int n = 1) {
        const int idx = item_index(id);
        if (idx < 0 || n <= 0 || count_of(idx) < n) return false;
        int left = n;
        for (ItemRef& s : slots) {
            if (s.empty() || s.def != std::uint16_t(idx)) continue;
            const int take = s.count < left ? s.count : left;
            s.count -= take;
            left -= take;
            if (s.empty()) s = ItemRef{};
            if (left == 0) return true;
        }
        return left == 0;
    }
    void clear() { slots.fill(ItemRef{}); }
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

// ── THE corpse-loot context (owner's design, 2026-08-27) ──────────────────
// «контекст от таблицы мобов × зоны сложности (0..255) × богатство ландмарка
// /экономика, и расширяемо — в принципе может быть ещё что-то».
//
// So loot is the door's own idiom (CANON S6): a PRODUCT of contributions, one
// per world system, each of them 1.0 and silent when its system has nothing
// to say. This struct grows by a FIELD — the weather, the dark field, a
// place's live stockpile — and no caller signature changes, exactly as
// MacroWorld grows (macro/macro_world.h). That is why it is a struct and not
// three arguments.
struct CorpseLootContext {
    // The danger byte of the cell the body fell in (macro/zones.h, the 0..255
    // continuum). Deep country pays better — and this is NOT the buried
    // autolevel CANON S12 killed: that one multiplied HP and damage on a body
    // AFTER it was picked. This multiplies what it CARRIES, which is economy,
    // and it is visible in the loot rather than hidden in a fight.
    std::uint8_t danger = 0;
    // The wealth of the place standing on that cell (landmark_registry
    // wealthMul; 1.0 = open land). When the honest-loan track lands (CANON
    // S5), this is where the landmark's live stockpile answers instead.
    float wealthMul = 1.0f;
};

// How much this contributes at full danger: the deepest ground doubles a
// purse. Named because it is a knob, and quoted here so the two ends of the
// continuum (0 → ×1, 255 → ×2) are readable without running the game.
inline constexpr float kDangerLootGain = 1.0f;

// And what a LEVEL adds: a tenth of the row's purse per level above the
// first, so a level-10 bandit carries roughly double a fresh one — the same
// shape as the danger term, on the body's own honest, visible property
// (CANON S12: a creature's level is a fact about the creature).
inline constexpr float kLootLevelGain = 0.1f;

// THE coin a dead body carries (damage-door Inc 5). The ROW says what this
// creature is worth to rob — a beast has no pockets, a merchant is rich
// because he is a merchant (macro/npc.h kNpcPurse), and its own level says
// how long it has been at it — then the WORLD modulates through the context
// above. The faction-keyed multiplier this replaced was a second wealth
// vocabulary, and the 2026-08-27 faction ruling made it wrong outright: the
// same wolf carried six times more coin under a ruin's banner than in a
// meadow. What the banner still decides is which realm's COIN it is.
int                    generate_loot_gold(int npcType, int level,
                                          const CorpseLootContext& ctx,
                                          RngFn rng);
// (generate_settlement_inventory is gone: the unified-container moment it was
// kept for arrived — landmark stocks are seeded by the ECONOMY's own law,
// econ_day.h seed_landmark_inventory, from the one commodity dictionary.)

// ── Unified loot table ─────────────────────────────────────────
// ONE loot registry keyed by a stable string `lootId`. Every drop — NPC or
// monster — resolves through `roll_loot_profile`, replacing the old split
// (NPCType-int vs faction-string) with a single keyed path. Registered ids:
// the 8 NPC roles (peasant..sorceress), plus faction defaults wildlife /
// demons / bandits. Unknown / empty id => no items.
std::vector<ItemRef> roll_loot_profile(const char* lootId, int level, RngFn rng);

// NPCType integer -> its loot-profile id (npc.h enum order). "" if out of range.
const char* npc_loot_id(int npcType) noexcept;

// Apply consumable effect to player. Returns the player-visible message
// (empty string if item not found, not consumable, or out of stock).
std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc);

} // namespace sm
