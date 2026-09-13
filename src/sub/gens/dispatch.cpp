// Open-air dispatch — resolve a cell's KIND, hand it to that kind's module,
// and own the two passes that belong to no module: the terrain the modules
// build on, and the reconciliation they all owe the water plane afterwards.
//
// The registry is a TABLE, not a switch. The switch cost four edits per new
// kind (enum, forward declaration, case, and a second case in resolve_mode)
// and silently answered "city" for kinds nobody had written yet; a row costs
// one, and the static_assert below refuses to build if a row goes missing.
#include "sub/gens/dispatch.h"

#include "sub/gens/gens.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/wall.h"
#include "sub/dgn/dispatch.h"
#include "sub/base_generator.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <cstdint>

namespace sm::sub {

// One row per SubworldMode: which module writes a cell of this kind.
//
// `gen` may be null for a mode that is not written by an open-air module —
// Dungeon is routed to sub/dgn/ before dispatch ever reaches the table.
struct GenKindRow {
    SubworldMode mode;
    void (*gen)(const GenInput&, SubworldMapData&);
};

constexpr GenKindRow kGenKindRows[] = {
    { SubworldMode::Open,      gen_open     },
    { SubworldMode::City,      gen_city     },
    { SubworldMode::Village,   gen_village  },
    { SubworldMode::Forest,    gen_forest   },
    { SubworldMode::Mountain,  gen_mountain },
    { SubworldMode::Swamp,     gen_swamp    },
    { SubworldMode::Ruin,      gen_ruin     },
    { SubworldMode::Water,     gen_water    },
    // Grassland is open ground that happens to carry no massif — the same
    // module, kept as its own mode because the RESOLVER distinguishes them.
    { SubworldMode::Grassland, gen_open     },
    { SubworldMode::Road,      gen_road     },
    { SubworldMode::Spire,     gen_spire    },
    { SubworldMode::Field,     gen_field    },
    { SubworldMode::Dungeon,   nullptr      },
};
static_assert(rows_in_enum_order(kGenKindRows, &GenKindRow::mode),
              "kGenKindRows row order must mirror SubworldMode");

SubworldMode resolve_mode(const CellContext& ctx) {
    // An interior context outranks everything: the cell shows what stands
    // BEHIND a door, not the land the door stands on (sub/dgn/).
    if (ctx.dungeon.kind != DungeonRef::None) return SubworldMode::Dungeon;
    const FeatureType feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    switch (ctx.landmark.kind) {
        case LandmarkType::City:    return SubworldMode::City;
        case LandmarkType::Village: return SubworldMode::Village;
        case LandmarkType::Ruin:    return SubworldMode::Ruin;
        case LandmarkType::Spire:   return SubworldMode::Spire;
        // Registry kinds no world places yet (Lair/Shrine/Mine/Tower): the
        // cell shows its LAND until each kind's generator module lands. It
        // used to fall through to a `landmark.id >= 0 -> City` catch-all one
        // line below, which meant the day a castle was placed on the map it
        // would raise a city — walls, keep and four hundred houses (GEN-5).
        // A kind with no module is a kind with no buildings, and that is the
        // honest answer until its TU exists.
        default:                    break;
        case LandmarkType::None:    break;
    }
    // Features come before the biome base: what men built on the cell decides
    // what the cell IS underfoot, then the biome fills in the terrain, then
    // trees compose on top (a forested peak is a Mountain cell with scattered
    // trees, not Forest mode). Features never fight each other — the layer is
    // one byte per cell, a road OR a field, settled at macro stamp time
    // (peasants plough around the road, stamp_field_features).
    if (feature == FT_Road)     return SubworldMode::Road;
    if (feature == FT_DirtRoad) return SubworldMode::Road;
    // A bridge is the road continued over a water cell: Road mode carves the
    // line, and gen_road's Biome::Water branch raises the span. The crews'
    // wooden bridge (v72) rides the same mode — the deck material is a
    // rendering nuance, the crossing is the crossing.
    if (feature == FT_Bridge)     return SubworldMode::Road;
    if (feature == FT_WoodBridge) return SubworldMode::Road;
    if (feature == FT_Field)    return SubworldMode::Field;
    if (ctx.biome == Biome::Mountain) return SubworldMode::Mountain;
    if (ctx.biome == Biome::Water)    return SubworldMode::Water;
    if (ctx.biome == Biome::Swamp)    return SubworldMode::Swamp;
    // Forest is a COUNT class, not a feature: massif interiors (tree count ≥
    // half the golden max) generate as Forest; edges/ambience stay open with
    // their trees composed on top by the count-driven scatter.
    if (is_forest_cell(ctx.treeCount)) return SubworldMode::Forest;
    return SubworldMode::Grassland;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const LandmarkType* nbLandmark,
                       const int* nbTreeCount,
                       const float* nbFertility) {
    CellContext safeCtx = ctx;
    safeCtx.feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    // Interior scene: the dungeon module writes the whole cell itself (flat
    // field, sealed geometry) — the open-air terrain pipeline below would
    // only be thrown away.
    if (safeCtx.dungeon.kind != DungeonRef::None) {
        dispatch_generate_dungeon(safeCtx, out);
        return;
    }
    std::uint8_t safeFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        safeFeature[i] = std::uint8_t(FeatureLayer::decode(nbFeature[i]));
    }

