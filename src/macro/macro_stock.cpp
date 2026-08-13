// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"

#include "ecs/world.h"
#include "macro/army.h"
#include "macro/fauna.h"
#include "macro/map_generator.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <vector>

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
// with people in it — and their ids share one space, so one lookup serves both.
int* find_population(const MacroWorld& w, std::int32_t subject) {
    if (!w.gs || subject < 0) return nullptr;
    for (auto& s : w.gs->settlements) if (s.id == subject) return &s.population;
    for (auto& v : w.gs->villages)    if (v.id == subject) return &v.population;
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
    return r ? int(r->members.size()) : 0;
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
        if (!remove_one_soldier_by_entity_id(r->members,
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
    return fauna_cell_capacity_at(w.gs, w.terrain, w.trees, x, y);
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

constexpr ResourceFieldDef kResourceFields[] = {
    /* Wheat */ {"wheat", &wheat_baseline, &crop_regrow_period_days,
                 nullptr, nullptr},
    /* Fauna */ {"fauna", &fauna_baseline, &fauna_regrow_period_days,
                 nullptr, nullptr},
    /* Trees */ {"trees", nullptr, nullptr, &trees_read, &trees_apply},
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

void resource_fields_daily_regrow(MacroWorld& w, int day) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0 || day <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        const ResourceFieldDef& def = kResourceFields[f];
        if (!def.regrowPeriodDays) continue;   // carrier rows do not heal
        const int period = def.regrowPeriodDays();
        if (period <= 0 || day % period != 0) continue;
        // Collect the keys first: the +1 goes through the field's write,
        // which self-cleans a healed cell OUT of the map we are walking.
        std::vector<std::uint32_t> scarred;
        scarred.reserve(w.gs->resourceScars[f].size());
        for (const auto& [idx, scar] : w.gs->resourceScars[f]) {
            (void)scar;
            scarred.push_back(idx);
        }
        for (const std::uint32_t idx : scarred) {
            const int x = int(idx % std::uint32_t(w.terrain->width));
            const int y = int(idx / std::uint32_t(w.terrain->width));
            resource_field_apply(w, ResourceFieldId(f), x, y, +1);
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

int crop_regrow_period_days() { return 32; }   // po2 game days per stand

void settle_macro_debt(MacroWorld& w, const ecs::MacroDebt& d, int sign) {
    if (d.stock >= std::uint8_t(MacroStock::Count) || d.amount == 0) return;
    macro_stock_apply(w, MacroStock(d.stock),
                      MacroStockKey{d.subject, d.cellX, d.cellY, d.detail},
                      sign * int(d.amount));
}

} // namespace sm
