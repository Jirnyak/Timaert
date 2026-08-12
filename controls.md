# Controls — one universal rebindable keymap

The game has **one** input scheme, shared by the macroworld and the
microworld: every game key is a single row in one table
(`kActionSpec`), and that table drives one **Controls** panel
(menu → Controls), one persisted `keymap.cfg`, and the binding every
consumer reads. There is deliberately no per-world input code to keep in
lockstep — the worlds differ only by each row's *scope*. Adding an action is
one enum value plus one table row (the same *spec-table → auto UI* idiom as
[ui-settings.md](ui-settings.md), whose `UiScope` vocabulary this reuses).

- **Registry / panel / persistence:** [`src/ui/keymap.h`](src/ui/keymap.h) /
  [`src/ui/keymap.cpp`](src/ui/keymap.cpp) (namespace `sm::ui`).
- **Consumers:** [`src/app/main.cpp`](src/app/main.cpp) —
  `handle_event_playing` (edge actions) and `poll_movement` (held actions);
  [`src/ui/screens.cpp`](src/ui/screens.cpp) — toolbar tooltips and the
  pause badge quote the LIVE binding.
- **Defaults table:** README «Controls (current build)» / MANIFEST «Controls».

## Design decisions (owner rulings)

| Decision | Choice | Why |
|----------|--------|-----|
| What is fixed | **Only Esc** | Esc opens the menu and cancels a rebind — the way back can never be bound away. Everything else, F5/F9/Enter included, is the player's. |
| Key identity | **Scancodes, not keysyms** | Positional bindings survive keyboard layouts; the old code mixed `SDLK_*` (layout-dependent) with scancodes and would scatter on non-QWERTY. An unbound action holds scancode 0, which SDL keeps permanently unpressed — no special case. |
| Conflicts | **Steal within a world** | Binding a key that another action of an overlapping scope holds *unbinds* that action (shown as “—”), never silently duplicates. Macro and Sub never conflict with each other — Space is pause above ground and jump below by design; `Both` overlaps everything. |
| Aliases | **One action, one binding** | The Tab→inventory and arrows→pan hardcoded aliases died with the rewrite. |
| Mouse | **Fixed (v1)** | LMB attack, wheel zoom, middle/right drag pan stay hardcoded. |
| Hints | **No hint bar; live quotes only** | The bottom hint bar was removed outright. Any UI text that names a key (toolbar tooltips, the `II PAUSED [..]` badge) formats it from the live keymap at draw time, so a rebind updates every hint the same frame and none can go stale. |

## The registry

- `enum class ActionId` + `struct ActionSpec { id, key, label, scope, def }` —
  `key` is the stable prefs-file identifier (order-independent), `scope` is
  `UiScope::{Both, Macro, Sub}`, `def` the default `SDL_Scancode`.
- `const ActionSpec kActionSpec[kActionCount]` — **the** table, one row per
  action (a `static_assert` guards the count). ~28 rows: 12 Both (panels,
  save/load, enter/leave, debug), 7 Macro (pause, rest, equipment, 4×pan),
  9 Sub (4×move, attack, cast, jump, interact, possess).
- `class Keymap` — `get(id)` / `set(id, sc)` (set applies the steal rule) /
  `reset_defaults()`, plus `pendingRebind`: the panel's “press a key now”
  latch, *armed* by the panel but *consumed* in `handle_event` — the SDL
  event is the only honest source of a scancode, so the capture runs there,
  before the Esc shortcuts and the ImGui keyboard gate.
- `scope_active(scope, subworldActive)` — the one dispatch gate: an action
  fires when the pressed scancode is its binding AND its scope listens in the
  active world. E is the equipment sheet above ground and the interact hand
  below — two actions, one physical key, zero special cases.
- The interact hand targets **by look**, never by proximity: the prop under the
  reticle, within that verb's own reach. The HUD prints `[<key>] <verb>` under
  the crosshair from the very same resolution the keypress runs, quoting the
  live binding — so the prompt cannot promise an action the key will not
  perform. Verbs and props: [dungeons.md](dungeons.md).
- The leave key is the universal way out: from the open subworld it surfaces
  you to the map, and from inside an interior it does the same from any storey
  — both gated on the danger law.

## Persistence

Same tiny, forgiving text-KV idiom as `ui_prefs.cfg`, a sibling in the same
`SDL_GetPrefPath` dir: `# timaert keymap v1`, one `act.<name> <scancode int>`
line per action (`0` = unbound). Missing file / unknown keys / junk values
are non-fatal — defaults stand; a hand-edited duplicate within one world
resolves to the LAST line, the earlier holder left visibly unbound. Saved on
every rebind and on the panel's Reset.

## Tests

`keymap_test` (CTest): spec-seeded defaults, steal rule (same-world /
cross-world / Both-scope / unbind), `scope_active` truth table,
save→load round-trip, forgiving-parser tolerance, `reset_defaults`.
The dispatch itself rides every seed-12345 smoke.
