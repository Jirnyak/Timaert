# Census follow-ups — deferred items for owner review

Status: **proposals only — nothing here has been applied.** These are the
codebase-census findings that were deliberately *not* touched autonomously,
because each one either (a) perturbs the seed-12345 world another agent is
actively validating, (b) needs coordination with the external TS parity
authority + a re-baseline of seeded tests, or (c) is a design decision that is
the owner's to make. Each item below is self-contained: the exact location, the
concrete change, and *why it was left alone*.

The one census finding that was **safe to fix in place has already been fixed** —
see the Fisher-Yates out-of-bounds guard in
`src/content/quests/procedural.cpp` (`shuffled_order`) and its regression test in
`tests/quest_lifecycle_test.cpp`. This document is the *rest* of the list.

---

## 1. `next_f01()` returns `1.0f` — breaks the documented `[0, 1)` contract (root cause)

**Where:** `src/core/rng.h:23`

```cpp
float next_f01() { return float(next_u32()) / 4294967296.0f; }
```

**The defect.** `next_u32()` can return values in `[0xFFFFFF80, 0xFFFFFFFF]`
(~128 codes). `float` has only 24 bits of mantissa, so every one of those
rounds **up** to exactly `2^32` when converted, and `2^32 / 2^32 == 1.0f`.
So `next_f01()` occasionally returns exactly `1.0f`, violating its `[0, 1)`
contract. Frequency ≈ 128 / 2^32 ≈ 1 in 33.5 million draws.

**Impact — audited this session.** Every `int(next_f01() * N)` consumer can
then produce `N` instead of `N-1`:

| Consumer | Effect of a `1.0f` draw | Severity |
|---|---|---|
| `content/quests/procedural.cpp:26` `shuffled_order` Fisher-Yates | `j = i+1` → **array index one past the end (heap OOB)** | **memory-unsafe — ALREADY FIXED locally** |
| `procedural.cpp` quantity/level/count draws (`3 + int(f*8)`, `1 + int(f*3)`, …) | range top widens by one (e.g. `3..10` → `3..11`) | cosmetic spec-drift |
| `procedural.cpp` angle/distance (`f * kTau`, `f * 40`) | angle can equal `kTau` (≡ 0), dist hits the exclusive max | cosmetic |
| `sub/spawn.cpp:289-290` spawn `fx/fy = origin + f*kCellSize` | position can land exactly on the cell's far edge | **touches seed-12345 world** |
| `sub/spawn.cpp:303` `int(floor(f*2))` level bonus | bonus can be `2` instead of `0..1` | **touches seed-12345 world** |
| `sub/ai.cpp:54,58` wander angle / timer | angle can equal `2π`, timer hits max | cosmetic |

Only the Fisher-Yates case was memory-unsafe, and it is already guarded
defensively at the call site. **Everything else is behaviorally benign** (no
crash, no UB) — but the boundary draws are real and, more importantly, they are
**observable in the seeded outputs** that parity tests and the seed-12345 render
pin.

**Two candidate root fixes (do NOT apply yet):**

- *Option A — clamp (smallest diff, preserves most outputs):*
  ```cpp
  float next_f01() {
      const float v = float(next_u32()) / 4294967296.0f;
      return v < 1.0f ? v : 0.99999994f;  // 0x1.fffffep-1, largest float < 1.0
  }
  ```
  Only the ~128 top codes change; every other draw is bit-identical. But those
  128 codes *do* change, so it is still a parity-affecting edit.

- *Option B — construct in-range (mathematically clean, changes many outputs):*
  ```cpp
  float next_f01() { return float(next_u32() >> 8) * 0x1p-24f; }  // 24-bit mantissa, exact, [0,1)
  ```
  Uses exactly the 24 bits `float` can hold, so the result is always `< 1.0f`
  by construction and uniform. This changes the low-order result of **most**
  draws, so it is a bigger re-baseline.

**Why deferred (all three reasons apply):**

1. **External TS parity.** `rng.h` is documented as bit-exact with a TypeScript
   authority. There are **no `.ts` files in this repo** — the authority lives in
   a sibling/parent project. Changing `next_f01()` here without the matching TS
   change would *break* parity, not fix it. This must be a coordinated change on
   both sides, followed by re-baselining every seeded parity test.
2. **Live seed-12345 world.** `spawn.cpp` and `ai.cpp` consume `next_f01()` for
   positions/levels/AI in the exact world the seam/render agent is validating.
   Applying this mid-flight would move creatures and invalidate their reference
   renders.
3. **Full rebuild / re-baseline.** Any change to a header this central rebuilds
   the tree and shifts seeded fixtures.

**Recommendation:** apply during a quiet window with no parallel subworld work,
as a *coordinated* TS+C++ change, then re-baseline the seeded tests in one
commit. Prefer **Option A** if the goal is "close the `[0,1)` hole with minimal
churn"; **Option B** if you want the generator provably correct regardless of
parity history. The local Fisher-Yates clamp already removes the only
memory-safety consequence, so this is a correctness/hygiene item, not an
outstanding crash.

---

## 2. Doc drift: loot-drop precedence (`roll_loot_profile` is the *fallback*)

**Where:** design docs describe monster loot as coming from
`roll_loot_profile` (`src/macro/items.h:108`), but on the actual death path a
creature's **carried `NpcInventory` drops take priority**, and
`roll_loot_profile` is the fallback when there is no carried inventory.

**Change:** a one-paragraph doc correction (in `docs/monsters.md` / wherever the
loot flow is described) stating the real precedence: *carried inventory first,
profile roll as fallback.*

