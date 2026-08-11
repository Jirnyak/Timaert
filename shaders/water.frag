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

// The wave field: three octaves of drifting value noise on a slowly-churning
// warped domain. Each octave drifts its OWN direction and speed and the warp
// drifts on a third clock, so the pattern never tiles and never repeats — the
// surface churns organically instead of sliding past as one frozen sheet.
// `fade` kills each octave at the camera distance where its wavelength drops
// toward a pixel: near water ripples, far water flattens toward a clean mirror
// instead of the shimmering noise-mush a fixed 33 cm ripple becomes at 500 m.
float wave_h(vec2 w, float time, vec3 fade) {
    return (wn(w * 0.16 + vec2( time * 0.09,  time * 0.06)) - 0.5) * 1.00 * fade.x
         + (wn(w * 0.70 + vec2(-time * 0.21,  time * 0.15)) - 0.5) * 0.42 * fade.y
         + (wn(w * 2.60 + vec2( time * 0.50, -time * 0.31)) - 0.5) * 0.16 * fade.z;
}

void main() {
    float time = pc.params.x;
    vec2 w = vWorld.xz;
    float dist = length(pc.camPos.xyz - vWorld);
    // Domain warp, computed ONCE (it is low-frequency; the finite-difference
    // taps below ride the same warped sheet, which is exactly what keeps the
    // derived normal coherent).
    vec2 churn = vec2(time * 0.017, -time * 0.013);
    vec2 ww = w + (vec2(wn(w * 0.09 + churn),
                        wn(w * 0.09 + churn + 37.2)) - 0.5) * 4.0;
    // Per-octave distance windows: swell / chop / ripple.
    vec3 fade = vec3(1.0 - smoothstep(250.0, 1100.0, dist),
                     1.0 - smoothstep(50.0, 260.0, dist),
                     1.0 - smoothstep(12.0, 60.0, dist));
    float e = 0.35;
    float h0 = wave_h(ww, time, fade);
    float hx = wave_h(ww + vec2(e, 0.0), time, fade) - h0;
    float hz = wave_h(ww + vec2(0.0, e), time, fade) - h0;
    vec3 N = normalize(vec3(-hx * 2.3, 1.0, -hz * 2.3));
    float n0 = clamp(h0 + 0.5, 0.0, 1.0);
    vec3 V = normalize(pc.camPos.xyz - vWorld);
    vec3 L = normalize(pc.sunDir.xyz);

    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    // Depth from the window heightfield (set 0 binding 2 — already bound for
    // every lit pass, zero new plumbing): the water knows how deep it stands
    // at this very point. Beer-style transmittance turns metres into colour —
    // bright turquoise shallows falling to the dark deep — and into
    // transparency: the bed shows through a knee-deep shore and vanishes under
    // a real lake. span==0 (the heightfield-less smoke harness) degrades to
    // all-deep, the pre-depth look.
    float span = u_pointLights.terrainParams.x;
    float depth = 30.0;
    if (span > 0.0)
        depth = max(pc.params.z - texture(u_heightM, w / span + 0.5).r, 0.0);
    float clear = exp(-depth * 0.30);
    vec3 water = mix(vec3(0.04, 0.15, 0.24), vec3(0.13, 0.38, 0.42), clear);
    water *= 0.8 + 0.4 * n0;
    float alpha = mix(0.88, 0.35, clear);

    float amb = pc.params.y;
    // The honest sky in the mirror: built from the SAME quantities that paint
    // the world. dayF is recovered from the ambient law (lighting.h:
    // ambient.y = 0.08 + dayF*0.32); the gradient constants are sky.frag's
    // own zenith/horizon pairs; twilight warmth comes from sunColor, which
    // compute_light_parameters already turns orange at a low sun — so dawn
    // and dusk stain the lake without the shader ever knowing the hour.
    vec3 R = reflect(-V, N);
    float ry = clamp(R.y, 0.02, 1.0);
    float dayF = clamp((amb - 0.08) / 0.32, 0.0, 1.0);
    vec3 sky = mix(mix(vec3(0.04, 0.04, 0.09), vec3(0.01, 0.01, 0.05), ry),
                   mix(vec3(0.58, 0.68, 0.82), vec3(0.18, 0.30, 0.62), ry),
                   dayF);
    float warmth = clamp(pc.sunColor.r - pc.sunColor.b, 0.0, 1.0);
    sky = mix(sky, vec3(0.60, 0.25, 0.08), warmth * (1.0 - ry) * 0.8);
    // Clouds lie mirrored on the water: march the reflected ray to a ~500 m
    // deck and sample the ONE cloud field (clouds.glsl) — the cloud you see
    // overhead, the shadow that crawls under your feet and the reflection on
    // the lake are the same object. cloudVis (the field straight above) also
    // gates the sun/moon glints below: no glitter road under an overcast sky.
    vec4 sp = u_pointLights.skyParams;
    float cloudVis = 1.0;
    if (sp.w > 0.01) {
        cloudVis = cloud_sun_visibility(w);
        vec2 cp = (w + R.xz / max(R.y, 0.12) * 500.0) * TIMAERT_CLOUD_WORLD_SCALE;
        float cc = cloud_cover(cp, sp.x, sp.yz, sp.w);
        vec3 cCol = mix(vec3(0.06, 0.06, 0.10), vec3(0.92, 0.92, 0.96), dayF);
        cCol = mix(cCol, vec3(0.95, 0.55, 0.20), warmth * 0.5);
        sky = mix(sky, cCol, cc * 0.55);
    }

    // The body is lit by the day (the ambient wash); the mirror is NOT — the
    // sky colours above are already day/night honest, and washing them again
    // would double-darken the night reflection.
    water *= amb + 0.65;
    vec3 col = mix(water, sky, 0.10 + 0.75 * fres);
    alpha = mix(alpha, 0.95, fres);

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
    col += pc.sunColor.rgb * (core * 0.9 + path * 1.7) * cloudVis;

    // Surf: three INDEPENDENT scales of chaos, multiplied — the anti-doily.
    // The failed first cut was one 20 cm noise thresholded along the whole
    // shoreline: a continuous lace ribbon, identical everywhere, pulsing in
    // sync. Each of its three sins is answered by its own layer:
    //   1) PATCHES (10s of metres) — a slowly-drifting two-octave field
    //      decides WHERE surf lives at all. Stretches of foam appear, migrate
    //      and dissolve; between them the waterline stays clean. Kills the
    //      unbroken ribbon AND the everywhere-synchronised pulse: each reach
    //      of coast lives on its own clock.
    //   2) BREAKERS — the swell itself, sampled UN-faded (the camera-distance
    //      fade that flattens far water must not becalm the far surf),
    //      arriving over the shallows: a crest strikes, rolls shoreward
    //      (depth-phase, scrambled per-stretch by the crest value) and dies.
    //      Waves become events, not texture.
    //   3) FROTH (sub-metre) — ragged flakes carved out of the strike by a
    //      density-dependent threshold: dense cores read near-solid with torn
    //      holes, thin edges scatter into clumps. The finest octave fades by
    //      ~150 m so distant surf reads as soft white patches, not fizz.
    float shoreZ = 1.0 - smoothstep(0.0, 3.0, depth);
    if (shoreZ > 0.0) {
        float pf = wn(w * 0.035 + vec2(time * 0.010, -time * 0.008))
                 + wn(w * 0.085 + vec2(-time * 0.014, time * 0.011)) * 0.5;
        float foamPatch = smoothstep(0.55, 0.95, pf);
        float crest = wn(w * 0.16 + vec2(time * 0.09, time * 0.06));
        float roll  = 0.5 + 0.5 * sin(depth * 2.5 + time * 1.3 + crest * 6.0);
        float breaker = smoothstep(0.50, 0.85, crest * 0.60 + roll * 0.40);
        float frothFade = 1.0 - smoothstep(30.0, 150.0, dist);
        float fr = wn(w * 1.7 + vec2(time * 0.35, time * 0.22)) * 0.6
                 + wn(w * 4.5 + vec2(-time * 0.55, time * 0.40)) * 0.4 * frothFade;
        float density = foamPatch * breaker * shoreZ;
        float foam = density * smoothstep(0.55 - 0.35 * density,
                                          0.80 - 0.20 * density, fr);
        // Waterline residual — the thin lace right at the edge, but ONLY
        // inside a living foamPatch; between patches the shoreline rests bare.
        float edge = (1.0 - smoothstep(0.0, 0.45, depth)) * foamPatch;
        foam = max(foam, edge * smoothstep(0.35, 0.75, fr));
        col = mix(col, vec3(0.87, 0.91, 0.93) * (amb + 0.55),
                  clamp(foam, 0.0, 1.0));
        alpha = max(alpha, min(foam * 1.3, 0.95));
    }

    // Positional lights reflected on the water (specular glint form). Added
    // AFTER the day/night ambient wash — a torch or spell reflection on the
    // surface is its own light source, not scaled by the sun's time of day, so a
    // lantern on the shore paints a shimmering coloured streak on the ripples at
    // night exactly as the same light pools on the ground beside it. Inert until
    // an emitter exists (returns 0 when the light buffer is empty).
    col += point_lights_spec(vWorld, N, V);
    outColor = vec4(col, alpha);
}
