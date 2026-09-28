// THE WINDOW'S VERTICAL TRUTH — see sub/height.h for the law it serves.
//
// Everything here moved down out of `Renderer3DVk::upload`, unchanged in
// arithmetic: the box average, the toroidal slide on a seam crossing, the
// flat-cell shortcut and the bilinear sample are the same numbers the mesh has
// always been built from. What changed is WHO OWNS THEM. The renderer used to
// hold the grid and the simulation reached up into it fifteen times; now the
// world holds it and the renderer reads down.
#include "sub/height.h"

#include "sub/seamless_manager.h"

#include <cstring>

namespace sm::sub
{

void SubworldHeightField::resample_block(const float* hm,
                                         int x0, int x1, int y0, int y1)
{
    // A vertex stands on composite tile (x * step) and averages the tiles
    // within ±half of it: the mesh is a 16-tile-per-quad reduction of a
    // 3072² heightmap, and taking one tile per vertex instead of the mean
    // would make the drawn surface — and every body standing on it — jitter
    // with whichever single tile happened to land under the vertex.
    constexpr int step = kHeightQuadTiles;
    constexpr int half = step / 2;
    for (int y = y0; y <= y1; ++y) {
        const int cy = std::min(kFullSize - 1, y * step);
        const int sy0 = std::max(0, cy - half);
        const int sy1 = std::min(kFullSize - 1, cy + half);
        for (int x = x0; x <= x1; ++x) {
            const int cx = std::min(kFullSize - 1, x * step);
            const int sx0 = std::max(0, cx - half);
            const int sx1 = std::min(kFullSize - 1, cx + half);
            float sum = 0.0f;
            int count = 0;
            for (int sy = sy0; sy <= sy1; ++sy) {
                const auto row = std::size_t(sy) * kFullSize;
                for (int sx = sx0; sx <= sx1; ++sx) {
                    sum += hm[row + std::size_t(sx)];
                    ++count;
                }
            }
            m_[std::size_t(y) * kHeightVerts + std::size_t(x)] =
                (count > 0 ? sum / float(count) : 0.0f) * kHeightScaleM;
        }
    }
}

void SubworldHeightField::rebuild_from(const float* compositeHeight)
{
    resample_block(compositeHeight, 0, kHeightVerts - 1, 0, kHeightVerts - 1);
    recompute_extent();
    built_ = true;
}

void SubworldHeightField::recompute_extent()
{
    // The window's extent, refreshed with its content: the flight ceiling
    // reads the max (sub/height.h), the shadow volume's vertical fit reads
    // both. One pass over ~37k floats, negligible next to the resample.
    maxM_ = 0.0f;
    minM_ = 1.0e30f;
    for (const float h : m_) {
        if (h > maxM_) maxM_ = h;
        if (h < minM_) minM_ = h;
    }
    if (minM_ > maxM_) minM_ = 0.0f;
}

void SubworldHeightField::clear()
{
    built_ = false;
    minM_ = 0.0f;
    maxM_ = 0.0f;
    std::memset(m_, 0, sizeof(m_));
}

void SubworldHeightField::debug_poke_vertex(int x, int y, float metres)
{
    if (x < 0 || x >= kHeightVerts || y < 0 || y >= kHeightVerts) return;
    m_[std::size_t(y) * kHeightVerts + std::size_t(x)] = metres;
}

void SubworldHeightField::refresh(const SeamlessSubworldManager& mgr,
                                  const CompositeDirty& dirty)
{
    const std::vector<float>& hmv = mgr.heightmap();
    if (hmv.size() != std::size_t(kFullSize) * kFullSize) {
        // No window composited yet (a bare harness, or a scene being torn
        // down). The world has no ground to state, and says so.
        if (built_) clear();
        return;
    }
    const float* hm = hmv.data();

    // A shift is only expressible against a grid that already holds the
    // pre-shift window; the first fill has nothing to slide.
    const bool wantShift =
        (dirty.shiftX != 0 || dirty.shiftY != 0) && built_;
    const bool doFull = dirty.fullHeight || !built_;
    bool anyCell = false;
    for (int i = 0; i < 9; ++i) anyCell |= dirty.heightCells[std::size_t(i)];
    if (!doFull && !anyCell && !wantShift) return;

    constexpr int Nv = kHeightVerts;
    // One macro cell spans this many vertices per axis; a cell's re-blit
    // changes exactly the inclusive block [c*cellVerts, (c+1)*cellVerts],
    // because a boundary vertex's ±half footprint reaches one cell in and no
    // further (half < step, asserted by the step's own arithmetic).
    constexpr int cellVerts = kCellSize / kHeightQuadTiles;

    if (doFull) {
        rebuild_from(hm);
        return;
    }
    if (wantShift) {
        // SEAM CROSSING. The manager has already shifted its composite
        // toroidally; slide the vertex grid the same way (a memmove, in
        // VERTICES: one cell = cellVerts steps) so the 6/9 (axis) or 4/9
        // (diagonal) overlap keeps its heights without resampling, and
        // only the freshly exposed cells below are resampled.
        const int vpx = -dirty.shiftX * cellVerts;
        const int vpy = -dirty.shiftY * cellVerts;
        const int adx = vpx < 0 ? -vpx : vpx;
        const int ady = vpy < 0 ? -vpy : vpy;
        const int copyW = Nv - adx;
        const int copyH = Nv - ady;
        if (copyW > 0 && copyH > 0) {
            const int srcX = vpx > 0 ? 0 : -vpx;
            const int dstX = vpx > 0 ? vpx : 0;
            float* d = m_;
            if (vpy > 0) {
                for (int srcY = copyH - 1; srcY >= 0; --srcY)
                    std::memmove(&d[std::size_t(srcY + vpy) * Nv + dstX],
                                 &d[std::size_t(srcY) * Nv + srcX],
                                 std::size_t(copyW) * sizeof(float));
            } else {
                const int srcY0 = -vpy;
                for (int y = 0; y < copyH; ++y)
                    std::memmove(&d[std::size_t(y) * Nv + dstX],
                                 &d[std::size_t(srcY0 + y) * Nv + srcX],
                                 std::size_t(copyW) * sizeof(float));
            }
        }
        // The footprint CLAMPS at the composite edge, so an outer-ring
        // vertex is a smaller average than the interior value the memmove
        // slid into it. half < step ⇒ exactly the 1-vertex border ring is
        // affected — resample it (idempotent with the fresh-cell pass).
        resample_block(hm, 0, Nv - 1, 0, 0);
        resample_block(hm, 0, Nv - 1, Nv - 1, Nv - 1);
        resample_block(hm, 0, 0, 0, Nv - 1);
        resample_block(hm, Nv - 1, Nv - 1, 0, Nv - 1);
    }
    for (int idx = 0; idx < 9; ++idx) {
        if (!dirty.heightCells[std::size_t(idx)]) continue;
        const int ox = idx % 3, oy = idx / 3;
        const int vx0 = ox * cellVerts, vx1 = (ox + 1) * cellVerts;
        const int vy0 = oy * cellVerts, vy1 = (oy + 1) * cellVerts;
        // A freshly exposed cell is a placeholder: ONE height across all
        // 1024² of its tiles. Box-averaging 289 copies of that number per
        // vertex, ~4.2k vertices per cell, came to 3.7 million scattered
        // reads of a 37 MB array on an axis crossing — and the mean of a
        // constant is the constant. Only the cell's four shared EDGES need
        // honest sampling, where a vertex's footprint reaches into the
        // neighbour; everything strictly inside is the constant.
        float flatH = 0.0f;
        if (mgr.cell_flat_height(idx, flatH)) {
            // THE CONSTANT IS TAKEN THROUGH THE SAMPLER, not spelled as
            // `flatH * kHeightScaleM`. In real arithmetic those are the
            // same number; in float32 the average of 289 equal values
            // misses the value itself by an ulp, and the spelling put
            // 3969 vertices per placeholder cell one bit off the surface
            // the full path would have drawn (measured: 0.00061 m). One
            // interior probe costs 289 reads and the shortcut stays exact:
            // every interior vertex averages the same 289 numbers in the
            // same order, so its bits ARE this probe's bits.
            resample_block(hm, vx0 + 1, vx0 + 1, vy0 + 1, vy0 + 1);
            const float hM =
                m_[std::size_t(vy0 + 1) * Nv + std::size_t(vx0 + 1)];
            for (int y = vy0 + 1; y <= vy1 - 1; ++y) {
                float* row = &m_[std::size_t(y) * Nv];
                for (int x = vx0 + 1; x <= vx1 - 1; ++x) row[x] = hM;
            }
            resample_block(hm, vx0, vx1, vy0, vy0);
            resample_block(hm, vx0, vx1, vy1, vy1);
            resample_block(hm, vx0, vx0, vy0, vy1);
            resample_block(hm, vx1, vx1, vy0, vy1);
            continue;
        }
        resample_block(hm, vx0, vx1, vy0, vy1);
    }


    recompute_extent();
}

float SubworldHeightField::sample(float tileX, float tileY) const
{
    if (!built_) return 0.0f;
    constexpr int Nv = kHeightVerts;
    float fx = tileX * float(kHeightQuads) / float(kFullSize);
    float fy = tileY * float(kHeightQuads) / float(kFullSize);
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > float(Nv - 1)) fx = float(Nv - 1);
    if (fy > float(Nv - 1)) fy = float(Nv - 1);
    const int xi = int(fx), yi = int(fy);
    int xn = xi + 1; if (xn > Nv - 1) xn = Nv - 1;
    int yn = yi + 1; if (yn > Nv - 1) yn = Nv - 1;
    const float tx = fx - float(xi), ty = fy - float(yi);
    const float h00 = m_[std::size_t(yi) * Nv + std::size_t(xi)];
    const float h10 = m_[std::size_t(yi) * Nv + std::size_t(xn)];
    const float h01 = m_[std::size_t(yn) * Nv + std::size_t(xi)];
    const float h11 = m_[std::size_t(yn) * Nv + std::size_t(xn)];
    const float a = h00 * (1.0f - tx) + h10 * tx;
    const float b = h01 * (1.0f - tx) + h11 * tx;
    return a * (1.0f - ty) + b * ty;
}

} // namespace sm::sub
