#include "sub/sky.h"
#include "gl/helpers.h"
#include "sub/lighting.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {

// CPU bake of the equirect star texture. Same Fibonacci-lattice +
// per-index hash jitter the shader used to compute per-pixel — just
// done once into a 2048x1024 RGBA8 texture so the render path becomes a
// single texture fetch. Splat is a 3x3 Gaussian footprint so stars look
// like soft pinpricks at any view zoom; A channel carries per-star
// twinkle phase.
static void bake_star_texture(GLuint tex) {
    constexpr int   kW          = 4096;
    constexpr int   kH          = 2048;
    constexpr int   kStarCount  = 700;
    constexpr float kGolden     = 2.39996323f;  // 2π / φ²
    constexpr float kPI         = 3.14159265358979f;
    constexpr float kTwoPi      = 6.28318530717958f;

    auto h11 = [](float p) -> float {
        p = p * 0.1031f;
        p -= std::floor(p);
        p *= p + 33.33f;
        p *= p + p;
        p -= std::floor(p);
        return p;
    };

    std::vector<std::uint8_t> px(std::size_t(kW) * kH * 4, 0);

    for (int i = 0; i < kStarCount; ++i) {
        const float fi = float(i) + 0.5f;
        float z   = 1.0f - 2.0f * fi / float(kStarCount);
        float r   = std::sqrt(std::max(0.0f, 1.0f - z * z));
        float th  = fi * kGolden;
        float dx  = r * std::cos(th);
        float dy  = z;
        float dz  = r * std::sin(th);
        const float h0 = h11(fi * 17.31f);
        const float h1 = h11(fi * 91.17f);
        const float h2 = h11(fi * 53.93f);
        dx += (h0 - 0.5f) * 0.18f;
        dy += (h1 - 0.5f) * 0.18f;
        dz += (h2 - 0.5f) * 0.18f;
        const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (len < 1e-5f) continue;
        dx /= len; dy /= len; dz /= len;
        if (dy < -0.05f) continue;

        const float mag    = h11(fi * 7.11f);
        // Pareto-ish: most stars dim, a handful bright. Bumped back up
        // — the previous tightening made stars almost invisible at
        // night while the per-pixel splat was already small enough.
        const float bright = 0.40f + mag * mag * 1.10f;
        const float twPh   = h11(fi * 13.7f);
        const float cm     = h11(fi * 3.7f);
        const float cR = 0.85f * (1.0f - cm) + 1.00f * cm;
        const float cG = 0.90f * (1.0f - cm) + 0.92f * cm;
        const float cB = 1.00f * (1.0f - cm) + 0.78f * cm;

        const float az = std::atan2(dz, dx);
        const float el = std::asin(std::clamp(dy, -1.0f, 1.0f));
        const float u  = az / kTwoPi + 0.5f;
        const float v  = el / kPI    + 0.5f;
        const float fx = u * float(kW);
        const float fy = v * float(kH);
        const int   cx = int(std::floor(fx));
        const int   cy = int(std::floor(fy));

        // Single bright pixel + 1-pixel ring at half intensity. LINEAR
        // texture filter spreads each star into a soft 1-2 px point on
        // screen — small enough to read as a real star, large enough to
        // remain visible at typical viewport sizes.
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                const float ddx = (float(cx) + float(ox) + 0.5f) - fx;
                const float ddy = (float(cy) + float(oy) + 0.5f) - fy;
                const float w   = std::exp(-(ddx*ddx + ddy*ddy) * 1.8f);
                int xi = ((cx + ox) % kW + kW) % kW;
                int yi = cy + oy;
                if (yi < 0 || yi >= kH) continue;
                const std::size_t off = (std::size_t(yi) * kW + std::size_t(xi)) * 4u;
                auto add = [&](int c, float v) {
                    const float cur = px[off + c] / 255.0f;
                    const float nv  = std::min(1.0f, cur + v);
                    px[off + c] = std::uint8_t(nv * 255.0f);
                };
                add(0, cR * bright * w);
                add(1, cG * bright * w);
                add(2, cB * bright * w);
                if (ox == 0 && oy == 0) {
                    px[off + 3] = std::uint8_t(twPh * 255.0f);
                }
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace sm::sub

namespace sm::sub {

// ── Vertex shader ─────────────────────────────────────────────────────
static const char* kVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    // z = 0.9999 keeps the sky behind everything else when depth-test on.
    gl_Position = vec4(aPos, 0.9999, 1.0);
}
)";

// ── Fragment shader (faithful port of src/game/subworld/sky.ts) ──────
// The shader reconstructs a world-space view ray from yaw/pitch/fov/aspect,
// so the sky behaves as a true celestial sphere: rotating the camera makes
// the sun, moons, stars, and clouds stay anchored to their real bearings.
static const char* kFS = R"(#version 330 core
precision highp float;
in  vec2 vUv;
out vec4 fragColor;

uniform float u_tod;       // 0..1 (0 = midnight, 0.5 = noon)
uniform float u_elapsed;   // real seconds (cloud drift + twinkle)
uniform float u_seed;      // world seed (moon arrangement)
uniform float u_yaw;       // camera yaw  (radians)
uniform float u_pitch;     // camera pitch (radians)
uniform float u_fov;       // vertical fov (radians)
uniform float u_aspect;    // viewport w / h
uniform vec3  u_fogColor;  // for seamless horizon match
uniform sampler2D u_starTex;   // pre-baked equirect star catalog (RGB+phase)

float h21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float h11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1,0)), f.x),
               mix(h21(i + vec2(0,1)), h21(i + vec2(1,1)), f.x), f.y);
}
float fbm4(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 4; ++i) { v += a * vnoise(p); p = r * p * 2.0; a *= 0.5; }
    return v;
}
float fbm3(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 3; ++i) { v += a * vnoise(p); p = r * p * 2.0; a *= 0.5; }
    return v;
}

