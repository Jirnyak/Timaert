// Movement cost — SP weights per biome × feature. Mirrors movement-cost.ts.
#pragma once
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

constexpr int   kMacroBaseSP = 10;
constexpr float kRestRecoveryPct = 0.10f;
constexpr int   kSubworldSpPer1000 = 10;

inline float biome_sp_weight(Biome b) {
    static const float kW[10] = {
        2.5f, 2.5f, 3.0f, 2.0f, 2.0f, 3.5f, 3.0f, 2.0f, 2.5f, 10.0f,
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
        case FT_Tree:     return 3.0f;
        case FT_Mountain: return 5.0f;
        default:          return 0.0f;
    }
}

inline float cell_sp_weight(Biome b, FeatureType f) {
    float fw = feature_sp_weight(f);
    return fw > 0.0f ? fw : biome_sp_weight(b);
}

inline int cell_sp_cost(Biome b, FeatureType f) {
    return int(float(kMacroBaseSP) * cell_sp_weight(b, f) + 0.5f);
}

} // namespace sm
