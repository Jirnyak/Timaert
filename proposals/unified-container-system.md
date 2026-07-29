# Architecture proposal — Unified Container System

**Status:** proposal (design only — no engine code written). Awaiting owner decision per `MASTER_PROMPT.md` §9.1.
**Author:** container-system architect agent, 2026-07-27.
**Scope:** ONE mechanism — an entity/holder-with-a-container — that backs *everything that holds items*: settlement & city stock, the whole landmark system (thousands of landmarks, each with inventory), local chests inside subworlds, NPC inventories, and corpses. Cache-friendly (SoA / handle-based), O(1)-cheap at thousands scale (lazy materialization + shared authored templates + seeded deterministic rolls).

> This document only proposes. Nothing here is built. It cites current code by `file:line` so the owner can verify every claim.

---

## 0. TL;DR (the shape of the answer)

1. **One handle, one arena.** A holder (settlement, village, landmark, NPC entity, corpse entity, chest) carries a 4-byte `ContainerRef{uint32 id}`. All item data lives in a single central SoA arena, `ContainerStore`, owned by `GameState`. No holder owns a heavyweight `Inventory` object.
2. **Contents are a pure function of `(templateId, seed)`.** Both are derivable from the holder's *existing* identity (a settlement already stores its `economy` string and `id`; `worldSeed` is global). So an unmaterialized container costs **zero** store bytes; it is rolled on first access and can be evicted and regenerated bit-identically.
3. **One authored table replaces every ad-hoc loot/stock generator.** `kSettlementBase`, `kEconFarming/Mining/Trade/Fishing/Crafting` (`items.cpp:198-223`), the NPC role tables, and the fauna/faction profiles all become rows of ONE `ContainerTemplate` table built from the *existing* `LootEntry` atom (`items.cpp:70-76`). Adding a container archetype = **one row**. The `if/else-if` economy chain in `generate_settlement_inventory` (`items.cpp:313-317`) is deleted — replaced by a keyed lookup.
4. **A container holds at most one stack per item id** (semantic rule, `items.h:5` + `Inventory::add`, `items.h:70-74`). The item catalog is a fixed 13-row enum (`kCatalog`, `items.cpp:21-57`). Therefore a container's item storage is **bounded and small** — an inline fixed-capacity array, no per-container heap allocation, no sub-allocator.
5. **Nothing new is serialized.** ECS entities are already regenerated, not saved (`save.cpp` touches no registry). Settlement/village `Inventory` is serialized today but is *always empty* (never populated — see §1.2). The end state stops serializing it and regenerates from `(templateId, seed)` on load, matching the repo's "regenerate the world from its seed" philosophy.

---

## 1. Ground truth — what the code does today (verified, cited)

### 1.1 The item + inventory vocabulary

- `ItemDef` — static blueprint, `items.h:40-49`. Catalog `kCatalog` has **13 rows** (`items.cpp:21-57`): `gold, potion_hp, potion_mp, food_bread, food_meat, mat_wood, mat_iron, mat_bone, mat_hide, mat_herb, wpn_dagger, arm_leather, misc_gem`. Single source of truth for item ids (`item_catalog()`, `items.cpp:258`).
- `ItemStack { std::string id; int count; }` — `items.h:54-57`. **String-keyed.**
- `Inventory { std::vector<ItemStack> stacks; … }` — `items.h:59-86`. **At most one entry per id** — `add()` merges (`items.h:70-74`). Comment at `items.h:5`: *"Stacking semantics: at most one entry per id."*
- Catalog lookup is a `std::unordered_map<std::string,const ItemDef*>` (`items.cpp:59-66`) — string hashing on every lookup. There is **no** integer catalog index helper today (`grep item_index` → none).

### 1.2 Who holds items today

| Holder | Field | Where | Populated? |
|---|---|---|---|
| Player | `PlayerState::inventory` (`Inventory`) | `state.h:98` | Yes — 2 potion_hp + 5 food_bread (`state.cpp:176-177`) |
| Settlement (city) | `Settlement::inventory` (`Inventory`) | `state.h:32` | **NO — always empty.** `populate_landmarks_from_politik` sets economy/eco/garrison but never `s.inventory` (`state.cpp:230-287`) |
| Village | `Village::inventory` (`Inventory`) | `state.h:47` | **NO — always empty** (same populate pass) |
| Subworld / macro NPC entity | `ecs::NpcInventory { Inventory inv; }` | `components.h:79` | Yes — at spawn from `generate_npc_inventory` (`npc_spawn.cpp:90-95`, `engine.cpp:1099-1108`) |
| Corpse entity | `ecs::CorpseLoot { Inventory inv; int gold; }` | `components.h:158-161` | Yes — on death (`engine.cpp:1316-1355`) |

