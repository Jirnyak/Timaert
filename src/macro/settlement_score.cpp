#include "macro/settlement_score.h"

#include <algorithm>

#include "macro/biomes.h"          // kMountainBiomeLevel
#include "macro/deposit_layer.h"
#include "macro/features.h"        // FeatureLayer::wrap_coord
#include "macro/map_generator.h"
#include "macro/npc_ai.h"          // kGathererReach — the crews' working box
#include "macro/resource_field.h"
#include "macro/spawners.h"        // kFieldsPerVillage — the parcels the term prices
#include "macro/tree_layer.h"

namespace sm {

namespace {

// Terms are 0..16. kTermMax is the contract every term clamps to; the
// score's ceiling is Σ weight × kTermMax, which tests may rely on.
constexpr int kTermMax = 16;

// ── Arable: what the site can plough ────────────────────────────────────
// Mean of the top-kFieldsPerVillage wheat-potential cells in the working
// ring — exactly the parcels stamp_field_features would embody. Read
// through the ONE registry door (baseline − scars), never the raw channel.
int arable_term(const SettlementSiteContext& ctx, int x, int y) {
    int best[kFieldsPerVillage] = {};
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            if (dx == 0 && dy == 0) continue;   // the town stands here
            const int wheat = resource_field_read(ctx.w, ResourceFieldId::Wheat,
                                                  x + dx, y + dy);
            // Insertion into the fattest-first shortlist.
            int at = -1;
            for (int k = 0; k < kFieldsPerVillage; ++k) {
                if (wheat > best[k]) { at = k; break; }
            }
            if (at < 0) continue;
            for (int k = kFieldsPerVillage - 1; k > at; --k) best[k] = best[k - 1];
            best[at] = wheat;
        }
    }
    int sum = 0;
    for (int k = 0; k < kFieldsPerVillage; ++k) sum += best[k];
    // Wheat baseline tops at kMaxWheatStandsPerCell = 4096 per cell:
    // mean/256 lands the term in 0..16.
    return std::min(kTermMax, (sum / kFieldsPerVillage) / 256);
}

// ── Water: the nearest drinkable/navigable cell ──────────────────────────
// Rivers are honest water cells (carved below sea level), so one predicate
// covers river and coast alike. Halving per cell of distance: 16/8/4/2.
int water_term(const SettlementSiteContext& ctx, int x, int y) {
    const TerrainData& td = *ctx.w.terrain;
    constexpr int kWaterReach = 4;
    for (int d = 1; d <= kWaterReach; ++d) {
        for (int dy = -d; dy <= d; ++dy) {
            for (int dx = -d; dx <= d; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != d) continue;
                const int wx = FeatureLayer::wrap_coord(x + dx, td.width);
                const int wy = FeatureLayer::wrap_coord(y + dy, td.height);
                if (td.is_water(wx, wy, ctx.seaLevel8)) return 32 >> d;
            }
        }
    }
    return 0;
}

// ── Forest: the thickest stand within reach ─────────────────────────────
int forest_term(const SettlementSiteContext& ctx, int x, int y) {
    if (!ctx.w.trees) return 0;
    int best = 0;
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy)
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx)
            best = std::max(best, int(ctx.w.trees->at(x + dx, y + dy)));
    // kMaxTreesPerCell = 16384 → /1024 lands in 0..16.
    return std::min(kTermMax, best / 1024);
}

// ── Deposit: the richest vein within the CREWS' working reach ────────────
// The score sees what the hands actually mine (owner 2026-08-31: silver
// sat 1/1024 mountain cells away from every settlement because this term
// looked 2 cells while the miners walk 16 — measured: minted = 0 for four
// game years on every seed). The reach field carries the splat
// (build_deposit_reach_field, ONE po2 distance ladder); a context without
// it prices no geology — the same fail-closed zero every absent layer
// answers.
int deposit_term(const SettlementSiteContext& ctx, int x, int y) {
    if (!ctx.depositReach || !ctx.w.terrain) return 0;
    const std::size_t idx =
        std::size_t(y) * std::size_t(ctx.w.terrain->width) + std::size_t(x);
    return int(ctx.depositReach[idx]);
}

} // namespace

