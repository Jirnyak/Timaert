// Исполнение одного глагола обмена — закон и провенанс в deal.h.
#include "macro/deal.h"

#include <algorithm>

#include "core/caps.h"        // kUnifiedCap — границы слотов сторон
#include "core/subtract.h"    // sub_sat — остатки намерения и спины («ниже
                              // нуля не бывает» — закон этих величин)
#include "bus/anketa.h"     // Inventory — контейнер обеих сторон
#include "macro/currency.h"   // densest_value_slot — стейджинг встречного лота
#include "bus/store.h"      // MacroStore — адресация сторон слотами
#include "bus/squad_sheet.h"  // effective_sheet_of — сила стороны с ЭФФЕКТИВНОГО листа
#include "macro/squad.h"      // carry_capacity_g — спина стороны

namespace sm {
namespace {

struct HalfFact {
    int  units   = 0;      // единиц уехало этой половиной
    bool refused = false;  // exact-строка не покрыта — вся сделка отказ
    bool broken  = false;  // снятие после кредита отказало — невыразимо по
                           // построению; честный fail-closed, мир не тронут
};

// Одна половина сделки НА КОПИЯХ: src → dst по строкам лота. massLeftG —
// остаток спины приёмника, общий на все строки половины. Остатки ходят
// дверью sub_sat (ЗАКОН ТРЁХ ДВЕРЕЙ): «ниже нуля» у них не бывает по
// построению, и дверь делает это высказыванием, а не надеждой.
HalfFact ship_half(Inventory& src, Inventory& dst,
                   std::span<DealLine> lines, std::uint64_t& massLeftG) {
    HalfFact out{};
    for (DealLine& line : lines) {
        line.done = 0;
        if (line.want <= 0 || line.def == 0) {
            if (line.exact) { out.refused = true; return out; }
            continue;
        }
        const std::uint16_t arriveDef = line.asDef ? line.asDef : line.def;
        const std::uint64_t unitG =
            std::uint64_t(unit_mass_g(item_def_at(int(arriveDef))));
        std::uint32_t left = std::uint32_t(line.want);
        // Один стак: 0 = не наш/пустой (идти дальше), 1 = взято, -1 = стоп
        // строки (масса кончилась или приёмник отказал — кламп, не отказ).
        const auto try_slot = [&](int i) -> int {
            const ItemRef s = src.slots[std::size_t(i)];
            if (s.empty() || s.def != line.def) return 0;
            const std::uint64_t byMass = massLeftG / unitG;
            const int take = int(std::min<std::uint64_t>(
                {std::uint64_t(left), std::uint64_t(s.count), byMass}));
            if (take <= 0) return -1;
            ItemRef payload = s;
            payload.def = arriveDef;
            payload.count = take;
            if (!dst.add_ref(payload)) return -1;
            if (!src.remove_at(i, take)) { out.broken = true; return -1; }
            left = sub_sat(left, std::uint32_t(take));
            massLeftG = sub_sat(massLeftG,
                                std::uint64_t(take) * unitG);
            return 1;
        };
        if (line.slot != kDealAnyStack) {
            if (int(line.slot) < kMaxInventorySlots)
                (void)try_slot(int(line.slot));
        } else {
            // Старые первыми. Предметная область растёт от 0 — восходящий
            // проход; пустеющий слот предмета дыры не чинит, шаг всегда.
            for (int i = 0; left > 0 && !out.broken
                            && i < src.creature_first(); ++i) {
                if (try_slot(i) < 0) break;
            }
            // Область существ растёт вниз от 1023 — нисходящий проход;
            // целиком снятый стак чинится НОВЕЙШИМ в тот же слот
            // (Inventory::remove_at), поэтому после взятия слот
            // ПЕРЕСМАТРИВАЕТСЯ, шаг только мимо чужих стаков. Зацикливание
            // невыразимо: повторное взятие упирается в left/массу/приёмник.
            for (int i = kMaxInventorySlots - 1;
                 left > 0 && !out.broken && i >= src.creature_first();) {
                const int r = try_slot(i);
                if (r < 0) break;
                if (r == 0) --i;
            }
        }
        line.done = line.want - int(left);
        out.units += line.done;
        if (line.exact && line.done != line.want) {
            out.refused = true;
            return out;
        }
    }
    return out;
}

} // namespace

DealFact deal(MacroStore& st,
              std::uint16_t a, std::uint16_t b,
              std::span<DealLine> fromA, std::span<DealLine> fromB,
              std::int64_t massCapToB, std::int64_t massCapToA) {
    DealFact out{};
    const auto zero_facts = [&] {
        for (DealLine& l : fromA) l.done = 0;
        for (DealLine& l : fromB) l.done = 0;
    };
    // Сторона сама с собой — не сделка; выход за кап — не адрес.
    if (a == b || a >= kUnifiedCap || b >= kUnifiedCap) {
        zero_facts();
        return out;
    }
    Inventory& realA = st.inventory[a].inv;
    Inventory& realB = st.inventory[b].inv;
    // Идиома barter_swap: исполнение на КОПИЯХ, коммит целиком — отказ
    // exact-строки или невыразимый слом оставляют мир нетронутым.
    Inventory na = realA;
    Inventory nb = realB;
    std::uint64_t leftToB =
        std::uint64_t(std::max<std::int64_t>(0, massCapToB));
    std::uint64_t leftToA =
        std::uint64_t(std::max<std::int64_t>(0, massCapToA));
    const HalfFact ab = ship_half(na, nb, fromA, leftToB);
    if (ab.refused || ab.broken) { zero_facts(); return out; }
    const HalfFact ba = ship_half(nb, na, fromB, leftToA);
    if (ba.refused || ba.broken) { zero_facts(); return out; }
    if (ab.units == 0 && ba.units == 0) return out;   // пустая сделка
    realA = na;
    realB = nb;
    out.unitsAB = ab.units;
    out.unitsBA = ba.units;
    out.settled = true;
    // ДОЛГ МЕСТА — приход гасит СРАЗУ (CANON S10). Ветки по роду нет:
    // у ходока счёт стоит нулями и дверь гашения молчит сама. Паства же
    // ДВЕРИ НЕ ПРИНАДЛЕЖИТ (вердикт 2026-10-23: «паства это только ответ
    // АИ модуля») — дверь объектна и душ не знает.
    if (ab.units > 0)
        econ_pay_debt(st.inventory[b].inv, st.upkeep[b].needDebt);
    if (ba.units > 0)
        econ_pay_debt(st.inventory[a].inv, st.upkeep[a].needDebt);
    return out;
}

StagedValue stage_value_lines(const Inventory& inv, int value,
                              std::span<DealLine> out) {
    StagedValue r{};
    if (value <= 0 || out.empty()) return r;
    bool skip[kMaxInventorySlots] = {};
    // Остаток покрытия — дверью sub_sat: последняя строка вправе ПЕРЕПЛАТИТЬ
    // (бартерный закон), и насыщение в ноль ровно это и говорит.
    std::uint64_t left = std::uint64_t(value);
    while (left > 0 && r.lines < int(out.size())) {
        const DensestSlot d = densest_value_slot(inv, skip);
        if (d.slot < 0) break;
        skip[d.slot] = true;
        const ItemRef& s = inv.slots[std::size_t(d.slot)];
        const std::uint64_t uv = std::uint64_t(d.value);
        const std::uint64_t want = (left + uv - 1) / uv;
        const int take = want < std::uint64_t(s.count) ? int(want)
                                                       : int(s.count);
        if (take <= 0) continue;
        DealLine& l = out[std::size_t(r.lines++)];
        l = DealLine{};
        l.want  = take;
        l.def   = s.def;
        l.slot  = std::uint16_t(d.slot);
        l.exact = 1;
        r.value += take * d.value;
        left = sub_sat(left, std::uint64_t(take) * uv);
    }
    return r;
}

RowSide side_row(const MacroStore& st, std::uint16_t slot, int itemIdx,
                 std::int64_t base) {
    const Inventory& inv = st.inventory[slot].inv;
    return row_side(itemIdx, base, st.upkeep[slot].needDebt, inv,
                    unit_mass_g(item_def_at(itemIdx)),
                    carry_capacity_g(st.runtime[slot], inv));
}

int side_trade_power(const MacroStore& st, std::uint16_t slot) {
    return trade_power_of(effective_sheet_of(st, handle_at(st, slot)));
}

StagedPayment stage_payment(const MacroStore& st, std::uint16_t payer,
                            std::uint16_t receiver, std::int64_t target,
                            std::int64_t ceiling, std::int64_t roomG,
                            std::span<DealLine> out) {
    StagedPayment r{};
    if (target <= 0 || out.empty()) return r;
    const Inventory& inv = st.inventory[payer].inv;
    const int payerPower = side_trade_power(st, payer);
    const int receiverPower = side_trade_power(st, receiver);
    bool skip[kMaxInventorySlots] = {};
    std::uint64_t room = std::uint64_t(roomG < 0 ? 0 : roomG);
    std::int64_t left = target;
    while (left > 0 && r.lines < int(out.size())) {
        const DensestSlot d = densest_value_slot(inv, skip);
        if (d.slot < 0) break;
        skip[d.slot] = true;
        const ItemRef& s = inv.slots[std::size_t(d.slot)];
        const std::uint64_t unitG =
            std::uint64_t(unit_mass_g(item_def_at(int(s.def))));
        const std::uint64_t byRoom = room / unitG;
        const std::int64_t avail = std::int64_t(
            byRoom < std::uint64_t(s.count) ? byRoom : std::uint64_t(s.count));
        if (avail <= 0) continue;
        // Строка могла уже уехать соседним стаком этой же корзины: обе
        // стороны считают её от состояния ПОСЛЕ уже застейдженного.
        std::int64_t staged = 0;
        for (int i = 0; i < r.lines; ++i)
            if (out[std::size_t(i)].def == s.def)
                staged += out[std::size_t(i)].want;
        RowSide giver = side_row(st, payer, int(s.def), d.value);
        RowSide taker = side_row(st, receiver, int(s.def), d.value);
        giver.net = giver.net - staged;
        taker.net = taker.net + staged;
        // Покрыть остаток с запасом — если плательщик остаётся под своим
        // потолком; иначе ровно столько, сколько укладывается в остаток.
        LotQuote q = cover_lot(giver, taker, payerPower, receiverPower,
                               avail, left);
        if (q.units > 0
            && r.giverWorth + lot_loss(giver, q.units) > ceiling)
            q = fit_lot(giver, taker, payerPower, receiverPower, avail, left);
        if (q.units <= 0) continue;
        DealLine& l = out[std::size_t(r.lines++)];
        l = DealLine{};
        l.want  = int(q.units);
        l.def   = s.def;
        l.slot  = std::uint16_t(d.slot);
        l.exact = 1;
        r.price += q.price;
        r.giverWorth += lot_loss(giver, q.units);
        r.takerWorth += lot_take(taker, q.units);
        left = q.price >= left ? 0 : left - q.price;
        room = sub_sat(room, std::uint64_t(q.units) * unitG);
    }
    return r;
}

} // namespace sm