**Key facts the design leans on:**

- `generate_settlement_inventory(population, economy, rng)` (`items.h:112`, `items.cpp:300-334`) is **dead** — declared + defined, **zero callers** (`grep` across `src/` + `tests/`). It was left in on purpose (`MASTER_PROMPT.md:593-596`). Its authored data (`kSettlementBase` + `kEcon*` + the population-tier bonus at `items.cpp:328-332`) is the intended template source.
- Settlement/village `Inventory` **is** written/read by the save file (`save.cpp:629,644,659,674`), but since it is never filled, it always serializes as an empty stack list. Stopping its serialization loses nothing.
- **ECS entities are not serialized.** `save.cpp` contains no `reg`/`entt`/registry access — only `PlayerState`, `Settlement`, `Village`, `Spire`, `Marker`, `Faction`, `Politik`, `WorldTime` are persisted. Macro NPCs (and thus their `NpcInventory`) and corpses are **regenerated / transient**.
- **The "thousands of landmarks" do not exist yet.** `LandmarkType` (`landmark_registry.h:41-52`) defines `City, Village, Spire, Ruin, Lair, Shrine, Mine, Tower` and `kLandmarks` gives each a `LandmarkDef` (`landmark_registry.h:64-74`), but only `City/Village/Spire` are instantiated (as `GameState` vectors). `Ruin/Lair/Shrine/Mine/Tower` are **defined but never placed** (`grep LandmarkType::` → only the def table). The container system must *enable* scaling to thousands, not just serve today's few hundred.
- **No "chest" concept exists** (`grep hest src/` → nothing). Subworld chests are entirely new; the design must make them a one-row archetype.

### 1.3 The loot pipeline (the part that already works and must be preserved)

- `LootEntry { const char* item; float chance; int min, max; int minLevel; }` — `items.cpp:70-76`. The universal weighted-line atom.
- `roll_loot(table, n, level, rng)` — `items.cpp:225-239`: per entry, skip if `level < minLevel`; if `rng() < chance`, `qty = min + int(rng()*(max-min+1))`. **Two RNG draws per entry, in table order.**
- `roll_loot_profile(lootId, level, rng)` — `items.cpp:281-285`: one keyed table (`kLootProfiles`, `items.cpp:167-179`) covering 8 NPC roles + `wildlife/demons/bandits`. This is the *already-unified* loot path.
- `generate_loot_gold(level, factionId, rng)` — `items.cpp:292-298`: gold is rolled **separately** by a formula `(3 + level*2) * factionMult * (1 + jitter)`, parallel to items (§9.2 wants this folded in).
- **The `RngFn` wart:** `items.cpp` takes `RngFn = float(*)()` (`items.h:108`). Callers bridge the real `sm::Rng` (`core/rng.h`) through a `thread_local Rng*` + a free function — duplicated in two places (`engine.cpp:85-88` `gLootRng`/`loot_rng_f01`; `npc_spawn.cpp:17-18` `tl_rng`/`tl_rng_f01`). The new populator should take `Rng&` directly and retire this shim.

### 1.4 The death → corpse → pickup flow (must keep working)

1. NPC dies → `resolve_subworld_deaths` (`engine.cpp:1316-1355`): copy the entity's `NpcInventory` if present; else `roll_loot_profile(lootId, lvl)`; roll `generate_loot_gold`; seed = `worldSeed ^ (entityId*kEntityLootMix) ^ (lvl*7919)` (`engine.cpp:1321-1323`). Create a corpse entity with `Structure::Corpse` + `CorpseLoot{inv, gold}`.
2. Player interacts → `SubworldEngine::interact` (`engine.cpp:942-972`): nearest corpse within 12u, transfer `loot.gold`→`player.gold` and each stack→`player.inventory`, destroy corpse.
3. Trade mutates a live `NpcInventory` in place (`macro_overlay.cpp:1042`, `main.cpp:849-852`) — so containers must support **runtime add/remove**, not just read.

### 1.5 Constraints that bind the design

- `-fno-exceptions -fno-rtti`, POD components, no virtuals on hot data (`AGENTS.md:97,158`). ECS components **under ~64 bytes**; split larger blobs (`AGENTS.md:168-169`).
- **Layering** (`AGENTS.md:219-230`): `L1 macro/ → ecs/, core/, gl/`; `L2 sub/ → macro/, ecs/, core/, gl/`; `ui/` above all. A lower layer must not include a higher one.
  - Note: `ecs/components.h` *already* includes `macro/items.h` (`components.h:3`) so that `NpcInventory`/`CorpseLoot` can embed `Inventory`. That is a lower(`ecs`)→higher(`macro`) include — tolerated today but irregular. **Our design removes it** (see §1 below: the ECS component becomes a bare `uint32` handle with no macro include).