SettlementSiteTerms settlement_site_terms(const SettlementSiteContext& ctx,
                                          int x, int y) {
    SettlementSiteTerms t{};
    if (!ctx.w.terrain || !ctx.w.terrain->has_rgba_storage()) return t;
    const TerrainData& td = *ctx.w.terrain;
    const int wx = FeatureLayer::wrap_coord(x, td.width);
    const int wy = FeatureLayer::wrap_coord(y, td.height);

    // Vetoes: nobody builds on water, inside a forest massif, or on the
    // mountain rock (fields already refuse it; so does the town).
    if (td.is_water(wx, wy, ctx.seaLevel8)) return t;
    if (float(td.height_at(wx, wy)) / 255.0f >= kMountainBiomeLevel) return t;
    if (ctx.w.trees && is_forest_cell(int(ctx.w.trees->at(wx, wy)))) return t;

    t.arable  = arable_term(ctx, wx, wy);
    t.water   = water_term(ctx, wx, wy);
    t.forest  = forest_term(ctx, wx, wy);
    t.deposit = deposit_term(ctx, wx, wy);
    return t;
}

int settlement_site_score(const SettlementSiteContext& ctx,
                          SettlementScoreRow row, int x, int y) {
    if (!ctx.w.terrain || !ctx.w.terrain->has_rgba_storage()) return -1;
    const TerrainData& td = *ctx.w.terrain;
    const int wx = FeatureLayer::wrap_coord(x, td.width);
    const int wy = FeatureLayer::wrap_coord(y, td.height);

    // Vetoes: nobody builds on water, inside a forest massif, or on the
    // mountain rock (fields already refuse it; so does the town).
    if (td.is_water(wx, wy, ctx.seaLevel8)) return -1;
    if (float(td.height_at(wx, wy)) / 255.0f >= kMountainBiomeLevel) return -1;
    if (ctx.w.trees && is_forest_cell(int(ctx.w.trees->at(wx, wy)))) return -1;

    const SettlementSiteTerms t = settlement_site_terms(ctx, wx, wy);
    const SettlementScoreWeights& w =
        kSettlementScoreRows[std::size_t(row)
                                 < std::size_t(SettlementScoreRow::Count)
                             ? std::size_t(row) : 0];
    return w.arable  * t.arable
         + w.water   * t.water
         + w.forest  * t.forest
         + w.deposit * t.deposit;
}

std::vector<std::uint16_t> build_deposit_reach_field(const DepositLayer& dl,
                                                     int mapW, int mapH) {
    std::vector<std::uint16_t> field;
    if (mapW <= 0 || mapH <= 0) return field;
    field.assign(std::size_t(mapW) * std::size_t(mapH), 0);
    // Veins are SPARSE — splatting each one's worth ladder over the crews'
    // working box is a few tens of millions of writes once per world, where
    // the per-candidate box scan it replaces priced billions of hash
    // lookups per generation.
    for (int k = 0; k < kDepositKindCount; ++k) {
        const int worth = kDepositDefs[k].siteWorth;
        for (const auto& [idx, remaining] : dl.cells[std::size_t(k)]) {
            (void)remaining;   // presence is what settles people
            const int cx = int(idx % std::uint32_t(mapW));
            const int cy = int(idx / std::uint32_t(mapW));
            for (int dy = -kGathererReach; dy <= kGathererReach; ++dy) {
                for (int dx = -kGathererReach; dx <= kGathererReach; ++dx) {
                    const int d = std::max(std::abs(dx), std::abs(dy));
                    // The ONE po2 distance ladder (village_pressure's law):
                    // full worth across the home-field box, halving per
                    // box-width beyond it.
                    const int v = d <= kSettlementReach
                        ? worth
                        : worth >> (d / (kSettlementReach + 1));
                    if (v <= 0) continue;
                    const int x = FeatureLayer::wrap_coord(cx + dx, mapW);
                    const int y = FeatureLayer::wrap_coord(cy + dy, mapH);
                    auto& slot =
                        field[std::size_t(y) * std::size_t(mapW)
                              + std::size_t(x)];
                    slot = std::uint16_t(
                        std::min(kTermMax, std::max(int(slot), v)));
                }
            }
        }
    }
    return field;
}

} // namespace sm
