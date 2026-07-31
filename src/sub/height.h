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
// Vertical simulation rules (enforced in SubworldEngine::tick):
//   - Grounded bodies REST on the support surface — max(terrain, structure
//     top within a step, sub/collide.h) — and stick to it while the drop
//     stays inside `kGroundStickM` (walking a slope is not a fall).
//   - Anything above its support FALLS: minimal honest gravity
//     (`vertical_step` below) — constant `kGravityMps2` on a vertical
//     velocity, terminal speed cap, landing on whatever support is beneath
//     (ground, wall walk, gate lintel). Losing flight mid-air, walking off a
//     battlement, a future jump: all the same three lines of physics.
//   - Flying bodies and the flying player: gravity-free 3D movement, clamped
//     to [support surface, flight ceiling]. The ceiling is ABSOLUTE for the
//     loaded 3×3 window — the highest terrain vertex of the window plus
//     `kFlightMaxAboveTerrainM` — so it never sits below any ground the
//     window can show (the old sea-level-relative ceiling put the whole
//     envelope underground in high mountains) and never yanks the camera
//     down when the ground drops away under it (owner decision 2026-07-30).
//   - Projectiles: own their z with NO ceiling — they arc freely and die on
//     honest terrain/structure collision.
// The ONLY 2D in the subworld is generation (heightmap/tiles) and the 3×3
// composite assembly; the simulation itself is full 3D.
#pragma once
#include "sub/base_generator.h"

#include <algorithm>

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

    // ── Minimal gravity (one knob each, honest physics) ──────────────────
    // The whole family is POWERS OF TWO — deliberate house style. Not for
    // speed (a float multiply costs the same for 9.81 or 8), but because a
    // po2 multiply is rounding-EXACT (pure exponent shift → bit-stable
    // determinism), the mental math collapses (v² = 16·h; safe drop exactly
    // 4 m; jump apex exactly 1 m), and honest physics only needs the right
    // ORDER of magnitude — this world owes Earth nothing past that.
    constexpr float kGravityMps2 = 8.0f;
    // Terminal fall speed (a real skydiver is ~55). Also bounds per-tick
    // travel so a long fall cannot tunnel past a thin support.
    constexpr float kTerminalFallMps = 64.0f;
    // Ground stick: a body whose support is within this drop stays ON it —
    // walking down a slope or off a kerb is not a fall. Sized above the worst
    // per-tick descent of the fastest walker on the steepest walkable grade
    // (~35 u/s × 1/30 s × grade 1.0 ≈ 1.17 m), below any ledge worth falling
    // off. Distinct from collide.h kStepUpM (stepping UP is a harder move
    // than keeping your feet going down).
    constexpr float kGroundStickM = 1.25f;

    // Jump take-off speed: v²/2g = 16/16 = exactly a 1 m leap — onto crates,
    // kerbs and low ledges, not onto roofs. One knob, po2.
    constexpr float kJumpSpeedMps = 4.0f;

    // ── Fall damage: honest kinetic energy, not percentages ──────────────
    // Landing harder than the safe speed hurts by the EXCESS kinetic energy
    // E = m(v² − v_safe²)/2, with the body's radius (the one size stat every
    // creature/NPC row already carries) as the linear mass proxy — bigger
    // bodies fall heavier. Flat physical damage, deliberately NOT scaled by
    // max HP: in a systemic RPG a pumped-health character survives the fall
    // that kills a peasant BECAUSE of those points, not despite them.
    // v_safe² / 2g = 64/16: exactly a 4 m drop is free — bruises only. Both
    // knobs po2, so the whole damage curve is mental math:
    // damage = 4 · radius · (dropMetres − 4).
    constexpr float kFallSafeSpeedMps = 8.0f;
    constexpr float kFallDamagePerEnergy = 0.5f; // HP per (mass·m²/s²) unit

    inline float fall_damage(float impactSpeed, float massProxy)
    {
        const float v2 = impactSpeed * impactSpeed
                       - kFallSafeSpeedMps * kFallSafeSpeedMps;
        if (v2 <= 0.0f) return 0.0f;
        return kFallDamagePerEnergy * 0.5f * massProxy * v2;
    }

    // THE vertical integrator, shared by every non-flying body (NPCs, the
    // player, jumpers). Given the support surface under the feet,
    // advances z/vz by dt and returns true when the body ends the step
    // grounded (vz consumed). Pure — unit-tested without an engine.
    //   - RESTING (vz exactly 0 — the grounded invariant this function itself
    //     maintains) within stick range: snap to the support. This is the
    //     slope-walk / kerb-step stick, and it also lifts a fresh spawn out
    //     of the floor (arriving is not falling). A body already FALLING
    //     (vz < 0) never sticks early — it lands on true contact, not 1.25 m
    //     above it;
    //   - otherwise integrate gravity, capped at terminal speed, and land on
    //     the support the moment the feet reach it.
    inline bool vertical_step(float supportZ, float dt, float& z, float& vz)
    {
        if (vz == 0.0f && z <= supportZ + kGroundStickM) {
            z = supportZ;
            vz = 0.0f;
            return true;
        }
        vz = std::max(vz - kGravityMps2 * dt, -kTerminalFallMps);
        z += vz * dt;
        if (z <= supportZ) {
            z = supportZ;
            vz = 0.0f;
            return true;
        }
        return false;
    }

    // GLSL echoes (shaders can't include this header): mesh.vert normalises
    // vertex Y with the literal 1500.0 (= kHeightScaleM); mesh.frag's shore
    // band smoothstep(0.40, 0.47, h) starts at WATER_LEVEL; water.vert takes
    // the plane Y via push constant (fed from kHeightScaleM in
    // vk_renderer_3d.cpp). Change a constant here → update those literals.

} // namespace sm::sub