- Seeded RNG only from `core/rng.h` (`Rng`, `next_f01`, `next_int`, `hash3` — `rng.h:7-38`). Save: `kSaveVersion = 8` (`state.h:20`), `kMagic = 0x534D5341` (`save.cpp:20`); loader **hard-rejects** any version mismatch (`save.cpp:263-264,1092`) — there is no migration path, a bump invalidates old saves.

---

## 2. Design law mapping

| DESIGN LAW | How this proposal satisfies it |
|---|---|
| Minimum systems / max functionality | One `ContainerStore`, one `ContainerRef`, one `ContainerTemplate` table replaces: settlement inventory generator, NPC inventory generator, corpse loot, and (future) chests + landmark stock. `Inventory`/`ItemStack`/`LootEntry` are **reused**, not re-invented. |
| NO hardcoding (new archetype = one row) | A container archetype is one `ContainerTemplate` row keyed by a stable id. The economy `if/else-if` chain (`items.cpp:313-317`) becomes a table lookup. Adding "dungeon chest" or "mine stock" = one row, zero branches. |
| Single source of truth | Item ids stay in `kCatalog` (`items.cpp:21-57`). Weighted lines stay `LootEntry`. Templates *compose* existing spans; no data is duplicated. |
| Four-layer arch | `ContainerRef` (pure POD handle) lives in `ecs/components.h` — **no new include**. `ContainerStore` + templates live in **L1 `macro/containers.{h,cpp}`** (they need the item catalog + economy, both L1). Consumers (`sub/`, `ui/`) are allowed to include `macro/`. `ecs/` gains *no* dependency (it loses one — see §1). |
| POD components < 64 bytes | The ECS/holder component is `ContainerRef` = **4 bytes**. The `Container` control block lives in the macro store (a plain array element, not an EnTT component), so the 64-byte ECS rule does not bind it; it is still kept tight (~76-140 bytes). |
| Seeded `Rng` from core/rng.h | The populator takes `sm::Rng&` directly and reproduces `roll_loot`'s draw order. The `thread_local RngFn` shim is retired. |

---

## 3. §1 — Container representation (the concrete types)

### 3.1 The handle (ECS/holder side — pure POD, zero includes)

```cpp
// ecs/components.h  (add; needs NO new include — this REMOVES the macro/items.h dependency
// that NpcInventory/CorpseLoot forced at components.h:3)
namespace sm::ecs {
    struct ContainerRef {          // 4 bytes
        std::uint32_t id = 0;      // 0 == kNullContainer (none / not yet materialized)
    };
}
```

Every holder carries one `ContainerRef`:
- `ecs` entities (NPC, corpse, chest) get it as a component.
- `Settlement`/`Village` get it as a plain field (they are not ECS entities).
- The player *may* get one (optional — see §4.5; there is exactly one player, so the win is uniformity, not scale).

`id` packs an index and a generation for stale-handle detection:

```cpp
// macro/containers.h
inline constexpr std::uint32_t kNullContainer = 0u;
// id layout: [ generation : 8 ][ index : 24 ]  (index 0 reserved => id 0 is always null)
constexpr std::uint32_t container_index(std::uint32_t id) { return id & 0x00FFFFFFu; }
constexpr std::uint32_t container_gen  (std::uint32_t id) { return id >> 24; }
```

24-bit index = 16.7M live containers; 8-bit generation catches use-after-free of recycled slots (corpses).

### 3.2 The item atom and the container control block (SoA, macro side)

```cpp
// macro/containers.h  (L1)
inline constexpr std::uint32_t kContainerCap = 16;  // >= item_catalog().size() (13 today).
                                                    // A container holds <= one stack per id
                                                    // (items.h:5), so 16 is a SEMANTIC bound,
                                                    // not a guess. Bump only if kCatalog grows.

struct ContainerItem {           // 8 bytes — cache-friendly, integer-keyed
    std::uint16_t itemId;        // catalog INDEX (new item_index()), not a std::string
    std::uint16_t flags;         // reserved: bound / quest / equipped (0 today)
    std::int32_t  count;         // matches ItemStack::count width
};

enum class ContainerState : std::uint8_t { Unmaterialized = 0, Live = 1 };

struct Container {               // control block; lives in ContainerStore::slots
    std::uint16_t   templateId;  // authored archetype row (drives regen + eviction)
    std::uint16_t   generation;  // must match the handle's generation
    std::uint32_t   seed;        // deterministic roll seed
    std::uint8_t    count;       // # live stacks (0..kContainerCap)
    ContainerState  state;
    std::uint16_t   pad;
    ContainerItem   items[kContainerCap];   // inline; NO per-container heap block
};
// sizeof(Container) = 12 header + 16*8 = 140 bytes. (u16 count + no flags => 76 bytes.)
```

