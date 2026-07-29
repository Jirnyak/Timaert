// Carried-light spawn contract (Inc 9) — data-driven NPC torches.
//
// The subworld renderer lights ANY entity that carries an ecs::LightEmitter
// through one universal gather (src/sub/vk_renderer_3d.cpp gather_point_lights);
// the player lantern, spell bolts and now NPC torches all take that identical
// path with zero per-emitter renderer code. The remaining question a unit test
// can answer (the >32-light overflow is covered by point_light_cull_test, and
// the on-screen pool by the validated night capture) is the SPAWN-LAYER wiring:
//
//   1. A carried light is PURE DATA on the NPC type row (NpcTypeDef.light*), and
//      is strictly OPT-IN — a row that sets no light fields (all rows but Guard
//      today) has lightRadius == 0 and must spawn NO emitter.
//   2. maybe_emplace_carried_light (via the shipping spawn path
//      spawn_cell_npcs → spawn_settlement_population) attaches an ecs::LightEmitter
//      to exactly the lit types, and copies the row's radius / intensity / linear
//      RGB / height VERBATIM — so tuning a torch is a one-row data edit, and a
//      new lit type needs no code change here.
//   3. The seated height goes on the +Y (world-up) offset, matching the player
//      lantern (engine.cpp) and the gather's g.pos[1] = wy + le.offY convention,
//      so the pool sits on the body, not the feet.
//
// Pure ECS + data assertions — no Vulkan, no window. Mirrors the construction
// style of subworld_spawn_parity_test (same manager init) so it stays in lock-
// step with the shipping spawn path rather than re-deriving it.
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/npc.h"
#include "sub/seamless_manager.h"
#include "sub/spawn.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL carried_light_spawn_test: %s\n", msg);
    return 1;
}

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// A flat meadow cell context, identical in spirit to the parity test's — the
// resolver the manager calls for each of its nine window cells.
sm::sub::CellContext meadow_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.62f;
    c.biome = sm::Biome::Meadow;
    c.feature = sm::FT_None;
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.landmarkKind = sm::sub::CellLandmarkKind::None;
    c.seed = 0x24680000u
        ^ (std::uint32_t(cx) * 73856093u)
        ^ (std::uint32_t(cy) * 19349663u);
    return c;
}

// ── 1. The data contract on the type table itself ────────────────────────────
// Guard opts IN (a warm torch); every other row is dark by default. This pins
// the "strictly opt-in, one data row" property at the source, independent of any
// spawn: adding a lit type can only ever be a row edit that trips this counter.
bool run_type_table_contract() {
    const sm::NpcTypeDef& guard = sm::npc_def(sm::NPCType::Guard);
    if (!(guard.lightRadius > 0.0f)) return false;      // Guard carries a light
    if (!(guard.lightIntensity > 0.0f)) return false;
    // Warm firelight: red ≥ green ≥ blue, and not pure white (a torch, not a
    // lantern-white bulb) — the row's colour intent, not an exact tuple.
    if (!(guard.lightR >= guard.lightG && guard.lightG >= guard.lightB)) return false;
    if (!(guard.lightB < guard.lightR)) return false;
    if (!(guard.lightHeight > 0.0f)) return false;      // seated off the feet

    // Exactly the intended set is lit — every other row defaults to dark. If a
    // future row opts in, bump the expected count here on purpose (a deliberate
    // gate, never a silent drift).
    int lit = 0;
    for (std::size_t i = 0; i < std::size_t(sm::NPCType::Count); ++i) {
        if (sm::kNpcTypeDefs[i].lightRadius > 0.0f) ++lit;
    }
    if (lit != 1) return false;   // only Guard today
    return true;
}

// ── 2 + 3. The spawn wiring, through the SHIPPING path ────────────────────────
// Populate a City cell (guarantees guards) and assert every guard — and ONLY the
// guards — carries an ecs::LightEmitter equal to its type row, seated on +Y.
bool run_spawn_attach_contract(const sm::sub::SeamlessSubworldManager& mgr) {
    sm::ecs::World world{};
    // Big population so the guard count is comfortably > 0 (guards = max(2, …)).
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             sm::FT_None,
                             sm::sub::LandmarkKind::City,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xC0FFEE11u,
                             /*landmarkPop*/4000,
                             /*zoneLevel*/0);

    const sm::NpcTypeDef& guardDef = sm::npc_def(sm::NPCType::Guard);

    int guards = 0;
    int guardsLit = 0;
    int nonGuardsLit = 0;

    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        const bool isGuard = kind.type == std::uint16_t(sm::NPCType::Guard);
        const bool hasLight = world.reg.all_of<sm::ecs::LightEmitter>(e);

        if (isGuard) {
            ++guards;
            if (!hasLight) return false;   // every guard must be lit
            ++guardsLit;
            // Verbatim copy of the type row → tuning is a one-row data edit.
            const auto& le = world.reg.get<sm::ecs::LightEmitter>(e);
            if (!near(le.radius, guardDef.lightRadius)) return false;
            if (!near(le.intensity, guardDef.lightIntensity)) return false;
            if (!near(le.r, guardDef.lightR)) return false;
            if (!near(le.g, guardDef.lightG)) return false;
            if (!near(le.b, guardDef.lightB)) return false;
            // Seated on world-up (+Y), no horizontal bias — matches the player
            // lantern and the gather's g.pos[1] = wy + le.offY.
            if (!near(le.offX, 0.0f)) return false;
            if (!near(le.offY, guardDef.lightHeight)) return false;
            if (!near(le.offZ, 0.0f)) return false;
        } else {
            // Only lit types (dark by default) get an emitter. No civilian row is
            // lit today, so any emitter on a non-guard is a wiring bug.
            if (hasLight) ++nonGuardsLit;
        }
    }

    if (guards < 2) return false;          // a city fields ≥ 2 guards
    if (guardsLit != guards) return false; // all of them lit
    if (nonGuardsLit != 0) return false;   // and nobody else
    return true;
}

}  // namespace

int main() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    if (!run_type_table_contract()) {
        sm::sub::clear_saved_subworlds();
        return fail("NpcTypeDef light data contract violated "
                    "(guard unlit / not warm / wrong lit-row count)");
    }

    if (!run_spawn_attach_contract(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("carried-light attach wrong "
                    "(guard unlit / values not verbatim / offset not +Y / "
                    "civilian lit)");
    }

    std::printf("OK carried_light_spawn_test type_contract=1 spawn_attach=1\n");
    sm::sub::clear_saved_subworlds();
    return 0;
}
