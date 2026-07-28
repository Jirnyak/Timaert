# UI Settings — one universal show/hide + resize registry

The game has **one** in-game interface-settings system, shared by the
macroworld and the microworld. Every toggleable / resizable HUD element and
pop-up panel is a single row in one table; that table drives one settings
panel, one persisted prefs file, and the per-element visibility/scale that
every draw call-site reads. There is deliberately no per-world or per-widget
settings code — adding a future element is one enum value plus one table row.

- **Registry / panel:** [`src/ui/ui_settings.h`](src/ui/ui_settings.h) /
  [`src/ui/ui_settings.cpp`](src/ui/ui_settings.cpp) (namespace `sm::ui`).
- **Wiring / call-sites:** [`src/app/main.cpp`](src/app/main.cpp).

## Design decisions

Three product choices are baked in (chosen with the owner):

| Decision | Choice | Why |
|----------|--------|-----|
| Persistence | **Global file on disk** | Prefs survive restarts and are shared across all saves — they are a *player* preference, not per-world state. |
| Access | **Pause menu only** | An **"Interface"** entry in the Esc pause menu. No toolbar button, no hotkey — keeps the HUD uncluttered and the discovery path obvious. |
| Coverage | **Everything hideable, panels too** | Chrome *and* pop-up panels each get on/off and (where meaningful) size. |

And one architectural choice: **one system for both worlds.** The macro and
micro views share the same registry, the same panel, and the same prefs file,
so features never get "multiplied" per world.

## The registry — spec table → auto UI

This widens the established *spec-table → auto UI* idiom (`kCustomParamSpec` in
`screens.cpp`) from float-only to **bool + float**:

- `enum class UiScope { Both, Macro, Sub }` — groups rows in the panel only.
  The visibility flag is honoured at every call-site regardless of scope.
- `enum class UiElementId : uint8_t { … , Count }` — one enumerator per
  element; `Count` stays last.
- `struct UiElementSpec { id, key, label, scope, defaultVisible, scalable,
  scaleMin, scaleMax, scaleDefault, tip }` — the static descriptor. `key` is the
  **stable** identifier written to disk (order-independent, so reordering the
  enum never corrupts an existing file); `label`/`tip` are user-facing.
- `const UiElementSpec kUiElementSpec[kUiElementCount]` — **the** table, one row
  per element. A `static_assert` on the row count guards against drift.
- `struct UiElementState { bool visible; float scale; }` — the per-element
  mutable user state.
- `class UiSettings` — holds `std::array<UiElementState, N>`, exposes
  `visible(id)`, `scale(id)`, `mut(id)`, `get(id)`, `reset_defaults()`. Its
  constructor seeds every entry from `kUiElementSpec`, so a **fresh install /
  missing file is already correct** with zero extra logic.

### Registered elements

| Id | key | scope | scalable |
|----|-----|-------|----------|
| `PlayerHud` | `hud.player` | Both | yes |
| `BottomToolbar` | `hud.toolbar` | Both | yes |
| `HintBar` | `hud.hint` | Both | yes |
| `PanelCharacter` | `panel.character` | Both | yes |
| `PanelQuestLog` | `panel.quest` | Both | yes |
| `PanelCodex` | `panel.codex` | Both | yes |
| `PanelMap` | `panel.map` | Both | yes |
| `MacroOverlay` | `macro.overlay` | Macro | **no** (world-space markers) |
| `NpcProximity` | `macro.npcnear` | Macro | yes |
| `PanelDiplomacy` | `panel.diplomacy` | Macro | yes |
| `PanelSettlement` | `panel.settlement` | Macro | yes |
| `SubMinimap` | `sub.minimap` | Sub | yes |
| `SubCombatLog` | `sub.combatlog` | Sub | yes |
| `SubDangerGem` | `sub.dangergem` | Sub | yes |

## Persistence — a tiny, forgiving text file

`load_ui_settings` / `save_ui_settings` are a minimal text key-value store via
`cstdio` (`fopen`/`fgets`/`fprintf`; no `std::filesystem`, matching house
style). Format:

```
# timaert ui prefs v1
# <element> <visible 0|1> <size multiplier>
hud.player 1 1.00
sub.minimap 0 1.25
```

The parser is deliberately **forgiving**: comments (`#`) and blank lines are
skipped; a line may carry `key`, `key vis`, or `key vis scale`; **unknown keys
are ignored** and **absent keys keep their default**; out-of-range scales are
clamped into `[scaleMin, scaleMax]`; a non-scalable element ignores any scale in
the file. This tolerance is the whole point of choosing text over binary —
adding or removing a UI element never invalidates an existing prefs file.

