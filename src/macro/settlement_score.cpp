#include "macro/settlement_score.h"

#include <algorithm>

#include "macro/biomes.h"          // kMountainBiomeLevel
#include "macro/deposit_layer.h"
#include "macro/features.h"        // FeatureLayer::wrap_coord
#include "macro/map_generator.h"
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

// ── Deposit: the richest vein within reach, decayed by distance ─────────
int deposit_term(const SettlementSiteContext& ctx, int x, int y) {
    if (!ctx.w.deposits) return 0;
    int best = 0;
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const int d = std::max(std::abs(dx), std::abs(dy));
            // The kind's own worth column prices the site (kDepositDefs —
            // iron is the prize, stone and clay are common wealth). A cell
            // may carry several kinds (a vein IN a quarry) — the richest
            // prize prices the site. Presence is what settles people; a
            // temporarily dry vein still anchors a mining village.
            for (int k = 0; k < kDepositKindCount; ++k) {
                const DepositKind kind = DepositKind(k);
                if (!ctx.w.deposits->remaining_at(kind, x + dx, y + dy))
                    continue;
                best = std::max(best, deposit_def(kind).siteWorth >> d);
            }
        }
    }
    return best;
}

} // namespace

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

    const SettlementScoreWeights& w =
        kSettlementScoreRows[std::size_t(row)
                                 < std::size_t(SettlementScoreRow::Count)
                             ? std::size_t(row) : 0];
    return w.arable  * arable_term(ctx, wx, wy)
         + w.water   * water_term(ctx, wx, wy)
         + w.forest  * forest_term(ctx, wx, wy)
         + w.deposit * deposit_term(ctx, wx, wy);
}

} // namespace sm
