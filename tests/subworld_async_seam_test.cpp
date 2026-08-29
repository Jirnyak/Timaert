#include "check.h"

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
    c.landmark.id = -1;
    c.landmark.size = 0;
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
    c.landmark.id = -1;
    c.landmark.size = 0;
    c.landmark.kind = sm::LandmarkType::None;
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

// Records ONE check under `reason`, and hands the verdict back so a caller can
// keep the original early-exit on a broken placeholder.
bool expect_placeholder(const sm::sub::SeamlessSubworldManager& mgr,
                        sm::sub::CellContext (*resolver)(int, int),
                        int x,
                        int y,
                        const char* reason) {
    const std::size_t idx = sm::sub::tile_index(x, y);
    if (idx >= mgr.tiles().size() || idx >= mgr.heightmap().size()) {
        CHECK(false, reason);
        return false;
    }
    const int cellX = x / sm::sub::kCellSize;
    const int cellY = y / sm::sub::kCellSize;
    const sm::sub::CellContext ctx = resolver(
        mgr.center_cx() + cellX - 1,
        mgr.center_cy() + cellY - 1);
    const float expectedHeight = expected_placeholder_height(ctx);
    const std::uint8_t expectedTile = expected_placeholder_tile(ctx, expectedHeight);
    if (mgr.tiles()[idx] != expectedTile) {
        CHECK(false, reason);
        return false;
    }
    if (std::fabs(mgr.heightmap()[idx] - expectedHeight) > 0.0001f) {
        CHECK(false, reason);
        return false;
    }
    CHECK(true, reason);
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

void run_case(sm::sub::CellContext (*resolver)(int, int),
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

    CHECK_OR_RETURN(mgr.center_cx() == 1 && mgr.center_cy() == 0,
                    "center did not shift east");
    CHECK_OR_RETURN(std::fabs(playerX - float(sm::sub::kCellSize + 8)) <= 0.01f,
                    "player frame was not recentered");
    CHECK_OR_RETURN(mgr.consume_composite_dirty(),
                    "boundary did not mark composite dirty");
    outTiming = mgr.last_seam_timing();
    CHECK_OR_RETURN(outTiming.crossed && outTiming.smoothMs == 0.0,
                    "boundary timing missing or smoothing stayed on seam path");
    if (!expect_placeholder(mgr, resolver,
            sm::sub::kCellSize * 2 + 8,
            sm::sub::kCellSize + 128,
            "east exposed slot was not macro placeholder")) {
        return;
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

    CHECK_OR_RETURN(outDirty >= 3,
                    "worker jobs did not stitch back into composite");
    CHECK_OR_RETURN(!(expectSmoothPublish && outDirty < 4),
                    "async composite smoothing did not publish");

    if (!expectSmoothPublish) {
        bool spurious = false;
        for (int i = 0; i < 30 && !spurious; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            mgr.check_boundary(playerX, playerY);
            if (mgr.consume_composite_dirty()) {
                spurious = true;
            }
        }
        CHECK_OR_RETURN(!spurious,
                        "no-road composite published unexpected smoothing");
    }

    const int roadMaskTiles = mgr.composite_road_mask_tiles();
    CHECK_OR_RETURN(!(expectSmoothPublish && roadMaskTiles <= 0),
                    "road case did not expose road-mask indices");
    CHECK_OR_RETURN(!(!expectSmoothPublish && roadMaskTiles != 0),
                    "plain case exposed unexpected road-mask indices");
    std::vector<std::int32_t> roadMaskIndices;
    mgr.append_composite_road_mask_indices(roadMaskIndices);
    CHECK_OR_RETURN(int(roadMaskIndices.size()) == roadMaskTiles,
                    "road-mask index count mismatch");
    bool allRoad = true;
    for (std::int32_t idx : roadMaskIndices) {
        if (idx < 0 || std::size_t(idx) >= mgr.tiles().size()
            || mgr.tiles()[std::size_t(idx)] != sm::sub::TILE_ROAD) {
            allRoad = false;
            break;
        }
    }
    CHECK_OR_RETURN(allRoad, "road-mask index points outside TILE_ROAD");

    std::fprintf(stderr,
        "subworld_async_seam_test: %s dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        label, outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
}

void run_diagonal_plain_case(int& outDirty, sm::sub::SeamTiming& outTiming) {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_plain_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 16);
    float playerY = float(sm::sub::kCellSize * 2 + 24);
    mgr.check_boundary(playerX, playerY);

    CHECK_OR_RETURN(mgr.center_cx() == 1 && mgr.center_cy() == 1,
                    "center did not shift diagonally");
    CHECK_OR_RETURN(std::fabs(playerX - float(sm::sub::kCellSize + 16)) <= 0.01f
                        && std::fabs(playerY - float(sm::sub::kCellSize + 24)) <= 0.01f,
                    "diagonal player frame was not recentered");
    CHECK_OR_RETURN(mgr.consume_composite_dirty(),
                    "diagonal boundary did not mark composite dirty");
    outTiming = mgr.last_seam_timing();
    CHECK_OR_RETURN(outTiming.crossed && outTiming.smoothMs == 0.0,
                    "diagonal timing missing or smoothing stayed on seam path");
    if (!expect_placeholder(mgr, resolve_plain_cell,
            sm::sub::kCellSize * 2 + 16,
            sm::sub::kCellSize * 2 + 24,
            "diagonal exposed slot was not macro placeholder")) {
        return;
    }

    outDirty = 0;
    for (int i = 0; i < 500 && outDirty < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            ++outDirty;
        }
    }
    CHECK_OR_RETURN(outDirty == 5,
                    "diagonal crossing did not stitch exactly five cells");

    bool spurious = false;
    for (int i = 0; i < 30 && !spurious; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            spurious = true;
        }
    }
    CHECK_OR_RETURN(!spurious,
                    "diagonal no-road composite published unexpected smoothing");

    std::fprintf(stderr,
        "subworld_async_seam_test: diagonal dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
}

void run_snapshot_during_pending_case() {
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

        CHECK_OR_RETURN(
            sm::sub::find_saved_subworld(leaving.seed, sm::sub::SubworldMode::Grassland),
            "leaving real cell was not saved during pending generation");
        CHECK_OR_RETURN(
            sm::sub::find_saved_subworld(surviving.seed, sm::sub::SubworldMode::Grassland),
            "surviving real cell was not saved during pending generation");
    }

    sm::sub::clear_saved_subworlds();
    std::fprintf(stderr, "subworld_async_seam_test: snapshot_pending ok\n");
}

