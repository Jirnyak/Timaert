// THE macro-world genesis (world_gen.h). Bodily moved out of the app's
// boot_world (2026-08-30): the call SEQUENCE below is the owner's causality
// law (CANON S8) and must not be reordered — trees and deposits are pure
// functions of terrain + seed and are derived BEFORE politik because
// settlement placement reads them (R2); spires need the zone field; dirt
// lanes need the spires; fields never overwrite a road of either class.
#include "macro/world_gen.h"

#include <algorithm>
#include <cstdio>

#include "core/torus.h"
#include "ecs/world.h"
#include "macro/chronicle.h"
#include "macro/deposit_layer.h"
#include "macro/knowledge.h"
#include "macro/landmark_grid.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/pathfinding.h"
#include "macro/player_entity.h"
#include "macro/politik.h"
#include "macro/settlement_score.h"
#include "macro/spawners.h"
#include "macro/spells.h"
#include "macro/spires.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "macro/world_tick.h"
#include "macro/zones.h"

namespace sm {

void generate_macro_world(const WorldGenOut& out, const WorldGenParams& p) {
    GameState& gs = *out.gs;

    LayerParameters lp = p.lpOverride ? *p.lpOverride : LayerParameters{};
    // The % 100000 decimation is UI heritage: the seed box and the boot log
    // print five digits, and it is ALSO what once kept the value exact in the
    // float-typed seed this field used to be. The field is an integer now
    // (S26); the decimation stays so on-screen seeds keep naming the same
    // worlds they always did.
    lp.seed = p.seed % 100000u;
    gs = default_game_state(p.seed, p.mapW, p.mapH, lp, p.targetTotalCities);
    // A new world starts DARK (v40): all-Unknown knowledge. The spawn's
    // surroundings open by the ordinary sight law — the first frame's sweep
    // from the player's cell — not by a special starting reveal.
    knowledge_reset(gs.knowledge, gs.mapW, gs.mapH);
    // A new world has no past. Sized here, beside the knowledge layer, because
    // both are per-cell memories of the same map (macro/chronicle.h).
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    reset_world_tick_runtime(gs.worldTickRt, p.seed);

    *out.terrain = generate_terrain(gs.mapW, gs.mapH, lp);
    const std::uint8_t sea8 = std::uint8_t(lp.seaLevel * 255.0f);

    // The owner's causality IS the boot order: terrain → climate → RESOURCES →
    // and only then settlement. Trees and deposits are pure functions of
    // terrain + seed (neither reads politik), so they are derived before the
    // political layer — settlement placement reads them (R2).
    *out.trees = spawn_trees(*out.terrain, gs.worldSeed, lp.seaLevel);
    // The per-cell tree-count layer: the spawn_trees massif mask (the organic
    // FBM лесные массивы) carries the forest term, biomes add a small
    // ambience (16384 = the golden densest massif interior). This derivation
    // is the field's INITIAL CONDITION; the load path restores the saved
    // living grid after the swap.
    {
        std::vector<std::uint8_t> forestMask(
            std::size_t(gs.mapW) * std::size_t(gs.mapH), 0);
        for (const auto& t : *out.trees) {
            const std::size_t i = std::size_t(wrapi(t.y, gs.mapH))
                                * std::size_t(gs.mapW)
                                + std::size_t(wrapi(t.x, gs.mapW));
            if (i < forestMask.size()) forestMask[i] = 1;
        }
        *out.treeLayer = build_tree_layer(*out.terrain, forestMask.data(),
                                          forestMask.size());
    }
    *out.deposits = build_deposit_layer(*out.terrain, gs.worldSeed,
                                        lp.seaLevel);

    // The score context politics reads (R2): the crown decides how many
    // and whose, the ground decides where and how large.
    SettlementSiteContext siteCtx{};
    siteCtx.w.gs       = &gs;
    siteCtx.w.trees    = out.treeLayer;
    siteCtx.w.terrain  = out.terrain;
    siteCtx.w.deposits = out.deposits;
    siteCtx.seaLevel8  = sea8;
    gs.politik = generate_politik(gs.worldSeed, gs.mapW, gs.mapH, out.terrain,
                                  sea8, p.targetTotalCities, &siteCtx);
    snap_cities_to_land(gs.politik, *out.terrain, sea8);
    finalize_politik(gs.politik, *out.terrain, sea8);
    populate_landmarks_from_politik(gs, *out.terrain, sea8, *out.treeLayer,
                                    *out.deposits);
    if (p.trace) {
        // The R2 report card: how many villages actually stand next to the
        // resources the score placed them by. The old roulette scored ~25%
        // on water; the causality law should hold most of the world.
        int nearWater = 0, nearPlough = 0, nearDeposit = 0;
        std::vector<const Landmark*> cityRows, villageRows;
        for (const auto& lm : gs.landmarks) {
            if (lm.type == LandmarkType::City) cityRows.push_back(&lm);
            else if (lm.type == LandmarkType::Village)
                villageRows.push_back(&lm);
        }
        for (const Landmark* vp : villageRows) {
            const auto& v = *vp;
            bool water = false, plough = false, deposit = false;
            for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy)
                for (int dx = -kSettlementReach; dx <= kSettlementReach;
                     ++dx) {
                    const int x = wrapi(v.x + dx, gs.mapW);
                    const int y = wrapi(v.y + dy, gs.mapH);
                    if (out.terrain->is_water(x, y, sea8)) water = true;
                    else if (out.terrain->moisture_at(x, y)
                             >= kFieldMoistureMin) plough = true;
                    if (out.deposits->any_at(x, y)) deposit = true;
                }
            nearWater   += water   ? 1 : 0;
            nearPlough  += plough  ? 1 : 0;
            nearDeposit += deposit ? 1 : 0;
        }
        const int n = std::max(1, int(villageRows.size()));
        // A town with no hamlet at all reads as a bug; count them out loud.
        int villageless = 0;
        for (const Landmark* cp : cityRows) {
            bool has = false;
            for (const Landmark* vp : villageRows)
                if (vp->nearestCityId == cp->id) { has = true; break; }
            if (!has) ++villageless;
        }
        // And how far apart they actually stand: the mean nearest-neighbour
        // distance among villages of the SAME city — the number that reads
        // "scattered around the town" versus "clumped a block apart".
        long long nnSum = 0;
        int nnCount = 0;
        for (const Landmark* vp : villageRows) {
            const auto& v = *vp;
            int nearest = 1 << 20;
            for (const Landmark* op : villageRows) {
                const auto& o = *op;
                if (op == vp || o.nearestCityId != v.nearestCityId) continue;
                const int ddx = std::min(std::abs(v.x - o.x),
                                         gs.mapW - std::abs(v.x - o.x));
                const int ddy = std::min(std::abs(v.y - o.y),
                                         gs.mapH - std::abs(v.y - o.y));
                nearest = std::min(nearest, std::max(ddx, ddy));
            }
            if (nearest < (1 << 20)) { nnSum += nearest; ++nnCount; }
        }
        std::fprintf(stderr,
                     "[worldgen] cities=%zu villages=%zu villageless=%d "
                     "vilSpacing=%lld nearWater=%d%% nearPlough=%d%% "
                     "nearDeposit=%d%%\n",
                     cityRows.size(), villageRows.size(),
                     villageless, nnCount ? nnSum / nnCount : 0,
                     100 * nearWater / n, 100 * nearPlough / n,
                     100 * nearDeposit / n);
        std::fflush(stderr);
    }

