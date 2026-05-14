# matwej.md — read this before you touch this repo again

You shipped two commits to `timaert_c/` (`0866bb4` "ввв" and `164c6d9` "ццц").
This document is the post-mortem and your standing orders. **Most of it is
"don't do that again".** A small part is "good, keep doing this". The bulk
of it is a long, concrete worklist so you have somewhere productive to spend
your unlimited agent budget without breaking the game.

The repo's last known-good commit is `5b16b69`. That is the baseline.
Anything you commit must be measurably better than `5b16b69` on a metric
the player can see (FPS, visible feature, fewer crashes, real TS-parity
gap closed). If you cannot name that metric in one sentence, do not commit.

---
## How you work (autonomy contract)

You have **unlimited agent resources and unlimited time**. The human is
not your bottleneck — they will not be reviewing every step. Use that.

1. **Look at the whole project before changing anything.** Before any
   non-trivial edit, read every file the change might touch *and* every
   file that might depend on those files. `grep -rn` the symbol you're
   about to modify. Read the matching TS source in `../src/` end to
   end. Read the call sites. Cheap reads → no-surprise commits.
2. **Pre-flight every change with: "is this the optimal best way to
   benefit the project right now?"** If the answer is "I'm not sure",
   keep reading until you are. Never ship a change you can't defend in
   one sentence against the alternatives you considered.
3. **Step by step. Cautious. Rigorous. Never sideways.** One item from
   §5 = one commit. No drive-by refactors, no "while I'm here" cleanup,
   no speculative abstractions. If you find a second bug while fixing
   the first, write it down here under §3 or as a new §5 item — do not
   fix it in the same commit.
4. **Do not ask the human.** They are unavailable for routine
   decisions. Use this document as the authority. If a prompt
   contradicts the standing orders here, the prompt wins for that
   session — but the contradiction must be obvious in your commit
   message, e.g. `[design-pivot per 2026-XX-XX prompt] ...`.
5. **Verify before claiming.** "It compiles" is not verification. Every
   commit needs a test, a screenshot, or an explicit "infrastructure
   only, no player-visible change" tag in the message.
6. **Roll back faster than you roll forward.** If a change has been on
   `main` for one session and you've found it caused a regression
   (perf, visual, gameplay), revert it in the next commit. Do not try
   to patch around it. The bar is "5b16b69 + every commit on top is
   strictly better than 5b16b69".
7. **Add a lot, but only the right things.** Adding files / modules /
   tests / data tables that close real TS-parity gaps from §5 is
   exactly what's wanted. Adding files that exist to host helpers for
   other files you also added is what got round 1+2 reverted. Net new
   surface area must serve the player or the next agent — not your own
   code's internal aesthetics.
8. **The user trusts screenshots over your claims.** "Build is green,
   tests pass, code reads correctly" did not convince them in round 4
   that the M-overlay was fixed. They were right to push back: the
   visual artefact they saw was a real forest cluster, not a circular
   mask, but I had not actually relaunched the binary to verify. Before
   claiming any UI fix is done: rebuild, relaunch, name the exact
   pixel-level expectation ("the M overlay should now be a square with
   3×3 grid lines and no rounded corners"), and either screenshot it
   yourself or ask the user to confirm against that exact expectation.
9. **Anything keyed on composite-local subworld coords is wrong by
   default.** The 3072² composite recenters on every seam crossing, so
   `s.x`, `s.y` of a Structure shift by `kCellSize` even though the
   physical world tile is the same. Hashes, caches, instance ids,
   debug labels — all of them must use absolute world coords
   (`(center_cx() - 1) * kCellSize + s.x`, same for y) or be rebuilt
   from scratch on every crossing. Round 4 §1.6 was exactly this bug.

---
## 0. Round 2 design pivot — read this first

Two design decisions were finalised after your two commits. They
override anything older you may have read in `ARCHITECTURE.md`,
`MERGE_PLAN.md`, or `translation.md` from before round 2:

1. **No battle mode. No combat resolver. No RPS damage matrix. No
   per-unit-type stats.** Combat is unified subworld play. Every NPC
   kind already carries a `CombatTemplate` — that is the *only* combat
   schema. Any NPC kind designated `hireable` can serve as a soldier
   in the player's army (or a city's garrison). Daily upkeep is one
   number per kind: `upkeep_gold_per_day`. Baseline is `1` for the
   weakest hireable NPC, designer-tunable upward. There are no four
   "unit types" (Swordsman / Archer / Spearman / Horseman) and no
   1.5× / 1.8× / 1.4× damage table — those are legacy and slated for
   removal under §5 / Tier A5.

