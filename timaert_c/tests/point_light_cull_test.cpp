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

#include "sub/lighting.h"

#include <cstdio>
#include <vector>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL point_light_cull_test: %s\n", msg);
    return 1;
}

using sm::sub::GpuLight;

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

} // namespace

int main() {
    using sm::sub::cull_nearest_lights;
    const sm::vec3 cam{100.0f, 2.0f, 100.0f};

    // ── 1. count < budget → untouched (same size, same order, same contents) ──
    {
        std::vector<GpuLight> v{mk(0, 0, 0, 0), mk(50, 0, 0, 1), mk(200, 0, 0, 2)};
        const std::vector<GpuLight> before = v;
        cull_nearest_lights(v, cam, 8);
        if (v.size() != before.size()) return fail("under-budget changed size");
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].color[0] != before[i].color[0])
                return fail("under-budget reordered/altered lights");
        }
    }

    // ── 2. count == budget → still a no-op (boundary) ─────────────────────────
    {
        std::vector<GpuLight> v{mk(0, 0, 0, 0), mk(50, 0, 0, 1)};
        const std::vector<GpuLight> before = v;
        cull_nearest_lights(v, cam, 2);
        if (v.size() != 2) return fail("at-budget changed size");
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i].color[0] != before[i].color[0])
                return fail("at-budget reordered/altered lights");
    }

    // ── 3. count > budget → keep exactly the `budget` NEAREST ─────────────────
    // Place lights at increasing distance from the camera; only the closest
    // `budget` may survive. Interleave near/far so a stable-prefix bug (keeping
    // the first N in gather order) would fail.
    {
        std::vector<GpuLight> v;
        // marker == rank by distance (0 = nearest). Distances chosen strictly
        // increasing: dx = 0,10,20,...; squared 0,100,400,...
        for (int i = 0; i < 20; ++i)
            v.push_back(mk(cam.x + float(i) * 10.0f, cam.y, cam.z, float(i)));
        // Shuffle deterministically (reverse) so gather order != distance order.
        std::vector<GpuLight> shuffled;
        for (int i = 19; i >= 0; --i) shuffled.push_back(v[std::size_t(i)]);

        const std::size_t budget = 5;
        cull_nearest_lights(shuffled, cam, budget);
        if (shuffled.size() != budget) return fail("over-budget wrong survivor count");

        // The survivors must be exactly markers 0..4 (the five nearest), in any
        // order. Verify: max survivor distance < min dropped distance would be
        // ideal, but we know the exact set here, so check the marker set.
        bool seen[20] = {false};
        for (const auto& g : shuffled) {
            const int m = int(g.color[0] + 0.5f);
            if (m < 0 || m >= 20) return fail("survivor marker out of range");
            seen[m] = true;
        }
        for (int m = 0; m < int(budget); ++m)
            if (!seen[m]) return fail("a nearest light was dropped");
        for (int m = int(budget); m < 20; ++m)
            if (seen[m]) return fail("a farther light survived over a nearer one");

        // Cross-check the geometric invariant directly: every survivor is at
        // least as close as every light that could have been dropped.
        float maxSurvivor = 0.0f;
        for (const auto& g : shuffled)
            maxSurvivor = std::max(maxSurvivor, dist2(g, cam));
        // marker 5 was the nearest DROPPED light in this construction.
        const GpuLight dropped = mk(cam.x + 5.0f * 10.0f, cam.y, cam.z, 5);
        if (maxSurvivor > dist2(dropped, cam))
            return fail("a survivor is farther than a dropped light");
    }

    // ── 4. the player's light (AT the camera) always survives a crowd ─────────
    // Emulate a dense city: the player lantern at distance 0 plus many far
    // torches. The lantern must never be culled.
    {
        std::vector<GpuLight> v;
        const float kPlayerMarker = 999.0f;
        v.push_back(mk(cam.x, cam.y, cam.z, kPlayerMarker));   // player, dist 0
        for (int i = 0; i < 100; ++i)                          // 100 far torches
            v.push_back(mk(cam.x + 500.0f + float(i), cam.y, cam.z, float(i)));

        cull_nearest_lights(v, cam, sm::sub::kSubworldMaxLights);
        if (int(v.size()) != sm::sub::kSubworldMaxLights)
            return fail("crowd not culled to budget");
        bool playerKept = false;
        for (const auto& g : v)
            if (g.color[0] == kPlayerMarker) playerKept = true;
        if (!playerKept) return fail("player's own light was dropped — camera goes dark");
    }

    std::printf("OK point_light_cull_test: under/at-budget no-op, over-budget keeps "
                "the %d nearest, player light (dist 0) always survives\n",
                sm::sub::kSubworldMaxLights);
    return 0;
}
