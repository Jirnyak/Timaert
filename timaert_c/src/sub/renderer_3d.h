// Subworld 3D first-person renderer. Builds a low-resolution terrain mesh
// (kMeshDim^2 quads) sampled from the seamless heightmap, shades with a
// 4-band quantised NdotL using the sun computed by lighting.h, and draws a
// translucent water plane at biome water level. Mirrors renderer-3d.ts in
// compact form. Re-builds the mesh whenever the manager re-centres.
#pragma once
#include <cstdint>
#include <vector>
#include "gl/gl.h"
#include "sub/seamless_manager.h"
#include "sub/camera.h"
#include "sub/textures.h"
#include "sub/tree_atlas.h"

namespace sm { struct WorldTime; }

namespace sm::sub {

struct Renderer3D {
    static constexpr int kMeshDim = 192;     // quads per side
    // Cached heightmap in metres at vertex grid resolution (Nv × Nv).
    // Used by sample_height_m() so the engine can place the camera above
    // the actual terrain without keeping a second copy.
    std::vector<float> heightVtxM;
    GLuint prog       = 0;
    GLuint vao        = 0;
    GLuint vboPos     = 0;
    GLuint ibo        = 0;
    GLsizei indexCount = 0;

    // Water program (separate, alpha-blended).
    GLuint waterProg  = 0;
    GLuint waterVao   = 0;
    GLuint waterVbo   = 0;

    // Procedural per-biome ground texture atlas (TS-faithful: gen_tundra,
    // gen_desert, ... per biome). Sampled in the terrain shader and
    // bilinearly blended across the 3×3 cell grid for natural transitions.
    TileAtlas atlas;

    // Road mask: R8 texture covering the full 3×3 composite tile grid.
    // Each pixel = 255 where the underlying tile is TILE_ROAD, 0 else.
    // Sampled with LINEAR filtering in the terrain shader to paint a
    // warm dirt-tan road colour on top of the biome ground. Rebuilt on
    // every cell shift via upload().
    GLuint roadMask = 0;

    // Billboard pass — instanced quads for trees only. Samples the
    // baked TreeAtlas (macro-style 7 species × 8 variants pixel-art).
    // Per-instance attributes: worldPos.xyz, scale, height, typeIdx,
    // variantIdx — typeIdx selects the species row, variantIdx the
    // column. No more procedural shader, no more rocks (mountain relief
    // is conveyed by the slope-driven rock/snow overlay on the terrain).
    TreeAtlas treeAtlas;
    GLuint billProg     = 0;
    GLuint billVao      = 0;
    GLuint billQuadVbo  = 0;
    GLuint billInstVbo  = 0;
    GLsizei billCount   = 0;

    void init();
    void destroy();
    void upload(const SeamlessSubworldManager& mgr);
    void render(int viewW, int viewH,
                const Camera& cam,
                const WorldTime& time,
                float waterLevel01,
                const SeamlessSubworldManager* mgr = nullptr);

    // Sample the uploaded heightmap (in metres) at composite tile coords
    // (0..kFullSize). Returns 0 if no mesh uploaded yet.
    float sample_height_m(float tileX, float tileY) const;
    // Convert composite tile coords to world-metre coords.
    static void tile_to_world(float tileX, float tileY,
                              float& wx, float& wz);
};

} // namespace sm::sub