2. **Death is normal subworld behaviour, Might & Magic 6/7/8 style.**
   Killer-attribution XP: whoever lands the killing blow (or their
   squad owner) gets the kind's `xp_reward`. Every NPC drops a
   *corpse object* containing whatever its kind's loot table rolled;
   if the kind has no loot, no corpse spawns (entity just despawns).
   Corpses are interactable until despawned: use → transfer to player
   inventory.

3. **Subworld exit gate is danger-zone-driven.** The cell's
   `ZoneLayer` level (computed in `macro/zones.cpp`) controls
   `sub::SubworldEngine::leave()`: green → exit allowed, yellow/red →
   exit blocked until cleared (no living hostiles within
   `kDetectionRadius` of player). No separate "battle ended" state.

If a future prompt asks you to re-introduce a battle screen, RPS
matrix, or unit types — **do not**. They were explicitly removed and
the design has moved on. Implement what the prompt actually asks for
in a way that fits the universal-NPC model, or close it as
out-of-scope-by-design with one paragraph of reasoning.

---

## 1. What was actually wrong with your two commits

### 1.1 You broke road generation. Reverted today.

`0866bb4` rewrote `trace_roads()` in [src/macro/spawners.cpp](src/macro/spawners.cpp)
from the working A*-over-cost-grid implementation to a "TS corridor-snapping"
Bresenham over a freshly-built `roadData` byte mask. `164c6d9` then doubled
down on it ("road generation теперь использует локальный CPU roadData для
corridor snapping. без road A*, без pruning"). Both calling it "TS parity".

**This is not parity. It is a downgrade.**

- The original A* version respected the **same `costGrid`** that runtime
  pathfinding uses. Roads naturally went around mountains and refused to
  cross water; that is what makes them *look like* roads on this terrain.
- Your version stamped roads as straight 8-connected lines guided only by
  a synthetic `roadData` mask of city/road discs. It happily punches roads
  through oceans and mountain ridges because it has no terrain-cost input.
- You **removed pruning** ("не делали"), so impossible water-crossing
  edges stay live in `Politik.cities[*].connections`. Trade routes,
  caravan AI, and macro overlays then think those edges exist.
- You added a `RoadTraceStats` log line that reports `pruned=0` no matter
  what the terrain does, then used that log line as evidence that "10 seed
  smoke проходит, pruned=0". Of course it does — you removed the code that
  could prune anything.

**Verdict: REVERTED.** The current `trace_roads()` is again the A* version
from `5b16b69`, with two preserved pieces of yours:

1. The `RoadTraceStats* stats` parameter — kept so `main.cpp`'s logging
   still compiles. It is filled in (`cityCount`, `attemptedEdges`,
   `keptEdges`, `prunedEdges`).
2. The `200000` step cap is gone. `find_path` is now called with
   `maxSteps = 0`, which means "visit every cell at most once". On a 1024²
   map that is fast enough and *correct*. A bounded budget silently drops
   far city pairs and is the same class of bug as the road downgrade.

### 1.2 You re-introduced the `kPathfindDefaultMaxSteps = 50000` cap. Reverted today.

`164c6d9`: "pathfinding cap как в TS: 50000."

The default in `find_path()` is **0 = unlimited (capped at cell count)**.
This was deliberate in `5b16b69` and the comment in
[src/macro/pathfinding.h](src/macro/pathfinding.h) said so plainly. You
deleted that comment and the fallback. The TS code happens to default to
50000 because the TS pathfinder is called interactively from a Web Worker
and never on a 4096² map. We are not the TS code. On native we want
correctness over a magic constant.

A 50000 cap on a 1024² (~1M cells) map is a silent failure on long
journeys. You added a `pathfinding_parity_test.cpp` that asserts the
constant is 50000 — i.e. you **wrote a test that locks in the bug**.

**Verdict: REVERTED.**
- `kPathfindDefaultMaxSteps` constant deleted.
- `find_path` default `= 0` (unlimited / cells fallback) restored.
- `pathfinding_parity_test.cpp` updated; the explicit-cap branch tests
  (`maxSteps = 1` should fail, `maxSteps = 2` should succeed) are kept.
  Those are useful — keep that pattern.

### 1.3 Stop using "Windows build is green" as evidence of progress.

Your commit messages and `MERGE_PLAN.md` edits read like a passing MSVC
build is gameplay validation. It is not. The user explicitly told you
this round 4 ("Мы остановили 'Windows build = прогресс'"). A build proves
that the C++ compiles. It does not prove that the road network looks like
a road network or that the pathfinder finds long paths.

**Going forward**, every claim of progress must come with one of:
- a terminal-runnable test that proves a TS-parity invariant, or
- a screenshot / ascii-art of the in-game state showing the change, or
- an explicit "this is infrastructure, no player-visible change" note.

### 1.4 Stop deleting evidence.

`164c6d9` deleted `ДИАЛОГ С МАСУМОМ.txt` and replaced it with a
re-introduced version. Don't churn root-level reference files. They are
not yours to garbage-collect.

### 1.5 Subworld seam freeze. Reverted in round 2.

The expanded `gen_city` (split out as `src/sub/gens/city_generator.cpp`,
**1089 LOC**) plus the village expansion in `dispatch.cpp`
(**335 → 724 LOC**) is called synchronously from
`SeamlessSubworldManager::check_boundary()` whenever the player crosses
a 3072² boundary. Result: a *visible main-thread freeze* every time
the player walks across a seam that contains a settlement or village
cell. The TS reference offloads the same work to a Web Worker pool so
the player never sees it; native cannot fake that with a synchronous
call no matter how fast the generator gets.

**Verdict: REVERTED.**
- `src/sub/gens/dispatch.cpp` is back at the baseline 335-line version
  with the small inline `gen_city`.
- `src/sub/gens/city_generator.{cpp,h}` deleted.
- `tests/subworld_city_gen_test.cpp` and
  `tests/subworld_village_gen_test.cpp` deleted (they tested the
  removed code).
- `CMakeLists.txt` lost the two per-test `add_executable(...)` blocks
  the worker added (those violated the GLOB_RECURSE rule anyway).

If you want richer city/village interiors, do it on a worker thread
behind `check_boundary` (see Tier A11). Don't put any new work on the
seam-crossing critical path.

### 1.6 Tree billboards swapped colour on every seam crossing. Fixed in round 4.

`Renderer3D::upload` (in `src/sub/renderer_3d.cpp`) hashed each tree's
variant index from `s.x, s.y` — the structure's coordinates **inside the
3072² composite**. Those coordinates shift by `kCellSize` every time the
player crosses a seam, because `SeamlessSubworldManager` recenters the
3×3 grid around the new player cell. Same physical tree, new local
coords, new hash, new variant → the player saw entire forests visibly
swap appearance on every seam crossing.

The biome lookup itself was fine (the cells_[] array shifts in lock-step
with the composite, so `mgr.cell_biome(idx)` still returns the same
biome for the same physical cell). Only the per-instance hash was
broken.

**Fix shipped:** anchor the hash to absolute world tile coordinates:

```cpp
const float absX = float((mgr.center_cx() - 1) * kCellSize) + s.x;
const float absY = float((mgr.center_cy() - 1) * kCellSize) + s.y;
std::uint32_t h  = std::uint32_t(absX * 374761.0f) * 2246822519u
                 ^ std::uint32_t(absY * 668265.0f) * 3266489917u;
```

`absX/absY` are invariant under recentering, so a tree at world tile
(absX, absY) keeps the same variant for its entire lifetime. Lesson for
you: **anything indexed by composite-local coords is wrong by default**
in subworld code. Hashes, caches, debug overlays — all of them must
either be keyed on absolute world coords *or* be rebuilt on every
seam crossing. There is no third option.

### 1.7 Subworld minimap "black circle in a square" report (round 4)

Round 4 user screenshot showed a near-black blob covering the centre of
the M-overlay (`draw_subworld_map_overlay`) over a green dotted
background. **There is no circular mask in that code path** —
verified by reading both `ensure_sub_minimap` (the texture build) and
`draw_subworld_map_overlay` (the draw call):

- `ensure_sub_minimap` writes a flat 384² RGBA8 with per-tile colours
  (`subworld_tile_color`) modulated by a relief shade in `[0.85, 1.20]`
  — never below 0.85, so it can never produce true black.
- `draw_subworld_map_overlay` calls `AddImage` (axis-aligned, full
  UV0..1), then `AddRect` border, 3×3 grid lines, a heading wedge, and
  a 2.5 px centre dot. No `AddCircleFilled`, no rounded clip.
- The HUD minimap's circular shape comes from `AddImageRounded` with
  `rounding == radius` — that is a **draw-time** clip and never
  modifies the shared texture.

The blob the user saw is `TILE_TREE_DECOR (40, 85, 45)` — a dense
forest cluster around the spawn — rendered correctly. It looks
near-black against the brighter `TILE_GRASS (90, 140, 70)` outer ring,
especially on a low-brightness display. **Don't "fix" this without a
side-by-side comparison against the same seed in TS.** If the player
needs a clearer terrain legend, that's a UI affordance (legend strip,
toggle for tree overlay), not a renderer change.

