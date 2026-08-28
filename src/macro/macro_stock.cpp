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
    for (auto& v : w.gs->villages) if (v.id == subject) return &v.population;
    for (auto& s : w.gs->settlements) if (s.id == subject) return &s.population;
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
    if (!r || k.detail == -1) return;   // a nameless death removes "one of
                                        // them" — refuse; the receipt names
                                        // its member or it pays nothing
    for (int i = 0; i < -delta; ++i) {
        if (!remove_one_soldier_by_entity_id(r->squad,
                                             std::uint32_t(k.detail))) {
            break;
        }
    }
}

// ── The ONE resource-field container (macro/resource_field.h) ─────────────
// Baseline = pure terrain/climate (resources come BEFORE settlement — the
// owner's causality); storage follows the row's law (resource_field.h):
// sparse scar rows (fauna, wheat) and carrier rows (trees — the dense grid
// the map renders). Deposits join as carrier rows in Inc B.

std::uint32_t field_cell_index(const MacroWorld& w, int x, int y) {
    const int wx = FeatureLayer::wrap_coord(x, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(y, w.terrain->height);
    return std::uint32_t(wy) * std::uint32_t(w.terrain->width)
         + std::uint32_t(wx);
}

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
                  && int(ResourceFieldId::Stone) == int(ResourceFieldId::Clay) + 2,
              "deposit rows must mirror DepositKind order");

// Clay/Iron/Stone: carrier rows over the deposit layer — one sparse map per
// kind, PRESENCE is the deposit (a dry vein reads 0 through a live cell; a
// cell with no vein reads 0 through absence and REFUSES writes — mining
// invents no geology; genesis goes through create_deposit, deliberately).
template <DepositKind K>
int deposit_read(const MacroWorld& w, int x, int y) {
    if (!w.deposits) return 0;
    const std::int32_t* r = w.deposits->remaining_at(K, x, y);
    return r ? int(*r) : 0;
}
template <DepositKind K>
void deposit_apply(MacroWorld& w, int x, int y, int delta) {
    if (!w.deposits || delta == 0) return;
    const std::int32_t* r = w.deposits->remaining_at(K, x, y);
    if (!r) return;   // fail closed: no vein here to move
    set_deposit_remaining(*w.deposits, K, x, y, *r + delta);
}

// ── The growth laws — the per-row CONTEXT of the one birth mechanism ──────

// Wheat: fertility is the whole context (the baseline already prices it),
// so a reaped cell replants one stand per visit — one per kGrowthEpochDays,
// the exact rate of the old 32-day healing period.
int wheat_growth_at(const MacroWorld&, int, int) { return 1; }

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

// Iron: born where it is SCARCE (the owner's negative context) — the lump a
// fresh vein opens with; the walker's Geology domain owns the global
// scarcity roll and the host-cell pick (the W2c rule as a table row).
int iron_growth_at(const MacroWorld&, int, int) { return iron_vein_lump(); }

constexpr ResourceFieldDef kResourceFields[] = {
    /* Wheat */ {"wheat", &wheat_baseline,
                 GrowthDomain::OwnScars, &wheat_growth_at,
                 ResourceFieldId::Wheat, nullptr, nullptr},
    /* Fauna */ {"fauna", &fauna_baseline,
                 GrowthDomain::OwnScars, &fauna_growth_at,
                 ResourceFieldId::Fauna, nullptr, nullptr},
    /* Trees */ {"trees", nullptr,
                 GrowthDomain::CarrierGrid, &trees_growth_at,
                 ResourceFieldId::Trees, &trees_read, &trees_apply},
    /* Clay  */ {"clay",  nullptr,
                 GrowthDomain::None, nullptr, ResourceFieldId::Clay,
                 &deposit_read<DepositKind::Clay>,
                 &deposit_apply<DepositKind::Clay>},
    /* Iron  */ {"iron",  nullptr,
                 GrowthDomain::Geology, &iron_growth_at,
                 ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Iron>,
                 &deposit_apply<DepositKind::Iron>},
    /* Stone */ {"stone", nullptr,
                 GrowthDomain::None, nullptr, ResourceFieldId::Stone,
                 &deposit_read<DepositKind::Stone>,
                 &deposit_apply<DepositKind::Stone>},
};
static_assert(sizeof(kResourceFields) / sizeof(kResourceFields[0])
                  == std::size_t(ResourceFieldId::Count),
              "every ResourceFieldId needs its def — the table IS the system");

