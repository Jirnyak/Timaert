// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"

#include "ecs/world.h"
#include "macro/army.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <algorithm>

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

constexpr MacroStockRow kRows[] = {
    {"tree_count", &read_tree_count, &write_tree_count},
    {"population", &read_population, &write_population},
    {"roster",     &read_roster,     &write_roster},
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
