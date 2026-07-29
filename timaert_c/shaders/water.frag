#version 450
#extension GL_GOOGLE_include_directive : require
// Subworld water surface (Phase 5): animated wave normal (two drifting noise
// fields), Fresnel sky reflection, sun specular glints, depth-tinted colour.
// Semi-transparent so the terrain shows through the shallows.
#include "lighting.glsl"
layout(location = 0) in vec3 vWorld;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 params; // x=time y=ambient z=waterLevel w=extent
} pc;

layout(location = 0) out vec4 outColor;

float wh(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float wn(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(wh(i), wh(i + vec2(1, 0)), f.x),
               mix(wh(i + vec2(0, 1)), wh(i + vec2(1, 1)), f.x), f.y);
}

void main() {
    float time = pc.params.x;
    vec2 w = vWorld.xz;
    vec2 drift = vec2(time * 0.3, time * 0.2);
    float e = 0.15;
    float n0 = wn(w * 3.0 + drift);
    float nx = wn((w + vec2(e, 0.0)) * 3.0 + drift) - n0;
    float nz = wn((w + vec2(0.0, e)) * 3.0 + drift) - n0;
    vec3 N = normalize(vec3(-nx * 2.5, 1.0, -nz * 2.5));
    vec3 V = normalize(pc.camPos.xyz - vWorld);
    vec3 L = normalize(pc.sunDir.xyz);

    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 water = mix(vec3(0.05, 0.18, 0.28), vec3(0.10, 0.34, 0.42), n0);
    vec3 skyish = vec3(0.45, 0.62, 0.85);
    vec3 col = mix(water, skyish, fres * 0.6);

    // Specular reflection of the active body (sun by day, moon by night — the
    // renderer folds whichever is up into sunDir/sunColor, so this one block
    // serves both). Half-vector model: the highlight lands where the wave
    // normal bisects light and view.
    vec3 H = normalize(L + V);
    float nh = max(dot(N, H), 0.0);
    // Tight core glint — the compact bright point (the daytime look, unchanged).
    float core = pow(nh, 80.0);
    // Glitter "road": a far wider lobe that only spreads when the light sits LOW
    // over the horizon. A low light grazing a rippled surface smears its
    // reflection into the long shimmering path pointing back at the viewer (the
    // "лунная дорожка"); a high/overhead light keeps a compact spot. lowLight is
    // 0 straight overhead → 1 at the horizon, so midday sun is untouched and the
    // road appears for the setting sun and the risen moon alike — universal.
    float lowLight = 1.0 - clamp(abs(L.y), 0.0, 1.0);
    float pathExp = mix(400.0, 14.0, lowLight);
    float path = pow(nh, pathExp) * lowLight;
    col += pc.sunColor.rgb * (core * 0.9 + path * 1.7);

    float amb = pc.params.y;
    col *= amb + 0.65;

    // Positional lights reflected on the water (specular glint form). Added
    // AFTER the day/night ambient wash — a torch or spell reflection on the
    // surface is its own light source, not scaled by the sun's time of day, so a
    // lantern on the shore paints a shimmering coloured streak on the ripples at
    // night exactly as the same light pools on the ground beside it. Inert until
    // an emitter exists (returns 0 when the light buffer is empty).
    col += point_lights_spec(vWorld, N, V);
    outColor = vec4(col, 0.82);
}