void run_worker_restore_saved_case() {
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
        CHECK_OR_RETURN(mgr.consume_composite_dirty(),
                        "restore saved boundary did not mark composite dirty");
        const int sampleX = sm::sub::kCellSize * 2 + 37;
        const int sampleY = sm::sub::kCellSize + 53;
        if (!expect_placeholder(mgr, resolve_plain_cell, sampleX, sampleY,
                "restore saved slot was not placeholder before stitch")) {
            return;
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
        CHECK_OR_RETURN(restored,
                        "worker generation did not restore saved heightmap");
        CHECK_OR_RETURN(restoredStructure,
                        "worker generation did not restore saved structure");

        mgr.snapshot_all_to_cache();
    }

    sm::sub::clear_saved_subworlds();
    std::fprintf(stderr, "subworld_async_seam_test: worker_restore_saved ok\n");
}

void run_water_plane_invariant_case() {
    CHECK_OR_RETURN(std::fabs(sm::sub::WATER_LEVEL - 0.40f) <= 0.0001f,
                    "WATER_LEVEL drifted from TS 0.40");
    CHECK_OR_RETURN(std::fabs(sm::sub::kLandMargin - 0.02f) <= 0.0001f,
                    "kLandMargin drifted from TS port margin 0.02");

    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_water_plane_cell);
    mgr.consume_composite_dirty();

    const auto& tiles = mgr.tiles();
    const auto& heights = mgr.heightmap();
    const std::size_t expected =
        std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize;
    CHECK_OR_RETURN(tiles.size() == expected && heights.size() == expected,
                    "water-plane composite buffers have wrong size");

    int waterTiles = 0;
    int landTiles = 0;
    int badWater = 0;
    int badLand = 0;
    float maxWater = -1.0f;
    float minLand = 2.0f;
    bool cellSeen[9] = {};

    constexpr float kEpsilon = 0.0001f;
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
                // Land must not sit BELOW the water plane (no submerged land).
                // The shore band [WATER_LEVEL, WATER_LEVEL + kLandMargin) is a
                // valid, intentional smooth beach — not a violation.
                if (h < sm::sub::WATER_LEVEL - kEpsilon) ++badLand;
            }
        }
    }

    CHECK_OR_RETURN(waterTiles > 0 && landTiles > 0,
                    "water-plane test did not cover both water and land");
    for (bool seen : cellSeen) {
        CHECK_OR_RETURN(seen, "water-plane test did not scan all nine cells");
    }
    std::fprintf(stderr,
        "subworld_async_seam_test: water_plane water=%d land=%d "
        "badWater=%d badLand=%d maxWater=%.5f minLand=%.5f\n",
        waterTiles, landTiles, badWater, badLand, maxWater, minLand);
    sm::sub::clear_saved_subworlds();
    CHECK(badWater == 0 && badLand == 0,
          "water-plane height invariant violated");
}

