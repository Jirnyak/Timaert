// Subworld tree species dispatch — pure CPU logic matching the macro
// temperature → tree-type mapping in the GLSL atlas bake shader.
#include "sub/tree_atlas.h"

namespace sm::sub {

// Mirror the macro temperature-driven tree-type dispatch in TREE_MAP_GLSL,
// but indexed by biome (subworld doesn't carry the temperature texture):
//   cold   → Pine (4) / Birch (2)
//   cool   → Birch (2) / Autumn (3)
//   warm   → Oak (0) / Willow (5) / Cherry (1)
//   hot    → Jungle (6)
int tree_type_for_temperature(float temperature, float hash) {
    if (temperature < 0.20f) return 4;
    if (temperature < 0.35f) return hash < 0.45f ? 4 : 2;
    if (temperature < 0.50f) return hash < 0.45f ? 2 : 3;
    if (temperature < 0.65f) {
        return hash < 0.40f ? 0 : (hash < 0.70f ? 3 : 5);
    }
    if (temperature < 0.80f) {
        return hash < 0.35f ? 1 : (hash < 0.65f ? 0 : 5);
    }
    return 6;
}

int tree_type_for(Biome b, float hash) {
    switch (b) {
        case Biome::Tundra:
        case Biome::Snow:   return 4;                          // PINE
        case Biome::Taiga:  return hash < 0.6f ? 4 : 2;        // PINE / BIRCH
        case Biome::Valley: return hash < 0.5f ? 2 : 3;        // BIRCH / AUTUMN
        case Biome::Meadow: return hash < 0.45f ? 0
                                   : (hash < 0.80f ? 3 : 5);   // OAK / AUTUMN / WILLOW
        case Biome::Steppe: return hash < 0.7f ? 0 : 5;        // OAK / WILLOW
        case Biome::Swamp:  return hash < 0.5f ? 5 : 6;        // WILLOW / JUNGLE
        case Biome::Desert: return hash < 0.5f ? 5 : 0;        // sparse OAK/WILLOW
        case Biome::Tropics:return 6;                          // JUNGLE
        case Biome::Water:  return 0;
    }
    return 0;
}

} // namespace sm::sub
