// Tiny math primitives. POD, header-only, no exceptions.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace sm {

struct vec2 { float x, y; };
struct vec3 { float x, y, z; };
struct vec4 { float x, y, z, w; };
struct ivec2 { int x, y; };

inline vec2 v2(float x, float y) { return {x, y}; }
inline vec3 v3(float x, float y, float z) { return {x, y, z}; }
inline vec4 v4(float x, float y, float z, float w) { return {x, y, z, w}; }

inline vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline vec2 operator*(vec2 a, float s) { return {a.x * s, a.y * s}; }
inline vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(vec3 v) {
    float l = length(v);
    return l > 1e-6f ? v * (1.0f / l) : vec3{0, 0, 0};
}

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep01(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }

// Column-major 4x4 matrix.
struct mat4 { float m[16]; };

inline mat4 mat4_identity() {
    mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r;
}

inline mat4 mat4_mul(const mat4& a, const mat4& b) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i * 4 + j] =
                a.m[0 * 4 + j] * b.m[i * 4 + 0] +
                a.m[1 * 4 + j] * b.m[i * 4 + 1] +
                a.m[2 * 4 + j] * b.m[i * 4 + 2] +
                a.m[3 * 4 + j] * b.m[i * 4 + 3];
    return r;
}

inline mat4 mat4_perspective(float fovyRad, float aspect, float zn, float zf) {
    mat4 r{};
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

inline mat4 mat4_lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r = mat4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    return r;
}

inline mat4 mat4_ortho(float l, float r, float b, float t, float zn, float zf) {
    mat4 m{};
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (zf - zn);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(zf + zn) / (zf - zn);
    m.m[15] = 1.0f;
    return m;
}

// TS-faithful post-multiply helpers (M' = M * T / S / Ry).
// Matches subworld/math3d.ts mat4Translate / mat4Scale / mat4RotateY.
inline mat4 mat4_translate(const mat4& m, float x, float y, float z) {
    mat4 r = m;
    r.m[12] += m.m[0] * x + m.m[4] * y + m.m[8]  * z;
    r.m[13] += m.m[1] * x + m.m[5] * y + m.m[9]  * z;
    r.m[14] += m.m[2] * x + m.m[6] * y + m.m[10] * z;
    return r;
}

inline mat4 mat4_scale(const mat4& m, float sx, float sy, float sz) {
    mat4 r = m;
    r.m[0] *= sx; r.m[1] *= sx; r.m[2] *= sx;
    r.m[4] *= sy; r.m[5] *= sy; r.m[6] *= sy;
    r.m[8] *= sz; r.m[9] *= sz; r.m[10] *= sz;
    return r;
}

inline mat4 mat4_rotate_y(const mat4& m, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    mat4 r = m;
    const float m0 = m.m[0],  m1 = m.m[1],  m2 = m.m[2];
    const float m8 = m.m[8],  m9 = m.m[9],  m10 = m.m[10];
    r.m[0]  = m0 * c + m8  * s;
    r.m[1]  = m1 * c + m9  * s;
    r.m[2]  = m2 * c + m10 * s;
    r.m[8]  = m8  * c - m0 * s;
    r.m[9]  = m9  * c - m1 * s;
    r.m[10] = m10 * c - m2 * s;
    return r;
}

} // namespace sm
