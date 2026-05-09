// 3×3 seamless subworld manager. Single-thread generation (Web Worker pool
// not yet ported — would use std::thread + lockless queue). Mirrors
// subworld/seamless-manager.ts.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "sub/map_data.h"

namespace sm::sub {

using CellResolver = std::function<CellContext(int cx, int cy)>;

struct LoadedCell {
    int cx, cy;
    SubworldMode mode;
    Biome biome;
    SubworldMapData data;
};

class SeamlessSubworldManager {
public:
    void init(int centerCx, int centerCy, CellResolver resolver);
    // Re-center if player crosses a boundary; loads/unloads as needed.
    void check_boundary(float& playerX, float& playerY);

    int  center_cx() const { return cx_; }
    int  center_cy() const { return cy_; }

    // Snapshot every currently loaded cell into the per-session save cache.
    // Called when the player leaves the subworld so the next visit
    // reproduces the exact heightmap and structure layout (including
    // felled trees / abandoned houses).
    void snapshot_all_to_cache();

    // Composited 3kx3k fields.
    const std::vector<std::uint8_t>& tiles() const { return composite_tiles_; }
    const std::vector<float>&        heightmap() const { return composite_height_; }
    const std::vector<Structure>&    structures() const { return composite_struct_; }

    // Per-cell biome of the 3×3 grid (idx = (oy+1)*3 + (ox+1), ox/oy in -1..1).
    Biome cell_biome(int idx) const { return cells_[std::size_t(idx)].biome; }

private:
    int cx_ = 0, cy_ = 0;
    CellResolver resolver_;
    std::array<LoadedCell, 9> cells_;
    std::vector<std::uint8_t> composite_tiles_;
    std::vector<float>        composite_height_;
    std::vector<Structure>    composite_struct_;

    void load_all();
    void blit_into_composite();
    void generate_one(int idx, int acx, int acy);
};

} // namespace sm::sub
