// ONE vertical-coordinate authority for the subworld.
// ----------------------------------------------------------------
// The subworld height model has exactly three layers, and every consumer
// (generation, simulation, rendering, tests) derives from the constants here:
//
//   1. NORMALISED heightmap — floats produced by `generate_heightmap`
//      (sub/base_generator.h). Sea surface sits at `WATER_LEVEL` (0.40);
//      land occupies [WATER_LEVEL + kLandMargin, ~1.2] (soft-compressed
//      ridge peaks may exceed 1.0; hard safety clamp at 2.0).
//   2. WORLD METRES — normalised × `kHeightScaleM`. This is the space of
//      `ecs::Position.z`, the 3D camera Y, point lights, particles and all
//      combat distance checks. `Renderer3DVk::sample_height_m(x, y)`
//      returns the terrain surface in this space.
//   3. The WATER PLANE — one global sea level, `kSeaLevelM` (≈ 600 m).
//      Rivers and seas are honest heightmap cells carved below it.
//
// Vertical simulation rules (enforced in SubworldEngine::tick /
// record_shadow):
//   - Ground walkers: pinned to the surface every tick
//     (`pos.z = sample_height_m`).
//   - Flying entities and the flying player: own their z, but are clamped
//     to [terrain surface, flight ceiling]. The ceiling is ABSOLUTE for the
//     loaded 3×3 window — the highest terrain vertex of the window plus
//     `kFlightMaxAboveTerrainM` — so it never sits below any ground the
//     window can show (the old sea-level-relative ceiling put the whole
//     envelope underground in high mountains) and never yanks the camera
//     down when the ground drops away under it (owner decision 2026-07-30).
//   - Projectiles: own their z with NO ceiling — they arc freely and die on
//     honest terrain collision (`pos.z < sample_height_m` → ground blast).
#pragma once
#include "sub/base_generator.h"

namespace sm::sub
{

    // Metres per 1.0 of normalised heightmap. THE vertical scale — the only
    // place the number 1500 may appear.
    constexpr float kHeightScaleM = 1500.0f;

    // World-space sea surface (metres): the global water plane.
    constexpr float kSeaLevelM = WATER_LEVEL * kHeightScaleM;

    // Flight ceiling margin above the loaded window's highest terrain vertex
    // (`Renderer3DVk::max_height_m()`).
    constexpr float kFlightMaxAboveTerrainM = 120.0f;

    // GLSL echoes (shaders can't include this header): mesh.vert normalises
    // vertex Y with the literal 1500.0 (= kHeightScaleM); mesh.frag's shore
    // band smoothstep(0.40, 0.47, h) starts at WATER_LEVEL; water.vert takes
    // the plane Y via push constant (fed from kHeightScaleM in
    // vk_renderer_3d.cpp). Change a constant here → update those literals.

} // namespace sm::sub
