// Unit test for the universal macroworld night-lighting bake (macro_lighting.*).
//
// Pure CPU, no GPU: exercises the two halves of the system independently —
//   collect_macro_lights  the data-driven emitter census (settlements, villages,
//                          active spires; depleted spires and opt-out types emit
//                          nothing) with population-scaled radius/intensity.
//   bake_light_field       the radial glow rasteriser: falloff, radial symmetry,
//                          torus wrap, colour fidelity, and clamp/encode.
//
// Increment A covers the isotropic radial bake. When increment B lands
// (terrain-occluded propagation: open land carries light far, forest attenuates,
// mountain blocks) this file gains the occlusion cases.
#include "macro/macro_lighting.h"
#include "macro/features.h"
#include "macro/landmark_registry.h"
#include "macro/state.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_fail = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL macro_lighting_test: %s\n", msg);
        ++g_fail;
    }
}

// One RGBA8 channel of the baked field at cell (x, y).
int chan(const std::vector<std::uint8_t>& f, int w, int x, int y, int c) {
    return int(f[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4u
                 + std::size_t(c)]);
}

} // namespace

int main() {
    using namespace sm;

    // ---- bake_light_field: empty input is a valid all-zero opaque field ----
    {
        std::vector<std::uint8_t> f;
        bake_light_field(8, 8, {}, f);
        check(f.size() == 8u * 8u * 4u, "empty bake sizes to w*h*4");
        bool zeroRGB = true, opaque = true;
        for (int i = 0; i < 8 * 8; ++i) {
            if (f[i * 4 + 0] || f[i * 4 + 1] || f[i * 4 + 2]) zeroRGB = false;
            if (f[i * 4 + 3] != 255) opaque = false;
        }
        check(zeroRGB, "empty bake has zero glow");
        check(opaque, "empty bake alpha is 255");
    }

    // ---- bake_light_field: single central light, monotonic radial falloff ----
    {
        const int W = 32, H = 32;
        MacroLight L{};
        L.nx = (16.0f + 0.5f) / W;
        L.ny = (16.0f + 0.5f) / H;
        L.radius = 6.0f;
        L.intensity = 1.0f;
        L.r = L.g = L.b = 1.0f;
        std::vector<std::uint8_t> f;
        bake_light_field(W, H, {L}, f);
        const int core   = chan(f, W, 16, 16, 0);
        const int near2  = chan(f, W, 18, 16, 0);  // 2 cells away
        const int far5   = chan(f, W, 21, 16, 0);  // 5 cells away (< radius 6)
        const int beyond = chan(f, W, 24, 16, 0);  // 8 cells away (> radius)
        check(core > 0, "central light glows at core");
        check(core > near2, "glow decreases with distance (core > near)");
        check(near2 > far5, "glow decreases with distance (near > far)");
        check(beyond == 0, "glow is zero beyond radius");
        check(chan(f, W, 14, 16, 0) == chan(f, W, 18, 16, 0),
              "radial symmetry left/right");
        check(chan(f, W, 16, 14, 0) == chan(f, W, 16, 18, 0),
              "radial symmetry up/down");
    }

    // ---- bake_light_field: torus wrap (world grid is cyclic) ----
    {
        const int W = 16, H = 16;
        MacroLight L{};
        L.nx = 0.5f / W;  // cell (0,0)
        L.ny = 0.5f / H;
        L.radius = 3.0f;
        L.intensity = 1.0f;
        L.r = L.g = L.b = 1.0f;
        std::vector<std::uint8_t> f;
        bake_light_field(W, H, {L}, f);
        check(chan(f, W, 0, 0, 0) > 0, "wrap: core glows");
        check(chan(f, W, W - 1, 0, 0) > 0, "wrap: x=0 light reaches x=W-1");
        check(chan(f, W, 0, H - 1, 0) > 0, "wrap: y=0 light reaches y=H-1");
    }

    // ---- bake_light_field: colour fidelity + per-channel clamp/encode ----
    {
        const int W = 8, H = 8;
        MacroLight red{};
        red.nx = (4.0f + 0.5f) / W;
        red.ny = (4.0f + 0.5f) / H;
        red.radius = 3.0f;
        red.intensity = 1.0f;
        red.r = 1.0f; red.g = 0.0f; red.b = 0.0f;
        std::vector<std::uint8_t> f;
        bake_light_field(W, H, {red}, f);
        check(chan(f, W, 4, 4, 0) > 0, "red light: R core > 0");
        check(chan(f, W, 4, 4, 1) == 0, "red light: G core == 0");
        check(chan(f, W, 4, 4, 2) == 0, "red light: B core == 0");

        // Master gain tames base brightness: a single max-intensity light has
        // core acc == 1.0, so its encoded byte is exactly the gain mapped onto
        // the [0, ceil] encode. Locks the brightness knob against regressions
        // (e.g. an accidental return to the pre-tuning full-strength bake).
        {
            const float gClamped =
                kMacroGlowGain < kMacroGlowCeil ? kMacroGlowGain : kMacroGlowCeil;
            const int expected = int(gClamped * (255.0f / kMacroGlowCeil) + 0.5f);
            check(chan(f, W, 4, 4, 0) == expected,
                  "gain: single max light core encodes to the gain level");
            check(expected < 128,
                  "gain: a lone city core stays well below saturation");
        }

        // Stack many full-white intense lights on one cell: the summed glow far
        // exceeds the ceiling, so every channel must clamp to 255 (not overflow).
        std::vector<MacroLight> many;
        for (int i = 0; i < 16; ++i) {
            MacroLight m = red;
            m.g = 1.0f; m.b = 1.0f;
            many.push_back(m);
        }
        bake_light_field(W, H, many, f);
        check(chan(f, W, 4, 4, 0) == 255, "clamp: saturated channel encodes to 255");
        check(chan(f, W, 4, 4, 3) == 255, "alpha stays 255 under saturation");
    }

    // ---- collect_macro_lights: census, opt-out, tint, population scaling ----
    {
        GameState gs;
        gs.mapW = 256;
        gs.mapH = 256;

        Settlement big{};
        big.id = 1; big.x = 10; big.y = 10; big.population = 5000;
        Settlement small{};
        small.id = 2; small.x = 20; small.y = 20; small.population = 20;
        gs.settlements.push_back(big);
        gs.settlements.push_back(small);

        Village v{};
        v.id = 1; v.x = 30; v.y = 30; v.population = 100;
        gs.villages.push_back(v);

        Spire active{};
        active.id = 1; active.x = 40; active.y = 40; active.depleted = false;
        Spire spent{};
        spent.id = 2; spent.x = 50; spent.y = 50; spent.depleted = true;
        gs.spires.push_back(active);
        gs.spires.push_back(spent);

        std::vector<MacroLight> lights = collect_macro_lights(gs);
        // 2 settlements + 1 village + 1 active spire; the depleted spire emits none.
        check(lights.size() == 4u, "census: 4 emitters (depleted spire excluded)");

        auto by_cell = [&](int cx, int cy) -> const MacroLight* {
            const float ex = (float(cx) + 0.5f) / 256.0f;
            const float ey = (float(cy) + 0.5f) / 256.0f;
            for (const MacroLight& L : lights)
                if (std::fabs(L.nx - ex) < 1e-4f && std::fabs(L.ny - ey) < 1e-4f)
                    return &L;
            return nullptr;
        };
        const MacroLight* lb = by_cell(10, 10);      // big settlement
        const MacroLight* ls = by_cell(20, 20);      // small settlement
        const MacroLight* lp = by_cell(40, 40);      // active spire
        const MacroLight* ldep = by_cell(50, 50);    // depleted spire (absent)
        check(lb && ls && lp, "settlement/spire lights present at their cells");
        check(ldep == nullptr, "depleted spire produced no light");

        if (lb && ls) {
            check(lb->radius > ls->radius, "bigger population -> bigger radius");
            check(lb->intensity >= ls->intensity,
                  "bigger population -> >= intensity");
        }
        // City warm hearth tint 0xFFFFC76B.
        if (lb) {
            check(std::fabs(lb->r - 1.0f) < 0.01f, "city tint R == 1.0");
            check(std::fabs(lb->g - float(0xC7) / 255.0f) < 0.01f, "city tint G");
            check(std::fabs(lb->b - float(0x6B) / 255.0f) < 0.01f, "city tint B");
        }
        // Spire arcane tint 0xFFA86CFF (bluish: B == 1.0, R < B).
        if (lp) {
            check(std::fabs(lp->b - 1.0f) < 0.01f, "spire tint B == 1.0");
            check(lp->r < lp->b, "spire tint is bluish (R < B)");
        }
    }

    // ================= Increment B: terrain-occluded propagation =============
    // Passing a FeatureLayer switches bake_light_field from Euclidean falloff to
    // a bounded Dijkstra over optical distance: open land carries light far,
    // forest dims it, mountains block it, roads extend it.

    // ---- open feature layer degrades gracefully to radial falloff ----
    {
        const int W = 24, H = 24;
        FeatureLayer feat;
        feat.resize(W, H);  // all FT_None
        MacroLight L{};
        L.nx = (12.0f + 0.5f) / W;
        L.ny = (12.0f + 0.5f) / H;
        L.radius = 6.0f;
        L.intensity = 1.0f;
        L.r = L.g = L.b = 1.0f;
        std::vector<std::uint8_t> f;
        bake_light_field(W, H, {L}, f, &feat);
        // Probe on-axis, where octile optical distance equals Euclidean exactly.
        check(chan(f, W, 12, 12, 0) > 0, "occluded/open: core glows");
        check(chan(f, W, 12, 12, 0) > chan(f, W, 12, 14, 0),
              "occluded/open: falls off (core > near)");
        check(chan(f, W, 12, 14, 0) > chan(f, W, 12, 17, 0),
              "occluded/open: falls off (near > far)");
        check(chan(f, W, 12, 20, 0) == 0, "occluded/open: zero beyond radius");
    }

    // ---- forest dims light: edge transmits, interior darkens ----
    {
        const int W = 40, H = 40;
        MacroLight L{};
        L.nx = (20.0f + 0.5f) / W;
        L.ny = (20.0f + 0.5f) / H;
        L.radius = 10.0f;
        L.intensity = 1.0f;
        L.r = L.g = L.b = 1.0f;

        FeatureLayer open;
        open.resize(W, H);
        FeatureLayer trees;
        trees.resize(W, H);
        // A solid forest block east of the emitter, so a probe inside it cannot
        // be reached by an open detour — the light must pass through canopy.
        for (int x = 21; x <= 27; ++x)
            for (int y = 17; y <= 23; ++y)
                trees.set(x, y, FT_Tree);

        std::vector<std::uint8_t> fo, ft;
        bake_light_field(W, H, {L}, fo, &open);
        bake_light_field(W, H, {L}, ft, &trees);
        const int openNear = chan(fo, W, 23, 20, 0);  // 3 east, open
        const int treeNear = chan(ft, W, 23, 20, 0);  // 3 east, forest edge
        const int treeDeep = chan(ft, W, 25, 20, 0);  // 5 east, forest interior
        check(openNear > 0, "forest: probe lit over open ground");
        check(treeNear > 0, "forest: canopy edge still transmits some light");
        check(treeNear < openNear, "forest: canopy dims the same probe");
        check(treeDeep == 0, "forest: deep interior goes dark");
    }

    // (Mountains were tested here as a hard FT_Mountain wall. They are now a
    // biome, not a feature — biomes.h Biome::Mountain, classified by elevation —
    // so they never enter the feature grid the bake reads. The solid forest
    // block above is the feature-grid occluder proof; occluding a bare massif is
    // a later elevation-sampling pass. Single-cell hard blocking was intrinsic to
    // the mountain's near-wall cost and has no feature-grid analogue now.)

    // ---- roads carry light further than open ground ----
    {
        const int W = 40, H = 40;
        FeatureLayer feat;
        feat.resize(W, H);
        for (int x = 21; x <= 32; ++x)
            feat.set(x, 20, FT_Road);  // a road running east
        MacroLight L{};
        L.nx = (20.0f + 0.5f) / W;
        L.ny = (20.0f + 0.5f) / H;
        L.radius = 8.0f;
        L.intensity = 1.0f;
        L.r = L.g = L.b = 1.0f;
        std::vector<std::uint8_t> f;
        bake_light_field(W, H, {L}, f, &feat);
        const int roadProbe = chan(f, W, 27, 20, 0);  // 7 east along the road
        const int openProbe = chan(f, W, 20, 27, 0);  // 7 south over open ground
        check(roadProbe > 0 && openProbe > 0, "road: both probes lit");
        check(roadProbe > openProbe,
              "road: light travels further along a road than over open ground");
    }

    if (g_fail == 0)
        std::fprintf(stderr, "macro_lighting_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
