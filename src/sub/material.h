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
#include <cmath>
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
        // Same ploughed field, furrows running north-south instead of
        // east-west. Appended so every existing id keeps its byte.
        TM_FieldV,
    };

    // Furrow orientation of a cell's ploughed field — the C++ twin of the
    // map's formula (shaders/macro.frag FT_Field branch: roadHash of
    // cell.x*127.1 + cell.y*311.7 + seed, with the macro renderer pushing
    // seed = 1.0). The map and the ground under your feet must plough the
    // same way; there is no mechanism holding them together but this comment
    // pair, so keep both sides in lockstep. Takes WRAPPED macro cell coords
    // (torus space — exactly what the map shader sees as pixel coords).
    // GPU float contraction can in principle flip a cell whose hash lands
    // within an ulp of 0.5 — cosmetic and vanishingly rare, accepted.
    inline bool field_furrows_vertical(int cellX, int cellY)
    {
        float n = float(cellX) * 127.1f + float(cellY) * 311.7f + 1.0f;
        n = n * 0.1031f;
        n -= std::floor(n);
        n *= n + 33.33f;
        n *= n + n;
        n -= std::floor(n);
        return n > 0.5f;
    }

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

    // Mountain ground by ALTITUDE — stone is for the PEAKS only. Below the
    // treeline band the massif is alive (grass, trees), so its ground reads
    // as meadow; through the band grass and stone dither out exactly as the
    // trees thin; above it the bare rock owns the summit. One band, three
    // consumers (tree scatter, 3D material, 2D map) — the same constants as
    // scatter_universal_trees' treeline.
    constexpr float kMtnGrassTopH = 0.72f;  // full grass below (treeline start)
    constexpr float kMtnRockBaseH = 0.92f;  // full rock above (treeline end)

    // Deterministic per-tile hash → [0,1). Keyed to ABSOLUTE tile coords so the
    // dither pattern is a property of the WORLD, not of the current 3×3 window
    // (the GPU seam shift relocates baked bytes — they must stay valid).
    //
    // In the header, and inline, because the material fill runs it once per
    // tile — a million times per cell — and a call across a translation unit
    // costs more than the hash inside it.
    inline float tile_hash01(long long ax, long long ay) {
        std::uint64_t h = std::uint64_t(ax) * 0x9E3779B97F4A7C15ull
                        ^ std::uint64_t(ay) * 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27; h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return float(h >> 40) * (1.0f / 16777216.0f);
    }

    // Where a height sits in the treeline band: <=0 all ground, >=1 all rock.
    inline float treeline_t(float hNorm) {
        return (hNorm - kMtnGrassTopH) / (kMtnRockBaseH - kMtnGrassTopH);
    }
    // THE treeline dither, in ONE place: inside the band, does this tile land
    // on stone? Both apply_mountain_treeline and any caller that has already
    // hoisted the constant parts out of its loop go through here, so the
    // pattern cannot fork.
    inline bool treeline_is_rock(float t, long long absX, long long absY) {
        return tile_hash01(absX * 7 + 3, absY * 7 - 5) < t;
    }

    // Per-structure shade wobble, as a property of the WORLD.
    //
    // Keyed to ABSOLUTE tile coords for the same reason the ground dither is:
    // window-relative keying is a seam defect. A crossing reindexes the
    // composite, every structure's coordinate moves by a whole cell, and if the
    // shade hangs off that coordinate then EVERY BUILDING IN VIEW changes
    // brightness at the instant the player steps over the boundary.
    //
    // Fixed point at 1/16 tile so two structures inside one tile still differ.
    inline float structure_shade(double absX, double absY) {
        const long long qx = (long long)(absX * 16.0 + (absX < 0 ? -0.5 : 0.5));
        const long long qy = (long long)(absY * 16.0 + (absY < 0 ? -0.5 : 0.5));
        return 0.86f + 0.20f * tile_hash01(qx, qy);
    }

    Biome apply_mountain_treeline(Biome picked, float hNorm,
                                  long long absX, long long absY);

} // namespace sm::sub
