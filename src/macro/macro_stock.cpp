// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"

#include "ecs/world.h"
#include "macro/army.h"
#include "macro/deposit_layer.h"
#include "macro/fauna.h"
#include "macro/map_generator.h"
#include "macro/squad.h"     // record_deed — THE chronicle door (вердикт №9)
#include "macro/spawners.h"   // the field-stamp contract this file defines
                              //   the runtime half of (plough_*)
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/rng.h"

namespace sm {
namespace {

struct MacroStockRow {
    const char* id;
    int  (*read )(const MacroWorld&, MacroStockKey);
    void (*write)(MacroWorld&, MacroStockKey, int delta);
};

// ── tree_count: the forest of one cell — the registry's carrier row ───────
int read_tree_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Trees, k.cellX, k.cellY);
}

void write_tree_count(MacroWorld& w, MacroStockKey k, int delta) {
    resource_field_apply(w, ResourceFieldId::Trees, k.cellX, k.cellY, delta);
}

// ── population: the people of a named place ────────────────────────────────
// A settlement and a village are the same kind of subject here — a named place
// with people in it — and since v54 the id ALONE names it: every landmark
// draws on the one issuer (GameState::nextLandmarkOrdinal), so this walks both
// lists knowing at most one can answer. The register bit that used to
// disambiguate two zero-based numberings is dead.
int* find_population(const MacroWorld& w, std::int32_t subject) {
    if (!w.gs || subject < 0) return nullptr;
    if (Landmark* lm = landmark_by_id(*w.gs, subject)) return &lm->population;
    return nullptr;
}

int read_population(const MacroWorld& w, MacroStockKey k) {
    const int* p = find_population(w, k.subject);
    return p ? *p : 0;
}

void write_population(MacroWorld& w, MacroStockKey k, int delta) {
    if (delta == 0) return;
    int* p = find_population(w, k.subject);
    if (!p) return;
    // A place can be emptied but never owe people.
    *p = std::max(0, *p + delta);
}

// ── roster: the members of a squad standing on the map ─────────────────────
// The squad IS its leader entity (ecs::SquadRoster doctrine), so the subject
// of this row is the squad's save-stable MacroSpawnId ordinal — the one
// identity that survives the ECS never being serialized — and `detail` names
// the member (SoldierRecord::entityId, compared as a bit pattern because ids
// may use the high bit). This is what makes "a squad cannot hold zero members"
// a consequence instead of a special case: members die through this row, the
// leader dies through the tracked-body path, and a Dead leader with an empty
// roster simply is no squad any more — nothing extra removes it.
ecs::SquadRoster* find_roster(const MacroWorld& w, std::int32_t subject) {
    if (!w.world || subject < 0) return nullptr;
    auto view = w.world->reg.view<ecs::MacroSpawnId, ecs::SquadRoster>();
    for (auto e : view) {
        if (view.get<ecs::MacroSpawnId>(e).index == std::uint32_t(subject)) {
            return &view.get<ecs::SquadRoster>(e);
        }
    }
    return nullptr;
}

int read_roster(const MacroWorld& w, MacroStockKey k) {
    const ecs::SquadRoster* r = find_roster(w, k.subject);
    return r ? int(r->squad.size()) : 0;
}

void write_roster(MacroWorld& w, MacroStockKey k, int delta) {
    if (delta >= 0) {
        // The creation direction (recruitment, deserters re-raised) cannot be
        // conjured from a count — a member is a kind and a level, which the
        // producers write through add_soldiers with real rows. A bare positive
        // delta names nobody, so it moves nothing: fail closed, like every
        // other malformed receipt.
        return;
    }
    ecs::SquadRoster* r = find_roster(w, k.subject);
    if (!r) return;
    // The receipt names its member or it pays nothing: by entityId for a
    // storied soul, by {kind, level} for a generic one (detailLevel > 0 is
    // the pair's liveness — kind alone cannot be, Peasant is row 0).
    SoldierRecord who{};
    who.entityId = k.detail == -1 ? 0u : std::uint32_t(k.detail);
    who.kind = k.detailKind;
    who.level = k.detailLevel;
    if (who.entityId == 0 && who.level <= 0) return;
    for (int i = 0; i < -delta; ++i) {
        if (!remove_one_soldier(r->squad, who)) break;
    }
}

