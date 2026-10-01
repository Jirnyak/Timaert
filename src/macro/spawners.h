// CPU placement of trees, mountains, dirt roads + road-network tracing.
// Mirrors tree-spawner / mountain-spawner / dirt-road-spawner / road-network.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/politik.h"

namespace sm {

struct TreeLayer;

struct TreePoint { int x, y; };

struct RoadTraceStats {
    int cityCount = 0;
    int attemptedEdges = 0;
    int keptEdges = 0;
    int prunedEdges = 0;
    int componentPrunedEdges = 0;
    int expansions = 0;
};

std::vector<TreePoint> spawn_trees(const TerrainData& td, std::uint32_t seed);

// ── THE road-class registry (owner approved, 2026-08-29) ────────────────
// A road is a ROW: the surface it lays (whose bed weight is already a column
// of kFeatureDefs, macro/features.h) and WHO it connects — data, not code.
// Both classes are traced by the ONE A* (pathfinding.h find_path) over THE
// step-cost law: wide water rejected, ONE-CELL water payable at its honest
// water bed as a cardinal, square-on hop (find_path waterCrossAxes) — the
// crossing lands as FT_Bridge, and every bridge is stone regardless of which
// class paid for it (owner, 2026-08-29). The hierarchy is free because stone
// is traced FIRST and the dirt pass prices laid stone (bridges included) at
// its 1.0 bed, so lanes merge into the highways on their own. The old dirt tracer (spiral scan +
// straight lerp whose only worldly knowledge was "not water") is dead.
enum class RoadLink : std::uint8_t {
    // The politik connections graph (MST + inter-kingdom links): city↔city
    // and city↔capital edges both live there — trace_roads walks it.
    CityConnections,
    // Village → its own city (Village::nearestCityId).
    VillageHomeCity,
    // Village → the closest landmark of its province within reach (spires
    // today; ruins etc. join by being appended to the landmark list, not by
    // code). The nearest-TARGET choice is the old spiral's surviving job;
    // the LAYING is A* only.
    VillageNearestLandmark,
};
struct RoadClassDef {
    FeatureType surface;   // the bed it lays (feature_def(surface).bedWeight)
    RoadLink    link;      // who it connects
};
inline constexpr RoadClassDef kRoadClasses[] = {
    {FT_Road,     RoadLink::CityConnections},
    {FT_DirtRoad, RoadLink::VillageHomeCity},
    {FT_DirtRoad, RoadLink::VillageNearestLandmark},
};

// One-time road tracing between connected cities (the FT_Road rows above).
// Deliberately keeps the native terrain-cost A* baseline instead of TS
// corridor snapping. Cross-island pairs are component-pruned; same-island
// pairs use THE find_path (pathfinding.h) with the last-known-good large-map
// budget and treat rejected water cells as blocked road terrain.
std::vector<std::uint8_t> trace_roads(const TerrainData& td,
                                      // ПЛАН ГЕНЕРАТОРА, не слой мира
                                      // (M-90): трассер ходит по рёбрам
                                      // `connections` и ВЫЧЁРКИВАЕТ из них
                                      // те, что не легли, — поэтому план
                                      // приходит по неконстантной ссылке.
                                      std::vector<City>& cities,
                                      RoadTraceStats* stats = nullptr,
                                      // The living forest: the planner walks
                                      // THE step law, and the law's canopy
                                      // term routes roads around deep woods.
                                      const TreeLayer* treeLayer = nullptr);

// A village as the dirt tracer sees it: where it stands and where its home
// city stands (hasCity=false for an orphan — it then honestly gets no lane,
// never a lerp). Any point a dirt lane may target is a RoadSite.
struct RoadSite { int x = 0; int y = 0; };
struct VillageRoadSite {
    int x = 0; int y = 0;
    int cityX = 0; int cityY = 0;
    bool hasCity = false;
};

// Dirt lanes (the FT_DirtRoad rows of kRoadClasses): for every village, a
// lane to its home city and one to the nearest landmark within
// `landmarkReach` cells (derive it from the world's own distance law —
// derive_city_spacing — not from a constant). Runs AFTER stone is already in
// `features`, over the same cost grid the stone pass used (water rejected,
// canopy priced), so lanes climb around ridges and bogs at honest cost and
// merge into stone the moment it is cheaper. Stamps FT_DirtRoad only on
// FT_None cells (stone wins; fields are stamped later and never overwrite
// roads) and stamps every village's own cell (the settlement-on-a-road
// invariant the subworld's road stitching reads). Returns the number of
// cells stamped; fails closed to 0 on malformed terrain or a feature layer
// that does not cover it.
int trace_dirt_roads(FeatureLayer& features, const TerrainData& td,
                     const std::vector<VillageRoadSite>& villages,
                     const std::vector<RoadSite>& landmarks,
                     int landmarkReach,
                     const TreeLayer* treeLayer = nullptr);

// Build the FeatureLayer from terrain + roads. Features are the MAN-MADE
// structures composed ON TOP of the biome ground (dirt roads, then roads,
// last-writer-wins); mountains are the Mountain biome (elevation-classified,
// see biomes.h) and forests are the tree-count field (macro/tree_layer.h) —
// neither touches this grid.
FeatureLayer build_feature_layer(const TerrainData& td,
                                 const std::vector<std::uint8_t>& roadMask,
                                 const std::vector<std::uint8_t>* dirtMask);

// Stamp FT_Field farmland around villages — the owner-requested man-made
// feature and the grain DEPOSIT of the economy loop. Fully CONTEXTUAL, no
// RNG: for each village the ring of cells within Chebyshev radius 1..2 is
// scored by the cell's WHEAT POTENTIAL read through the resource-field
// registry (ResourceFieldId::Wheat — one door for fertility, so the parcels
// a village ploughs and the score that placed the village there can never
// drift apart), and the kFieldsPerVillage best land cells that carry no
// feature yet become fields. Never overwrites roads, never touches water,
// torus-wrapped. A future runtime "деревня распахала новое поле" is the
// same stamp on one cell.
//
// `world` carries the registry context (gs + terrain). Passing a context
// whose terrain is null falls back to nothing stamped — fail closed.
struct MacroWorld;
struct FieldSite { int x = 0; int y = 0; };
inline constexpr int kFieldsPerVillage = 4;
// ── ЗДЕСЬ СТОЯЛА ПЛАНКА ПАХОТЫ, И ОНА СНЕСЕНА (вердикт владельца 2026-10-01)
// Дословно: «снести планку, потому что у нас будет потом сделана новая
// экономика через поля ресурсов и агностичные веса и стоимости и потребности
// — там такая будет система, что никакие больше не понадобятся условности».
//
// Умерли `kFieldMoistureMin01` (байт 96 из 255) и `field_wheat_min()`. Планка
// была ступенью над ГЛАДКИМ полем: клетка с фертильностью 95 не родила бы
// ничего, с 96 — полный надел; а вывода у числа не было вовсе (ЗАКОН
// КОНСТАНТ), оно пришло из байтовой эпохи. По ЗАКОНУ КЛАМПА предел обязан
// наступать обратной связью мира, а не планкой: сколько уродится, решает сама
// фертильность, и на камне её и так нет — ровно тем доводом был снят горный
// запрет 2026-09-23 («поля тоже не запрет а ПО ФЕРТИЛЬНОСТИ СМОТРИМ»).
//
// ДЫРА ОСТАЁТСЯ ОТКРЫТОЙ И НАЗВАННОЙ (§1 «временные костыли не нужны»):
// пахотной стала всякая не-водная клетка, и различать богатую землю от тощей
// будет новая экономика, наряд M-191. Заглушки на место планки НЕ ставить.
void stamp_field_features(FeatureLayer& fl, const MacroWorld& world,
                          const std::vector<FieldSite>& villages);

// The RUNTIME half of the field stamp (owner 2026-08-31, CANON S10 «фичи
// создаются сквадами»): ONE cell ploughed by a crew's day of work — the
// same gates and the same FT_Field write the worldgen stamp lands, shared
// through plough_cell_ok, so a runtime field and a genesis field can never
// disagree. Returns false when the cell refuses the plough (feature there
// already / water / rock / lean soil). Torus-wrapped.
bool plough_cell_ok(const FeatureLayer& fl, const MacroWorld& world,
                    int x, int y, int& wheatOut);
// `parcel` — ЧЕМ засеяно (FT_Field хлебом, FT_FlaxField льном): земля и цена
// у них одни, различается только строка цели, которая с парцеллы берёт.
bool plough_field_cell(FeatureLayer& fl, const MacroWorld& world,
                       int x, int y, FeatureType parcel = FT_Field);

// The pasture's same two doors (CANON S10 «ЛОШАДЬ — ЮНИТ»): the plough
// gates with the HERD row's fertility bar, laying FT_Pasture. There is no
// worldgen stamp for pastures — a crew raises the first one the day the
// horse goal wins its auction, which is the same bootstrap every mine has.
bool pasture_cell_ok(const FeatureLayer& fl, const MacroWorld& world,
                     int x, int y, int& herdOut);
bool fence_pasture_cell(FeatureLayer& fl, const MacroWorld& world,
                        int x, int y);

} // namespace sm
