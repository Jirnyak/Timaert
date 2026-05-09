// Torus geometry helpers — wraparound for the macroworld.
#pragma once
#include <cmath>

namespace sm {

inline int wrapi(int v, int size) {
    int m = v % size;
    return m < 0 ? m + size : m;
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