std::unordered_map<std::uint32_t, std::uint16_t>&
scars_of(GameState& gs, ResourceFieldId f) {
    return gs.resourceScars[std::size_t(f)];
}

} // namespace

const ResourceFieldDef& resource_field_def(ResourceFieldId f) {
    return kResourceFields[std::size_t(f)
                               < std::size_t(ResourceFieldId::Count)
                           ? std::size_t(f) : 0];
}

int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y) {
    const ResourceFieldDef& def = resource_field_def(f);
    if (def.carrierRead) return def.carrierRead(w, x, y);
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    const auto& scars = w.gs->resourceScars[std::size_t(f)];
    int scar = 0;
    const auto it = scars.find(field_cell_index(w, x, y));
    if (it != scars.end()) scar = int(it->second);
    return std::max(0, def.baseline(w, x, y) - scar);
}

void resource_field_apply(MacroWorld& w, ResourceFieldId f, int x, int y,
                          int delta) {
    if (delta == 0) return;
    const ResourceFieldDef& carrier = resource_field_def(f);
    if (carrier.carrierApply) { carrier.carrierApply(w, x, y, delta); return; }
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return;
    auto& scars = scars_of(*w.gs, f);
    const std::uint32_t idx = field_cell_index(w, x, y);
    int scar = 0;
    const auto it = scars.find(idx);
    if (it != scars.end()) scar = int(it->second);
    // Spending (−delta) deepens the scar, returning (+delta, regrowth)
    // heals it; the scar never exceeds the baseline, so a runaway writer
    // cannot wind a cell into a millennium of regrowth.
    const int cap = std::max(0, resource_field_def(f).baseline(w, x, y));
    scar = std::clamp(scar - delta, 0, cap);
    // A healed cell needs no override — the map self-cleans, so the
    // persisted set stays "cells play has scarred", never the world.
    if (scar <= 0) scars.erase(idx);
    else           scars[idx] = std::uint16_t(scar);
}

int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx) {
    const auto& scars = gs.resourceScars[std::size_t(f)];
    const auto it = scars.find(cellIdx);
    return it != scars.end() ? int(it->second) : 0;
}

void resource_fields_daily_growth(MacroWorld& w, int day) {
    if (!w.gs || day <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        const ResourceFieldDef& def = kResourceFields[f];
        switch (def.growthDomain) {
        case GrowthDomain::None:
            break;

        case GrowthDomain::CarrierGrid: {
            // The due 1/32 slice of the dense carrier (8192 cells of a
            // 512² map): each cell is visited once per epoch and gets an
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
            // Only scarred cells can grow (capacity is the baseline).
            // Collect the due keys first: the write self-cleans a healed
            // cell OUT of the map we are walking.
            if (!w.terrain || w.terrain->width <= 0) break;
            std::vector<std::uint32_t> due;
            for (const auto& [idx, scar] : w.gs->resourceScars[f]) {
                (void)scar;
                if (growth_cell_due(idx, day)) due.push_back(idx);
            }
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
            for (const auto& [idx, rem] : own) { (void)idx; remaining += rem; }
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
            // Candidates: host cells not yet holding this row, in SORTED
            // order (the map's iteration order is unspecified — the pick
            // must not depend on it).
            const auto& host =
                w.deposits->cells[std::size_t(deposit_kind_of(
                    def.growthHost))];
            std::vector<std::uint32_t> candidates;
            candidates.reserve(host.size());
            for (const auto& [idx, rem] : host) {
                (void)rem;
                if (!own.count(idx)) candidates.push_back(idx);
            }
            if (candidates.empty()) break;
            std::sort(candidates.begin(), candidates.end());
            const std::uint32_t pick =
                hash3(std::uint32_t(day), 0x51F7u, w.gs->worldSeed)
                % std::uint32_t(candidates.size());
            const std::uint32_t idx = candidates[pick];
            const int x = int(idx % std::uint32_t(w.deposits->width));
            const int y = int(idx / std::uint32_t(w.deposits->width));
            create_deposit(*w.deposits,
                           deposit_kind_of(ResourceFieldId(f)), x, y, lump);
            // New geology is world news (W2c kept its voice).
            char line[64];
            std::snprintf(line, sizeof(line),
                          "Prospectors struck a new %s vein.", def.id);
            push_event_log(w.gs->player, {LogType::World, line, day});
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
                      MacroStockKey{d.subject, d.cellX, d.cellY, d.detail},
                      sign * int(d.amount));
}

} // namespace sm
