// Procedural 64x64 RGBA8 tile atlas for the subworld. 9 biome grounds +
// water, generated once at startup, uploaded as a single 640x64 GL_TEXTURE_2D.
// Mirrors textures.ts. Pure CPU synthesis (no shader pass needed) so the
// 2D and 3D renderers can sample with a tile index.
#pragma once
#include <cstdint>
#include <vector>
#include "gl/gl.h"
#include "macro/biomes.h"

namespace sm::sub {

struct TileAtlas {
    static constexpr int kTile      = 64;
    static constexpr int kTileCount = 10;       // 9 biomes + water
    GLuint tex = 0;

    void init();
    void destroy();
    int  index_for(Biome b) const;              // tile column index
};

} // namespace sm::sub
