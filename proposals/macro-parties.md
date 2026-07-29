# Architecture Proposal — Macro Parties (отряды)

**Status:** proposal / design only. No engine code in this document. Bring to the
owner for approval before building (MASTER_PROMPT §9.4).
**Author track:** L1 macro-world simulation architect.
**Date:** 2026-07-27.
**Prereqs shipped:** universal `CharacterSheet` + `project_combat` (Inc 1–3),
player-as-ECS-entity + `PlayerTag` on both sides of the seam (Inc 4a–4d,
macro-4a). Possession (Inc 5) is designed but not built.

---

## 0. TL;DR — the shape of the decision

A **party (отряд) is a leader NPC** — the Mount & Blade model. Concretely:

- The **leader** is an ordinary macro **ECS entity** (the substrate that already
  exists — `Position + NPCKind + MacroNpcRuntime + Health + CharacterSheet`),
  additionally carrying a new `ecs::PartyLeaderTag` and an `ecs::Party` handle
  component. The party *is* that entity; kill/possess the leader and you have
  taken/broken the party.
- The **roster** is NOT a set of entities. It is a **cache-friendly SoA arena of
  12-byte POD member rows** hanging off the leader. A member references a
  monster (`0x100 | catalogIndex`) or a humanoid (`NPCType < 8`) through the
  **same `NPCKind.type` uint16 encoding the ECS already uses** — one identity
  scheme, no discriminator invented. Humanoid member sheets are regenerated on
  demand from seed (`make_character_sheet`), never stored; monsters stay
  sheet-less (their combat is the `FaunaEntry` row). Rosters scale to millions of
  rows for a few MB.
- This **subsumes** the current army model: `SoldierSquad`/`SoldierRecord`
  *become* the roster type; the player's army, settlement garrisons and the
  deserter pool are all just rosters. There is no parallel unit model.
- **Residency, not culling.** The mass of members is never frozen or faked — only
  its *execution unit* changes: cold SoA rows / GPU-resident today, promoted
  ("embodied") to full ECS entities the instant the player can act on them (in a
  subworld), de-embodied back afterward. This is the same identity throughout
  (ARCHITECTURE.md §GPU-Driven Simulation, lines 103–176) and it is exactly why
  the engine is data-oriented — *ради этого всё затевалось.*
- **Possession** (Inc 5) needs no new "take party" verb: the single `PlayerTag`
  flag moving onto a leader entity transfers the whole roster automatically,
  because the roster hangs off that entity.

The riskiest open question is **§8-A: how faithful must the CPU macro
auto-resolve of a party-vs-party battle be** to the embodied subworld fight
before the GPU crowd sim (§9.5) exists — i.e. is a capped/analytic interim battle
acceptable, or must every battle the player could reach be fully embodiable now?

---

## 1. Research grounding (verified against code — file:line)

Everything below is checked against the tree at HEAD, not assumed.

### 1.1 The one identity encoding already exists

`ecs::NPCKind` (`src/ecs/components.h:56`):

```cpp
struct NPCKind { std::uint16_t type; std::uint16_t factionIdx; };
```

`type < NPCType::Count (=8)` ⇒ humanoid; `type = 0x100 | catalogIndex` ⇒ monster.
The `≥0x100` split is load-bearing across spawn, loot and XP (MASTER_PROMPT §7;
`creature_def_from_kind`, `src/sub/fauna.cpp:191`). A member row will reuse this
verbatim — the roster and an embodied entity share one identity key.

### 1.2 The army model today — the thing parties must subsume

`src/macro/army.h:30-38`:

```cpp
struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle
    std::uint8_t  kind     = 0; // NPCType value; validated by npc.h helpers
    std::int16_t  level    = 1;
};
struct SoldierSquad { std::vector<SoldierRecord> members; };
```

Key limitation: **`kind` is `std::uint8_t`** — it structurally *cannot* hold a
monster id (`0x100 | idx` needs 16 bits). Helpers assume humanoid: `soldier_npc_type`
(`src/macro/npc.h:169`) maps `kind → NPCType`, `calculate_squad_upkeep`
(`src/macro/npc.h:187`) sums per-kind gold, `hire_npc` (`src/macro/npc.h:243`)
moves a record garrison→player. `SoldierSquad` is the player army
(`PlayerState.army`, `src/macro/state.h:100`), every settlement garrison
(`Settlement.garrison`, `state.h:32`) and the `deserterPool` (`state.h:128`).
Consumers: `src/ui/overlays.cpp`, `src/sub/spawn.cpp`, `src/macro/state.cpp`,
`src/sub/engine.cpp`, `src/macro/save.cpp`, `src/macro/world_tick.cpp`. There is
**no** `Party`/`отряд`/roster/warband type anywhere in `src/` today (grep clean) —
this is greenfield with one type to evolve.