- **File:** `ui_prefs.cfg` under `SDL_GetPrefPath("Timaert", "timaert_c")`
  (`resolve_prefs_path()` in `main.cpp`). On macOS that is
  `~/Library/Application Support/Timaert/timaert_c/ui_prefs.cfg`.
- **Load** once at boot (right after the save path resolves). A missing file is
  non-fatal — the constructor defaults stand.
- **Save** when the Interface panel closes after an edit (a `uiPrefsDirty`
  flag), plus a shutdown backstop.
- **Independent of `save.bin`.** Prefs are global and get their own file with
  its own `# … v1` header. They never touch the per-slot binary save, so they
  never force a `kSaveVersion` bump (which would silently invalidate saves — see
  the Save/Load rule in [ARCHITECTURE.md](ARCHITECTURE.md)).

## Access — the pause-menu "Interface" entry

The Esc pause menu (`draw_pause_menu`) has an **Interface** button that sets
`ShellResult.openInterface`. `apply_shell_actions` handles it like the Codex
entry: `app.state = Playing; app.ui.settings = true;`. The `Playing` branch then
draws `draw_ui_settings_panel(app.uiSettings, &app.ui.settings)`; when it closes
with changes, the prefs are flushed.

## Visibility — pure call-site gates

Each registered draw is wrapped in `if (app.uiSettings.visible(Id))`. Hiding or
resizing an element is a **data change, not new code**. Some UI is deliberately
**not** hideable, because hiding it would soft-lock or remove essential
feedback:

- **Narrative / combat modals** — `draw_show_dialog`, `draw_story_overlay`,
  `draw_encounter_modal` (hiding these blocks progression).
- **Momentary gameplay feedback** — the hit-flash and the centred status line.
- **Dev-only** — the debug HUD (its own `F3` toggle).
- **App-state screens** — title / pause / death / load, and the settings panel
  itself (it must stay reachable).

## Scaling

Scale is honoured differently depending on how an element is drawn — three
mechanisms plus the geometric minimap:

- **A · ImGui windowed panels** (character, quest, codex, map, diplomacy,
  settlement, toolbar, npc-proximity): scale the `FirstUseEver` size hint and
  call `ImGui::SetWindowFontScale(scale)` right after `Begin` — ImGui auto-sizes
  the window to the scaled content.
- **B · full-width HUD bars** (player status bar): scale the bar height,
  `SetWindowFontScale`, and the fixed pixel widths inside it.
- **C · foreground draw-list overlays** (hint bar, subworld combat log, danger
  gem): `SetWindowFontScale` does **not** reach a foreground draw list, so these
  pass an explicit size to the `AddText(font, GetFontSize()*scale, …)` overload
  and multiply every geometry constant by `scale`.
- **Minimap** (`draw_subworld_minimap_hud`) is geometric: one radius choke point
  `clamp(shortSide*kRadiusFrac*scale, kRadiusMin*scale, kRadiusMax*scale)`, and
  its disc is anchored below the top bar via a `topInset` the caller derives from
  the player-HUD element — `visible(PlayerHud) ? kTopStatusBarHeight*scale(PlayerHud) : 0`
  — so the minimap follows the bar when it is resized or hidden (one source of
  truth for the anchor).
- **`MacroOverlay`** is `scalable = false` (world-space markers); its slider is
  simply not shown.

## Input / cursor integration

The Interface panel participates in the single `gameplay_panel_open(app)`
predicate (`main.cpp`), exactly like the Codex / Character / Map panels. That
predicate gates two things while any panel is open:

1. **Mouse capture** — in the subworld, `wants_subworld_relative_mouse()`
   returns false, so SDL relative-mouse (camera) mode is released and the cursor
   is visible and clickable. Omitting the settings panel here caused the cursor
   to vanish the moment Interface opened; adding it to the one predicate fixed
   it for every panel at once.
2. **World input** — `poll_movement()` stops feeding WASD / attack to the
   subworld player, so tweaking checkboxes never also walks or swings in-world.

## Adding a new element

1. Add a `UiElementId` enumerator (before `Count`).
2. Add one `kUiElementSpec` row in the same position.
3. Gate its draw with `if (app.uiSettings.visible(Id))` and, if scalable, pass
   `app.uiSettings.scale(Id)` into the draw.

It then appears in the settings panel, persists across launches, and is
toggle/resize-able — with no other code.

## Testing

`tests/ui_settings_test.cpp` (target `ui_settings_test`, registered with CTest)
covers the persistence/state logic without a window: spec-seeded defaults, the
save/load round-trip, unknown-key / comment / partial-line tolerance, scale
clamping, non-scalable handling, and `reset_defaults()`. The panel drawing needs
an ImGui context and is exercised by the app itself.
