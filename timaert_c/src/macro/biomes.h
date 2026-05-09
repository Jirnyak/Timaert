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
};
constexpr int kBiomeLandCount = 9;

struct BiomeDef {
    Biome id;
    const char* name;
    float r, g, b;
};

inline constexpr BiomeDef kBiomes[10] = {
    {Tundra,  "Tundra",  0.50f, 0.52f, 0.45f},
    {Taiga,   "Taiga",   0.22f, 0.38f, 0.28f},
    {Snow,    "Snow",    0.90f, 0.92f, 0.96f},
    {Valley,  "Valley",  0.55f, 0.52f, 0.32f},
    {Meadow,  "Meadow",  0.40f, 0.52f, 0.28f},
    {Swamp,   "Swamp",   0.28f, 0.38f, 0.22f},
    {Desert,  "Desert",  0.82f, 0.72f, 0.48f},
    {Steppe,  "Steppe",  0.68f, 0.60f, 0.32f},
    {Tropics, "Tropics", 0.10f, 0.35f, 0.10f},
    {Water,   "Water",   0.18f, 0.30f, 0.55f},
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

} // namespace sm
