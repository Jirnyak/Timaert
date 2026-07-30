// Ground material resolution for the subworld — CPU side of mesh.frag's
// materialBase().
// ----------------------------------------------------------------
// A tile's material id is either AUTHORED (roads, fields, rock, shore,
// water — explicit tile bytes, biome-independent, always crisp) or the
// BIOME GROUND fallback. The biome used for that fallback is NOT the flat
// per-cell biome: like the height manifold, it blends across the 3×3
// macro neighbourhood — `pick_ground_biome` bilinearly weights the owning
// cell's biome ring and DITHERS between the candidates with a hash keyed
// to ABSOLUTE tile coordinates. Near a cell border the two grounds
// interleave in a ~250-tile band whose mix follows the bilinear weight,
// so taiga fades into meadow the way foothills fade into plains — the
// straight "texture wall" at every subworld seam is gone.
//
// Determinism / seam contract: the pick is a pure function of
// (owning cell's biome ring, local tile coords, absolute tile coords).
// All three are window-independent — the ring is captured from the macro
// resolver when the cell is generated and travels with it — so the GPU
// toroidal shift can relocate baked material bytes across a re-centre and
// a from-scratch recompute still matches byte-for-byte (seam selfcheck).
#pragma once
#include <cstdint>
#include "sub/map_data.h"

namespace sm::sub
{

    // Material ids sampled per-fragment by mesh.frag (materialBase must
    // stay in sync).
    enum TerrainMaterial : std::uint8_t
    {
        TM_Tundra = 0, TM_Taiga, TM_Snow, TM_Valley, TM_Meadow,
        TM_Swamp, TM_Desert, TM_Steppe, TM_Tropics,
        TM_Field, TM_Shore, TM_Rock, TM_Road, TM_Water,
    };

    // Authored tiles carry their material regardless of biome; everything
    // else falls back to the biome ground. Pure (tile, biome) → id.
    float terrain_material_for(std::uint8_t tile, Biome biome);

    // True when `tile`'s material ignores the biome (the authored branch
    // of terrain_material_for) — such tiles never dither.
    bool material_is_authored(std::uint8_t tile);

    // The biome ground under tile (lx, ly) of the cell whose 3×3 biome
    // ring is `nbBiome` (row-major, owner at index 4) and whose top-left
    // tile sits at absolute tile coords (absX0, absY0). Water neighbours
    // never bleed onto land (a grass bank must not speckle into "water
    // bed"); a Water OWNER returns Water untouched (its tiles are authored
    // TILE_WATER anyway).
    Biome pick_ground_biome(const Biome nbBiome[9],
                            int lx, int ly, int cellSize,
                            long long absX0, long long absY0);

    // Bulk form for the renderer's 1024²-tile fills: the bilinear weights are
    // separable, so precompute one axis table per dimension (i0/i1 = the two
    // 3×3 columns-or-rows a coordinate blends, f = its sharpened fraction)
    // and resolve each tile from two table entries. pick_ground_biome() is
    // the one-shot reference built on the same axis math — material_seam_test
    // locks their equivalence.
    struct GroundAxis
    {
        std::uint8_t i0, i1;
        float f;
    };
    void ground_axis_table(int cellSize, GroundAxis *out); // cellSize entries
    Biome pick_ground_biome_axis(const Biome nbBiome[9],
                                 const GroundAxis &ax, const GroundAxis &ay,
                                 long long absX, long long absY);

} // namespace sm::sub