// ── garrison: the standing army of a NAMED place (§42 Инк 7) ──────────────
// The roster whose OWNER is a landmark rather than a squad — the same one
// SoldierSquad form, the same by-name strike (remove_one_soldier_by_
// entity_id, the exact helper the Roster row runs), a different address
// book: `subject` = the landmark's world-unique id (the v54 issuer), and
// `detail` names the member. A street guard's death pays here: killed on
// the wall = struck from the roll — «убил стража, в гарнизоне дыра».
SoldierSquad* find_garrison(const MacroWorld& w, std::int32_t subject) {
    if (!w.gs || subject < 0) return nullptr;
    Landmark* lm = landmark_by_id(*w.gs, subject);
    return lm ? &lm->garrison.squad : nullptr;
}

int read_garrison(const MacroWorld& w, MacroStockKey k) {
    const SoldierSquad* g = find_garrison(w, k.subject);
    return g ? total_soldiers(*g) : 0;
}

void write_garrison(MacroWorld& w, MacroStockKey k, int delta) {
    if (delta >= 0) {
        // The creation direction is recruiting (world_tick's garrison law)
        // — a bare positive delta names nobody, so it moves nothing.
        return;
    }
    SoldierSquad* g = find_garrison(w, k.subject);
    if (!g || k.detail == -1) return;
    for (int i = 0; i < -delta; ++i) {
        if (!remove_one_soldier_by_entity_id(*g, std::uint32_t(k.detail))) {
            break;
        }
    }
}

// ── The ONE resource-field container (macro/resource_field.h) ─────────────
// Baseline = pure terrain/climate (resources come BEFORE settlement — the
// owner's causality); storage follows the row's law (resource_field.h):
// sparse scar rows (fauna, wheat) and carrier rows (trees — the dense grid
// the map renders). Deposits join as carrier rows in Inc B.

// (field_cell_index умер 2026-09-18 вместе с хешем шрамов: поле индексирует
// себя само — клетка спрашивается координатами, а не ключом.)

// Wheat potential: the climate's fertility channel (master G — the same
// number the field stamp scores cells by), scaled to stands per cell by
// kMaxWheatStandsPerCell (resource_field.h — the row's own door, so the
// field stamp and the settlement score share this scale).
// Settlement (FT_Field parcels, garden plots) only decides WHERE this
// potential is embodied — it never enters the baseline.
int wheat_baseline(const MacroWorld& w, int x, int y) {
    const int wx = FeatureLayer::wrap_coord(x, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(y, w.terrain->height);
    const std::size_t idx =
        (std::size_t(wy) * std::size_t(w.terrain->width) + std::size_t(wx))
        * 4u + 1u;   // G = moisture, "the fertility" (macro/spawners.h)
    if (idx >= w.terrain->rgba.size()) return 0;
    return int(w.terrain->rgba[idx]) * kMaxWheatStandsPerCell / 255;
}

int fauna_baseline(const MacroWorld& w, int x, int y) {
    return fauna_cell_capacity_at(w, x, y);
}

// Trees: the carrier row. The live state is the dense TreeLayer grid the
// map renders (worldgen filled it; growth and felling move it from there —
// the virgin derivation is an initial condition, not an attractor), so
// read/apply go to the grid, and set_tree_count keeps clamp + revision at
// the layer's own door.
int trees_read(const MacroWorld& w, int x, int y) {
    return w.trees ? int(w.trees->at(x, y)) : 0;
}
void trees_apply(MacroWorld& w, int x, int y, int delta) {
    if (!w.trees || delta == 0) return;
    const int now = int(w.trees->at(x, y));
    set_tree_count(*w.trees, x, y, now + delta);
}

// The registry row id → the deposit layer's kind (the deposit rows are a
// contiguous enum block; the static_assert below keeps that a fact).
DepositKind deposit_kind_of(ResourceFieldId f) {
    return DepositKind(std::uint8_t(f) - std::uint8_t(ResourceFieldId::Clay));
}
static_assert(int(ResourceFieldId::Iron) == int(ResourceFieldId::Clay) + 1
                  && int(ResourceFieldId::Stone) == int(ResourceFieldId::Clay) + 2
                  && int(ResourceFieldId::Silver) == int(ResourceFieldId::Clay) + 3,
              "deposit rows must mirror DepositKind order");

// Clay/Iron/Stone: carrier rows over the deposit layer — one sparse map per
// kind, PRESENCE is the deposit (a dry vein reads 0 through a live cell; a
// cell with no vein reads 0 through absence and REFUSES writes — mining
// invents no geology; genesis goes through create_deposit, deliberately).
template <DepositKind K>
int deposit_read(const MacroWorld& w, int x, int y) {
    if (!w.deposits) return 0;
    return int(w.deposits->remaining_at(K, x, y));
}
template <DepositKind K>
void deposit_apply(MacroWorld& w, int x, int y, int delta) {
    if (!w.deposits || delta == 0) return;
    const std::int32_t r = w.deposits->remaining_at(K, x, y);
    if (r == 0) return;   // fail closed: no vein here to move
    set_deposit_remaining(*w.deposits, K, x, y, r + delta);
}

// ── The growth laws — the per-row CONTEXT of the one birth mechanism ──────

// Wheat: fertility is the whole context (the baseline already prices it),
// and the field turns over its own POTENTIAL — a quarter per visit (one
// visit per kGrowthEpochDays = a season), so a reaped parcel regrows its
// full baseline in a year. The seasonal-pulse harvest (autumn × moisture,
// W2 design) is deferred with field seasonality (field track); this is its
// smooth stand-in, and the divisor is a balance-run tunable. The old law —
// one stand per season regardless of fertility — fed the world ~7 grain a
// day and starved every farmer within a week of genesis (measured,
// balance_run 2026-08-30).
int wheat_growth_at(const MacroWorld& w, int x, int y) {
    return std::max(1, wheat_baseline(w, x, y) / kWheatSeasonsToRegrow);
}

// Fauna: beasts breed where beasts are. One head per visit while at least
// HALF the 3×3 valley is alive (the old 1/32-days rate at a living edge);
// a wiped-out region has nobody left to breed, and repopulates from its
// edges inward — or never, if the whole valley was emptied.
int fauna_growth_at(const MacroWorld& w, int x, int y) {
    int alive = 0, cap = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            cap   += fauna_cell_capacity_at(w,
                                            x + dx, y + dy);
            alive += resource_field_read(w, ResourceFieldId::Fauna,
                                         x + dx, y + dy);
        }
    }
    if (cap <= 0) return 0;
    return (2 * alive >= cap) ? 1 : 0;
}

