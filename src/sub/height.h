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

    // ── DRY FOOTING — where a body may be MATERIALISED ───────────────────
    // A place is dry when what would carry a body there stands above the
    // water plane — and "what carries" is the support surface, max(terrain,
    // the solid top under the feet), not the ground alone. So a bridge deck
    // is dry footing over a drowned bed, and so is a jetty, a wall walk or
    // any future platform: none of them needed naming here, which is the
    // point (owner, 2026-08-29: «лучше не хардкодить на будущее, а сделать
    // универсально — мост тут ни при чём»).
    //
    // The probe is how high above the plane a spawn query looks for a solid
    // to stand on: a deck clears the water by its freeboard, a wall walk by
    // storeys, and beyond this a body would be materialising onto a roof it
    // has no business on.
    constexpr float kDryFootingProbeM = 40.0f;
    inline bool is_dry_footing(float supportM) {
        return supportM > kSeaLevelM + 0.05f;
    }

    // THE height a humanoid looks and shoots from, above its feet — one number
    // serving both, on purpose. `Position.z` is the SOLE surface a body stands
    // on, so everything else about a body is measured up from it.
    //
    // This used to be a camera-only constant living in the engine, and the gap
    // it left was a real one: the aim ray started at the eye while projectiles
    // were born at `player_z()` — the FEET. You aimed along one line and fired
    // along another 1.7 m below it, so shots landed low, and a bolt fired level
    // died on the first rise of ground (spell_effects.cpp reaps a projectile
    // once `pos.z < groundM`) instead of reaching anything. Seating the muzzle
    // and the eye at the SAME height makes the crosshair honest by construction
    // — no aim-compensation fudge anywhere, at any range.
    //
    // One height for every body today (owner's call, 2026-08-05). When bodies
    // want their own — a goblin shooting lower than a troll — this becomes a
    // per-body lookup and every caller keeps working, because they all ask for
    // "the muzzle height of THIS body" rather than adding a constant themselves.
    constexpr float kBodyEyeM = 1.7f;

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
    //   - RESTING within stick range: snap to the support. This is the
    //     slope-walk / kerb-step stick, and it also lifts a fresh spawn out
    //     of the floor (arriving is not falling). A body in the air never
    //     sticks early — it lands on true contact, not 1.25 m above it;
    //   - otherwise integrate gravity, capped at terminal speed, and land on
    //     the support the moment the feet reach it.
    //
    // `resting` is the CALLER's own grounded fact (playerGrounded_, or the
    // absence of an ecs::Airborne), and it is a parameter because inferring it
    // from `vz == 0` was a real bug: gravity is po2 (g = 8, dt = 1/64, jump
    // v = 4), so a jump's velocity passes through EXACTLY 0.0f at the apex —
    // and the apex, 1 m, sits inside the 1.25 m stick. The rising body was
    // therefore declared "resting" at the top of its arc and teleported to the
    // ground: the jump lost its whole descent. No epsilon would have saved it;
    // a body knows whether it is standing, and now it says so.
    inline bool vertical_step(float supportZ, float dt, float& z, float& vz,
                              bool resting)
    {
        if (resting && z <= supportZ + kGroundStickM) {
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

    // ── THE CLEAR A GATEWAY OWES, and where a lifted span's underside sits ──
    //
    // Both live here because both are HEIGHT laws, not wall laws. Every kind
    // of wall the world raises asks them — the city's masonry arch and the
    // village's timber frame alike — and neither is derived from what the wall
    // is made of. A rider is a rider.

    // Underside of a gateway's crosspiece above the roadway: a mounted body
    // passes beneath it. kBodyEyeM is a walking man's eye; a rider sits about
    // one body-eye higher again, and the head of the gate clears him — so
    // three eye-heights is the span's clear, not a number chosen to look
    // right.
    constexpr float kGateClearM = kBodyEyeM * 3.0f;

    // Settle every LIFTED span onto the ground it actually bridges.
    //
    // A lifted solid states its clear as a height above ONE terrain sample:
    // the one under its own centre (map_data.h structure_solid_span — the
    // renderer and the collision index both resolve it that way). That is the
    // right seat only while the ground under the whole span is that height. It
    // never is on a hillside, and a gateway's crosspiece is the span it
    // matters to: promise five metres of air over the road, measure them from
    // the middle of a slope, and the uphill half of the opening is inside the
    // hill — a gate that reads from the ground as a hole with nothing over it
    // (owner, 2026-09-13; city_gate_lintel_test holds the numbers).
    //
    // THIS IS A SEPARATE PASS ON PURPOSE. The ground a gateway stands on is
    // still being cut when the wall goes up: every generator's roads are
    // smoothed into the relief afterwards (gens/dispatch.cpp
    // smooth_road_heights), so a seat computed at stamp time is measured
    // against a hill that no longer exists. Run once the map is final and
    // there is exactly one place that knows the law — which is also why it
    // does not live in either wall module: they both run too early.
    //
    // A world-levelled span (zWorld) is left alone: a bridge deck answers to
    // the water, not to the bed under it.
    inline void seat_lifted_spans(SubworldMapData& out) {
        auto ground_m = [&out](float fx, float fy) {
            const int x = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
            const int y = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
            return out.heightmap[std::size_t(y) * kCellSize + x] * kHeightScaleM;
        };
        for (Structure& s : out.structures) {
            if (s.zWorld || s.zBase <= 0.0f) continue;
            // THE SPAN RESTS ON ITS ENDS, so its clear belongs to the HIGHEST
            // ground it bridges — the uphill jamb's foot — and never to the
            // midpoint. Add the difference and the promise holds at every
            // point across the opening, on any slope.
            const float cs = std::cos(s.yaw);
            const float sn = std::sin(s.yaw);
            const float hx = structure_half_x(s);
            const float hy = structure_half_y(s);
            const float seat = ground_m(s.x, s.y);
            float rise = 0.0f;
            const int nx = std::max(2, int(hx * 2.0f));
            const int ny = std::max(2, int(hy * 2.0f));
            for (int iy = 0; iy <= ny; ++iy) {
                const float ly = -hy + 2.0f * hy * float(iy) / float(ny);
                for (int ix = 0; ix <= nx; ++ix) {
                    const float lx = -hx + 2.0f * hx * float(ix) / float(nx);
                    rise = std::max(rise,
                        ground_m(s.x + lx * cs - ly * sn,
                                 s.y + lx * sn + ly * cs) - seat);
                }
            }
            s.zBase += rise;
        }
    }

    // GLSL echoes (shaders can't include this header): mesh.vert normalises
    // vertex Y with the literal 1500.0 (= kHeightScaleM); mesh.frag's shore
    // band smoothstep(0.40, 0.47, h) starts at WATER_LEVEL; water.vert takes
    // the plane Y via push constant (fed from kHeightScaleM in
    // vk_renderer_3d.cpp). Change a constant here → update those literals.

} // namespace sm::sub
