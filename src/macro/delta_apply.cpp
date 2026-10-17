#include "macro/delta_apply.h"

#include "core/torus.h"
#include "macro/chronicle.h"
#include "macro/macro_stock.h"
#include "macro/macro_world.h"
#include "macro/squad.h"
#include "macro/state.h"

#include <cstdio>

namespace sm {

void apply_local_delta(MacroWorld& w, DeltaBuffer& d) {
    if (!w.gs) return;
    const int side = w.gs->mapW;

    // ── РОД 3: операции над счётчиками — ведомостью ────────────────────────
    // Строка несёт субъект в законе нуля-ординала (0 = «никто», клеточный
    // ряд); словарь ключа ведомости говорит «клетка = −1» — перевод живёт
    // здесь, у единственного места, где оба диалекта видны.
    const std::uint32_t nStock = d.stockCount < kDeltaStockCap
        ? d.stockCount : kDeltaStockCap;
    for (std::uint32_t i = 0; i < nStock; ++i) {
        const StockOp& op = d.stockOps[i];
        if (op.amount == 0) continue;
        const MacroStockKey k{op.subject > 0 ? op.subject : -1,
                              std::int16_t(cell_x(op.cell, side)),
                              std::int16_t(cell_y(op.cell, side)),
                              op.detail, op.detailKind, op.detailLevel};
        macro_stock_apply(w, MacroStock(op.stock), k, op.amount);
    }

    // ── РОД 5: факты — той же дверью деяния (подшивка + слава) ─────────────
    // Протухший АВТОР факта (сквад умер между записью и границей) не рвёт
    // применение: record_deed подшивает с субъектом, какой назван, — летопись
    // и так пишет неизвестных нулём (закон нуля-ординала).
    const std::uint32_t nFact = d.factCount < kDeltaFactCap
        ? d.factCount : kDeltaFactCap;
    for (std::uint32_t i = 0; i < nFact; ++i) {
        if (w.store) {
            record_deed(*w.store, *w.gs, d.factOps[i], MacroHandle{});
        } else {
            chronicle_record(w.gs->chronicle, d.factOps[i]);
        }
    }

    // Потерянная строка — громко: тихий дроп операции есть мир, соврвший
    // игроку о том, что он сделал (ЗАКОН ДВУХ ТЕМПОВ п.3).
    if (d.droppedOps > 0) {
        std::fprintf(stderr,
                     "[delta] ДРОП %u операций: кап буфера (%u/%u строк) "
                     "мал для одного тика — поднять кап, не глотать\n",
                     d.droppedOps, kDeltaStockCap, kDeltaFactCap);
    }
    d.stockCount = 0;
    d.factCount = 0;
    d.droppedOps = 0;
}

} // namespace sm
