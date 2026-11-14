// ИИ-МОДУЛЬ СКВАДА РОДА Village — деревня: крафт своим решением,
// харч детям у порога, артель нужного типа. Строка `kAiModuleRows`;
// окно — `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

#include "bus/anketa.h"      // Inventory, Skills, count_human_souls
#include "core/subtract.h"   // sub_sat — угасание слова ожидания
#include "macro/deal.h"      // DealLine, kNoCarryLimitG — лот голов крю
#include "macro/econ_day.h"  // craft_outputs, hunger_commodity_ordinal
#include "macro/economy.h"   // row_side, lot_take, lot_loss, gain_units
#include "macro/squad.h"     // carry_capacity_g
#include "tables/actions.h"
#include "tables/commodity.h"
#include "tables/items.h"    // item_parts, item_yield
#include "tables/npc.h"      // is_folk_kind — людская ли голова
#include "tables/world_row.h"  // creature_of_world_row — род головы

namespace sm {

namespace {

// ── МОДУЛЬ Village ───────────────────────────────────────────────────────
// Всё отсюда до `village_think` включительно — тело одного модуля деревни,
// написанное целиком; у города свой комплект ниже, и копия у соседа законна
// (AGENTS §4 п.5: под модули общего не создаётся).

// ДУШИ СВОБОДНЫ ТОЛЬКО ТЕ, БЕЗ КОТОРЫХ ДЕРЕВНЯ ОСТАЁТСЯ СТОЯТЬ, — отсюда
// строгое «больше», а не «не меньше». Это не потолок численности (ЗАКОН
// КЛАМПА): предел рождения даёт сама плата душами (аватар и лот голов крю),
// а здесь сказано лишь то, что последнюю душу из дома не выписывают — иначе
// деревня с ровно восемью головами ушла бы в крю ЦЕЛИКОМ и осталась бы с
// одним рабочим юнитом — аватаром: тот же бар покупал бы ему в девять раз
// меньше работы, и производить больше она собралась бы решением, которым
// производить перестала.
bool village_has_spare_souls_(const AiSelf& s) {
    return count_human_souls(s.bag) > kVillageCrewSouls;
}

// КРЮ — ТЕ ЖЕ ГОЛОВЫ ДОМА, И КАКИЕ ИМЕННО, РЕШАЕТ ДЕРЕВНЯ (вердикт владельца
// 2026-11-13, CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ» п.3; ряд M-496).
// Рождение — два шага: дверь рождения ставит строку и аватара — одну душу
// дома (`AiPult::spawn`), — а остальные души крю едут к рождённому ЛОТОМ
// ГОЛОВ одной передачей (`AiPult::hand`): те же головы, с их родом и
// уровнем, никого «из спецификации».
//
// ОЧЕРЕДЬ ОТДАЧИ — та же, в какой дом теряет души и дверью убыли
// (`bleed_flock@src/bus/world_row.h`): людские стаки снизу области существ
// вверх, новейший первым. Каждая строка называет СВОЙ стак и едет ровно
// (`exact`): снятие стака целиком чинит дыру только у себя и ниже
// (`Inventory::remove_at@src/bus/anketa.h`), поэтому стаки дальше по очереди
// адреса не меняют. Возврат — число строк; голов в них до `souls`.
int village_crew_lot_(const Inventory& bag, int souls,
                      DealLine (&lot)[kVillageCrewSouls]) {
    int lines = 0;
    int given = 0;   // голов уже в лоте — счёт вверх, до `souls`
    for (int k = bag.creature_first();
         k < kMaxInventorySlots && given < souls && lines < kVillageCrewSouls;
         ++k) {
        const ItemRef& head = bag.slots[std::size_t(k)];
        if (!is_folk_kind(std::uint16_t(creature_of_world_row(head.def))))
            continue;
        const int n = std::min(souls - given, int(head.count));
        lot[lines] = DealLine{};
        lot[lines].want = n;
        lot[lines].def = head.def;
        lot[lines].slot = std::uint16_t(k);
        lot[lines].exact = 1;
        ++lines;
        given += n;
    }
    return lines;
}

// РОДИТЬ АРТЕЛЬ ТИПА `t` — строка и аватар дверью рождения, затем крю дома
// лотом голов. Счёт душ точен по построению: рожает деревня только при
// душах сверх крю (`village_has_spare_souls_` — гейт звонящего), аватар
// снимает ровно одну, и после него дома людей не меньше `крю − 1`, так что
// лот набирает их полностью; передача атомарна (exact-строки, исполнение на
// копиях), а сумка рождённого пуста (`make_npc@src/macro/npc_spawn.cpp`),
// и принять лот ей нечем отказать. Дом теряет ровно крю, рождённый несёт
// аватара и `крю − 1` голов.
void village_raise_crew_(AiSelf& s, AiPult& pult, SquadType t) {
    const MacroHandle born = pult.spawn(s, t);
    if (born.slot == kMacroNoSlot) return;
    DealLine lot[kVillageCrewSouls];
    const int lines = village_crew_lot_(s.bag, kVillageCrewSouls - 1, lot);
    if (lines > 0)
        pult.hand(s.h.slot, born.slot,
                  std::span<DealLine>(lot, std::size_t(lines)),
                  kNoCarryLimitG);
}

// КОГДА МОДУЛЬ ДЕРЕВНИ ДУМАЕТ О КРАФТЕ — ТРИ ВОПРОСА, ТРИ НОСИТЕЛЯ.
// «Занят ли» после крафта назначает АКТ: он вешает восстановление по
// сделанному (ЗАКОН СПОСОБНОСТИ п.4), и модуль лишь спрашивает гейт. «Есть ли
// что решать» отвечает счётчик изменений своего инвентаря и свёртка счёта
// нужды: решение зависит только от них (навыки неизменны, спина и юниты
// выводятся из того же инвентаря), поэтому пока они не сдвинулись, прежнее
// «крафтить нечего» верно, и думка стоит одного сравнения. Таймера нет
// (вердикт владельца 2026-11-13, ряд M-497). «Круг решений о рождении» идёт
// раз в сутки своим словом (`AiView::has_child` — холодный проход, и его
// шапка требует дневного каденса).
//
// Истина — «ещё жду»: слово убывает на думку.
bool village_waits_(AiSelf& s, int word) {
    std::uint32_t& left = s.rt.mem[word];
    if (left == 0) return false;
    left = sub_sat(left, std::uint32_t(1));   // угасание счёта ожидания
    return true;
}

// Свёртка счёта нужды в слово памяти: порядок строк в неё входит, поэтому
// перестановка долга между товарами её сдвигает.
std::uint32_t village_need_word_(const std::int32_t* needDebt) {
    if (!needDebt) return 0;
    std::uint32_t h = 0;
    for (int c = 0; c < kCommodityCount; ++c)
        h = h * 31u + std::uint32_t(needDebt[c]);
    return h;
}

} // namespace — решение о крафте судит свидетель, ему нужна внешняя связь

// ── ЧТО И СКОЛЬКО КРАФТИТЬ — РЕШЕНИЕ МОДУЛЯ ДЕРЕВНИ ─────────────────────────
// Вердикты владельца (CANON S25, 2026-11-13; п.4 вердиктов 2026-10-22):
// решает модуль, механика акта одна на всех. Это решение деревни; у города
// своё (`city_craft_pick` ниже), копия законна (AGENTS §4 п.5).
//
// ПРАВИЛО: крафт есть сделка с самим собой (вердикт 2026-11-13, «Б»). Выгода
// лота — во сколько сквад ценит пришедший выход минус во сколько ценил ушедшие
// входы, той же оценкой запаса, какой меряет торговля
// (`lot_take@src/macro/economy.h`, `lot_loss@src/macro/economy.h`; нужду несёт
// счёт). Из всего, на что есть право, берётся лот с наибольшей выгодой;
// объём — не больше, чем кормят материалы и чем покупает нынешний бар SP (в
// долг деревня не трудится). Ни один лот оценку не растит — крафтить
// нечего. Потолка числом нет: выход копится — масса против спины роняет его
// оценку; вход тает — его оценка растёт.
//
// ПОИСК ОБЪЁМА: выгода как функция числа партий вогнута (каждая следующая
// даёт не больше предыдущей; ревью 2026-11-13 — 16 817 состояний точной
// арифметикой), поэтому максимум один и ищется делением отрезка на трети с
// перебором остатка. Оценка округлена до кванта, так что на плато поиск
// вправе встать в пределах шума округления от максимума.
CraftPick village_craft_pick(const Inventory& bag,
                             const std::int32_t* needDebt,
                             const Skills& skills, std::int64_t backG,
                             int mintFactionIdx, int maxSp, int sp,
                             int units) {
    CraftOutput outs[kCraftOutputCap];
    const int outCount = craft_outputs(mintFactionIdx, outs);
    const auto side = [&](int idx) {
        const ItemDef* d = item_def_at(idx);
        return row_side(idx, d ? d->value : 0, needDebt, bag,
                        d ? std::int64_t(item_mass_g(*d)) : 1, backG);
    };
    CraftPick best{};
    for (int c = 0; c < outCount; ++c) {
        const CraftOutput& o = outs[c];
        const ActionDef& act = action_def(int(o.action));
        if (!recipe_known(skills, act.gateSkill, int(act.minRank))) continue;
        // Предел объёма: бар (цена строки, делённая на рабочих юнитов), и
        // материалы — каждая часть состава.
        std::int64_t most =
            act_sp_reach(maxSp, act.spRatePerDay, std::int64_t(sp), units);
        const auto parts = item_parts(o.outIdx);
        RowSide partSide[kMaxItemParts];
        int partCount = 0;
        for (const ItemPart& part : parts) {
            most = std::min<std::int64_t>(
                most, bag.count_of(int(part.def)) / int(part.count));
            partSide[partCount++] = side(int(part.def));
        }
        const int yield = item_yield(o.outIdx);
        // Страж ширины двери материи: счёт выхода лежит в `int`.
        most = std::min<std::int64_t>(
            most, std::numeric_limits<int>::max() / std::max(1, yield));
        if (most <= 0) continue;
        const RowSide outSide = side(o.outIdx);
        const auto gain = [&](std::int64_t b) {
            std::int64_t spent = 0;
            for (int k = 0; k < partCount; ++k)
                spent += lot_loss(partSide[k], b * int(parts[std::size_t(k)].count));
            return lot_take(outSide, b * yield) - spent;
        };
        std::int64_t lo = 1, hi = most;
        while (hi - lo > 16) {
            const std::int64_t third = (hi - lo) / 3;
            const std::int64_t m1 = lo + third, m2 = hi - third;
            if (gain(m1) < gain(m2)) lo = m1; else hi = m2;
        }
        for (std::int64_t b = lo; b <= hi; ++b) {
            const std::int64_t g = gain(b);
            if (g > best.gain) best = CraftPick{o.outIdx, int(b), g};
        }
    }
    return best;
}

namespace {

// КРАФТ-ДУМКА ДЕРЕВНИ: модуль решает по СВОЕЙ строке (инвентарь, счёт,
// навыки, спина, бар, юниты) и зовёт глагол. Истина — акт состоялся.
bool village_craft_(AiSelf& s, const AiView& view, AiPult& pult) {
    const CraftPick pick = village_craft_pick(
        s.bag, view.need_account(s.h.slot), s.sheet.skills,
        carry_capacity_g(s.rt, s.bag), view.coin_faction(s.h.slot),
        int(s.pools.maxSp), int(s.pools.sp), work_units(s.bag));
    return pick.outIdx >= 0 && pult.craft(s, pick.outIdx, pick.batches) > 0;
}

// «Думать ли о крафте на этой думке» и сама думка. Запоминается состояние, в
// котором крафтить было НЕЧЕГО; состоявшийся акт сам сдвигает счётчик
// инвентаря, и помнить после него нечего. Пустой бар не запоминается: он
// наполняется сам, без единой записи в инвентарь.
void village_craft_think_(AiSelf& s, const AiView& view, AiPult& pult) {
    if (squad_recovering(s.rt)) return;
    const std::uint32_t seen = std::uint32_t(s.bag.revision() + 1);
    const std::uint32_t debt = village_need_word_(view.need_account(s.h.slot));
    if (s.rt.mem[kVillageMemCraftSeen] == seen
        && s.rt.mem[kVillageMemCraftDebt] == debt) {
        return;
    }
    if (village_craft_(s, view, pult) || s.pools.sp <= 0) return;
    s.rt.mem[kVillageMemCraftSeen] = seen;
    s.rt.mem[kVillageMemCraftDebt] = debt;
}

void village_think(AiSelf& s, const AiView& view, AiPult& pult) {
    // КРАФТ: что и сколько — решает модуль, акт вешает восстановление по
    // сделанному. Решение пересчитывается, только когда изменились инвентарь
    // или счёт нужды.
    village_craft_think_(s, view, pult);
    // КРУГ РЕШЕНИЙ О РОЖДЕНИИ — РАЗ В СУТКИ, и от занятости крафтом он не
    // зависит: рождение — не способность.
    if (village_waits_(s, kVillageMemBirthWait)) return;
    s.rt.mem[kVillageMemBirthWait] = kAiThinksPerDay;

    // ── ХАРЧ ДЕТЯМ: деревня грузит едой всякого своего ребёнка у порога ─────
    // Каждый модуль распоряжается только своим инвентарём: артель отдаёт
    // добычу сама, едой её грузит дом. Семья судит голым сравнением оценок
    // (силы (0, 1)): единица едет, пока ребёнок ценит её выше дома, и не
    // больше его непогашенного счёта харча. Тип ребёнка и его трюм не
    // спрашиваются: голоден и стоит дома — этого довольно.
    static const int kFood = commodity_item_index(hunger_commodity_ordinal());
    view.each_child_here(s.h, [&](std::uint16_t child) {
        const RowSide theirs = view.row_of(child, kFood);
        if (theirs.net >= 0) return;
        const std::int64_t n = gain_units(view.row_of(s.h.slot, kFood),
                                          theirs, -theirs.net, true, 0, 1);
        if (n > 0) pult.hand(s.h.slot, child, kFood, int(n));
    });

    // ── ЧТО ДЕРЕВНЕ НАДО — И АРТЕЛЬ КАКОГО ТИПА ПОДНЯТЬ ─────────────────────
    // (вердикт владельца 2026-11-13: «деревне чтото надо она создаёт артель
    // этого типа и она идёт по своему модулю»). НАДО товар, когда его строка
    // сама в нужде (счёт не оплачен — так стоит еда) либо когда он — сырьё
    // вещи, которую хотят жители (счёт по вещи не оплачен), руки её умеют, а
    // сырья на полке меньше, чем на одну партию. Что даёт место работы,
    // отвечает мир; своего списка товаров деревня не держит.
    if (!village_has_spare_souls_(s)) return;
    // Неопределённая артель у дома сама возьмёт нужду — новую не поднимаю.
    if (view.has_child(s.h, SquadType::Artel)) return;
    const auto wants = [&](int product) {
        if (product < 0) return false;
        if (view.row_of(s.h.slot, product).net < 0) return true;
        CraftOutput outs[kCraftOutputCap];
        const int outCount = craft_outputs(view.coin_faction(s.h.slot), outs);
        for (int c = 0; c < outCount; ++c) {
            if (outs[c].outIdx < 0) continue;
            const ActionDef& act = action_def(int(outs[c].action));
            if (!recipe_known(s.sheet.skills, act.gateSkill, int(act.minRank)))
                continue;
            if (view.row_of(s.h.slot, outs[c].outIdx).net >= 0) continue;
            for (const ItemPart& part : item_parts(outs[c].outIdx)) {
                if (int(part.def) == product
                    && s.bag.count_of(product) < int(part.count)) {
                    return true;
                }
            }
        }
        return false;
    };
    // По одной живой артели на тип; одно рождение за круг. Гейт дубля стоит
    // последним — проход по детям платится только за решение, дошедшее до
    // рождения.
    const std::uint32_t home = s.cell.idx;
    const auto raise = [&](SquadType t, ResourceFieldId row,
                           std::uint32_t site) {
        if (site == ecs::kNoCell || !wants(view.yield_at(row, site)))
            return false;
        if (view.has_child(s.h, t)) return false;
        village_raise_crew_(s, pult, t);
        return true;
    };
    if (raise(SquadType::Ploughmen, ResourceFieldId::Wheat,
              view.field_near(FT_Field, home, home))) return;
    if (raise(SquadType::Hunters, ResourceFieldId::Fauna,
              view.fauna_near(home, home))) return;
    if (raise(SquadType::Flaxers, ResourceFieldId::Wheat,
              view.field_near(FT_FlaxField, home, home))) return;
    if (raise(SquadType::Woodcutters, ResourceFieldId::Trees,
              view.tree_near(home, home))) return;
    (void)raise(SquadType::Miners, ResourceFieldId::Vein,
                view.vein_near(home));
}

}  // namespace

// Переходники пятёрки — те же, что у трёх образцов, МИНУС преамбула
// рефлексов (причина названа в шапке модулей выше).
void ai_village(MacroHandle e, ecs::MacroNpcRuntime& rt,
                ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    AiSelf self{e, store_ctx(ctx).cell[e.slot], rt, pools,
                store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    village_think(self, view, pult);
}

// ДЕРЕВНЯ СНИМАЕТ ФИЧУ ВОВСЕ — ПОЛЕ ЗАРОСЛО (вердикт владельца 2026-10-25,
// строка смерти деревни). `FT_None` здесь ЗНАЧЕНИЕ, а не отказ: у снятия
// вода не спрашивается вовсе (`feature_put` судит воду только у постановки),
// поэтому деревня исчезает с карты фич на любой земле одинаково. Руины она
// по себе не оставляет, и это не копия городской строки, а ДРУГОЙ ответ:
// деревня есть пашня с домами, и без рук её затягивает, а не рушит.
void village_death(MacroHandle self, AiPult& pult) {
    pult.reshape_feature(self, FT_None);
}

}  // namespace sm
