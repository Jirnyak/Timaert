# Sprites — THE one law for every visible body

> **Owner ruling, 2026-08-20.** There is ONE sprite system. A visible kind is a
> ROW; the row names drawn art if the artist has drawn it, and the engine
> generates the body procedurally if he has not. The same law serves the macro
> map and the subworld — a peasant is the same peasant whether you look at the
> world from above or stand next to him.
>
> The paper-doll composite is **not** part of this law. It is being removed.

## The law, in four sentences

1. **A kind is a row.** Adding a creature, an NPC type, a squad banner or a
   landmark is one table row — never an `if` in a draw path.
2. **Drawn art wins.** If the row names an asset, that asset is the body.
3. **Procedural is the floor, not the fallback-of-shame.** A row with no art
   renders from its `archetype` body plan (`shaders/creature_sprite.glsl`:
   quadruped, avian, serpent, biped, undead, hulk, critter) tinted and seeded
   from the same row. A humanoid with no art is a *biped* — it degrades into
   the monster law with no special case anywhere.
4. **One coverage, one shadow.** Whatever produces the silhouette produces the
   shadow: `creature_sprite.glsl` is called by both the lit pass
   (`creature.frag`) and the depth-only caster (`shadow_creature.frag`), so no
   body can cast a shadow it does not have.

Consequences that are rules, not preferences:

- **A squad draws ONE sprite.** Its kind is its picture. How many souls march
  under it is the roster's business (`SquadRoster`), not the map's.
- **Art is expensive, so it is banked, not composed.** A sprite whose pixels are
  expensive to derive is resolved to texels ONCE per unique frame and drawn many
  times (`gpu::SpriteArray`: one image, one sampler, ONE descriptor set, LRU).
  A sprite whose pixels are cheap math stays inline in its fragment stage. The
  per-fragment compositing experiment (7cd71e2) paid the full layer loop per
  PIXEL per FRAME — one close-up body covering the screen cost millions of
  composites.
- **No second delivery.** One composed frame is resolved by one mechanism. The
  crash of problems.md §23.2 was not a pool that was too small; it was the same
  frame being delivered twice, once through the bank and once through 4096
  individual ImGui descriptor sets.

## Why the paper-doll composite is leaving

It generated a face per SOUL: a 5000-strong city meant ~7.2k unique frames in
flight (measured, session 28). That is why the pool had to be enormous, why the
slot had to stay at 48×48, and why a second delivery could exhaust the ImGui
descriptor pool by itself. Under the artist's system a goblin is one goblin for
all goblins: demand collapses from *a frame per soul* to *a frame per KIND*, and
that collapse is what pays for a bigger, better-looking slot.

It was also hardcoded to a single population — town humanoids and squads — while
the game is monsters, armies, and many NPC kinds, all of which the artist draws.

## State of the track

**Stage 1 — SHIPPED (88ac51a).** The macro map's paper-doll delivery is deleted
(`character_paperdoll_gl.{h,cpp}`, the 4096-texture cache, presets, facings,
walk animation, the three-figure squad cluster). Every macro walker draws its
kind's drawn sprite through the one `sprite_get` path, tinted by hour of day
(`figure_tint_for_time`). The ImGui descriptor census fell 4127 → 31, which
killed problems.md §23.2 at its cause. The player's row borrows the peasant art
until his figure is drawn — `player.png` is the title-screen skull and was never
a walker.

Knowingly lost on the map: per-soul appearance, the walk cycle, four facings.
The subworld kept all three — the bank was not touched.

**Stage 2 — SHIPPED.** THE table exists: `macro/sprite_rows.h`, one row per
visible kind — `{id, name, asset, archetype, tint}` — with the enum-order guard
(`rows_in_enum_order`). A goblin and a peasant are now adjacent rows in one
list; one names a body plan, the other a PNG.

Four scattered look-registries collapsed into it:
- `kAssets` (11 PNG rows in `sprite_atlas.cpp`) — gone; the atlas is now only a
  loader that turns a row's `asset` into a texture;
- `npc_sprite()`'s `switch` over `NPCType` — gone; the kind's own row says which
  picture it wears, so a new NPC kind no longer has to remember to edit the
  map's drawing code;
- `NpcTypeDef::portrait`, a path string with **zero readers** — a fourth asset
  vocabulary nobody was speaking. Swapped for `NpcTypeDef::sprite`, so the row
  did not grow and dead data became live;
- `FaunaEntry::color` + `FaunaEntry::archetype` — moved into the table, along
  with `CreatureArchetype` and the billboard aspect ratios, so every statement
  about how a thing LOOKS lives in one header.

Kinds share rows on purpose: eleven NPC types resolve to five pictures, because
every unremarkable townsman is a peasant to the eye. Creatures do not share —
each names the row of its own name, which `sprite_rows_test` enforces (with a
negative control proving the check can fail: pointing goblin at troll's row
reddens it).

Landmarks were already in this shape before the track (`kLandmarkDraw` rows,
guarded, with a documented glyph fallback and spire active/spent as two separate
sprite rows), so they only changed vocabulary.

**Stage 3 — PENDING.** `npc.frag` and `creature.frag` merge: bank slot if the
row has art, procedural silhouette if it does not. The A7 (NPC) and A8
(creature) passes collapse into one, two pipelines and two shadow pipelines
become one and one. The humanoid/monster split disappears from the renderer
because it was never a rendering distinction.

**Stage 4 — PENDING.** The composite dies: `character_paperdoll.{h,cpp}`,
`paperdoll_atlas.*`, `doll_pool.glsl`, `assets/character/atlas.bin` + `atlas.png`,
`character_paperdoll_test`. ~2.5k lines and the last dependency on the TS-era
atlas leave the project. This lands only once drawn art (or the голыши system
below) covers humanoids — retiring it earlier leaves people in the subworld as
motionless cards facing one way.

## Slot size — open, owner's call

The bank packs slots into array layers because MoltenVK caps
`maxImageArrayLayers` at 2048. Today: 48×48 frames, 2×2 per 96² layer, 8192
slots ≈ 75.5 MB. At 128×128 the same 8192 slots cost ≈ 536 MB — which is why the
size question is *bound to* the per-kind collapse above, not free on its own.
Drawn art currently exists in two sizes (128² for peasant/witch/city/corovan/
male/female, 256² for the newer set) and one size must win.

## The future: NPCs are голыши, not paper dolls

Recorded so it is not re-invented differently later. A humanoid will be a base
body drawn by the artist **without colour**; palettes supply skin, eyes, hair
and their colours; clothing layers on top from inventory and context (citizens
in civilian sets, guards in theirs). It resembles a paper doll in spirit and
differs in the thing that matters: it is **extended by adding assets**, not by
growing one monolithic atlas that only its own composer can read.

Nothing of this is built. Do not build it as a special case beside the law
above — it is a third resolution tier *inside* it: art → голыш+palette+clothes →
procedural archetype.

## Where the code lives

| Piece | File |
|---|---|
| Procedural body plans (the floor) | `shaders/creature_sprite.glsl` |
| Row → archetype / tint | `macro/fauna.h`, `ecs::Sprite` (`components.h`) |
| Drawn art registry (macro) | `assets/sprite_atlas.{h,cpp}` |
| Banked frames on GPU | `gpu/vk_sprite_array.{h,cpp}` |
| Map draw path | `ui/macro_overlay.cpp` |
| Subworld passes (to be merged) | `shaders/npc.frag`, `shaders/creature.frag` |
