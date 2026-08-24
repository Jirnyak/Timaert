#include "sub/seamless_manager.h"
#include "sub/gens/dispatch.h"
#include "sub/base_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>

namespace sm::sub {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void clear_placeholder_data(SubworldMapData& out) {
    out.tiles.clear();
    out.trav.clear();
    out.heightmap.clear();
    out.structures.clear();
    out.waterLevel = WATER_LEVEL;
}

void collect_road_indices(const std::vector<std::uint8_t>& tiles,
                          std::vector<std::int32_t>& smoothOut,
                          std::vector<std::int32_t>& maskOut) {
    smoothOut.clear();
    maskOut.clear();
    smoothOut.reserve(tiles.size() / 64);
    maskOut.reserve(tiles.size() / 128);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const std::uint8_t tile = tiles[i];
        if (tile == TILE_ROAD) {
            const auto idx = std::int32_t(i);
            smoothOut.push_back(idx);
            maskOut.push_back(idx);
        } else if (tile == TILE_SQUARE) {
            smoothOut.push_back(std::int32_t(i));
        }
    }
}

float placeholder_height_for(const CellContext& ctx) {
    if (ctx.biome == Biome::Water) {
        const float t = std::clamp(ctx.macroHeight / kMacroSeaLevel, 0.0f, 1.0f);
        return t * t * WATER_LEVEL;
    }

    const float landFloor = WATER_LEVEL + kLandMargin;
    const float landScale = (1.0f - landFloor) / (1.0f - kMacroSeaLevel);
    const float h = landFloor + (ctx.macroHeight - kMacroSeaLevel) * landScale;
    return std::clamp(h, landFloor, 2.0f);
}

std::uint8_t placeholder_tile_for(const CellContext& ctx, float height) {
    if (ctx.biome == Biome::Water || height < WATER_LEVEL) {
        return TILE_WATER;
    }
    if (ctx.biome == Biome::Mountain) {
        return TILE_ROCK;  // streaming placeholder reads as rock, not grass
    }
    return TILE_GRASS;
}

template <typename T>
void shift_buffer(std::vector<T>& data, int px, int py) {
    constexpr int w = kFullSize;
    constexpr int h = kFullSize;
    if (data.size() != std::size_t(w) * h) return;
    if (px <= -w || px >= w || py <= -h || py >= h) return;

    const int copyW = w - std::abs(px);
    const int copyH = h - std::abs(py);
    if (copyW <= 0 || copyH <= 0) return;

    const int srcX = px > 0 ? 0 : -px;
    const int dstX = px > 0 ? px : 0;

    if (py > 0) {
        for (int srcY = copyH - 1; srcY >= 0; --srcY) {
            const int dstY = srcY + py;
            std::memmove(&data[std::size_t(dstY) * w + dstX],
                         &data[std::size_t(srcY) * w + srcX],
                         std::size_t(copyW) * sizeof(T));
        }
    } else {
        const int srcY0 = -py;
        for (int y = 0; y < copyH; ++y) {
            const int srcY = srcY0 + y;
            const int dstY = y;
            std::memmove(&data[std::size_t(dstY) * w + dstX],
                         &data[std::size_t(srcY) * w + srcX],
                         std::size_t(copyW) * sizeof(T));
        }
    }
}

} // namespace

SeamlessSubworldManager::~SeamlessSubworldManager() {
    shutdown_worker();
}

void SeamlessSubworldManager::init(int cx, int cy, CellResolver r) {
    shutdown_worker();
    cx_ = cx; cy_ = cy; resolver_ = std::move(r);
    nextGeneration_ = 1;
    clear_composite_dirty();
    lastTiming_ = {};
    composite_tiles_.assign(std::size_t(kFullSize) * kFullSize, 0);
    composite_height_.assign(std::size_t(kFullSize) * kFullSize, 0.0f);
    composite_struct_.clear();
    start_worker();
    load_all();
}

void SeamlessSubworldManager::start_worker() {
    for (auto& worker : workers_) {
        if (worker.joinable()) continue;
        worker = std::jthread([this](std::stop_token stop) {
            worker_loop(stop);
        });
    }
}

void SeamlessSubworldManager::shutdown_worker() {
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingJobs_.clear();
        pendingSmoothJobs_.clear();
        completedJobs_.clear();
        completedSmoothJobs_.clear();
        saveJobsPaused_ = false;
        compositeSmoothQueued_ = false;
    }

    bool anyWorker = false;
    for (auto& worker : workers_) {
        if (!worker.joinable()) continue;
        anyWorker = true;
        worker.request_stop();
    }
    if (anyWorker) {
        workerCv_.notify_all();
    }
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    for (;;) {
        drain_completed_saves(1024);
        std::lock_guard<std::mutex> lock(workerMutex_);
        if (completedSaveJobs_.empty()) break;
    }

    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingJobs_.clear();
        pendingSaveJobs_.clear();
        pendingSmoothJobs_.clear();
        completedJobs_.clear();
        completedSaveJobs_.clear();
        completedSmoothJobs_.clear();
        activeGenerationJobs_ = 0;
        activeSaveJobs_ = 0;
        activeSmoothJobs_ = 0;
        saveJobsPaused_ = false;
        compositeSmoothQueued_ = false;
    }
}

