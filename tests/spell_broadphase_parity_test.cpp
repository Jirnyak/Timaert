// The spell broad phase is a STRUCTURAL move, and this test is its proof of
// identity: for the same scene and the same projectile, the grid-backed
// candidate enumeration must produce EXACTLY the hits of the full O(N·M)
// registry scan it replaced. The full scan is kept alive in the shipping code
// as the nullptr / -1 arm of for_each_spell_candidate, so the reference here
// is the real code path, not a copy.
//
// Four scenarios, one per scan that was replaced: the swept bolt (plus its
// muzzle stretch), the blast sphere, the beam, and the chain. Each runs on
// four identical worlds:
//   • Full      — no broad phase (the reference),
//   • Grid      — the bucket-grid callback, mirroring the engine's
//                 spell_neighbors_callback over battlePick_,
//   • Fallback  — a callback that always answers -1 (must equal Full: the
//                 "cannot promise completeness" arm is a fallback, not a loss),
//   • Lossy     — a callback that returns zero candidates. This one MUST
//                 DIFFER: it is the negative control that proves the parity
//                 assertions can catch a broad phase that loses bodies.
#include "check.h"

#include "sub/spell_effects.h"
#include "sub/battle.h"
#include "sub/body.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using sm::ecs::Position;
using sm::ecs::Health;
using sm::ecs::Projectile;
using sm::ecs::SubworldTag;

constexpr float kBodyHp = 100.0f;

entt::entity add_body(entt::registry& reg, float x, float y, float z) {
    const auto e = reg.create();
    reg.emplace<Position>(e, x, y, z);
    reg.emplace<Health>(e, kBodyHp, kBodyHp);
    reg.emplace<SubworldTag>(e);
    return e;
}

// One deterministic scene per scenario: the acting bodies first, then a
// pseudo-random crowd far from the action so the broad phase has something
// real to exclude. The LCG is the test's own — no world seed, no clock.
void build_scene(entt::registry& reg, int scenario) {
    switch (scenario) {
    case 0:                                   // swept bolt + muzzle stretch
        add_body(reg, 96.0f, 100.0f, 0.5f);   // caster (owner of the bolt)
        add_body(reg, 120.0f, 100.5f, 1.0f);  // first victim on the path
        add_body(reg, 140.0f, 99.0f, 1.2f);   // behind the first — must survive
        add_body(reg, 120.0f, 108.0f, 1.0f);  // near the path, out of reach
        break;
    case 1:                                   // blast
        for (int i = 0; i < 8; ++i) {         // inside the 12-unit sphere
            const float a = float(i) * 0.7853982f;
            add_body(reg, 500.0f + std::cos(a) * 8.0f,
                     500.0f + std::sin(a) * 8.0f, 1.0f + float(i % 3));
        }
        // Inside the 2D circle, OUTSIDE the 3D sphere: the broad phase must
        // offer it and the exact check must refuse it.
        add_body(reg, 511.0f, 500.0f, 8.0f);
        add_body(reg, 516.0f, 500.0f, 1.0f);  // outside both
        break;
    case 2:                                   // beam
        add_body(reg, 615.0f, 600.5f, 4.0f);  // on the beam line
        add_body(reg, 610.0f, 606.0f, 4.0f);  // near, off the line
        add_body(reg, 650.0f, 600.0f, 4.0f);  // past the beam's end
        break;
    case 3:                                   // chain
        add_body(reg, 815.0f, 800.0f, 1.0f);  // struck by the bolt
        add_body(reg, 824.0f, 803.0f, 1.0f);  // hop 1 (9.5 away)
        add_body(reg, 836.0f, 801.0f, 1.0f);  // hop 2 (12.2 further)
        add_body(reg, 860.0f, 820.0f, 1.0f);  // beyond chainRadius — safe
        break;
    }
    std::uint32_t lcg = 0xC0FFEEu;
    for (int i = 0; i < 150; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        const float x = 1000.0f + float(lcg >> 20);          // 1000..5095
        lcg = lcg * 1664525u + 1013904223u;
        const float y = 1000.0f + float(lcg >> 20);
        add_body(reg, x, y, 1.0f);
    }
}

std::vector<entt::entity> bodies_in_creation_order(entt::registry& reg) {
    std::vector<entt::entity> v;
    auto view = reg.view<Position, Health>();
    for (auto e : view) v.push_back(e);
    // EnTT iterates newest-first; reverse for stable creation order.
    std::vector<entt::entity> r(v.rbegin(), v.rend());
    return r;
}

