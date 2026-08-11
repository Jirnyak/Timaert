// 3×3 seamless subworld manager. Boundary generation is worker-backed:
// freed slots receive deterministic placeholders immediately, then finished
// cells are stitched into the composite on the main thread.
#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "core/small_function.h"
#include "sub/map_data.h"
#include "sub/map_factory.h"

namespace sm::sub {

using CellResolver = sm::SmallFunction<CellContext(int cx, int cy)>;

struct LoadedCell {
    int cx = 0, cy = 0;
    std::uint32_t seed = 0;
    SubworldMode mode = SubworldMode::Open;
    Biome biome = Biome::Meadow;
    // 3×3 macro biome ring captured at generation (owner at index 4). The
    // ground-material dither (sub/material.h pick_ground_biome) reads it, so
    // a cell's material bytes are a window-independent property of the cell.
    Biome nbBiome[9] = {Biome::Meadow, Biome::Meadow, Biome::Meadow,
                        Biome::Meadow, Biome::Meadow, Biome::Meadow,
                        Biome::Meadow, Biome::Meadow, Biome::Meadow};
    float macroTemperature = 0.5f;
    // Furrow orientation of this cell's ploughed-field material (see
    // CellContext.fieldFurrowsVert) — travels with the cell like the
    // temperature so the material bytes stay window-independent.
    bool fieldFurrowsVert = false;
    SubworldMapData data;
    bool placeholder = false;
    std::uint64_t generation = 0;
    int roadTiles = 0;
    std::vector<std::int32_t> roadIndices;
    int roadMaskTiles = 0;
    std::vector<std::int32_t> roadMaskIndices;
    // True while EVERY tile of this cell in the composite holds exactly
    // `flatHeight` and `flatTile` — the state place_placeholder leaves a
    // freshly exposed cell in until its real terrain drains in. Maintained at
    // every composite write (blit, smooth), so a consumer may trust it rather
    // than infer it from `placeholder`.
    //
    // Both halves of the crossing frame used to spend their milliseconds
    // recovering these two numbers from a million copies of themselves: the
    // height path box-averaged 289 identical samples per vertex, the material
    // path walked 1024² identical tiles through a LUT. A constant does not need
    // to be integrated, and it does not need to be looked up.
    bool         heightIsFlat = false;
    float        flatHeight = 0.0f;
    std::uint8_t flatTile = 0;
};

struct SeamTiming {
    double genMs = 0.0;
    double smoothMs = 0.0;
    double totalMs = 0.0;
    bool crossed = false;
};

// Which parts of the 3×3 composite changed since the last consume. Lets the
// renderer rebuild only the affected cells instead of the whole 3k×3k mesh +
// material on every async drain (the seam-crossing upload cascade). A `full*`
// flag means "rebuild everything of that kind" (first build, seam shift, or a
// height smooth); otherwise the per-cell flags (idx = oy*3+ox, the same index
// as cell_biome) mark exactly the 1024-tile cells that changed. `structs` marks
// the tree/structure instance set (rebuilt whenever cells or heights change).
struct CompositeDirty {
    bool any = false;
    bool fullHeight = false;
    bool fullMaterial = false;
    bool structs = false;
    std::array<bool, 9> heightCells{};
    std::array<bool, 9> materialCells{};
    // Seam-crossing relocation. On a crossing the manager toroidally shifts its
    // CPU composite buffers by (shiftX,shiftY) cells (±1 each) and fills the
    // newly exposed cells with placeholders. A nonzero shift tells the renderer
    // to slide its own GPU material image + CPU height-vertex grid the same way
    // (a cheap on-GPU copy / memmove) and then rebuild ONLY the fresh cells
    // flagged in heightCells/materialCells — instead of a full 3k×3k rebuild.
    // A full* flag overrides the shift for that kind (the full path re-reads the
    // manager's already-shifted source arrays, so it is always safe).
    int shiftX = 0;
    int shiftY = 0;