void SeamlessSubworldManager::worker_loop(std::stop_token stop) {
    for (;;) {
        GenerationJob job;
        SaveJob saveJob;
        SmoothJob smoothJob;
        bool runGeneration = false;
        bool runSave = false;
        bool runSmooth = false;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerCv_.wait(lock, stop, [this] {
                return !pendingJobs_.empty()
                    || (activeGenerationJobs_ == 0
                        && (!pendingSmoothJobs_.empty()
                            || (!saveJobsPaused_ && !pendingSaveJobs_.empty())));
            });
            if (stop.stop_requested() && pendingJobs_.empty()
                && pendingSmoothJobs_.empty() && pendingSaveJobs_.empty()) {
                return;
            }
            if (!pendingJobs_.empty()) {
                job = std::move(pendingJobs_.front());
                pendingJobs_.pop_front();
                ++activeGenerationJobs_;
                runGeneration = true;
            } else if (activeGenerationJobs_ == 0 && !pendingSmoothJobs_.empty()) {
                smoothJob = std::move(pendingSmoothJobs_.front());
                pendingSmoothJobs_.pop_front();
                ++activeSmoothJobs_;
                runSmooth = true;
            } else if (activeGenerationJobs_ == 0 && !saveJobsPaused_
                       && !pendingSaveJobs_.empty()) {
                saveJob = std::move(pendingSaveJobs_.front());
                pendingSaveJobs_.pop_front();
                ++activeSaveJobs_;
                runSave = true;
            } else {
                continue;
            }
        }

        if (runSmooth) {
            const auto t0 = Clock::now();
            smooth_road_heights_indexed(smoothJob.height, smoothJob.roadIndices,
                                        kFullSize, kFullSize);

            CompletedSmoothJob done;
            done.generation = smoothJob.generation;
            // Which cells this run actually moved: every cell within the
            // OUTPUT reach of a road tile, and no others. Computed here, on the
            // worker, from the same index list the smoothing ran on — so the
            // answer cannot disagree with the work that was done.
            for (std::int32_t i : smoothJob.roadIndices) {
                const int x = int(i % kFullSize), y = int(i / kFullSize);
                const int cx0 = std::max(0, (x - kRoadSmoothOutputReach) / kCellSize);
                const int cx1 = std::min(2, (x + kRoadSmoothOutputReach) / kCellSize);
                const int cy0 = std::max(0, (y - kRoadSmoothOutputReach) / kCellSize);
                const int cy1 = std::min(2, (y + kRoadSmoothOutputReach) / kCellSize);
                for (int cy = cy0; cy <= cy1; ++cy)
                    for (int cx = cx0; cx <= cx1; ++cx)
                        done.touchedCells[std::size_t(cy * 3 + cx)] = true;
            }
            done.height = std::move(smoothJob.height);
            done.smoothMs = elapsed_ms(t0, Clock::now());

            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                completedSmoothJobs_.push_back(std::move(done));
                --activeSmoothJobs_;
            }
            workerCv_.notify_all();
            continue;
        }

        if (runSave) {
            const auto t0 = Clock::now();
            CompletedSaveJob done;
            done.saved = snapshot_subworld(saveJob.seed, saveJob.mode, saveJob.data);
            done.saveMs = elapsed_ms(t0, Clock::now());

            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                completedSaveJobs_.push_back(std::move(done));
                --activeSaveJobs_;
            }
            workerCv_.notify_all();
            continue;
        }

        if (runGeneration) {
            CompletedJob done;
            done.acx = job.acx;
            done.acy = job.acy;
            done.seed = job.seed;
            done.generation = job.generation;
            done.mode = resolve_mode(job.ctx);
            done.biome = job.ctx.biome;
            for (int i = 0; i < 9; ++i) done.nbBiome[i] = job.nbBiome[i];
            done.macroTemperature = job.ctx.macroTemperature;
            done.fieldFurrowsVert = job.ctx.fieldFurrowsVert;
            dispatch_generate(job.ctx, job.nbHeights, job.nbBiome,
                              job.nbFeature, done.data, job.nbLandmark,
                              job.nbTreeCount, job.nbFertility);
            if (job.saved) {
                restore_into(*job.saved, done.data);
            }
            collect_road_indices(done.data.tiles,
                                 done.roadIndices,
                                 done.roadMaskIndices);
            done.roadTiles = int(done.roadIndices.size());
            done.roadMaskTiles = int(done.roadMaskIndices.size());

            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                completedJobs_.push_back(std::move(done));
                --activeGenerationJobs_;
            }
            workerCv_.notify_all();
        }
    }
}

