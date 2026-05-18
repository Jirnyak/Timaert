#include "sub/seamless_manager.h"
#include "sub/base_generator.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

sm::sub::CellContext resolve_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.58f;
    c.biome = sm::Biome::Meadow;
    c.feature = (cx == 2 || cx == 3) ? sm::FT_Road : sm::FT_None;
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.seed = 0x5eed0000u
        ^ (std::uint32_t(cx) * 73856093u)
        ^ (std::uint32_t(cy) * 19349663u);
    return c;
}

sm::sub::CellContext resolve_plain_cell(int cx, int cy) {
    sm::sub::CellContext c = resolve_cell(cx, cy);
    c.feature = sm::FT_None;
    c.seed ^= 0x13579bdfu;
    return c;
}

sm::sub::CellContext resolve_water_plane_cell(int cx, int cy) {
    sm::sub::CellContext c = resolve_plain_cell(cx, cy);
    const int mx = ((cx % 3) + 3) % 3;
    const int my = ((cy % 3) + 3) % 3;
    const int slot = my * 3 + mx;
    c.feature = sm::FT_None;
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.landmarkKind = sm::sub::CellLandmarkKind::None;
    c.seed ^= 0x4a71f00du ^ (std::uint32_t(slot) * 0x9e3779b9u);

    if (slot == 0 || slot == 4 || slot == 8) {
        c.macroHeight = 0.18f;
        c.biome = sm::Biome::Water;
    } else if (slot == 1 || slot == 5 || slot == 6) {
        c.macroHeight = 0.405f;
        c.biome = sm::Biome::Swamp;
    } else {
        c.macroHeight = 0.62f;
        c.biome = sm::Biome::Meadow;
    }
    return c;
}

bool fail(const char* reason) {
    std::fprintf(stderr, "subworld_async_seam_test: FAIL %s\n", reason);
    return false;
}

float expected_placeholder_height(const sm::sub::CellContext& c) {
    if (c.biome == sm::Biome::Water) {
        const float t = std::clamp(c.macroHeight / sm::sub::kMacroSeaLevel, 0.0f, 1.0f);
        return t * t * sm::sub::WATER_LEVEL;
    }
    const float landFloor = sm::sub::WATER_LEVEL + sm::sub::kLandMargin;
    const float landScale = (1.0f - landFloor) / (1.0f - sm::sub::kMacroSeaLevel);
    const float h = landFloor + (c.macroHeight - sm::sub::kMacroSeaLevel) * landScale;
    return std::clamp(h, landFloor, 2.0f);
}

std::uint8_t expected_placeholder_tile(const sm::sub::CellContext& c, float height) {
    if (c.biome == sm::Biome::Water || height < sm::sub::WATER_LEVEL) {
        return sm::sub::TILE_WATER;
    }
    if (height < sm::sub::WATER_LEVEL + 0.05f) {
        return sm::sub::TILE_SHORE;
    }
    return sm::sub::TILE_GRASS;
}

bool expect_placeholder(const sm::sub::SeamlessSubworldManager& mgr,
                        sm::sub::CellContext (*resolver)(int, int),
                        int x,
                        int y,
                        const char* reason) {
    const std::size_t idx = sm::sub::tile_index(x, y);
    if (idx >= mgr.tiles().size() || idx >= mgr.heightmap().size()) {
        return fail(reason);
    }
    const int cellX = x / sm::sub::kCellSize;
    const int cellY = y / sm::sub::kCellSize;
    const sm::sub::CellContext ctx = resolver(
        mgr.center_cx() + cellX - 1,
        mgr.center_cy() + cellY - 1);
    const float expectedHeight = expected_placeholder_height(ctx);
    const std::uint8_t expectedTile = expected_placeholder_tile(ctx, expectedHeight);
    if (mgr.tiles()[idx] != expectedTile) {
        return fail(reason);
    }
    if (std::fabs(mgr.heightmap()[idx] - expectedHeight) > 0.0001f) {
        return fail(reason);
    }
    return true;
}