    // OR-merge another dirty set into this one. Lets a caller accumulate work
    // that hasn't been uploaded yet across frames (consume clears the manager's
    // state, so a deferred upload must hold the union of every consume since).
    void merge(const CompositeDirty& o) {
        if (!o.any) return;
        // A relocation can only be represented cleanly against an empty pending
        // set: prior cell/shift flags are in the pre-shift coordinate frame, so
        // a second shift (two un-drained crossings) or a shift landing on
        // already-pending work falls back to a full rebuild — always correct.
        if (o.shiftX || o.shiftY) {
            if (any) {
                fullHeight = fullMaterial = structs = true;
                shiftX = shiftY = 0;
                heightCells.fill(false);
                materialCells.fill(false);
            } else {
                shiftX = o.shiftX;
                shiftY = o.shiftY;
            }
        }
        any = true;
        fullHeight |= o.fullHeight;
        fullMaterial |= o.fullMaterial;
        structs |= o.structs;
        for (std::size_t i = 0; i < 9; ++i) {
            heightCells[i] = heightCells[i] || o.heightCells[i];
            materialCells[i] = materialCells[i] || o.materialCells[i];
        }
    }
};

class SeamlessSubworldManager {
public:
    ~SeamlessSubworldManager();

    void init(int centerCx, int centerCy, CellResolver resolver);
    // Re-center if player crosses a boundary; loads/unloads as needed.
    void check_boundary(float& playerX, float& playerY);
    // Returns whether anything changed and clears all dirty state. Kept as the
    // coarse "something changed" signal (tests, callers that always rebuild).
    bool consume_composite_dirty();
    // Detailed variant: reports exactly which cells / whole-composite fields
    // changed and clears all dirty state. The renderer uses this to upload only
    // the affected regions. Consume via EITHER this or the bool form per frame,
    // never both — each clears the shared dirty state.
    CompositeDirty consume_composite_dirty_cells();
    const SeamTiming& last_seam_timing() const { return lastTiming_; }

    int  center_cx() const { return cx_; }
    int  center_cy() const { return cy_; }
    // Resolve ANY cell's macro context through the engine-installed resolver —
    // the same one door the window cells come through. The shadow apron
    // (vk_renderer_3d upload) asks for the ring of cells beyond the window so
    // out-of-window massifs keep casting into it.
    CellContext resolve_cell(int cx, int cy) const { return resolver_(cx, cy); }

    // Snapshot every currently loaded cell into the per-session save cache.
    // Called when the player leaves the subworld so the next visit
    // reproduces the exact heightmap and structure layout (including
    // felled trees / abandoned houses).
    void snapshot_all_to_cache();

    // Fell/harvest the nearest LOOTABLE prop (structure_is_lootable — a kind
    // with a loot row: trees, crops, whatever the table grows) within
    // `maxDist` of composite-space (x, y). `onlyKind` narrows the search to
    // one kind (the console `chop` and the chop smoke ask for a tree
    // specifically; the bare melee swing takes whatever is nearest). Removes
    // the record from BOTH the owning cell's data (so session snapshots
    // persist the loss) and the live composite, clears a tree's
    // TILE_TREE_DECOR back to grass (a harvested crop leaves its ploughed
    // tile), and marks structures + that cell's material dirty. On success
    // fills the owning cell's ABSOLUTE macro coords — the caller settles the
    // macro ledger (the micro → macro writeback) — and, optionally, a copy
    // of the record that fell, so the caller can pay out by what the prop
    // actually WAS (its metric height) instead of a flat per-swing constant.
    bool fell_prop_near(float x, float y, float maxDist,
                        int& outMacroCx, int& outMacroCy,
                        Structure* outFelled = nullptr,
                        const Structure::Kind* onlyKind = nullptr);

    // Composited 3kx3k fields.
    const std::vector<std::uint8_t>& tiles() const { return composite_tiles_; }
    const std::vector<float>&        heightmap() const { return composite_height_; }
    const std::vector<Structure>&    structures() const { return composite_struct_; }
    // A window cell whose composite content is one constant (LoadedCell::
    // heightIsFlat) — its height, and its tile id. False for any cell holding
    // real terrain, so the caller falls back to honest sampling.
    bool cell_flat_height(int idx, float& outHeight) const;
    bool cell_flat_tile(int idx, std::uint8_t& outTile) const;
    bool has_composite_roads() const;
    int composite_road_mask_tiles() const;
    void append_composite_road_mask_indices(std::vector<std::int32_t>& out) const;