// ── The grid broad phase under test — the engine callback's twin ───────────
// Same walk, same padding law (fattest body radius; the scene is static so
// the per-tick-drift term is zero), same -1 overflow honesty.
struct GridBroadPhase {
    sm::sub::BattleUnits units;
    sm::sub::UnitGrid pick;
    std::vector<entt::entity> ents;
    float pad = 0.0f;

    void build(entt::registry& reg) {
        units.clear();
        ents.clear();
        auto view = reg.view<Position, Health>(entt::exclude<sm::ecs::Dead>);
        for (auto e : view) {
            const auto& p = view.get<Position>(e);
            sm::sub::BattleUnitDesc d{};
            d.x = p.x; d.y = p.y; d.z = p.z;
            d.radius = sm::sub::body_radius(reg, e);
            units.add(d);
            ents.push_back(e);
        }
        const sm::sub::BattleParams prm{};
        build_unit_grid(pick, units, pick_cell_for(units, prm), 256);
        pad = units.maxRadius;
    }

    static int fn(void* user, float x, float y, float r,
                  std::uint32_t* out, int maxOut) {
        auto* self = static_cast<GridBroadPhase*>(user);
        const auto& u = self->units;
        const auto& g = self->pick;
        if (u.count <= 0) return 0;
        const float rr = r + self->pad;
        const float rr2 = rr * rr;
        const int c0 = g.col_of(x - rr), c1 = g.col_of(x + rr);
        const int r0 = g.row_of(y - rr), r1 = g.row_of(y + rr);
        int n = 0;
        for (int cy = r0; cy <= r1; ++cy) {
            for (int cx = c0; cx <= c1; ++cx) {
                const std::size_t ci = std::size_t(cy) * std::size_t(g.cols)
                                     + std::size_t(cx);
                for (std::uint32_t k = g.begin[ci]; k < g.begin[ci + 1u]; ++k) {
                    const std::size_t sj = std::size_t(g.items[k]);
                    const float dx = u.x[sj] - x, dy = u.y[sj] - y;
                    if (dx * dx + dy * dy > rr2) continue;
                    if (n >= maxOut) return -1;
                    out[n++] = std::uint32_t(
                        entt::to_integral(self->ents[sj]));
                }
            }
        }
        return n;
    }

    static int fallback_fn(void*, float, float, float, std::uint32_t*, int) {
        return -1;
    }
    static int lossy_fn(void*, float, float, float, std::uint32_t*, int) {
        return 0;                             // loses every body — must be caught
    }
};

enum class Mode { Full, Grid, Fallback, Lossy };

// Build the scene, fire the scenario's projectile, tick, and report every
// body's hp in creation order.
std::vector<float> run_scenario(int scenario, Mode mode) {
    sm::ecs::World w{};
    auto& reg = w.reg;
    build_scene(reg, scenario);
    const auto bodies = bodies_in_creation_order(reg);

    const float dt = 1.0f / 64.0f;
    int ticks = 1;
    const auto pe = reg.create();
    switch (scenario) {
    case 0: {
        const std::uint32_t owner =
            std::uint32_t(entt::to_integral(bodies[0]));
        reg.emplace<Position>(pe, 98.5f, 100.0f, 1.0f);
        reg.emplace<Projectile>(pe,
            400.0f, 0.0f, 0.0f,               // vx, vy, vz
            1.5f,                             // radius
            1.0f, 1.0f,                       // lifeTimer, maxLifeTimer
            13.0f, 0.0f,                      // damage, blastRadius
            98.5f, 100.0f,                    // originX, originY
            0.0f, 0.0f, 0.0f,                 // beamLength, chainDecay, chainRadius
            1u, owner,
            std::int16_t{0}, Projectile::Bolt,
            false, false, false);
        ticks = 10;
        break;
    }
    case 1:
        reg.emplace<Position>(pe, 500.0f, 500.0f, 1.0f);
        reg.emplace<Projectile>(pe,
            0.0f, 0.0f, 0.0f, 1.5f,
            0.005f, 1.0f,                     // expires on the first tick
            20.0f, 12.0f,
            500.0f, 500.0f,
            0.0f, 0.0f, 0.0f,
            2u, std::uint32_t(0xFFFFFFFFu),
            std::int16_t{0}, Projectile::Bolt,
            false, false, true);              // explodeOnExpiry
        break;
    case 2: {
        // Direction (10, 0, 2) normalised inside; entity sits at the beam
        // midpoint, origin Z therefore 1.0 (see apply_spell_beam).
        const float len3 = std::sqrt(10.0f * 10.0f + 2.0f * 2.0f);
        const float nx = 10.0f / len3, nz = 2.0f / len3;
        reg.emplace<Position>(pe, 600.0f + nx * 20.0f, 600.0f,
                              1.0f + nz * 20.0f);
        reg.emplace<Projectile>(pe,
            10.0f, 0.0f, 2.0f, 1.2f,
            0.005f, 1.0f,
            15.0f, 0.0f,
            600.0f, 600.0f,
            40.0f, 0.0f, 0.0f,                // beamLength
            3u, std::uint32_t(0xFFFFFFFFu),
            std::int16_t{0}, Projectile::Beam,
            false, false, false);
        break;
    }
    case 3:
        reg.emplace<Position>(pe, 800.0f, 800.0f, 1.0f);
        reg.emplace<Projectile>(pe,
            400.0f, 0.0f, 0.0f, 1.5f,
            1.0f, 1.0f,
            16.0f, 0.0f,
            800.0f, 800.0f,
            0.0f, 0.5f, 15.0f,                // chainDecay, chainRadius
            4u, std::uint32_t(0xFFFFFFFFu),
            std::int16_t{3}, Projectile::Bolt, // chainRemaining
            false, false, false);
        ticks = 10;
        break;
    }
    reg.emplace<SubworldTag>(pe);

    GridBroadPhase grid{};
    grid.build(reg);

    sm::sub::SpellNeighborsFn fn = nullptr;
    void* user = nullptr;
    switch (mode) {
    case Mode::Full: break;
    case Mode::Grid:     fn = &GridBroadPhase::fn; user = &grid; break;
    case Mode::Fallback: fn = &GridBroadPhase::fallback_fn; break;
    case Mode::Lossy:    fn = &GridBroadPhase::lossy_fn; break;
    }

    for (int t = 0; t < ticks; ++t) {
        sm::sub::tick_spell_projectiles(w, nullptr, dt,
                                        nullptr, nullptr,   // log
                                        nullptr, nullptr,   // canHit
                                        nullptr, nullptr,   // fx
                                        nullptr, nullptr,   // height
                                        nullptr, nullptr,   // solid
                                        fn, user);
    }

    std::vector<float> hp;
    hp.reserve(bodies.size());
    for (auto e : bodies) hp.push_back(reg.get<Health>(e).hp);
    return hp;
}

