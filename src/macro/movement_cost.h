// Movement cost — SP weights per biome × feature. Mirrors movement-cost.ts.
#pragma once
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

constexpr int   kMacroBaseSP = 10;
constexpr float kRestRecoveryPct = 0.10f;
constexpr int   kSubworldSpPer1000 = 10;

inline float biome_sp_weight(Biome b) {
    // Indexed by Biome id: Tundra..Water (0..9), Mountain (10). Mountain carries
    // the traversal cost that used to live on the FT_Mountain feature (5.0 → the
    // old 50-SP crossing), now that mountains are a biome, not a feature.
    static const float kW[11] = {
        2.5f, 2.5f, 3.0f, 2.0f, 2.0f, 3.5f, 3.0f, 2.0f, 2.5f, 10.0f, 5.0f,
    };
    const int idx = int(b);
    if (idx < 0 || idx >= int(sizeof(kW) / sizeof(kW[0]))) {
        return 2.0f;
    }
    return kW[idx];
}

inline float feature_sp_weight(FeatureType f) {
    switch (f) {
        case FT_Road:     return 1.0f;
        case FT_DirtRoad: return 1.5f;
        default:          return 0.0f;
    }
}

// Undergrowth drag of a forest-CLASS cell (macro/tree_layer.h
// is_forest_cell) — the weight FT_Tree used to carry. A road cut through a
// forest keeps its road weight: man-made features win over natural cover.
inline constexpr float kForestSpWeight = 3.0f;

inline float cell_sp_weight(Biome b, FeatureType f, bool forest = false) {
    float fw = feature_sp_weight(f);
    if (fw > 0.0f) return fw;
    if (forest) return kForestSpWeight;
    return biome_sp_weight(b);
}

inline int cell_sp_cost(Biome b, FeatureType f, bool forest = false) {
    return int(float(kMacroBaseSP) * cell_sp_weight(b, f, forest) + 0.5f);
}

} // namespace sm
