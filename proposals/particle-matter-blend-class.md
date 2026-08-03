# FX matter blend class — fix for the shipped additive blood/dust bug

> Salvaged 2026-08-03 from the graphics-agent session log (`jopus/timaert_shaders.txt`,
> session killed by an API 401 one step before the fix). **The bug is LIVE in
> shipped code** — the buggy content landed, the fix did not.

## The bug (verified against HEAD)

`FxKind::Blood` / `Dust` / `Ember` presets exist (`src/sub/particles.h`,
preset rows in `particles.cpp`) and are actually emitted from combat
(`src/sub/engine.cpp` ~:1430 Blood, ~:1435 Dust). But there is exactly **one**
particle pipeline and it is hardcoded additive
(`src/sub/vk_renderer_3d.cpp` ~:518-525, `create_mesh(..., /*additive=*/true)`),
and `FxPreset` has **no blend-mode field**.

Blood and dust are **matter**, but the particle pass is additive/emissive:

- Blood reads as a red *glow* — additive can only add light, never darken.
- Dust saturates to a white-hot orb — grey cards overlapping on additive push
  all channels to 255, losing the tan hue entirely.

The additive pass is *correct* for energy (magic, fire, impacts, embers — all
shipped and verified); it is *wrong* for matter, which occludes/darkens.

## The fix (universal, table-driven)

Give the FX system a **second material class**: alpha-over (matter) alongside
additive (energy), selected by **one field in the `FxPreset` table**:

1. add a blend-class field to `FxPreset`;
2. partition the instance pack by blend class per frame;
3. second pipeline via `create_mesh(additive=false)` — **which already
   exists** — plus a matter fragment shader (straight-alpha output, no
   white-hot lift that `particle.frag` applies for energy);
4. Blood/Dust move to matter; Ember stays energy; any future smoke/debris is
   one table row.

Squarely graphics/renderer/shader work; generalizes to every future matter
particle with zero engine change.

## Side-finding from the same session

Lit windows on houses are blocked because `mgr.structures()` is a plain list,
not ECS entities (only corpses are ECS) — a structure cannot carry
`ecs::LightEmitter` today.
