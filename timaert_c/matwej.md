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

## 2026-05-15 status overlay

This file is still a post-mortem and standing-order document, but several
worklist entries below are historical. Current verified state:

- Save schema is `kSaveVersion = 8`; `save_roundtrip_test` and GUI
  save/load smoke are the current evidence.
- `ShowDialog` and `ShowStory` have native UI consumers in `ui/overlays.cpp`.
- Spell overlay/casting, NPC Talk/Trade/Attack, settlement trade/quest
  surfaces, subworld time, and story/dialog smoke paths are wired.
- Settlement Build is intentionally a non-action surface because no
  TS/native build-project contract exists.
- Tests are not auto-discovered from `tests/`; each new test executable is
  wired into `CMakeLists.txt` explicitly until that build rule changes.
  `subworld_async_seam_test` is now wired, builds, and passed an isolated
  2026-05-15 MSVC run. The current proof covers axis, diagonal, rapid
  reversal, placeholder, pending snapshot, saved restore, saved structure,
  and sparse 3D road-mask metadata. Latest logged worker generation slices
  were 97.725 ms (road), 40.816 ms (plain), 36.966 ms (diagonal), and
  54.870 ms (rapid reversal); these Debug timings are scheduler-noisy and
  are not a frame-time proxy.
- Native audio is no longer silent: SDL2_mixer is required for native CMake,
  `audio_contract_test` and `audio_runtime_test` are current proof, and the
  no-mixer backend is not a silent native fallback. The dedicated seed-42
  `new_game,wait_boot_done,subworld_audio,quit` app smoke also passed with
  SDL dummy audio and proved the `explore -> subworld -> explore` transition.