// Generate one cell into `cells_[idx]` for absolute (acx, acy).
// Restores from the snapshot cache after a fresh dispatch_generate so
// player-edited terrain (e.g. felled trees) survives reloads.
void SeamlessSubworldManager::generate_one(int idx, int acx, int acy) {
    CellContext ctx = resolver_(acx, acy);
    float nb[9];
    Biome nbBiome[9];
    std::uint8_t nbFeature[9];
    LandmarkType nbLandmark[9];
    int nbTreeCount[9];
    float nbFertility[9];
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            CellContext nctx = resolver_(acx + xx - 1, acy + yy - 1);
            nb        [yy * 3 + xx] = nctx.macroHeight;
            nbBiome   [yy * 3 + xx] = nctx.biome;
            nbFeature [yy * 3 + xx] = std::uint8_t(nctx.feature);
            nbLandmark[yy * 3 + xx] = effective_landmark(nctx);
            nbTreeCount[yy * 3 + xx] = nctx.treeCount;
            nbFertility[yy * 3 + xx] = nctx.fertility01;
        }
    }
    auto& cell = cells_[std::size_t(idx)];
    cell.cx = ctx.cx;
    cell.cy = ctx.cy;
    cell.seed = ctx.seed;
    cell.mode = resolve_mode(ctx);
    cell.biome = ctx.biome;
    for (int i = 0; i < 9; ++i) cell.nbBiome[i] = nbBiome[i];
    cell.macroTemperature = ctx.macroTemperature;
    cell.fieldFurrowsVert = ctx.fieldFurrowsVert;
    cell.placeholder = false;
    cell.generation = 0;
    dispatch_generate(ctx, nb, nbBiome, nbFeature, cell.data, nbLandmark,
                      nbTreeCount, nbFertility);
    if (std::shared_ptr<const SavedSubworld> sv =
            find_saved_subworld_ref(ctx.seed, cell.mode)) {
        restore_into(*sv, cell.data);
    }
    collect_road_indices(cell.data.tiles,
                         cell.roadIndices,
                         cell.roadMaskIndices);
    cell.roadTiles = int(cell.roadIndices.size());
    cell.roadMaskTiles = int(cell.roadMaskIndices.size());
}

void SeamlessSubworldManager::load_all() {
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int idx = (oy + 1) * 3 + (ox + 1);
            generate_one(idx, cx_ + ox, cy_ + oy);
        }
    }
    blit_into_composite(true);
    mark_composite_full();
}

void SeamlessSubworldManager::blit_into_composite(bool smoothRoads) {
    std::size_t structureCount = 0;
    for (const auto& cell : cells_) {
        if (!cell.placeholder) structureCount += cell.data.structures.size();
    }
    composite_struct_.clear();
    composite_struct_.reserve(structureCount);
    for (int oy = 0; oy < 3; ++oy) {
        for (int ox = 0; ox < 3; ++ox) {
            int idx = oy * 3 + ox;
            auto& cell = cells_[std::size_t(idx)];
            cell.heightIsFlat = false;   // this blit writes the cell's own data
            int dxOff = ox * kCellSize, dyOff = oy * kCellSize;
            for (int y = 0; y < kCellSize; ++y) {
                std::size_t srcRow = std::size_t(y) * kCellSize;
                std::size_t dstRow = std::size_t(dyOff + y) * kFullSize + dxOff;
                std::memcpy(&composite_tiles_[dstRow], &cell.data.tiles[srcRow], kCellSize);
                std::memcpy(&composite_height_[dstRow], &cell.data.heightmap[srcRow],
                            kCellSize * sizeof(float));
            }
            if (!cell.placeholder) {
                for (const auto& s : cell.data.structures) {
                    Structure t = s;
                    t.x += float(dxOff);
                    t.y += float(dyOff);
                    composite_struct_.push_back(t);
                }
            }
        }
    }

    lastTiming_.smoothMs = 0.0;
    if (smoothRoads && has_composite_roads()) {
        const auto t0 = Clock::now();
        smooth_road_heights(composite_height_, composite_tiles_, kFullSize, kFullSize);
        lastTiming_.smoothMs = elapsed_ms(t0, Clock::now());
    }
}

