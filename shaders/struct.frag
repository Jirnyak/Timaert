#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced structure fragment stage (Phase 5): stone city walls + tan houses
// with red-brown roofs, lit by the same sun + ambient and PCF shadow map as the
// terrain (walls cast AND receive real shadows). Colour is keyed by the instance
// `type`; adding a structure kind is one more branch, no new pipeline.
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform sampler2DShadow u_shadowFar;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vType;
layout(location = 3) in float vLocalY;
layout(location = 4) in float vSeed;
layout(location = 5) in vec2 vFace;   // face-local [-1,1]² (see struct.vert)

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

#include "shadow_common.glsl"
#include "lighting.glsl"

float s_hash(vec2 p) {
    p = floor(p);
    return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453);
}

// Plank grain: vertical boards with a darker seam every `pitch` metres and a
// per-board tint. Shared by every wooden thing so a door, a bed and a stair
// read as the same timber.
vec3 s_planks(vec3 wp, float seed, float pitch, vec3 tint) {
    // Boards run along whichever horizontal axis varies fastest across the
    // face, so a wall and a lid both get grain instead of stripes-or-nothing.
    float u = abs(wp.x) > abs(wp.z) ? wp.z : wp.x;
    float board = floor(u / pitch);
    float seam = abs(fract(u / pitch) - 0.5) * 2.0;       // 0 mid-board, 1 seam
    float grain = 0.88 + 0.12 * s_hash(vec2(board, floor(wp.y * 3.0)) + seed);
    return tint * grain * (1.0 - 0.28 * pow(seam, 6.0));
}

