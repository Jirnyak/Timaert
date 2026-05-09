// Procedural sky dome — fullscreen-quad fragment shader that reconstructs
// a world-space view ray per pixel from camera yaw/pitch/fov/aspect, so the
// sky is a true sphere around the player and does NOT rotate with the
// camera framebuffer (rotating left makes the sun stay on its real bearing
// — the same result as standing in a real field). Faithful port of
// `src/game/subworld/sky.ts`: gradient + sun + procedural moons + twinkling
// stars + animated FBM clouds.
#pragma once
#include "gl/gl.h"
#include "macro/state.h"
#include "sub/camera.h"

namespace sm::sub {

struct Sky {
    GLuint prog = 0;
    GLuint vao = 0, vbo = 0;
    // Pre-baked star texture — RGB = pre-multiplied star colour, A =
    // per-star twinkle phase 0..1. Built once on CPU at init() so the
    // fragment shader does a single texture lookup per pixel instead
    // of looping over hundreds of stars (massive night-time speedup).
    GLuint starTex = 0;
    // Cached uniform locations (resolved once at init).
    GLint  uTod = -1, uElapsed = -1, uSeed = -1;
    GLint  uYaw = -1, uPitch = -1, uFov = -1, uAspect = -1, uFogColor = -1;
    GLint  uStarTex = -1;

    void init();
    void destroy();
    // `elapsed` = real seconds since subworld entered (drives cloud drift +
    // star twinkle). `seed` = world seed (per-world moon arrangement).
    // `fogR/G/B` = renderer fog colour for seamless horizon blend.
    void render(int viewW, int viewH, const WorldTime& time,
                const Camera& cam, float elapsed, std::uint32_t seed,
                float fogR, float fogG, float fogB);
};

} // namespace sm::sub

