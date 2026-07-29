// 3x3 biome matrix — temperature × moisture. Mirrors src/game/biomes.ts.
#pragma once
#include <cstdint>
#include <array>

namespace sm {

enum Biome : std::uint8_t {
    Tundra = 0, Taiga = 1, Snow = 2,
    Valley = 3, Meadow = 4, Swamp = 5,
    Desert = 6, Steppe = 7, Tropics = 8,
    Water = 9,
    // Elevation-classified biomes live outside the 3x3 climate matrix, exactly
    // like Water. Mountain is the massif base terrain (see biome_at); trees and
    // roads remain orthogonal *features* composed on top of it.
    Mountain = 10,
};
// Number of biomes produced by the 3x3 temperature x moisture matrix. Water
// (9) and Mountain (10) are elevation overrides outside the matrix, so this
// count is unchanged by their existence.
constexpr int kBiomeLandCount = 9;

struct BiomeDef {
    Biome id;
    const char* name;
    float r, g, b;
};

inline constexpr BiomeDef kBiomes[11] = {
    {Tundra,   "Tundra",   0.50f, 0.52f, 0.45f},
    {Taiga,    "Taiga",    0.22f, 0.38f, 0.28f},
    {Snow,     "Snow",     0.90f, 0.92f, 0.96f},
    {Valley,   "Valley",   0.55f, 0.52f, 0.32f},
    {Meadow,   "Meadow",   0.40f, 0.52f, 0.28f},
    {Swamp,    "Swamp",    0.28f, 0.38f, 0.22f},
    {Desert,   "Desert",   0.82f, 0.72f, 0.48f},
    {Steppe,   "Steppe",   0.68f, 0.60f, 0.32f},
    {Tropics,  "Tropics",  0.10f, 0.35f, 0.10f},
    {Water,    "Water",    0.18f, 0.30f, 0.55f},
    {Mountain, "Mountain", 0.55f, 0.53f, 0.50f},
};

// Temperature row × moisture col → Biome. Mirrors BIOME_MATRIX +
// `biomeFromClimate` (round-to-nearest of t01 * (rows-1)).
inline Biome biome_from_climate(float temperature01, float moisture01) {
    int row = int(temperature01 * 2.0f + 0.5f); if (row > 2) row = 2; if (row < 0) row = 0;
    int col = int(moisture01    * 2.0f + 0.5f); if (col > 2) col = 2; if (col < 0) col = 0;
    static const Biome kMatrix[3][3] = {
        {Tundra, Taiga,  Snow},
        {Valley, Meadow, Swamp},
        {Desert, Steppe, Tropics},
    };
    return kMatrix[row][col];
}

// Cells at or above this normalized elevation are the Mountain biome (the
// procedural massif), overriding the climate matrix — the elevation parallel to
// how cells below sea level are Water. Trees/roads remain features on top.
inline constexpr float kMountainBiomeLevel = 0.75f;

// The single elevation-aware biome classifier: Water below sea level, Mountain
// at/above the massif line, otherwise the climate matrix. This is the CPU
// mirror of the shader's bt_biome (shaders/macro.frag); keep the two in sync.
inline Biome biome_at(float temperature01, float moisture01, float height01,
                      float seaLevel, float mountainLevel) {
    if (height01 < seaLevel)       return Water;
    if (height01 >= mountainLevel) return Mountain;
    return biome_from_climate(temperature01, moisture01);
}

} // namespace sm