    // Per-cell biome of the 3×3 grid (idx = (oy+1)*3 + (ox+1), ox/oy in -1..1).
    Biome cell_biome(int idx) const { return cells_[std::size_t(idx)].biome; }
    // The cell's captured 3×3 macro biome ring (owner at index 4) — feeds the
    // ground-material dither. Placeholders hold a uniform ring (their biome).
    const Biome* cell_biome_ring(int idx) const {
        return cells_[std::size_t(idx)].nbBiome;
    }
    float cell_temperature(int idx) const {
        return cells_[std::size_t(idx)].macroTemperature;
    }
    bool cell_field_furrows_vertical(int idx) const {
        return cells_[std::size_t(idx)].fieldFurrowsVert;
    }

private:
    int cx_ = 0, cy_ = 0;
    CellResolver resolver_;
    std::array<LoadedCell, 9> cells_;
    std::vector<std::uint8_t> composite_tiles_;
    std::vector<float>        composite_height_;
    std::vector<Structure>    composite_struct_;

    void load_all();
    void blit_into_composite(bool smoothRoads);
    void blit_cell_into_composite(int idx);

    // ── Composite dirty bookkeeping (see CompositeDirty) ──
    // Whole composite invalidated: first build / seam shift. Everything rebuilds.
    void mark_composite_full() {
        compositeDirty_ = true;
        dirtyFullHeight_ = dirtyFullMaterial_ = dirtyStructs_ = true;
        dirtyHeightCells_.fill(true);
        dirtyMaterialCells_.fill(true);
        dirtyShiftX_ = dirtyShiftY_ = 0;
    }
    // Seam crossing: the composite CPU buffers were just toroidally shifted by
    // (shiftX,shiftY) cells and the newly exposed `fresh` cells filled with
    // placeholders. Record the shift + mark only those fresh cells dirty so the
    // renderer slides its GPU/CPU mirrors and rebuilds just the new cells — not
    // the whole 3×3. `structs` is always marked (positions reindex on a shift).
    void mark_composite_shift(int shiftX, int shiftY, const bool fresh[9]) {
        compositeDirty_ = true;
        dirtyShiftX_ = shiftX;
        dirtyShiftY_ = shiftY;
        dirtyStructs_ = true;
        for (int i = 0; i < 9; ++i) {
            if (!fresh[i]) continue;
            dirtyHeightCells_[std::size_t(i)] = true;
            dirtyMaterialCells_[std::size_t(i)] = true;
        }
    }
    // One async cell stitched in: its height + material regions and the struct
    // set changed; the other eight cells are untouched.
    void mark_composite_cell(int idx) {
        if (idx < 0 || idx >= 9) return;
        compositeDirty_ = true;
        dirtyStructs_ = true;
        dirtyHeightCells_[std::size_t(idx)] = true;
        dirtyMaterialCells_[std::size_t(idx)] = true;
    }
    // Road/height smooth pass: every vertex height (hence trees) may move, but
    // the tile material ids are unchanged — skip the 9 MB material rebuild.
    void mark_composite_height_all() {
        compositeDirty_ = true;
        dirtyFullHeight_ = true;
        dirtyStructs_ = true;
        dirtyHeightCells_.fill(true);
    }
    // One cell's HEIGHT changed and nothing else: the smoothing pass moves
    // heights only within kRoadSmoothReach tiles of a road, so it names the
    // cells it could have reached instead of claiming the whole window. Note
    // `dirtyFullHeight_` stays clear — that is the difference between a 3 ms
    // full resample and a 0.3 ms one.
    void mark_composite_cell_height(int idx) {
        if (idx < 0 || idx >= 9) return;
        compositeDirty_ = true;
        dirtyStructs_ = true;
        dirtyHeightCells_[std::size_t(idx)] = true;
    }
    void clear_composite_dirty() {
        compositeDirty_ = false;
        dirtyFullHeight_ = dirtyFullMaterial_ = dirtyStructs_ = false;
        dirtyHeightCells_.fill(false);
        dirtyMaterialCells_.fill(false);
        dirtyShiftX_ = dirtyShiftY_ = 0;
    }
    void shift_composite_buffers(int shiftX, int shiftY);
    void rebuild_composite_structures();
    void generate_one(int idx, int acx, int acy);
    void place_placeholder(int idx, const CellContext& ctx, std::uint64_t generation);
    void queue_generation(const CellContext& ctx, std::uint64_t generation);
    void queue_save(LoadedCell&& cell);
    bool drain_completed_jobs(int maxJobs);
    void drain_completed_saves(int maxCells);
    void drain_completed_smooth(int maxJobs);
    void flush_save_jobs();
    void queue_composite_smooth();
    void cancel_pending_smooth();
    bool has_placeholders() const;
    int find_cell_index(int acx, int acy) const;
    void start_worker();
    void shutdown_worker();
    void worker_loop(std::stop_token stop);