Why inline `items[kContainerCap]` instead of a shared item pool:
- The stacking rule (`items.h:5`) bounds a container to `<= |catalog|` distinct stacks. With a 13-row catalog, 16 slots always suffice. **No sub-allocator, no ranges to move on add/remove, O(1) everything, each container is one contiguous block.**
- A shared flat pool (`std::vector<ContainerItem>` + per-container `{offset,length}`) is the classic SoA alternative and is *more* compact for huge catalogs, but it needs a slab allocator and range moves on mutation. Given the fixed small catalog, that complexity buys nothing. **Recommended: inline. Documented alternative: shared pool** (adopt only if the catalog ever becomes large/unbounded — it cannot today by the stacking rule).

### 3.3 The store (one arena on GameState)

```cpp
// macro/containers.h  (L1)
struct ContainerStore {
    std::vector<Container>     slots;      // index space; slots[0] reserved (null)
    std::vector<std::uint32_t> freeList;   // recycled indices (corpses, released chests)

    // --- lifecycle ---
    ContainerRef reserve(std::uint16_t templateId, std::uint32_t seed);  // O(1), Unmaterialized
    void         release(ContainerRef);                                  // -> freeList, ++generation
    bool         valid  (ContainerRef) const;                            // index + generation check

    // --- access (materializes lazily on first touch) ---
    std::span<ContainerItem> items(ContainerRef);        // rolls template if Unmaterialized
    int  count (ContainerRef, std::uint16_t itemId) const;
    void add   (ContainerRef, std::uint16_t itemId, int n);
    bool remove(ContainerRef, std::uint16_t itemId, int n);
    void evict (ContainerRef);                           // Live -> Unmaterialized (frees nothing but count)

    // --- boundary adapter for legacy string-keyed call sites (UI, player.inventory) ---
    Inventory to_inventory(ContainerRef) const;          // integer -> string, only at UI edge
    void      merge_into  (Inventory& dst, ContainerRef) const;  // corpse -> player.inventory
};
```

`GameState` owns exactly one:

```cpp
// state.h  (add one field to GameState)
struct GameState {
    …
    ContainerStore containers;   // the single arena for ALL item holders
};
```

Every consumer already has a `GameState&` in scope where it needs items: `engine.cpp` holds `gs_`; `macro_overlay.cpp` holds `gs`; `npc_spawn.cpp` is L1. So resolving a `ContainerRef` → items is always `gs.containers.items(ref)`. **No layering violation** (store is L1; consumers are L2/ui, allowed to include L1).

### 3.4 Two new one-line catalog helpers (additive, `items.{h,cpp}`)

```cpp
// items.h
std::uint16_t     item_index(const std::string& id) noexcept; // catalog index, 0xFFFF if unknown
const char*       item_id_at(std::uint16_t index) noexcept;    // reverse; "" if out of range
```
These are the only additions to `items.*` in Phase 0. They let `ContainerItem` be integer-keyed while the boundary adapter (`to_inventory`) still speaks strings for the save file and the trade UI.

### 3.5 Cache-friendliness & memory budget at scale

**Naive approach the owner warns against** — an inline `Inventory` per landmark:
- `Inventory` = a `std::vector` header (24 B) that heap-allocates on first `add`. A rolled settlement (~2 base + 2 econ + 1-2 pop = ~6 stacks) → ~6 × `sizeof(ItemStack)`. `ItemStack` = `std::string`(32 B on libc++) + `int`(4) + pad → 40 B ⇒ ~240 B heap + header + malloc bookkeeping ≈ **~280 B and one heap block per landmark**.
- 10k landmarks eager ⇒ **~2.8 MB + 10,000 independent heap allocations.** Iterating "all landmark stock" chases 10k cold pointers — cache-hostile, fragmented.

**This proposal:**
- Holder handle: `ContainerRef` = **4 B**. 10k × 4 = **40 KB** (contiguous, or omitted entirely if kept in a side map for only-materialized holders).
- Unmaterialized container: **0 store bytes.** `(templateId, seed)` is derivable from the holder's existing fields (settlement `economy` + `id`, `worldSeed`), so nothing is stored until first access.
- Materialized `Container`: 140 B (or 76 B with u16 counts), inline, contiguous in `slots`.
  - Realistic (player has opened ~500 containers): 500 × 140 = **~70 KB.**
  - Pathological (materialize ALL 10k at once): 10k × 140 = **~1.4 MB**, still contiguous, evictable, regenerable — vs ~2.8 MB + 10k heap blocks for the naive path.
