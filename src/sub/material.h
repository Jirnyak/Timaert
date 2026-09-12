// Ground material resolution for the subworld — CPU side of mesh.frag's
// materialBase().
// ----------------------------------------------------------------
// A tile's material id is either AUTHORED (roads, fields, rock, shore,
// water — explicit tile bytes, biome-independent, always crisp) or the
// BIOME GROUND fallback. The biome used for that fallback is NOT the flat
// per-cell biome: like the height manifold, it blends across the 3×3
// macro neighbourhood — `pick_ground_biome` bilinearly weights the owning
// cell's GROUND ring and DITHERS between the candidates with a hash keyed
// to ABSOLUTE tile coordinates. Near a cell border the two grounds
// interleave in a ~250-tile band whose mix follows the bilinear weight,
// so taiga fades into meadow the way foothills fade into plains — the
// straight "texture wall" at every subworld seam is gone.
//
// RING CONTRACT: the 9 entries are GROUND aliases, never Water — a flooded
// cell (river/lake/coast) enters the ring as its unflooded climate ground
// (map_data.h ground_biome; the manager's nbGround capture). Water TILES
// stay authored TILE_WATER, so real water never consults the dither; what
// the alias buys is the flooded cell's DRY margin: its banks paint as the
// land their climate says, blended like any land↔land pair. The old
// Water-entry handling (zero + renormalise + first-land-in-scan-order
// fallback) painted the banks with the ring's NW-most land and drew
// razor-straight walls mid-cell and between water cells — the ground-band
// defect of 2026-08-29.
//
// Determinism / seam contract: the pick is a pure function of
// (owning cell's ground ring, local tile coords, absolute tile coords).
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

    // The biome ground under tile (lx, ly) of the cell whose 3×3 GROUND
    // ring is `nbBiome` (row-major, owner at index 4; see the RING CONTRACT
    // above — entries are ground aliases, never Water) and whose top-left
    // tile sits at absolute tile coords (absX0, absY0).
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
    // consumers (tree scatter, 3D material, 2D map) — this is THE one home:
    // scatter_universal_trees (base_generator.cpp) reads these constants.
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

    // ── THE GROUND-BOUNDARY LAW ──────────────────────────────────────────
    //
    // EVERY boundary the micro world draws between two grounds goes through
    // this one field: biome against biome across a cell border
    // (pick_ground_biome_axis) and grass against stone through the treeline
    // (treeline_is_rock). A boundary is always the same act — two claims are
    // close, and something has to say which one owns this square metre — so
    // there is one thing that says it.
    //
    // It is CORRELATED, and that is the whole point. It used to be
    // tile_hash01: an independent coin flipped per square metre, which turns
    // a boundary into PEPPER — a metre of stone, a metre of grass, a metre of
    // stone — where nature puts patches. The owner photographed it on a
    // mountainside (2026-09-12) and asked the right question: is that the
    // mountain's defect or everyone's? It was everyone's. Two call sites, one
    // mechanism, copied by hand rather than shared; the treeline's own comment
    // said outright that it used "the same style of hash the seam dither
    // uses". The biome one simply hid: it mixes two green grounds across a
    // ~250-tile band, so its pepper is a metre of one green among another.
    //
    // kGroundPatchTiles = 24 m is the correlation length — a stand of scrub, a
    // spill of scree, the scale at which ground actually changes its mind. The
    // second octave at a third of it gives a patch its fringe.
    //
    // What is NOT swept in: structure_shade below. That is a per-OBJECT wobble,
    // not a boundary — correlating it would paint neighbouring houses the same
    // brightness, which is the opposite of its job. It keeps the coin.
    //
    // The seam contract is untouched: like the hash, this is a pure function of
    // ABSOLUTE tile coordinates, so one physical metre answers the same before
    // and after a window recentre. Mean stays 1/2, so the AMOUNT of each ground
    // in a band is what it always was — only its arrangement changes.
    constexpr long long kGroundPatchTiles = 24;

    // Value noise over the tile hash: one lattice cell per kGroundPatchTiles,
    // smoothstep between. Integer floor-division that also works below zero —
    // absolute tile coordinates are signed and the torus does reach there.
    inline float ground_field01(long long ax, long long ay, long long cell) {
        const long long ix = (ax >= 0 ? ax : ax - cell + 1) / cell;
        const long long iy = (ay >= 0 ? ay : ay - cell + 1) / cell;
        const float fx = float(ax - ix * cell) / float(cell);
        const float fy = float(ay - iy * cell) / float(cell);
        const float sx = fx * fx * (3.0f - 2.0f * fx);
        const float sy = fy * fy * (3.0f - 2.0f * fy);
        const float a = tile_hash01(ix, iy), b = tile_hash01(ix + 1, iy);
        const float c = tile_hash01(ix, iy + 1), d = tile_hash01(ix + 1, iy + 1);
        const float t0 = a + (b - a) * sx;
        const float t1 = c + (d - c) * sx;
        return t0 + (t1 - t0) * sy;
    }

    inline float ground_dither01(long long ax, long long ay) {
        return ground_field01(ax, ay, kGroundPatchTiles) * 0.72f
             + ground_field01(ax + 8191, ay - 5779,
                              kGroundPatchTiles / 3) * 0.28f;
    }

    // Where a height sits in the treeline band: <=0 all ground, >=1 all rock.
    inline float treeline_t(float hNorm) {
        return (hNorm - kMtnGrassTopH) / (kMtnRockBaseH - kMtnGrassTopH);
    }
    // THE treeline dither, in ONE place: inside the band, does this tile land
    // on stone? Both apply_mountain_treeline and any caller that has already
    // hoisted the constant parts out of its loop go through here, so the
    // pattern cannot fork.
    //
    // Decorrelated from the biome boundary by an OFFSET, never by scaling the
    // coordinate. Scaling was right for a coin (it only reshuffled it) and is
    // wrong for a field: multiplying by 7, as this line used to, would divide
    // the patch down to three metres and hand the pepper straight back.
    inline bool treeline_is_rock(float t, long long absX, long long absY) {
        return ground_dither01(absX + 9973, absY - 7919) < t;
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