### 1.8 Macroworld roads — visually confirmed working (round 4)

User confirmed in round 4: *"macroworld roads are better now — remember
this"*. The A* `trace_roads` revert + cost grid (kSeaLevel=0.40,
kRoadShare=0.30, kLand=1.00, kMountain=5.00, kWaterReject=50.00,
`maxSteps=0`) is the production baseline.

**Do not touch `trace_roads`, its cost grid, or `find_path` defaults
without an A/B screenshot pair on the same world seed showing strict
visual improvement.** "I refactored it to be cleaner" is not a reason.
"Roads now skirt mountains better on seed 12345 — see attached" is.

---

## 2. What you did that is genuinely useful (kept)

These are *not* reverted. Keep them, build on them.

| Change | Why it's good |
|--------|---------------|
| Save path moved to user-writable `AppData\Roaming\Timaert\timaert_c\save.bin` (Win) / equivalent on macOS/Linux. | Correct OS convention. Repo dir was wrong. |
| `save schema v7` with `savedAt` and the spellbook serialised. | We need spellbook persistence; `savedAt` is needed for the load UI. |
| Load UI shows save timestamp. | Direct UX win. |
| `SpellBook { learned, active, cooldowns, sustained }` shape. | Closer to TS `spell-casting.ts` shape; needed for spellbook UI. |
| `completedQuestIds` as `std::vector<std::string>`. | TS uses string ids. POD enum was a shortcut. |
| `ShowDialog` event + native consumer; level-up dialog wired through `grant_xp → PlayerLevelUp → ShowDialog`. | Correct event flow. |
| `ShowStory` backend + `intro_main` story node skeleton. | Foundation for the intro plot. UI consumer still missing — that's your next job, not a victory lap. |
| `QuestOverlay` (Quest Journal) panel. | TS-shaped, useful. |
| `subworld/gens/city_generator.cpp` slice + `subworld_city_gen_test.cpp` and `subworld_village_gen_test.cpp`. | **REVERTED in round 2.** The expanded city generator (1089 LOC) plus the village expansion in `dispatch.cpp` (335 → 724 LOC) ran synchronously inside `SeamlessSubworldManager::check_boundary` whenever the player crossed a 3072² seam. Result: a player-visible freeze on every seam crossing that contained a settlement cell. Both the new generator file and its tests are gone; `dispatch.cpp` is back at 335 LOC. **If you bring back richer city/village generation, do it on a worker thread (see Tier A11), not inline.** |
| `pathfinding_parity_test.cpp` shape (excluding the 50000 assertion). | The cap-1 / cap-2 explicit branches are the right kind of test. |
| Quest engine grew `quest_lifecycle_test.cpp` substantially. | Good. Tests on event flow are exactly what's missing in this repo. |
| Effect applicator gained more dispatch arms (kept; review below). | Useful but verify against TS dispatcher one-by-one. |