bool has_structure(const sm::sub::SeamlessSubworldManager& mgr,
                   sm::sub::Structure::Kind kind,
                   float expectedX,
                   float expectedY,
                   float expectedRadius,
                   float expectedHeight) {
    for (const sm::sub::Structure& s : mgr.structures()) {
        if (s.kind != kind) continue;
        if (std::fabs(s.x - expectedX) > 0.001f) continue;
        if (std::fabs(s.y - expectedY) > 0.001f) continue;
        if (std::fabs(s.radius - expectedRadius) > 0.001f) continue;
        if (std::fabs(s.height - expectedHeight) > 0.001f) continue;
        return true;
    }
    return false;
}

bool run_case(sm::sub::CellContext (*resolver)(int, int),
              bool expectSmoothPublish,
              const char* label,
              int& outDirty,
              sm::sub::SeamTiming& outTiming) {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolver);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 8);
    float playerY = float(sm::sub::kCellSize + 128);
    mgr.check_boundary(playerX, playerY);

    if (mgr.center_cx() != 1 || mgr.center_cy() != 0) {
        return fail("center did not shift east");
    }
    if (std::fabs(playerX - float(sm::sub::kCellSize + 8)) > 0.01f) {
        return fail("player frame was not recentered");
    }
    if (!mgr.consume_composite_dirty()) {
        return fail("boundary did not mark composite dirty");
    }
    outTiming = mgr.last_seam_timing();
    if (!outTiming.crossed || outTiming.smoothMs != 0.0) {
        return fail("boundary timing missing or smoothing stayed on seam path");
    }
    if (!expect_placeholder(mgr, resolver,
            sm::sub::kCellSize * 2 + 8,
            sm::sub::kCellSize + 128,
            "east exposed slot was not macro placeholder")) {
        return false;
    }

    const int targetDirty = expectSmoothPublish ? 4 : 3;
    outDirty = 0;
    for (int i = 0; i < 500 && outDirty < targetDirty; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            ++outDirty;
        }
    }

    if (outDirty < 3) {
        return fail("worker jobs did not stitch back into composite");
    }
    if (expectSmoothPublish && outDirty < 4) {
        return fail("async composite smoothing did not publish");
    }

    if (!expectSmoothPublish) {
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            mgr.check_boundary(playerX, playerY);
            if (mgr.consume_composite_dirty()) {
                return fail("no-road composite published unexpected smoothing");
            }
        }
    }

    const int roadMaskTiles = mgr.composite_road_mask_tiles();
    if (expectSmoothPublish && roadMaskTiles <= 0) {
        return fail("road case did not expose road-mask indices");
    }
    if (!expectSmoothPublish && roadMaskTiles != 0) {
        return fail("plain case exposed unexpected road-mask indices");
    }
    std::vector<std::int32_t> roadMaskIndices;
    mgr.append_composite_road_mask_indices(roadMaskIndices);
    if (int(roadMaskIndices.size()) != roadMaskTiles) {
        return fail("road-mask index count mismatch");
    }
    for (std::int32_t idx : roadMaskIndices) {
        if (idx < 0 || std::size_t(idx) >= mgr.tiles().size()
            || mgr.tiles()[std::size_t(idx)] != sm::sub::TILE_ROAD) {
            return fail("road-mask index points outside TILE_ROAD");
        }
    }

    std::fprintf(stderr,
        "subworld_async_seam_test: %s dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        label, outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
    return true;
}

bool run_diagonal_plain_case(int& outDirty, sm::sub::SeamTiming& outTiming) {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_plain_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 16);
    float playerY = float(sm::sub::kCellSize * 2 + 24);
    mgr.check_boundary(playerX, playerY);

    if (mgr.center_cx() != 1 || mgr.center_cy() != 1) {
        return fail("center did not shift diagonally");
    }
    if (std::fabs(playerX - float(sm::sub::kCellSize + 16)) > 0.01f ||
        std::fabs(playerY - float(sm::sub::kCellSize + 24)) > 0.01f) {
        return fail("diagonal player frame was not recentered");
    }
    if (!mgr.consume_composite_dirty()) {
        return fail("diagonal boundary did not mark composite dirty");
    }
    outTiming = mgr.last_seam_timing();
    if (!outTiming.crossed || outTiming.smoothMs != 0.0) {
        return fail("diagonal timing missing or smoothing stayed on seam path");
    }
    if (!expect_placeholder(mgr, resolve_plain_cell,
            sm::sub::kCellSize * 2 + 16,
            sm::sub::kCellSize * 2 + 24,
            "diagonal exposed slot was not macro placeholder")) {
        return false;
    }

    outDirty = 0;
    for (int i = 0; i < 500 && outDirty < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            ++outDirty;
        }
    }
    if (outDirty != 5) {
        return fail("diagonal crossing did not stitch exactly five cells");
    }

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            return fail("diagonal no-road composite published unexpected smoothing");
        }
    }

    std::fprintf(stderr,
        "subworld_async_seam_test: diagonal dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
    return true;
}

