// Items & inventory. This file is the source of truth (the TS original is
// dead — the migration is closed).
//
// ItemType order is save-relevant and fixed:
//   ItemType { Weapon=0, Armor=1, Potion=2, Food=3, Material=4, Misc=5 }
// Stacking semantics: at most one entry per id (addItem stacks quantity).
//
// Catalog: the 4 faction coins, consumables (potion_hp/mp, bread, food_meat),
// monster materials (mat_bone/hide/herb), equipment (wpn_dagger/sword/spear/
// axe/mace/staff, arm_leather), misc_gem — plus the economy's 14 commodity
// nouns (macro/commodity.h, owner's one-dictionary ruling): wood, stone,
// iron, silver, clay, grain, bricks, cloth, tools, furniture, wagon, jewelry,
// carving, statue. The bread a city bakes and the bread in the player's bag
// are ONE row; ids and weights match commodity.h verbatim (link law in
// econ_v1_test). New rows APPEND — a saved ItemRef carries the ordinal.

#pragma once
#include "macro/bonus.h"
#include "macro/damage_types.h"
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

// Can this kind be swallowed for its bonuses (use_item's gate, the UI's
// "Use" button)? ONE answer — it used to be written out twice, in items.cpp
// and in the inventory overlay.
inline constexpr bool item_type_consumable(ItemType t) {
    return t == ItemType::Potion || t == ItemType::Food;
}

// How a weapon row's blow TRAVELS (owner verdicts 2026-09-09, CANON S14
// «СТРЕЛЬБА/МЕТАНИЕ»). Melee swings at reach; Missile looses a projectile
// down the aim line the instant it is asked — NO ammo of any kind (the ARPG
// conceit: «даже в M&B стрелы восполняются каждый бой») — and pays the same
// body recovery gate from the same mass law as a swing; Thrown is the
// reserved third value (the item itself flies — awakens after the demo).
enum class Delivery : std::uint8_t {
    Melee   = 0,
    Missile = 1,
    Thrown  = 2,
};

// 8 (owner verdict 2026-09-07, affix track): the random ladder above 4 is
// astronomically rare (each further affix pays ÷4), but ARTIFACTS are fixed
// SETS through the same door and a designed set wants the room — cheaper to
// state once now than to move the save format twice. In bytes it is +8 per
// slot next to the pair-of-structs era (see the SoA note on ItemRef).
inline constexpr int kMaxItemAffixes = 8;

// What a row DOES, as rows of the one bonus registry (macro/bonus.h).
//
// It was a struct of six named ints — `hp, mp, sp, str, end, agi` — and half
// of it was fiction: nothing anywhere read `str`, `end` or `agi`, so the
// dagger's authored "+2 STR when equipped" and the leather's "+2 END" did
// nothing at all, and `agi` named an attribute the sheet does not even have
// (it is `spd`). As many cells as an item INSTANCE carries (kMaxItemAffixes):
// a catalog row and a rolled affix say the same kind of thing, so they say it
// in the same words — and a unique's innate set gets the same room a rolled
// set does.
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
    // What it stops — nine columns, one per DamageType, same units as damage
    // and as a creature row's own armour, because all three meet in ONE law
    // (macro/damage_types.h mitigate_amount). Scalar-era rows convert with
    // uniform_armor(x); per-column authoring (a fire-warding cloak) is what
    // the nine columns are FOR.
    ArmorProfile  armor{};

    // ── What a WEAPON row deals (CANON S13: урон = NdM строкой предмета) ──
    // dice{0,1} (every non-weapon) rolls nothing; dmgType names the armour
    // column the blow argues with; skill names WHICH weapon skill multiplies
    // a strike made with this row (SkillId::Count = none — the wielder falls
    // back to Unarmed, which is honest for a swung sack of grain).
    // There is deliberately NO tempo column: a weapon's swing time is
    // DERIVED from its `weight` above (macro/anatomy.h weapon_swing_seconds
    // — owner verdict 2026-09-07: «скорость привязать к массе»), so every
    // future row gets its pace for free from the one kilogram figure the
    // carry law already prices, and the two can never disagree.
    Dice          dice{};
    DamageType    dmgType   = DamageType::Blunt;
    SkillId       skill     = SkillId::Count;
    // How the blow travels (enum above). A Missile row's damage deliberately
    // takes NO attribute add (hand_strike_fields, owner verdict 2026-09-09):
    // dice + typed skill + LCK only — range is the compensation, and a future
    // firearm is this exact law with fatter flat dice.
    Delivery      delivery  = Delivery::Melee;
    // How far a loosed missile is aimed and flies (grid units; the projectile
    // door derives its lifetime from this). 0 on melee rows — their reach
    // stays the engine's arm's-length constant.
    float         range     = 0.0f;
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
// an item is CALLED, but the type is the same one a perk and an aura carry.
// Since the SoA move (v82) it is the CURRENCY of affix_at/set_affix rather
// than the stored shape — the instance keeps rows and values in two flat
// arrays and hands them out as this pair.
using ItemAffix = Bonus;