### 1.3 Macro NPCs are already ECS entities, ticked by a budgeted cold sweep

`make_npc` (`src/macro/npc_spawn.cpp:41`) builds each macro NPC as an entity:
`Position + VisualPos + NPCKind + MacroNpcRuntime + Health + NpcLevel +
NpcTraits + NpcInventory + NpcCharacter`. `spawn_macro_npcs`
(`npc_spawn.cpp:135`) is called once at `boot_world` (`src/app/main.cpp:1250`).

`tick_macro_npc_ai_budgeted` (`src/macro/npc_ai.cpp:555`) already amortises the
whole macro population across frames with a **cursor + queued-sweep** scheme
(`sweepCursor`, `pendingSweeps`, `max_npc_ticks`) — no per-frame full sweep, and
**no proximity-to-player gate**: the view is global (`view<Position, NPCKind,
MacroNpcRuntime, Health>(exclude<Dead>)`), and `ctx.playerX/Y` feeds only the
`Aggressive` chase behaviour, never a cull. This is the seed of the cold party
sim. Note two facts to reconcile:
- Monsters are skipped by macro AI today: `if (kind.type >= NPCType::Count) continue;`
  (`npc_ai.cpp:503`, and the budgeted variant at `:603`). Members-as-monsters
  are fine because members do not tick individually — the *leader* ticks.
- The macro AI tick is invoked in the **non-subworld branch** of the main loop
  (`src/app/main.cpp:2148`/`2166`). Pillar §3.1 requires the macro sim to keep
  running **while the player is in a subworld** (subworld time slower). The party
  macro tick must therefore be hoisted to run unconditionally on a background
  cadence — flagged in §8.

### 1.4 Macro NPCs are NOT saved — they regenerate from seed

`save.cpp` serialises `GameState` only (settlements, villages, player + army,
garrisons, deserterPool, trade routes). No ECS macro entity is written; on load,
`boot_world` regenerates them from `worldSeed` (`main.cpp:1291-1326`). This
"persist identity/divergence, regenerate derivations" split is the model parties
will follow (§6).

### 1.5 Embodiment + reconciliation already exist for the player squad

`SubworldEngine::enter` (`src/sub/engine.cpp:381`): after `respawn_npcs_for_center()`
it calls `spawn_player_squad(ecs, gs.player.army, …)` (`engine.cpp:426`) then
`spawn_player_entity()`. `spawn_player_squad` (`src/sub/spawn.cpp:304`) embodies
each `SoldierRecord` into a subworld entity: `Position + NPCKind + Health +
Combat` (via `project_combat`, `character_sheet.h:177`) `+ PlayerSoldierTag +
SubworldTag + CharacterSheet + SoldierLink`. The ambient monster path
`respawn_subworld_npcs` (`spawn.cpp:231-292`) embodies fauna with the
`0x100 | creature_index` id (`spawn.cpp:258-260`). `spawn_hostile_npc`
(`engine.h:60`) embodies one hostile (monster or humanoid) per encounter.

Crucially, **de-embody reconciliation is already wired**: when a `PlayerSoldierTag`
entity dies in the subworld, the engine writes the casualty back to the macro
roster via the `SoldierLink` — `remove_one_soldier_by_entity_id(gs_->player.army,
link->entityId)` (`engine.cpp:1290-1292`). The party embodiment reconciliation
(§4) is a direct generalisation of this exact mechanism.

### 1.6 The player is a movable flag on both sides (possession substrate)

`ensure_macro_player_entity` (`src/macro/player_entity.cpp`) keeps a minimal
`Position + PlayerTag` macro flag; `SubworldEngine::spawn_player_entity`
(`engine.cpp:484`) makes it a full combat actor in a subworld. Invariant:
**exactly one `PlayerTag` at all times**. Inc 5 will add a runtime-only
`MacroOrigin { entt::entity macro; }` backlink so the flag can "exit as the lord"
(memory `npc-sheet-possession-plan`, item 5). Parties plug straight into this.

### 1.7 Combat / RNG / layering invariants

- `project_combat(sheet, base)` and `make_character_sheet(role, level, seed)` are
  header-only, deterministic in seed (`src/macro/character_sheet.h:125,177`).
  Monster combat is the raw `FaunaEntry` row (`src/sub/fauna.h:49`).
- Seeded xorshift32 `sm::Rng` + `hash3(x,y,seed)` (`src/core/rng.h`) — the only
  sanctioned RNG; every derived value below uses it, no `Math.random`.
