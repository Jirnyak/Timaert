#include "sub/seamless_manager.h"
#include "sub/gens/dispatch.h"
#include "sub/base_generator.h"
#include "sub/map_factory.h"
#include <cstring>

namespace sm::sub {

void SeamlessSubworldManager::init(int cx, int cy, CellResolver r) {
    cx_ = cx; cy_ = cy; resolver_ = std::move(r);
    composite_tiles_.assign(std::size_t(kFullSize) * kFullSize, 0);
    composite_height_.assign(std::size_t(kFullSize) * kFullSize, 0.0f);
    composite_struct_.clear();
    load_all();
}

// Generate one cell into `cells_[idx]` for absolute (acx, acy).
// Restores from the snapshot cache after a fresh dispatch_generate so
// player-edited terrain (e.g. felled trees) survives reloads.
void SeamlessSubworldManager::generate_one(int idx, int acx, int acy) {
    CellContext ctx = resolver_(acx, acy);
    float nb[9];
    Biome nbBiome[9];
    std::uint8_t nbFeature[9];
    for (int yy = 0; yy < 3; ++yy)
        for (int xx = 0; xx < 3; ++xx) {
            CellContext nctx = resolver_(acx + xx - 1, acy + yy - 1);
            nb       [yy * 3 + xx] = nctx.macroHeight;
            nbBiome  [yy * 3 + xx] = nctx.biome;
            nbFeature[yy * 3 + xx] = std::uint8_t(nctx.feature);
        }
    auto& cell = cells_[std::size_t(idx)];
    cell.cx = ctx.cx;
    cell.cy = ctx.cy;
    cell.mode = resolve_mode(ctx);
    cell.biome = ctx.biome;
    dispatch_generate(ctx, nb, nbBiome, nbFeature, cell.data);
    if (const SavedSubworld* sv = find_saved_subworld(ctx.seed, cell.mode)) {
        restore_into(*sv, cell.data);
    }
}

void SeamlessSubworldManager::load_all() {
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int idx = (oy + 1) * 3 + (ox + 1);
            generate_one(idx, cx_ + ox, cy_ + oy);
        }
    }
    blit_into_composite();
}

void SeamlessSubworldManager::blit_into_composite() {
    composite_struct_.clear();
    for (int oy = 0; oy < 3; ++oy) {
        for (int ox = 0; ox < 3; ++ox) {
            int idx = oy * 3 + ox;
            const auto& cell = cells_[std::size_t(idx)];
            int dxOff = ox * kCellSize, dyOff = oy * kCellSize;
            for (int y = 0; y < kCellSize; ++y) {
                std::size_t srcRow = std::size_t(y) * kCellSize;
                std::size_t dstRow = std::size_t(dyOff + y) * kFullSize + dxOff;
                std::memcpy(&composite_tiles_[dstRow],  &cell.data.tiles[srcRow],  kCellSize);
                std::memcpy(&composite_height_[dstRow], &cell.data.heightmap[srcRow], kCellSize * sizeof(float));
            }
            for (auto& s : cell.data.structures) {
                Structure t = s;
                t.x += float(dxOff);
                t.y += float(dyOff);
                composite_struct_.push_back(t);
            }
        }
    }
    // Global cross-cell road flattening — per-cell smoothing in
    // dispatch_generate cannot see across cell boundaries, so a road that
    // crosses a seam ends up with two locally-flat halves meeting at a
    // visible kink. Run the same harmonic smoother on the stitched
    // composite so the road becomes a single continuous planar piece on
    // the relief, regardless of which 1024² cell each tile came from.
    smooth_road_heights(composite_height_, composite_tiles_, kFullSize, kFullSize);
}

void SeamlessSubworldManager::check_boundary(float& playerX, float& playerY) {
    int shiftX = 0, shiftY = 0;
    if (playerX < kCellSize)            shiftX = -1;
    else if (playerX >= kCellSize * 2)  shiftX = +1;
    if (playerY < kCellSize)            shiftY = -1;
    else if (playerY >= kCellSize * 2)  shiftY = +1;
    if (!shiftX && !shiftY) return;

    // Snapshot only the cells that are about to leave the 3×3 window.
    // Cells that survive the shift keep their live data — no needless
    // round-trip through the cache, no precision loss.
    auto leaving = [&](int ox, int oy) {
        if (shiftX > 0 && ox != -1) return false;  // moving +x: only ox=-1 col leaves
        if (shiftX < 0 && ox != +1) return false;  // moving -x: only ox=+1 col leaves
        if (shiftY > 0 && oy != -1) return false;
        if (shiftY < 0 && oy != +1) return false;
        return (shiftX != 0 && (ox == (shiftX > 0 ? -1 : +1)))
            || (shiftY != 0 && (oy == (shiftY > 0 ? -1 : +1)));
    };
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (!leaving(ox, oy)) continue;
            int idx = (oy + 1) * 3 + (ox + 1);
            const auto& cell = cells_[std::size_t(idx)];
            std::uint32_t seed = resolver_(cell.cx, cell.cy).seed;
            store_saved_subworld(snapshot_subworld(seed, cell.mode, cell.data));
        }
    }

    // Shift the surviving cells in the 3×3 array by (shiftX, shiftY).
    // Cells that move out of [-1..1] are dropped; the freed slots will be
    // regenerated below. Use a temp copy so we can move freely.
    std::array<LoadedCell, 9> next{};
    bool fresh[9] = {true, true, true, true, true, true, true, true, true};
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int srcOx = ox + shiftX;
            int srcOy = oy + shiftY;
            if (srcOx < -1 || srcOx > 1 || srcOy < -1 || srcOy > 1) continue;
            int dstIdx = (oy + 1) * 3 + (ox + 1);
            int srcIdx = (srcOy + 1) * 3 + (srcOx + 1);
            next[std::size_t(dstIdx)] = std::move(cells_[std::size_t(srcIdx)]);
            fresh[dstIdx] = false;
        }
    }
    cells_ = std::move(next);

    cx_ += shiftX; cy_ += shiftY;
    playerX -= shiftX * kCellSize;
    playerY -= shiftY * kCellSize;

    // Regenerate only the slots that were freed (3 for an axis-aligned
    // step, 5 for a diagonal step).
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int idx = (oy + 1) * 3 + (ox + 1);
            if (fresh[idx]) generate_one(idx, cx_ + ox, cy_ + oy);
        }
    }
    blit_into_composite();
}

void SeamlessSubworldManager::snapshot_all_to_cache() {
    if (!resolver_) return;
    for (const auto& cell : cells_) {
        std::uint32_t seed = resolver_(cell.cx, cell.cy).seed;
        store_saved_subworld(snapshot_subworld(seed, cell.mode, cell.data));
    }
}

} // namespace sm::sub