**Why deferred:** the authoritative death/loot code path currently lives in a
file that is **dirty under another agent's edits** (`engine.cpp`), so pinning an
exact line here would be stale by the time it's read. This is a
documentation-only fix; it should be made against the tree *after* the
in-flight engine work lands, reading the then-current precedence directly.

---

## 3. Two hand-synced switches over `NPCType` in the macro overlay

**Where:** `src/ui/macro_overlay.cpp` — `npc_color(NPCType)` (~line 83) and
`npc_sprite(NPCType)` (~line 100) are two separate `switch` statements over the
same enum. Adding an `NPCType` means editing both, and nothing enforces that
they stay in sync (the `default:` arms silently absorb a forgotten case).

**Change (minor, single-source-of-truth):** collapse to one table keyed by
`NPCType`, e.g.

```cpp
struct NpcVisual { ImU32 color; SpriteId sprite; };
const NpcVisual& npc_visual(NPCType t);   // one switch, or a static array indexed by NPCType
```

so a new NPC type is described in exactly one place.

**Why deferred:** purely cosmetic/ergonomic; no behavior change and no urgency.
It touches a UI file, so it is trivially safe to do *any* time there is no other
overlay work in flight — but it is not worth risking a merge collision during
the active subworld push. Low priority.

---

## 4. Faction id spaces don't line up: `settlement_faction` vs loot/fauna factions

**Where:** `src/macro/npc.h:264` — `settlement_faction()` returns one of
`"magika"`, `"barbarians"`, `"timaert"`, `"empire"` (chosen by latitude).
Meanwhile the loot / fauna faction ids in use elsewhere are
`"bandits"`, `"wildlife"`, `"demons"`, `"empire"`.

Only `"empire"` is shared. In particular `"barbarians"` (settlements) vs
`"bandits"` (loot) look like they *might* be meant to be the same faction under
two spellings — or might be genuinely distinct concepts (a barbarian *realm* vs
a bandit *loot table*). It is not clear from the code which.

**Change:** owner to confirm intent, then reconcile to a single faction-id
vocabulary (a shared `enum`/`constexpr` string set) if they are meant to match,
or document that the two id spaces are deliberately separate if not.

**Why deferred:** this is a **design/semantics decision**, not a mechanical bug.
Guessing wrong (e.g. renaming `"barbarians"`→`"bandits"`) could silently merge
two factions that were meant to be distinct. Needs owner sign-off.

---

## 5. `damage_hp` has no floor at 0 (unlike `drain_sp` / `drain_mp`)

**Where:** `src/events/effect_applicator.cpp:17`

```cpp
// damage_hp:
cs.currentHp -= value;              // can go negative
// drain_sp (nearby):
cs.currentSp = std::max(0, cs.currentSp - value);   // floored at 0
```

`damage_hp` lets `currentHp` go negative; the sibling drain effects clamp to 0.
The inline comment states this is **TS-faithful** (the TS authority also lets it
go negative).

**Change (owner decision):** either (a) leave as-is to preserve TS parity and
accept negative `currentHp` (downstream death checks appear to test `<= 0`, so
negative is harmless in practice), or (b) add `std::max(0, …)` for consistency —
**but only if the TS side is changed to match**, else it breaks parity.

**Why deferred:** same TS-parity constraint as item 1 — the asymmetry is
*intentional per the comment*, so "fixing" it unilaterally would diverge from the
authority. Flagged only so the owner can confirm the asymmetry is desired rather
than an oversight.

---

## 6. `generate_settlement_inventory` is dead code — **but intentionally kept**

**Where:** `src/macro/items.h:112` (decl) / `src/macro/items.cpp:300` (def),
plus the authored economy tables it reads (`kSettlementBase`, `kEconFarming`,
`kEconMining`, `kEconTrade`, `kEconFishing`, `kEconCrafting`).

This function currently has no callers. **Do not delete it.** `MASTER_PROMPT.md`
(≈ lines 180-182 and 593-596) records that it and its economy data were *"left
in place on purpose — do not delete the authored economy data ahead of this
design"*, because it is the seed of the unified container/economy system
tracked as **§9.1** (see `proposals/unified-container-system.md`).

**Change:** none now. When §9.1 is designed, this function + tables become the
data source the unified settlement-container path consumes. Listed here only so
a future "delete dead code" sweep does **not** remove it by mistake.

**Why deferred:** explicitly owner-protected; it is pending-design, not dead.

---

## Also flagged by the census (not re-verified this session)

- **POD structs default-constructed without member initializers.** The census
  noted several plain-data component/struct definitions that rely on
  aggregate/zero-init at every call site rather than declaring in-class member
  initializers, which is fragile if a new call site forgets to `{}`-init. This
  was *not* re-located precisely in this session (to avoid churning files under
  active edit); recommend the next agent re-run the census grep for structs used
  as ECS components and add `= {}` / `= 0` in-class defaults where missing. Pure
  hygiene, no behavior change.

---

### Summary for the owner

| # | Item | Kind | Blocked on |
|---|---|---|---|
| 1 | `next_f01()` `[0,1)` root fix | correctness + parity | TS coordination + re-baseline + quiet subworld window |
| 2 | loot-drop precedence doc | docs | in-flight `engine.cpp` edits to settle |
| 3 | overlay: two synced switches → one table | refactor | nothing (low priority, avoid merge collision) |
| 4 | faction id vocabulary reconcile | design | owner intent |
| 5 | `damage_hp` negative-HP asymmetry | parity | owner intent (TS-faithful as-is) |
| 6 | `generate_settlement_inventory` dead-but-kept | design (§9.1) | owner — **do not delete** |

The only memory-safety issue in the whole list (item 1's Fisher-Yates consumer)
is already fixed and regression-tested. Everything else is correctness-parity,
design, docs, or hygiene that is safer to do with the owner in the loop than
autonomously mid-flight.