    RoadTraceStats roadStats;
    auto roads = trace_roads(*out.terrain, gs.politik, &roadStats,
                             lp.seaLevel, out.treeLayer);
    if (p.trace) {
        std::fprintf(stderr,
                     "[roads] cities=%d attempted=%d kept=%d pruned=%d "
                     "componentPruned=%d expansions=%d\n",
                     roadStats.cityCount,
                     roadStats.attemptedEdges,
                     roadStats.keptEdges,
                     roadStats.prunedEdges,
                     roadStats.componentPrunedEdges,
                     roadStats.expansions);
        std::fflush(stderr);
    }
    const auto& citiesFlat = gs.politik.cities;
    // Macro invariant, city half: every city sits on a main road. Neighbour
    // road-stitching in the seamless subworld is feature-driven, so stamping
    // the road cell is what makes roads reach every settlement and adjacent
    // settlements merge with no seam. `build_feature_layer` still fails
    // settlement cells closed on water. (The village half of the invariant
    // now lives in trace_dirt_roads, which runs AFTER spires exist below.)
    {
        const int mw = gs.mapW, mh = gs.mapH;
        for (const auto& c : citiesFlat) {
            if (c.x < 0 || c.y < 0 || c.x >= mw || c.y >= mh) continue;
            const std::size_t idx =
                std::size_t(c.y) * std::size_t(mw) + std::size_t(c.x);
            if (idx < roads.size()) roads[idx] = 255;
        }
    }
    // Stone first (kRoadClasses hierarchy): the feature layer carries only
    // the main roads here; dirt lanes land in it AFTER zones and spires, so
    // their A* can both target the spires and price the stone at its bed.
    *out.features = build_feature_layer(*out.terrain, roads, nullptr,
                                        lp.seaLevel);
    build_tree_grid(*out.treeGrid, *out.trees, gs.mapW, gs.mapH);

