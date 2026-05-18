// Bake the macroworld tree shader into a 2D atlas texture.
//
// We adapt the verbatim TREE_MAP_GLSL from macro_renderer.cpp into a
// fragment shader that takes (variantIdx, typeIdx) from gl_FragCoord and
// produces a single 16×16 pixel-art tree per atlas tile.  Each tile is
// rendered at kTileSize×kTileSize so subworld billboards have 4× over-
// sampling for cleanish edges under bilinear magnification.
#include "sub/tree_atlas.h"
#include "gl/helpers.h"

namespace sm::sub {

// Mirror of TREE_MAP_GLSL with the temperature-driven tp dispatch ripped
// out and replaced by `tp = uTypeRow` (per atlas row).  `cell` is faked
// from (variantIdx, worldSeed) so each atlas column gets a stable but
// distinct random tree of that species.
static const char* kBakeVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kBakeFS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform float uSeed;
uniform float uVariants;
uniform float uTypes;

float th(float n) {
    n = fract(n * 0.1031); n *= n + 33.33; n *= n + n; return fract(n);
}
float th2(vec2 c, float o) {
    vec2 p = c + o;
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    // Determine atlas tile from uv.
    float tileX = floor(vUv.x * uVariants);     // 0..variants-1
    float tileY = floor(vUv.y * uTypes);        // 0..types-1
    int   tp    = int(tileY);
    vec2 localUV = vec2(
        fract(vUv.x * uVariants),
        fract(vUv.y * uTypes)
    );

    // Synthetic cell coord per (variant, type) so each tile gets a stable
    // but unique random seed.
    vec2 cell = vec2(tileX * 17.0 + 3.0, tileY * 31.0 + 7.0);

    float v1 = th2(cell, uSeed + 1.0);
    float v2 = th2(cell, uSeed + 2.0);

    vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
    float cs = th2(cell, uSeed) * 1e3;
    float ph = th(cs + p.x * 17.1 + p.y * 31.7);
    float cx = 7.0 + floor((v1 - 0.5) * 2.0);

    vec3 bark1, bark2, leaf1, leaf2, leaf3;
    if (tp == 0) {
        bark1 = vec3(79,56,41)/255.0;  bark2 = vec3(101,67,33)/255.0;
        leaf1 = vec3(30,120,30)/255.0; leaf2 = vec3(50,160,50)/255.0; leaf3 = vec3(75,105,42)/255.0;
    } else if (tp == 1) {
        bark1 = vec3(60,40,30)/255.0;   bark2 = vec3(85,55,40)/255.0;
        leaf1 = vec3(255,160,180)/255.0; leaf2 = vec3(255,120,165)/255.0; leaf3 = vec3(225,105,145)/255.0;
    } else if (tp == 2) {
        bark1 = vec3(195,195,190)/255.0; bark2 = vec3(240,240,235)/255.0;
        leaf1 = vec3(105,195,85)/255.0; leaf2 = vec3(135,215,105)/255.0; leaf3 = vec3(85,165,65)/255.0;
    } else if (tp == 3) {
        bark1 = vec3(70,50,40)/255.0;   bark2 = vec3(95,68,48)/255.0;
        leaf1 = vec3(235,125,10)/255.0; leaf2 = vec3(225,65,10)/255.0; leaf3 = vec3(245,200,15)/255.0;
    } else if (tp == 4) {
        bark1 = vec3(88,58,38)/255.0; bark2 = vec3(105,72,52)/255.0;
        leaf1 = vec3(12,82,12)/255.0; leaf2 = vec3(32,115,32)/255.0; leaf3 = vec3(18,68,18)/255.0;
    } else if (tp == 5) {
        bark1 = vec3(88,62,48)/255.0;  bark2 = vec3(105,72,38)/255.0;
        leaf1 = vec3(125,190,45)/255.0; leaf2 = vec3(105,170,35)/255.0; leaf3 = vec3(145,205,55)/255.0;
    } else {
        bark1 = vec3(62,45,30)/255.0; bark2 = vec3(80,55,35)/255.0;
        leaf1 = vec3(15,95,20)/255.0; leaf2 = vec3(25,130,30)/255.0; leaf3 = vec3(10,75,15)/255.0;
    }

    vec3 bk = ph < 0.5 ? bark1 : bark2;
    vec3 lf = ph < 0.33 ? leaf1 : (ph < 0.66 ? leaf2 : leaf3);

    vec3 col = vec3(0.0);
    float drawn = 0.0;

    if (tp == 4) {
        float trT = 10.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        float baseY = 1.0 + floor(v1 * 2.0);
        for (int i = 0; i < 3; i++) {
            float tT = baseY + float(i) * 3.0;
            float tB = tT + 3.0;
            if (p.y >= tT && p.y <= tB) {
                float frac = (p.y - tT) / 3.0;
                float halfW = 0.5 + frac * (2.2 + float(i) * 0.7);
                float eN = (th(cs + p.y * 7.1 + float(i) * 97.0) - 0.5) * 0.7;
                if (abs(p.x - cx) <= halfW + eN) {
                    vec3 lc = lf;
                    if (p.y < tT + 1.0) lc *= 1.18;
                    else if (p.y >= tB) lc *= 0.72;
                    col = lc; drawn = 1.0;
                }
            }
        }
    } else if (tp == 2) {
        float trT = 5.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) {
            col = bk;
            if (mod(p.y + floor(v1 * 3.0), 3.0) < 1.0 && ph > 0.4) col = vec3(0.22,0.22,0.20);
            drawn = 1.0;
        }
        float cY = trT - 2.5;
        float rX = 2.5 + v1 * 1.2;
        float rY = 3.5 + v2 * 1.5;
        vec2 dd = (p - vec2(cx, cY)) / vec2(rX, rY);
        float eN = (th(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.25;
        if (dot(dd, dd) <= 1.0 + eN) {
            vec3 lc = lf;
            if (dd.y < -0.35) lc *= 1.18;
            else if (dd.y > 0.35) lc *= 0.78;
            col = lc; drawn = 1.0;
        }
    } else if (tp == 5) {
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        float cY = 4.5;
        float cR = 4.5 + v1;
        float d = length(p - vec2(cx, cY));
        float eN = (th(cs + p.x * 13.3 + p.y * 23.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.15;
            else if (p.y > cY + cR * 0.15) lc *= 0.82;
            col = lc; drawn = 1.0;
        }
    } else if (tp == 6) {
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) <= 1.0) { col = bk; drawn = 1.0; }
        if (p.y >= 13.0 && p.y <= 15.0) {
            float rootW = 2.5 - (15.0 - p.y) * 0.5;
            if (abs(p.x - cx) <= rootW && abs(p.x - cx) > 1.0) {
                col = bk * 0.85; drawn = 1.0;
            }
        }
        float cY1 = 3.5;
        float rX1 = 5.5 + v1 * 1.5;
        float rY1 = 4.0 + v2;
        vec2 dd1 = (p - vec2(cx, cY1)) / vec2(rX1, rY1);
        float eN1 = (th(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.35;
        if (dot(dd1, dd1) <= 1.0 + eN1) {
            vec3 lc = lf;
            if (dd1.y < -0.3) lc *= 1.15;
            else if (dd1.y > 0.3) lc *= 0.75;
            if (dot(dd1, dd1) > 0.7 + eN1) lc *= 0.85;
            col = lc; drawn = 1.0;
        }
        float cx2 = cx + (v1 < 0.5 ? -2.0 : 2.0);
        float cY2 = 2.0 + v2;
        float rC2 = 3.0 + v1 * 0.8;
        float d2 = length(p - vec2(cx2, cY2));
        float eN2 = (th(cs + p.x * 9.1 + p.y * 15.3) - 0.5) * 0.5;
        if (d2 <= rC2 + eN2) {
            vec3 lc = leaf2;
            if (p.y < cY2 - rC2 * 0.3) lc *= 1.12;
            else if (p.y > cY2 + rC2 * 0.2) lc *= 0.78;
            col = lc; drawn = 1.0;
        }
    } else {
        // OAK / CHERRY / AUTUMN — round canopy
        float trT = 9.0 - floor(v2 * 2.0);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        float cR = 4.5 + v1 * 1.5;
        float cY = trT - cR + 1.5;
        float d = length(p - vec2(cx, cY));
        float eN = (th(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.22;
            else if (p.y > cY + cR * 0.3) lc *= 0.72;
            if (d > cR + eN - 1.2) lc *= 0.88;
            col = lc; drawn = 1.0;
            if (tp == 1 && ph > 0.82) col = vec3(1.0, 0.96, 0.98);
        }
    }

    frag = vec4(col, drawn);
}
)";

void TreeAtlas::bake(std::uint32_t worldSeed) {
    // Allocate texture if first call.
    if (!tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // FBO + fullscreen quad for the bake.
    GLint prevFbo = 0, prevVp[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);

    GLuint prog = gl_link(kBakeVS, kBakeFS);
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float quad[12] = {
        -1.f,-1.f,  1.f,-1.f,  -1.f, 1.f,
         1.f,-1.f,  1.f, 1.f,  -1.f, 1.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform1f(glGetUniformLocation(prog, "uSeed"),
                float(worldSeed & 0xffffu) * 0.01f);
    glUniform1f(glGetUniformLocation(prog, "uVariants"), float(kVariants));
    glUniform1f(glGetUniformLocation(prog, "uTypes"),    float(kTypes));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFbo));
    glDeleteFramebuffers(1, &fbo);
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void TreeAtlas::destroy() {
    if (tex) glDeleteTextures(1, &tex);
    tex = 0;
}

// Mirror the macro temperature-driven tree-type dispatch in TREE_MAP_GLSL,
// but indexed by biome (subworld doesn't carry the temperature texture):
//   cold   → Pine (4) / Birch (2)
//   cool   → Birch (2) / Autumn (3)
//   warm   → Oak (0) / Willow (5) / Cherry (1)
//   hot    → Jungle (6)
int tree_type_for_temperature(float temperature, float hash) {
    if (temperature < 0.20f) return 4;
    if (temperature < 0.35f) return hash < 0.45f ? 4 : 2;
    if (temperature < 0.50f) return hash < 0.45f ? 2 : 3;
    if (temperature < 0.65f) {
        return hash < 0.40f ? 0 : (hash < 0.70f ? 3 : 5);
    }
    if (temperature < 0.80f) {
        return hash < 0.35f ? 1 : (hash < 0.65f ? 0 : 5);
    }
    return 6;
}

int tree_type_for(Biome b, float hash) {
    switch (b) {
        case Biome::Tundra:
        case Biome::Snow:   return 4;                          // PINE
        case Biome::Taiga:  return hash < 0.6f ? 4 : 2;        // PINE / BIRCH
        case Biome::Valley: return hash < 0.5f ? 2 : 3;        // BIRCH / AUTUMN
        case Biome::Meadow: return hash < 0.45f ? 0
                                   : (hash < 0.80f ? 3 : 5);   // OAK / AUTUMN / WILLOW
        case Biome::Steppe: return hash < 0.7f ? 0 : 5;        // OAK / WILLOW
        case Biome::Swamp:  return hash < 0.5f ? 5 : 6;        // WILLOW / JUNGLE
        case Biome::Desert: return hash < 0.5f ? 5 : 0;        // sparse OAK/WILLOW
        case Biome::Tropics:return 6;                          // JUNGLE
        case Biome::Water:  return 0;
    }
    return 0;
}

} // namespace sm::sub
