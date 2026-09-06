#version 450
// World-precipitation fragment stage (Inc D). The vertex stage placed the
// drop in the world and lit its tint; here only the SHAPE lives: rain is a
// soft vertical streak fading at both ends, snow a round flake, hail a hard
// pellet. Alpha-over blend, depth test on — a drop behind a wall or under a
// hill never reaches this stage at all.
layout(location = 0) in vec2 vUv;    // [-1,1]² across the drop quad
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in float vKind;

layout(location = 0) out vec4 outColor;

void main() {
    int kind = int(vKind + 0.5);
    float a;
    if (kind == 1) {
        // SNOW — soft round flake.
        float r = length(vUv);
        a = smoothstep(1.0, 0.35, r);
    } else if (kind == 2) {
        // HAIL — small hard pellet with a crisp edge.
        float r = length(vUv);
        a = smoothstep(0.9, 0.55, r);
    } else {
        // RAIN — thin streak: soft across (u), fading toward both ends (v).
        float across = 1.0 - abs(vUv.x);
        float along = 1.0 - abs(vUv.y);
        a = across * across * smoothstep(0.0, 0.45, along);
    }
    a *= vColor.a;
    if (a <= 0.01) discard;
    outColor = vec4(vColor.rgb, a);
}