    std::vector<ZoneSeed> zsCities, zsVills;
    for (auto& c : citiesFlat) zsCities.push_back({c.x, c.y});
    for (auto& v : gs.landmarks)
        if (v.type == LandmarkType::Village) zsVills.push_back({v.x, v.y});
    *out.zones = generate_zones(gs.mapW, gs.mapH, gs.worldSeed,
                                zsCities, zsVills, *out.features,
                                out.terrain->rgba.data(),
                                out.terrain->rgba.size(),
                                out.treeLayer);

    // Spires need the zone field (their placement law), so they are the one
    // landmark placed after generate_zones rather than in
    // populate_landmarks_from_politik (which cleared the list). One spire per
    // registered spell; a load overwrites gs.spires from the save afterwards
    // (boot_world_from_save), exactly like settlements.
    {
        generate_spires(gs, *out.zones, *out.terrain, sea8);
        // The landmark set is complete — bake the cell → landmark index the
        // whole game asks (macro/landmark_grid.h).
        *out.landmarkGrid = build_landmark_grid(gs);
        if (p.trace) {
            // The placement report card: every spell offered, every spire in
            // the wild band, and a spread that reads "scattered", not "heap".
            int zoneMin = 9, zoneMax = 0, minPair = gs.mapW + gs.mapH;
            std::vector<const Landmark*> spireRows;
            for (const auto& lm : gs.landmarks)
                if (lm.type == LandmarkType::Spire)
                    spireRows.push_back(&lm);
            for (const Landmark* spp : spireRows) {
                const auto& sp = *spp;
                const int z = int(out.zones->at(sp.x, sp.y));
                zoneMin = std::min(zoneMin, z);
                zoneMax = std::max(zoneMax, z);
                for (const Landmark* op : spireRows) {
                    const auto& o = *op;
                    if (op == spp) continue;
                    const int ddx = std::min(std::abs(sp.x - o.x),
                                             gs.mapW - std::abs(sp.x - o.x));
                    const int ddy = std::min(std::abs(sp.y - o.y),
                                             gs.mapH - std::abs(sp.y - o.y));
                    minPair = std::min(minPair, std::max(ddx, ddy));
                }
            }
            std::fprintf(stderr,
                         "[worldgen] spires=%zu/%d zones=[%d..%d] "
                         "minPairDist=%d\n",
                         spireRows.size(), kSpellCount,
                         spireRows.empty() ? 0 : zoneMin,
                         spireRows.empty() ? 0 : zoneMax, minPair);
            std::fflush(stderr);
        }
    }

