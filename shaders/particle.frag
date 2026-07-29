#version 450
// Additive particle fragment (FX Inc A). Emissive: no lighting, no texture — a
// procedural soft-round spark. The pipeline blends ADDITIVELY (src·srcAlpha +
// dst), so overlapping particles accumulate into a bright core, giving the
// fantasy-pixel "glow" bloom on dense stacks (fireballs, magic bursts). The
// 8-bit LDR target clamps at 1.0, which is exactly the desired saturate-to-white
// hot centre.
layout(location = 0) in vec2 vUv;    // [-1,1]² across the quad
layout(location = 1) in vec4 vColor; // rgb + faded alpha (from the sim)

layout(location = 0) out vec4 outColor;

void main() {
    // Radial falloff: 1 at the centre, 0 at the quad edge. Squared for a soft
    // core with a bright pinpoint, matching a round pixel-art spark rather than
    // a hard disc.
    float r = length(vUv);
    float fall = clamp(1.0 - r, 0.0, 1.0);
    fall = fall * fall;
    float a = vColor.a * fall;
    if (a <= 0.003) discard; // skip fully-transparent corners (cheap + clean)

    // Lift toward white at the very centre so a hot core reads as "energy".
    vec3 rgb = vColor.rgb;
    rgb = mix(rgb, vec3(1.0), fall * 0.35); // white-hot centre

    // We output rgb·a with alpha a; the pipeline's additive blend factors are
    // (src = SRC_ALPHA, dst = ONE), so the framebuffer accumulates rgb·a².
    // The extra alpha power is deliberate: it steepens the radial falloff
    // (∝ (1−r)⁴ overall) into a tight bright core with a soft halo — the
    // fantasy-pixel glow — and slows the march to full white so dense stacks
    // bloom gracefully instead of clipping instantly. Verified by the
    // GPU_SMOKE_FX night A/B (core saturates, halo fades, nothing darkens).
    outColor = vec4(rgb * a, a);
}
