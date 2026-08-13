# Dungeons & props — the interior layer and the one interaction

The world has three layers and each is a projection of the one above it. The
macro map is the source of truth; the subworld is that map made walkable; a
**dungeon** is what stands behind a door in the subworld. This document is the
write-up for that third layer and for the prop/interaction system that reaches
it — `ARCHITECTURE.md` carries the summary, this carries the reasoning.

Code: `src/sub/dgn/` (dispatch + one module per interior kind), the dungeon
session in `src/sub/engine.{h,cpp}`, the prop table in `src/sub/map_data.h`,
the materials in `shaders/struct.frag`.

---

## 1. A dungeon is a pocket subworld

**One engine, one ECS, one renderer.** An interior is simulated, drawn and
fought in by the very same `SubworldEngine` that runs the open world. There is
no interior engine, no second combat path, no second persistence rule. What
changes is the *scene*: `enter_dungeon_scene` installs a synthetic resolver on
the ordinary `SeamlessSubworldManager` where the door's cell resolves to the
interior and the eight ring cells to sealed `DungeonRef::Void` filler.
Everything downstream — composite, upload, collision index, battle pass, spell
sweeps — runs the unchanged window path.

Two consequences follow, and they are the reason the model is cheap:

- the window is **static** — no seam check, no re-centre; the room is walled
  and its ring is filler, so there is nothing to cross into;
- the interior renders with **no sea**. The world's water plane sits at
  `WATER_LEVEL` (0.40 of the normalised height range) and an interior floor is
  its cell's own `macroHeight`, which for a coastal town is *below* that. The
  shared plane used to flood cellars.

**Identity, not storage.** `DungeonRef` is `{kind, level, ordinal, footprint}`;
with the door's macro cell and the world seed it hashes — through the one
`dungeon_scene_seed` — into the scene. Same door, same interior, byte for byte,
forever. Nothing below the map is saved, exactly as nothing below the map is
saved for the subworld: an interior is a projection of a projection, and every
lasting act pays UP through a macro stock.

## 2. Ways in and out

`E` is the universal interaction (§4). Outside it opens the door prop you are
**looking at**; inside, a stair shaft under your feet changes storey and the
threshold walks you back out to the exact tile you knocked from.

There are two ways out and both obey the danger law:

| | What it is | Where it lands you | Gate |
|---|---|---|---|
| Walked | the door prop, or a stair shaft | the doorstep you came from, one layer up | the subworld exit gate |
| Quick | the ordinary leave key | the **macro map**, from any storey | the HUD's danger gem must be green |

The quick exit is gated on the gem rather than on the cell's zone on purpose:
a dungeon's macro cell is a town square, so the zone would report the safety of
the *street* while a troll stands two paces behind you (owner ruling
2026-08-12: an interior must not be a place the player walks out of backwards).

## 3. Storeys, and what a module owes

`DungeonRef::level` is signed: 0 is the level the door opens onto, +1 up, −1 a
cellar. A stair is the *same* portal as the street door, pointed at the same
identity one level along — a dungeon→dungeon scene swap that never surfaces.
Two shafts sit at fixed room corners (NW joins 0↔+1, NE joins 0↔−1), so the
geometry is a rule the generator stamps, the engine reads and the test asserts,
rather than data plumbed between them.

`sub/dgn/dispatch.{h,cpp}` mirrors `sub/gens/dispatch`: one self-contained TU
per kind, routed by `DungeonRef::kind`. A module owes three answers:

- its **shape** (`gen_dungeon_*`),
- its **room** (`dungeon_*_room`) — the rectangle the shared rules address,
- its **floor tile** (`dungeon_floor_tile`) — a hall is flagged, a cavern is
  bare scree. Every placement pass reads this rather than assuming a house; the
  cave's first cut used one tile id for floor and walls alike and *nothing*
  could be placed in it.

Three kinds ship, and the differences between them are the point of the layer:

**`house.cpp` stamps.** A rectangle cut by partitions with one doorway each,
furniture placed with a clearance ring, a household, storeys joined by shafts.
A house is understood from its doorway.

**`cave.cpp` grows.** Chambers chained by wandering galleries carved out of
solid rock; connectivity holds by construction because consecutive carve discs
always share ground. A cave has to be walked. The mouth's footprint drives the
cavern (a crack opens on a burrow, a yawning gap on a hall), which is why the
mouths are rolled at different sizes where they are placed.