void main() {
    vec3 base;
    if (vType < 0.5) {
        // Stone: grey with a faint per-block speckle.
        base = vec3(0.54, 0.54, 0.57)
               * (0.90 + 0.10 * s_hash(vWorld.xz * 9.0 + vSeed));
    } else if (vType < 1.5) {
        // House: tan walls, red-brown roof on the upper band.
        vec3 wall = vec3(0.72, 0.60, 0.42)
                    * (0.92 + 0.08 * s_hash(vWorld.xz * 7.0 + vSeed));
        vec3 roof = vec3(0.47, 0.23, 0.16);
        base = vLocalY > 0.25 ? roof : wall;
    } else if (vType < 2.5) {
        // Wood: planks all over, no roof band — furniture, decks, stairs.
        base = s_planks(vWorld, vSeed, 0.9, vec3(0.52, 0.36, 0.22));
    } else if (vType < 3.5) {
        // Door: a dark planked leaf in a lighter frame, with a handle — drawn
        // in FACE space (vFace, [-1,1]²), so the pattern keeps its proportions
        // whatever size the leaf is and wherever in the world it hangs.
        vec3 leaf  = vec3(0.30, 0.18, 0.10)
                     * (0.86 + 0.14 * s_hash(vec2(floor(vFace.x * 3.0),
                                                  floor(vFace.y * 1.5)) + vSeed));
        // Four vertical boards: darken the seams between them.
        float seam = abs(fract(vFace.x * 2.0) - 0.5) * 2.0;
        leaf *= 1.0 - 0.30 * pow(seam, 8.0);
        vec3 frame = vec3(0.46, 0.32, 0.19);
        // Frame: a border of the outer sixth on every side of the face.
        float border = max(smoothstep(0.80, 0.90, abs(vFace.x)),
                           smoothstep(0.80, 0.90, abs(vFace.y)));
        base = mix(leaf, frame, border);
        // Handle: one brass disc at waist height on the opening side.
        float h = length(vec2(vFace.x - 0.55, vFace.y + 0.05) * vec2(1.0, 1.6));
        base = mix(base, vec3(0.80, 0.68, 0.32),
                   1.0 - smoothstep(0.07, 0.11, h));
    } else if (vType < 4.5) {
        // Lantern: a dark post whose head burns. The glow is emissive — the
        // light it CASTS is a real point light (the prop table's light row),
        // this is only the lamp reading as lit from any distance.
        vec3 post = vec3(0.22, 0.18, 0.14);
        float head = smoothstep(0.55, 0.75, vLocalY);
        base = mix(post, vec3(1.00, 0.78, 0.42), head);
    } else if (vType < 5.5) {
        // Chest: dark timber banded with iron. The straps are face-space
        // bars, so a coffer of any size wears the same ironwork.
        vec3 timber = vec3(0.26, 0.16, 0.09)
                      * (0.85 + 0.15 * s_hash(vec2(floor(vFace.x * 4.0),
                                                   floor(vFace.y * 3.0)) + vSeed));
        float strap = max(smoothstep(0.16, 0.10, abs(abs(vFace.x) - 0.55)),
                          smoothstep(0.14, 0.08, abs(vFace.y)));
        base = mix(timber, vec3(0.32, 0.33, 0.35), strap);
    } else if (vType < 6.5) {
        // Cave mouth: rock with a black opening bitten out of it. The dark is
        // an ellipse in face space, so a mouth of any size keeps its shape —
        // and it is nearly black, because what is behind it is not lit from
        // out here. That contrast IS the read: a hole, not a boulder.
        vec3 rock = vec3(0.38, 0.36, 0.33)
                    * (0.80 + 0.20 * s_hash(vWorld.xz * 6.0 + vSeed));
        float hole = length(vec2(vFace.x, (vFace.y + 0.25) * 1.15));
        base = mix(vec3(0.03, 0.03, 0.04), rock,
                   smoothstep(0.55, 0.80, hole));
    } else if (vType < 7.5) {
        // Well: wet stones with dark water at the crown, so a curb reads as
        // full rather than as a barrel.
        vec3 stone = vec3(0.46, 0.46, 0.44)
                     * (0.82 + 0.18 * s_hash(vWorld.xz * 5.0 + vSeed));
        base = vLocalY > 0.92 ? vec3(0.06, 0.10, 0.14) : stone;
    } else if (vType < 8.5) {
        // Sign: a pale painted board over a dark post — the board is the
        // upper part of the face, the post the strip below it.
        float board = smoothstep(-0.10, 0.05, vFace.y);
        vec3 plank = vec3(0.62, 0.50, 0.30)
                     * (0.90 + 0.10 * s_hash(vec2(floor(vFace.x * 5.0),
                                                  floor(vFace.y * 5.0)) + vSeed));
        base = mix(vec3(0.28, 0.20, 0.13), plank, board);
    } else {
        // Spire orb: a black plinth whose crown BURNS the spire's own night
        // tint (macro landmark row 0xA86CFF family) — the lantern's law: a
        // vertical head band reads from every side and any distance (the
        // light it CASTS is the prop row's point light), with a white-hot
        // rim right at the top edge.
        vec3 plinth = vec3(0.10, 0.09, 0.13)
                      * (0.85 + 0.15 * s_hash(vWorld.xz * 8.0 + vSeed));
        float head = smoothstep(0.55, 0.80, vLocalY);
        vec3 glow = mix(vec3(0.66, 0.42, 1.00), vec3(0.98, 0.92, 1.00),
                        smoothstep(0.88, 1.00, vLocalY));
        base = mix(plinth, glow, head);
    }

    vec3 N = normalize(vNormal);
    float ndlRaw = max(dot(N, normalize(pc.sunDir.xyz)), 0.0);
    float ndl = floor(ndlRaw * 4.0) / 4.0; // 4-band quantise
    float sh = shadowFactorHandoff(u_shadow, u_shadowFar,
                                   pc.lightMvp * vec4(vWorld, 1.0),
                                   far_light_clip(vWorld), ndlRaw,
                                   TIMAERT_SHADOW_SPREAD_MESH);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl, sh, vWorld);
    // Additive positional lights on walls/roofs (window vWorld space, matching the
    // sun/shadow math). Inert until the light buffer is populated (Inc 3+).
    col += base * point_lights(vWorld, N);
    outColor = vec4(col, 1.0);
}