struct ItemRef {
    std::uint16_t def = 0;         // catalog ordinal
    // 1 + raw row of the commodity dictionary (macro/commodity.h); 0 = the
    // row's own default. The +1 exists because raw row 0 (wood) is a real
    // material and 0 must keep meaning "unset". At SCRAP the byte substitutes
    // part 0 of the row's composition (owner verdict 2026-09-11): a steel
    // sword returns steel, not the row's default iron.
    std::uint8_t  material = 0;
    std::uint8_t  quality = 0;     // 0 = ordinary
    std::int32_t  count = 0;       // 0 = THIS SLOT IS EMPTY
    std::uint32_t seed = 0;        // 0 = plain, not procedurally rolled
    // The affixes, SoA: rows in one flat array, values in another. The array
    // of {u8 row, i16 value} pairs this replaces paid a padding byte per cell
    // to alignment — a third of the affix block spent on nothing, in the one
    // struct the game keeps 256 × per container. Two arrays carry the same
    // facts at 3 bytes a cell exactly, and the save writes them without the
    // padding it used to (v82). Readers go through affix_at/set_affix, so the
    // layout is this struct's own business.
    std::uint8_t  affixRow[kMaxItemAffixes]{};
    std::int16_t  affixValue[kMaxItemAffixes]{};

    bool empty() const { return count == 0; }
    Bonus affix_at(int i) const {
        return Bonus{affixRow[i], affixValue[i]};
    }
    void set_affix(int i, Bonus b) {
        affixRow[i] = b.row;
        affixValue[i] = b.value;
    }
    // Everything except the count — the whole stacking rule.
    bool same_kind_as(const ItemRef& o) const {
        if (def != o.def || material != o.material || quality != o.quality
            || seed != o.seed) {
            return false;
        }
        for (int i = 0; i < kMaxItemAffixes; ++i) {
            if (affixRow[i] != o.affixRow[i]
                || affixValue[i] != o.affixValue[i]) {
                return false;
            }
        }
        return true;
    }
};