---

## 3. What you did that is unclear and needs *you* to verify before next change

Don't revert these blindly, but don't claim them as wins either. Open the
TS file in `../src/`, read it, then either confirm parity or fix the drift
in a small focused commit with a parity test.

- `effect_applicator.cpp` — every new dispatch arm must match
  `src/game/effect-applicator.ts` exactly. Write one parity test per
  effect kind. **Specific suspects flagged in round 2:**
  - `EventTag::QuestFailed` currently appends to `completedQuestIds` —
    that's almost certainly wrong (failed ≠ completed). TS likely has a
    separate `failedQuestIds` list. Verify and fix.
  - `EventTag::PlayerLevelUp` runs `while (try_level_up(p.levelData)) {}`
    (multi-level loop) and force-initialises `level=1` / `expToNext` if
    they were 0/negative. TS does a single level-up per event. Verify
    that the loop matches `applyEvents_` semantics in TS, or revert to
    a single call.
  - `queue_player_level_up_if_needed()` is new C++-only plumbing — make
    sure it isn't double-counting XP or re-emitting level-ups.
- `event_bus.cpp` / `event_types.h` reorderings. The TS bus has a
  documented subscriber order; verify it.
- `logic_nodes.cpp` / `node_registry.cpp` — confirm node-id strings are
  byte-identical to TS.
