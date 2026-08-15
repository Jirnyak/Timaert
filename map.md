# Map & Knowledge — THE doc

The map system and the exploration (fog-of-war) mechanic: what the player
knows of the world, how knowing spreads, how the live view and the map page
draw that knowledge, and what the subworld's map instruments are. Shipped
2026-08-15 (the map & knowledge track); owner rulings quoted where they are
load-bearing.

**Owner ruling №1 — the map is an IMAGE of the world, never the world.**
The map page is a menu drawn OVER the game with its own presentation and its
own (annotation-only) input. Nothing on it commands the world.

**Owner ruling №2 — the remembered world is the same frame, drowned.**
The live view's explored-but-not-in-sight zone shows EVERYTHING the visible
zone shows — terrain, forests, roads, sun, shadows, night, town glow —
except living entities, merely faded. No special cases per layer.

---

## 1. The knowledge layer (the mechanic)

[src/macro/knowledge.h](src/macro/knowledge.h) — one byte per macro cell:

| Level | Meaning | Persistent? | Live view | Map page |
|---|---|---|---|---|
| 0 `Unknown` | terra incognita | — | black | black |
| 1 `Explored` | memory (charted) | **yes — save v40** | the finished frame, drowned | full chart colours |
| 2 `Visible` | in sight NOW | no — projection | the living picture | same as Explored (a chart shows what is charted) |

Only Explored persists: the save clamps Visible → Explored on write, and a
load recomputes sight from the restored position. "What I see now" is always
a projection of where the player stands — never cargo.

### Sight IS light

`update_player_sight` runs the SAME bounded terrain-optical Dijkstra as the
night-glow bake — [src/macro/optics.h](src/macro/optics.h) `optical_sweep`,
one physics, one cost table:

- open land spends 1.0 budget per cell — the baseline;
- roads spend less (0.65 / 0.85) — the eye carries far along a corridor;
- tree canopy adds `kCanopyOpticalCost`·density — a massif smothers sight
  exactly as it smothers glow;
- every uphill step pays `kClimbOpticalCost`·rise — a ridge walls a valley
  off; downhill is free — a hilltop overlooks its plain.

The sight **budget is derived, not tuned** (`player_sight_budget_cells`,
main.cpp): a standing eye at `kBodyEyeM` sees to the horizon
d = √(2·R·h) ≈ 4.7 km ≈ 4.6 macro cells of open ground. **Future: attributes
and skills MULTIPLY through this one door** — perception, a ranger's eye, a
spyglass item, a high-ground perk are all one factor applied in one place.

### The rhythm

- One integer compare per frame; the sweep runs only when the player
  CROSSES a cell border (and once after every boot/load — the sight anchor
  starts invalid). Nothing per-frame, nothing in hot loops.
- On a crossing: yesterday's Visible set decays to Explored, the new sweep
  marks the new Visible set. Decay is the ONLY forgetting in the system —
  memory itself never fades.
- A new world starts all-Unknown; the spawn's surroundings open by the
  ordinary law — the first frame's sweep — not by a special starting reveal.

### The doors

- `gs.knowledge.at(x, y)` — the ONE question every consumer asks
  (fail-closed: an absent grid answers Unknown).
- `reveal_area(x, y, budget)` — the quest/event door: marks MEMORY, never
  sight, with the same optics from the target's viewpoint ("the elder
  described the place"). Wired: every derived quest pin reveals its target
  area when the quest-marker set changes (process_world_events).
- `revealmap` (dev console) — full-vision toggle THROUGH the system: on =
  every cell pinned Visible and registered in the sight list, sweep
  suspended; off = the ordinary decay runs — the whole map stays charted
  (drowned), the live disc re-sweeps. No un-reveal path exists because none
  is needed.

### Persistence

Save v40 carries the grid whole (cell order = byte order, clamped to
{Unknown, Explored}); a zero-length grid in a payload means "layer never
built" (partial fixture states) and the world simply stays dark. A non-zero
grid must cover the map exactly — anything else is a corrupt payload, fail
closed. Roundtrip + clamp locked by `save_roundtrip_test`; the sweep law
(canopy shortens, roads extend, ridge blocks/crest overlooks, decay, reveal,
torus, fail-closed door) by `tests/knowledge_test.cpp` — all relations
against the cost table, no pinned numbers.

## 2. The live macro view (the knowledge law in render)

`u_knowledgeMap` — R8, binding 5, LINEAR + repeat, encoded level/2, so the
fog border breathes across a cell instead of stepping. Refresh is surgical
and frequent-safe: `upload_knowledge_field` rewrites texels IN PLACE
(`VulkanTexture::update_region`, queue-ordered barriers, its own fence — no
`vkDeviceWaitIdle` drain on a mere cell crossing); gated by
`knowledge.revision`, above the pause gate so a fresh boot never shows a
frame of yesterday's fog.

The law is two blends at the END of macro.frag, over the finished frame:

```glsl
drowned = mix(col, luminance, 0.62) * vec3(0.70, 0.76, 0.86);
col     = mix(drowned, col, visible) * explored;
```

- **Visible** — the frame as composed.
- **Explored** — the SAME finished frame (sun, cast shadows, night tint,
  settlement glow, water glints — everything) drowned: ~⅓ saturation, cool
  cast, slight dim. Ground seen through still water; legible as country,
  visibly not-now.
- **Unknown** — black.

Entities are CPU overlays and obey the same door: walkers draw only in
Visible cells; landmarks hide in Unknown and fade in Explored (alpha only —
the ONE registry colour stands); marker pins need Explored; the hover
tooltip answers "Uncharted" and speaks live tree counts only inside sight.

