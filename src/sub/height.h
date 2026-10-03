// ONE vertical-coordinate authority for the subworld.
// ----------------------------------------------------------------
// The subworld height model has exactly three layers, and every consumer
// (generation, simulation, rendering, tests) derives from the constants here:
//
//   1. NORMALISED heightmap — floats produced by `generate_heightmap`
//      (sub/base_generator.h). Sea surface sits at the SCENE'S PLANE
//      (SubworldMapData::waterLevel; `WATER_LEVEL` 0.40 only where there is no
//      world to inherit from); land occupies [plane + kLandMargin, ~1.2]
//      (soft-compressed
//      ridge peaks may exceed 1.0; hard safety clamp at 2.0).
//   2. WORLD METRES — normalised through `height_m` below, which is a CURVE
//      and not a multiplier (owner, 2026-10-01). This is the space of
//      `ecs::Position.z`, the 3D camera Y, point lights, particles and all
//      combat distance checks. `SubworldHeightField::sample(x, y)` below
//      returns the terrain surface in this space, and it is THE answer: the
//      renderer is one of its readers, never its owner.
//   3. The WATER PLANE — one sea level per SCENE, `sea_level_m(plane)`, where
//      the plane is the macroworld's own (CellContext::seaLevel) and a dungeon's
//      is 0. Rivers and seas are honest heightmap cells carved below it.
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
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sm::sub
{

    // The window and its change set — the height field's source. Declared, not
    // included: sub/height.h is pulled in by thirty-odd translation units and
    // owes none of them the manager's thread pool.
    class SeamlessSubworldManager;
    struct CompositeDirty;

    // ── THE TRANSFER CURVE: FIELD → METRES ───────────────────────────────
    //
    // This used to be one multiplier (`× 1500`), and the multiplier was the
    // reason the world was flat: a linear map gives the shore and the summit
    // the SAME metres per unit of field, so a range that occupies the top
    // third of the field can only ever be a third of the world's height.
    // Owner's verdict 2026-10-01, verbatim: «тогда может кривую высот сделать
    // не ступенями а гладко? типа для берегов будет как ща примерно плавно а
    // чем ввше высота теперь обрывистей без конкнетнных границ». A PIECEWISE
    // curve was rejected by him and by measurement — it put a ×24.8 break of
    // slope on one contour, i.e. a visible kerb ringing every massif exactly
    // where the biome border runs.
    //
    // So the gain GROWS EXPONENTIALLY with the field, and the curve is its
    // integral:
    //
    //     g(n)        = kShoreGainM · 2^((n − WATER_LEVEL)·kHeightDoublings)
    //     height_m(n) = ∫ g  = kHeightCurveM · (2^(…) − 2^(−0.4·10))
    //
    // There is no kink anywhere, INCLUDING at the water: a kink there would
    // be the one remaining "border", and it would stand on the single contour
    // the player looks at most.

    // THE ONE SHAPE KNOB: how many times the gain DOUBLES across the whole
    // field [0,1]. Ten means the roof of the field is exactly 1024× steeper
    // than the shoreline — and it is a whole number on purpose, so the sea
    // plane (0.40) lands exactly FOUR doublings above the field's floor and
    // the constant below is exact arithmetic rather than a rounded fit.
    //
    // MEASURED, not chosen: `height_census` over five seeds puts the roof of
    // the land field at 0.9490/0.9490/0.9608/0.9686/0.9961 (mean 0.9647) —
    // fBm never reaches its nominal 1.0, and a curve fitted to 1.0 would have
    // shipped a world a third as tall as the one approved. Ten doublings put
    // the summit of those five worlds at 9.5/9.5/10.3/10.9/13.3 km above the
    // sea, against the owner's target of 9–10 km («над пиком тоже хочется
    // полетать… как дракон самолёт»). Nine point six doublings hit the band's
    // middle exactly but dropped two seeds of five below 9 km; a whole number
    // that never underdelivers beats a fractional one that sometimes does.
    constexpr float kHeightDoublings = 10.0f;

    // Metres per unit of field AT THE WATER. This is TODAY'S number, kept
    // deliberately: the curve is anchored to the existing shoreline, so no
    // beach, no river mouth and no harbour moves by a metre — the whole
    // change happens above, where it was asked for.
    constexpr float kShoreGainM = 1500.0f;

    // The curve's own metre scale, g0/(N·ln2) — a consequence, not a knob.
    constexpr float kHeightCurveM = kShoreGainM / (kHeightDoublings * 0.6931472f);

    // Where the field's floor sits on the curve, in curve units: the sea plane
    // is kHeightDoublings·WATER_LEVEL = 4 doublings up, so the floor is 2^−4 =
    // 1/16. Subtracting it seats the OCEAN BED at exactly zero metres, which
    // keeps every altitude in this world positive (`z`, the camera, the flight
    // envelope) — the sea surface simply stops being 600 m up and becomes 203,
    // i.e. the ocean gets shallower. That is a consequence of anchoring the
    // gain at the shore, and it was accepted as one.
    constexpr float kFieldFloorGain = 0.0625f;
    static_assert(kHeightDoublings * WATER_LEVEL == 4.0f,
                  "the sea plane must sit a WHOLE number of doublings above "
                  "the field floor - otherwise kFieldFloorGain is a rounding");

    // THE ONE DOOR from normalised field to world metres. Every consumer —
    // generation, simulation, renderer, witnesses — comes through here; there
    // is no multiplier left to copy.
    inline float height_m(float h01) {
        return kHeightCurveM
             * (std::exp2((h01 - WATER_LEVEL) * kHeightDoublings)
                - kFieldFloorGain);
    }

    // METRES PER UNIT OF FIELD *HERE* — the curve's slope at an altitude, and
    // the second door the перепись of the old multiplier turned up.
    //
    // Under a linear map "× 1500" answered two different questions with one
    // spelling: "how high is this cell" and "how many metres is this gradient
    // worth". A curve separates them, and silently keeping the old spelling
    // for the second would have been the real defect — a gradient measured
    // with the shoreline's gain reads a 700 m mountain face as 30 m. Every
    // caller that multiplies a DIFFERENCE of field values comes here and says
    // where it is; every caller that converts a LEVEL goes to `height_m`.
    inline float height_gain_m(float h01) {
        return kShoreGainM * std::exp2((h01 - WATER_LEVEL) * kHeightDoublings);
    }

    // FIELD UNITS PER SHORE-METRE AT THIS ALTITUDE — how a detail authored in
    // the field is kept worth the same METRES wherever it lands.
    //
    // THE SUBWORLD'S OWN DETAIL IS NOT PART OF THE MACRO RELIEF, and the
    // curve must not treat it as if it were. A hummock, a dune, a bog dip —
    // `generate_heightmap` adds them to the field in amplitudes tuned when a
    // field unit was 1500 m everywhere. Put the sum on the curve unchanged
    // and a 2 m hummock on a 990 m meadow becomes a 9 m one, a 24 m one on a
    // massif: measured consequence, `subworld_generator_parity_test` — an
    // ordinary Field cell at 0.62 (q67 of the world's land) grew NO
    // ploughland at all, because every tile read steeper than the plough
    // gate. The macro relief is what the owner asked to make steep; the
    // hummocks on top of it were never the subject.
    //
    // So detail is authored in metres and divided onto the curve here. At the
    // water this is exactly 1 — the shore is untouched, as promised.
    inline float detail_field_scale(float h01) {
        return kShoreGainM / height_gain_m(h01);
    }

    // СКОЛЬКО ПОЛЯ СТОИТ ЭТОТ МЕТР ЗДЕСЬ — дверь для фактуры, которая
    // авторится В МЕТРАХ и обязана остаться этими метрами на любой высоте.
    //
    // Заведена потому, что фактуры биомов авторились В ЕДИНИЦАХ ПОЛЯ, и
    // прочесть такое число как метры не мог никто: `0.05` у болотной мочажины
    // оказалось **75 метрами**, `0.15` у бархана — **225**. Число, которое
    // нельзя прочесть в единицах мира, и есть число с потолка, даже когда у
    // него есть история. Через эту дверь модуль пишет «кочка в полшага» и это
    // видно: `kStepUpM` стоит в сигнатуре вызова, а не в комментарии.
    inline float field_delta_of_m(float metres, float h01) {
        return metres / height_gain_m(h01);
    }

    // The inverse, for the two callers that state a LEVEL in metres and need
    // the field value that carries it (a bridge deck's freeboard). Exact
    // inverse of the above, not an approximation of it.
    inline float height01_of_m(float metres) {
        return WATER_LEVEL
             + std::log2(std::max(metres / kHeightCurveM + kFieldFloorGain,
                                  1e-9f)) / kHeightDoublings;
    }

    // THE VERTICAL FULL SCALE, metres — owner's choice, 2^14. It stopped being
    // a multiplier and became a CEILING with two jobs: shaders normalise a
    // world Y by it (an exact division, which is what the power of two buys),
    // and it states how much sky the curve is allowed to use. The field's
    // nominal roof reaches 13.6 km against it, so the headroom is real and the
    // assertion below is not decorative.
    constexpr float kHeightScaleM = 16384.0f;

    // ── THE WATER PLANE IN METRES ────────────────────────────────────────
    // Owner's verdict, 2026-09-27: the datum FOLLOWS THE SCENE. It used to be
    // `constexpr kSeaLevelM = 0.40 × 1500 = 600 m`, and that constant was not a
    // harmless echo: a bridge deck is stated as `datum + freeboard`, so on a
    // world whose sea sits at 0.60 (900 m) every span would have been built
    // 300 m UNDER its own river, and `is_dry_footing` would have called a seabed
    // at 700 m dry — bodies materialising in open water, the player not
    // drowning. The plane is an editor value the player moves; a datum derived
    // from it cannot be a constant.
    inline float sea_level_m(float seaLevel01) {
        return height_m(seaLevel01);
    }

    // The DEFAULT world's datum — what a harness with no world behind it
    // answers with, and the altitude the air law below is stated against.
    // Never read by a real scene: a real scene knows its own plane. A function
    // rather than a constant because the curve is transcendental and
    // `std::exp2` is not constexpr; the relations that used to be
    // static_asserts against it now live in `air_law_test`.
    inline float default_sea_level_m() { return height_m(WATER_LEVEL); }

    // Flight ceiling margin above the loaded window's highest terrain vertex
    // (`SubworldHeightField::max_m()`).
    constexpr float kFlightMaxAboveTerrainM = 120.0f;

    // ── THE WINDOW'S HEIGHT FIELD — the world's own vertical truth ───────
    //
    // A vertex grid over the loaded 3×3 window, in metres. It used to live
    // inside the Vulkan renderer (`Renderer3DVk::heightVtxM_`) and the
    // simulation read it back out fifteen times — an obverse edge from render
    // to world, which the RENDER DEAD-END LAW forbids: what goes into the
    // renderer goes nowhere else, and a question ABOUT THE WORLD is answered
    // BY the world. It is stated here, filled from the composite heightmap the
    // window manager already owns, and the renderer is now one reader of it.
    //
    // WHY A VERTEX GRID AND NOT THE COMPOSITE ITSELF. The composite carries a
    // height per TILE (kFullSize², sub/seamless_manager.h). The surface a body
    // stands on is the DRAWN one — the box-averaged vertex grid the mesh is
    // built from — so seating bodies on raw tiles would sink them into every
    // slope the tessellation smooths away. One surface, one answer.
    //
    // Sampling step: one vertex per kHeightQuadTiles tiles. Sixteen is not a
    // taste — a macro cell (kCellSize) must span a WHOLE number of quads so a
    // cell boundary lands exactly ON a vertex; without that the per-cell
    // incremental refresh (a stitched cell, a seam shift) cannot name the
    // block it owns, and the whole window would have to be resampled on every
    // async drain. Both static_asserts below hold the property.
    constexpr int kHeightQuadTiles = 16;
    constexpr int kHeightQuads     = kFullSize / kHeightQuadTiles;  // 192
    constexpr int kHeightVerts     = kHeightQuads + 1;              // 193
    static_assert(kFullSize % kHeightQuadTiles == 0,
                  "the window must be a whole number of height quads");
    static_assert(kCellSize % kHeightQuadTiles == 0,
                  "a macro cell boundary must land on a height vertex");

    class SubworldHeightField
    {
    public:
        // THE surface of the world at composite tile coords, in metres.
        // Bilinear between the four surrounding vertices; clamped at the
        // window edge. 0 before the first refresh (no window, no ground).
        float sample(float tileX, float tileY) const;
        // Highest / lowest terrain vertex of the loaded window, metres.
        // The flight ceiling is max_m() + kFlightMaxAboveTerrainM (above);
        // the shadow volume fits itself vertically to BOTH.
        float max_m() const { return maxM_; }
        float min_m() const { return minM_; }
        bool  built() const { return built_; }
        // Raw vertices, row-major kHeightVerts² — for the consumer that walks
        // the whole grid (mesh build, normals, the march apron). A reader's
        // convenience, never a second owner: nobody writes through it.
        const float* vertices() const { return m_; }

        // Bring the field up to date with the composite. `dirty` is the
        // window manager's own change set: a full flag resamples everything, a
        // shift slides the grid toroidally and resamples only the newly
        // exposed cells, per-cell flags resample just those blocks. The three
        // paths are BYTE-IDENTICAL by construction — every one of them reaches
        // the arithmetic through the single `resample_block` below, so there is
        // no second sampling law to drift (subworld_height_field_test).
        void refresh(const SeamlessSubworldManager& mgr,
                     const CompositeDirty& dirty);
        // The FULL path on its own, over a raw composite heightmap
        // (kFullSize² normalised floats). refresh() calls exactly this when
        // the whole window must be resampled; a harness that holds a
        // heightmap but no window manager calls it directly, so a witness
        // measuring the drawn surface measures THE law instead of a copy of
        // it (mountain_mesh_smoothness_test used to keep its own).
        void rebuild_from(const float* compositeHeight);
        // Forget the window (leaving a scene). Next refresh rebuilds in full.
        void clear();
        // HARNESS ONLY: poke one vertex so a parity witness can prove its own
        // detector fires. Never called by the game.
        void debug_poke_vertex(int x, int y, float metres);

    private:
        // The ONE place the box average is computed, for every path — and ONE
        // instance of it in machine code, which is what `noinline` buys.
        //
        // MEASURED, not feared: with the body inlined at its several call
        // sites, a seam crossing left 8068 of 37249 vertices disagreeing with
        // a full resample, worst delta 0.000732 m — one float ulp at this
        // scale, and only ever on vertices an incremental path had touched.
        // The shipped TU is built with -ffast-math, so the 289-tile reduction
        // is free to reassociate, and it reassociates differently under
        // constant loop bounds (the full-grid call) than under runtime ones (a
        // cell block). Two spellings of the same sample then disagree in the
        // last bit, which is a body a hair inside the ground on one path and a
        // hair above it on the other. One instance, one answer; the cost is
        // eight calls per refresh, against a block of thousands of vertices.
        [[gnu::noinline]]
        void resample_block(const float* hm, int x0, int x1, int y0, int y1);
        // Window min/max, restated whenever the content changes.
        void recompute_extent();

        float m_[std::size_t(kHeightVerts) * kHeightVerts];
        float minM_ = 0.0f;
        float maxM_ = 0.0f;
        bool  built_ = false;
    };

    // 193² floats + the window's extent + the built flag. Pinned because the
    // size was NAMED (DOD 10): one field lives in the world, ~145.5 KiB.
    static_assert(sizeof(SubworldHeightField)
                      == sizeof(float) * std::size_t(kHeightVerts) * kHeightVerts
                       + 3 * sizeof(float),
                  "the height field is a flat grid plus its extent - nothing else");

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
    // `seaLevelM` is the SCENE's datum (sea_level_m of its plane) — passed in
    // rather than read, because "above the water" is a question about THIS
    // world's water.
    inline bool is_dry_footing(float supportM, float seaLevelM) {
        return supportM > seaLevelM + 0.05f;
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
            return height_m(out.heightmap[std::size_t(y) * kCellSize + x]);
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
    // vertex Y with the literal 16384.0 (= kHeightScaleM). THE CURVE ITSELF
    // never crosses into GLSL — vertices arrive in metres already, so there is
    // nothing for a shader to echo and no second curve to keep in step.
    // Everything ABOUT THE
    // WATER now travels as a uniform instead of being echoed as a literal —
    // mesh.frag's shore band takes the plane and the band's width in
    // `pc.shore`, water.vert takes the plane Y via push constant — because the
    // plane is a scene value and a literal cannot follow one.

} // namespace sm::sub