- `SeamlessSubworldManager` no longer calls exposed-cell generation inline on
  the seam-crossing path. It uses owned `std::jthread` workers, placeholder
  cells, completed-job stitching, outgoing save jobs, and async composite road
  smoothing. Remaining seam work is runtime upload/smoothing measurement and
  polish, not first wiring.

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
| `save schema v8` with `savedAt` and the spellbook serialised. | We need spellbook persistence; `savedAt` is needed for the load UI. |
| Load UI shows save timestamp. | Direct UX win. |
| `SpellBook { learned, active, cooldowns, sustained }` shape. | Closer to TS `spell-casting.ts` shape; needed for spellbook UI. |
| `completedQuestIds` as `std::vector<std::string>`. | TS uses string ids. POD enum was a shortcut. |
| `ShowDialog` event + native consumer; level-up dialog wired through `grant_xp → PlayerLevelUp → ShowDialog`. | Correct event flow. |
| `ShowStory` backend + `intro_main` story node + native UI consumer. | The intro story loop now reaches `draw_story_overlay`; future work is parity/polish, not first consumer wiring. |
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
   sub,events,content,ui,assets}/`. Do **not** edit `CMakeLists.txt` for
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

A1. **Road parity audit.** DONE for the current native road baseline.
    `trace_roads()` intentionally diverges from TS corridor-guided
    Bresenham: it keeps the terrain-cost A* baseline, component-prunes
    cross-island pairs, blocks water during expansion, and caps large-map
    searches. `road_river_generation_test` covers rejected-water pruning,
    small dry detours, A* terrain-cost behavior, and over-budget pruning.
    Future rewrites still need same-seed A/B proof and must keep the
    rejected-water invariant.

A2. **River generation.** DONE for first native macro integration.
    `map_generator.cpp` builds `TerrainData::riverData` / `riverTexture`
    as a CPU post-pass from the heightmap, carves river cells into the
    land mask, `spawn_trees()` applies the TS-style 2-cell river exclusion
    buffer, and `macro_renderer.cpp` samples `u_riverMap` for the visible
    river overlay. Future work is visual polish, not missing first wiring.

A3. **Audio (`audio.ts` → `macro/audio.{h,cpp}`).** DONE for first native
    wiring. Native CMake hard-fails without SDL2_mixer; `audio_contract_test`
    and `audio_runtime_test` cover the stable registry and dummy-driver
    playback path. The dedicated seed-42 `subworld_audio` app smoke passed,
    including `explore -> subworld -> explore` music transition proof.

A4. **Sprite atlas / animation parity.** DONE for first native wiring.
    `character/atlas-loader.ts`, `character/animation.ts`,
    `character/palette.ts`, `character/character-generator.ts`, and
    `character/renderer.ts` are represented by `assets/character_paperdoll.*`,
    `assets/character_paperdoll_gl.*`, `ui/macro_overlay.cpp`, and
    `sub/renderer_3d.cpp`. Evidence: `character_paperdoll_test` and
    `character_paperdoll_gl_smoke_test` pass with the same atlas hash, and the
    seed-42 app smoke loads `atlas.bin` / `atlas.png` once during boot.
    Future work is animation/pose polish, not missing TS transfer.

A5. **Universal NPC-as-soldier (replaces the old "combat resolver" item).**
    DONE for first native wiring. There is **no separate combat resolver and
    no battle mode**; combat is normal subworld play with normal NPC kinds.
    `macro/npc.h` owns per-kind `upkeepGoldPerDay`, `hireable`, and
    `xpReward`; `macro/army.h` stores concrete `SoldierRecord` entries, not a
    `{Sword:N, Arc:N}` histogram. Legacy `damage_multiplier()` / `kHireCost[]`
    / `kUpkeepCost[]` / `kUnitStats[]` / `UnitType` / `ArmyComposition` /
    `hire_unit` are absent from current source. `sub/engine.cpp::leave()`
    blocks non-forced exit in danger zones while hostiles remain nearby, and
    dead NPCs resolve into player XP plus `Structure::Corpse` loot when loot
    exists. Evidence: `combat_squad_test`; seed-42 app smoke
    `new_game,wait_boot_done,subworld_exit_gate,subworld_loot_xp,quit` passed
    with zone-9 exit blocked, corpse interaction, XP `0->25`, and
    `misc_gem 0->2`.

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

A6. **`ShowStory` UI consumer (DONE).** `ui/overlays.cpp` now opens and
    renders the story overlay from `ShowStory`, and `app/main.cpp` applies
    the intro `StoryResult` path. Keep future changes focused on parity and
    polish.

A7. **Full dialog choices (DONE).** `ShowDialog` consumes `DialogChoicePayload`
    labels/effects. Choices carrying `nodeId` route through the app layer into
    `LogicNodeEngine::activate`; malformed count-only dialogs render disabled
    placeholders with the missing-backend reason.

A8. **`SpellOverlay` (DONE).** The character-panel Spells tab reads learned
    spells, active spell, MP, cooldowns, sustained state, and cast hooks.

A9. **Settlement panel: Build tab (EXPLICIT NO-OP).** Trade and Quest accept
    work. Build is not a real gameplay surface until TS/native build-project
    data exists; do not invent a fake economy.

A10. **Proximity NPC panel: trade and attack actions (DONE).** Talk, Trade,
     and Attack are wired. Future work is parity review and UX polish.

A11. **Async subworld cell generation (replaces the reverted big
     city generator).** DONE for first native wiring. Current
     `SeamlessSubworldManager` owns `std::jthread` workers, queues exposed
     cells on boundary shifts, installs flat traversable placeholders, stitches
     completed `SubworldMapData` back on the main thread, drains outgoing save
     jobs, prunes stale generations, and queues async composite road smoothing
     after placeholders are replaced. Evidence: `subworld_async_seam_test`.
     Current isolated MSVC run exits 0 and reports generation slices of
     70.331 ms (road), 56.191 ms (plain), 339.109 ms (diagonal), and
     25.314 ms (rapid reversal), all with seam-path smoothing at 0.000 ms.
     The harness also proves sparse road-mask indices used by the 3D upload.
     These Debug timings are scheduler-noisy, so do not use this harness as a
     frame-time proxy.

     Runtime seam measurement now exists through `TIMAERT_SEAM_TRACE=1` and the
     `subworld_seam` smoke action. Latest private Debug MSVC app smoke crossed
     a real 3D seam and reported `[seam-cross] gen=22.695ms smooth=0.000ms
     upload3d=51.785ms upload2d=0.000ms total=74.603ms`. This is not a
     release frame-time claim; it proves generation/smoothing are off the
     boundary path and that active renderer upload is now the residual hitch.

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

     2026-05-15 result: timing is implemented behind `TIMAERT_SEAM_TRACE`.
     The first real 3D seam smoke exposed a double-upload fault:
     `upload3d=1949.772ms`, `upload2d=1091.408ms`, `total=3308.131ms`.
     `SubworldEngine` now uploads only the active renderer on the seam frame
     and defers the inactive renderer until view switch. `Renderer3D` also
     skips the full 3072² road-mask upload for no-road composites, builds
     road masks from sparse manager indices instead of scanning 9.4M tiles,
     uploads road composites as a 1024² R8 mask with one-pixel dilation
     instead of a 3072² mask, reuses CPU scratch buffers, and keeps the
     terrain index buffer static.
     A speculative GL sub-update/storage-retention trial was measured and
     rejected after it regressed the same smoke to `upload3d=331.363ms`.
     Latest accepted private Debug MSVC 3D seam smoke: `gen=22.695ms`,
     `smooth=0.000ms`, `upload3d=51.785ms`, `upload2d=0.000ms`,
     `total=74.603ms`. A terrain-payload shader-grid trial
     (height+normal VBO, X/Z reconstructed in the shader) was measured and
     rejected because it regressed to `upload3d=63.248ms`, `total=93.941ms`.
     Latest freshly rebuilt current-code Debug smoke passed at `gen=38.989ms`,
     `smooth=0.000ms`, `upload3d=118.795ms`, `upload2d=0.000ms`,
     `total=157.938ms`; this is not claimed as faster than the accepted
     1024-mask best. Remaining performance target: active 3D terrain/instance
     upload, not worker generation or smoothing.

### Tier B — Subworld gameplay parity

B1. Grow `subworld/gens/*` slice work only with seam timing evidence. A11
    now has worker-generation timing evidence, but bigger city/village slices
    still need one TS generator per commit, one focused test per generator,
    and measurement that seam crossing stays tolerable after worker job
    completion and renderer uploads.

B2. **Subworld water plane.** DONE. `dispatch.cpp` now applies a final
    tile-class height clamp after all mode tile edits and road smoothing:
    `TILE_WATER <= WATER_LEVEL` and every non-water tile
    `>= WATER_LEVEL + kLandMargin`. `subworld_async_seam_test` now clears the
    saved-cell cache around the water-plane case, scans the full 3x3 composite,
    and passed with `water=3145728`, `land=6291456`, `badWater=0`,
    `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`. Latest focused
    generation slices were `roadGen=31.578ms`, `plainGen=23.261ms`,
    `diagonalGen=29.785ms`, and `reversalGen=24.892ms`.

B3. `seamless-manager.ts` 9-cell grid + edge re-center pre-gen verification:
    worker-backed proof exists through `subworld_async_seam_test`; latest
    shared MSVC 13-test run passes with 22.451-94.605 ms generation slices
    (`roadGen=22.451ms`, `plainGen=94.605ms`, `diagonalGen=35.042ms`,
    `reversalGen=54.911ms`). A 5-run focused stability loop also passes after
    the saved-cache lifetime fix. Remaining work is full runtime seam timing
    on target hardware, especially renderer uploads.

B4. **Subworld NPC spawn parity with TS `subworld/spawn.ts`.** DONE.
    `respawn_subworld_npcs` now keeps the TS-fauna RNG stream after
    `roll_fauna`; `roll_fauna` itself uses the TS float path
    (`floor(rng()*span)`, then `rng()*totalWeight` subtraction) instead of
    modulo bias. Spawn consumes placement rolls before the random `+0/+1`
    fauna level and applies the TS `deriveStats` 15% per-level HP/damage
    scale on top of zone context multipliers. Evidence:
    `subworld_spawn_parity_test` passed with `fauna=6 seed=324478056
    zone=5 water_squad_blocked=1`; `combat_squad_test`,
    `subworld_generator_parity_test`, `subworld_async_seam_test`, full
    MSVC build, and seed-42 `subworld_audio,subworld_exit_gate,
    subworld_loot_xp` app smoke also passed.

### Tier C — Engine/infra (only after Tier A & B make real progress)

C1. **Save format hygiene.** Current schema is v8. Document every field in a
    single header doc-comment before the next breaking shape change; only bump
    again when the serialized field shape actually changes.

C2. **Replace remaining `std::rand` calls.** DONE by audit. The 2026-05-15
    scan returned no matches:
    `rg "std::rand|rand\(|srand|random_device|mt19937|default_random_engine" src tests -S`.
    New spawn/fauna parity work uses `core::Rng` only, including TS-style
    float weighted fauna rolls.
    Per-frame allocation audit remains a separate hot-loop profiling task,
    not evidence of hidden `std::rand`.

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

Add tests to CMake explicitly for now. The current `CMakeLists.txt` auto-globs
source modules only; test executables are listed one by one.

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