    struct GenerationJob {
        int acx = 0;
        int acy = 0;
        std::uint32_t seed = 0;
        std::uint64_t generation = 0;
        CellContext ctx{};
        float nbHeights[9]{};
        Biome nbBiome[9]{};
        std::uint8_t nbFeature[9]{};
        CellLandmarkKind nbLandmark[9]{};
        // Macro tree counts (CellContext.treeCount); -1 = unknown/derive.
        int nbTreeCount[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
        // Neighbour fertility (CellContext.fertility01) — the field-plot
        // module keys plots off their OWNING cell's fertility. -1 = unknown.
        float nbFertility[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
        std::shared_ptr<const SavedSubworld> saved;
    };

    struct CompletedJob {
        int acx = 0;
        int acy = 0;
        std::uint64_t generation = 0;
        SubworldMode mode = SubworldMode::Open;
        Biome biome = Biome::Meadow;
        Biome nbBiome[9] = {Biome::Meadow, Biome::Meadow, Biome::Meadow,
                            Biome::Meadow, Biome::Meadow, Biome::Meadow,
                            Biome::Meadow, Biome::Meadow, Biome::Meadow};
        float macroTemperature = 0.5f;
        bool fieldFurrowsVert = false;
        std::uint32_t seed = 0;
        SubworldMapData data;
        int roadTiles = 0;
        std::vector<std::int32_t> roadIndices;
        int roadMaskTiles = 0;
        std::vector<std::int32_t> roadMaskIndices;
    };

    struct SaveJob {
        std::uint32_t seed = 0;
        SubworldMode mode = SubworldMode::Open;
        SubworldMapData data;
    };

    struct CompletedSaveJob {
        SavedSubworld saved;
        double saveMs = 0.0;
    };

    struct SmoothJob {
        std::uint64_t generation = 0;
        std::vector<std::int32_t> roadIndices;
        std::vector<float> height;
    };

    struct CompletedSmoothJob {
        std::uint64_t generation = 0;
        std::vector<float> height;
        double smoothMs = 0.0;
        // Which of the nine window cells the smoothing could have touched.
        // Road smoothing only moves heights within kRoadSmoothReach tiles of a
        // road tile, so a window whose roads all sit in two cells has no
        // business making the renderer resample the other seven — that full
        // rebuild is a 3 ms hitch a few frames after every crossing.
        bool touchedCells[9] = {false, false, false, false, false,
                                false, false, false, false};
    };

    bool accepts_generation_result(int acx, int acy, std::uint64_t generation) const;
    void prune_stale_generation_work();

    static constexpr int kWorkerCount = 2;
    std::array<std::jthread, kWorkerCount> workers_;
    std::mutex workerMutex_;
    std::condition_variable_any workerCv_;
    std::deque<GenerationJob> pendingJobs_;
    std::deque<CompletedJob> completedJobs_;
    std::deque<SaveJob> pendingSaveJobs_;
    std::deque<CompletedSaveJob> completedSaveJobs_;
    std::deque<SmoothJob> pendingSmoothJobs_;
    std::deque<CompletedSmoothJob> completedSmoothJobs_;
    int activeGenerationJobs_ = 0;
    int activeSaveJobs_ = 0;
    int activeSmoothJobs_ = 0;
    bool saveJobsPaused_ = false;
    std::uint64_t nextGeneration_ = 1;
    std::uint64_t smoothGeneration_ = 1;
    bool compositeDirty_ = false;
    bool dirtyFullHeight_ = false;
    bool dirtyFullMaterial_ = false;
    bool dirtyStructs_ = false;
    std::array<bool, 9> dirtyHeightCells_{};
    std::array<bool, 9> dirtyMaterialCells_{};
    int  dirtyShiftX_ = 0;
    int  dirtyShiftY_ = 0;
    bool compositeSmoothQueued_ = false;
    SeamTiming lastTiming_{};
};

} // namespace sm::sub