// The byte price is a stated fact, not a discovery: 12 of header + 8 rows +
// 16 values = 36, no padding. A field added without reading this line trips
// here instead of silently growing every container in the game by kilobytes.
static_assert(sizeof(ItemRef) == 36, "ItemRef grew — reprice the containers");

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
    // By SLOT — what a panel that already stands on the exact stack uses.
    // remove_of(def, n) is blind to affixes: with procedural instances in
    // play, wearing a rolled sword and removing "a sword" by ordinal could
    // strip the PLAIN stack and leave the rolled one duplicated — the
    // conservation law broken in both directions at once.
    bool remove_at(int slot, int n) {
        if (slot < 0 || slot >= kMaxInventorySlots || n <= 0) return false;
        ItemRef& s = slots[std::size_t(slot)];
        if (s.empty() || s.count < n) return false;
        s.count -= n;
        if (s.empty()) s = ItemRef{};
        return true;
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

// ── THE matter law: what a row is MADE OF (owner verdicts 2026-09-11) ──────
// CANON «Крафт/Скрап»: the game is resource-oriented — every item is
// materialised raw matter, and craft/scrap/production are ONE reversible
// reaction over ONE table. That table is the catalog itself: a row's
// composition lives HERE, as up to kMaxItemParts pairs {catalog ordinal,
// count}, so the 36-byte instance grows by nothing and the one answer feeds
// four consumers — the city's production day (econ_day), the craft door, the
// scrap door, and the AI's overflow scrap below.
//
// A row with NO parts is TERMINAL — raw matter that "consists of itself"
// (ore, hide, gems, meat, coins). Parts may reference ONLY terminal rows, so
// the reaction is always one step deep; recursion cannot exist to be guarded.
// 4 slots: the wagon wants wood+iron today, a composite armour wants
// hide+iron+cloth tomorrow — 12 cold bytes per catalog row buy the headroom
// «на века» (owner verdict; most rows author 1–2).
inline constexpr int kMaxItemParts = 4;
struct ItemPart {
    std::uint16_t def   = 0;   // catalog ordinal of a TERMINAL row
    std::uint8_t  count = 0;   // units of it in ONE batch (see item_yield)
};
// The row's composition; an empty span = terminal. Resolved once from the
// authoring table in items.cpp (strings author, ordinals run).
std::span<const ItemPart> item_parts(int defIdx) noexcept;
// How many items one BATCH of the composition makes (owner verdict
// 2026-09-12, «привести к единой системе»). 1 for nearly every row; the
// coin rows say {silver 1} → 32 — which UNIFIES the mint: «состав монеты и
// есть монетный двор», and melting coin is the same scrap door as melting a
// sword. The witness pins value-neutrality (yield × coin value == the
// composition's value), so this column and the price anchor cannot drift
// apart unseen.
int item_yield(int defIdx) noexcept;

// The row's LABOUR: batches one person-day of work runs (owner verdicts
// 2026-09-12, «единая SP-система труда»). ONE number serves three workers:
//   · the CITY prices its production day with it (econ_produce_day — a
//     person-day is what a population buys, «чем больше населения, тем
//     больше SP на дела»);
//   · the HAND pays SP with it: one person-day = one FULL SP bar (the
//     anchor kGatherPerWorkerDay is already derived from the quarter-bar
//     work cycle), so a batch costs maxSp / labour — into the negative,
//     with the march's own exhaustion bite («как в Elin», no second labour
//     law);
//   · a future harvest/craft SKILL multiplies through this same column
//     (owner: «потом расширяемо») — appended, not designed now.
// 0 = the row is not made by work (terminal raw matter).
int item_labour(int defIdx) noexcept;

// ── The reversible reaction (CANON «Крафт/Скрап», owner 2026-09-11/12) ─────
// FORWARD — craft: consume exactly n BATCHES of the composition (full
// price), emit n × yield WHITE base items (seed 0, no affixes — «закон
// нулевых аффиксов»: affixes are born in the world, never at a bench).
// Refuses terminal rows (nothing composes them), missing materials or a
// full bag — NOTHING ELSE (owner verdict 2026-09-12, ЗАГЛАВНЫМИ: «У НАС
// БАРТЕРНАЯ ЭКОНОМИКА... ПРОСТО ГОРОД ДЕЛАЕТ МОНЕТЫ ЧЕРЕЗ СИСТЕМУ КРАФТА
// ПО СВОЕМУ АИ»): coin is a commodity like bread, the mint IS this door run
// by the town's own AI, and hand-striking coin is harmless by arithmetic —
// the reaction is value-neutral (the static_assert beside the table), so a
// forger earns nothing a smith doesn't. All-or-nothing on a copy: a refused
// craft leaves the bag untouched (CANON S5 — goods never evaporate).
bool craft_item(Inventory& inv, int defIdx, int n);
// REVERSE — scrap: n units of the SLOT (the instance is what is scrapped,
// not the id — a rolled sword and its bare twin are different stacks) melt
// back HALF THEIR TOTAL MATTER, floored per part: floor(n × count / (2 ×
// yield)) («закон энтропии» pooled — owner verdict 2026-09-12: one sword
// still returns 1 iron, one dagger's handle still burns whole, and 64 coins
// melt to 1 silver through this same door — no coin special case exists).
// The seed and every affix burn with no return («запрет вечного реролла» as
// arithmetic). A non-zero material byte substitutes part 0 (see
// ItemRef.material). Terminal rows refuse: raw matter has no reverse.
// All-or-nothing on a copy.
bool scrap_at(Inventory& inv, int slot, int n);
// The AI's slot hygiene (CANON: «склад не забивается говном»). While MORE
// than half the container is occupied, scrap the CHEAPEST non-fungible stacks
// (rolled/affixed instances — plain rows stack into one slot and cannot clog)
// whole, cheapest first by value_of × count, into raw matter. Returns stacks
// scrapped. The player's own bag NEVER passes through here — his scrap is a
// manual act (owner law, same CANON section).
inline constexpr int kAutoScrapSlots = kMaxInventorySlots / 2;  // 128 = 50%
int auto_scrap_overflow(Inventory& inv);

// Loot generation. `rng()` returns float in [0, 1).
using RngFn = float (*)();

// ── THE coin run — the one shape of "how many" and "how strong" ───────────
// (owner verdict 2026-09-07: «везде где можно — единые формулы-константы»).
// Flip a p = num/den coin until it fails or the cap stops it; the count of
// heads is the answer. Geometric, smooth, no tiers: the same law says how
// many affixes a drop grows and how strong each one rolls, and a deeper
// power does not open new shelves — it bends the one curve. Every user
// quotes p as (base + power) / kAffixChanceDen with its own base below.
inline int coin_run(RngFn rng, int num, int den, int cap) {
    int n = 0;
    while (n < cap && rng() * float(den) < float(num)) ++n;
    return n;
}

inline constexpr int kAffixChanceDen  = 512;
// How many: at power 0 a field hare's dagger rolls an affix 1-in-32; at 255
// every second deep drop carries one, and the SAME coin keeps flipping for
// the 2nd..8th (p² , p³ … — multi-affix finds live in the deep end's curve,
// not in a rule).
inline constexpr int kAffixCountBase  = 16;
// How strong: the value coin starts three times friendlier (25%..75% across
// the power span), so "+1 usually, the bigger the rarer" holds everywhere
// and the tail is exponential rather than shelved.
inline constexpr int kAffixValueBase  = 128;
// The roll's own ceiling, in UNITS of the affix row's step (one constant for
// every row — «на века»): 16 units ≈ a tenth of the endgame's ~100-point
// scores at step 1. It caps what LUCK can write, not what the format holds —
// an int16 value cell lets a designed artifact state numbers the coin never
// will.
inline constexpr int kAffixValueCapUnits = 16;

// The world's one affix-power byte: how loaded the dice are where this thing
// dropped. Level pulls 8/level, the danger byte rides in whole, the place's
// wealth adds up to 64 («богатство = стоимость, шире золота» — the third
// term is the owner's verdict, not a double-count of the purse law).
std::uint8_t affix_power(int level, std::uint8_t danger, float wealthMul);

// ── THE affix writer (owner's design 2026-09-07) ──────────────────────────
// One door for every way an item gains modifiers. The RANDOM path — this
// call — is the loot roll's special case: pick rows from the affix table by
// the weight column of this item's type (no zeros — armour CAN roll damage,
// it is merely untypical), roll each value on the coin above, stamp the seed
// so the stacking law separates it. A FIXED issuance (artifact, quest
// reward, a script) writes the same cells through set_affix and never calls
// the coin — «рандом = частный случай выдачи».
// Non-wearables (slotMask 0) are refused: an affix is a WORN thing.
void grant_affixes(ItemRef& item, std::uint8_t power, RngFn rng);

// The affix table's own listing, for printers (the dev console) — same
// contract as loot_profile_count/loot_profile_id.
std::size_t affix_def_count() noexcept;
const char* affix_def_key(std::size_t i) noexcept;

// How many affix cells actually say something — the number the UI tint and
// the suffix read.
int affix_count(const ItemRef& item) noexcept;

// What THIS instance is worth: the row's price plus every affix cell priced
// per point of its bonus row («чем реже, тем ценнее» made arithmetic). A
// plain stack answers exactly def->value, so nothing in the economy moves.
// Floored at 0 — a cursed thing is worthless, not a debt.
int value_of(const ItemRef& item) noexcept;

// The title's tail, from the affix table's own name column: the FIRST
// speaking cell names the instance ("Rusty Dagger of Strength"). "" when
// plain.
const char* affix_suffix(const ItemRef& item) noexcept;

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
// `affixPower` — the context byte the wearable stacks roll their affixes on
// (affix_power above). NOT defaulted on purpose: a new call site must state
// what the world says there, and the compiler asks where a comment could not.
std::vector<ItemRef> roll_loot_profile(const char* lootId, int level, RngFn rng,
                                       std::uint8_t affixPower);

// The registry's own listing — for printers (the dev console's `loots`), so
// the id list is never restated anywhere. Index order is the table's.
std::size_t loot_profile_count() noexcept;
const char* loot_profile_id(std::size_t i) noexcept;

// NPCType integer -> its loot-profile id (npc.h enum order). "" if out of range.
const char* npc_loot_id(int npcType) noexcept;

// Apply consumable effect to player. Returns the player-visible message
// (empty string if item not found, not consumable, or out of stock).
std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc);

} // namespace sm
