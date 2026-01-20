#pragma once

#include <cstdint>

inline float noise2D(int x, int y, uint32_t seed)
{
    uint32_t h = x * 374761393u + y * 668265263u + seed * 1442695041u;
    h ^= h >> 13;
    h *= 1274126177u;
    return (h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}


