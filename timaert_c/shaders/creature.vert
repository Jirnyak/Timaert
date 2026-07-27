#version 450
// Instanced procedural-creature billboard vertex stage. One draw for every
// subworld fauna entity: the quad corners come from gl_VertexIndex, the
// per-instance buffer supplies feet world position / size / body-plan archetype
// / seed / tint. Camera-facing (cylindrical) so creatures always face the
// viewer. Per-archetype aspect ratio lets a wide beast and a tall biped share
// one pass. uv.y = 0 at the feet, 1 at the crown.
layout(location = 0) in vec3  iPos;   // instance: feet world position
layout(location = 1) in float iSize;  // instance: overall scale (metres-ish)
layout(location = 2) in float iArch;  // instance: body-plan archetype (0..6)
layout(location = 3) in float iSeed;  // instance: per-instance variation seed
layout(location = 4) in vec3  iTint;  // instance: base colour (0..1)

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;  // xyz = camera right (world)
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;  // world -> light clip (shadow map)
} pc;

layout(location = 0) out vec2  vUv;
layout(location = 1) flat out float vArch;
layout(location = 2) flat out float vSeed;
layout(location = 3) flat out vec3  vTint;
layout(location = 4) flat out vec4  vLightClip; // feet in light space

// (width, height) billboard multipliers per body plan. Kept identical to
// shadow_creature.vert so the cast shadow matches the lit silhouette exactly.
vec2 archAspect(int a) {
    if (a == 0) return vec2(1.70, 1.15); // quadruped (wide, low)
    if (a == 1) return vec2(1.50, 1.05); // avian
    if (a == 2) return vec2(1.15, 1.50); // serpent (tall)
    if (a == 3) return vec2(1.25, 1.80); // biped
    if (a == 4) return vec2(1.10, 1.80); // undead
    if (a == 5) return vec2(1.80, 2.05); // hulk
    return vec2(0.95, 0.80);             // critter
}

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv   = vec2(c.x + 0.5, c.y); // feet at uv.y = 0 (procedural is shipping art)
    vArch = iArch;
    vSeed = iSeed;
    vTint = iTint;

    vec2 asp = archAspect(int(iArch + 0.5));
    vec3 right = pc.camRight.xyz;
    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * asp.x;
    float h = iSize * asp.y;
    vec3 world = iPos + right * (c.x * w) + up * (c.y * h);
    gl_Position = pc.mvp * vec4(world, 1.0);
    // Shadow coord at the feet (flat) so the whole billboard shades as a unit.
    vLightClip = pc.lightMvp * vec4(iPos, 1.0);
}