bool run_snapshot_during_pending_case() {
    sm::sub::clear_saved_subworlds();

    {
        sm::sub::SeamlessSubworldManager mgr;
        mgr.init(0, 0, resolve_plain_cell);
        mgr.consume_composite_dirty();

        float playerX = float(sm::sub::kCellSize * 2 + 12);
        float playerY = float(sm::sub::kCellSize + 80);
        mgr.check_boundary(playerX, playerY);
        mgr.consume_composite_dirty();

        const sm::sub::CellContext leaving = resolve_plain_cell(-1, 0);
        const sm::sub::CellContext surviving = resolve_plain_cell(0, 0);
        mgr.snapshot_all_to_cache();

        if (!sm::sub::find_saved_subworld(leaving.seed, sm::sub::SubworldMode::Grassland)) {
            return fail("leaving real cell was not saved during pending generation");
        }
        if (!sm::sub::find_saved_subworld(surviving.seed, sm::sub::SubworldMode::Grassland)) {
            return fail("surviving real cell was not saved during pending generation");
        }
    }

    sm::sub::clear_saved_subworlds();
    std::fprintf(stderr, "subworld_async_seam_test: snapshot_pending ok\n");
    return true;
}

bool run_worker_restore_saved_case() {
    sm::sub::clear_saved_subworlds();

    constexpr float kSavedHeight = 0.73f;
    constexpr float kSavedBridgeX = 123.25f;
    constexpr float kSavedBridgeY = 277.5f;
    constexpr float kSavedBridgeRadius = 9.0f;
    constexpr float kSavedBridgeHeight = 3.5f;
    sm::sub::SubworldMapData savedMap;
    savedMap.heightmap.assign(
        std::size_t(sm::sub::kCellSize) * sm::sub::kCellSize, kSavedHeight);
    savedMap.structures.push_back(sm::sub::Structure{
        sm::sub::Structure::Bridge,
        kSavedBridgeX,
        kSavedBridgeY,
        kSavedBridgeRadius,
        kSavedBridgeHeight});
    const sm::sub::CellContext savedCtx = resolve_plain_cell(2, 0);
    sm::sub::store_saved_subworld(sm::sub::snapshot_subworld(
        savedCtx.seed, sm::sub::SubworldMode::Grassland, savedMap));

    {
        sm::sub::SeamlessSubworldManager mgr;
        mgr.init(0, 0, resolve_plain_cell);
        mgr.consume_composite_dirty();

        float playerX = float(sm::sub::kCellSize * 2 + 32);
        float playerY = float(sm::sub::kCellSize + 52);
        mgr.check_boundary(playerX, playerY);
        if (!mgr.consume_composite_dirty()) {
            return fail("restore saved boundary did not mark composite dirty");
        }
        const int sampleX = sm::sub::kCellSize * 2 + 37;
        const int sampleY = sm::sub::kCellSize + 53;
        if (!expect_placeholder(mgr, resolve_plain_cell, sampleX, sampleY,
                "restore saved slot was not placeholder before stitch")) {
            return false;
        }

        const float expected = float(std::lround((kSavedHeight / 2.5f) * 65535.0f))
            / 65535.0f * 2.5f;
        const float expectedBridgeX = float(sm::sub::kCellSize * 2) + kSavedBridgeX;
        const float expectedBridgeY = float(sm::sub::kCellSize) + kSavedBridgeY;
        bool restored = false;
        bool restoredStructure = false;
        for (int i = 0; i < 500; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            mgr.check_boundary(playerX, playerY);
            mgr.consume_composite_dirty();
            const std::size_t idx = sm::sub::tile_index(sampleX, sampleY);
            if (idx < mgr.heightmap().size()
                && std::fabs(mgr.heightmap()[idx] - expected) < 0.0001f) {
                restored = true;
            }
            restoredStructure = has_structure(mgr,
                sm::sub::Structure::Bridge,
                expectedBridgeX,
                expectedBridgeY,
                kSavedBridgeRadius,
                -kSavedBridgeHeight);
            if (restored && restoredStructure) {
                break;
            }
        }
        if (!restored) {
            return fail("worker generation did not restore saved heightmap");
        }
        if (!restoredStructure) {
            return fail("worker generation did not restore saved structure");
        }

        mgr.snapshot_all_to_cache();
    }

    sm::sub::clear_saved_subworlds();
    std::fprintf(stderr, "subworld_async_seam_test: worker_restore_saved ok\n");
    return true;
}

