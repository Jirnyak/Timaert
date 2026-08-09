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

// ── tree_count: the forest of one cell ─────────────────────────────────────
int read_tree_count(const MacroWorld& w, MacroStockKey k) {
    if (!w.trees) return 0;
    return int(w.trees->at(k.cellX, k.cellY));
}

void write_tree_count(MacroWorld& w, MacroStockKey k, int delta) {
    if (!w.trees || !w.gs || delta == 0) return;
    const int now  = int(w.trees->at(k.cellX, k.cellY));
    const int next = std::clamp(now + delta, 0, kMaxTreesPerCell);
    set_tree_count(*w.trees, w.gs->treeOverrides, k.cellX, k.cellY, next);
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

// ── fauna_count: the wild headcount of one cell ────────────────────────────
// The same derive-don't-store shape as trees, minus the full grid: nothing
// renders this count, so the sparse overrides ARE the storage. Baseline =
// the cell's own spawn-table capacity (macro/fauna.h), override = the cell
// as play left it. Return to a hunted cell and the count is what you made
// it — the infinite XP/loot farm of repopulate-on-recenter dies here.
std::uint32_t fauna_cell_index(const MacroWorld& w, MacroStockKey k) {
    const int wx = FeatureLayer::wrap_coord(k.cellX, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(k.cellY, w.terrain->height);
    return std::uint32_t(wy) * std::uint32_t(w.terrain->width)
         + std::uint32_t(wx);
}

int read_fauna_count(const MacroWorld& w, MacroStockKey k) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    const auto it = w.gs->faunaOverrides.find(fauna_cell_index(w, k));
    if (it != w.gs->faunaOverrides.end()) return int(it->second);
    return fauna_cell_capacity_at(w.gs, w.terrain, w.trees, k.cellX, k.cellY);
}

void write_fauna_count(MacroWorld& w, MacroStockKey k, int delta) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0 || delta == 0) return;
    const int cap = fauna_cell_capacity_at(w.gs, w.terrain, w.trees,
                                           k.cellX, k.cellY);
    const int next = std::clamp(read_fauna_count(w, k) + delta, 0, cap);
    const std::uint32_t idx = fauna_cell_index(w, k);
    // A cell back at its baseline needs no override — the map self-cleans,
    // so the persisted set stays "cells play has scarred", never the world.
    if (next >= cap) w.gs->faunaOverrides.erase(idx);
    else             w.gs->faunaOverrides[idx] = std::uint16_t(next);
}

// ── crop_count: the standing wheat of one cell ─────────────────────────────
// Same wrap, DIFFERENT storage direction than fauna: the override is the
// HARVEST SCAR (stands taken and not yet regrown), not the remaining count.
// Only generation knows a cell's true yield — a settlement's garden plots
// against a full FT_Field cell — so the macro side records the wound and the
// scatter (sub/gens/dispatch.cpp scatter_field_crops) plants its natural
// yield minus it. `read` answers the macro consumers (the farmer's "is there
// grain left") with a fertility-derived ESTIMATE minus the scar — the owner's
// law: crop capacity is a function of the cell's own fertility (the moisture
// channel, the same one the fields were stamped by), settlements included,
// no special case.
constexpr int kMaxCropStandsPerCell = 4096;   // po2 scale of the estimate

std::uint32_t crop_cell_index(const MacroWorld& w, MacroStockKey k) {
    const int wx = FeatureLayer::wrap_coord(k.cellX, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(k.cellY, w.terrain->height);
    return std::uint32_t(wy) * std::uint32_t(w.terrain->width)
         + std::uint32_t(wx);
}

int crop_cell_capacity_at(const MacroWorld& w, MacroStockKey k) {
    const int wx = FeatureLayer::wrap_coord(k.cellX, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(k.cellY, w.terrain->height);
    const std::size_t idx =
        (std::size_t(wy) * std::size_t(w.terrain->width) + std::size_t(wx))
        * 4u + 1u;   // G = moisture, "the fertility" (macro/spawners.h)
    if (idx >= w.terrain->rgba.size()) return 0;
    return int(w.terrain->rgba[idx]) * kMaxCropStandsPerCell / 255;
}

int read_crop_count(const MacroWorld& w, MacroStockKey k) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    int scar = 0;
    const auto it = w.gs->cropOverrides.find(crop_cell_index(w, k));
    if (it != w.gs->cropOverrides.end()) scar = int(it->second);
    return std::max(0, crop_cell_capacity_at(w, k) - scar);
}

void write_crop_count(MacroWorld& w, MacroStockKey k, int delta) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0 || delta == 0) return;
    const std::uint32_t idx = crop_cell_index(w, k);
    int scar = 0;
    const auto it = w.gs->cropOverrides.find(idx);
    if (it != w.gs->cropOverrides.end()) scar = int(it->second);
    // Spending stock (−delta) deepens the scar; returning (+delta, regrowth)
    // heals it. The scar is capped at the estimate scale so a runaway writer
    // cannot wind a cell into a millennium of regrowth.
    scar = std::clamp(scar - delta, 0, kMaxCropStandsPerCell);
    // A healed cell needs no override — the map self-cleans, so the
    // persisted set stays "cells the sickle has scarred", never the world.
    if (scar <= 0) w.gs->cropOverrides.erase(idx);
    else           w.gs->cropOverrides[idx] = std::uint16_t(scar);
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

void fauna_daily_regrow(MacroWorld& w) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return;
    // Collect the keys first: the +1 goes through the stock row, whose write
    // self-cleans a healed cell OUT of the map we are walking.
    std::vector<std::uint32_t> scarred;
    scarred.reserve(w.gs->faunaOverrides.size());
    for (const auto& [idx, count] : w.gs->faunaOverrides) {
        (void)count;
        scarred.push_back(idx);
    }
    for (const std::uint32_t idx : scarred) {
        const int x = int(idx % std::uint32_t(w.terrain->width));
        const int y = int(idx / std::uint32_t(w.terrain->width));
        macro_stock_apply(w, MacroStock::FaunaCount,
                          MacroStockKey{-1, std::int16_t(x), std::int16_t(y)},
                          +1);
    }
}

int crop_regrow_period_days() { return 32; }   // po2 game days per stand

void crop_daily_regrow(MacroWorld& w) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return;
    // Collect the keys first: the +1 goes through the stock row, whose write
    // self-cleans a healed cell OUT of the map we are walking.
    std::vector<std::uint32_t> scarred;
    scarred.reserve(w.gs->cropOverrides.size());
    for (const auto& [idx, scar] : w.gs->cropOverrides) {
        (void)scar;
        scarred.push_back(idx);
    }
    for (const std::uint32_t idx : scarred) {
        const int x = int(idx % std::uint32_t(w.terrain->width));
        const int y = int(idx / std::uint32_t(w.terrain->width));
        macro_stock_apply(w, MacroStock::CropCount,
                          MacroStockKey{-1, std::int16_t(x), std::int16_t(y)},
                          +1);
    }
}

void settle_macro_debt(MacroWorld& w, const ecs::MacroDebt& d, int sign) {
    if (d.stock >= std::uint8_t(MacroStock::Count) || d.amount == 0) return;
    macro_stock_apply(w, MacroStock(d.stock),
                      MacroStockKey{d.subject, d.cellX, d.cellY, d.detail},
                      sign * int(d.amount));
}

} // namespace sm