vec3 viewRay(vec2 uv) {
    float tanHF = tan(u_fov * 0.5);
    float x = (uv.x * 2.0 - 1.0) * u_aspect * tanHF;
    float y = (uv.y * 2.0 - 1.0) * tanHF;
    float cy = cos(u_yaw),   sy = sin(u_yaw);
    float cp = cos(u_pitch), sp = sin(u_pitch);
    vec3 forward = vec3(cy * cp, sp, sy * cp);
    vec3 right   = vec3(-sy, 0.0, cy);
    vec3 up      = cross(right, forward);
    return normalize(forward + x * right + y * up);
}

void main() {
    vec3  rd   = viewRay(vUv);
    float t    = u_tod;
    float elev = rd.y;

    // ── Time-of-day phases ───────────────────────────────────────────
    float dayF   = clamp(smoothstep(0.22, 0.35, t) - smoothstep(0.65, 0.78, t), 0.0, 1.0);
    float nightF = 1.0 - dayF;
    float dawn   = smoothstep(0.20, 0.26, t) * smoothstep(0.35, 0.28, t);
    float dusk   = smoothstep(0.65, 0.72, t) * smoothstep(0.80, 0.74, t);
    float twilight = dawn + dusk;

    // ── 1. Sky gradient ──────────────────────────────────────────────
    vec3 zenithDay   = vec3(0.18, 0.30, 0.62);
    vec3 horizDay    = vec3(0.58, 0.68, 0.82);
    vec3 zenithNight = vec3(0.01, 0.01, 0.04);
    vec3 horizNight  = vec3(0.04, 0.04, 0.08);
    vec3 twiCol      = vec3(0.60, 0.25, 0.08);

    float he = clamp(elev, 0.0, 1.0);
    vec3 skyDay   = mix(horizDay,  zenithDay,   he);
    vec3 skyNight = mix(horizNight, zenithNight, he);
    vec3 col = mix(skyNight, skyDay, dayF);
    col = mix(col, twiCol, twilight * smoothstep(0.25, 0.0, he) * 0.7);

    if (elev < 0.0) col = mix(col, u_fogColor, smoothstep(0.0, -0.12, elev));

    // ── 2. Sun ───────────────────────────────────────────────────────
    float sunAng = (t - 0.25) * 6.28318;
    vec3  sunDir = normalize(vec3(cos(sunAng), sin(sunAng), 0.0));
    float sunVis = smoothstep(0.22, 0.30, t) * smoothstep(0.78, 0.70, t);

    float sunDot  = dot(rd, sunDir);
    float disc    = smoothstep(0.9992, 0.9996, sunDot);
    float glow    = pow(max(sunDot, 0.0), 256.0) * 0.6;
    float scatter = pow(max(sunDot, 0.0), 8.0)   * 0.12;

    vec3 sunCol = mix(vec3(1.0, 0.45, 0.10), vec3(1.0, 0.92, 0.7), dayF);
    col += sunCol * (disc + glow + scatter) * sunVis;

    // ── 3. Moons ─────────────────────────────────────────────────────
    float seedBits = fract(abs(u_seed) * 0.00013751);
    int   mCount   = 1 + int(floor(seedBits * 3.0));
    float moonVis  = clamp(nightF * 1.4, 0.0, 1.0);

    for (int m = 0; m < 3; ++m) {
        if (m >= mCount) break;
        float ms   = abs(u_seed) + float(m) * 1337.7;
        float mRad = 0.02 + h11(ms * 0.137) * 0.015;
        float mAng = (t - 0.75 + h11(ms * 0.419) * 0.25) * 6.28318
                   + float(m) * 2.094;
        vec3 moonDir = normalize(vec3(cos(mAng),
                                      abs(sin(mAng)) * 0.7 + 0.2,
                                      sin(mAng) * 0.4));
        float mDot = dot(rd, moonDir);
        float mD   = acos(clamp(mDot, -1.0, 1.0));

        float edgeNoise = vnoise(rd.xz * 60.0 + ms) * 0.006;
        float moonDisc  = smoothstep(mRad + edgeNoise, mRad * 0.8, mD);
        float surf      = vnoise(rd.xz / mRad * 4.0 + ms * 0.1) * 0.15;

        vec3 mCol = vec3(0.72 + h11(ms *  7.0) * 0.12,
                         0.74 + h11(ms * 11.0) * 0.10,
                         0.80 + h11(ms * 13.0) * 0.12) - surf;

        float mGlow = exp(-mD * mD / (mRad * mRad) * 3.0) * 0.08;
        col = mix(col, mCol, moonDisc * moonVis);
        col += vec3(0.6, 0.65, 0.8) * mGlow * moonVis;
    }

    // ── 4. Stars — single texture lookup ────────────────────────────
    // The star catalog is baked once on CPU into an equirectangular RGBA
    // texture (RGB = pre-multiplied colour, A = per-star twinkle phase).
    // This collapses what used to be a 700-iteration per-pixel loop into
    // one texture sample, which is what made night frames hitch hard on
    // the previous version. Equirect distortion is fine here — stars are
    // points, the pole region is tiny, and the bottom hemisphere is
    // never sampled (we cull on `elev > -0.02`).
    if (nightF > 0.05 && elev > -0.02) {
        float az = atan(rd.z, rd.x);
        float el = asin(clamp(rd.y, -1.0, 1.0));
        vec2 starUv = vec2(az / 6.2831853 + 0.5, el / 3.1415927 + 0.5);
        vec4 sStar = texture(u_starTex, starUv);
        float twPh = sStar.a * 6.2831853;
        float tw = 0.65 + 0.35 * sin(u_elapsed * 1.6 + twPh);
        col += sStar.rgb * tw * nightF;
    }

    // ── 5. Clouds — animated FBM drifting across the dome ────────────
    if (elev > -0.05) {
        float domeH = max(elev + 0.05, 0.01);
        vec2 cloudUV = rd.xz / domeH;
        float wx = u_elapsed * 0.008;
        float wy = u_elapsed * 0.003;
        float c1 = smoothstep(0.42, 0.72, fbm4(cloudUV * 0.6 + vec2(wx, wy)));
        float c2 = smoothstep(0.48, 0.80,
                              fbm3(cloudUV * 1.4 + vec2(wx * 1.6, -wy * 0.5) + 40.0))
                 * 0.35;
        float clouds = clamp(c1 + c2, 0.0, 1.0);

        vec3 cCol = mix(vec3(0.06, 0.06, 0.10), vec3(0.92, 0.92, 0.96), dayF);
        cCol = mix(cCol, vec3(0.95, 0.55, 0.20), twilight * 0.6);

        float cloudSunDot = dot(normalize(vec3(cloudUV, 1.0)), sunDir);
        float cloudLight  = pow(max(cloudSunDot, 0.0), 4.0) * 0.15 * sunVis;
        cCol += sunCol * cloudLight;

        float cloudFade  = smoothstep(-0.05, 0.08, elev);
        float cloudAlpha = clouds * mix(0.55, 0.35, dayF) * cloudFade;
        col = mix(col, cCol, cloudAlpha);
    }

    fragColor = vec4(col, 1.0);
}
)";