bool run_water_plane_invariant_case() {
    if (std::fabs(sm::sub::WATER_LEVEL - 0.40f) > 0.0001f) {
        return fail("WATER_LEVEL drifted from TS 0.40");
    }
    if (std::fabs(sm::sub::kLandMargin - 0.02f) > 0.0001f) {
        return fail("kLandMargin drifted from TS port margin 0.02");
    }

    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_water_plane_cell);
    mgr.consume_composite_dirty();

    const auto& tiles = mgr.tiles();
    const auto& heights = mgr.heightmap();
    const std::size_t expected =
        std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize;
    if (tiles.size() != expected || heights.size() != expected) {
        return fail("water-plane composite buffers have wrong size");
    }

    int waterTiles = 0;
    int landTiles = 0;
    int badWater = 0;
    int badLand = 0;
    float maxWater = -1.0f;
    float minLand = 2.0f;
    bool cellSeen[9] = {};

    constexpr float kEpsilon = 0.0001f;
    const float landFloor = sm::sub::WATER_LEVEL + sm::sub::kLandMargin;
    for (int y = 0; y < sm::sub::kFullSize; ++y) {
        const int cellY = y / sm::sub::kCellSize;
        for (int x = 0; x < sm::sub::kFullSize; ++x) {
            const int cellX = x / sm::sub::kCellSize;
            cellSeen[cellY * 3 + cellX] = true;
            const std::size_t idx = sm::sub::tile_index(x, y);
            const float h = heights[idx];
            if (tiles[idx] == sm::sub::TILE_WATER) {
                ++waterTiles;
                if (h > maxWater) maxWater = h;
                if (h > sm::sub::WATER_LEVEL + kEpsilon) ++badWater;
            } else {
                ++landTiles;
                if (h < minLand) minLand = h;
                if (h < landFloor - kEpsilon) ++badLand;
            }
        }
    }

    if (waterTiles <= 0 || landTiles <= 0) {
        return fail("water-plane test did not cover both water and land");
    }
    for (bool seen : cellSeen) {
        if (!seen) return fail("water-plane test did not scan all nine cells");
    }
    std::fprintf(stderr,
        "subworld_async_seam_test: water_plane water=%d land=%d "
        "badWater=%d badLand=%d maxWater=%.5f minLand=%.5f\n",
        waterTiles, landTiles, badWater, badLand, maxWater, minLand);
    if (badWater != 0 || badLand != 0) {
        sm::sub::clear_saved_subworlds();
        return fail("water-plane height invariant violated");
    }
    sm::sub::clear_saved_subworlds();
    return true;
}

bool run_water_placeholder_case() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(1, 0, resolve_water_plane_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 8);
    float playerY = float(sm::sub::kCellSize + 128);
    mgr.check_boundary(playerX, playerY);
    if (mgr.center_cx() != 2 || !mgr.consume_composite_dirty()) {
        return fail("water placeholder boundary crossing failed");
    }

    const int sampleX = sm::sub::kCellSize * 2 + 8;
    const int sampleY = sm::sub::kCellSize + 128;
    if (!expect_placeholder(mgr, resolve_water_plane_cell, sampleX, sampleY,
            "water exposed slot was not water-aware placeholder")) {
        return false;
    }

    const std::size_t idx = sm::sub::tile_index(sampleX, sampleY);
    if (mgr.tiles()[idx] != sm::sub::TILE_WATER
        || mgr.heightmap()[idx] >= sm::sub::WATER_LEVEL) {
        return fail("water placeholder did not stay below water plane");
    }

    sm::sub::clear_saved_subworlds();
    return true;
}

