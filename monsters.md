# Monsters & Loot — Таблица монстров и таблица лута

**ONE global monster table and ONE loot table — both single sources of
truth.** Every creature from rabbit to dragon is a single row in the monster
registry; every drop — monster *or* NPC — resolves through the one loot
registry. Adding content is one data row, never an `if`-chain.

- **Code:** [sub/fauna.h](src/sub/fauna.h), [sub/fauna.cpp](src/sub/fauna.cpp)
  (global monster table + accessors); [macro/items.h](src/macro/items.h),
  [macro/items.cpp](src/macro/items.cpp) (`roll_loot_profile`, loot registry);
  [sub/engine.cpp](src/sub/engine.cpp) (`resolve_subworld_deaths` death/loot/XP
  dispatch, `spawn_hostile_npc` spawn-any-creature branch);
  [sub/spawn.cpp](src/sub/spawn.cpp) (ambient fauna spawn)
- **TS origin:** `subworld/fauna.ts`, `subworld/spawn.ts`, `game/items.ts`,
  `game/npc.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System, §L2

## The monster table

`FaunaEntry` ([sub/fauna.h](src/sub/fauna.h)) is the row schema; `fauna.cpp`
holds the rows. Each entry is the *complete* definition of a creature — no
stats scattered across spawn sites.

| Field | Meaning |
|-------|---------|
| `id` | Stable machine id (`"wolf"`) — **the source of truth**, distinct from `label` |
| `label` | Display name (`"Wolf"`) |
| `weight` | Spawn weight inside a biome/feature table (0 = never spawns randomly, still exists) |
| `faction` | `FaunaFaction` — Neutral / Wildlife / Bandits / Demons (drives hostility + gold + default loot) |
| `ai` | `FaunaAi` — Wander / Flee / Combat |
| `combat` | `CombatTemplate` (the universal combat spine, shared with NPCs) |
| `baseLevel` | Level floor; spawn adds `floor(rng()*2)` |
| `color` | `0xRRGGBB` procedural billboard tint |
| `radius` | Billboard footprint |
| `archetype` | `CreatureArchetype` procedural body plan (render only) |
| `lootId` | Loot-profile override; `null` ⇒ faction default |
| `xpReward` | Per-creature XP base; `0` ⇒ generic level-scaled |

**Catalog (19 rows today, append-only).** Wildlife (12): `rabbit deer fox wolf
bear boar snake hawk frog goat eagle crocodile`. Monsters (7): `goblin skeleton
troll swamp_thing ice_wraith sand_scorpion stone_golem`.

> **⚠ The catalog order is append-only. Never reorder or delete rows.** The
> subworld spawn path bakes the ECS `NPCKind.type` as `0x100 | catalogIndex`, so
> reordering re-keys every live entity of that creature. Append new creatures at
> the end.

### Accessors (mirror `item_catalog()`)

```cpp
std::span<const FaunaEntry* const> creature_catalog();       // flat enumeration
const FaunaEntry* creature_def(std::string_view id);         // "wolf" -> row (nullptr if unknown)
const FaunaEntry* creature_def_from_kind(std::uint16_t kind);// 0x100|idx -> row (nullptr if not a monster)
int               creature_index(const FaunaEntry* entry);   // row -> catalog index (-1 if absent)
```

### The `≥0x100` discriminator (load-bearing)

`NPCKind.type` is the one field that tells a monster from a humanoid NPC:

- **Humanoid NPC:** `type < NPCType::Count` (=8). One of the 8 `NPCType` roles
  (Peasant … Sorceress). Has an Elder-Scrolls-style character sheet (future
  universal NPC system — see [rpg.md](rpg.md)).
- **Monster/fauna:** `type = 0x100 | catalogIndex`. No character sheet.
  `creature_def_from_kind(type)` recovers the row.

`monsters ≠ NPCs` — they are deliberately separate branches everywhere (spawn,
loot, XP). The `0x100` bit is what keeps them apart.

## The loot table

`roll_loot_profile(lootId, level, rng)` ([macro/items.h](src/macro/items.h)) is
the **single** loot entry point. It looks up a named profile in the
`kLootProfiles[]` registry ([macro/items.cpp](src/macro/items.cpp)) and rolls it
through the shared `roll_loot()` core. Unknown / empty `lootId` ⇒ no items.

Registered profile ids:

- **8 NPC roles** (npc.h enum order): `peasant woodcutter merchant caravan
  bandit guard witch sorceress`. `npc_loot_id(npcType)` maps an `NPCType`
  integer to its id (`""` if out of range).
- **3 faction defaults:** `wildlife`, `demons`, `bandits`. Any creature without
  a `lootId` override falls back to its faction's default profile.

> The `bandits` profile (reusing `kBanditLoot`) closes a latent zero-loot gap:
> before unification, a Bandits-faction creature matched no faction loot string
> and dropped **zero items**. Now every faction has a real profile.

### Death dispatch (one keyed path)

`resolve_subworld_deaths` ([sub/engine.cpp](src/sub/engine.cpp)) routes *all*
drops through the one resolver:

```cpp
const char* lootId;
if (kind->type < NPCType::Count)                 lootId = npc_loot_id(kind->type);      // humanoid
else if (cd = creature_def_from_kind(...); cd && cd->lootId && cd->lootId[0])
                                                 lootId = cd->lootId;                    // per-creature override