void SeamlessSubworldManager::blit_cell_into_composite(int idx) {
    if (idx < 0 || idx >= 9) return;
    const int ox = idx % 3;
    const int oy = idx / 3;
    auto& cell = cells_[std::size_t(idx)];
    const int dxOff = ox * kCellSize;
    const int dyOff = oy * kCellSize;
    for (int y = 0; y < kCellSize; ++y) {
        const std::size_t srcRow = std::size_t(y) * kCellSize;
        const std::size_t dstRow = std::size_t(dyOff + y) * kFullSize + dxOff;
        std::memcpy(&composite_tiles_[dstRow], &cell.data.tiles[srcRow], kCellSize);
        std::memcpy(&composite_height_[dstRow], &cell.data.heightmap[srcRow],
                    kCellSize * sizeof(float));
    }
    cell.heightIsFlat = false;   // real terrain now: nothing flat about it
}

void SeamlessSubworldManager::shift_composite_buffers(int shiftX, int shiftY) {
    const int px = -shiftX * kCellSize;
    const int py = -shiftY * kCellSize;
    shift_buffer(composite_tiles_, px, py);
    shift_buffer(composite_height_, px, py);
}

void SeamlessSubworldManager::rebuild_composite_structures() {
    std::size_t structureCount = 0;
    for (const auto& cell : cells_) {
        if (!cell.placeholder) structureCount += cell.data.structures.size();
    }
    composite_struct_.clear();
    composite_struct_.reserve(structureCount);
    for (int idx = 0; idx < 9; ++idx) {
        const auto& cell = cells_[std::size_t(idx)];
        if (cell.placeholder) continue;
        const int ox = idx % 3;
        const int oy = idx / 3;
        const float dxOff = float(ox * kCellSize);
        const float dyOff = float(oy * kCellSize);
        for (const auto& s : cell.data.structures) {
            Structure t = s;
            t.x += dxOff;
            t.y += dyOff;
            composite_struct_.push_back(t);
        }
    }
}

bool SeamlessSubworldManager::fell_prop_near(float x, float y, float maxDist,
                                             int& outMacroCx, int& outMacroCy,
                                             Structure* outFelled,
                                             const Structure::Kind* onlyKind) {
    // Nearest standing lootable prop in composite space (props never move, so
    // a plain linear scan over the composite is exact; harvesting is a
    // per-swing event, not a per-tick one — O(structures) is fine).
    int best = -1;
    float bestD2 = maxDist * maxDist;
    for (std::size_t i = 0; i < composite_struct_.size(); ++i) {
        const Structure& s = composite_struct_[i];
        // The loot gate protects the BARE swing (no onlyKind) from felling
        // walls; a caller that names a kind knows what it is consuming —
        // that is how the spire orb (no loot row) is taken by its verb.
        if (!onlyKind && !structure_is_lootable(s.kind)) continue;
        if (onlyKind && s.kind != *onlyKind) continue;
        const float dx = s.x - x, dy = s.y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; best = int(i); }
    }
    if (best < 0) return false;

    const Structure victim = composite_struct_[std::size_t(best)];
    const int ox = std::clamp(int(victim.x) / kCellSize, 0, 2);
    const int oy = std::clamp(int(victim.y) / kCellSize, 0, 2);
    const int idx = oy * 3 + ox;
    auto& cell = cells_[std::size_t(idx)];
    if (cell.placeholder) return false; // composite/cell raced a shift: bail

    // Remove from the owning cell's data (position match in cell-local
    // coords) so the session snapshot — and hence every revisit — keeps the
    // stump gone. If the cell entry can't be found the composite is stale
    // against an in-flight regeneration; fail closed and fell nothing.
    const float lx = victim.x - float(ox * kCellSize);
    const float ly = victim.y - float(oy * kCellSize);
    int cellEntry = -1;
    for (std::size_t i = 0; i < cell.data.structures.size(); ++i) {
        const Structure& s = cell.data.structures[i];
        if (s.kind == victim.kind
            && std::fabs(s.x - lx) < 0.01f && std::fabs(s.y - ly) < 0.01f) {
            cellEntry = int(i);
            break;
        }
    }
    if (cellEntry < 0) return false;
    cell.data.structures.erase(cell.data.structures.begin() + cellEntry);
    composite_struct_.erase(composite_struct_.begin() + best);

    // The stump tile reverts to open ground, in both the cell (authoritative
    // for snapshots) and the live composite (2D map / movement cost).
    const int tx = std::clamp(int(lx), 0, kCellSize - 1);
    const int ty = std::clamp(int(ly), 0, kCellSize - 1);
    const std::size_t ci = std::size_t(ty) * kCellSize + tx;
    if (cell.data.tiles[ci] == TILE_TREE_DECOR)
        cell.data.tiles[ci] = TILE_GRASS;
    const std::size_t gi = tile_index(std::clamp(int(victim.x), 0, kFullSize - 1),
                                      std::clamp(int(victim.y), 0, kFullSize - 1));
    if (composite_tiles_[gi] == TILE_TREE_DECOR)
        composite_tiles_[gi] = TILE_GRASS;

    // Structures changed + one cell's material byte changed; heights did not.
    compositeDirty_ = true;
    dirtyStructs_ = true;
    dirtyMaterialCells_[std::size_t(idx)] = true;

    outMacroCx = cell.cx;
    outMacroCy = cell.cy;
    if (outFelled) *outFelled = victim;
    return true;
}