// Trees: the forest plants the forest. Growth per visit derives from
// r = 1/1024 of the local 3×3 density per DAY:
//   perVisit = localSum9 × kGrowthEpochDays / (9 × 1024)
// so a clear-cut cell inside a living massif (neighbour density 16384)
// gains ~512/visit and crosses the forest class (8192) in ~14 visits ≈ 448
// days ≈ 3.5 game years — the owner's "вырубка среди леса зарастает ~4
// года". Below a local mean of ~288 trees the integer term is 0: scattered
// brush does not seed a forest. The biome GATE reuses the ambient table
// (no new constants): min(kBiomeBaseTreeCount, 1024)/1024 — taiga and
// tropics (≥1024) grow at full rate, meadow at ~0.6, desert (40) almost
// never, water not at all.
int trees_growth_at(const MacroWorld& w, int x, int y) {
    if (!w.trees || !w.gs || !w.terrain || !w.terrain->has_rgba_storage())
        return 0;
    int sum = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            sum += int(w.trees->at(x + dx, y + dy));
    int growth = sum * kGrowthEpochDays / (9 * 1024);
    if (growth <= 0) return 0;
    // THE cell cascade (map_generator.h biome_at_cell) — fail-closed to
    // Water, whose base tree count is 0: no storage, no growth.
    const Biome biome = biome_at_cell(*w.terrain, x, y);
    const int b = int(biome);
    const int base = biome_base_tree_count(b);
    return growth * std::min(base, 1024) / 1024;
}

// Horses: the herd a cell's GRASS can feed — the same fertility the wheat
// row prices, read as mouths: a mouth eats kDaysPerSeason × 4 food a year
// (one a day, the human ration the horse row shares), and the cell's grass
// yields its wheat baseline a year — so the best land (4096) carries 32
// head, lean land none. No new fertility door, no second constant.
int horses_baseline(const MacroWorld& w, int x, int y) {
    return wheat_baseline(w, x, y) / (kDaysPerSeason * 4);
}

