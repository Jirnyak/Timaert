// Locks the contract of cull_nearest_lights (src/sub/lighting.h) — the nearest-N
// culling that decides which point lights survive when live emitters overflow
// the GPU SSBO budget (kSubworldMaxLights).
//
// This path is not reachable in-game today (a scene has only a handful of
// emitters: the player lantern + the odd spell bolt), so ONLY a unit test can
// prove it. If it regresses, a dense settlement full of torches / lit windows
// would silently drop the WRONG lights — most damagingly the player's own light,
// which must always survive because the camera rides it.
//
// The invariants pinned here:
//   * count ≤ budget → no-op: the vector is left untouched (same lights, same
//     order). This is what makes every real scene byte-identical to the
//     pre-cull renderer.
//   * count > budget → exactly `budget` lights remain, and they are precisely
//     the `budget` closest to the camera by 3D squared distance (no farther
//     light survives while a nearer one is dropped).
//   * a light sitting AT the camera (distance 0 — the player's lantern) is
//     always among the survivors, no matter how many farther lights compete.

#include "check.h"
#include "sub/lighting.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

using sm::sub::GpuLight;

const sm::vec3 kCam{100.0f, 2.0f, 100.0f};

// A light at world (x,y,z); colour/radius are irrelevant to the cull, so they
// carry a marker (the original index) in color[0] to let us identify survivors.
GpuLight mk(float x, float y, float z, float marker) {
    GpuLight g{};
    g.pos[0] = x; g.pos[1] = y; g.pos[2] = z; g.pos[3] = 10.0f;
    g.color[0] = marker; g.color[1] = 0.0f; g.color[2] = 0.0f; g.color[3] = 1.0f;
    return g;
}

float dist2(const GpuLight& g, const sm::vec3& c) {
    const float dx = g.pos[0] - c.x, dy = g.pos[1] - c.y, dz = g.pos[2] - c.z;
    return dx * dx + dy * dy + dz * dz;
}

// ── count < budget → untouched (same size, same order, same contents) ──────
void test_under_budget_is_a_no_op() {
    using sm::sub::cull_nearest_lights;
    std::vector<GpuLight> v{mk(0, 0, 0, 0), mk(50, 0, 0, 1), mk(200, 0, 0, 2)};
    const std::vector<GpuLight> before = v;
    cull_nearest_lights(v, kCam, 8);
    CHECK_OR_RETURN(v.size() == before.size(),
                    "a scene under the budget keeps every light it had");
    int compared = 0, moved = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        ++compared;
        if (v[i].color[0] != before[i].color[0]) ++moved;
    }
    CHECK(compared == int(before.size()), "every light was actually compared");
    CHECK(moved == 0,
          "and they are left in the SAME ORDER — an under-budget scene is "
          "byte-identical to the pre-cull renderer");
}

// ── count == budget → still a no-op (the boundary) ─────────────────────────
void test_exactly_at_budget_is_a_no_op_too() {
    using sm::sub::cull_nearest_lights;
    std::vector<GpuLight> v{mk(0, 0, 0, 0), mk(50, 0, 0, 1)};
    const std::vector<GpuLight> before = v;
    cull_nearest_lights(v, kCam, 2);
    CHECK_OR_RETURN(v.size() == 2,
                    "a scene exactly at the budget loses nothing");
    int compared = 0, moved = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        ++compared;
        if (v[i].color[0] != before[i].color[0]) ++moved;
    }
    CHECK(compared == 2, "both lights were actually compared");
    CHECK(moved == 0, "the boundary case does not reorder either");
}

// ── count > budget → keep exactly the `budget` NEAREST ─────────────────────
// Lights at strictly increasing distance, handed in REVERSED so gather order is
// the opposite of distance order: a cull that merely keeps the first N would
// keep the farthest five and fail every claim below.
void test_over_budget_keeps_the_nearest_and_only_those() {
    using sm::sub::cull_nearest_lights;
    constexpr int kLights = 20;
    const std::size_t budget = 5;

    std::vector<GpuLight> v;
    // marker == rank by distance (0 = nearest). Distances strictly increasing:
    // dx = 0,10,20,...; squared 0,100,400,...
    for (int i = 0; i < kLights; ++i)
        v.push_back(mk(kCam.x + float(i) * 10.0f, kCam.y, kCam.z, float(i)));
    std::vector<GpuLight> shuffled;
    for (int i = kLights - 1; i >= 0; --i) shuffled.push_back(v[std::size_t(i)]);

    cull_nearest_lights(shuffled, kCam, budget);
    CHECK_OR_RETURN(shuffled.size() == budget,
                    "an over-budget scene is cut to EXACTLY the budget");

    bool seen[kLights] = {false};
    int inspected = 0, stranger = 0;
    for (const auto& g : shuffled) {
        ++inspected;
        const int m = int(g.color[0] + 0.5f);
        if (m < 0 || m >= kLights) { ++stranger; continue; }
        seen[m] = true;
    }
    CHECK(inspected == int(budget), "every survivor was inspected");
    CHECK(stranger == 0,
          "every survivor is one of the lights the caller handed in");

    int nearestDropped = 0;
    for (int m = 0; m < int(budget); ++m) if (!seen[m]) ++nearestDropped;
    CHECK(nearestDropped == 0,
          "not one of the nearest lights was dropped");
    int fartherKept = 0;
    for (int m = int(budget); m < kLights; ++m) if (seen[m]) ++fartherKept;
    CHECK(fartherKept == 0,
          "no farther light survived while a nearer one was cut — the cull "
          "sorts by DISTANCE, not by gather order");

    // The same law stated geometrically, independent of the marker bookkeeping.
    float maxSurvivor = 0.0f;
    for (const auto& g : shuffled)
        maxSurvivor = std::max(maxSurvivor, dist2(g, kCam));
    const GpuLight nearestDropped5 =
        mk(kCam.x + 5.0f * 10.0f, kCam.y, kCam.z, 5);   // the nearest cut light
    CHECK(maxSurvivor <= dist2(nearestDropped5, kCam),
          "the farthest SURVIVOR is still no farther than the nearest light "
          "that was cut");
}

// ── the player's light (AT the camera) always survives a crowd ─────────────
// A dense city: the player lantern at distance 0 plus a hundred far torches.
void test_the_players_own_light_is_never_culled() {
    using sm::sub::cull_nearest_lights;
    const float kPlayerMarker = 999.0f;
    std::vector<GpuLight> v;
    v.push_back(mk(kCam.x, kCam.y, kCam.z, kPlayerMarker));   // player, dist 0
    for (int i = 0; i < 100; ++i)                             // 100 far torches
        v.push_back(mk(kCam.x + 500.0f + float(i), kCam.y, kCam.z, float(i)));

    cull_nearest_lights(v, kCam, sm::sub::kSubworldMaxLights);
    CHECK(int(v.size()) == sm::sub::kSubworldMaxLights,
          "a crowd of emitters is cut down to the GPU budget");
    int playerKept = 0;
    for (const auto& g : v) if (g.color[0] == kPlayerMarker) ++playerKept;
    CHECK(playerKept == 1,
          "the player's own light survives any crowd — the camera rides it, "
          "and dropping it puts the player in the dark");
}

} // namespace

int main() {
    test_under_budget_is_a_no_op();
    test_exactly_at_budget_is_a_no_op_too();
    test_over_budget_keeps_the_nearest_and_only_those();
    test_the_players_own_light_is_never_culled();
    return sm::test::report("point_light_cull_test");
}