void run_water_placeholder_case() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(1, 0, resolve_water_plane_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 8);
    float playerY = float(sm::sub::kCellSize + 128);
    mgr.check_boundary(playerX, playerY);
    CHECK_OR_RETURN(mgr.center_cx() == 2 && mgr.consume_composite_dirty(),
                    "water placeholder boundary crossing failed");

    const int sampleX = sm::sub::kCellSize * 2 + 8;
    const int sampleY = sm::sub::kCellSize + 128;
    if (!expect_placeholder(mgr, resolve_water_plane_cell, sampleX, sampleY,
            "water exposed slot was not water-aware placeholder")) {
        return;
    }

    const std::size_t idx = sm::sub::tile_index(sampleX, sampleY);
    CHECK_OR_RETURN(mgr.tiles()[idx] == sm::sub::TILE_WATER
                        && mgr.heightmap()[idx] < sm::sub::WATER_LEVEL,
                    "water placeholder did not stay below water plane");

    sm::sub::clear_saved_subworlds();
}

void run_rapid_reversal_case(int& outDirty, sm::sub::SeamTiming& outTiming) {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_plain_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 20);
    float playerY = float(sm::sub::kCellSize + 64);
    mgr.check_boundary(playerX, playerY);
    CHECK_OR_RETURN(mgr.center_cx() == 1 && mgr.consume_composite_dirty(),
                    "rapid reversal initial east crossing failed");

    playerX = float(sm::sub::kCellSize - 20);
    mgr.check_boundary(playerX, playerY);
    CHECK_OR_RETURN(mgr.center_cx() == 0 && mgr.consume_composite_dirty(),
                    "rapid reversal west crossing failed");
    outTiming = mgr.last_seam_timing();
    CHECK_OR_RETURN(outTiming.crossed && outTiming.smoothMs == 0.0,
                    "rapid reversal timing missing or smoothing stayed on seam path");

    outDirty = 0;
    for (int i = 0; i < 500 && outDirty < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            ++outDirty;
        }
    }
    CHECK_OR_RETURN(outDirty == 3,
                    "rapid reversal did not stitch exactly current three cells");

    bool spurious = false;
    for (int i = 0; i < 30 && !spurious; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (mgr.consume_composite_dirty()) {
            spurious = true;
        }
    }
    CHECK_OR_RETURN(!spurious,
                    "rapid reversal published stale or smoothing dirty event");

    std::fprintf(stderr,
        "subworld_async_seam_test: rapid_reversal dirty=%d gen=%.3fms smooth=%.3fms total=%.3fms\n",
        outDirty, outTiming.genMs, outTiming.smoothMs, outTiming.totalMs);
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
        run_case(resolve_cell, true, "road", roadDirty, roadTiming);
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "plain") == 0) {
        run_case(resolve_plain_cell, false, "plain", plainDirty, plainTiming);
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "diagonal") == 0) {
        run_diagonal_plain_case(diagonalDirty, diagonalTiming);
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "reversal") == 0) {
        run_rapid_reversal_case(reversalDirty, reversalTiming);
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "snapshot_pending") == 0) {
        run_snapshot_during_pending_case();
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "worker_restore") == 0) {
        run_worker_restore_saved_case();
        return sm::test::report("subworld_async_seam_test");
    }
    if (only && std::strcmp(only, "water_placeholder") == 0) {
        run_water_placeholder_case();
        return sm::test::report("subworld_async_seam_test");
    }
    // The full sweep stops at the first failing case, exactly like the old
    // `if (!run_case(...)) return 1;` chain did — a failed seam case leaves
    // worker threads mid-flight, and the later cases' timeouts would just
    // burn seconds repeating the news.
    run_case(resolve_cell, true, "road", roadDirty, roadTiming);
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_case(resolve_plain_cell, false, "plain", plainDirty, plainTiming);
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_diagonal_plain_case(diagonalDirty, diagonalTiming);
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_rapid_reversal_case(reversalDirty, reversalTiming);
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_snapshot_during_pending_case();
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_worker_restore_saved_case();
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_water_placeholder_case();
    if (sm::test::failures() != 0) return sm::test::report("subworld_async_seam_test");
    run_water_plane_invariant_case();

    if (sm::test::failures() == 0) {
        std::fprintf(stderr,
            "subworld_async_seam_test: ok roadDirty=%d plainDirty=%d diagonalDirty=%d "
            "reversalDirty=%d roadGen=%.3fms plainGen=%.3fms diagonalGen=%.3fms reversalGen=%.3fms\n",
            roadDirty, plainDirty, diagonalDirty, reversalDirty,
            roadTiming.genMs, plainTiming.genMs, diagonalTiming.genMs, reversalTiming.genMs);
    }
    return sm::test::report("subworld_async_seam_test");
}