// Horses breed where horses graze — the fauna law on the herd's own field:
// one head per seasonal visit while at least half the 3×3 valley's capacity
// is alive; an emptied range repopulates from its edges inward.
int horses_growth_at(const MacroWorld& w, int x, int y) {
    int alive = 0, cap = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            cap   += horses_baseline(w, x + dx, y + dy);
            alive += resource_field_read(w, ResourceFieldId::Horses,
                                         x + dx, y + dy);
        }
    }
    if (cap <= 0) return 0;
    return (2 * alive >= cap) ? 1 : 0;
}

// A VEIN KIND is born where it is SCARCE (the owner's negative context) —
// the lump its own row opens with; the walker's Geology domain owns the
// global scarcity roll and the host-cell pick (the W2c rule as a table row).
// One template for every metal: iron_growth_at and silver_growth_at were two
// copies of this line, and six metals would have been six.
template <DepositKind K>
int vein_growth_at(const MacroWorld&, int, int) {
    return deposit_vein_lump(K);
}

constexpr ResourceFieldDef kResourceFields[] = {
    /* Wheat */ {"wheat", &wheat_baseline,
                 GrowthDomain::OwnScars, &wheat_growth_at,
                 ResourceFieldId::Wheat, nullptr, nullptr, 0},
    /* Fauna */ {"fauna", &fauna_baseline,
                 GrowthDomain::OwnScars, &fauna_growth_at,
                 ResourceFieldId::Fauna, nullptr, nullptr, 0},
    // The forest reaches nobody: no gate asks «is there wood near» today.
    // And the day one does, this 0 is NOT the whole of it — the promise that
    // used to stand here («and nothing else») was false in two ways. Trees
    // are a CARRIER row: their live state is the TreeLayer, so
    // allocate_world_fields skips them and there is no ResourceGrid whose
    // reach disc could be stamped; and that allocator passes a literal 0 for
    // every scar row anyway, never reading this column. The reach a row
    // actually gets asked about today is the vein rows', owned by the deposit
    // layer (deposit_layer.cpp, kGathererReach). Teaching the forest the
    // neighbourhood question means giving the carrier rows a reach field of
    // their own — a build, not a flipped digit.
    /* Trees */ {"trees", nullptr,
                 GrowthDomain::CarrierGrid, &trees_growth_at,
                 ResourceFieldId::Trees, &trees_read, &trees_apply, 0},
    /* Horses */ {"horses", &horses_baseline,
                 GrowthDomain::OwnScars, &horses_growth_at,
                 ResourceFieldId::Horses, nullptr, nullptr, 0},
    /* Clay  */ {"clay",  nullptr,
                 GrowthDomain::None, nullptr, ResourceFieldId::Clay,
                 &deposit_read<DepositKind::Clay>,
                 &deposit_apply<DepositKind::Clay>, kGathererReach},
    /* Iron  */ {"iron",  nullptr,
                 GrowthDomain::Geology, &vein_growth_at<DepositKind::Iron>,
                 ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Iron>,
                 &deposit_apply<DepositKind::Iron>, kGathererReach},
    /* Stone */ {"stone", nullptr,
                 GrowthDomain::None, nullptr, ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Stone>,
                 &deposit_apply<DepositKind::Stone>, kGathererReach},
    /* Silver */ {"silver", nullptr,
                 GrowthDomain::Geology, &vein_growth_at<DepositKind::Silver>,
                 ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Silver>,
                 &deposit_apply<DepositKind::Silver>, kGathererReach},
    // The other two mint metals (v96): same born-where-scarce geology, same
    // stone host, their own rows — «новый род = новый массив и строка закона».
    /* Copper */ {"copper", nullptr,
                 GrowthDomain::Geology, &vein_growth_at<DepositKind::Copper>,
                 ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Copper>,
                 &deposit_apply<DepositKind::Copper>, kGathererReach},
    /* Gold  */ {"gold", nullptr,
                 GrowthDomain::Geology, &vein_growth_at<DepositKind::Gold>,
                 ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Gold>,
                 &deposit_apply<DepositKind::Gold>, kGathererReach},
};
static_assert(sizeof(kResourceFields) / sizeof(kResourceFields[0])
                  == std::size_t(ResourceFieldId::Count),
              "every ResourceFieldId needs its def — the table IS the system");
