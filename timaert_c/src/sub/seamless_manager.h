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
    float macroTemperature = 0.5f;
    SubworldMapData data;
    bool placeholder = false;
    std::uint64_t generation = 0;
    int roadTiles = 0;
    std::vector<std::int32_t> roadIndices;
    int roadMaskTiles = 0;
    std::vector<std::int32_t> roadMaskIndices;
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

    // OR-merge another dirty set into this one. Lets a caller accumulate work
    // that hasn't been uploaded yet across frames (consume clears the manager's
    // state, so a deferred upload must hold the union of every consume since).
    void merge(const CompositeDirty& o) {
        any |= o.any;
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

    // Snapshot every currently loaded cell into the per-session save cache.
    // Called when the player leaves the subworld so the next visit
    // reproduces the exact heightmap and structure layout (including
    // felled trees / abandoned houses).
    void snapshot_all_to_cache();

    // Composited 3kx3k fields.
    const std::vector<std::uint8_t>& tiles() const { return composite_tiles_; }
    const std::vector<float>&        heightmap() const { return composite_height_; }
    const std::vector<Structure>&    structures() const { return composite_struct_; }
    bool has_composite_roads() const;
    int composite_road_mask_tiles() const;
    void append_composite_road_mask_indices(std::vector<std::int32_t>& out) const;

    // Per-cell biome of the 3×3 grid (idx = (oy+1)*3 + (ox+1), ox/oy in -1..1).
    Biome cell_biome(int idx) const { return cells_[std::size_t(idx)].biome; }
    float cell_temperature(int idx) const {
        return cells_[std::size_t(idx)].macroTemperature;
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
    void clear_composite_dirty() {
        compositeDirty_ = false;
        dirtyFullHeight_ = dirtyFullMaterial_ = dirtyStructs_ = false;
        dirtyHeightCells_.fill(false);
        dirtyMaterialCells_.fill(false);
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
        std::shared_ptr<const SavedSubworld> saved;
    };

    struct CompletedJob {
        int acx = 0;
        int acy = 0;
        std::uint64_t generation = 0;
        SubworldMode mode = SubworldMode::Open;
        Biome biome = Biome::Meadow;
        float macroTemperature = 0.5f;
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
    bool compositeSmoothQueued_ = false;
    SeamTiming lastTiming_{};
};

} // namespace sm::sub
