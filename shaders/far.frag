#version 450
// THE FAR WORLD'S GROUND (CANON S18.1) — fragment stage, and it is short on
// purpose. Everything that makes the far world convincing is a law that
// already exists; this shader's whole job is to not invent a second one.
//
// COLOUR IS THE MIDPOINT OF THE ROW'S TWO AUTHORED CONSTITUENTS — the very
// pair the near ground's mixture converges to as its detail falls below a
// pixel (ground_surface.glsl kGroundFresh/kGroundWorn, the same generated
// table mesh.frag reads). So the join at the composite's edge is invisible BY
// CONSTRUCTION: both sides are literally the same colour there, and nothing
// was tuned to make that true. «На дальности деталь УБИРАЕТСЯ, а не
// подменяется» — the midpoint is what removal converges to, not a stand-in.
//
// AND THE AIR IS THE LAST LINE, as it is in every lit pass. Without it the far
// world is a hard-edged cardboard cutout; with it the plain dissolves on its
// own and the ridge floats above the haze — which is the entire point of the
// track and the only thing that cannot be faked here.
#include "lighting.glsl"
#include "ground_surface.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vMaterial;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // The table's own length — clamped to it rather than to a number, so a
    // fifteenth ground row appearing tomorrow needs no edit here.
    uint m = uint(clamp(vMaterial, 0.0,
                        float(kGroundFresh.length() - 1)) + 0.5);
    // THE limit of the near ground's mixture: half of each constituent.
    vec3 base = (kGroundFresh[m] + kGroundWorn[m]) * 0.5;

    // One law of light for everything below (CANON S18): the same directional
    // term the near ground is lit by, on the surface's own normal.
    vec3 n = normalize(vNormal);
    float ndl = max(dot(n, normalize(pc.sunDir.xyz)), 0.0);
    vec3 col = base * (pc.ambient.rgb + pc.sunColor.rgb * ndl);

    outColor = vec4(aerial_perspective(col, vWorld), 1.0);
}
