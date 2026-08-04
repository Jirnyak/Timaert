// Politik — kingdoms drive the world. Mirrors politik.ts (compact registry).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "macro/language.h"
#include "macro/faction.h"

namespace sm {

// World-generation seed data for one kingdom. IDENTITY — name, colour,
// description, temperament (which drives all relations) — is NOT here: the id
// references a row in the faction registry (macro/faction.h), the single
// source of truth for every faction. This struct only says where the kingdom
// grows on the map. The old Lineage enum is gone; its one functional use
// (relations) became the registry's temperament matrix.
struct KingdomDef {
    const char* id;            // must match a kFactionDefs row
    float       cx, cy;        // Normalised seed position [0,1]
    int         minCities, maxCities;
    int         priority;
    bool        capital_requires_lake;
};

inline const std::vector<KingdomDef>& kingdom_defs() {
    static const std::vector<KingdomDef> defs = {
        {"old_magica",      0.12f, 0.14f, 3, 6,   1, false},
        {"northern_magica", 0.40f, 0.12f, 8, 14,  2, false},
        {"lower_magica",    0.25f, 0.30f, 5, 10,  3, false},
        {"lake_duchy",      0.55f, 0.30f, 2, 4,   4, true},
        {"empire",          0.30f, 0.55f, 14, 22, 5, false},
        {"timaert",         0.80f, 0.40f, 5, 9,   6, false},
        {"barbarian_north", 0.10f, 0.85f, 2, 4,   7, false},
        {"barbarian_south", 0.35f, 0.88f, 3, 6,   8, false},
        {"barbarian_west",  0.60f, 0.85f, 2, 5,   9, false},
        {"barbarian_east",  0.85f, 0.85f, 2, 4,  10, false},
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
    std::string name;              // materialized from the faction registry
    Temperament temperament;       // ditto — drives the UI label, not relations
    int         capitalCityIdx;
    std::vector<int> cityIdxs;
    Language    language;
    std::uint32_t color;           // ditto
};

struct Politik {
    std::vector<City>    cities;
    std::vector<Kingdom> kingdoms;
    std::vector<std::uint8_t> cellOwner; // size = mapW * mapH ; 0xff = unowned
    int mapW = 0, mapH = 0;
};

// THE settlement→faction resolver, and the only one: a settlement belongs to the
// faction of the KINGDOM that owns it — the honest ownership the politik layer
// computes and the save persists (Settlement/Village::kingdomIdx) — resolved to
// a registry index. Every populated place in the game (macro NPC spawns,
// subworld citizens, procedural quest rewards) asks THIS function, so a town of
// Old Magica can never again field imperial peasants on one layer and Magica
// ones on another. An unowned settlement (kingdomIdx < 0) or a kingdom whose id
// is missing from the registry degrades to the empire — the historical
// central-band behaviour, kept as one explicit fallback instead of a hardcode
// scattered across spawn sites.
inline std::uint16_t faction_index_for_kingdom(const Politik& politik,
                                               int kingdomIdx) {
    if (kingdomIdx >= 0 && kingdomIdx < int(politik.kingdoms.size())) {
        const int fi = faction_index(
            politik.kingdoms[std::size_t(kingdomIdx)].id.c_str());
        if (fi >= 0) return std::uint16_t(fi);
    }
    return std::uint16_t(faction_index("empire"));
}

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
