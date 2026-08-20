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

**Stages 3 and 4 — SHIPPED, and in the opposite order to the one first
planned.** The plan said "merge the shaders, then kill the composite". That was
wrong, and the reconnaissance said so: `npc.frag` samples a SLOT and does not
care what is in it, so the composite could be retired without the shader
changing by a line. What had to change was who fills the slots.

`assets/sprite_bank.{h,cpp}` fills them: every drawn body row decoded once at
load into one array layer, one image, one sampler, one descriptor set. That is
the whole class. What it replaced needed an LRU, a staging ring sliced per
frame in flight, a per-frame clock, a bijective frame key, a fallback to a
canonical pose for a starved body, and a 256-seed quantisation — all of it
machinery for a working set of thousands, and all of it gone with the set:

| | paper-doll pool | sprite bank |
|---|---|---|
| slots in play | up to 8192 | **5** (one per drawn body) |
| VRAM | 75.5 MB | 1.3 MB |
| uploads | per frame, staged, raced once | once, at load |
| cold start | real (a walking crowd missed) | none — complete at boot |

Deleted: `character_paperdoll.{h,cpp}`, `paperdoll_atlas.{h,cpp}`,
`character_paperdoll_test`, `assets/character/atlas.bin` + `atlas.png` — the last
dependency on the TS-era atlas — plus the renderer's `descBySeed_` cache and its
facing helper.

The pass split changed with it, and this is the part that matters: it used to
ask *what sort of thing is this* (`archetype == 0xFF` ⇒ paper-doll pass), and
now it asks *what does this row have* — drawn art goes to the banked pass, a
body plan to the procedural one, neither to nobody. `ecs::Sprite::archetype`
became `ecs::Sprite::spriteRow` (a raw ordinal, because ECS may not include
macro/), so a body carries its row and the renderer reads the table like
everyone else.

**The merge — SHIPPED.** `npc.frag` + `creature.frag` → `body.frag`,
`shadow_npc.frag` + `shadow_creature.frag` → `shadow_body.frag`, and on the CPU
side two pipelines, two shadow pipelines, two instance buffers, two counters,
two fill loops and four draw blocks became one of each. A peasant and a wolf are
now the same instance record in the same buffer through the same draw.

The discriminator is the row, carried in `kind`: low 16 bits are the bank slot
or `kBbNoSlot`, high 16 the procedural body plan (`gpu::bb_body_kind`, twinned
by `bb_slot`/`bb_archetype` in doll_pool.glsl). No magic bit — two honest fields
in one lane, with the sentinel the bank already speaks.

The two old pipelines differed in exactly one flag: the doll pass blended, the
creature pass did not, because its coverage is binary. Binary coverage emits
alpha 1.0, for which blending is a no-op — so the merged pipeline blends and
both branches draw exactly as before.

`gpu_smoke3d` now puts BOTH branches in the same crowd, including the
`GPU_SMOKE_NPC_CLOSE` framing that exists to prove body rendering. It used to
draw only banked bodies, so a merge that silently lost the procedural half would
have photographed exactly as green as a correct one.

Cost, accepted knowingly by the owner: a human in the subworld is one static
picture per kind, camera-facing — the same terms every procedural monster has
always had. Walk cycles and facings return with the artist's sheets.

**Scar, paid on the first frame of the cutover:** a PNG arrives head-first
(row 0 = the top), the bank stores the world convention (v = 0 at the FEET), and
the pool this replaced flipped rows at upload. The bank did not, and the entire
town stood on its head. It cost one capture to see and one loop to fix — which
is the argument for looking at a frame rather than trusting a green build.

## Slot size — settled at 256²

The size question was bound to the per-kind collapse, not free on its own: at
the old demand, 128² slots would have cost ~536 MB. With a picture per kind the
whole question evaporates — five slots at the artist's authored 256² cost 1.3 MB,
so art is stored at the resolution it was drawn and nothing is rescaled into
blur. The bank refuses a sheet of any other size, loudly, rather than
guessing.

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