else                                             lootId = faction_id_for_kind(kind);     // faction default
auto stacks = roll_loot_profile(lootId, lvl, &loot_rng_f01);
```

Gold is still rolled by `generate_loot_gold(level, faction)` with
`gold_faction_mult` (wildlife 0.1 / demons 0.6 / bandits 0.8 / else 1.0) — a
noted later refinement, kept separate for now to stay behavior-preserving.

## Spawn paths (three, one table)

1. **Ambient fauna** — [sub/spawn.cpp](src/sub/spawn.cpp) rolls a biome/feature
   `FaunaTable` (`roll_fauna`) and emplaces each pick. This is the natural
   wildlife/monster population of a subworld cell.
2. **Directed hostile spawn** — `spawn_hostile_npc`
   ([sub/engine.cpp](src/sub/engine.cpp)) resolves `creature_def(id)` first
   (monster branch: fauna combat/archetype/color, `0x100|index`, no char sheet),
   and only falls back to the humanoid `NPCType` path if the id is not a
   creature. So the console `spawn wolf` / `spawn goblin` works with no special
   casing.
3. **Console `spawn <id>`** — [app/main.cpp](src/app/main.cpp) routes straight
   into `spawn_hostile_npc`, so it accepts any monster id or NPC role.

## Per-creature XP

The death path awards `xpReward + (level-1)*5` when a creature sets a non-zero
`xpReward`; `xpReward = 0` keeps the generic `exp_from_fight(level)` (=10·level)
fallback. Humanoids use `npc_xp_reward(type, level)`. XP goes to the killing
blow's owner ([microcombat.md](microcombat.md)).

## Data-driven extension

- **Add a creature** → append one `FaunaEntry` row in `fauna.cpp` (with a stable
  `id`) and reference it in one or more biome/feature tables. Spawn, loot (via
  faction default or a `lootId`), XP, rendering (via `archetype`), and the
  console `spawn` all pick it up with **no engine change**.
- **Add a loot profile** → one `kLootProfiles[]` row keyed by a new `lootId`;
  point a creature's `lootId` or an NPC role at it.
- **Add an item** → one `item_catalog()` row (see [rpg.md](rpg.md)); loot tables
  reference it by id.

## Connections

Combat resolution and hostility → [microcombat.md](microcombat.md); items,
inventory, and the item catalog → [rpg.md](rpg.md); NPC roles and the
NPC-as-soldier model → [macrosim.md](macrosim.md); biome spawn density →
[biomes.md](biomes.md); danger scaling of spawns → [zones.md](zones.md).

## Deferred (future tracks, built on this foundation)

1. **Route-1 roaming macro monster parties** drawing from this same table
   (macro-scale hordes that descend into subworld combat).
2. **The universal NPC character-sheet system** (Elder Scrolls / Oblivion model:
   every NPC — generic and plot — has a per-instance sheet + inventory through
   one path). Monsters stay sheet-less; this is the NPC branch only.
3. **Populate `xpReward` / `lootId` per creature** (data/balance pass — defaults
   are behavior-preserving today).
4. **Gold unification** — fold `generate_loot_gold` into the profile so gold is
   one more loot-table column rather than a separate faction path.