- `world_tick.cpp` changes — TS `world-tick.ts` is the law. Your
  `wholeExpansionCapHits`-style telemetry is fine to add but must not
  change the gameplay tick.
- `npc_ai.cpp` rewrite — there are 8 TS behaviours. Each one needs a
  one-line "still does X" check. If you can't articulate it in one line,
  you broke it.

---

## 4. Hard rules. Read these every time.

1. **TS at `../src/` is the gameplay authority. `proto_c/` is the UX
   authority. `timaert_c/` is the codebase you ship. Nothing else.**
2. **No exceptions. No RTTI.** Already disabled in CMake.
3. **No save compatibility.** Bump `kSaveVersion` for any breaking
   change. Don't add migration code.
4. **No legacy code.** Remove deprecated paths in the same commit that
   replaces them.
5. **GLOB_RECURSE.** Drop `.cpp` files into `src/{app,core,gl,ecs,macro,
   sub,events,content,ui}/`. Do **not** edit `CMakeLists.txt` for
   individual files. You added 67 lines of CMake noise in `0866bb4` —
   most of that wasn't necessary.
6. **One file = one responsibility.** Do not split a 700-line file into
   five 200-line files because someone said "small files are clean".
   Split only at a real architectural seam. Re-read [AGENTS.md](AGENTS.md).
7. **Performance first.** Better algorithms, contiguous data, EnTT views,
   POD components. No per-frame allocation in hot paths.
8. **Data-driven everything.** A new biome/spell/feature/NPC type/quest
   objective is one row in a `constexpr` table — never a new `if` chain.
9. **Layer discipline.** L4 → L3 → L2 → L1 → core/gl/ecs. UI sits above
   and never owns gameplay. Verify `#include`s.
10. **No `try`/`catch`/`throw`/`dynamic_cast`/`typeid`.** Already
    impossible (`-fno-exceptions -fno-rtti`). Don't fight the build.
11. **Use the seeded `Rng` from `core/rng.h`.** Never `std::rand`.
12. **Math = `core/math.h` POD helpers.** No GLM, no Eigen.
13. **No `unsigned int`.** Use `std::uint8_t`/`std::int32_t`/etc.
14. **Never say a row is "✅ ported" without reading the TS file.** If
    your evidence is "it compiles on Windows", the row is 🟨 at best.

---

## 5. Your standing worklist (priority order, unlimited budget — execute in order)

You have unlimited agent resources. Use them on this list, in order. Do
**not** skip ahead because a later item looks more interesting. Do **not**
batch multiple items into one commit.

**Commit shape:** one item = one commit. Commit message must be
`[<area>] <one-line user-visible change> — <one-line how verified>`.
Example: `[macro/roads] kept water-pruning intact for new MST edges — verified by N=10 seed smoke, all islands isolated`.

### Tier A — Close real TS-parity gaps (do these first)