void SeamlessSubworldManager::place_placeholder(int idx, const CellContext& ctx,
                                                std::uint64_t generation) {
    auto& cell = cells_[std::size_t(idx)];
    cell.cx = ctx.cx;
    cell.cy = ctx.cy;
    cell.seed = ctx.seed;
    cell.mode = resolve_mode(ctx);
    cell.biome = ctx.biome;
    // Placeholder: uniform ring — flat placeholder material until the real
    // cell (with its captured macro ring) drains in.
    for (int i = 0; i < 9; ++i) cell.nbBiome[i] = ctx.biome;
    cell.macroTemperature = ctx.macroTemperature;
    cell.fieldFurrowsVert = ctx.fieldFurrowsVert;
    cell.placeholder = true;
    cell.generation = generation;
    cell.roadTiles = 0;
    cell.roadIndices.clear();
    cell.roadMaskTiles = 0;
    cell.roadMaskIndices.clear();
    clear_placeholder_data(cell.data);

    const int ox = idx % 3;
    const int oy = idx / 3;
    const int dxOff = ox * kCellSize;
    const int dyOff = oy * kCellSize;
    const float placeholderHeight = placeholder_height_for(ctx);
    const std::uint8_t placeholderTile = placeholder_tile_for(ctx, placeholderHeight);
    // One number across the whole cell — recorded so consumers do not have to
    // integrate a million copies of it to find out.
    cell.heightIsFlat = true;
    cell.flatHeight = placeholderHeight;
    cell.flatTile = placeholderTile;
    for (int y = 0; y < kCellSize; ++y) {
        const std::size_t dstRow = std::size_t(dyOff + y) * kFullSize + dxOff;
        std::fill_n(&composite_tiles_[dstRow], kCellSize, placeholderTile);
        std::fill_n(&composite_height_[dstRow], kCellSize, placeholderHeight);
    }
}

void SeamlessSubworldManager::queue_generation(const CellContext& ctx,
                                               std::uint64_t generation) {
    GenerationJob job;
    job.acx = ctx.cx;
    job.acy = ctx.cy;
    job.seed = ctx.seed;
    job.generation = generation;
    job.ctx = ctx;
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            CellContext nctx = (xx == 1 && yy == 1)
                ? ctx
                : resolver_(ctx.cx + xx - 1, ctx.cy + yy - 1);
            const int ni = yy * 3 + xx;
            job.nbHeights[ni] = nctx.macroHeight;
            job.nbBiome[ni] = nctx.biome;
            job.nbFeature[ni] = std::uint8_t(nctx.feature);
            job.nbLandmark[ni] = effective_landmark(nctx);
            job.nbTreeCount[ni] = nctx.treeCount;
            job.nbFertility[ni] = nctx.fertility01;
        }
    }
    const SubworldMode mode = resolve_mode(job.ctx);
    job.saved = find_saved_subworld_ref(job.ctx.seed, mode);
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingJobs_.push_back(std::move(job));
    }
    workerCv_.notify_one();
}

int SeamlessSubworldManager::find_cell_index(int acx, int acy) const {
    for (int i = 0; i < 9; ++i) {
        const auto& cell = cells_[std::size_t(i)];
        if (cell.cx == acx && cell.cy == acy) return i;
    }
    return -1;
}