const char* scenario_name(int s) {
    switch (s) {
    case 0: return "sweep";
    case 1: return "blast";
    case 2: return "beam";
    default: return "chain";
    }
}

void test_scenario(int s) {
    const auto full = run_scenario(s, Mode::Full);
    const auto grid = run_scenario(s, Mode::Grid);
    const auto fall = run_scenario(s, Mode::Fallback);
    const auto lossy = run_scenario(s, Mode::Lossy);

    int damaged = 0;
    for (float h : full) {
        if (h < kBodyHp) ++damaged;
    }
    CHECK(damaged > 0, "the scenario draws blood (else parity proves nothing)");

    CHECK(full.size() == grid.size() && full.size() == fall.size(),
          "same bodies in every world");
    bool gridSame = true, fallSame = true, lossySame = true;
    for (std::size_t i = 0; i < full.size(); ++i) {
        if (full[i] != grid[i]) gridSame = false;
        if (full[i] != fall[i]) fallSame = false;
        if (full[i] != lossy[i]) lossySame = false;
    }
    std::fprintf(stderr,
                 "[spell_broadphase] %s: %d damaged, grid %s, fallback %s\n",
                 scenario_name(s), damaged,
                 gridSame ? "==" : "!=", fallSame ? "==" : "!=");
    CHECK(gridSame, "grid broad phase hits EXACTLY what the full scan hits");
    CHECK(fallSame, "the -1 arm falls back to the full scan losslessly");
    // Negative control: a broad phase that loses bodies must be VISIBLE to
    // the comparisons above, or they guard nothing.
    CHECK(!lossySame, "a lossy broad phase must change outcomes (control)");
}

// The blast scene plants a body inside the 2D query circle but outside the
// 3D blast sphere — assert the exact phase, not the broad phase, decides.
void test_superset_is_filtered() {
    const auto full = run_scenario(1, Mode::Grid);
    // Creation order: 8 victims, then the 2D-in/3D-out body, then the safe one.
    CHECK(full[8] == kBodyHp,
          "a candidate the exact 3D check refuses is not damaged");
    CHECK(full[9] == kBodyHp, "a body outside both circles is untouched");
    for (int i = 0; i < 8; ++i) {
        CHECK(full[std::size_t(i)] < kBodyHp, "every body in the sphere bled");
    }
}

} // namespace

int main() {
    for (int s = 0; s < 4; ++s) test_scenario(s);
    test_superset_is_filtered();
    return sm::test::report("spell_broadphase_parity_test");
}