Dev harnesses that pass no layer get a 1×1 Visible texel — their pictures
are the fog-free world, golden captures unchanged.

## 3. The map page (M)

[src/ui/map_screen.h](src/ui/map_screen.h) — a full-screen MENU while the
world is paused (the ordinary pausing-panel bit). While it is open the world
overlay is **not drawn at all**; the page draws everything itself.

### The chart basemap

`pc.mapStyle` selects the CHART composition in macro.frag — a document, not
the world: flat atlas biome colours with the altitude ramp, water depth
shading, ink-stroked roads, forest density darkening the print, a FIXED
north-west cartographic hillshade. No clock, no night, no glints, no motion.
Everything is CELL-SHARP — classification from the cell-centre texel
(`bt_biome`'s convention), and the shoreline is a thin ink band inside the
water cell along every water↔land cell edge, so a carved one-cell river
prints as exactly one crisp outlined cell. (The first cut traced a
smooth-height iso-line, which oscillates around a one-cell canyon and
printed wavy triple bands — the rivers were always honest water cells; the
chart's pen was wrong.)

### On the chart — primitives only

No sprites, no paperdolls on a document (owner):

- **Landmarks** — the presentation row's `MiniShape` (ring / dot / diamond,
  [src/ui/landmark_draw.h](src/ui/landmark_draw.h)) at the ONE registry
  colour; ashen when depleted, faded when merely remembered; names as text
  at the row's label zoom.
- **Markers** — ink: the waypoint diamond and POI cross are geometry (★◆ are
  not in the ImGui font), quest/danger keep the typographic "!".
- **The player** — a cyan ring + crosshair position mark.
- Hover rect + tooltip (coords; "Uncharted" or the biome via the one
  `biome_at` authority + landmark name).

### Camera — derived bounds

Floor = the whole world's height fitted to the viewport; ceiling = the live
view's `kMacroZoomMax`; first open = their geometric mean (the region
scale). Wheel zooms, drag GRABS the world 1:1 (dpr-honest), toolbar +/−
drive whichever camera owns the screen. The camera survives page closes;
every open re-anchors on the player.

### Annotation, not command

A map click never orders a march and never opens a settlement. A single
click toggles a **waypoint pin** on the clicked charted cell (ink cannot
land on terra incognita) through the universal marker layer — and
`kMarkerSurface` ([src/macro/markers.h](src/macro/markers.h)) states each
style's surface as data: **waypoints exist ONLY on the chart**, never
floating in the world; quest/POI/danger signal on both. Pins persist in the
save like any marker. The chrome lists them: rename in place, jump the
camera, lift.

### Chrome

Header (size, seed, position, Charted % — a census recounted only when the
knowledge revision moves) + a registry-driven legend that lists **only what
the player has discovered** — totals never leak how much world is left.

The old 256×256 CPU-baked minimap window (`MiniMapCache`) is deleted.

## 4. The subworld's instruments

The subworld is a projection of the macro world (Persistence ruling), so
**knowledge is a macro concept** — the subworld has no fog layer of its own
and needs none; you are standing there, you see it.

- **Minimap HUD** — circular, always-on top-right
  (`draw_subworld_minimap_hud`, overlays.cpp): the local 3×3 cell tile
  composite around the player, rebuilt only when the seamless centre shifts
  or every ~2 s; live NPC/monster blips coloured by `PlayerRelation`;
  rotates with the camera yaw.
- **Subworld map page** — the same M key while below
  (`draw_subworld_map_overlay`): the whole 3×3 composite + player marker in
  a window.

Both untouched by the macro knowledge track; if the subworld instruments
ever want the chart language (primitives, ink) they adopt the same
presentation rules, not a second system.

## 5. Future tracks (design intent, doors already standing)

- **Vision from attributes & skills** — one multiplier inside
  `player_sight_budget_cells()`. Perception/awareness attributes, a scout
  skill, a spyglass, a watchtower bonus: all are factors on the ONE budget;
  the optics (canopy, ridges, roads) already shape whatever budget arrives.
- **Trackers / следопыты** — "who passed here": the natural shape is a
  per-cell PASSAGE field (age-stamped counts written by macro walkers on
  cell crossings — the same write rhythm as sight), read by a tracking
  skill: high skill turns recent passage into overlay hints ("riders, half
  a day old"). A layer + a presentation rule, no new architecture; pairs
  with the seasons/weather track (rain washes tracks).
- **Landmark rumours** — "every city known as a rumour" (start-knowledge
  option discussed and deferred): a reveal variant that marks landmark
  cells Explored without their surroundings, through the same
  `reveal_area` door with a zero budget.
- **Shared knowledge** — party members / hired scouts contributing sweeps:
  more SOURCES for the same `update_player_sight` call, nothing else
  changes.

## Files

| Concern | File |
|---|---|
| Knowledge layer + sight + reveal | `src/macro/knowledge.{h,cpp}` |
| The one optical Dijkstra (light AND sight) | `src/macro/optics.h` |
| Sight budget door + triggers + `revealmap` | `src/app/main.cpp` |
| Render law + chart composition | `shaders/macro.frag` (binding 5, `pc.mapStyle`) |
| Knowledge texture upload | `src/macro/vk_macro_renderer.{h,cpp}` |
| The map page | `src/ui/map_screen.{h,cpp}` |
| Marker surfaces (map ink vs world signal) | `src/macro/markers.h` `kMarkerSurface` |
| World-overlay knowledge gates | `src/ui/macro_overlay.cpp` |
| Subworld minimap + map page | `src/ui/overlays.cpp` |
| Save block (v40) | `src/macro/save.cpp` |
| Tests | `tests/knowledge_test.cpp`, `tests/save_roundtrip_test.cpp` |