**`spire_tower.cpp` climbs.** One round hall per storey (the exterior 7-tile
cylinder at the shared ×4 interior scale), a masonry ring of oriented chords,
storeys 0..tier−1 where `ordinal` IS the spire spell's tier — every spire
raises the same tower, only its height varies, so unlike a house the module
ignores the door's footprint. The shafts form a LADDER: the W pad always
climbs, the E pad always descends (an up-shaft tops out on the next storey's
DOWN pad — `dungeon_shaft_arrival_point` is the one dispatch that keeps the
engine's placement and the module's pads agreeing). The top storey has a
second way out: the roof HATCH, which ends the session on the tower's CROWN
in the open-air scene (`playerZ = seat + kSpireTowerHeightM`; the honest
support physics stands the body on the cylinder), where the orb waits
([spells.md](spells.md) §Learning).

## 4. Props and the one interaction

The world is built in two passes. Generators place **props** — every physical
thing that is not terrain — and all of them are `Structure` records in the one
composite array. Everything the engine wants to know about a kind is a
**column** in `kStructureKindRows`:

| Column | What it decides |
|--------|-----------------|
| `minHalfXy`, `minHeight` | degenerate-record floors, shared by renderer and collision |
| `lootId`, `yieldRefHeightM` | what harvesting it pays, through the one loot registry |
| `solid` | whether it enters the collision index |
| `draw` | *which pass draws it* — billboard, solid, or not yet |
| `material` | how it looks (`shaders/struct.frag` has one branch per row) |
| `interact` | what pressing E on it does |
| `opens` | for a Door verb: WHICH interior it raises |
| `lightRgb`, `lightRadiusTiles`, `lightHeightM` | the light it casts |

Per-*instance* payload rides `Structure::tag`: a door carries the ordinal of
the building it opens, a stair its direction. So a new prop is visible, solid,
lit, minimapped and testable the day its row lands — and a new interaction is
one row plus one case in the single dispatcher.

*Scar tissue.* Two hardcoded kind lists used to live in the renderer, one per
pass, so every prop added after them was invisible until somebody remembered to
edit both — that is why doors did not exist for a while even though `E` worked.
And `material` used to be a single wood/stone **bool** that really meant "draw
me like a house", painting every door and bed with the house's red *roof* band.

### Targeting is by look

The resolver takes the prop under the reticle inside a forward cone on the
camera yaw, within that verb's own reach measured to the prop's *surface*
(never its centre, or a wide building would be unreachable from its own
doorstep). There is no proximity fallback — owner ruling 2026-08-12: two doors
side by side must be two different choices. Corpses answer the same rule with
the `Loot` verb although they are ECS bodies rather than composite props, so
there is one keypress and one mental model.

The HUD prompt under the crosshair runs the **identical** resolution, so it can
never offer a verb the keypress will not perform, and it quotes the live
binding.

### The verbs

| Verb | Prop | What it does | Whose law it borrows |
|------|------|--------------|----------------------|
| `Door` | house leaf, cave mouth | raises the interior its row declares | the dungeon session |
| `Stairs` | shaft block | same identity, one storey along | the dungeon session |
| `Loot` | corpse | the kill's own drop | the one loot registry |
| `Search` | chest | a stack of the owning landmark's store, at a price in standing | `Settlement::inventory` + `add_player_reputation` |
| `Drink` | well | an hour of rest, standing | `kSpRegenPctPerHour` |
| `Read` | signboard | names the place | the settlement roster |
| `Learn` | spire orb | flips the spire depleted, burns the orb out of the scene, teaches the spell | `EventTag::SpireDepleted` → the app's ordinal resolve ([spells.md](spells.md) §Learning) |

(The row order of `kInteractRows` MUST mirror the enum — it once ran
Search/Drink/Read against an enum running Drink/Read/Search, so the well
prompted "Search", the sign "Drink", the chest "Read"; the shared 5-tile
reach hid the swap from every smoke. And a zBase-LIFTED prop joins the reach
test with its vertical gap — the roof orb is reachable from its deck, not
from the ground a tower-height below.)

**The chest is the pattern for any verb that must GIVE.** It has no loot row:
a prop that conjures goods is a prop the player farms by walking out and back
in, since interiors re-derive from their identity. So it draws from the store
of whoever owns the place — an emptied town has bare chests, and the goods
return only the way that town's goods ever return. Which stack a chest holds is
hashed from its position in the interior's identity, so searching the same
chest twice does not re-roll the town's goods in the player's favour.