bool SeamlessSubworldManager::drain_completed_jobs(int maxJobs) {
    bool changed = false;
    if (maxJobs <= 0) return false;

    int remaining = 0;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        remaining = int(completedJobs_.size());
    }

    int installed = 0;
    while (remaining-- > 0 && installed < maxJobs) {
        CompletedJob done;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (completedJobs_.empty()) break;
            done = std::move(completedJobs_.front());
            completedJobs_.pop_front();
        }

        const int idx = find_cell_index(done.acx, done.acy);
        if (std::getenv("TIMAERT_SEAM_TRACE")) {
            std::fprintf(stderr,
                "[seam-drain] popped acx=%d acy=%d gen=%llu idx=%d\n",
                done.acx, done.acy, (unsigned long long)done.generation, idx);
            std::fflush(stderr);
        }
        if (idx < 0) continue;

        auto& cell = cells_[std::size_t(idx)];
        if (!cell.placeholder || cell.generation != done.generation) {
            continue;
        }

        cell.mode = done.mode;
        cell.biome = done.biome;
        for (int i = 0; i < 9; ++i) cell.nbBiome[i] = done.nbBiome[i];
        cell.macroTemperature = done.macroTemperature;
        cell.fieldFurrowsVert = done.fieldFurrowsVert;
        cell.seed = done.seed;
        cell.data = std::move(done.data);
        cell.placeholder = false;
        cell.generation = 0;
        cell.roadTiles = done.roadTiles;
        cell.roadIndices = std::move(done.roadIndices);
        cell.roadMaskTiles = done.roadMaskTiles;
        cell.roadMaskIndices = std::move(done.roadMaskIndices);
        blit_cell_into_composite(idx);
        rebuild_composite_structures();
        mark_composite_cell(idx);
        if (std::getenv("TIMAERT_SEAM_TRACE")) {
            std::fprintf(stderr, "[seam-drain] stitched idx=%d gen=%llu\n",
                         idx, (unsigned long long)done.generation);
            std::fflush(stderr);
        }
        changed = true;
        ++installed;
    }
    if (changed && !has_placeholders()) {
        queue_composite_smooth();
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            saveJobsPaused_ = false;
        }
        workerCv_.notify_all();
    }
    return changed;
}

void SeamlessSubworldManager::queue_save(LoadedCell&& cell) {
    if (cell.placeholder) return;

    SaveJob job;
    job.seed = cell.seed;
    job.mode = cell.mode;
    job.data = std::move(cell.data);
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingSaveJobs_.push_back(std::move(job));
    }
    workerCv_.notify_one();
}

void SeamlessSubworldManager::drain_completed_saves(int maxCells) {
    for (int i = 0; i < maxCells; ++i) {
        CompletedSaveJob done;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (completedSaveJobs_.empty()) break;
            done = std::move(completedSaveJobs_.front());
            completedSaveJobs_.pop_front();
        }
        store_saved_subworld(std::move(done.saved));
    }
}

void SeamlessSubworldManager::drain_completed_smooth(int maxJobs) {
    if (maxJobs <= 0) return;

    int remaining = 0;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        remaining = int(completedSmoothJobs_.size());
    }

    int applied = 0;
    while (remaining-- > 0 && applied < maxJobs) {
        CompletedSmoothJob done;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (completedSmoothJobs_.empty()) break;
            done = std::move(completedSmoothJobs_.front());
            completedSmoothJobs_.pop_front();
        }
        if (done.generation != smoothGeneration_) {
            continue;
        }
        composite_height_ = std::move(done.height);
        // Road smoothing may have moved any height in the composite; no cell
        // can claim to be flat afterwards. (It cannot run while a placeholder
        // exists — queue_composite_smooth refuses — but the flag is maintained
        // here anyway so it stays true by construction, not by argument.)
        for (auto& c : cells_) c.heightIsFlat = false;
        lastTiming_.smoothMs = done.smoothMs;
        compositeSmoothQueued_ = false;
        // Only the cells the run actually touched. Marking all nine cost a
        // full 3 ms height resample a few frames after every crossing, for a
        // pass that moves heights one tile either side of a road.
        int touched = 0;
        for (int i = 0; i < 9; ++i) {
            if (!done.touchedCells[std::size_t(i)]) continue;
            mark_composite_cell_height(i);
            ++touched;
        }
        if (std::getenv("TIMAERT_SEAM_TRACE")) {
            std::fprintf(stderr, "[seam-drain] smooth touched %d/9 cells\n",
                         touched);
            std::fflush(stderr);
        }
        if (std::getenv("TIMAERT_SEAM_TRACE")) {
            std::fprintf(stderr, "[seam-drain] smooth gen=%llu\n",
                         (unsigned long long)done.generation);
            std::fflush(stderr);
        }
        ++applied;
    }
}

bool SeamlessSubworldManager::accepts_generation_result(int acx, int acy,
                                                        std::uint64_t generation) const {
    for (const auto& cell : cells_) {
        if (cell.placeholder && cell.cx == acx && cell.cy == acy
            && cell.generation == generation) {
            return true;
        }
    }
    return false;
}

