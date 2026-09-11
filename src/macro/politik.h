// Politik — the settled world's geometry: where realms seed, which cities
// they grow, which roads join them, whose land every cell is.
//
// KINGDOMS ARE GONE (owner 2026-09-11: «королевств теперь нет, у нас только
// фракции — одна система»). The Kingdom struct was already a shell by then:
// its name/colour/temperament were materialized DUPLICATES of the faction
// registry (macro/faction.h, the single source of truth since 2026-07-30),
// its relations lived in the registry's temperament matrix, and the save
// never carried it. What survives it found new homes:
//   * a realm's IDENTITY — the faction registry row (was Kingdom::id);
//   * a city's OWNER — City::factionIdx and Landmark::factionIdx, a direct
//     registry index (was the kingdomIdx → kingdoms[i].id indirection);
//   * the SUZERAIN edge — Landmark::suzerainLandmarkId (was
//     Kingdom::capitalLandmarkId + the village's nearestCityId: one column,
//     CANON S24 «каждый узел знает только сюзерена»);
//   * the naming LANGUAGE — derived per faction (language.h
//     faction_language), never stored;
//   * the GEOMETRY seed below (RealmSeedDef — was KingdomDef: not a kingdom,
//     just where a faction's realm grows at genesis).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "core/torus.h"
#include "macro/language.h"
#include "macro/faction.h"

namespace sm {

// World-generation seed data for one realm: WHERE a faction's cities grow.
// IDENTITY — name, colour, description, temperament (which drives all
// relations) — is NOT here: `factionId` references a row of the faction
// registry, and everything the realm IS lives on that row.
struct RealmSeedDef {
    const char* factionId;     // must match a kFactionDefs row
    float       cx, cy;        // Normalised seed position [0,1]
    int         minCities, maxCities;
    int         priority;
    bool        capital_requires_lake;
};

inline const std::vector<RealmSeedDef>& realm_seed_defs() {
    static const std::vector<RealmSeedDef> defs = {
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
    std::int16_t factionIdx = -1;   // faction registry index; -1 = unowned
                                    //   (resolves through faction_or_freefolk)
    bool isCapital = false;         // the realm's crown city — the suzerain
                                    //   edge of every other city of its faction
    int connections[8];             // up to 8 neighbour city indices, -1-term.
    int population = 0;
};

struct Politik {
    std::vector<City>         cities;
    // Whose land each cell is: a FACTION REGISTRY INDEX per cell (it fits —
    // kMaxFactions is 64), 0xff = unowned wilds. It used to store a kingdom
    // index that then chased kingdoms[i].id through the registry; now the
    // byte IS the answer.
    std::vector<std::uint8_t> cellOwner; // size = mapW * mapH ; 0xff = unowned
    int mapW = 0, mapH = 0;
};

// Who owns the cell at (cx,cy)? The politik layer's per-cell answer for the
// whole map, so a body that appears with no owner of its own — a scripted
// encounter, a console spawn, whatever system comes next — can still be
// placed honestly: it belongs to the faction whose land it is standing on,
// and to the FREE FOLK out in the unclaimed wilds. Coordinates wrap (the map
// is a torus); a politik with no ownership map yet degrades to the free folk
// like any unowned ground.
inline std::uint16_t faction_index_for_cell(const Politik& politik,
                                            int cx, int cy) {
    const int w = politik.mapW;
    const int h = politik.mapH;
    if (w <= 0 || h <= 0
        || politik.cellOwner.size() != std::size_t(w) * std::size_t(h)) {
        return faction_or_freefolk(-1);
    }
    const int wx = wrapi(cx, w);
    const int wy = wrapi(cy, h);
    const std::uint8_t owner =
        politik.cellOwner[std::size_t(wy) * std::size_t(w) + std::size_t(wx)];
    return faction_or_freefolk(owner == 0xffu ? -1 : int(owner));
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

// Place capitals and scatter realm cities, build MST + extra inter-realm
// links. When `terrain` is provided, candidate positions are restricted to
// land tiles, and the minimum inter-city separation is derived from the
// land area / target city count (no hardcoded distance).
// `targetTotalCities` (when > 0) overrides the registry totals: every
// realm's min/max is scaled by `target / registryTotal`, so the world
// ends up with approximately the requested number of cities while each
// realm keeps its relative weight. Min capped at 1 city per realm.
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
// realm whose seed def has `capital_requires_lake` (currently Lake Duchy).
void finalize_politik(Politik& p, const TerrainData& td, std::uint8_t seaLevel8);

} // namespace sm