This is also why the larder is **not** a resource field: a field's baseline is
a pure function of terrain and climate, and settlement is downstream of
resources (`macro/resource_field.h`). A larder is the opposite — it exists
because people do — so it belongs to the settlement.

### Light

A lit prop needs no renderer code: the engine hangs an ordinary
`ecs::LightEmitter` body on each one (`rebuild_prop_cache`, rebuilt on the same
"structures changed" signal as the solidity index), so a street lantern reaches
the shader through the very path a carried torch does. Street spacing is **half**
the light's reach, because a pool falls off to nothing at its reach and posts a
full reach apart meet where both give nothing. Density is free: these lamps
land in the light *field*, not in the exact SSBO loop (which carries only the
player and projectiles). Measured at 112 lamps in a 2000-soul city: 52 fps
before and after.

## 5. Population

Interiors populate themselves from the door's context, through the same
spawners and the same stocks the open world uses. The player enters **alone** —
the squad waits outside (owner ruling 2026-08-12, the one deliberate exception
to unified combat).

- **Residents** (house, living storeys): a deterministic household drawn from
  the settlement's `Population` stock, bounded by it — a door in an emptied
  town opens on an empty house. A death behind the door thins the town in that
  tick, through the receipt a street kill settles.
- **Vermin** (cellars, caves, spire storeys): creatures from the den's own
  landmark family of the one monster table — Ruin for a cellar or a cave,
  Spire (the demon rows) for a tower storey — borrowed from the cell's own
  `FaunaCount`. A cleared den stays cleared until the ONE growth law breeds
  the cell back from its living neighbours (resources.md; a den emptied with
  its whole valley stays empty) — one respawn system for subworld and dungeon
  (owner ruling). The difference between a townhouse cellar, a hillside den
  and a spire storey is not a rule but the cell: a settled cell's wild
  capacity is the Ruin table's floor, a mountain's is its own full headcount,
  a spire cell's is its demon garrison — and the scorched yard's roamers and
  the storey guards thin the SAME headcount.

## 6. What is deliberately not here

- **A bell / alarm.** It needs a behaviour the AI vocabulary does not have
  ("come to the sound"); the honest minimum would be a new system, which the
  standing rule forbids without a decision. Not built.
- **A cave hoard's contents.** The chest is placed, but a cave has no owner to
  draw from. The shape of the promise is there; what the dead left is not.
- **Trespass reaction.** Walking into a home is currently free. The rule (theft
  only? guards? reputation?) is a design decision, not a coding one.
- **Culture in props.** An imperial door and a barbarian one are the same
  timber. That is the `material` column when somebody wants it.

## 7. Tests and smokes

| Where | What it guards |
|-------|----------------|
| `dungeon_house_test` | room geometry, connectivity per seed/footprint with a severed-row control, pad safety, partitions, furniture, ceiling, Void filler |
| `dungeon_cave_test` | flood-fill reachability with a walled-ring control, floor/wall distinguishable by tile, apron, prop set + no two grounded solids inside one another, footprint drives the chamber, no storeys, determinism |
| `spire_tower_test` | round hall inside the room circle for every tier × storey, the shaft-ladder rule (pads, tags, hatch, gate), connectivity, sealed masonry ring with a removed-chord control, one cylinder ceiling on the wall crowns, shaft-arrival mapping, floors clamp, severed-hall control |
| `prop_interaction_test` | table consistency, door-per-house with a gapless ordinal set, interior door/stair geometry, snapshot round trip whose control strips a kind from *both* sides |
| smoke `dungeon_house` | enter through the aimed door, land on floor, household + population write-back, storeys, cellar vermin + fauna write-back, chest moves goods and costs standing, well and board, quick exit, tile-hash determinism |
| smoke `dungeon_cave` | hunts a real mouth in the world, enters, hoard present, no stairs, and the danger gate **both** ways |
| smoke `spire_climb` | the whole spire loop live: yard fight (the danger law holds the gate while demons roam), gate, every storey's guard, roof hatch onto the crown (Δz = the tower height), orb → spell learned + spire depleted + orb gone + log entry; STAY hooks (`TIMAERT_SMOKE_SPIRE_STAY=ground\|hall\|roof`) for photo regressions |