// THE VEIN ROWS' REACH, pinned where the decision would be made. The deposit
// layer sizes its fields with kGathererReach directly (its table cannot link
// this one — the growth laws below drag the ECS with them), so this is what
// keeps "the radius is the ROW's column" true rather than merely intended: a
// row that ever wants a different reach reddens here, at the table.
static_assert(kResourceFields[std::size_t(ResourceFieldId::Clay)].reachCells
                  == kGathererReach
              && kResourceFields[std::size_t(ResourceFieldId::Iron)].reachCells
                  == kGathererReach
              && kResourceFields[std::size_t(ResourceFieldId::Stone)].reachCells
                  == kGathererReach
              && kResourceFields[std::size_t(ResourceFieldId::Silver)].reachCells
                  == kGathererReach
              && kResourceFields[std::size_t(ResourceFieldId::Copper)].reachCells
                  == kGathererReach
              && kResourceFields[std::size_t(ResourceFieldId::Gold)].reachCells
                  == kGathererReach,
              "deposit_layer.cpp sizes the vein fields with kGathererReach; "
              "give a vein row its own reach and teach it that first");

ResourceGrid& scars_of(GameState& gs, ResourceFieldId f) {
    return gs.resourceScarCells[std::size_t(f)];
}

} // namespace

void allocate_world_fields(GameState& gs, int width, int height) {
    if (width <= 0 || height <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        ResourceGrid& g = gs.resourceScarCells[f];
        // A CARRIER row keeps no scar of its own — its live state IS its
        // carrier (the tree grid, the vein grids), so it pays for no field
        // here. The registry says which, so nobody has to remember.
        if (kResourceFields[f].carrierRead) continue;
        if (g.width == width && g.height == height && g.live()) continue;
        // Reach 0: the scar answers «сколько здесь», and no gate asks the
        // neighbourhood question of a scar (the vein rows, which do get
        // asked, own their reach inside the deposit layer).
        g.allocate(width, height, 0);
    }
    if (!(gs.worked.width == width && gs.worked.height == height
          && gs.worked.live())) {
        gs.worked.allocate(width, height, 0);
    }
}

// ── The plough: the runtime half of the field stamp (owner 2026-08-31) ───
// Declared in spawners.h beside the worldgen stamp; DEFINED here because
// the labour rotation (npc_ai.cpp) calls them and its test targets link
// the land's doors, not the worldgen.

int field_wheat_min() {
    // The ploughable bar, carried into the registry's units by the
    // wheat baseline's own scale — ONE fertility door, so raising the
    // bar or rescaling the field can never leave the two disagreeing.
    return int(kFieldMoistureMin) * kMaxWheatStandsPerCell / 255;
}

// The parcel GROUND gates, one truth for plough and fence alike (roads
// win, water refuses, no rock terraces): what differs between a field and
// a pasture is only WHICH row's fertility bars the gate, never the ground.
static bool parcel_ground_ok_(const FeatureLayer& fl, const MacroWorld& world,
                              int x, int y, float seaLevel,
                              std::size_t& idxOut) {
    std::size_t total = 0;
    if (!FeatureLayer::cell_count_for(fl.width, fl.height, total)
        || fl.data.size() < total || !world.terrain)
        return false;
    const TerrainData& td = *world.terrain;
    if (td.width != fl.width || td.height != fl.height
        || td.rgba.size() < total * 4u)
        return false;
    const int wx = FeatureLayer::wrap_coord(x, fl.width);
    const int wy = FeatureLayer::wrap_coord(y, fl.height);
    const std::size_t idx =
        std::size_t(wy) * std::size_t(fl.width) + std::size_t(wx);
    if (fl.data[idx] != FT_None) return false;  // roads win
    const std::uint8_t alpha = td.rgba[idx * 4u + 3];
    const float height01 = float(td.rgba[idx * 4u + 0]) / 255.0f;
    if (alpha == 0 || height01 < seaLevel) return false;   // water
    if (height01 >= kMountainBiomeLevel) return false;     // no rock terraces
    idxOut = idx;
    return true;
}

bool plough_cell_ok(const FeatureLayer& fl, const MacroWorld& world,
                    int x, int y, int& wheatOut, float seaLevel)
{
    std::size_t idx = 0;
    if (!parcel_ground_ok_(fl, world, x, y, seaLevel, idx)) return false;
    // The ONE fertility door: potential minus what play has taken.
    wheatOut = resource_field_read(world, ResourceFieldId::Wheat,
                                   FeatureLayer::wrap_coord(x, fl.width),
                                   FeatureLayer::wrap_coord(y, fl.height));
    return wheatOut >= field_wheat_min();
}

