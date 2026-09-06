#version 450
// WORLD-SPACE precipitation (particles-unified-matter, Inc D) — the successor
// of the screen-space precip.frag sheet. Every drop is a real point in the
// world: the vertex stage computes its position from gl_InstanceIndex + time
// (hash grid in a cylinder around the CAMERA), so drops are occluded by
// walls and hills through the ordinary depth test — no particle pool, no CPU
// sim, no buffers at all. The engine only picks the instance count
// (~kMaxDrops × precip01) and pushes the weather context.
//
// Kinds (SkyContext precipKind, same calendar law as before): 0 rain — fast
// streaks sheared along their own fall velocity (wind included); 1 snow —
// slow flakes with a lazy per-drop sway; 2 hail — fast hard pellets, barely
// deflected. A drop below the water plane dies (alpha 0): rain ends at the
// river's skin, not under it.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camPos;   // xyz = camera world position, w = time (s)
    vec4 camRight; // xyz = camera right (world),  w = precip01
    vec4 p0;       // x = kind, y = windX, z = windZ, w = storm flash01
    vec4 p1;       // x = water plane Y (m), y = sun lum, z = ambient lum
} pc;

layout(location = 0) out vec2 vUv;    // [-1,1]² across the drop quad
layout(location = 1) out vec4 vColor; // rgb lit tint + alpha
layout(location = 2) flat out float vKind;

float hashd(uint n) {
    n = (n ^ 61u) ^ (n >> 16);
    n = n + (n << 3);
    n = n ^ (n >> 4);
    n = n * 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x7fffffffu) / 2147483647.0;
}

const float kRadiusM = 24.0; // drop cylinder around the camera
const float kHeightM = 22.0; // fall column height (wraps toroidally in time)
const float kTopM    = 14.0; // column top above the camera eye

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vUv = c;

    uint i = uint(gl_InstanceIndex);
    float h1 = hashd(i * 3u + 1u);
    float h2 = hashd(i * 7u + 5u);
    float h3 = hashd(i * 13u + 11u);
    float h4 = hashd(i * 29u + 17u);

    int kind = int(pc.p0.x + 0.5);
    vKind = pc.p0.x;
    float time = pc.camPos.w;

    // Fall speed / drop dimensions per kind.
    float speed = (kind == 1) ? 1.4 : (kind == 2 ? 9.0 : 13.0);
    speed *= 0.85 + h4 * 0.3; // per-drop variety
    float halfW = (kind == 1) ? 0.040 : (kind == 2 ? 0.028 : 0.016);
    float halfL = (kind == 0) ? 0.24 : halfW; // rain is a streak, others dots

    // Column position: uniform disk around the camera (sqrt for area-uniform),
    // XZ FIXED to the camera so the volume follows the viewer.
    float r = sqrt(h1) * kRadiusM;
    float th = h2 * 6.28318531;
    float x = pc.camPos.x + cos(th) * r;
    float z = pc.camPos.z + sin(th) * r;

    // Fall: phase-offset toroidal wrap over the column height.
    float fall = fract((time * speed) / kHeightM + h3);
    float y = pc.camPos.y + kTopM - fall * kHeightM;

    // Wind: the same cloud wind that tilted the old screen sheet now bends
    // the PATH — horizontal drift grows with distance fallen. Snow drifts
    // freely and sways; hail barely deflects.
    float windK = (kind == 1) ? 1.6 : (kind == 2 ? 0.25 : 0.9);
    vec2 drift = vec2(pc.p0.y, pc.p0.z) * windK * fall * 2.0;
    x += drift.x;
    z += drift.y;
    if (kind == 1) {
        x += sin(time * 0.7 + h2 * 6.28) * 0.35;
        z += cos(time * 0.9 + h1 * 6.28) * 0.25;
    }

    // The quad: width along the camera's right axis; length along the drop's
    // own velocity (down + wind) so a gale visibly slants every streak.
    vec3 vel = normalize(vec3(pc.p0.y * windK * 2.0, -speed,
                              pc.p0.z * windK * 2.0));
    vec3 right = pc.camRight.xyz;
    vec3 world = vec3(x, y, z) + right * (c.x * halfW) + vel * (-c.y * halfL);

    // Lit tint: cool white scaled by the sky's light (night rain is dark),
    // silvered for a lightning flash instant. Alpha carries the intensity
    // ramp and dies below the water plane.
    float lum = clamp(pc.p1.z + pc.p1.y * 0.6, 0.05, 1.2)
                * (1.0 + pc.p0.w * 0.8);
    vec3 tint = (kind == 1) ? vec3(0.95, 0.97, 1.00) : vec3(0.70, 0.78, 0.90);
    float a = (kind == 1) ? 0.55 : (kind == 2 ? 0.50 : 0.34);
    a *= clamp(pc.camRight.w * 1.6, 0.0, 1.0); // gentle at drizzle
    if (y < pc.p1.x) a = 0.0;                  // under water: no drop
    vColor = vec4(tint * lum, a);

    gl_Position = pc.mvp * vec4(world, 1.0);
}
