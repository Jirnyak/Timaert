// Torus geometry helpers — wraparound for the macroworld.
#pragma once
#include <cmath>
#include <cstdint>

namespace sm {

// THE wrap. Six copies of this function lived in macro/ — `wrap_index` twice,
// `wrap_cell`, `wrap_coord`, `pf_wrap` — one per author who needed a torus and
// did not look for the one that existed. That is six chances to forget the
// modulo on the day a new field is written, and a forgotten modulo IS a seam
// (CANON.md S1: seamlessness is a property of construction).
//
// It takes the strongest body of the six: a 64-bit intermediate, so a
// coordinate far outside the map cannot overflow on the way in, and a
// fail-closed answer for a degenerate size, because `v % 0` is undefined
// behaviour and a world with no width is a caller's bug, not a crash site.
inline int wrapi(int v, int size) {
    if (size <= 0) return 0;
    std::int64_t m = std::int64_t(v) % std::int64_t(size);
    if (m < 0) m += size;
    return int(m);
}

inline float wrapf(float v, float size) {
    float m = std::fmod(v, size);
    return m < 0 ? m + size : m;
}

inline float torus_dist(float ax, float ay, float bx, float by, float w, float h) {
    float dx = std::fabs(ax - bx);
    float dy = std::fabs(ay - by);
    if (dx > w * 0.5f) dx = w - dx;
    if (dy > h * 0.5f) dy = h - dy;
    return std::sqrt(dx * dx + dy * dy);
}

inline float torus_dist_sq(float ax, float ay, float bx, float by, float w, float h) {
    float dx = std::fabs(ax - bx);
    float dy = std::fabs(ay - by);
    if (dx > w * 0.5f) dx = w - dx;
    if (dy > h * 0.5f) dy = h - dy;
    return dx * dx + dy * dy;
}

// Unit bearing (direction) from (fx,fy) to (tx,ty) on a w×h torus, using the
// shortest wrapped delta. Writes the normalized direction into ux,uy and
// returns true; when the two points coincide it writes {0,0} and returns
// false (no meaningful bearing). Uses the same integer half-extent wrap as
// torus_dist_sq so bearings agree with the distance metric.
inline bool torus_bearing(int fx, int fy, int tx, int ty, int w, int h,
                          float& ux, float& uy) {
    int dx = tx - fx;
    int dy = ty - fy;
    if (dx >  w / 2) dx -= w; else if (dx < -w / 2) dx += w;
    if (dy >  h / 2) dy -= h; else if (dy < -h / 2) dy += h;
    const float len = std::sqrt(float(dx) * float(dx) + float(dy) * float(dy));
    if (len < 1e-6f) { ux = 0.0f; uy = 0.0f; return false; }
    ux = float(dx) / len;
    uy = float(dy) / len;
    return true;
}

// True when the bearing f→b is a shallow, SAME-direction fan off the bearing
// f→a — i.e. the two roads leaving f point nearly the same way, so a second
// road to b would read as a doubled diagonal of the road to a. Opposite
// bearings (dot < 0, a genuine alternate route) and coincident points return
// false. `cosThreshold` is cos(min separation angle): closer to 1 prunes only
// the most parallel fans, closer to 0 prunes wider ones.
inline bool torus_bearings_parallel(int fx, int fy, int ax, int ay,
                                     int bx, int by, int w, int h,
                                     float cosThreshold) {
    float ux, uy, vx, vy;
    if (!torus_bearing(fx, fy, ax, ay, w, h, ux, uy)) return false;
    if (!torus_bearing(fx, fy, bx, by, w, h, vx, vy)) return false;
    return (ux * vx + uy * vy) > cosThreshold;
}

struct Step { int nx, ny; };
inline Step torus_step_toward(int fx, int fy, int tx, int ty, int w, int h) {
    int dx = tx - fx;
    int dy = ty - fy;
    if (dx >  w / 2) dx -= w; else if (dx < -w / 2) dx += w;
    if (dy >  h / 2) dy -= h; else if (dy < -h / 2) dy += h;
    int nx = fx + (dx == 0 ? 0 : (dx > 0 ? 1 : -1));
    int ny = fy + (dy == 0 ? 0 : (dy > 0 ? 1 : -1));
    return {wrapi(nx, w), wrapi(ny, h)};
}

} // namespace sm
