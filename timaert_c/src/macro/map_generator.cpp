#include "macro/map_generator.h"
#include "gl/helpers.h"
#include <cstdio>

namespace sm {

// ---- Shaders ----
static const char* kVS = R"(#version 330 core
layout(location=0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Procedural climate generator — faithful port of `src/webgl/shaders.ts`
// terrain shader. Uses **periodic Perlin noise** (Ken Perlin's permutation
// table) so the master texture tiles seamlessly: a 1024² map wraps with no
// visible seam at uv=0/1 in either axis. Required for our toroidal world.
static const char* kFS = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;

uniform float u_seed;
uniform float u_seaLevel;
uniform float u_heightScale;
uniform float u_moistureScale;
uniform float u_continentScale;
uniform float u_continentIntensity;
uniform float u_ridgeIntensity;
uniform float u_domainWarp;
uniform float u_temperatureVariation;

// Ken Perlin's classic 256-entry permutation table, doubled to 512 to avoid
// modulo on indices. Identical to the TS shader so noise output matches.
const int perm[512] = int[512](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
);

float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

// Periodic Perlin noise: lattice indices wrap modulo `period` so the field
// repeats every `period` units in `p`. With pos = uv*8 and period=8 the
// texture tiles exactly across uv [0,1] → seamless wraparound.
float periodicNoise(vec2 p, float period, float seed) {
    p += seed;
    float px = mod(p.x, period);
    float py = mod(p.y, period);
    int xi  = int(floor(px)) & 255;
    int yi  = int(floor(py)) & 255;
    int xi1 = int(mod(floor(px) + 1.0, period)) & 255;
    int yi1 = int(mod(floor(py) + 1.0, period)) & 255;
    float xf = fract(px);
    float yf = fract(py);
    float u  = fade(xf);
    float v  = fade(yf);
    int aa = perm[perm[xi]  + yi];
    int ab = perm[perm[xi]  + yi1];
    int ba = perm[perm[xi1] + yi];
    int bb = perm[perm[xi1] + yi1];
    float x1 = mix(grad(aa, xf, yf      ), grad(ba, xf - 1.0, yf      ), u);
    float x2 = mix(grad(ab, xf, yf - 1.0), grad(bb, xf - 1.0, yf - 1.0), u);
    return mix(x1, x2, v);
}

// fbm(p, octaves, persistence, period, seed) — exact TS signature/semantics.
float fbm(vec2 p, int oct, float persistence, float period, float seed) {
    float value = 0.0;
    float amp   = 1.0;
    float freq  = 1.0;
    float total = 0.0;
    for (int i = 0; i < 8; i++) {
        if (i >= oct) break;
        value += amp * periodicNoise(p * freq, period * freq, seed + float(i) * 100.0);
        total += amp;
        amp   *= persistence;
        freq  *= 2.0;
    }
    return value / total;
}

void main() {
    vec2 uv  = v_uv;
    vec2 pos = uv * 8.0;

    // Domain warp. Period 8 matches the base scale → tiles cleanly.
    float warpX = fbm(pos + vec2(0.0, 0.0), 3, 0.5, 8.0, u_seed + 50.0);
    float warpY = fbm(pos + vec2(5.2, 1.3), 3, 0.5, 8.0, u_seed + 60.0);
    vec2 q = vec2(warpX, warpY) * u_domainWarp;

    // Base height (6 octaves @ period 8).
    int heightOct = 6;
    float noiseHeight = fbm(pos + q, heightOct, 0.5, 8.0, u_seed) * 0.5 + 0.5;

    // Continental structure — low frequency. Period scales with cScale to
    // keep the periodic Perlin tile aligned with the sampled domain.
    float cScale        = max(0.001, u_continentScale);
    float continentBias = fbm(pos * cScale, 2, 0.5, 8.0 * cScale, u_seed + 700.0);
    noiseHeight += continentBias * u_continentIntensity;

    // Mountain ridges: 1-abs(noise) creates connected chains. period = 8*0.7.
    float ridgeBase = fbm(pos * 0.7, 3, 0.55, 5.6, u_seed + 800.0);
    float ridge     = pow(1.0 - abs(ridgeBase), 3.0) * u_ridgeIntensity;
    noiseHeight += ridge;
    noiseHeight = clamp(noiseHeight, 0.0, 1.0);
    noiseHeight = pow(noiseHeight, u_heightScale);

    // Moisture — period 4 → tiles twice across the map.
    float noiseMoist = fbm(pos + q * 0.5, 4, 0.5, 4.0, u_seed + 200.0) * 0.5 + 0.5;
    noiseMoist = pow(noiseMoist, u_moistureScale);

    // Temperature — latitude-driven with a noise contribution.
    float latitude  = 1.0 - abs(uv.y - 0.5) * 2.0; // 1 at equator, 0 at poles
    float noiseTemp = fbm(pos, 3, 0.5, 4.0, u_seed + 300.0) * 0.5 + 0.5;
    float temp01    = latitude * (1.0 - u_temperatureVariation)
                    + noiseTemp * u_temperatureVariation;
    temp01 = clamp(temp01, 0.0, 1.0);

    float mask = noiseHeight < u_seaLevel ? 0.0 : 1.0;

    fragColor = vec4(noiseHeight, noiseMoist, temp01, mask);
}
)";

TerrainData generate_terrain(int w, int h, const LayerParameters& params) {
    TerrainData td;
    td.width = w; td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4, 0);

    GLuint prog = gl_link(kVS, kFS);
    if (!prog) {
        std::fprintf(stderr, "[macro] terrain shader failed; falling back to flat ground\n");
        for (std::size_t i = 0; i < td.rgba.size(); i += 4) {
            td.rgba[i + 0] = 128; td.rgba[i + 1] = 128; td.rgba[i + 2] = 128; td.rgba[i + 3] = 255;
        }
        td.texture = gl_make_texture_rgba8(w, h, td.rgba.data(), GL_LINEAR, GL_LINEAR, GL_REPEAT);
        return td;
    }

    FBO fbo; fbo.create_rgba8(w, h, GL_NEAREST);
    fbo.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform1f(glGetUniformLocation(prog, "u_seed"), params.seed);
    glUniform1f(glGetUniformLocation(prog, "u_seaLevel"), params.seaLevel);
    glUniform1f(glGetUniformLocation(prog, "u_heightScale"), params.heightScale);
    glUniform1f(glGetUniformLocation(prog, "u_moistureScale"), params.moistureScale);
    glUniform1f(glGetUniformLocation(prog, "u_continentScale"), params.continentScale);
    glUniform1f(glGetUniformLocation(prog, "u_continentIntensity"), params.continentIntensity);
    glUniform1f(glGetUniformLocation(prog, "u_ridgeIntensity"), params.ridgeIntensity);
    glUniform1f(glGetUniformLocation(prog, "u_domainWarp"), params.domainWarp);
    glUniform1f(glGetUniformLocation(prog, "u_temperatureVariation"), params.temperatureVariation);

    FullscreenQuad q; q.create();
    q.draw();
    glFinish();
    gl_read_pixels_rgba8(w, h, td.rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    q.destroy();
    glDeleteProgram(prog);

    // Keep the FBO color texture as our master.
    td.texture = fbo.color;
    fbo.color = 0;
    fbo.destroy();
    glBindTexture(GL_TEXTURE_2D, td.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return td;
}

void destroy_terrain(TerrainData& t) {
    if (t.texture) { glDeleteTextures(1, &t.texture); t.texture = 0; }
    t.rgba.clear();
    t.width = t.height = 0;
}

} // namespace sm
