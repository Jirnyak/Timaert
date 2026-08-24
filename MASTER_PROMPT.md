# MASTER PROMPT — Timaert (onboarding)

> **You are the next engineer on this game.** This page exists so you and the
> owner speak the same language from the first word. Read it, then read
> **`CANON.md`** — nothing else matters until you have.
>
> Rewritten 2026-08-23. The old 1100-line MASTER_PROMPT described the world of
> 2026-07 (TS parity, paperdoll, GPU crowd sim, possession roadmap) and is
> gone; git history keeps it.

## 1. What this project is

**Timaert** is a single-developer C++ RPG: **one seamless game with two
scales.**

- **Macro world = Mount & Blade.** A living, procedural, toroidal 1024×1024
  overworld — kingdoms, caravans, lords, squads — simulated honestly and
  isotropically: the world lives the same where the player is and where he
  is not.
- **Micro world (subworld) = Might & Magic 6/7/8 (+ Daggerfall).** Zoom into
  any macro cell and you get a first-person, real-3D 3×3 window — the same
  world seen up close, never a second world. Combat is sword-and-magic ARPG
  in place; there is no separate battle screen.

**C++ is the whole game.** C++23 + EnTT + Dear ImGui; SDL2 for
platform/input/audio only; rendering is **Vulkan** (MoltenVK on macOS). The
TypeScript original is dead and holds zero authority.

The owner communicates primarily in Russian; match that when speaking to
him. Radical, T.A.R.S.-style honesty; the owner decides the vision, you
execute and advise; never invent scope.

## 2. Where truth lives

| Question | Document |
|---|---|
| What is the game SUPPOSED to be? | **`CANON.md`** — ЭТАЛОН ЗАМЫСЛА, 26 systems (S1–S26), the owner's words, 2026-08-20. Code that deviates is a defect even if it works and is tested. |
| How do I work here? (build, smoke, commits, discipline) | **`AGENTS.md`** — process rules, non-negotiable. |
| Which doc covers what? | **`MANIFEST.md`** — the map of all docs. |
| How does today's code measure against the canon? | **`canon-audit.md`** — the current audit. (`audit.md` is a historical log; `problems.md` is a bug→cause→fix journal, not a plan.) |
| Long-term memory across sessions | `/Users/jirnyak/.claude/projects/-Users-jirnyak-Mirror-timaert/memory/MEMORY.md` — read the index at session start. |

The canon in one breath: the macro world is the truth and the micro world its
derivative (S2); squads are the only macro entity and the player is just a
flag (S4); the world is fields on cells, and cells do not think (S5); every
mechanic asks ONE context door (S6); content is table rows, never if-chains
(S16); minimum systems, each maximally general (S26). Save = a full macro
snapshot, `kSaveVersion` currently **42** (`src/macro/state.h`), no backward
compatibility — bump on break.

## 3. Current focus of work

**The context door (CANON S6).** Every mechanic (spawn, movement, combat,
resource growth, light, sound, trade price, AI) must ask the context of a
cell in ONE place and receive the summed contribution of ALL systems — biome,
feature, landmark, danger, season, weather, black energy, time of day,
knowledge — with zero as a legal, silent contribution. Today context is
gathered piecemeal (`CellContext`, spawn weights, step cost, ledger); that is
the next major seam. Related open front behind the same door:
spawn-composition weights (the autolevel was demolished 2026-08-20 — S12;
until the weights exist, zones do not affect spawn at all, honestly).

## 4. Things that are DEAD — do not resurrect them

- **TS authority / parity.** No TypeScript file judges anything; `timaert_c/`
  is gone. Decisions are made on the merits, against the canon.
- **«Monsters ≠ NPCs».** One creature table from rabbit to dragon, one loot
  table, one role-playing sheet for everyone (S14/S16). A squad member's
  `kind` is one 16-bit id space — a wolf can stand in a roster (save v42).
- **The paperdoll.** The 37-layer composite, `npc.frag` bank, doll pool —
  demolished entirely 2026-08-20 (−2.5k lines). Sprite law: a visible kind is
  a ROW; one static image per kind; the bank is `src/assets/sprite_bank.*`,
  5 slots of 256×256. See `sprites.md`.
- **GPU macro simulation.** Deferred to the far future by the owner (S5):
  squads run on the CPU; scale is held by baked paths and O(N) discipline,
  not compute shaders. Docs that present-tense a GPU crowd sim are wrong,
  not aspirational.

## 5. First moves in a new session

1. Read `CANON.md`, then `AGENTS.md`, then the memory index.
2. Verify before asserting: re-check every fact against `src/` with `rg` —
   docs drift; the code is the ground truth of what IS, the canon of what
   SHOULD BE.
3. Build: `cmake --build build --target timaert -j`. Verify: the `check`
   target (not bare ctest) + validated smokes with a PINNED seed and a sweep
   (`sh smoke.sh <tokens> 12345,1,7,999`). One green build + one validated
   smoke per increment; no regressions; tests assert invariants with
   negative controls.
4. Code changes only with the owner's explicit per-change approval — propose
   options with a recommendation, never edit unasked.
