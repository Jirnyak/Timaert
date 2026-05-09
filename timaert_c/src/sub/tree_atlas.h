// Subworld tree sprite atlas — bakes the macroworld procedural tree
// shader (game/tree-spawner.ts TREE_MAP_GLSL) into a single 2D texture
// at boot. Subworld billboards sample this atlas instead of running the
// per-pixel pixel-art logic per fragment.
//
// Atlas layout: kVariants columns × kTypes rows of kTileSize² RGBA8 tiles.
// Tree type rows (matching macro temperature dispatch):
//   0 = OAK      (temperate broadleaf)
//   1 = CHERRY   (sakura)
//   2 = BIRCH    (cool conifer/birch)
//   3 = AUTUMN   (warm broadleaf)
//   4 = PINE     (cold conifer)
//   5 = WILLOW   (warm temperate)
//   6 = JUNGLE   (tropics)
//
// `tree_type_for(biome, hash)` mirrors the macro temperature → tp dispatch
// using the biome enum (no temperature texture in subworld), so trees in
// each subworld cell match the species shown on the macro map.
#pragma once
#include <cstdint>
#include "gl/gl.h"
#include "macro/biomes.h"

namespace sm::sub {

struct TreeAtlas {
    static constexpr int kTileSize = 64;
    static constexpr int kTypes    = 7;
    static constexpr int kVariants = 8;
    GLuint tex = 0;
    int    width  = kTileSize * kVariants;
    int    height = kTileSize * kTypes;

    // One-shot bake at world boot.  Idempotent: re-uses `tex` if already
    // created.  Restores caller's GL state (FBO + viewport).
    void bake(std::uint32_t worldSeed);
    void destroy();
};

// Pick a tree species index (row in the atlas) appropriate for a biome.
// `hash` is a per-instance 0..1 noise sample so adjacent trees vary.
int tree_type_for(Biome b, float hash);

} // namespace sm::sub