    // Per-cell fertility ring for the field-plot module. Missing array or a
    // negative entry = "unknown" — fall back to the centre cell's own value
    // (synthetic/test contexts).
    float safeFertility[9];
    for (int i = 0; i < 9; ++i) {
        safeFertility[i] = (nbFertility && nbFertility[i] >= 0.0f)
            ? nbFertility[i] : ctx.fertility01;
    }

    // Per-cell tree counts (macro TreeLayer). A missing array or a negative
    // entry means "unknown" (synthetic/test contexts without the layer) and
    // falls back to the ring cell's biome ambience — bare contexts have no
    // massif mask, so the forest term is zero by definition.
    int safeTreeCount[9];
    for (int i = 0; i < 9; ++i) {
        if (nbTreeCount && nbTreeCount[i] >= 0) {
            safeTreeCount[i] = std::min(nbTreeCount[i], kMaxTreesPerCell);
        } else {
            safeTreeCount[i] = int(derived_tree_count(nbBiome[i], 0.0f));
        }
    }

    // Universal terrain flattening: one TerrainMod per neighbour, resolved
    // from its effective landmark + feature (roads/settlements calm the
    // terrain they stand on — see base_generator.h). Without neighbour
    // landmark data the centre cell still flattens itself.
    TerrainMod nbMods[9]{};
    for (int i = 0; i < 9; ++i) {
        const LandmarkType lm = nbLandmark
            ? nbLandmark[i]
            : (i == 4 ? effective_landmark(safeCtx) : LandmarkType::None);
        nbMods[i] = terrain_mod_for(lm, FeatureType(safeFeature[i]));
    }

    out.heightmap.clear();
    generate_heightmap(out.heightmap, kCellSize, nbHeights, nbBiome,
                       safeCtx.biome, safeCtx.seed,
                       safeCtx.cx * kCellSize, safeCtx.cy * kCellSize,
                       nbMods, safeCtx.worldCellsX);
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();
    out.waterLevel = WATER_LEVEL;

    const GenInput in{safeCtx, nbBiome, safeFeature, safeTreeCount, safeFertility};
    const std::size_t row = std::size_t(resolve_mode(safeCtx));
    if (row < std::size(kGenKindRows) && kGenKindRows[row].gen != nullptr) {
        kGenKindRows[row].gen(in, out);
    } else {
        gen_open(in, out);
    }

    // TS-faithful post-pass: smooth heightmap under road / square tiles so
    // paths look like they were carved into the relief instead of riding
    // its bumps. No-op when the cell has no roads. Mirrors `smoothRoadHeights`
    // applied at the end of `BaseGenerator.generateHeightmap` in TS.
    smooth_road_heights(out.heightmap, out.tiles, kCellSize, kCellSize);
    // …and only NOW does a lifted span know what it bridges: the pass above
    // was still cutting the roadway a gateway stands on (kit/wall.h).
    kit::seat_lifted_spans(out);
    kit::sync_water_tiles_from_heightmap(out);
}

} // namespace sm::sub