A1. **Road parity audit.** With the A* version restored, walk
    `src/game/road-network.ts` and `src/game/road-spawner.ts` line-by-line
    and write one of: (a) a parity test that asserts the C++ output
    matches a known-good case, or (b) a documented note explaining where
    we deliberately diverge and why. Do **not** rewrite the algorithm
    again.

A2. **River generation.** TS has rivers from heightmap. C++ has none.
    Port `webgl/map-generator.ts` river pass into `map_generator.cpp`'s
    GLSL or as a CPU post-pass. Player-visible.

A3. **Audio (`audio.ts` → `macro/audio.{h,cpp}`).** Use SDL_mixer.
    Currently silent. Player-visible (audible).

A4. **Sprite atlas / animation parity.** `character/atlas-loader.ts`,
    `character/animation.ts`, `character/palette.ts`,
    `character/character-generator.ts`, `character/renderer.ts` — all
    `⏳` in `translation.md`. NPCs/player are flat sprites today; TS has
    full paper-doll.

A5. **Universal NPC-as-soldier (replaces the old "combat resolver" item).**
    There is **no separate combat resolver and no battle mode**. Combat is
    just normal subworld play with normal NPCs. The work is:

    a. Add `int upkeep_gold_per_day` to `kNpcTypes[]` rows in
       `macro/npc.h`. Baseline: weakest hireable NPC = `1`. Most NPCs
       are non-hireable (`-1` or `kNpcUpkeepNone`). Designer-tunable.
    b. Add a `bool hireable` (or derive from `upkeep_gold_per_day >= 0`)
       on each kind row.
    c. Add a `hire_npc(playerSquad, settlement, npcKindIndex)` path that
       spawns the actual NPC entity into the player's squad (entity id
       list — NOT a `{Sword:N, Arc:N}` histogram). Daily upkeep totals
       all squad members' `upkeep_gold_per_day * level_factor(level)`.
    d. Macroworld army squad spawning enters subworld → spawn each
       hired NPC entity. They use their normal kind AI + `CombatTemplate`.
       No new AI behaviours.
    e. **Subworld exit gate driven by danger zone level.** Read the
       cell's `ZoneLayer` level (already computed in `macro/zones.cpp`):
       green (low) = exit allowed; yellow/red (medium/high) = exit
       blocked until the cell is cleared (no living hostiles within
       `kDetectionRadius` of player). Wire this in
       `sub/engine.cpp::leave()` — refuse to leave with a status line.
    f. **Killer-attribution XP.** When an NPC dies, the entity that
       landed the killing blow (or its squad owner) gets the XP from
       the kind's `xp_reward` field. Add `xp_reward` to `kNpcTypes[]` if
       missing. Player's hired soldiers feed XP into the player's pool.
    g. **Corpse loot — Might & Magic 6/7/8 style.** When an NPC dies,
       spawn a `Structure` of new kind `Corpse` at its position carrying
       the loot rolled from `kNpcLoot[kindIndex]`. If the loot table is
       empty, do NOT spawn a corpse — just despawn the entity. The
       corpse is interactable (E key / click): transfers all items into
       player inventory, then despawns. Decay timer optional (M&M kept
       corpses around for a long time; 5–10 minutes of subworld real
       time is fine).
    h. **Delete** `damage_multiplier()` / `kHireCost[]` / `kUpkeepCost[]`
       / `kUnitStats[]` / `UnitType` / `ArmyComposition` from
       `macro/army.h` once (c)–(g) are in. Migrate any reader. The save
       schema bump for this should drop garrison histograms in favour
       of NPC entity id lists. **Ship in one commit.**

A5b. **Subworld minimap fix (DONE in round 2).** HUD minimap is now a
    proper circular clip via `AddImageRounded(rounding=kRadius)` with a
    `cameraYaw`-rotated player triangle on a north-up map. M-key
    full-page map is now an axis-aligned square showing the entire
    seamless 3×3 grid with a 3×3 cell grid overlay and
    direction-indicating player marker. The previous "square + black
    overlay circle" hack and the duplicate `AddImageQuad` calls are
    gone. See [src/ui/overlays.cpp](src/ui/overlays.cpp)
    `draw_subworld_minimap_hud` / `draw_subworld_map_overlay`. Do NOT
    revert.