- Layering (AGENTS.md:224): **L1 `macro/` may include `ecs/`, `core/`.** So the
  party model + macro sim live in `src/macro/party.{h,cpp}`; ECS components live
  in `src/ecs/components.h`; embodiment (roster→subworld) lives in L2
  `src/sub/spawn.cpp` (L2 may include macro/). POD components, `-fno-exceptions
  -fno-rtti`, forward-declare in headers.
- GPU residency contract: ARCHITECTURE.md:103-176 — two residency tiers, one
  identity; SoA + bit-pack (`level(8)|kindOrWeaponId(8)|hp(16)`, line 146);
  lookup buffers indexed by id; embody the few, never read back the mass.

---

## 2. Party representation (the concrete structs)

### 2.1 The member row — one 12-byte POD, one identity scheme

New in `src/ecs/components.h` (POD, no pointers, no heap):

```cpp
// A single roster member. NOT an entity — a cold SoA row. Identity is the SAME
// encoding as ecs::NPCKind.type: kind < 0x100 => humanoid NPCType (gets a
// CharacterSheet regenerated from seed on embodiment); kind >= 0x100 => monster
// (0x100 | catalogIndex, sheet-less, combat = FaunaEntry row). Membership is
// orthogonal to the sheet — a monster is a full member and stays sheet-less.
struct PartyMember {                 // 12 bytes
    std::uint32_t id;                // stable member id: XP/embodiment link + save
    std::uint16_t kind;              // NPCKind.type convention (THE discriminator)
    std::int16_t  level;             // combat scale (project_combat / FaunaEntry)
    std::uint16_t factionIdx;        // 0 = inherit leader faction; else override
    std::uint16_t flags;             // data bitfield: Wounded|Veteran|Embodied|… (reserved)
};
```

Design notes:
- **No stored sheet, no stored seed.** A humanoid member's sheet is
  `make_character_sheet(NPCType(kind), level, hash3(party.seed, id, 0))` computed
  *only* when the member embodies (§4). `id` is stable, `party.seed` is stable ⇒
  the sheet is stable across save/load and across slot reordering after
  casualties. Monsters skip this entirely (`creature_def_from_kind(kind)`).
- **The `kind` field is the whole trick** for "monster and humanoid mixed
  freely": one uint16, the value ≥0x100 is the only branch, and it is the *same*
  branch the spawn/loot/XP code already takes.
- `flags` keeps future per-member state as **data bits, not new fields/types** —
  no-hardcoding compliance (add a bit, not a struct).

### 2.2 The roster arena — cache-friendly to millions of rows

Two storage tiers, one API. The API (in `src/macro/party.h`) is what all callers
see; storage can evolve underneath without touching them.

**Bootstrap storage (Stage S2–S6): per-leader vector.** Exactly today's
`SoldierSquad` shape, so migration is mechanical and every step compiles:

```cpp
struct PartyRoster { std::vector<PartyMember> members; };  // evolves SoldierSquad
```

**Target storage (Stage S7+, when profiling/scale demands): one global SoA arena.**

```cpp
// One arena for ALL parties. Members are contiguous per party; the leader's
// Party component holds a span. Order within a span is irrelevant (stable ids),
// so casualties are O(1) swap-remove; recruitment appends; a span that outgrows
// its slot is moved and the old slot returned to a size-bucketed free list.
struct PartyArena {
    std::vector<PartyMember> pool;   // dense; the single source of truth
    // free-list of returned [first,count) runs, bucketed by capacity … (impl)
};
// Leader-side handle (see 2.3): { first, count, cap } index the arena.
```

The API is identical over both: `roster_add`, `roster_remove_by_id`,
`roster_count`, `roster_count_of_kind`, `roster_merge`, `roster_split`,
`roster_aggregate`. `add_squad`/`drain_squad`/`reserve_soldiers_for_append`
(`army.h:94-130`) fold into these.

### 2.3 The leader — the party *is* this entity

The leader is an existing macro NPC entity plus two new components:

```cpp
// Marks an entity as a party leader. view<PartyLeaderTag> = "all parties".
struct PartyLeaderTag {};

// The party handle hanging off the leader entity. POD, ~32 bytes.
struct Party {
    std::uint32_t seed;        // per-party RNG root (member sheets, composition)
    std::uint32_t rosterFirst; // arena span start (bootstrap: index into a side table)
    std::uint32_t rosterCount; // live member count
    std::uint32_t rosterCap;   // arena span capacity
    std::int32_t  treasury;    // gold for upkeep (M&B party purse)
    std::int16_t  morale;      // 0..1000; drives desertion / rout
    std::uint8_t  aiPolicy;    // data-selected macro behaviour (see §3)
    std::uint8_t  allegiance;  // faction/kingdom band; 0 = independent
};

// Cheap per-party summary recomputed on roster mutation (NOT every tick), so the
// hot macro sweep reads O(1) aggregates instead of walking members.
struct PartyAggregate {
    std::uint32_t headcount;
    std::int32_t  upkeepPerDay;   // Σ humanoid upkeep (monster provisioning col)
    float         powerScore;     // Σ derived combat power (auto-resolve input)
    float         speed;          // min member speed → party map speed
    std::int16_t  avgLevel;
    std::uint16_t composition;    // packed monster/humanoid ratio bucket (UI/AI)
};
```

- The player's party leader is the `PlayerTag` entity. **Player = leader NPC of
  their party** (§2 pillar) falls out for free.
- Ambient lone NPCs are **not** forced to be parties. A party is any entity with
  `PartyLeaderTag`; a wandering peasant is just an NPC. This keeps entity counts
  sane (thousands of *leaders*, not millions) while staying single-model: a lone
  NPC and a member share one `kind` identity, and recruiting a lone NPC = moving
  it from "un-partied entity" residency into "member row" residency (§4).

### 2.4 Memory-budget math (why members must be rows, not entities)

Member row = **12 bytes**. Leader overhead = `Party (32) + PartyAggregate (~28)
+ tags` ≈ 64 bytes on top of an entity that already exists.

| Scale (parties × avg members) | Members | Member rows | Leaders | Total party memory |
|---|---|---|---|---|
| 2,000 × 500 | 1,000,000 | 12 MB | ~128 KB | ~12 MB |
| 5,000 × 1,000 | 5,000,000 | 60 MB | ~320 KB | ~60 MB |
| 10,000 × 2,000 (stretch) | 20,000,000 | 240 MB | ~640 KB | ~240 MB |

**Counterfactual — members as full macro entities.** Each `make_npc` entity is
`Position(8)+VisualPos(12)+NPCKind(4)+MacroNpcRuntime(~40)+Health(8)+NpcLevel(2)+
NpcTraits(4)+NpcInventory(vector+heap)+NpcCharacter(12)` ≈ 150–400 B *plus a heap
inventory allocation per member* plus EnTT pool overhead. 1,000,000 members ⇒
150–400 MB **and one million heap allocations** — untenable, and it defeats the
whole point. **The budget is the argument:** the residency split (cold 12-byte
rows for the mass, ECS entities only for the embodied few) is forced by the math,
not a preference.

**GPU alignment.** The ARCHITECTURE SSBO row `level(8)|kind(8)|hp(16)` (4 B) +
two `float` position lanes (8 B) = 12 B — the *same width* as `PartyMember`. The
CPU arena is deliberately the CPU shadow of the GPU SSBO at one schema; §9.5 can
lift the arena to VRAM (1M members = 12 MB VRAM) with no schema change, and
`kind` indexes the lookup-buffer stat tables exactly as it indexes the CPU
registries.

---

## 3. Relationship to `SoldierSquad`/army — SUBSUME, don't shadow

Single source of truth: the roster **is** the army. `SoldierSquad`/`SoldierRecord`
are *renamed/evolved into* `PartyRoster`/`PartyMember`, not duplicated. Delta:

| Concept | Today | After | Why |
|---|---|---|---|
| Member record | `SoldierRecord{u32 entityId; u8 kind; i16 level}` (`army.h:30`) | `PartyMember{u32 id; u16 kind; i16 level; u16 factionIdx; u16 flags}` | `kind` u8→**u16** to hold `0x100\|idx`; +faction/flags as data |
| Container | `SoldierSquad{vector<SoldierRecord>}` (`army.h:36`) | `PartyRoster` (bootstrap) → `PartyArena` span (target) | one army model, cache-friendly at scale |
| Player army | `PlayerState.army` (`state.h:100`) | player party roster; leader = `PlayerTag` entity | player = leader NPC |
| Garrison | `Settlement.garrison` (`state.h:32`) | a roster parked on the settlement; spawns a defending party when engaged | M&B garrisons |
| Deserters | `deserterPool` (`state.h:128`) | a leaderless roster (recruit pool) | unchanged role |
| Kind→role | `soldier_npc_type` assumes <8 (`npc.h:169`) | `member_is_monster(kind)` / `member_npc_type(kind)` split | mixed roster |
| Validity gate | `valid_npc_kind(kind<8)` (`npc.h:165`) | `valid_member_kind` = humanoid `<8` **or** monster `0x100\|idx` in catalog range | save + roster invariants |
| Upkeep | `calculate_squad_upkeep` humanoid-only (`npc.h:187`) | same, + monster provisioning column (default 0) folded in | data-driven |
| Hire | `hire_npc` garrison→player (`npc.h:243`) | `roster_recruit` (same move, any kind) | generalised |
| Embody | `spawn_player_squad` humanoid-only (`spawn.cpp:304`) | `embody_member` dispatch on ≥0x100 (humanoid ∪ monster) | mixed roster in subworld |
| Reconcile | `remove_one_soldier_by_entity_id` on death (`engine.cpp:1292`) | keyed by `PartyMember.id` via `PartyMemberLink` | same mechanism |
| Backlink comp | `SoldierLink{u32; u8 kind; i16}` (`components.h:91`) | `PartyMemberLink{u32 leaderId; u32 memberId; u16 kind}` (kind u8→u16) | monster-capable |