bool run_rapid_reversal_case(int& outDirty, sm::sub::SeamTiming& outTiming) {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_plain_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 20);
    float playerY = float(sm::sub::kCellSize + 64);
    mgr.check_boundary(playerX, playerY);
    if (mgr.center_cx() != 1 || !mgr.consume_composite_dirty()) {
        return fail("rapid reversal initial east crossing failed");
    }

    playerX = float(sm::sub::kCellSize - 20);
    mgr.check_boundary(playerX, playerY);
    if (mgr.center_cx() != 0 || !mgr.consume_composite_dirty()) {
        return fail("rapid reversal west crossing failed");
    }
    outTiming = mgr.last_seam_timing();
    if (!outTiming.crossed || outTiming.smoothMs != 0.0) {
        return fail("rapid reversal timing missing or smoothing stayed on seam path");
    }

    outDirty = 0;
    for (int i = 0; i < 500 && outDirty < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            ++outDirty;
        }
    }
    if (outDirty != 3) {
        return fail("rapid reversal did not stitch exactly current three cells");
    }

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            return fail("rapid reversal published stale or smoothing dirty event");
        }
    }

    std::fprintf(stderr,
        "subworld_async_seam_test: rapid_reversal dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
    return true;
}

} // namespace

int main() {
    const char* only = std::getenv("TIMAERT_SUBWORLD_ASYNC_CASE");
    int roadDirty = 0;
    int plainDirty = 0;
    int diagonalDirty = 0;
    int reversalDirty = 0;
    sm::sub::SeamTiming roadTiming{};
    sm::sub::SeamTiming plainTiming{};
    sm::sub::SeamTiming diagonalTiming{};
    sm::sub::SeamTiming reversalTiming{};
    if (only && std::strcmp(only, "road") == 0) {
        return run_case(resolve_cell, true, "road", roadDirty, roadTiming) ? 0 : 1;
    }
    if (only && std::strcmp(only, "plain") == 0) {
        return run_case(resolve_plain_cell, false, "plain", plainDirty, plainTiming) ? 0 : 1;
    }
    if (only && std::strcmp(only, "diagonal") == 0) {
        return run_diagonal_plain_case(diagonalDirty, diagonalTiming) ? 0 : 1;
    }
    if (only && std::strcmp(only, "reversal") == 0) {
        return run_rapid_reversal_case(reversalDirty, reversalTiming) ? 0 : 1;
    }
    if (only && std::strcmp(only, "snapshot_pending") == 0) {
        return run_snapshot_during_pending_case() ? 0 : 1;
    }
    if (only && std::strcmp(only, "worker_restore") == 0) {
        return run_worker_restore_saved_case() ? 0 : 1;
    }
    if (only && std::strcmp(only, "water_placeholder") == 0) {
        return run_water_placeholder_case() ? 0 : 1;
    }
    if (!run_case(resolve_cell, true, "road", roadDirty, roadTiming)) return 1;
    if (!run_case(resolve_plain_cell, false, "plain", plainDirty, plainTiming)) return 1;
    if (!run_diagonal_plain_case(diagonalDirty, diagonalTiming)) return 1;
    if (!run_rapid_reversal_case(reversalDirty, reversalTiming)) return 1;
    if (!run_snapshot_during_pending_case()) return 1;
    if (!run_worker_restore_saved_case()) return 1;
    if (!run_water_placeholder_case()) return 1;
    if (!run_water_plane_invariant_case()) return 1;

    std::fprintf(stderr,
        "subworld_async_seam_test: ok roadDirty=%d plainDirty=%d diagonalDirty=%d "
        "reversalDirty=%d roadGen=%.3fms plainGen=%.3fms diagonalGen=%.3fms reversalGen=%.3fms\n",
        roadDirty, plainDirty, diagonalDirty, reversalDirty,
        roadTiming.genMs, plainTiming.genMs, diagonalTiming.genMs, reversalTiming.genMs);
    return 0;
}