void SeamlessSubworldManager::prune_stale_generation_work() {
    std::lock_guard<std::mutex> lock(workerMutex_);

    auto pendingIt = pendingJobs_.begin();
    while (pendingIt != pendingJobs_.end()) {
        if (accepts_generation_result(pendingIt->acx, pendingIt->acy,
                                      pendingIt->generation)) {
            ++pendingIt;
        } else {
            pendingIt = pendingJobs_.erase(pendingIt);
        }
    }

    auto completedIt = completedJobs_.begin();
    while (completedIt != completedJobs_.end()) {
        if (accepts_generation_result(completedIt->acx, completedIt->acy,
                                      completedIt->generation)) {
            ++completedIt;
        } else {
            completedIt = completedJobs_.erase(completedIt);
        }
    }
}

void SeamlessSubworldManager::flush_save_jobs() {
    for (;;) {
        drain_completed_saves(1024);
        std::unique_lock<std::mutex> lock(workerMutex_);
        if (pendingSaveJobs_.empty() && completedSaveJobs_.empty()
            && activeSaveJobs_ == 0) {
            break;
        }
        workerCv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

void SeamlessSubworldManager::queue_composite_smooth() {
    if (compositeSmoothQueued_ || has_placeholders()) return;
    if (composite_tiles_.empty() || composite_height_.empty()) return;
    if (!has_composite_roads()) return;

    SmoothJob job;
    job.generation = ++smoothGeneration_;
    int roadCount = 0;
    for (const auto& cell : cells_) {
        if (!cell.placeholder) roadCount += cell.roadTiles;
    }
    job.roadIndices.reserve(std::size_t(roadCount));
    for (int idx = 0; idx < 9; ++idx) {
        const auto& cell = cells_[std::size_t(idx)];
        if (cell.placeholder || cell.roadIndices.empty()) continue;
        const int ox = idx % 3;
        const int oy = idx / 3;
        const int baseX = ox * kCellSize;
        const int baseY = oy * kCellSize;
        for (std::int32_t local : cell.roadIndices) {
            const int x = int(local % kCellSize);
            const int y = int(local / kCellSize);
            job.roadIndices.push_back(std::int32_t(
                (baseY + y) * kFullSize + baseX + x));
        }
    }
    std::sort(job.roadIndices.begin(), job.roadIndices.end());
    if (job.roadIndices.empty()) return;
    job.height = composite_height_;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingSmoothJobs_.clear();
        pendingSmoothJobs_.push_back(std::move(job));
    }
    compositeSmoothQueued_ = true;
    workerCv_.notify_one();
}

void SeamlessSubworldManager::cancel_pending_smooth() {
    ++smoothGeneration_;
    compositeSmoothQueued_ = false;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingSmoothJobs_.clear();
        completedSmoothJobs_.clear();
    }
}

bool SeamlessSubworldManager::has_placeholders() const {
    for (const auto& cell : cells_) {
        if (cell.placeholder) return true;
    }
    return false;
}

bool SeamlessSubworldManager::has_composite_roads() const {
    for (const auto& cell : cells_) {
        if (!cell.placeholder && cell.roadTiles > 0) return true;
    }
    return false;
}

int SeamlessSubworldManager::composite_road_mask_tiles() const {
    int total = 0;
    for (const auto& cell : cells_) {
        if (!cell.placeholder) total += cell.roadMaskTiles;
    }
    return total;
}

void SeamlessSubworldManager::append_composite_road_mask_indices(
    std::vector<std::int32_t>& out) const {
    const std::size_t baseSize = out.size();
    int roadCount = 0;
    for (const auto& cell : cells_) {
        if (!cell.placeholder) roadCount += cell.roadMaskTiles;
    }
    out.reserve(baseSize + std::size_t(roadCount));
    for (int idx = 0; idx < 9; ++idx) {
        const auto& cell = cells_[std::size_t(idx)];
        if (cell.placeholder || cell.roadMaskIndices.empty()) continue;
        const int ox = idx % 3;
        const int oy = idx / 3;
        const int baseX = ox * kCellSize;
        const int baseY = oy * kCellSize;
        for (std::int32_t local : cell.roadMaskIndices) {
            const int x = int(local % kCellSize);
            const int y = int(local / kCellSize);
            out.push_back(std::int32_t(
                (baseY + y) * kFullSize + baseX + x));
        }
    }
}