- The whole arena is one `std::vector<Container>` ⇒ linear scans (e.g. "tick all settlement economies") are cache-linear, and any container can be **evicted to Unmaterialized and regenerated bit-identically** from `(templateId, seed)`.

At 100k landmarks the story is unchanged: 400 KB of handles, ~1.6 MB if fully resident, ~0 if lazily left cold. **O(1) per operation, O(materialized) memory — not O(landmarks).**

---

## 4. §2 — One mechanism for all five holders

Every holder maps to the ONE mechanism by choosing a `templateId` and a `seed`. Nothing else differs.

| Holder | `templateId` (archetype row) | `seed` | Materialize when | Lifetime |
|---|---|---|---|---|
| **Settlement / city** | `economy` string → row (`"settlement.farming"`, …) | `worldSeed ^ (id*A)` | player opens trade / stock inspected | persistent (regen on load) |
| **Village** | `"settlement.village"` (+ economy overlay) | `worldSeed ^ (id*A)` | on open | persistent (regen on load) |
| **Landmark (ruin/lair/shrine/mine/tower)** | one row per `LandmarkType` (`"landmark.ruin"`, …) | `hash3(x, y, worldSeed)` (`rng.h:33`) | on enter/loot | persistent-by-seed (never stored; **this is the thousands-scale win**) |
| **Subworld chest** | `"chest.common"`, `"chest.dungeon"`, … (new rows) | `hash3(x, y, sceneSeed)` | on open | subworld-session (released on leave) |
| **NPC entity** | NPC role row (reuse `kNpcLoot` / `npc_loot_id`) | spawn seed | at spawn (already eager) | transient (regenerated per spawn) |
| **Corpse entity** | the dead entity's own role/creature loot row | `worldSeed ^ (entityId*kEntityLootMix) ^ (lvl*7919)` (as `engine.cpp:1321-1323`) | at death | transient (released on pickup) |