    // Dirt lanes — the FT_DirtRoad rows of the road-class registry
    // (spawners.h kRoadClasses): village → its home city, village → the
    // nearest landmark within reach. Laid by THE find_path over the cost grid
    // that already prices the stone above at its bed, so lanes merge into the
    // highways; a village with no reachable target honestly gets no lane.
    {
        std::vector<VillageRoadSite> villageSites;
        for (const auto& v : gs.landmarks) {
            if (v.type != LandmarkType::Village) continue;
            VillageRoadSite site{};
            site.x = v.x;
            site.y = v.y;
            if (const Landmark* s = landmark_by_id(gs, v.nearestCityId);
                s && s->type == LandmarkType::City) {
                site.cityX = s->x;
                site.cityY = s->y;
                site.hasCity = true;
            }
            villageSites.push_back(site);
        }
        std::vector<RoadSite> landmarkSites;
        for (const auto& sp : gs.landmarks)
            if (sp.type == LandmarkType::Spire)
                landmarkSites.push_back({sp.x, sp.y});
        // Reach comes from THE distance law of the settled world
        // (politik.h derive_city_spacing) — one city spacing, not a magic
        // radius: a village's world ends about where the next town's begins.
        const int landmarkReach = derive_city_spacing(
            out.terrain, sea8, gs.mapW, gs.mapH, int(citiesFlat.size()));
        const int dirtStamped = trace_dirt_roads(
            *out.features, *out.terrain, villageSites, landmarkSites,
            landmarkReach, lp.seaLevel, out.treeLayer);
        if (p.trace) {
            std::size_t stoneCells = 0, dirtCells = 0, bridgeCells = 0;
            int bridgeX = -1, bridgeY = -1; // first span, for MACROPOS repros
            // ...and the first span with a road passing BESIDE it (more than
            // the two ends of its own crossing): the case where the subworld
            // has to put the fork on the bank, and the one worth looking at.
            int forkX = -1, forkY = -1;
            for (std::size_t i = 0; i < out.features->data.size(); ++i) {
                const std::uint8_t b = out.features->data[i];
                stoneCells += b == std::uint8_t(FT_Road) ? 1u : 0u;
                dirtCells += b == std::uint8_t(FT_DirtRoad) ? 1u : 0u;
                if (b == std::uint8_t(FT_Bridge)) {
                    const int bx = int(i % std::size_t(out.features->width));
                    const int by = int(i / std::size_t(out.features->width));
                    if (bridgeCells == 0u) { bridgeX = bx; bridgeY = by; }
                    if (forkX < 0) {
                        int roadNeighbours = 0;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                const FeatureType f =
                                    out.features->at(bx + dx, by + dy);
                                if (f == FT_Road || f == FT_DirtRoad
                                    || f == FT_Bridge) {
                                    ++roadNeighbours;
                                }
                            }
                        }
                        if (roadNeighbours > 2) { forkX = bx; forkY = by; }
                    }
                    ++bridgeCells;
                }
            }
            std::fprintf(stderr,
                         "[roads] stone cells=%zu dirt cells=%zu "
                         "bridge cells=%zu first_bridge=%d,%d "
                         "fork_bridge=%d,%d stamped=%d reach=%d\n",
                         stoneCells, dirtCells, bridgeCells, bridgeX, bridgeY,
                         forkX, forkY, dirtStamped, landmarkReach);
            std::fflush(stderr);
        }
    }
    // Farmland after every road (fields never overwrite a road of either
    // class): FT_Field stamped on the wettest land cells around each village
    // — contextual (moisture channel), deterministic. These cells are the
    // villages' grain deposit for the economy loop.
    {
        std::vector<FieldSite> fieldSites;
        for (const auto& v : gs.landmarks)
            if (v.type == LandmarkType::Village)
                fieldSites.push_back(FieldSite{v.x, v.y});
        stamp_field_features(*out.features, siteCtx.w, fieldSites,
                             lp.seaLevel);
    }

    *out.pathCost = build_cost_grid(*out.terrain, out.features, out.treeLayer);
    gs.lastWorldRebakeDay = gs.worldTime.day();

    // A loaded world does NOT respawn its people from the seed — the macro
    // snapshot restores them (Session 17); only a NEW world gets a genesis.
    if (p.spawnMacroNpcs) {
        spawn_macro_npcs(gs, *out.world, *out.terrain, gs.worldSeed,
                         out.deposits);
        // (Генезисный цензус профессий умер со сносом профессий — поручения
        // раздаёт аукцион ротации, CANON S10, владелец 2026-09-02.)
    }

    if (p.anchorPlayer) {
        if (!citiesFlat.empty()) {
            gs.player.x = float(citiesFlat[0].x);
            gs.player.y = float(citiesFlat[0].y);
        } else {
            gs.player.x = float(gs.mapW / 2);
            gs.player.y = float(gs.mapH / 2);
        }
        // macro-4a: materialise the player's persistent PlayerTag flag on the
        // macro map (Position + PlayerTag). The macro tick re-heals it
        // thereafter; doing it here makes the invariant hold immediately
        // after genesis, before the first tick.
        ensure_macro_player_entity(gs, *out.world);
    }
}

}  // namespace sm