bool plough_field_cell(FeatureLayer& fl, const MacroWorld& world,
                       int x, int y, float seaLevel, FeatureType parcel)
{
    // ЧЕМ ЗАСЕЯНО — КОЛОНКА ФИЧИ, А НЕ ВТОРАЯ ДВЕРЬ (2026-09-20): земля,
    // проверка и цена у хлебной и льняной парцеллы одни и те же, поэтому
    // вспашка одна, а вид парцеллы — её аргумент.
    int wheat = 0;
    if (!plough_cell_ok(fl, world, x, y, wheat, seaLevel)) return false;
    const int wx = FeatureLayer::wrap_coord(x, fl.width);
    const int wy = FeatureLayer::wrap_coord(y, fl.height);
    fl.data[std::size_t(wy) * std::size_t(fl.width) + std::size_t(wx)] =
        std::uint8_t(parcel);
    return true;
}

bool pasture_cell_ok(const FeatureLayer& fl, const MacroWorld& world,
                     int x, int y, int& herdOut, float seaLevel)
{
    // The plough's own ground gates; the bar is the HERD row's — one head
    // must actually graze here, or the fence would enclose dust.
    std::size_t idx = 0;
    if (!parcel_ground_ok_(fl, world, x, y, seaLevel, idx)) return false;
    herdOut = resource_field_read(world, ResourceFieldId::Horses,
                                  FeatureLayer::wrap_coord(x, fl.width),
                                  FeatureLayer::wrap_coord(y, fl.height));
    return herdOut >= 1;
}

bool fence_pasture_cell(FeatureLayer& fl, const MacroWorld& world,
                        int x, int y, float seaLevel)
{
    int herd = 0;
    if (!pasture_cell_ok(fl, world, x, y, herd, seaLevel)) return false;
    const int wx = FeatureLayer::wrap_coord(x, fl.width);
    const int wy = FeatureLayer::wrap_coord(y, fl.height);
    fl.data[std::size_t(wy) * std::size_t(fl.width) + std::size_t(wx)] =
        FT_Pasture;
    return true;
}

const ResourceFieldDef& resource_field_def(ResourceFieldId f) {
    return kResourceFields[std::size_t(f)
                               < std::size_t(ResourceFieldId::Count)
                           ? std::size_t(f) : 0];
}

int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y) {
    const ResourceFieldDef& def = resource_field_def(f);
    if (def.carrierRead) return def.carrierRead(w, x, y);
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    const int scar = int(w.gs->resourceScarCells[std::size_t(f)].at(x, y));
    return std::max(0, def.baseline(w, x, y) - scar);
}

void resource_field_apply(MacroWorld& w, ResourceFieldId f, int x, int y,
                          int delta) {
    if (delta == 0) return;
    const ResourceFieldDef& carrier = resource_field_def(f);
    if (carrier.carrierApply) { carrier.carrierApply(w, x, y, delta); return; }
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return;
    ResourceGrid& scars = scars_of(*w.gs, f);
    // THE write sizes the field over THIS world if nobody did yet — the same
    // fail-open the worked layer's door keeps. A caller that has to remember
    // to allocate is a caller that will forget, and a lost scar is invisible:
    // the read simply answers "whole" (the fold-up lesson, applied to the one
    // door instead of to every fixture).
    if (!scars.live()) {
        scars.allocate(w.terrain->width, w.terrain->height, 0);
        if (!scars.live()) return;
    }
    // Spending (−delta) deepens the scar, returning (+delta, regrowth)
    // heals it; the scar never exceeds the baseline, so a runaway writer
    // cannot wind a cell into a millennium of regrowth.
    const int cap = std::max(0, resource_field_def(f).baseline(w, x, y));
    const int scar = std::clamp(int(scars.at(x, y)) - delta, 0, cap);
    // A healed cell is the field's own legal ZERO — no erase, no key, and
    // the live count the grid keeps is what the wire writes out.
    scars.write(x, y, std::int32_t(scar));
}

int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx) {
    const ResourceGrid& scars = gs.resourceScarCells[std::size_t(f)];
    if (!scars.live() || cellIdx >= scars.cells.size()) return 0;
    return int(scars.cells[cellIdx]);
}