Everything else (`add_squad`, `drain_squad`, `count_soldiers_of_kind`,
`reserve_soldiers_for_append`) survives as roster ops. The rename is
compiler-checked: a missed call site or a narrowing `kind` assignment fails to
compile.

---

## 4. Macro simulation — a cold background sim, everything everywhere always

Parties move / fight / merge / split / recruit / take upkeep with **no
player-distance culling**, including while the player is in a subworld (§1.3 gap
must be closed). The design keeps the per-tick cost O(parties), not O(members),
by reading `PartyAggregate` on the hot path and walking members only on discrete
events.

### 4.1 Movement
The leader entity already moves via `MacroNpcRuntime` + a behaviour
(`npc_ai.cpp`). A party rides its leader — members have no individual macro
position (they *are* the leader's position until embodied). Party map speed =
`aggregate.speed` (min member speed, data-driven from `CombatTemplate.speed` /
`FaunaEntry.speed`). Behaviour is chosen by `Party.aiPolicy` (a **data column**,
dispatched exactly like `AIBehaviour` already is at `npc_ai.cpp:439`), not a
leader-type `if`-chain. New policies (e.g. `PatrolTerritory`, `RaidSettlements`,
`SeekBattle`, `FleeToGarrison`) are rows, not branches.

### 4.2 Tick budget & no-cull
Reuse `tick_macro_npc_ai_budgeted`'s cursor/queued-sweep (`npc_ai.cpp:555`) over
`view<PartyLeaderTag, Party, Position, MacroNpcRuntime>`. It already amortises
thousands of actors across frames with no proximity gate. **Required change:**
run the party sweep unconditionally (both the macro and the subworld frame
branches), with a **slower cadence while in a subworld** so "subworld time <
macro time" (pillar §3.1) holds — a single `partySimClock` scaled by a
subworld-time-dilation constant.

### 4.3 Engagement & battle
When two mutually-hostile leaders come within an engagement radius (faction test
via `factions[...].relation`, as combat already does — microcombat.md), resolve:

- **Player's party involved ⇒ EMBODY into the subworld** (§5 below) — the real,
  full-fidelity fight. This is the primary case and the reason subworlds exist.
- **Neither side is the player ⇒ CPU auto-resolve** on the macro side. This must
  be the **same rules, integrated** — not a random dice roll. Compute each side's
  power from the *same* `project_combat` / `FaunaEntry` numbers the embodied fight
  would use (that is what `PartyAggregate.powerScore` caches), attrit rosters by
  removing member rows, award XP to survivors (level-ups via the same curves),
  route the loser (morale collapse) or destroy it, transfer survivors/loot. "Not
  a cheat" (ARCHITECTURE.md:173): an off-map battle and an embodied one run the
  same combat math; only the *integration method* (analytic vs tick-by-tick)
  differs. **Fidelity of this interim analytic model is the top open question
  (§8-A).**

### 4.4 Merge / split / recruit / upkeep
- **Merge** (two friendly leaders meet): `roster_merge(dst, src)`; the absorbed
  leader either demotes to a member row (its entity destroyed) or the parties
  stay distinct allies — data policy.
- **Split**: `roster_split` moves members into a new party; a member is
  **promoted to a leader** (create an ECS entity, seed it from the party, move
  one member row out as its leader identity).
- **Recruit**: `roster_recruit` appends rows — from a settlement garrison roster
  (M&B hire, generalising `hire_npc`), from battle survivors, or by absorbing a
  lone ambient NPC (de-embody that entity into a member row).
- **Upkeep**: extend the daily tick (`tick_player_daily_`, `world_tick.cpp:189`
  already calls `calculate_squad_upkeep`) to **every** party: deduct
  `aggregate.upkeepPerDay` from `Party.treasury`; on shortfall, `morale` drops;
  below a threshold, members desert (rows move to `deserterPool` or vanish).
  Monster upkeep is a new data column on `FaunaEntry` (provisioning cost, default
  0 — behaviour-preserving), never a special case.

### 4.5 World-gen seeding
`spawn_macro_npcs` (`npc_spawn.cpp:135`) gains a party pass: designated leaders
(bandit captains, lords, monster warband leaders) receive `PartyLeaderTag +
Party` and a **seeded roster rolled from the ONE global tables** — humanoids from
`kNpcTypeDefs`, monsters from the `FaunaEntry` catalog / biome fauna tables
(`fauna.cpp`), mixed per an authored composition template keyed by leader
role/faction (data rows). Deterministic in `worldSeed` — so pre-battle rosters
regenerate identically and need no save until they diverge (§6).

---

## 5. Embodiment — roster ⇄ subworld, and the residency boundary

Embodiment is the promotion of members to full ECS entities the instant the
player can act on them; de-embodiment writes results back. Identity (member `id`,
`kind`, level) is preserved across the transition — same member, embodied or not
(ARCHITECTURE.md:120-124). It is **never** a freeze/fake/LOD-skip — only the
execution unit changes.

### 5.1 The player's OWN party embodies (already 90% built)
On `SubworldEngine::enter`, `spawn_player_squad(gs.player.army,…)`
(`engine.cpp:426`) already embodies the player roster and `spawn_player_entity()`
already embodies the leader (the `PlayerTag` actor). Generalisation: replace
`spawn_player_squad` with `embody_party(reg, playerParty, …)` that calls a single
`embody_member(reg, PartyMember, pos, seed)` which **dispatches on the ≥0x100
bit**:
- humanoid → `make_character_sheet` + `project_combat` (as `spawn.cpp:304` does)
  + `PlayerSoldierTag`;
- monster → raw `FaunaEntry` row (as `respawn_subworld_npcs`, `spawn.cpp:231`
  does) + `PlayerSoldierTag`.

Each embodied entity carries a `PartyMemberLink{leaderId, memberId, kind}`
(generalising `SoldierLink`, `components.h:91`) so results reconcile back.

### 5.2 The enemy party embodies symmetrically
Generalise `spawn_hostile_npc` (`engine.h:60`, one-at-a-time) into
`embody_party(reg, enemyParty, …)` spawning the enemy roster as hostile subworld
entities via the *same* `embody_member`. Faction/hostility flows through
`NPCKind.factionIdx` unchanged.

### 5.3 The residency boundary — embody the few, simulate the rest (never freeze)
A party can be thousands; a 3×3 subworld (3072²) cannot hold thousands of ECS
entities without the AI/render paths choking. So embody **only the engagement
set** — members inside the player's 3×3 window / engagement radius / under the
reticle (microcombat.md; ARCHITECTURE.md:128) — and keep the remainder simulated
but not embodied:

- **Post-GPU-sim (§9.5 target):** the remainder is the **GPU-resident compute
  crowd** (billboards driven by compute over the SSBO); a member is promoted to
  ECS the moment it enters the engagement set and de-embodied when it leaves —
  same entity, no discontinuity, no stall (double-buffered/fenced staging, never
  a per-frame readback — ARCHITECTURE.md:132-139).
- **Pre-GPU-sim (honest interim):** the remainder keeps fighting via the §4.3
  analytic model running **in lockstep** at the party level — casualties keep
  accruing to the un-embodied rows every tick; they are *simulated, not frozen*.
  The engagement-set size is capped (a data constant) and this cap is the ceiling
  on pre-GPU battle scale — see §8-D.

Either way the **member arena is the CPU shadow of the mass**; embodiment copies
a row → entity, de-embodiment writes the entity's Health/level/XP/loot → row.

### 5.4 De-embody & reconcile on `leave()`
Generalise the existing casualty path (`engine.cpp:1290-1292`). On `leave()`
(`engine.cpp:1361`), for every embodied entity with a `PartyMemberLink`: write its
`Health`/level/XP/loot back to its roster row by `memberId`; drop dead rows;
apply level-ups (same curves); merge player-side loot to the player. Then the
existing `clear_subworld_entities` + `clear_player_entity` tear down the scene.
The macro leader flag remaps per Inc 5 (§6/§ possession). Rosters are the
authority; the subworld entities were a session projection (SoldierLink comment,
`components.h:90` — already the stated contract).

---

## 6. Possession tie-in (Inc 5)

- The single `PlayerTag` flag lives on a **leader** entity. **Taking a party =
  possessing its leader** — no separate verb, because the roster hangs off the
  leader (`Party` component). `possess_entity(target)` (Inc 5) = `remove<PlayerTag>(old);
  emplace<PlayerTag>(target)`; if `target` has `PartyLeaderTag`, you now command
  its roster automatically.
- **Body-native stats (owner D3):** the possessed leader fights on its OWN sheet
  (`make_character_sheet`/`project_combat`) — a lord is strong, a rat-warband
  captain is weak. `gs.player` hero is preserved as the revert target. No
  hero-loadout stamping.
- **Exactly-one-PlayerTag (unchanged):** still one flag. In a subworld,
  possessing a leader and then leaving uses the planned `MacroOrigin` backlink to
  remap the flag onto that leader's macro entity ("exit as the lord"); the vacated
  player-hero leader reverts to an AI-led party leader NPC — **its roster
  persists**, now a cold-sim party (or disbands per policy). MACRO stays
  authoritative at the seam (pillar §8).
- **Possessing a non-leader member** (owner said the flag can move to *any* NPC):
  proposed default — you control that one body, but the party keeps its AI leader
  (you did not take the party, because "the party IS its leader"). Optionally,
  possessing a member auto-promotes it (a split). **Owner call — §8-C.**

---

## 7. Save implications

Persist **identity + divergence**; regenerate **derivations** (mirrors §1.4).

**Never persisted (regenerated):** member `CharacterSheet`s (humanoid, from seed),
monster combat (`FaunaEntry`), initial world-gen roster composition (from
`worldSeed`), and *all* subworld embodiment (rebuilt on every `enter`).

**Must persist (diverges from seed):** roster contents after casualties/recruits,
`Party.treasury`/`morale`/position, and the leader identity.

Staged save impact:
- **v9 (Stage S3) — member row widen.** `PartyMember.kind` u8→u16 + `id` +
  faction/flags changes the `write_squad`/`read_squad` byte layout
  (`save.cpp:433-459`, currently `u32,u8,i16` per member) and the
  `valid_npc_kind` gate (`save.cpp:436,458`) must widen to `valid_member_kind`
  (accept `0x100|idx`). This is the **one unavoidable `kSaveVersion` bump**
  (`state.h:20`, 8→9). Update `save_roundtrip_test`. Player army + garrisons +
  deserterPool are already saved as squads — they upgrade in place.
- **v10 (Stage S7) — persistent roaming parties.** Add `std::vector<PartyState>`
  to `GameState` (leader kind/level/id/seed, position, faction, treasury, morale,
  roster). Until this lands, roaming parties may still regenerate from `worldSeed`
  at boot (like today's macro NPCs) and only diverge once battles mutate them —
  so v10 can be deferred behind a dirty-flag ("save only parties that diverged
  from their seed"), bounding save growth (§8-B). Respect the existing save caps
  (`kMaxSoldiers`, `kMaxSettlements`, etc., `save.cpp:23-32`); add a
  `kMaxParties`/`kMaxMembersPerParty` cap.

---

## 8. Incremental, additive-first migration (each step compiles + smoke/`*_test` green)

Small steps; new module + new components first; the `SoldierSquad` evolution is
mechanical and compiler-checked. Verify with BOTH the validated smoke and the
standalone `build/*_test` binaries each step (memory `unit-test-suite`).

- **S1 — Components (inert).** Add `PartyMember`, `PartyLeaderTag`, `Party`,
  `PartyAggregate`, `PartyMemberLink` to `src/ecs/components.h`. No wiring. Green.
- **S2 — Party module (L1).** New `src/macro/party.{h,cpp}` with the roster API
  (`roster_add/remove/count/merge/split/aggregate/upkeep`) over a bootstrap
  `PartyRoster` vector, delegating to `army.h` helpers. New `party_test`
  standalone unit test. No callers yet. Green.
- **S3 — Widen `kind` u8→u16 + save v9.** Evolve `SoldierRecord`→`PartyMember`,
  `SoldierSquad`→`PartyRoster`, `SoldierLink`→`PartyMemberLink`; update
  `write_squad`/`read_squad` + `valid_member_kind`; bump `kSaveVersion`.
  Compiler finds every call site. Update `save_roundtrip_test` (new v9 bytes).
- **S4 — Player army = a party; mixed embodiment.** `gs.player.army` becomes the
  player party roster; leader = `PlayerTag` entity. Add `embody_member`
  (≥0x100 dispatch) and route `spawn_player_squad`→`embody_party`. Smoke:
  a mixed player roster (add a captured `goblin`) embodies — humanoid via sheet,
  monster via `FaunaEntry`.
- **S5 — Roaming parties (movement only).** `spawn_macro_npcs` gives designated
  leaders `Party` + seeded mixed rosters; hoist the party sweep to run
  unconditionally with subworld-slower cadence (§4.2). No battles yet. Smoke:
  N parties roam while the player is in a subworld; aggregates correct.
- **S6 — Enemy embodiment on engagement.** Generalise `spawn_hostile_npc`→
  `embody_party`; wire de-embody reconciliation via `PartyMemberLink` on
  `leave()`. Smoke: engage a roaming party → its roster embodies → kills write
  back → survivors persist.
- **S7 — Macro auto-resolve + persistent parties (save v10).** Same-rules
  analytic battle for non-player parties; persist diverged parties (dirty-flag).
- **S8 — Possession of leaders (Inc 5).** Flag onto a leader = take the party;
  `MacroOrigin` remap on `leave()`; keep exactly-one-PlayerTag.
- **S9 — GPU residency (§9.5, later).** Lift the member arena to an SSBO (same
  12-B schema); embody-on-demand only the engagement set; compute crowd for the
  rest. Removes the pre-GPU engagement cap (§8-D).

---

## 8-bis. Risks / open questions for the owner

- **A. Auto-resolve fidelity (RISKIEST).** Pre-GPU, party-vs-party battles the
  player is not in must resolve on the CPU. How faithful must this analytic model
  be to an embodied fight (which the "not a cheat" pillar demands are identical in
  rules)? Acceptable to ship an integrated-power model now and tighten later, or
  must outcomes match a headless embodied sim within a tolerance?
- **B. Save growth vs seed-regeneration.** Persisting thousands of stateful
  rosters (v10) grows saves and load time. OK to persist only parties that
  diverged from their seed (dirty-flag), regenerating the rest? Or must all be
  authored/persistent?
- **C. Possessing a non-leader member** — control-only, auto-promote (split), or
  disallowed? (§6.)
- **D. Pre-GPU engagement-set cap.** Until §9.5, how many ECS entities may a
  subworld battle embody at once (the cap that bounds interim battle scale)? This
  sets whether truly-thousands battles *require* the GPU crowd first.
- **E. Leader death mid-battle** — promote a member to leader, rout the party
  (morale), or disband? (Data policy; M&B routs.)
- **F. Faction/allegiance model.** `NPCKind.factionIdx` is a thin u16 today
  (`npc_spawn.cpp:112`). Parties want kingdom membership + player-hire + war/peace
  — reconcile `Party.allegiance` with `politik`/`factions`. Scope now or later?
- **G. Monster provisioning.** Do monster members cost upkeep (new `FaunaEntry`
  column) or are they free? Default 0 is behaviour-preserving; owner may want a
  cost.
- **H. Ambient NPCs vs parties.** Confirm lone NPCs stay un-partied entities
  (recruitable into rows), i.e. "some NPCs are leaders; not all NPCs are parties"
  — vs. the stricter reading "every NPC belongs to a party of ≥1."

---

## 9. Why this satisfies the design law

- **Minimum systems / max functionality:** one new L1 module + a handful of POD
  components; reuses the existing entity substrate, budgeted AI sweep,
  `project_combat`/`FaunaEntry`, embodiment path, and `SoldierLink`
  reconciliation. Battle, recruit, garrison, player army and possession all
  collapse onto one roster type.
- **No hardcoding:** composition, upkeep, AI policy and behaviour are **data rows
  / bitflags**, dispatched like the existing `AIBehaviour` table — never
  leader-type `if`-chains.
- **Single source of truth:** the roster **is** the army; `SoldierSquad` is
  subsumed, not shadowed; the `≥0x100` `NPCKind` bit is the *only* monster/humanoid
  discriminator; the global `FaunaEntry` + `kNpcTypeDefs` tables remain the only
  content registries.
- **Four-layer arch:** party model + macro sim in L1 `macro/`; components in
  `ecs/`; embodiment in L2 `sub/` (L2→macro allowed). POD, `-fno-exceptions
  -fno-rtti`, seeded `core/rng.h`.
- **No proximity culling:** members are never frozen/faked/LOD-skipped — only
  their execution unit (cold row / GPU / embodied ECS) changes; the party sim
  runs everywhere always, including during subworld visits, at a slower subworld
  cadence.
- **Possession-ready:** the flag on a leader entity *is* command of the party;
  exactly-one-PlayerTag holds; MACRO stays authoritative across the seam.
