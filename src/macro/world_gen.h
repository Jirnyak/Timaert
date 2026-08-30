// World genesis — THE one macro-world baker (CANON S8: мир → рельеф → климат →
// ресурсы → заселение; the generation ORDER is the law of causality).
//
// Extracted from the app's boot_world (2026-08-30) so the game boot and the
// headless balance harness raise THE SAME world through THE SAME function — a
// second generation path would be a second answer to "what world is this"
// (CANON S26), and the two worlds would drift apart silently. Pure L1: no SDL,
// no Vulkan, no event bus — the app attaches renderers and the bus AFTER
// genesis; the harness never needs them.
#pragma once

#include <cstdint>
#include <vector>

namespace sm {

struct GameState;
struct TerrainData;
struct TreePoint;
struct TreeLayer;
struct DepositLayer;
struct FeatureLayer;
struct ZoneLayer;
struct TreeGrid;
struct LandmarkGrid;
struct PathCostData;
struct LayerParameters;
namespace ecs { struct World; }

struct WorldGenParams {
    std::uint32_t seed = 0;
    int mapW = 1024;   // CANON S1: the torus is 1024×1024 cells
    int mapH = 1024;
    const LayerParameters* lpOverride = nullptr;  // null = defaults
    int targetTotalCities = 0;                    // 0 = derive from the map
    // The load path restores people from the macro snapshot (Session 17) —
    // only a NEW world gets a genesis of squads.
    bool spawnMacroNpcs = true;
    // Place the player squad at the first city and materialise his PlayerTag
    // entity. The starter kit is NOT dealt here — that is chargen, app-side.
    bool anchorPlayer = true;
    // stderr [worldgen]/[roads] report cards (the TIMAERT_BOOT_TRACE channel).
    bool trace = false;
};

// Caller-owned storage the genesis fills — the same members App keeps, so the
// boot passes its fields and the harness passes locals; nobody copies a world.
// Every pointer is required; genesis touches all of them.
struct WorldGenOut {
    GameState*              gs = nullptr;
    TerrainData*            terrain = nullptr;
    std::vector<TreePoint>* trees = nullptr;
    TreeLayer*              treeLayer = nullptr;
    DepositLayer*           deposits = nullptr;
    FeatureLayer*           features = nullptr;
    ZoneLayer*              zones = nullptr;
    TreeGrid*               treeGrid = nullptr;
    LandmarkGrid*           landmarkGrid = nullptr;
    PathCostData*           pathCost = nullptr;
    ecs::World*             world = nullptr;
};

void generate_macro_world(const WorldGenOut& out, const WorldGenParams& p);

}  // namespace sm
