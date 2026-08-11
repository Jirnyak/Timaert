#version 450
// THE subworld billboard vertex stage — one stage for every camera-facing
// sprite pass (trees, procedural creatures, paper-doll NPCs). The quad corners
// come from gl_VertexIndex; the per-instance record is gpu/bb_instance.h, the
// ONE contract the renderer and the smoke harness both include. Cylindrical
// facing: world-up stays vertical, right follows the camera.
//
// Conventions decided here, once, for every pass:
//   - uv.y = 0 at the BASE (feet/roots), 1 at the crown;
//   - the quad is halfW/height from the instance — every aspect decision
//     (tree atlas law, creature body plan) was made on the CPU, so the lit
//     and shadow silhouettes cannot disagree;
//   - `kind`/`seed`/`tint` pass through untouched; each fragment stage reads
//     what its kind means (seed carries a float's bits — uintBitsToFloat).
layout(location = 0) in vec3  iPos;    // instance: base world position
layout(location = 1) in float iHalfW;  // instance: half-width, world units
layout(location = 2) in float iHeight; // instance: full height, world units
layout(location = 3) in uint  iKind;   // instance: pass-specific discrete id
layout(location = 4) in uint  iSeed;   // instance: variation (float bits)
layout(location = 5) in uint  iTint;   // instance: packed RGBA8

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;  // xyz = camera right (world space)
    vec4 sunColor;  // rgb = sun colour
    vec4 ambient;   // rgb = ambient (sky / moon) light
    mat4 lightMvp;  // world -> light clip (shadow map projection)
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out uint vKind;
layout(location = 2) flat out uint vSeed;
layout(location = 3) flat out vec4 vTint;
layout(location = 4) out vec4 vLightClip; // NEAR light clip: xy at the true
                                          // fragment, z at the trunk axis
layout(location = 5) out vec3 vWorld;     // interpolated world pos (point lights)
layout(location = 6) out vec3 vAxisWorld; // trunk axis at this fragment's height
                                          // (far shadow z reference)

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv   = vec2(c.x + 0.5, c.y);
    vKind = iKind;
    vSeed = iSeed;
    vTint = unpackUnorm4x8(iTint);

    vec3 right = pc.camRight.xyz;
    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iHalfW * 2.0;
    float h = iHeight;
    vec3 world = iPos + right * (c.x * w) + up * (c.y * h);
    gl_Position = pc.mvp * vec4(world, 1.0);
    vWorld = world;
    // Per-fragment shadow RECEIVING without self-shadow acne: in light clip
    // space a shift along the light ray moves only z, never xy — so xy comes
    // from the true fragment position (the map texel it really covers) while
    // z comes from the trunk AXIS at the same height. The axis lies exactly
    // in the plane the sprite's own caster drew (shadow_bb.vert spans
    // lightRight × up through iPos), so self-comparison stays borderline-by-
    // construction — same as the old base sample — while neighbour and
    // terrain shadows cut the sprite at their true mathematical edge.
    // The WIDE level's matrix lives in the light SSBO (fragment stage), so
    // the axis POSITION travels instead and the fragment repeats the trick.
    vec3 axis = iPos + up * (c.y * h);
    vec4 fullClip = pc.lightMvp * vec4(world, 1.0);
    vec4 axisClip = pc.lightMvp * vec4(axis, 1.0);
    vLightClip = vec4(fullClip.xy, axisClip.z, fullClip.w);
    vAxisWorld = axis;
}