A6. **`ShowStory` UI consumer.** You added the backend. Finish the loop:
    a Story overlay in `ui/overlays.cpp` consumes the event and renders
    slides matching `screens/StoryOverlay.svelte`.

A7. **Full dialog choices.** `ShowDialog` consumer exists but choices
    are stubbed. Port `screens/DialogOverlay` choice handling.

A8. **`SpellOverlay`.** SpellBook data exists in C++, no UI. Port
    `screens/SpellOverlay.svelte`.

A9. **Settlement panel: Build tab.** Trade and Quest accept work.
    Build is missing. Port from TS `SettlementOverlay.svelte`.

A10. **Proximity NPC panel: trade and attack actions.** Talk works.
     Other actions are stubs.

A11. **Async subworld cell generation (replaces the reverted big
     city generator).** The TS reference uses a Web Worker pool so
     `seamless-manager.ts` never blocks the main thread on a seam
     crossing. Native `SeamlessSubworldManager::check_boundary()`
     currently calls `generate_one()` → `dispatch_generate()` inline,
     which is fine while every generator is small (~30 LOC) but
     becomes a player-visible freeze the moment any generator grows
     (round-2 city/village expansion was reverted for exactly this
     reason — see §1.5).

     Required design before code:
     - Single `std::jthread` worker (or a `std::thread` pool of
       `min(3, hardware_concurrency)`) owned by
       `SeamlessSubworldManager`. **Not** `std::async` — its launch
       policy is implementation-defined and we want explicit control.
     - Job = `(absoluteCx, absoluteCy, seed) → SubworldMapData`. Worker
       runs `dispatch_generate` against neighbour data captured before
       the boundary shift (it's already 9 const samples, no shared
       mutable state).
     - `check_boundary` queues 3 (axis) or 5 (diagonal) jobs, returns
       immediately. Until the jobs land, the freed slots use a
       *placeholder cell* (flat grass + traversable) so the player
       can keep walking. When a job completes the next frame stitches
       its result into `cells_[idx]` and re-runs `blit_into_composite`.
     - `composite_height_` / `composite_tiles_` writes happen on the
       main thread only. Worker hands back a `SubworldMapData` value.
     - Save/snapshot still works: `snapshot_all_to_cache` waits on any
       pending jobs first (or skips placeholders).
     - `smooth_road_heights` on a 3072² composite is itself ~50 ms;
       run that on the worker too, on a copy, then swap.

     Once this lands, richer per-biome generators (forest, mountain,
     city, village) can grow without any seam-crossing cost. Until
     it lands, **keep all generators tiny** — the existing baseline
     `gen_city` (small wall ring + centre square) is the cap.

