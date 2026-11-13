#include "macro/economy.h"
#include "bus/anketa.h"
#include "macro/econ_day.h"

namespace sm {

// ── МОСТ «НУЖДА → ЯДРО» ──────────────────────────────────────────────────
// Нужда строки — остаток счёта сквада по ней, и больше ничего (закон и шрам —
// у объявления). Строка вне словаря товаров счёта не имеет.
RowSide row_side(int itemIdx, std::int64_t base, const std::int32_t* needDebt,
                 const Inventory& inv, std::int64_t unitMassG,
                 std::int64_t backG) {
    RowSide r{};
    r.base = base;
    r.unitMassG = unitMassG;
    r.backG = backG;
    if (itemIdx < 0) return r;
    const int commodity = commodity_of_item(itemIdx);
    const std::int64_t debt =
        (needDebt && commodity >= 0 && needDebt[commodity] > 0)
            ? std::int64_t(needDebt[commodity]) : 0;
    r.net = std::int64_t(inv.count_of(itemIdx)) - debt;
    return r;
}

} // namespace sm
