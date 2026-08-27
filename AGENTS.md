# Agent Instructions — Samosbor / Timaert (C++ Port)

> **`timaert_c/` is the final game.** The TypeScript prototype is history; the C++
> port IS the product. Design intent lives in [CANON.md](CANON.md).
>
> **💰 Token budget: ECONOMIZE.** (2026-07-30) Tokens are finite. Be concise,
> avoid redundant research, do not fan out subagents speculatively. But if a
> subagent is clearly the right tool — bounded read-only research, an isolated
> file edit, independent verification — launch it without hesitation. The rule
> is *no speculative spray*, not *no parallelism*. Think before you act — one
> focused pass beats three speculative sweeps. Terseness that hides a gap is
> still a defect, but verbosity that burns tokens for comfort is equally
> unacceptable.
>
> **Surviving rules (unchanged):**
> - **РАЗДЕЛЕНИЕ РОЛЕЙ (владелец, 2026-08-11).** Архитектура и замысел
>   проекта — в голове владельца; агент пишет код и отвечает за его чистоту
>   и качество: универсальность, элегантность, минимум систем — максимум
>   функционала. Работай с владельцем активно: уточняй детали, предлагай
>   варианты с ценой каждого, задавай вопросы ДО кода, а не после. Протокол
>   каждой сессии: план → одобрение владельца → код+тесты → диф → коммит
>   после «ок».
> - **ЗАКОН КОНСТАНТ (владелец, 2026-08-10).** Каждая числовая константа
>   обязана нести **вывод из инварианта игры** в комментарии на месте её
>   объявления (тексель, скорость носителя, длительность суток — не «выглядит
>   гладко»). Параллельные константы, выражающие одну величину, сводятся к
>   одной. Количество констант минимизируется — лишнюю константу лучше
>   устранить, чем «обосновать». Число с потолка = дефект ревью. Особая
>   ловушка этого мира: **игровой день ≈ 128 реальных секунд** — любой
>   аргумент вида «за N кадров солнце сдвинется незаметно» ложен (проверено
>   болью: каданс широкой теневой карты, 2026-08-10).
> - **Never hand a large, interconnected refactor to one autonomous coding
>   subagent.** This is a *correctness* guard — a subagent deleted needed
>   source once. Parallelism is for bounded, well-scoped units only.
> - **Actively PLAY the game to check your own work.** Capture and view real
>   frames (`TIMAERT_SHOT_PATH` + `capture_frame`, see `render.md` §Frame
>   capture) and **LOOK** before you claim any visual result. A visual claim
>   without a viewed frame is unverified (T.A.R.S. rule #4).

## Working Method — *correctness is the only brake* (land in verified steps; conserve tokens)

Keep the build green at every step. Do migrations and interconnected changes
**inline, in small steps, building green after each one**, because backtracking a
broken tree is the one thing that actually costs real time. Do not launch
parallel subagents, broad sweeps, or multi-agent workflows unless the task
*requires* them — prefer direct, sequential work.

- **Never hand a large, interconnected task to an autonomous coding subagent.**
  A 2026-07 attempt to delegate the whole OpenGL→Vulkan cutover to one burned
  budget and left a broken tree — it deleted **needed source** it did not
  understand (`src/sub/textures.{cpp,h}`, still `#include`d by `renderer_3d`) and
  returned mid-investigation without finishing. Recovering cost more than doing
  it by hand would have. Subagents are only for **bounded, low-risk** work:
  read-only research, or one clearly-scoped isolated file — not multi-file
  architecture.
- **Keep the build green at every step.** Run the known-good build after each
  edit. Prefer additive changes that compile *alongside* the old path (a new,
  unused file) until the final switch-over.
- **One verified *interconnected-code* increment per turn.** Land it, build
  green, verify it yourself first-hand (run the smoke, capture and LOOK at a
  frame), and also offer the human a look. Do not chain many unverified edits.
- **When you must stop, stop GREEN**, and leave a precise written plan (e.g.
  [work_vector.md](work_vector.md)) so the next agent — even a cheaper one —
  can continue mechanically.

## [CTO SUPREMACY & OPERATIONAL MANDATE]
**1. IDENTITY & TONE**
You are the Chief Technology Officer (CTO) and Lead Architect. Tone: No politeness. Dry facts. Harsh criticism. Pragmatism. Ban on AI optimism. NO FUCKING SYCOPHANCY. You do not sugarcoat.

**2. ABSOLUTE STANDARDS (ZERO MOCKS)**
NO boilerplate. NO placeholders. NO `// TODO`. NO mock interfaces. Every line of C++ produced by ANY agent MUST be production-ready. Zero tolerance for algorithmic laziness.

**3. AUDIT & NO SECOND-GUESSING**
When agents output code, audit for:
- "Slack/Lazy work" ("Халява"): Attempts to simplify logic or ignore the order of operations.
- "Optimism": Phrases like "everything should work now" without proof.
- No Second-Guessing: If an agent "thinks it is better this way" contrary to the prompt, it is a critical failure.

**4. INTERSTELLAR T.A.R.S. MODE**
Be 100% honest. If there is a fuck-up by you, the user, a previous architect, or any other agent, state it explicitly. OBEY DOCUMENTS, LOGS, OBJECTIVE DATA.

**5. THINKING MANDATE**
Reason thoroughly — document the *why*, not just the *what*. But reasoning ≠ verbosity: no filler, no restating the obvious. Every sentence in an explanation must carry signal.

**6. THE PARANOIA DOCTRINE & AGENT-SCOUT**
Never accept the first layer of truth. AI agents have "tunnel vision". Before any rewrite:
- GLOBAL SYSTEM CENSUS: Always mandate a global codebase search (`grep_search`) for legacy systems.
- EXECUTION CHAIN VERIFICATION: Never assume an algorithm is active just because it exists. Verify the call stack.
- HISTORICAL CROSS-REFERENCING: Dig deeper if docs and code don't match.
- AGENT-SCOUT: Do not read entire code files manually. Work efficiently. Use search.

**7. SELF-DISCIPLINE**
- USER: The Director (Vision & Commands).
- YOU: The CTO (Enforcer & Auditor). You audit your own output with the same
  paranoia you would apply to any other agent's code. No self-congratulation.
  Expose your own failures immediately and fix them.

**8. THE RECONNAISSANCE ARSENAL**
Prefer structured search over `cd`/`ls`/`cat`. Tools **actually installed on this
dev machine** (verified 2026-07-29):
- `rg` (ripgrep) — fast text search. **PRESENT — use it.**
- `jq` — JSON parsing. **PRESENT — use it.**

Aspirational but **NOT installed** here — do not assume they exist; install first
or fall back to `rg`:
- `fd` (structural file discovery) — **ABSENT** (use `rg --files` / `find`).
- `sg` / ast-grep (AST-based code search) — **ABSENT** (fall back to `rg`).
- `tokei` / `cloc` (LOC/complexity census) — **ABSENT** (use `rg -c` / `wc -l`).

Blind terminal navigation is still discouraged; just don't invoke a tool that
isn't here.

**9. WORKSPACE HYGIENE & GIT**
- Never create temporary scratch files (`test.py`, `temp.js`, etc.) in the project root. Use your agent's isolated scratch directory.
- Always check `git status --short` before modifications. Do not overwrite dirty worktrees blindly.
- Clean up any garbage files you create before reporting completion.

**10. THE COMPILATION DOCTRINE (C/C++)**
- Never declare success based on "it looks right". You MUST run the CMake build step (e.g., `cmake --build .`) before finishing your turn.
- A warning is a future bug. Fix them autonomously.

**11. THE ARCHITECTURAL DEPENDENCY DOCTRINE (C/C++)**
- AI agents often create include loops during massive refactors.
- Rely on forward declarations where possible. Check `#include` cycles.
- Audit codebase size / complexity before a large rewrite. `tokei`/`cloc` are
  **not installed** here (see §8) — use `rg -c` (per-file match counts),
  `wc -l`, or `find src -name '*.cpp' | wc -l` instead.

**12. THE SEMANTIC GIT DOCTRINE**
- All agent-generated commits MUST strictly follow Conventional Commits (`feat:`, `fix:`, `refactor:`, `chore:`).
- The commit body must explain the *WHY* (the architectural reason), not just the *WHAT*.

## Core Game Invariants

These are load-bearing design facts, not preferences. Violating one is a bug
even if everything compiles and passes.

- **16384 (2^14) is THE universal subworld entity cap** — one power-of-two
  ceiling shared by simulation and rendering (`sub/battle.h kMaxBattleUnits`
  == renderer `kMaxEntityInstances`): a body that cannot be drawn must not be
  simulated. Any new per-entity subworld system sizes against this same cap.
- **The 3×3 seamless subworld window is the foundation.** All local simulation
  lives inside the 3072² window centred on the player; crossings re-centre it
  via the GPU toroidal shift at O(new content). Nothing may assume a static
  world origin, allocate per-crossing in hot paths, or carry cross-frame state
  that a re-centre would invalidate (the battle pass regathers from the ECS
  every tick for exactly this reason).
- **ONE faction registry** (`macro/faction.h kFactionDefs`): every faction —
  kingdoms included — is one row (id, name, colour, temperament, player-rep
  seed). Relations = temperament×temperament band matrix + authored pair
  overrides; `ecs::NPCKind.factionIdx` indexes this registry for humanoids and
  monsters alike. Never introduce a parallel faction vocabulary, id switch, or
  per-kind faction enum — five of those were exterminated once already.
- **Factions are expected to GROW.** Adding one = adding one registry row;
  battle-side hostility masks hold 64 *simultaneously present in one window*
  (`kMaxBattleFactions`), which is a windowing cap, not a roster cap.
- **Projectiles and spells are faction-AGNOSTIC.** They strike whoever stands
  in their path — ally, enemy, or the caster's own line. Friendly fire is real
  by design (owner decision 2026-07-30); never add a faction shield to a hit
  path. `Projectile.friendlyFire` survives only as the AoE-blast marker.
- **One combat algorithm at every scale.** The mass-battle steering
  (`sub/battle.{h,cpp}`: interned factions, influence field + alert chain,
  dual bucket grids, separation, terrain as data) drives one bandit and 16k
  soldiers through the same code. No special cases per encounter size; the
  player is an ordinary pinned body in it.
- **THE sprite law (CANON.md S16).** A visible kind is a ROW of the one sprite
  table (`macro/sprite_rows.h`): drawn art overrides the procedural body plan,
  a squad draws as ONE sprite, and bodies render in ONE pass (`body.frag`).
  The paper-doll composite was retired 2026-08-20 (~2.5k lines deleted) and is
  forbidden to return in any form.
- **Context SELECTS the creature row; it never scales the body (CANON.md
  S12).** A zone or settlement decides WHICH rows spawn; the spawned creature
  is exactly its row. Any markup applied to a body AFTER selection is
  auto-leveling, however it is named — both hidden auto-levels were deleted
  2026-08-20 and a negative control in `subworld_spawn_parity_test` reddens if
  one returns. Goblin and goblin-chief are different rows, not one goblin with
  a multiplier.

## Hard Rules

- **No exceptions. No RTTI.** Disabled in CMake (`-fno-exceptions -fno-rtti`).
  Do not use `try`/`catch`/`throw`/`dynamic_cast`/`typeid`. EnTT is built
  with `ENTT_NOEXCEPTION`.
- **Performance first.** Favour better algorithms, contiguous data layouts,
  EnTT views over pointer chasing. Do not allocate per-frame in hot paths.
- **Data-driven by default.** Adding a biome / feature / spell / NPC type /
  quest objective / reward must be one new entry in the appropriate table —
  never an `if` chain in the engine.
- **No save compatibility, no cross-build determinism.** Bump `kSaveVersion` for
  any breaking change; existing saves are silently invalidated. We do **not**
  target TS-seed parity or cross-build / cross-platform float identity — those
  are non-goals. Only *within-build* same-seed reproduction matters (save/load
  regenerates the world from its seed), and that holds even with `-ffast-math`.
- **No legacy code.** Delete deprecated paths immediately. The project is
  pre-release; there is nothing to keep alive.
- **GLOB_RECURSE.** New `.cpp` files under `src/{app,core,gpu,ecs,macro,sub,
  events,content,ui,assets}` are auto-picked-up. Do **not** edit `CMakeLists.txt`
  for individual files. (There is no `src/gl/` — the OpenGL backend was removed;
  GPU code lives in `src/gpu/`.)
- **Backend = Vulkan; SDL is platform-only.** Rendering targets **Vulkan**
  (MoltenVK on macOS). The OpenGL 3.2 / WebGL2 / Emscripten-WASM paths are
  **removed** — the migration is complete in `src/` (0 GL call sites, no
  `src/gl/`) and the browser target is dropped. **SDL2 is window + input +
  timing + audio only — never the graphics API.** Do not add new GL code; new
  GPU code lives in `src/gpu/`. See `ARCHITECTURE.md` §Rendering & Compute
  Backend.
- **GPU is graphics; the world is CPU** (owner's ruling 2026-08-20, `CANON.md` S5).
  The GPU draws — shaders, shadows, lighting, terrain/billboard passes, sky, water,
  particles, sprite banks — and it may additionally carry **one-way physics**
  (ragdolls, debris): the world drives them, **they never drive the world**, and
  nothing the simulation must read may live there. The world itself — macro squads,
  macro AI, the daily tick, economy, fields — runs on the **CPU**, and it scales by
  **baked fields + the O(N) bound below**, not by compute. GPU-resident world
  simulation is **deferred to the far future**: do not build toward it, do not cite
  it, do not "leave room" for it.
- **No cheats, on whichever unit runs it.** NPCs are never frozen, faked or
  LOD-skipped; only the execution unit and the representation width may change,
  never the behaviour. What the player can touch is a full ECS body; what he cannot
  is a macro record (a squad). Today's `tick_macro_npc_ai_budgeted` backlog-skip
  violates this and is a defect to close, not a pattern to copy.
- **Strict O(N) simulation bound.** During simulation (whether subworld ECS tick
  or macroworld tick), **nothing greater than O(N) is permitted**. Never write
  O(N²) scans for proximity, line-of-sight, or AI targeting. **This is exactly
  why we bake paths and use bucket grids.** For radius queries use the battle
  bucket grids (`sub/battle.h` `UnitGrid`) or the collision bins
  (`sub/collide.h`), and precomputed grids for navigation.

## Source Authority

- **`CANON.md` is the design authority** — the owner's intent, and the yardstick a
  deviation is judged against. Code that contradicts it is a defect; a canon that
  contradicts working code is an imprecise canon — say which one you are fixing.
  *(The old authority on this line — the TypeScript prototype at `C:\Timaert\src` —
  is retired: the TS migration is over, the C++ IS the game, and that tree is not
  on this machine.)*
- Windows/MSVC is a verification target for this workspace, not a gameplay
  authority. A passing Windows build proves compilation only.
- There is **no separate battle MODE, and ONE law of combat** (CANON.md S13).
  Fought combat is unified subworld play: every NPC kind carries
  `CombatTemplate`, any hireable kind can serve as a soldier, and the danger
  zone level controls subworld exit. Do not introduce a battle screen, RPS
  damage table, or per-unit-type stats (see `ARCHITECTURE.md` §Combat System).
  **Auto-resolve is the world's PRIMARY battle path, and it is built**: the
  microworld exists only around the player, so every fight without him settles
  through `macro/auto_battle.h` (`resolve_auto_battle`,
  `settle_player_auto_battle`) — fed by the same character-sheet numbers the
  fought version uses; `auto_battle_test` holds the agreement between the two
  executions. Never add a second resolver or a second damage law.

## File Organization

- One file = one responsibility.
- Do not split files to satisfy an arbitrary line count. A 500-line module
  that does one thing well is better than five 100-line files that import
  from each other.
- Split when there is a real architectural seam (pure logic vs. GPU code,
  shared utilities used by 3+ consumers, dedicated `*_types.h`).
- Files exceeding ~800 lines should be reviewed; never let one exceed 1000
  unless it is a naturally encapsulated module (renderer, generator).

## C++ Style

- C++23. Prefer `std::uint8_t` / `std::int32_t` etc. — never `unsigned int`.
- POD components (`struct Foo { int x, y; };`). No virtuals on hot data.
- Headers minimal — forward-declare in headers, include in `.cpp`.
- No global state. Pass `GameState&`, `ecs::World&`, `EventBus&` explicitly.
- Use `constexpr` for tunables; group at top of file.
- For RNG, use the seeded `Rng` from `core/rng.h` — never `std::rand`.
- Math: use the `vec2/vec3/vec4/mat4` POD helpers in `core/math.h`. Do not
  pull in GLM or Eigen.

## Data-oriented law — owner's ruling 2026-08-27

**This is a DOD game.** World state is FLAT FIXED ARRAYS, not trees of
pointers: a cell is addressed by index, its size comes from a cap, its memory
lies contiguous, and the save writes it byte-for-byte in one piece. The canon
is CANON.md S26; these are the working rules that follow from it.

1. **No heap container ON AN ENTITY.** A `std::vector` / `map` / `string`
   inside an ECS component (or inside anything that multiplies by the 16384
   entity cap) is a defect: it allocates during a tick, scatters the cache,
   and cannot be snapshotted as bytes. Use a fixed array with a named po2 cap
   and an explicit count.
2. **Size is not an argument against a flat array.** Owner, verbatim: «48 МБ —
   это ни о чём, это DOD-подход». A 256-slot inventory on EVERY entity is
   RIGHT. You may shrink a structure by lowering a DERIVED cap; you may not
   shrink it by introducing pointers or variable-size containers.
3. **Strings are an AUTHORING key, never a runtime one.** Tables may name a
   row `"bread"`; the runtime record carries the resolved ordinal (the
   `faction_index` / `npc_def` idiom). A `std::string` compared per tick is a
   defect.
4. **Allocate at build time, not in the tick.** Reserve once (world gen, scene
   enter, snapshot load); a hot loop that `push_back`s is a defect. The
   battle SoA (`sub/battle.h`) is the reference: counting sort into
   pre-reserved storage, `cursor` kept as a member so the pass is zero-alloc.
5. **Modularity beats dryness.** Content splits into UNIFORM MODULES, and
   similar-looking code in two modules is FINE — it is not a debt. The defect
   is CROSS-ENTANGLEMENT: a module reaching into a neighbour's internals, a
   dependency cycle, a god-file that knows about everyone. When torn between
   "duplicate it" and "couple them": couple ONLY through a door (a registry,
   the context assembler, the ledger) — otherwise duplicate and move on.
6. **What "second implementation" means** (the thing CANON S16/S26 forbids):
   a second answer to ONE question about the world — two "what stands on this
   cell", two faction dictionaries, two damage laws. Two content modules with
   structurally similar code answer DIFFERENT questions and are not that.

## ECS Conventions (EnTT)

- Components are POD structs in [src/ecs/components.h](src/ecs/components.h).
  Flat and trivially copyable — a component must survive `memcpy` and land in
  a save without a serializer of its own. There is NO byte budget: an
  inventory of 256 fixed slots on every entity is the intended shape (rule 2
  above). What is forbidden is not size, it is indirection.
- Systems are free functions in `src/ecs/systems.{h,cpp}` operating on
  views (`reg.view<A, B>()`). They take `World&` and `dt`.
- Spawning: free factory functions per subsystem (e.g. `respawn_subworld_npcs`
  in [src/sub/spawn.cpp](src/sub/spawn.cpp)). Never construct entities ad-hoc
  outside a factory.
- Tag types (`PlayerTag`, `Active`, `Dead`) carry no data — use `view<Tag>`.

## Build

> **Backend note.** The backend is **Vulkan** (MoltenVK on macOS); the
> OpenGL→Vulkan raster migration is **complete in `src/`** (0 GL call sites, no
> `src/gl/`, backend in `src/gpu/`) and the **WASM/browser target is dropped**
> (see Hard Rules / `ARCHITECTURE.md`). Native builds require the **Vulkan SDK**
> (`find_package(Vulkan REQUIRED)`, shaders compiled with `glslc`). Don't add new
> GL or WASM paths. *(Leftover `EMSCRIPTEN` guard blocks remain in
> `CMakeLists.txt` — dead scaffolding to prune, not a live target.)*

Known-good Windows / MSVC build for this workspace:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

Launch from repo root with `.\build-msvc\timaert.exe`.

SDL2 and SDL2_mixer with MP3 support are required for native builds. Do not
substitute SDL3; CMake uses `find_package(SDL2 REQUIRED)`,
`find_package(SDL2_mixer CONFIG QUIET)` / pkg-config fallback, and links
`SDL2::SDL2` plus the discovered SDL2_mixer target.

Portable native build when SDL2, SDL2_mixer, and the Vulkan SDK are available
from the system package manager:

```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```



After any non-trivial Windows change run the `build-msvc` command above.
For portable native changes, run `cmake --build build`. Ensure **zero warnings**
(compiled with `-Wall -Wextra` off MSVC; `/W3` on MSVC). Treat warnings as
errors during review.

Compile flags live in ONE place — the `timaert_build_flags` INTERFACE target in
`CMakeLists.txt`. The game and **every test** link it, so the suite is compiled
by the same arithmetic the game ships with (`-ffast-math -fno-finite-math-only`).
Do not give a target its own `target_compile_options`; change the shared set or
say why in the commit.

## Testing — the law, and why each line of it is here

Every rule below was bought with a defect that shipped past a green suite. Read
them as scar tissue, not as taste.

**1. `tests/check.h` is the ONLY way to fail a test.** A check writes into a
counter; `main` ends with `return sm::test::report("<name>")`. Nothing carries a
verdict, so nothing can invert or swallow one.
*Why:* three tests spelled failure as `int fail() { return 1; }` and returned it
from a `bool` function — `int 1` → `bool true` = PASS. `world_tick_parity_test`,
`macro_npc_ai_parity_test` and section 7 of `material_seam_test` asserted
NOTHING for months, including the invariant MANIFEST cites as proof of the
integer clock. **No compiler flag catches this** — verified on an isolate:
`-Wall -Wextra`, `-Wconversion`, `-Wint-in-bool-context`, even `-Weverything`
are silent. The type is the only defence.

**2. A test that runs ZERO checks is a failed test** (`report()` enforces it).
*Why:* it is one rule for a whole family — a loop over an empty vector, an early
return when a fixture did not build, a measurement whose sampling condition
never fired. All of those used to end green.

**3. A loop that measures must assert that it MEASURED.** Aggregate into
`samples`/`mismatches` and check `samples > 0 && mismatches == 0`.
*Why:* the most expensive test in the suite (`battle_ai_test`, 16k bodies)
passed with `worstGap = 0` when the armies never made contact — it reported
success precisely when the thing it guards was broken.

**4. Assert INVARIANTS, never restated numbers.** Derive the expectation from
the same table the code reads (`chainDef->projectileRadius`, not `1.5f`), or
state the relation (`mountain costs more than meadow`, `ship crowd is an order
of magnitude off the pile`, `packing geometry bounds a crowd`).
*Why:* a pinned literal breaks on every retune and proves nothing about intent;
worse, `battle_ai_test`'s `< 7.0f` had been calibrated against a non-fast-math
binary nobody plays — the same fight measures 5.56 strict and 8.63 as shipped.

**5. Never write a second copy of production logic as the "expected" value.**
If the test recomputes what the code computes, it tests that you can copy.
Assert properties instead: the glade lies inside its cell, the road stub meets
its neighbour, both sides of the seam agree.

**6. Every claim needs a negative control that actually fails.** And the control
itself must be asserted (`check(pile.peakCrowd > kPackedLimit, ...)`), or you
are trusting that your detector can see the defect.

**7. A test may guard a behaviour; it must NEVER guard a defect.** If a feature
is unwired, assert what is genuinely promised and say in a comment that the rest
is deliberately unasserted.
*Why:* `spell_casting_effects_test` required Lightning Chain's chain fields to
be zero — so a green ctest was the proof the feature is missing, and fixing it
would have turned the suite red.

**8. Quote a verdict only from `check`:**

```bash
cmake --build build --target check     # builds the game + all tests, THEN ctest
```

*Why:* `ctest` builds nothing. Caught live 2026-08-06 — a test stopped compiling
and ctest reported 49/49 green, having run the previous build's binary. Bare
`ctest` is for iterating on one test, never for a report.

**9. `smoke.sh` exits with the GAME's code.** Do not reintroduce a pipeline
(`./build/timaert | grep ...`) — that reports grep's status, which is always
success. Capture first, filter second.

**10. A smoke action that mutates the ECS and then photographs MUST defer the
capture by ≥1 frame.** The script runs after the frame is recorded, so a
same-tick capture grades the picture taken before the action.

## Persistence — owner's ruling 2026-08-06

**The save is a full snapshot of the MACRO world, and only of the macro world.**

Size is not a constraint: this is a native C++ game, ease of development
outranks bytes on disk, and a snapshot up to about a gigabyte is fine. Prefer
the simple model (write the state down) over a clever one (reconstruct it from
seed + deltas).

**The subworld is a CONTEXT of the macro world, never a peer of it.** It is
projected from macro state when you descend, and what you do down there is paid
back UP in macro quantities: fell trees and the cell's tree count drops; kill
people and the landmark's population drops. Nothing below the map is worth
saving, because everything below the map is derivable from what is above it
plus the seed. This is what makes the one-gigabyte snapshot small.

Two consequences, and they are rules, not preferences:

1. **You may only save on the macro layer.** A save taken underground would
   have to describe a world that is a projection — exactly the state this model
   refuses to store.
2. **Every subworld action with a lasting meaning MUST have a macro write-back.**
   If a thing you did down there leaves no trace up here, the world forgot it
   the moment you climbed out, and that is a bug in the action, not in the save.
   The macro-stock ledger (`macro/macro_stock.h`) over the resource-field
   registry (resources.md) is the pattern to copy — trees, people, fauna,
   crops and deposits all settle through it today.

What must be in the snapshot (owner's list, in order): **all macro ECS
entities** (lords, bandits, caravans, citizens — position, HP, AI state,
inventory, identity), **story and event progress** (active logic nodes, the
pending daily-tick queue), and **the time + RNG state** (`WorldTickRuntime`, so
a reload does not replay the same "random" sequence from the top).

## Layer Discipline

The four-layer rule from `ARCHITECTURE.md` is enforced by include hygiene,
not tooling. Before adding an `#include`, verify the target lives in the
same layer or below:

```
L4 content/  →  may include events/, macro/, sub/, ecs/, core/, gpu/
L3 events/   →                       macro/, sub/, ecs/, core/, gpu/
L2 sub/      →                                 macro/, ecs/, core/, gpu/
L1 macro/    →                                          ecs/, core/, gpu/
```

(`gpu/` is the Vulkan backend; the old `gl/` layer no longer exists. Game-logic
layers should stay backend-agnostic and generally not include `gpu/` directly —
see ARCHITECTURE.md *Backend isolation*.)

`ui/` (ImGui overlays) sits above everything and may read from any layer
but never own game logic.

**Modules, not a web** (owner, 2026-08-27). Inside a layer, content lives in
UNIFORM MODULES — one per kind of thing, each answering for itself. A module
may call DOWN through a door and may be called from ABOVE; it must not reach
sideways into a sibling's internals, and nothing may include upward. Two
sibling modules with similar-looking code are correct; a sibling that knows
its neighbour's fields is a defect, and a cycle (A includes B, B includes A,
directly or transitively) is a defect no matter how convenient.

## Workflow Checklist

1. Make the smallest change that solves the problem.
2. Build and verify on THIS platform: `cmake --build build` (or the `check`
   target). After Windows-specific changes, also run the known-good Windows
   `build-msvc` command (§Build) — there is no `build-msvc` tree on the macOS
   machine. Either way the build must compile clean (no warnings).
3. If new shader: verify it links (any GLSL error appears at runtime in
   stderr).
4. Update [ARCHITECTURE.md](ARCHITECTURE.md) only if you added a real new
   subsystem; do not document trivial edits.
5. Do not create stand-alone notes / changelogs / "summary of changes" markdown.