void resource_fields_daily_growth(MacroWorld& w, int day) {
    if (!w.gs || day <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        const ResourceFieldDef& def = kResourceFields[f];
        switch (def.growthDomain) {
        case GrowthDomain::None:
            break;

        case GrowthDomain::CarrierGrid: {
            // The due 1/32 slice of the dense carrier (32768 cells of the
            // 1024² map): each cell is visited once per epoch and gets an
            // epoch's worth of growth — smooth long-term dynamics, no
            // full-map day.
            if (!w.trees || !w.trees->has_complete_storage()) break;
            const int W = w.trees->width;
            const std::size_t n = w.trees->cell_count();
            for (std::size_t idx = std::size_t(day % kGrowthEpochDays);
                 idx < n; idx += std::size_t(kGrowthEpochDays)) {
                const int x = int(idx % std::size_t(W));
                const int y = int(idx / std::size_t(W));
                const int born = def.growthAt(w, x, y);
                if (born > 0) {
                    resource_field_apply(w, ResourceFieldId(f), x, y, born);
                }
            }
            break;
        }

        case GrowthDomain::OwnScars: {
            // Only scarred cells can grow (capacity is the baseline), and the
            // row's own FIELD is walked — its live cells ARE the scarred
            // ones, which is the same work the hash did without pretending
            // the world was a bag of keys. The due indices are collected
            // first because the write below heals cells out from under the
            // walk (a grid's zero is a legal value, but the count moves).
            if (!w.terrain || w.terrain->width <= 0) break;
            const ResourceGrid& scars = w.gs->resourceScarCells[f];
            if (!scars.live()) break;
            std::vector<std::uint32_t> due;
            scars.for_each_live([&](std::uint32_t idx, std::int32_t) {
                if (growth_cell_due(idx, day)) due.push_back(idx);
            });
            for (const std::uint32_t idx : due) {
                const int x = int(idx % std::uint32_t(w.terrain->width));
                const int y = int(idx / std::uint32_t(w.terrain->width));
                const int born = def.growthAt(w, x, y);
                if (born > 0) {
                    resource_field_apply(w, ResourceFieldId(f), x, y, born);
                }
            }
            break;
        }

        case GrowthDomain::Geology: {
            // Lump birth on the HOST row's cells that lack this row — the
            // scarcer the world's stock, the likelier a strike (W2c):
            // chance/day = depletion × 1/8. Deterministic: the roll is a
            // pure hash of (worldSeed, day) — no RNG stream consumed, a
            // reload replays the same calendar.
            if (!w.deposits) break;
            const std::size_t ownKind =
                std::size_t(deposit_kind_of(ResourceFieldId(f)));
            const auto& own = w.deposits->cells[ownKind];
            // Scarcity is world level vs BORN level (owner, 2026-08-28):
            // virginUnits is derived from terrain + seed at layer build, so
            // annihilated veins need no memorial and a world born without
            // the metal never prospects for it.
            const std::int64_t virgin = w.deposits->virginUnits[ownKind];
            const int lump = def.growthAt(w, 0, 0);
            if (lump <= 0 || virgin <= 0) break;
            std::int64_t remaining = 0;
            own.for_each_live([&](std::uint32_t idx, std::int32_t rem) {
                (void)idx;
                remaining += rem;
            });
            // Discovery can push the live stock ABOVE the born level; a
            // richer-than-born world simply misses nothing (a negative
            // deficit cast to unsigned would prospect every day, forever).
            const std::int64_t deficit =
                remaining < virgin ? virgin - remaining : 0;
            // depletion ∈ [0,1] in 1/8 steps of the day roll below: the
            // comparison runs in integers — hash24 < depletion × 2^24 / 8.
            const std::uint64_t hash24 =
                hash3(std::uint32_t(day), 0x6E0Cu, w.gs->worldSeed) >> 8;
            const std::uint64_t bar =
                std::uint64_t(deficit * (1 << 24) / (virgin * 8));
            if (hash24 >= bar) break;
            // Candidates: host cells not yet holding this row. A FIELD walks
            // itself in index order, so the list comes out sorted by
            // construction — the explicit sort that used to stand here existed
            // only because a hash's iteration order is unspecified and the
            // pick must never depend on it.
            const ResourceGrid& host =
                w.deposits->grid(deposit_kind_of(def.growthHost));
            std::vector<std::uint32_t> candidates;
            candidates.reserve(std::size_t(host.liveCells));
            host.for_each_live([&](std::uint32_t idx, std::int32_t rem) {
                (void)rem;
                if (own.at_index(idx) == 0) candidates.push_back(idx);
            });
            if (candidates.empty()) break;
            const std::uint32_t pick =
                hash3(std::uint32_t(day), 0x51F7u, w.gs->worldSeed)
                % std::uint32_t(candidates.size());
            const std::uint32_t idx = candidates[pick];
            const int x = int(idx % std::uint32_t(w.deposits->width));
            const int y = int(idx / std::uint32_t(w.deposits->width));
            create_deposit(*w.deposits,
                           deposit_kind_of(ResourceFieldId(f)), x, y, lump);
            // New geology is a FACT of the world (FactKind::Discovered:
            // "found what the world did not know it had"), not a UI line:
            // the legends will be asked where the late-age iron came from,
            // and whoever stands nearby learns it through his journal. The
            // land itself struck it — no ordinal takes the credit — so the
            // subject is the PLACE, and amount names the row, +1, the same
            // deed-against-the-land shape the gatherer's Drained writes.
            {
                WorldFact df{};
                df.day = day;
                df.kind = std::uint16_t(FactKind::Discovered);
                df.subjectKind = std::uint8_t(FactSubject::Cell);
                df.x = std::int16_t(x);
                df.y = std::int16_t(y);
                df.amount = int(f) + 1;
                // THROUGH THE ONE DOOR (вердикт №9, 2026-09-17: прямые
                // chronicle_record «сводим в одну систему»): record_deed
                // files the fact AND settles what it was worth by the one
                // law — a bare chronicle_record here was a writer that filed
                // for free, and the two halves of S20.1 must never come
                // apart. The land takes the credit, so no subject ordinal is
                // handed over (a cell has no renown to earn, which the door
                // answers with 0 rather than with a branch).
                if (w.world) {
                    record_deed(*w.world, *w.gs, df);
                } else {
                    // No ECS wired (a field-only test world): the fact is
                    // still the world's memory — fail OPEN on the memory,
                    // never on the deed.
                    chronicle_record(w.gs->chronicle, df);
                }
            }
            break;
        }
        }
    }
}

