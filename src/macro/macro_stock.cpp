// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"

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

constexpr MacroStockRow kRows[] = {
    {"tree_count", &read_tree_count, &write_tree_count},
    {"population", &read_population, &write_population},
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
                      MacroStockKey{d.subject, d.cellX, d.cellY},
                      sign * int(d.amount));
}

} // namespace sm