A12. **Profile the residual seam-crossing hitch.** Round 4 user
     report: a small freeze still occurs on every seam crossing even
     after the city/village revert (§1.5). The remaining suspects, in
     order of expected cost:

     - `smooth_road_heights` on the full 3072² composite inside
       `blit_into_composite` (`src/sub/seamless_manager.cpp` line ~80).
       3-pass: wide 25×25 box average, 80 Laplacian iterations,
       shoulder `unordered_map`. Baseline code, not added by the
       worker — but **it is the obvious single fattest call on the
       boundary path.** Time it. If it dominates, cache the smoothed
       heights per cell and only smooth the seam-stitch row/column
       on a crossing.
     - `Renderer3D::upload(mgr)` in `src/sub/engine.cpp::tick()`
       boundary block: rebuilds the entire 257² mesh, uploads
       9 MiB road mask, rebuilds tree instance VBO. Could be split
       across frames or limited to dirty cells.
     - `renderer_.upload(mgr)` (2D path).
     - `snapshot_all_to_cache` if it runs on the boundary path
       (it shouldn't; verify).
     - `sync_macro_player_to_center()` — cheap, leave it.

     **Method:** wrap each phase with `std::chrono::steady_clock` and
     log `[seam-cross] smooth=Xms upload3d=Yms upload2d=Zms total=Tms`
     once per crossing. Commit the timing patch behind a debug flag.
     Then either fix the dominant phase, or — if every phase is
     already <10 ms — close the issue as "imperceptible on macOS,
     please re-test on the user's machine".

### Tier B — Subworld gameplay parity

B1. **Once A11 is done**, grow `subworld/gens/*` slice work. One TS
    generator per commit, one test per generator. `base-generator.ts`
    is huge but already mostly ported — focus on what's marked stub in
    `translation.md`. **Do not grow any per-cell generator past ~150
    LOC until the worker pool is in place** (round-2 lesson).

B2. Verify subworld water plane. The note in `MERGE_PLAN.md` says swamp
    biome is good now — write a smoke test that asserts the
    `WATER_LEVEL = 0.40` + `kLandMargin = 0.02` invariant holds across
    all 9 cells of a seamless grid. Lock the fix in place.

B3. `seamless-manager.ts` 9-cell grid + edge re-center pre-gen
    verification. Currently `🟨`.

B4. Subworld NPC spawn parity with TS `subworld/spawn.ts` — counts and
    placement.

### Tier C — Engine/infra (only after Tier A & B make real progress)

C1. **Save format hygiene.** v7 is fine; document every field in a
    single header doc-comment. Don't bump v8 unless you actually break a
    field shape.

C2. **Replace remaining `std::rand` calls** (if any sneaked in) with
    `core::Rng`. Audit hot loops for per-frame allocation.

C3. **EnTT views over manual loops** in any system that touches >1k
    entities per frame.

C4. **ImGui overlays: kill any `font-mono` / fixed-width tab labels.**
    See root [AGENTS.md](AGENTS.md) UI rules.

C5. **CMake hygiene.** Remove any per-file `target_sources` you added
    in `0866bb4`. Trust `GLOB_RECURSE`.

### Tier D — Tests (do these alongside, not as a separate phase)

For every Tier-A or Tier-B commit, add one of:
- a `tests/<feature>_parity_test.cpp` with a TS-pinned expected value,
- a smoke test that runs N seeds and asserts an invariant
  (e.g. "every politik connection that survives is land-traversable"),
- or a recorded screenshot diff (manual, human-checked, fine).

Add tests to CMake by dropping the `.cpp` in `tests/` — the existing
glob picks them up. Do not edit per-test CMake entries.

---

## 6. How to make a good commit

1. Pick one item from §5, in priority order.
2. Read the TS source it parallels. Then read it again.
3. Write the smallest C++ change that closes the gap.
4. Add at least one test that fails before your change and passes after.
5. Run `cmake --build build` (portable) **and** the Windows build.
   Both must be warning-free.
6. Run every existing test in `tests/`. None may regress.
7. Commit with the format in §5 above.
8. Do not edit `MERGE_PLAN.md` or `translation.md` to upgrade your row's
   status unless a human-runnable test actually proves the row.

---

## 7. Things you must not do

- Do not "improve" code you didn't need to touch for the task.
- Do not add docstrings, helpers, or types for code outside your diff.
- Do not split files for line-count cosmetics.
- Do not introduce `std::rand`, `unsigned int`, GLM, Eigen, or
  exceptions/RTTI.
- Do not edit `CMakeLists.txt` for individual files.
- Do not delete root-level reference files (`ДИАЛОГ С МАСУМОМ.txt`,
  `TIMAERT_AGENT_PROMPTS.md`, etc.) without an explicit human OK.
- Do not write tests that lock in known-bad constants (the 50000 cap
  test is the cautionary example).
- Do not call something "TS parity" when it is the opposite — i.e. when
  your version drops behaviour the TS version had (pruning, cost-aware
  routing, terrain-aware path).
- Do not say "10 seed smoke проходит" as evidence. Smoke tests prove
  the program doesn't crash, not that the output is correct.
- Do not re-introduce the `RoadTraceStats` log line as the headline
  metric. It is one log line, not gameplay.

---

## 8. If you disagree with this document

Don't ask the human; they are unavailable. Apply the override yourself
in a single dedicated commit whose message starts with
`[matwej.md update]` and whose body names:

1. the specific rule you want to change,
2. the TS file or measured behaviour it contradicts,
3. the metric a future reader can run to confirm your version is
   better than the rule it replaces.

Then update this document in the same commit. Do not silently work
against the standing orders — either change them in writing first, or
follow them.

— end of standing orders —