namespace {

// ── The stock rows over the container ──────────────────────────────────────

int read_fauna_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Fauna, k.cellX, k.cellY);
}
void write_fauna_count(MacroWorld& w, MacroStockKey k, int delta) {
    resource_field_apply(w, ResourceFieldId::Fauna, k.cellX, k.cellY, delta);
}
int read_crop_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Wheat, k.cellX, k.cellY);
}
void write_crop_count(MacroWorld& w, MacroStockKey k, int delta) {
    resource_field_apply(w, ResourceFieldId::Wheat, k.cellX, k.cellY, delta);
}

constexpr MacroStockRow kRows[] = {
    {"tree_count",  &read_tree_count,  &write_tree_count},
    {"population",  &read_population,  &write_population},
    {"roster",      &read_roster,      &write_roster},
    {"fauna_count", &read_fauna_count, &write_fauna_count},
    {"crop_count",  &read_crop_count,  &write_crop_count},
    {"garrison",    &read_garrison,    &write_garrison},
};
static_assert(sizeof(kRows) / sizeof(kRows[0])
                  == std::size_t(MacroStock::Count),
              "every MacroStock value needs its row — the table IS the system");

const MacroStockRow& row_of(MacroStock s) {
    return kRows[std::size_t(s) < std::size_t(MacroStock::Count)
                     ? std::size_t(s) : 0];
}

} // namespace

int macro_stock_read(const MacroWorld& w, MacroStock s, MacroStockKey k) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return 0;
    return row_of(s).read(w, k);
}

void macro_stock_apply(MacroWorld& w, MacroStock s, MacroStockKey k, int delta) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return;
    row_of(s).write(w, k, delta);
}

const char* macro_stock_id(MacroStock s) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return "?";
    return row_of(s).id;
}


void settle_macro_debt(MacroWorld& w, const ecs::MacroDebt& d, int sign) {
    if (d.stock >= std::uint8_t(MacroStock::Count) || d.amount == 0) return;
    macro_stock_apply(w, MacroStock(d.stock),
                      MacroStockKey{d.subject, d.cellX, d.cellY, d.detail,
                                    d.detailKind, d.detailLevel},
                      sign * int(d.amount));
}

} // namespace sm
