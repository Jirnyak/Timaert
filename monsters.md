# Bodies & Loot — ОДНА таблица тел и таблица лута

**ONE table of living things and ONE loot table — both single sources of
truth.** A peasant, a lord, a wolf and a troll are rows of the SAME registry
(`macro/npc.h kNpcTypeDefs`, thirty rows: eleven roles and nineteen creatures);
every drop resolves through the one loot registry. Adding content is one data
row, never an `if`-chain.

> **The monster catalog is gone (2026-08-20, owner's ruling).** There used to be
> two tables of bodies addressed by two id spaces spliced into one number — a
> humanoid was an `NPCType` ordinal, a creature was `0x100 | catalog index` —
> so every consumer that met a body had to ask which of the two it was holding.
> The owner's words: *«у нас всё равно все сохраняемые изменяемые NPC только в
> макромире, а в микромире просто генерик NPC/мобы… лорд может быть не только
> человеком, но и драконом, демоном и гоблином, так что в этом нет смысла»*.
> Now a kind is simply an ordinal of the one table, `is_creature_row` is the
> last vestige and is documented as temporary, and three defects died with the
> split: the whole bestiary was dropping EMPTY loot, the auto-resolver and the
> fought battle were costing creatures by two different laws, and a squad led by
> a beast vanished on projection, roster and all. See CANON.md S16.

- **Code:** [macro/npc.h](src/macro/npc.h) (`kNpcTypeDefs[30]` — THE table of
  bodies, roles and creatures alike),
  [macro/behaviour.h](src/macro/behaviour.h) (the one `AIBehaviour` column),
  [macro/fauna.h](src/macro/fauna.h),
  [macro/fauna.cpp](src/macro/fauna.cpp) (biome/feature/landmark spawn tables,
  creature accessors, per-cell capacity; MACRO data since 2026-08-07 — the file
  moved from sub/ when the honest headcount made the macro layer its second
  reader);
  [macro/items.h](src/macro/items.h),
  [macro/items.cpp](src/macro/items.cpp) (`roll_loot_profile`, loot registry);
  [sub/engine.cpp](src/sub/engine.cpp) (`resolve_subworld_deaths` death/loot/XP
  dispatch, `spawn_npc_body` — one path for any row);
  [sub/spawn.cpp](src/sub/spawn.cpp) (`emplace_body` — the single body birth;
  ambient fauna spawn)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System, §L2

## The table of bodies

`NpcTypeDef` ([macro/npc.h](src/macro/npc.h)) is the row schema and
`kNpcTypeDefs[30]` holds the rows — every living thing in the game, roles and
creatures in ONE contiguous id space. `FaunaEntry`
([macro/fauna.h](src/macro/fauna.h)) is a `using` alias of `NpcTypeDef`, kept
only so the spawn tables read as what they are: lists of rows a place may roll.
Each row is the *complete* definition of a kind — no stats scattered across
spawn sites.

| Field | Meaning |
|-------|---------|
| `type` | The row's own ordinal — MUST equal its index (compiler guard below the table) |
| `id` | Stable machine id (`"wolf"`, `"peasant"`) — **the source of truth**, distinct from `label` |
| `label` | Display name (`"Wolf"`) |
| `sprite` | `SpriteId` — a row of THE sprite table ([sprites.md](sprites.md)); kinds share rows on purpose |
| `baseHp` | The row's base hit points |
| `baseLevel` | Level floor; spawn adds `floor(rng()*2)` |
| `ai` | `AIBehaviour` ([macro/behaviour.h](src/macro/behaviour.h)) — ONE vocabulary for every row (Gatherer, Trader, Aggressive, Patrol, Flee, …); folds to a subworld stance through `subworld_ai_for` |
| `combat` | `CombatTemplate` (the universal combat spine, one for all rows) |
| `upkeepGoldPerDay` / `hireable` | The soldier half of the row |
| `xpReward` | Per-kind XP base; `0` ⇒ generic level-scaled |
| `weight` | Spawn weight when the world rolls blind (0 = never rolled blind; a place must name the row) |
| `factionId` | Faction registry id string (`"wildlife"` / `"demons"` / …, macro/faction.h); `nullptr` = the LAND decides |
| `lootId` | Loot-profile override; `nullptr` ⇒ faction default |
| `radius` | Body radius in metres; 0 = derive from the sheet like a humanoid |
| `names` / `talkLines` | Fixed-arity pools |
| `light*` | Optional carried point light (torch/glow) — pure data, opt-in |

**Rows (30 today, append-only).** Roles (11): `peasant woodcutter merchant
caravan bandit guard witch sorceress miner quarryman claydigger`. Wildlife (12):
`rabbit deer fox wolf bear boar snake hawk frog goat eagle crocodile`.
Monsters (7): `goblin skeleton troll swamp_thing ice_wraith sand_scorpion
stone_golem`.

> **⚠ The row order is append-only. Never reorder or delete rows.** A kind is
> saved as its ordinal (soldier records, macro spawns, save v42 all carry raw
> kind numbers), so reordering re-keys every saved body of that kind. Append
> new rows at the end — the creature rows were themselves appended after the
> roles for exactly this reason.

### Accessors (mirror `item_catalog()`)

```cpp
std::span<const FaunaEntry* const> creature_catalog();       // the creature rows, flat
const FaunaEntry* creature_def(std::string_view id);         // "wolf" -> row (nullptr if unknown)
const FaunaEntry* creature_def_from_kind(std::uint16_t kind);// ordinal -> row (nullptr if not a creature)
int               creature_index(const FaunaEntry* entry);   // row -> its ordinal (-1 if absent)
```

The kind IS the row: `creature_def_from_kind` indexes `kNpcTypeDefs` directly —
there is no bit to mask and no second array (the old `& 0xFF` mask was the
dead encoding's last hiding place).

### One id space (the `0x100` discriminator is DEAD)

There is no field that "tells a monster from an NPC" any more, because the
engine no longer asks. `NPCKind.type` is a plain ordinal of the one table for
everyone. What remains at the boundary:

- **`is_creature_row(t)`** (`npc.h`) — `t >= NPCType::Rabbit`, an ordinal
  boundary predicate documented in the code as *deliberately ugly and
  temporary*: it exists only while procedural names/portraits and raw combat
  lines still differ per half, and it dies when they merge.
- **Everyone has a character sheet.** The single body birth `emplace_body`
  ([sub/spawn.cpp](src/sub/spawn.cpp)) runs `make_character_sheet` for every
  row — peasant, lord, wolf — and the leader's aura lands IN the sheet before
  any number is projected from it (CANON S14). "Monsters are sheet-less" is
  history.

## The loot table

`roll_loot_profile(lootId, level, rng)` ([macro/items.h](src/macro/items.h)) is
the **single** loot entry point. It looks up a named profile in the
`kLootProfiles[]` registry ([macro/items.cpp](src/macro/items.cpp)) and rolls it
through the shared `roll_loot()` core. Unknown / empty `lootId` ⇒ no items.

Registered profile ids:

- **11 NPC roles** (one per role row): `peasant woodcutter miner quarryman
  claydigger merchant caravan bandit guard witch sorceress`.
  `npc_loot_id(npcType)` maps an `NPCType` integer to its id (`""` if out of
  range).
- **3 faction defaults:** `wildlife`, `demons`, `bandits`. Any creature without
  a `lootId` override falls back to its faction's default profile.
- **World props** (things, not inhabitants): `tree`. A prop's *kind* names its
  profile — `structure_loot_id(Structure::Kind)`
  ([sub/map_data.h](src/sub/map_data.h)) — so breaking a thing and killing a
  body pay out through the SAME resolver. Rocks/bushes/cairns become rows here
  the day they exist; no new path is needed for them.

### Prop payout

`SubworldEngine::grant_prop_loot(prop)` ([sub/engine.cpp](src/sub/engine.cpp))
is the props' counterpart of the death dispatch. Two rules it adds on top of
the shared roll:

- **Deterministic per PLACE, not per swing** — the RNG is seeded from the world
  seed and the prop's *absolute tile coords*, so the same tree always pays the
  same wood and a felling cannot be re-rolled by reloading.
- **The yield scales by the prop's own metric height** against a reference
  trunk (`kPropYieldRefHeightM = 14 m`): a 20 m mast is worth more than a 6 m
  tundra scrub. The size the renderer draws is the size the axe is paid for —
  which is only possible because `Structure::height` is now real metres (see
  [microworld.md](microworld.md#tree-size--the-place-rolls-it-the-species-scales-it)).

Felling therefore closes the macro → micro → macro loop in one act: the tree
leaves the cell's structure list, the owning macro cell's `TreeLayer` count
drops by one through the registry's Trees row (save-stable — the living
grid rides the save whole — and thins the map sprite),
and the trunk becomes cargo in the player's pack.

> **Status: new and unsettled.** The prop payout shipped as the first step of
> the environment-props track and has not yet met rocks, bushes or anything
> else it claims to generalise to — so its shape is a hypothesis, not a proven
> law. Open questions, honestly: should a broken prop drop a lootable **pile**
> (reusing the corpse path) instead of paying straight into the pack, so the
> world keeps the object until you take it? Should the yield scale live in the
> loot **row** ("per 10 m of trunk") rather than as a constant in the payout?
> Should the profile key be the prop's **species** (oak vs pine) rather than
> its kind? None of these is decided, and the current answer may well not be
> the most elegant one. Expect this section to change when `PropRow` lands.

> The `bandits` profile (reusing `kBanditLoot`) closes a latent zero-loot gap:
> before unification, a Bandits-faction creature matched no faction loot string
> and dropped **zero items**. Now every faction has a real profile.

### Death dispatch (one keyed path)

`resolve_subworld_deaths` ([sub/engine.cpp](src/sub/engine.cpp)) routes *all*
drops through the one resolver, and the chain no longer branches on WHAT the
body is — the row answers first with its own column, then the per-role list,
then the faction:

```cpp
const NpcTypeDef* row = row_for(kind);
const char* lootId = row && row->lootId && row->lootId[0]
    ? row->lootId                                  // the row's own column
    : npc_loot_id(int(kind->type));                // the per-role list
if (!lootId || !lootId[0]) lootId = faction_id_for_kind(kind);  // faction default
auto stacks = roll_loot_profile(lootId, lvl, &loot_rng_f01);
```

One chain for a bandit and for a wolf. A body that died with a real inventory
drops that inventory instead — the roll fills only empty pockets.

Gold is still rolled by `generate_loot_gold(level, faction)` with
`gold_faction_mult` (wildlife 0.1 / demons 0.6 / bandits 0.8 / else 1.0) — a
noted later refinement, kept separate for now to stay behavior-preserving.
**Known defect** (canon-audit B2, [economy.md](economy.md)): this mints coin
into every corpse ON TOP of the purse the same body was already minted at
spawn — a double mint against the conservation law.

## Spawn paths (three, one table)

1. **Ambient fauna** — [sub/spawn.cpp](src/sub/spawn.cpp) rolls the cell's
   `FaunaTable` (`roll_fauna`; landmark > forest class > biome, each of the
   nine window cells from its OWN macro context) and emplaces each pick. The
   roll PROPOSES, the macro stock DISPOSES — see The honest headcount below.
2. **Directed spawn** — `spawn_npc_body`
   ([sub/engine.cpp](src/sub/engine.cpp)): the token names a row of THE one
   body table and that is the end of the question — `spawn wolf` and
   `spawn bandit` take the same path through the one birth `emplace_body`. The
   separate creature branch (own components, own wander pace, no sheet) died
   with the second table.
3. **Console `spawn <id>`** — [app/main.cpp](src/app/main.cpp) routes straight
   into `spawn_npc_body`, so it accepts any monster id or NPC role. Console
   creatures carry NO fauna receipt: a body from thin air owes the map nothing.

## The honest headcount (Session 16, 2026-08-07)

A wild creature is one unit of its cell's **`fauna_count`** — the fourth row
of the one macro-stock table ([macro/macro_stock.h](src/macro/macro_stock.h)).
The baseline is DERIVED, never stored: `fauna_cell_capacity_at` = the winning
spawn table's `maxCount` for the cell's own biome / forest class / landmark.
What persists is the sparse scar map `GameState::faunaOverrides` (saved since
v33):

- the ambient roll clamps to the cell's live count, and every embodied
  creature is stamped with the cell's receipt at birth (`BodyLoan`, the
  spawn_derived_body doctrine);
- the ONE death reaper settles the receipt — a kill thins the cell for good,
  and returning to it does NOT resurrect the culled (the old
  repopulate-on-recenter infinite XP/loot farm is dead; landmark tables too:
  a cleared ruin STANDS cleared, owner's ruling — no special cases);
- unloading the window settles nothing — eviction is not death;
- the wilds heal on the calendar: +1 head per scarred cell every
  `fauna_regrow_period_days()` (32 game days, a season per head), applied by
  the daily world tick through the same row, whose write self-cleans a healed
  cell out of the map. Context (season, zone danger, biome) enters that one
  door as data when it arrives; seasonal fauna COMPOSITION is a separate
  future increment.

Pinned by `fauna_stock_test` (stock law + regrow cadence, negative controls
run) and the `fauna_kill_writeback` smoke (kill underground → the map is one
head thinner next frame; reddens if the row write is muted).

## Per-kind XP

ONE law for every row: the death path awards `xpReward + (level-1)*5` when the
row sets a non-zero `xpReward`; `xpReward = 0` keeps the generic
`exp_from_fight(level)` (=10·level) fallback. (There used to be two of these —
the humanoid one via `npc_xp_reward`, the creature one written out again; the
macro squad ledger still calls `npc_xp_reward`, which computes the same
formula off the same column.) XP goes to the killing blow's owner
([microcombat.md](microcombat.md)).

## Data-driven extension

- **Add a creature** → append one row to `kNpcTypeDefs` in `npc.h` (one new
  `NPCType` enumerator at the end + one table entry with a stable `id`) and
  reference it in one or more biome/feature spawn tables (`fauna.cpp`). Spawn,
  loot (via faction default or a `lootId`), XP, rendering (via the sprite row),
  and the console `spawn` all pick it up with **no engine change**.
- **Add a loot profile** → one `kLootProfiles[]` row keyed by a new `lootId`;
  point a creature's `lootId` or an NPC role at it.
- **Add an item** → one `item_catalog()` row (see [rpg.md](rpg.md)); loot tables
  reference it by id.

## Connections

Combat resolution and hostility → [microcombat.md](microcombat.md); items,
inventory, and the item catalog → [rpg.md](rpg.md); NPC roles and the
NPC-as-soldier model → [macrosim.md](macrosim.md); biome spawn density →
[biomes.md](biomes.md). There is NO danger scaling of spawned bodies — the
zone markup was demolished 2026-08-20 (CANON S12, [zones.md](zones.md)): a
creature is exactly its row, and until context weights land the zone has no
say in a spawn at all.

## Deferred (future tracks, built on this foundation)

1. **Route-1 roaming macro monster parties** drawing from this same table
   (macro-scale hordes that descend into subworld combat).
2. **The universal character-sheet system — LANDED for everyone.** Every body
   — role or creature — gets its sheet through the one birth `emplace_body`
   (CANON S14); "monsters stay sheet-less" is dead. What remains deferred is
   the residue behind `is_creature_row`: procedural names/portraits and the
   raw combat line still differ per half, and the predicate dies when they
   merge.
3. **Populate `xpReward` / `lootId` per creature** (data/balance pass — defaults
   are behavior-preserving today).
4. **Gold unification** — fold `generate_loot_gold` into the profile so gold is
   one more loot-table column rather than a separate faction path.
