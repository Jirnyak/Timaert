// Politik — kingdoms drive the world. Mirrors politik.ts (compact registry).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "core/torus.h"
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
// ones on another.
//
// A settlement with no owner (kingdomIdx < 0) belongs to the FREE FOLK — the
// registry row for everyone who answers to no crown. It used to fall back to
// the empire, which quietly annexed every ownerless town in the world; now
// "unowned" is a state the data can express. A kingdom whose id has no registry
// row is a data error (faction_relations_test catches it at build time) and
// lands in the same place rather than on a garbage index.
inline std::uint16_t faction_index_for_kingdom(const Politik& politik,
                                               int kingdomIdx) {
    if (kingdomIdx >= 0 && kingdomIdx < int(politik.kingdoms.size())) {
        const int fi = faction_index(
            politik.kingdoms[std::size_t(kingdomIdx)].id.c_str());
        if (fi >= 0) return std::uint16_t(fi);
    }
    return std::uint16_t(faction_index("freefolk"));
}

// And the same question asked of the GROUND: who owns the cell at (cx,cy)?
// `cellOwner` is the politik layer's per-cell answer for the whole map, so a
// body that appears with no owner of its own — a scripted encounter, a console
// spawn, whatever system comes next — can still be placed honestly: it belongs
// to the realm whose land it is standing on, and to the free folk out in the
// unclaimed wilds. That is the fallback rule for "no context", and it needs no
// new state because the world already knows who holds every cell.
// Coordinates wrap (the map is a torus); a politik with no ownership map yet
// degrades to the free folk like any unowned ground.
inline std::uint16_t faction_index_for_cell(const Politik& politik,
                                            int cx, int cy) {
    const int w = politik.mapW;
    const int h = politik.mapH;
    if (w <= 0 || h <= 0
        || politik.cellOwner.size() != std::size_t(w) * std::size_t(h)) {
        return faction_index_for_kingdom(politik, -1);
    }
    const int wx = wrapi(cx, w);
    const int wy = wrapi(cy, h);
    const std::uint8_t owner =
        politik.cellOwner[std::size_t(wy) * std::size_t(w) + std::size_t(wx)];
    return faction_index_for_kingdom(politik, owner == 0xffu ? -1 : int(owner));
}

// THE distance law of the settled world, in one door: the minimum
// inter-city spacing, derived from land area and settlement count —
// nearest-neighbour distance of N points scattered over area A is
// ≈ √(A/N), and 60% of that is the rejection radius. City placement
// enforces it (generate_politik), and village hinterlands are HALF of
// it: a village belongs to the nearest city by construction, so half
// the spacing is exactly the land that city can claim. Land is counted
// by a subsampled scan (every 4th cell); a null/mismatched terrain
// counts the whole map as land.
struct TerrainData;
int derive_city_spacing(const TerrainData* terrain, std::uint8_t seaLevel8,
                        int mapW, int mapH, int totalCities);

// Place capitals and scatter kingdom cities, build MST + extra inter-kingdom
// links. When `terrain` is provided, candidate positions are restricted to
// land tiles, and the minimum inter-city separation is derived from the
// land area / target city count (no hardcoded distance).
// `targetTotalCities` (when > 0) overrides the registry totals: every
// kingdom's min/max is scaled by `target / registryTotal`, so the world
// ends up with approximately the requested number of cities while each
// kingdom keeps its relative weight. Min capped at 1 city per kingdom.
//
// `site` (R2) is the resource-score context: politics keeps deciding HOW
// MANY cities and WHOSE, the score decides WHERE among the candidates —
// and HOW LARGE (population derives from the site's capacity). A null
// site prices every cell equally (tests, resource-less callers), which
// degrades to the old first-valid placement.
struct SettlementSiteContext;
Politik generate_politik(std::uint32_t seed, int mapW, int mapH,
                        const TerrainData* terrain = nullptr,
                        std::uint8_t seaLevel8 = 0,
                        int targetTotalCities = 0,
                        const SettlementSiteContext* site = nullptr);

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