The corpse case shows the unification: today `engine.cpp:1316-1355` hand-rolls `roll_loot_profile` + `generate_loot_gold` into a `CorpseLoot`. Under this design, a corpse is just `store.reserve(deadRoleTemplate, deathSeed)`; the same template machinery that stocks a city stocks a corpse. **Gold folds in naturally**: `gold` is catalog index 0 (`items.cpp:23`), so a gold line is just another row entry — this is exactly §9.2 ("gold unification"), realized for free (see §5.4, owner's call).

Chests (new) cost one enum value + one template row + a spawn call in the relevant `sub/gens/` interior generator. No engine branch: a chest is an entity with `Structure` + `ContainerRef`, looted by the *same* `interact()` path that loots corpses (`engine.cpp:942-972`) once that path keys on "has `ContainerRef`" instead of "is `Corpse`".

---

## 5. §3 — Population: economy tables → authored templates

### 5.1 The template table (one keyed table, built from existing atoms)

```cpp
// macro/containers.h  (L1)
enum class ScaleMode : std::uint8_t { None, PerLevel, PopulationTier, GoldFormula };

struct ContainerTemplate {
    std::string_view          id;        // stable key: "settlement.farming", "npc.merchant", …
    std::span<const LootEntry> base;     // REUSE items.cpp LootEntry rows verbatim
    std::span<const LootEntry> overlay;  // optional 2nd span (e.g. economy overlay); may be empty
    ScaleMode                 scale;      // how the roll context (level/population) modulates it
};
```

The whole authored surface is a `constexpr ContainerTemplate kTemplates[]` in `containers.cpp`. It *references* the existing spans — no data is copied:

```cpp
// containers.cpp  (illustrative rows — data all already exists in items.cpp)
constexpr ContainerTemplate kTemplates[] = {
    // settlements: base + economy overlay + population-tier bonus (== old generate_settlement_inventory)
    {"settlement.farming",  kSettlementBase, kEconFarming,  ScaleMode::PopulationTier},
    {"settlement.mining",   kSettlementBase, kEconMining,   ScaleMode::PopulationTier},
    {"settlement.trade",    kSettlementBase, kEconTrade,    ScaleMode::PopulationTier},
    {"settlement.fishing",  kSettlementBase, kEconFishing,  ScaleMode::PopulationTier},
    {"settlement.crafting", kSettlementBase, kEconCrafting, ScaleMode::PopulationTier},
    {"settlement.village",  kSettlementBase, {},            ScaleMode::PopulationTier},
    // NPC roles: reuse the existing loot tables (parity with generate_npc_inventory / roll_loot_profile)
    {"npc.peasant",   kPeasantLoot,   {}, ScaleMode::PerLevel},
    {"npc.merchant",  kMerchantLoot,  {}, ScaleMode::PerLevel},
    // … the other 6 roles + wildlife/demons/bandits …
    // future chests / landmarks: ONE row each, no engine change
    {"chest.common",  kChestCommon,   {}, ScaleMode::PerLevel},
    {"landmark.ruin", kRuinLoot,      {}, ScaleMode::PerLevel},
};
```

To share the same lookup surface as the loot registry, `roll_loot_profile` and the settlement path become one call: `roll_container(templateId, ctx, rng)`.

### 5.2 The generic roll (preserves `roll_loot` draw order for save/seed parity)

```cpp
// containers.cpp
struct RollCtx { int level; int population; };

void roll_container(Container& c, const ContainerTemplate& t, RollCtx ctx, Rng& rng) {
    apply_span(c, t.base,    ctx, rng);          // == items.cpp roll_loot(): per entry, chance draw then qty draw
    apply_span(c, t.overlay, ctx, rng);          // economy overlay
    if (t.scale == ScaleMode::PopulationTier) {  // == items.cpp:328-332 (potion_mp / misc_gem tiers)
        const int tier = ctx.population / 200;
        if (tier >= 1) add(c, item_index("potion_mp"), 1 + int(rng.next_f01()*float(tier)));
        if (tier >= 2) add(c, item_index("misc_gem"),      int(rng.next_f01()*float(tier)));
    }
    c.state = ContainerState::Live;
}
```

`apply_span` mirrors `roll_loot` exactly (`items.cpp:225-239`) — two draws per entry, table order — so **NPC and corpse loot stay bit-identical** for a given seed (settlement stock is brand-new content, so no prior seed to match). The populator takes `Rng&` directly, retiring the `thread_local RngFn` shim (`engine.cpp:85-88`, `npc_spawn.cpp:17-18`).

### 5.3 Lazy vs eager — the scale decision

- **Lazy (default for landmarks & chests):** `reserve()` sets `state=Unmaterialized` and stores only `(templateId, seed)`. First `items()`/`count()` call runs `roll_container` into the inline array. Thousands of ruins/lairs cost only their handle (4 B) — or nothing, if the handle is derived on demand from the holder's identity. This is what makes "thousands of landmarks each with inventory" O(1)-cheap.
- **Eager (NPCs, corpses):** already spawned eagerly today; keep rolling at `reserve` time. These are few (subworld-session counts) and transient.
- **Eviction:** a `Live` container far from the player can be `evict()`ed back to `Unmaterialized` (drop `count`); it regenerates identically on next touch because contents = `f(templateId, seed)`. Enables a fixed memory ceiling regardless of world size.

### 5.4 Determinism

Contents are a pure function of `(templateId, seed, ctx)`. `seed` is derived deterministically: settlements `worldSeed ^ (id*A)`; landmarks/chests `hash3(x, y, worldSeed)` (`rng.h:33`); corpses the existing `engine.cpp:1321-1323` mix. Same world seed ⇒ same stock everywhere, every load, without storing anything.

---

## 6. §4 — The data-row extension point

Adding a container archetype is **one `ContainerTemplate` row** + (for a brand-new item mix) **one `constexpr LootEntry[]`** beside the existing tables in `items.cpp`. Concretely, to add a dungeon chest:

1. `constexpr LootEntry kChestDungeon[] = { {"potion_hp",0.8f,1,3,0}, {"misc_gem",0.5f,1,2,3}, {"wpn_dagger",0.3f,1,1,5} };` (one array, beside `kEcon*`).
2. `{"chest.dungeon", kChestDungeon, {}, ScaleMode::PerLevel},` (one row in `kTemplates`).
3. In the dungeon interior generator (`sub/gens/`), spawn an entity with `Structure` + `store.reserve(template_id("chest.dungeon"), hash3(x,y,sceneSeed))`.

No `if`/`switch` anywhere in the container machinery grows. The economy `if/else-if` chain (`items.cpp:313-317`) is *deleted*, not extended — economy→template is a keyed lookup. This is the "no hardcoding" law made literal.

---

## 7. §5 — Migration plan (additive-first, green at every step)

Each phase compiles and passes smokes on its own. No phase deletes a live path before its replacement is wired.

**Phase 0 — Scaffold (pure addition, nothing uses it yet).**
- Add `item_index()` / `item_id_at()` to `items.{h,cpp}`.
- Add `macro/containers.{h,cpp}`: `ContainerItem`, `Container`, `ContainerStore`, `ContainerTemplate`, `kTemplates` (referencing existing spans), `roll_container`, `to_inventory`. Register `containers.cpp` in `CMakeLists.txt`.
- Add `ecs::ContainerRef` to `components.h`.
- Add `ContainerStore containers;` to `GameState`.
- **Green build, zero behavior change.** (No `kSaveVersion` bump — no on-disk change yet.)

**Phase 1 — Settlements (this is where `generate_settlement_inventory` is REPLACED).**
- In `populate_landmarks_from_politik` (`state.cpp:230-287`), after building each settlement/village, `s.containerRef = gs.containers.reserve(template_for_economy(s.economy), worldSeed ^ (s.id*A))` (lazy). Same for villages.
- Settlement trade UI reads stock via `gs.containers.items(s.containerRef)` (materialize-on-open) instead of the empty `s.inventory`.
- **Delete `generate_settlement_inventory`** from `items.{h,cpp}` (`items.h:112`, `items.cpp:300-334`) — it is dead, so nothing breaks. Its authored data now lives as the `settlement.*` template rows; its `if/else-if` chain and pop-tier bonus are reproduced by `roll_container` (§5.2). **This is the exact replacement point the owner asked to mark.**
- Keep the inline `Settlement::inventory`/`Village::inventory` fields for now (still serialized, still empty) — removing them is Phase 5.
- **Green build.** Settlements now actually have stock for the first time.

**Phase 2 — NPC inventories.**
- Give NPC entities a `ContainerRef` component. At spawn (`npc_spawn.cpp:90-95`, `engine.cpp:1099-1108`), `reserve(npcRoleTemplate, spawnSeed)` (eager) instead of building an inline `NpcInventory`.
- Trade (`macro_overlay.cpp:1042`, `main.cpp:849`) mutates via `store.add/remove(ref, …)`.
- Keep `NpcInventory` as a thin deprecated alias during the phase if any smoke still reads it; remove once all call sites move. `generate_npc_inventory` stays as the `npc.*` template data source.
- **Green build.** Removes the `ecs → macro/items.h` include coupling for this component.

**Phase 3 — Corpses (+ optional gold fold, §9.2).**
- Death path (`engine.cpp:1316-1355`): create corpse entity with `Structure` + `ContainerRef` = `reserve(deadRoleTemplate, deathSeed)`; drop `CorpseLoot`.
- Loot path (`engine.cpp:942-972`): key on "entity has `ContainerRef`" (so it also loots chests), `store.merge_into(player.inventory, ref)`, then `release(ref)`.
- **Owner's call:** fold gold into the template as a `gold` line / `ScaleMode::GoldFormula` row, retiring `generate_loot_gold`'s separate field. Completes §9.2. If deferred, keep a parallel `int gold` on the corpse.
- **Green build.**

**Phase 4 — Chests + extra landmarks (new content, pure data).**
- Add `chest.*` and `landmark.*` template rows; spawn chests/landmark stock in `sub/gens/` and the landmark placement pass. No engine change — the Phase 3 loot path already handles any `ContainerRef` holder.

**Phase 5 — Cleanup / full SoA (the riskiest, do last).**
- Remove the now-unused inline `Settlement::inventory` / `Village::inventory` fields and stop serializing them (regenerate from template+seed on load). **This changes the save format ⇒ bump `kSaveVersion` 8→9** (see §8). The player inventory optionally becomes a `ContainerRef` too.
- Optionally flip the boundary: make integer-keyed `ContainerItem` canonical end-to-end and reduce `to_inventory` usage to just the trade/character UI.

---

## 8. §6 — Save / serialization implications

- **ECS-side holders (NPCs, corpses, chests): nothing to do.** They are already not serialized (`save.cpp` has no registry access); they regenerate on world/subworld (re)build. `ContainerRef`s into transient containers are re-`reserve`d at spawn.
- **Settlement/village stock: regenerate, don't store.** It is `f(templateId, seed)` and `(templateId, seed)` are derivable from fields already saved (`economy`, `id`, `worldSeed`). This matches the repo's regenerate-from-seed philosophy.
  - Phases 1-4 keep the existing (empty) `write_inventory`/`read_inventory` calls (`save.cpp:629,644,659,674`) so **no version bump is needed** through Phase 4.
  - Phase 5 removes those calls and derives stock on load ⇒ on-disk layout changes ⇒ **`kSaveVersion` 8→9** (`state.h:20`). Because the loader hard-rejects mismatches (`save.cpp:263-264`), old saves are invalidated — acceptable per the repo's regenerate-the-world stance, but it is the owner's call *when* to take the bump.
- **Player inventory stays serialized** regardless (it holds hand-acquired, non-regenerable items). If the player becomes a `ContainerRef` (Phase 5, optional), `write_player`/`read_player` (`save.cpp:566-620`) serialize that one container's items explicitly — again a v9 change.
- **`ContainerStore` itself is never serialized.** It is a runtime cache: persistent containers are reconstructed from holder identity, transient ones are respawned. Zero new blobs on disk.

---

## 9. §7 — Risks & open questions (owner decides)

1. **[RISKIEST] The int-key boundary vs the string-keyed world.** `ItemStack`/`Inventory`/save/trade-UI/`use_item` are all `std::string`-keyed (`items.h:54`, `save.cpp:412-430`). The cache-friendly store is integer-keyed. The plan keeps a `to_inventory` adapter at the UI/save edge and flips the core to integers only in Phase 5. **If the owner wants full SoA cache-friendliness sooner, Phase 5 moves earlier and touches save + all trade/inventory UI at once — a bigger, riskier change and a `kSaveVersion` bump.** *Decision needed: adapter-at-the-edge for a long time (low risk, a little conversion cost when a container is opened) vs. a hard integer cutover (max performance, one disruptive change)?* This is the single call most likely to reshape the plan.
2. **Inline cap vs shared pool.** Inline `items[16]` is O(1) and allocation-free but assumes the "one stack per id" rule and a small catalog hold forever. If the catalog is ever meant to grow large/unbounded, the shared-pool variant is needed instead. *Decision: is the 13-row catalog + one-stack-per-id rule permanent?*
3. **Gold unification (§9.2) coupling.** Folding gold into templates is elegant and free here, but the current gold formula `(3+level*2)*mult*(1+jitter)` (`items.cpp:292-298`) isn't a flat min/max — it needs a `GoldFormula` scale mode (one row type) to stay in "one table." *Decision: fold gold in Phase 3, or keep it a separate sidecar for now?*
4. **Where does the player live?** Exactly one player, always materialized, holds non-regenerable items. Making it a `ContainerRef` is uniform but low-value and forces a save change. *Decision: unify the player, or leave `PlayerState::inventory` as the one hand-authored exception?*
5. **Handle stability across regeneration.** Persistent settlement handles are re-`reserve`d in a deterministic order on load, so indices are stable *if* the populate order is stable (it is — `populate_landmarks_from_politik` iterates politik cities in order). Transient handles recycle via `freeList` + generation. *Confirm: is deterministic reserve-order guaranteed by every populate path we add?*
6. **Determinism parity requirement.** NPC/corpse loot must stay bit-identical to today (live saves/seeds). The plan preserves `roll_loot`'s exact draw order; any deviation in `apply_span` silently changes seeded loot. Needs a smoke asserting a known (seed → loot) vector before/after Phase 2-3.
7. **`ecs → macro` include direction.** The design *improves* hygiene (drops `components.h:3`'s `macro/items.h` include by using a bare handle), but `ContainerStore` living in L1 `macro/` means L2 `sub/engine.cpp` calls into it — already allowed, but confirm no `core/`- or `ecs/`-level code ever needs to resolve a container (it shouldn't; keep resolution in `macro/`+ above).

---

## 10. Appendix A — exact authored data being promoted to templates (verbatim, `items.cpp`)

- `kSettlementBase` (`items.cpp:198-201`): `food_bread` ×5-14 @1.0; `potion_hp` ×3-9 @1.0.
- `kEconFarming` (`:202-205`), `kEconMining` (`:206-209`), `kEconTrade` (`:210-215`), `kEconFishing` (`:216-219`), `kEconCrafting` (`:220-223`).
- Population-tier bonus (`:328-332`): `potion_mp` if `pop/200 >= 1`; `misc_gem` if `>= 2`.
- NPC roles `kPeasantLoot`…`kSorceressLoot` (`:82-125`) + fauna `kWildlifeLoot`/`kDemonsLoot` (`:145-153`), already unified under `kLootProfiles` (`:167-179`).

All of the above become `ContainerTemplate` rows that *reference* these spans — the data is not duplicated, and `items.cpp` remains its single source of truth.

## 11. Appendix B — call-site checklist (what each phase touches)

- `items.{h,cpp}`: +`item_index`/`item_id_at` (P0); −`generate_settlement_inventory` (P1); −`generate_loot_gold` (P3, optional).
- `macro/containers.{h,cpp}`: new (P0). `CMakeLists.txt`: +1 source (P0).
- `ecs/components.h`: +`ContainerRef` (P0); −`macro/items.h` include, −`NpcInventory`, −`CorpseLoot` (P2-P3).
- `macro/state.h`: +`ContainerStore containers` and holder `ContainerRef` fields (P0-P1); −inline `Inventory` fields (P5).
- `macro/state.cpp`: settlement/village populate (P1).
- `macro/npc_spawn.cpp`, `sub/engine.cpp`: spawn + death + interact paths (P2-P3); retire `RngFn` shims.
- `ui/macro_overlay.cpp`, `app/main.cpp`: trade + inspect via store (P2-P3).
- `macro/save.cpp`: unchanged P0-P4; format change + `kSaveVersion`→9 (P5).

---

*End of proposal. Build nothing until the owner picks answers to §9 (especially §9.1 — the int-key boundary timing).*
