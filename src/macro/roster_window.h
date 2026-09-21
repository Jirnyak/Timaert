// СУД ГРАНИЦЫ СЕЗОНА — ОДНА ДВЕРЬ НА ВСЯКИЙ РОСТЕР МИРА.
//
// CANON S4 («гарнизон = ростер ландмарка, армия = ростер, артель = ростер;
// одна система, одна арифметика пищи, ОДИН СУД ГРАНИЦЫ») и S10 («у всякого,
// кто кормит, есть счёт»). До 2026-09-21 этот суд был написан ДВАЖДЫ —
// `garrison_upkeep_` для мест и `squad_season_window` для сквадов, — и обе
// копии делали одно и то же в три шага: взыскать прошлый счёт пропорционально,
// перевыставить по составу после убыли, погасить чем есть.
//
// ЧТО ОСТАЁТСЯ ЗА ВЫЗЫВАЮЩИМ И ПОЧЕМУ. Дверь не считает СЧЁТ — его приносят.
// Цена содержания зависит от контекста (дома ли ростер, чей фуражир его ведёт,
// какие строки существ в нём стоят), и это законные различия КОНТЕКСТА, а не
// формы контейнера. Спрятав их внутрь, мы получили бы ветку по роду владельца —
// ровно ту стену по виду, которую S16 запрещает. Поэтому счёт — параметр, и
// два сегодняшних расхождения (полцены дома у гарнизона, скидка фуражира у
// артели) стоят на виду в двух вызовах, а не прячутся в общем теле.
#pragma once

#include "macro/currency.h"   // pay_value_dense / inventory_value — плата стоимостью
#include "macro/econ_day.h"   // econ_pay_debt, hunger_commodity_ordinal, EconFactSink
#include "macro/roster.h"

#include <algorithm>

namespace sm {

// СЧЁТ, ВЫСТАВЛЕННЫЙ РОСТЕРУ НА СЕЗОН. Две строки, и у каждой свой исход
// (CANON S4 «ХАРЧ — СМЕРТЬ, ПЛАТА — УХОД»).
struct RosterBill {
    int          board = 0;   // харч: единиц голодной строки на сезон
    std::int64_t wage  = 0;   // жалованье: стоимостью
};

struct RosterWindowOutcome {
    int  walked = 0;      // душ снято с ростера за неоплату
    bool byWage = false;  // КАКАЯ строка победила: плата (true) или харч
};

// Один суд, три шага. `bill` пересчитывает счёт по СЕГОДНЯШНЕМУ составу —
// шаблоном, а не указателем на функцию: у артели он спрашивает лист ведущего,
// у места — свою половинную ставку, и ни одна из этих зависимостей не имеет
// права протечь в этот заголовок.
template <class BillFn>
RosterWindowOutcome roster_season_window(Roster& r, Inventory& store,
                                         const RosterBill& lastBill,
                                         BillFn&& bill,
                                         SoldierSquad& pool,
                                         std::int64_t& burnedValue,
                                         EconFactSink sink, void* user) {
    RosterWindowOutcome out{};
    const int boardOrd = hunger_commodity_ordinal();
    if (boardOrd < 0) return out;   // мир без голодной строки не судит никого

    // ── 1. ВЗЫСКАНИЕ ПРОШЛОГО СЧЁТА — ПРОПОРЦИОНАЛЬНО ────────────────────
    // Доля НЕОПЛАЧЕННОГО и есть доля ушедших: зеркало закона мест
    // (econ_debt_boundary). Кромка «покрыто ЦЕЛИКОМ или не списывается»,
    // доля 1/8 и пол «хотя бы одна душа» умерли 2026-09-21 — все три были
    // одним дефектом: ростер из трёх душ терял треть вместо восьмой, а
    // покрывший 99 % нужды терял столько же, сколько не покрывший ничего.
    const int souls = r.squad.size();
    float unpaid = 0.0f;
    if (lastBill.board > 0 && r.needDebt[boardOrd] > 0) {
        unpaid = float(r.needDebt[boardOrd]) / float(lastBill.board);
    }
    if (lastBill.wage > 0 && r.wageDebt > 0) {
        // Берётся ХУДШАЯ из двух долей, а не сумма: один и тот же человек
        // может быть и не кормлен, и не плачен. Строка-победительница
        // называется вслух — без неё вечерний уровень говорит «сто душ ушло»
        // и молчит о том, кормить их надо было или платить.
        const float wageShare = float(r.wageDebt) / float(lastBill.wage);
        if (wageShare > unpaid) { unpaid = wageShare; out.byWage = true; }
    }
    if (unpaid > 1.0f) unpaid = 1.0f;
    int walkers = int(float(souls) * unpaid);
    while (walkers-- > 0 && !r.squad.empty()) {
        SoldierRecord walker{};
        if (!r.squad.pop_soul_back(walker)) break;
        if (!pool.push(walker)) {
            r.squad.push(walker);   // пул полон: человек остаётся
            break;
        }
        ++out.walked;
    }

    // ── 2. НОВЫЙ СЧЁТ по составу ПОСЛЕ убыли, ПЕРЕЗАПИСЬЮ ────────────────
    // Старая недоимка не переносится: взыскали — выставили новый. Поэтому
    // хранить исходную сумму не нужно, она пересчитывается из состава.
    const RosterBill next = bill(r.squad);
    r.needDebt[boardOrd] = std::int32_t(next.board);
    r.wageDebt = next.wage;
    if (r.squad.empty()) {
        r.needDebt[boardOrd] = 0;
        r.wageDebt = 0;
        return out;
    }

    // ── 3. НЕМЕДЛЕННОЕ ГАШЕНИЕ, И ОНО ЧАСТИЧНОЕ ──────────────────────────
    // Харч — той же дверью, что у населения этого же склада: что лежит, то
    // и съедено сейчас, остальное остаётся долгом и гасится приходом весь
    // сезон. Плата — стоимостью, монеты первыми по плотности value/kg;
    // уплаченное СГОРАЕТ в пул лута («жалование сгорает естественно»).
    if (r.needDebt[boardOrd] > 0) {
        econ_pay_debt(store, r.needDebt, sink, user);
    }
    if (r.wageDebt > 0) {
        const std::int64_t canPay =
            std::min<std::int64_t>(r.wageDebt, inventory_value(store));
        if (canPay > 0) {
            burnedValue += pay_value_dense(store, int(canPay));
            r.wageDebt -= canPay;
        }
    }
    return out;
}

} // namespace sm
