// Politik — kingdoms drive the world. Mirrors politik.ts (compact registry).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "macro/language.h"

namespace sm {

enum class Lineage : std::uint8_t { Empire = 0, Magika, Timaert, Barbarians };

struct KingdomDef {
    const char* id;
    const char* name;
    Lineage     lineage;
    float       cx, cy;        // Normalised seed position [0,1]
    int         minCities, maxCities;
    std::uint32_t color_rgb;   // 0xRRGGBB
    int         priority;
    bool        capital_requires_lake;
};

inline const std::vector<KingdomDef>& kingdom_defs() {
    static const std::vector<KingdomDef> defs = {
        {"old_magica",       "Old Magica",       Lineage::Magika,    0.12f, 0.14f, 3, 6,  0xa78bfa, 1, false},
        {"northern_magica",  "Northern Magica",  Lineage::Magika,    0.40f, 0.12f, 8, 14, 0x7c3aed, 2, false},
        {"lower_magica",     "Lower Magica",     Lineage::Magika,    0.25f, 0.30f, 5, 10, 0xc4b5fd, 3, false},
        {"lake_duchy",       "Lake Duchy",       Lineage::Magika,    0.55f, 0.30f, 2, 4,  0x60a5fa, 4, true},
        {"empire",           "Empire of Light",  Lineage::Empire,    0.30f, 0.55f, 14, 22,0xf59e0b, 5, false},
        {"timaert",          "Republic of Timaert", Lineage::Timaert,0.80f, 0.40f, 5, 9,  0x10b981, 6, false},
        {"barbarian_north",  "North Barbarians", Lineage::Barbarians,0.10f, 0.85f, 2, 4,  0x991b1b, 7, false},
        {"barbarian_south",  "South Barbarians", Lineage::Barbarians,0.35f, 0.88f, 3, 6,  0xb91c1c, 8, false},
        {"barbarian_west",   "West Barbarians",  Lineage::Barbarians,0.60f, 0.85f, 2, 5,  0xdc2626, 9, false},
        {"barbarian_east",   "East Barbarians",  Lineage::Barbarians,0.85f, 0.85f, 2, 4,  0xef4444, 10, false},
    };
    return defs;
}

struct City {
    int x, y;
    std::string name;
    int kingdomIdx;       // index into kingdom_defs(), -1 = unowned
    int connections[8];   // up to 8 neighbour city indices, -1-terminated
    int population;
};

struct Kingdom {
    std::string id;
    std::string name;
    Lineage     lineage;
    int         capitalCityIdx;
    std::vector<int> cityIdxs;
    Language    language;
    std::uint32_t color;
};

struct Politik {
    std::vector<City>    cities;
    std::vector<Kingdom> kingdoms;
    std::vector<std::uint8_t> cellOwner; // size = mapW * mapH ; 0xff = unowned
    int mapW = 0, mapH = 0;
};

// Place capitals and scatter kingdom cities, build MST + extra inter-kingdom
// links. When `terrain` is provided, candidate positions are restricted to
// land tiles, and the minimum inter-city separation is derived from the
// land area / target city count (no hardcoded distance).
struct TerrainData;
// `targetTotalCities` (when > 0) overrides the registry totals: every
// kingdom's min/max is scaled by `target / registryTotal`, so the world
// ends up with approximately the requested number of cities while each
// kingdom keeps its relative weight. Min capped at 1 city per kingdom.
Politik generate_politik(std::uint32_t seed, int mapW, int mapH,
                        const TerrainData* terrain = nullptr,
                        std::uint8_t seaLevel8 = 0,
                        int targetTotalCities = 0);

// Belt-and-suspenders: nudge any city left on water onto the nearest land
// cell. Becomes mostly a no-op when `generate_politik` is called with a
// terrain pointer (cities are then placed on land directly).
void snap_cities_to_land(Politik& p, const TerrainData& td,
                         std::uint8_t seaLevel8, int radius = 80);

// Phase-2: rebuild `cellOwner` via multi-source 4-neighbour BFS over land
// cells. Each city is a seed; the first wave to reach a cell claims it.
// Waves cannot cross water — territories are bounded by coastlines.
// Mirrors politik.ts buildCellOwnership(). Optionally also lake-snaps any
// kingdom whose def has `capital_requires_lake` (currently Lake Duchy).
void finalize_politik(Politik& p, const TerrainData& td, std::uint8_t seaLevel8);

} // namespace sm