void SeamlessSubworldManager::check_boundary(float& playerX, float& playerY) {
    lastTiming_ = {};
    drain_completed_smooth(1);
    drain_completed_jobs(1);

    int shiftX = 0, shiftY = 0;
    if (playerX < kCellSize)           shiftX = -1;
    else if (playerX >= kCellSize * 2) shiftX = +1;
    if (playerY < kCellSize)           shiftY = -1;
    else if (playerY >= kCellSize * 2) shiftY = +1;
    if (!shiftX && !shiftY) {
        drain_completed_saves(1);
        return;
    }

    lastTiming_.crossed = true;
    const auto totalStart = Clock::now();
    const auto genStart = Clock::now();
    cancel_pending_smooth();
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        saveJobsPaused_ = true;
    }

    // Move real cells that are about to leave into a bounded stack buffer.
    // Queue their save jobs after generation jobs so snapshot quantisation
    // cannot occupy every worker before newly exposed cells start generating.
    // Placeholder cells have no player edits; skipping them is deterministic.
    auto leaving = [&](int ox, int oy) {
        return (shiftX != 0 && ox == (shiftX > 0 ? -1 : +1))
            || (shiftY != 0 && oy == (shiftY > 0 ? -1 : +1));
    };
    std::array<LoadedCell, 5> leavingCells{};
    int leavingCount = 0;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (!leaving(ox, oy)) continue;
            int idx = (oy + 1) * 3 + (ox + 1);
            auto& cell = cells_[std::size_t(idx)];
            if (cell.placeholder) continue;
            if (leavingCount < int(leavingCells.size())) {
                leavingCells[std::size_t(leavingCount++)] = std::move(cell);
            }
        }
    }

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
    prune_stale_generation_work();

    cx_ += shiftX; cy_ += shiftY;
    playerX -= shiftX * kCellSize;
    playerY -= shiftY * kCellSize;

    shift_composite_buffers(shiftX, shiftY);

    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int idx = (oy + 1) * 3 + (ox + 1);
            if (!fresh[idx]) continue;
            const std::uint64_t generation = nextGeneration_++;
            const int acx = cx_ + ox;
            const int acy = cy_ + oy;
            const CellContext ctx = resolver_(acx, acy);
            place_placeholder(idx, ctx, generation);
            queue_generation(ctx, generation);
        }
    }
    for (int i = 0; i < leavingCount; ++i) {
        queue_save(std::move(leavingCells[std::size_t(i)]));
    }

    rebuild_composite_structures();
    // The CPU composite buffers are already toroidally shifted (overlap moved,
    // fresh cells placeholder-filled). Emit that shift + only the fresh cells so
    // the renderer slides its GPU/CPU mirrors and rebuilds just the new cells —
    // O(new content), not O(whole 3×3). (Renderer may still fall back to a full
    // rebuild for any kind whose shift path is not yet wired; both are correct.)
    mark_composite_shift(shiftX, shiftY, fresh);
    lastTiming_.genMs = elapsed_ms(genStart, Clock::now());
    lastTiming_.smoothMs = 0.0;
    lastTiming_.totalMs = elapsed_ms(totalStart, Clock::now());
    if (std::getenv("TIMAERT_SEAM_TRACE")) {
        std::size_t nPend = 0, nDone = 0;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            nPend = pendingJobs_.size();
            nDone = completedJobs_.size();
        }
        std::fprintf(stderr,
            "[seam-cross-state] shift=%d,%d placeholders=%d pending=%zu completed=%zu\n",
            shiftX, shiftY, has_placeholders() ? 1 : 0, nPend, nDone);
        std::fflush(stderr);
    }
}

bool SeamlessSubworldManager::cell_flat_height(int idx, float& outHeight) const {
    if (idx < 0 || idx >= 9) return false;
    const auto& cell = cells_[std::size_t(idx)];
    if (!cell.heightIsFlat) return false;
    outHeight = cell.flatHeight;
    return true;
}

bool SeamlessSubworldManager::cell_flat_tile(int idx, std::uint8_t& outTile) const {
    if (idx < 0 || idx >= 9) return false;
    const auto& cell = cells_[std::size_t(idx)];
    if (!cell.heightIsFlat) return false;
    outTile = cell.flatTile;
    return true;
}

bool SeamlessSubworldManager::consume_composite_dirty() {
    const bool dirty = compositeDirty_;
    clear_composite_dirty();
    return dirty;
}

CompositeDirty SeamlessSubworldManager::consume_composite_dirty_cells() {
    CompositeDirty d;
    d.any = compositeDirty_;
    d.fullHeight = dirtyFullHeight_;
    d.fullMaterial = dirtyFullMaterial_;
    d.structs = dirtyStructs_;
    d.heightCells = dirtyHeightCells_;
    d.materialCells = dirtyMaterialCells_;
    d.shiftX = dirtyShiftX_;
    d.shiftY = dirtyShiftY_;
    clear_composite_dirty();
    return d;
}

void SeamlessSubworldManager::snapshot_all_to_cache() {
    if (!resolver_) return;
    drain_completed_jobs(9);
    cancel_pending_smooth();
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingJobs_.clear();
        saveJobsPaused_ = false;
    }
    workerCv_.notify_all();
    for (auto& cell : cells_) {
        if (cell.placeholder) continue;
        queue_save(std::move(cell));
    }
    flush_save_jobs();
}

} // namespace sm::sub
