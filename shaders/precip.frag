#version 450
// Screen-space precipitation — the SECOND fullscreen pass of the atmosphere
// submodule. sky.frag is the backdrop drawn BEFORE the world; this sheet of
// weather is drawn AFTER it (last in the main pass, depth off, alpha over),
// falling between the camera and everything it sees. Pure shader, no
// particles, no textures: three hash-grid depth layers give cheap parallax.
//
// Context only (SkyContext → PrecipPush): intensity and KIND are the
// calendar's weather (sub/sky.h weather_at — winter snow, summer rain,
// spring hail, owner ruling), the streak tilt is the cloud wind projected
// onto the camera. When precip01 == 0 the renderer skips the draw entirely,
// so the pass is provably inert on a dry day.
layout(push_constant) uniform Push {
    vec4 p0; // x=resX y=resY z=time(sec) w=precip01
    vec4 p1; // x=kind (0 rain, 1 snow, 2 hail) y=tilt z=flash01 w=reserved
} pc;

layout(location = 0) out vec4 outColor;

float ph(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2  res    = pc.p0.xy;
    float time   = pc.p0.z;
    float amount = pc.p0.w;
    int   kind   = int(pc.p1.x + 0.5);
    float tilt   = pc.p1.y;

    // Aspect-true units: y spans [0,1], x spans the aspect ratio, so drops
    // are the same size on any window.
    vec2 uv = gl_FragCoord.xy / res.y;

    float a = 0.0;
    vec3  col = vec3(0.8, 0.85, 0.95);

    // Three layers, nearest first: nearer = larger, faster, more opaque.
    for (int L = 0; L < 3; ++L) {
        float depth = 1.0 + float(L) * 0.9;
        float seed  = float(L) * 61.7;

        if (kind == 0) {
            // RAIN — tall narrow cells scrolling fast; streaks shear with
            // the wind so a gale visibly drives the rain sideways.
            vec2 p = uv;
            p.x += uv.y * tilt;
            p = p * vec2(26.0, 3.2) * depth;
            p.y += time * 7.0 / depth;
            p.x += seed;
            vec2 cell = floor(p);
            vec2 f = fract(p);
            if (ph(cell) < amount * 0.40) {
                float streak = smoothstep(0.16, 0.02, abs(f.x - 0.5))
                             * smoothstep(0.00, 0.20, f.y)
                             * smoothstep(1.00, 0.60, f.y);
                a += streak * 0.16 / depth;
                col = vec3(0.65, 0.72, 0.85);
            }
        } else if (kind == 1) {
            // SNOW — slow round flakes with a lazy shared sway per layer.
            vec2 p = uv;
            p.x += sin(time * 0.7 + float(L) * 2.1) * 0.04 + uv.y * tilt * 0.3;
            p = p * 18.0 * depth;
            p.y += time * 0.9 / depth;
            p.x += seed;
            vec2 cell = floor(p);
            vec2 f = fract(p);
            if (ph(cell) < amount * 0.35) {
                vec2 c = vec2(0.30 + 0.40 * ph(cell + 7.7),
                              0.30 + 0.40 * ph(cell + 3.3));
                float d = length(f - c);
                float r = 0.10 + 0.08 * ph(cell + 5.1);
                a += smoothstep(r, r * 0.25, d) * 0.40 / depth;
                col = vec3(0.95, 0.97, 1.00);
            }
        } else {
            // HAIL — small hard fast pellets, barely deflected by wind.
            vec2 p = uv;
            p.x += uv.y * tilt * 0.5;
            p = p * 22.0 * depth;
            p.y += time * 4.5 / depth;
            p.x += seed;
            vec2 cell = floor(p);
            vec2 f = fract(p);
            if (ph(cell) < amount * 0.30) {
                vec2 c = vec2(0.35 + 0.30 * ph(cell + 7.7),
                              0.35 + 0.30 * ph(cell + 3.3));
                float d = length(f - c);
                a += smoothstep(0.09, 0.02, d) * 0.45 / depth;
                col = vec3(0.90, 0.95, 1.00);
            }
        }
    }

    // A lightning flash silvers the falling sheet for its instant.
    a *= 1.0 + pc.p1.z * 0.8;

    outColor = vec4(col, clamp(a, 0.0, 0.85));
}