void Sky::init() {
    prog = gl_link(kVS, kFS);
    static const float quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    uTod      = glGetUniformLocation(prog, "u_tod");
    uElapsed  = glGetUniformLocation(prog, "u_elapsed");
    uSeed     = glGetUniformLocation(prog, "u_seed");
    uYaw      = glGetUniformLocation(prog, "u_yaw");
    uPitch    = glGetUniformLocation(prog, "u_pitch");
    uFov      = glGetUniformLocation(prog, "u_fov");
    uAspect   = glGetUniformLocation(prog, "u_aspect");
    uFogColor = glGetUniformLocation(prog, "u_fogColor");
    uStarTex  = glGetUniformLocation(prog, "u_starTex");
    glGenTextures(1, &starTex);
    bake_star_texture(starTex);
}

void Sky::destroy() {
    if (prog) glDeleteProgram(prog);
    if (vao)  glDeleteVertexArrays(1, &vao);
    if (vbo)  glDeleteBuffers(1, &vbo);
    if (starTex) glDeleteTextures(1, &starTex);
    prog = vao = vbo = 0;
    starTex = 0;
}

void Sky::render(int viewW, int viewH, const WorldTime& time,
                 const Camera& cam, float elapsed, std::uint32_t seed,
                 float fogR, float fogG, float fogB) {
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, viewW, viewH);
    glUseProgram(prog);
    const float tod = (float(time.hour) + float(time.minute) / 60.0f) / 24.0f;
    const float aspect = viewH > 0 ? float(viewW) / float(viewH) : 1.0f;
    const float fovRad = cam.fovDeg * 3.14159265f / 180.0f;
    glUniform1f(uTod,     tod);
    glUniform1f(uElapsed, elapsed);
    glUniform1f(uSeed,    float(seed));
    glUniform1f(uYaw,     cam.yaw);
    glUniform1f(uPitch,   cam.pitch);
    glUniform1f(uFov,     fovRad);
    glUniform1f(uAspect,  aspect);
    glUniform3f(uFogColor, fogR, fogG, fogB);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, starTex);
    glUniform1i(uStarTex, 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

} // namespace sm::sub

