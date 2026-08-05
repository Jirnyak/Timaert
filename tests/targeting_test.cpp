// Unit tests for sm::sub::aim_target — the pure subworld targeting primitive
// (Increment 5 possession groundwork). Builds a bare entt::registry, populates
// candidates, and pins the selection contract: nearest live enemy within the
// range AND the forward cone, excluding the player's own side and the shooter.
//
// Style mirrors tests/item_use_parity_test.cpp: no framework, plain main().

#include "ecs/world.h"
#include "sub/targeting.h"

#include <cmath>
#include <cstdio>

using namespace sm;

static int g_fails = 0;
static bool expect(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_fails; }
    return ok;
}

// A live, targetable subworld enemy at (x,y).
static entt::entity make_enemy(entt::registry& reg, float x, float y) {
    entt::entity e = reg.create();
    reg.emplace<ecs::Position>(e, x, y, 0.0f);
    reg.emplace<ecs::Health>(e, 10.0f, 10.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(4), std::uint16_t(0)); // any kind
    reg.emplace<ecs::SubworldTag>(e);
    reg.emplace<ecs::Active>(e);
    return e;
}

// cos of a half-angle in degrees, for cone tests.
static float cos_deg(float deg) {
    return std::cos(deg * 3.14159265358979f / 180.0f);
}

// yaw 0 -> forward = (cos0, sin0) = (+1, 0), i.e. facing +x.
static constexpr float kFaceX = 0.0f;
static constexpr float kFull  = -1.0f;       // 360° cone
static constexpr float kCone30 = 0;          // placeholder, set in main via cos_deg

int main() {
    const float cone30 = cos_deg(30.0f); // ±30° cone

    // ── nearest within a forward cone ─────────────────────────────────────
    {
        entt::registry reg;
        entt::entity a = make_enemy(reg, 105, 100); // ahead, dist 5
        make_enemy(reg, 110, 100);                  // ahead, dist 10 (farther)
        make_enemy(reg, 95, 100);                   // behind, dist 5 (out of cone)
        entt::entity picked = sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30);
        expect(picked == a, "picks nearest enemy inside the forward cone");
    }

    // ── range gate ────────────────────────────────────────────────────────
    {
        entt::registry reg;
        entt::entity a = make_enemy(reg, 105, 100); // dist 5
        make_enemy(reg, 110, 100);                  // dist 10
        expect(sub::aim_target(reg, 100, 100, kFaceX, 8.0f, cone30) == a,
               "range 8: near (5) selected, far (10) excluded");
        expect(sub::aim_target(reg, 100, 100, kFaceX, 3.0f, cone30) == entt::null,
               "range 3: everything out of range -> null");
    }

    // ── cone gate: a target directly behind ───────────────────────────────
    {
        entt::registry reg;
        entt::entity behind = make_enemy(reg, 95, 100); // dist 5, bearing 180°
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30) == entt::null,
               "narrow cone: target behind the shooter is not selected");
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, kFull) == behind,
               "full circle: behind target IS selected (reduces to melee)");
    }

    // ── full circle picks global nearest regardless of bearing ────────────
    {
        entt::registry reg;
        make_enemy(reg, 105, 100);                  // ahead, dist 5
        entt::entity near = make_enemy(reg, 100, 103); // side, dist 3 (nearest)
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, kFull) == near,
               "full circle: nearest overall selected irrespective of facing");
    }

    // ── player-side exclusion ─────────────────────────────────────────────
    {
        entt::registry reg;
        entt::entity a = make_enemy(reg, 106, 100);  // ahead, dist 6
        entt::entity soldier = make_enemy(reg, 101, 100); // ahead, dist 1
        reg.emplace<ecs::PlayerSoldierTag>(soldier);  // player's own -> skip
        entt::entity ptag = make_enemy(reg, 102, 100); // ahead, dist 2
        reg.emplace<ecs::PlayerTag>(ptag);            // the player -> skip
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30) == a,
               "player-side entities excluded even when nearer");
    }

    // ── dead exclusion ────────────────────────────────────────────────────
    {
        entt::registry reg;
        entt::entity a = make_enemy(reg, 106, 100); // ahead, dist 6
        entt::entity corpse = make_enemy(reg, 101, 100); // ahead, dist 1
        reg.emplace<ecs::Dead>(corpse);
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30) == a,
               "Dead entities excluded even when nearer");
        // Zero-HP but not yet tagged Dead is also excluded.
        entt::entity downed = make_enemy(reg, 102, 100);
        reg.get<ecs::Health>(downed).hp = 0.0f;
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30) == a,
               "zero-HP entities excluded even when nearer");
    }

    // ── shooter self-exclusion ────────────────────────────────────────────
    {
        entt::registry reg;
        entt::entity self = make_enemy(reg, 100, 100); // co-located "body"
        entt::entity a = make_enemy(reg, 105, 100);
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, kFull, self) == a,
               "shooter entity is never its own target");
    }

    // ── co-located enemy is reachable (no NaN bearing) ────────────────────
    {
        entt::registry reg;
        entt::entity onTop = make_enemy(reg, 100, 100); // exactly at shooter
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, cone30) == onTop,
               "co-located enemy is selected (treated as in-front)");
    }

    // ── empty world ───────────────────────────────────────────────────────
    {
        entt::registry reg;
        expect(sub::aim_target(reg, 100, 100, kFaceX, 50.0f, kFull) == entt::null,
               "no candidates -> null");
    }

    (void)kCone30;
    if (g_fails == 0) {
        std::printf("OK targeting_test: all assertions passed\n");
        return 0;
    }
    std::printf("targeting_test: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
