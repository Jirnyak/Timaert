#version 450
// Subworld water surface (Phase 5): animated wave normal (two drifting noise
// fields), Fresnel sky reflection, sun specular glints, depth-tinted colour.
// Semi-transparent so the terrain shows through the shallows.
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

    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), 80.0);
    col += pc.sunColor.rgb * spec * 0.9;

    float amb = pc.params.y;
    col *= amb + 0.65;
    outColor = vec4(col, 0.82);
}
