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
#include <array>
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
    // THE BULK FORM, and the body the one-shot above delegates to: the caller
    // brings the boundary field walked along its row (GroundDitherRow below)
    // plus the tile's absolute x.
    //
    // It takes the ROW, not a float, on purpose. Deep inside a cell all four
    // ring corners agree and the answer needs no field at all — the common
    // case — and the original coin was flipped lazily, after that early-out.
    // Handing a value in made it EAGER and doubled the material fill
    // (19.4 → 39.1 ms over the 9.4M tiles of a full build, measured). A field
    // must be drawn exactly where the coin was flipped: after the early-out,
    // never before it.
    struct GroundDitherRow;
    Biome pick_ground_biome_axis(const Biome nbBiome[9],
                                 const GroundAxis &ax, const GroundAxis &ay,
                                 GroundDitherRow &row, long long absX);

    // THE FOUR GROUNDS A TILE BLENDS, and whether they are all the same one.
    //
    // A row of a cell crosses at most TWO axis spans (ground_axis_for maps a
    // local coordinate to i0 ∈ {0,1}), so along a row these four are constant
    // over long stretches — and where they are all equal the pick has no work
    // to do at all. Naming them lets the million-tile fill hoist them out of
    // its inner loop without copying one line of the law: it asks here, once
    // per span, and asks the pick below per tile only when `uniform` is false.
    struct GroundCorners
    {
        Biome b00, b10, b01, b11;
        bool uniform; // all four the same ground: the answer needs no field
    };
    GroundCorners ground_corners(const Biome nbBiome[9],
                                 const GroundAxis &ax, const GroundAxis &ay);
    // The pick given corners already in hand. pick_ground_biome_axis is this
    // with ground_corners() called on the spot — one body, two entry points.
    Biome pick_ground_biome_corners(const GroundCorners &c, float fx, float fy,
                                    GroundDitherRow &row, long long absX);

    // THE GROUND OF A BIOME, tabulated. For a tile that is NOT authored,
    // terrain_material_for's answer depends on the biome ALONE — the tile
    // byte only chooses whether the authored branch fires. So the fill needs
    // no per-tile switch for it: eleven bytes, built from the door itself so
    // the table cannot drift away from it.
    const std::uint8_t *biome_ground_materials(); // [11], indexed by Biome

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
    // The bilinear and the smoothstep in ONE place each, so the one-shot and
    // the row walker below are the same arithmetic and not two copies that
    // agree by inspection. (They still cannot agree bit for bit: this TU ships
    // with -ffast-math, so each inlined site contracts its own multiply-add
    // and the two part company at ~1e-7 — the same verdict the height
    // self-check already recorded. What must agree is the DECISION, and
    // material_seam_test sweeps every threshold one can turn on.)
    inline float ground_smooth(float f) { return f * f * (3.0f - 2.0f * f); }
    inline float ground_lerp4(float a, float b, float c, float d,
                              float sx, float sy) {
        const float t0 = a + (b - a) * sx;
        const float t1 = c + (d - c) * sx;
        return t0 + (t1 - t0) * sy;
    }

    inline float ground_field01(long long ax, long long ay, long long cell) {
        const long long ix = (ax >= 0 ? ax : ax - cell + 1) / cell;
        const long long iy = (ay >= 0 ? ay : ay - cell + 1) / cell;
        const float sx = ground_smooth(float(ax - ix * cell) / float(cell));
        const float sy = ground_smooth(float(ay - iy * cell) / float(cell));
        return ground_lerp4(tile_hash01(ix, iy), tile_hash01(ix + 1, iy),
                            tile_hash01(ix, iy + 1),
                            tile_hash01(ix + 1, iy + 1), sx, sy);
    }

    // ONE octave, not two. A second, finer octave would fray the patch's
    // edge — and mesh.frag already frays every boundary it draws, by up to
    // edge_m metres, with a field of its own (ground.md, "the joint"). Paying
    // for the same fringe twice would cost the material fill a second field
    // per tile, and the fill runs 9.4 million times on a full build.
    inline float ground_dither01(long long ax, long long ay) {
        return ground_field01(ax, ay, kGroundPatchTiles);
    }

    // ── THE SAME LAW, WALKED BY THE ROW ───────────────────────────────────
    // This is how a FIELD comes out cheaper per tile than the coin it
    // replaced, and it had to, because the material fill runs 9.4 million
    // times on a full build and the seam's load time is the one number in
    // this game allowed to move in exactly one direction (owner, 2026-09-12).
    //
    // A coin must be flipped per tile: a 64-bit mix, every time. A field is
    // CONSTANT over its lattice, so walking a row east:
    //   • the four corner hashes refresh once per lattice column — once per
    //     24 tiles for the coarse octave, once per 8 for the fine one;
    //   • the y smoothstep is a row constant;
    //   • the x smoothstep depends only on the offset inside the column, so
    //     the row builds a table of `cell` entries once and indexes it;
    //   • east is an increment, so no division falls per tile.
    // Three lerps and a table read is what remains.
    //
    // THE TRAP, twice paid for: hoisting turns CONDITIONAL work
    // unconditional. A first version filled whole rows into buffers, which
    // drew the treeline's field for every tile in cells that have no band at
    // all; a second handed the pick a ready float, which drew the seam's
    // field for every tile deep inside a cell where all four ring corners
    // agree and no field is needed. Both measured SLOWER than the coin. Hence
    // a walker with `at()`: the caller asks exactly where it used to ask.
    // The x smoothstep depends only on a tile's offset inside the lattice
    // column, and the lattice has ONE size in this world — so the table is
    // built once per process, not once per row. Built per row it cost 24
    // divisions × 1024 rows × 9 cells, which measured as 1.2 ms of the
    // material fill: hoisting is only hoisting if it leaves the loop for
    // good.
    inline const std::array<float, std::size_t(kGroundPatchTiles)>
        kGroundSmoothX = [] {
            std::array<float, std::size_t(kGroundPatchTiles)> t{};
            for (int i = 0; i < int(kGroundPatchTiles); ++i)
                t[std::size_t(i)] =
                    ground_smooth(float(i) / float(kGroundPatchTiles));
            return t;
        }();

    // The boundary field for ONE ROW, walked east. `begin` once per row,
    // `at` exactly where the coin used to be flipped — and nowhere else,
    // because above and below a band, and deep inside a cell, the answer
    // needs no field at all.
    //
    // WHAT WAS TRIED AND LOST, so nobody spends the afternoon again (all
    // measured on the material fill of a full 9-cell build, 9.4M tiles,
    // against the coin's 19.4 ms):
    //   • the field drawn per tile, eagerly, by handing the pick a ready
    //     float — 39.1 ms. Hoisting turned the early-out's laziness into
    //     eager work: the single worst mistake of the whole exercise.
    //   • whole rows pre-filled into buffers — ~39 ms, same disease: cells
    //     with no treeline band paid for a treeline field.
    //   • a second, finer octave — 25.5 ms. mesh.frag already frays every
    //     boundary it draws; paying for the fringe twice is what it cost.
    //   • the lattice hashed once per CELL (44² corners, no hash in the
    //     loop) — 27.3 ms. Hashes were never the cost; an 8 KB table in the
    //     hot loop is.
    //   • the material tabulated for every ring biome — 22.3 ms. The two
    //     switches are cheaper than the cache line.
    // What is left — a row walker, one octave, a shared smoothstep table,
    // and asking only where the coin was flipped — costs 20.9 ms.
    struct GroundDitherRow {
        long long ix = 0, iy = 0, nextAx = 0;
        int off = 0;
        bool primed = false;
        float a = 0, b = 0, c = 0, d = 0, sy = 0;

        void refresh() {
            a = tile_hash01(ix, iy);
            b = tile_hash01(ix + 1, iy);
            c = tile_hash01(ix, iy + 1);
            d = tile_hash01(ix + 1, iy + 1);
        }
        void begin(long long ay) {
            constexpr long long cell = kGroundPatchTiles;
            iy = (ay >= 0 ? ay : ay - cell + 1) / cell;
            sy = ground_smooth(float(ay - iy * cell) / float(cell));
            primed = false;
        }
        float at(long long ax) {
            constexpr long long cell = kGroundPatchTiles;
            if (primed && ax == nextAx) {
                if (++off == int(cell)) { off = 0; ++ix; refresh(); }
            } else {
                ix = (ax >= 0 ? ax : ax - cell + 1) / cell;
                off = int(ax - ix * cell);
                refresh();
                primed = true;
            }
            nextAx = ax + 1;
            return ground_lerp4(a, b, c, d,
                                kGroundSmoothX[std::size_t(off)], sy);
        }
    };

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
    // Row form of the same line: a caller that has already drawn the field
    // (GroundDitherRow at the treeline's offset) hands the value in. The
    // comparison stays HERE, so the formula keeps exactly one home.
    inline bool treeline_is_rock_at(float t, float r) { return r < t; }

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
    // The same law for a caller that already drew the boundary field for this
    // tile. The one-shot above is this one with the field drawn on the spot,
    // so the million-tile fill copies NO law of its own — it only brings its
    // own field, and only for the tiles inside the band (treeline_t below is
    // the gate, and it is THE band function, not a copy).
    Biome apply_mountain_treeline_at(Biome picked, float hNorm, float dither);
    // ...and the bulk form, which takes the ROW for the same reason the pick
    // does: above and below the band the answer needs no field, so the field
    // must not be drawn there. The caller brings its row and its absolute x
    // and copies no part of the law — not even the band test.
    Biome apply_mountain_treeline_row(Biome picked, float hNorm,
                                      GroundDitherRow &row, long long absX);

} // namespace sm::sub
