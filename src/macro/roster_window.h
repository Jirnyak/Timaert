// СУД ГРАНИЦЫ СЕЗОНА — ОДНА ДВЕРЬ НА ВСЯКИЙ РОСТЕР МИРА.
//
// CANON S4 («гарнизон = ростер ландмарка, армия = ростер, артель = ростер;
// одна система, одна арифметика пищи, ОДИН СУД ГРАНИЦЫ») и S10 («у всякого,
// кто кормит, есть счёт»). До 2026-09-21 этот суд был написан ДВАЖДЫ —
// `garrison_upkeep_` для мест и `squad_season_window` для сквадов, — и обе
// копии делали одно и то же в три шага: взыскать прошлый счёт пропорционально,
// перевыставить по составу после убыли, погасить чем есть.
//
// СЧЁТ ТОЖЕ ЖИВЁТ ЗДЕСЬ — И ЭТО ПРАВКА 2026-09-22, ОТМЕНЯЮЩАЯ ПРЕЖНЮЮ ЗАПИСЬ.
// Здесь стояло: «дверь не считает СЧЁТ — его приносят; цена содержания
// зависит от контекста (дома ли ростер, чей фуражир его ведёт), и это
// законные различия КОНТЕКСТА». Вердикт владельца 2026-09-22 снял оба
// различия («один закон без исключений»), и довод вместе с ними: полцены
// дома было хардкодом `>> 1` без вывода (почему половина, а не треть?), а
// скидка фуражира — вторым ответом на «сколько ест ростер». Контекста не
// осталось — значит счёт не параметр, а ЗАКОН, и место закона здесь.
//
// ЧТО ЭТИМ СНЕСЕНО, ПОИМЁННО: шаблонный параметр `BillFn`, две лямбды в двух
// вызовах, `squad_season_needs`/`SquadSeasonNeeds` (npc_ai) и расхождение
// «кто считается ртом» — у места ели ВСЕ головы, у сквада только строки с
// `upkeepGoldPerDay >= 0`, то есть зверьё в поле не ело вовсе. Теперь рот —
// это строка существа и её колонка рациона (npc.h `npc_board_per_day`).
#pragma once

#include "macro/currency.h"   // pay_value_dense / inventory_value — плата стоимостью
#include "macro/econ_day.h"   // econ_pay_debt, hunger_commodity_ordinal, EconFactSink
#include "macro/npc.h"        // npc_board_per_day / soldier_upkeep — счёт по строкам
#include "macro/roster.h"
#include "macro/world_row.h"  // область существ единого контейнера (M-71)

#include <algorithm>

namespace sm {

// СЧЁТ, ВЫСТАВЛЕННЫЙ РОСТЕРУ НА СЕЗОН. Две строки, и у каждой свой исход
// (CANON S4 «ХАРЧ — СМЕРТЬ, ПЛАТА — УХОД»).
struct RosterBill {
    int          board = 0;   // харч: единиц голодной строки на сезон
    std::int64_t wage  = 0;   // жалованье: стоимостью
};

// ОДИН ПРОХОД НА ОБЕ СТРОКИ СЧЁТА, И ОБЕ — ПО ТАБЛИЦЕ СУЩЕСТВ.
// Считает по СЛОТАМ области существ ЕДИНОГО контейнера (M-71), а не по
// душам: тысячная деревня — это два-три слота (генерики стоят стопкой),
// поэтому цена двери не зависит от размера ростера.
inline RosterBill roster_bill(const Inventory& container) {
    int boardDay = 0;
    int wageDay  = 0;
    for (int i = container.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = container.slots[std::size_t(i)];
        const std::uint16_t kind =
            std::uint16_t(creature_of_world_row(s.def));
        // Обе колонки спрашиваются у ОДНОЙ строки, своими дверьми:
        // `npc_board_per_day` — рацион, `soldier_upkeep` — плата (она же
        // читает `kNpcUpkeepNone` как ноль, поэтому зверь ест, но не
        // получает, и это сказано данными, а не веткой).
        boardDay += npc_board_per_day(soldier_npc_type(kind)) * s.count;
        wageDay  += soldier_upkeep(kind, s.level) * s.count;
    }
    return RosterBill{boardDay * kDaysPerSeason,
                      std::int64_t(wageDay) * kDaysPerSeason};
}

struct RosterWindowOutcome {
    int  walked = 0;      // душ снято с ростера за неоплату
    bool byWage = false;  // КАКАЯ строка победила: плата (true) или харч
};

// Один суд, три шага. Счёт берётся ДВАЖДЫ одной и той же дверью: до убыли —
// как знаменатель доли неоплаченного, после — как новая недоимка.
// `store` — ЕДИНЫЙ контейнер владельца (M-71): его область существ и есть
// ростер, его предметная область и есть склад; суд читает обе.
inline RosterWindowOutcome roster_season_window(Roster& r, Inventory& store,
                                                Inventory& pool,
                                                std::int64_t& burnedValue,
                                                EconFactSink sink,
                                                void* user) {
    RosterWindowOutcome out{};
    const int boardOrd = hunger_commodity_ordinal();
    if (boardOrd < 0) return out;   // мир без голодной строки не судит никого
    const RosterBill lastBill = roster_bill(store);

    // ── 1. ВЗЫСКАНИЕ ПРОШЛОГО СЧЁТА — ПРОПОРЦИОНАЛЬНО ────────────────────
    // Доля НЕОПЛАЧЕННОГО и есть доля ушедших: зеркало закона мест
    // (econ_debt_boundary). Кромка «покрыто ЦЕЛИКОМ или не списывается»,
    // доля 1/8 и пол «хотя бы одна душа» умерли 2026-09-21 — все три были
    // одним дефектом: ростер из трёх душ терял треть вместо восьмой, а
    // покрывший 99 % нужды терял столько же, сколько не покрывший ничего.
    const int souls = creature_heads(store);
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
    while (walkers-- > 0 && !creatures_empty(store)) {
        SoldierRecord walker{};
        if (!creatures_pop_back(store, walker)) break;
        if (!creatures_push(pool, walker)) {
            creatures_push(store, walker);   // пул полон: человек остаётся
            break;
        }
        ++out.walked;
    }

    // ── 2. НОВЫЙ СЧЁТ по составу ПОСЛЕ убыли, ПЕРЕЗАПИСЬЮ ────────────────
    // Старая недоимка не переносится: взыскали — выставили новый. Поэтому
    // хранить исходную сумму не нужно, она пересчитывается из состава.
    const RosterBill next = roster_bill(store);
    r.needDebt[boardOrd] = std::int32_t(next.board);
    r.wageDebt = next.wage;
    if (creatures_empty(store)) {
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
