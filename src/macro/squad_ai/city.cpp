// ИИ-МОДУЛЬ СКВАДА РОДА City — город: крафт своим решением, сборщик и
// корован, погрузка корована. Строка `kAiModuleRows`; окно —
// `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

#include "bus/anketa.h"      // Inventory, Skills, count_human_souls, inventory_mass_g
#include "core/subtract.h"   // sub_sat — угасание слова ожидания
#include "macro/currency.h"  // payable_row, inventory_value — пуст ли трюм
#include "macro/deal.h"      // DealLine, kNoCarryLimitG — лоты погрузки и крю
#include "macro/econ_day.h"  // craft_outputs
#include "macro/economy.h"   // row_side, lot_take, lot_loss, gain_units, gain_denser
#include "macro/squad.h"     // carry_capacity_g
#include "tables/actions.h"
#include "tables/items.h"    // item_parts, item_yield
#include "tables/npc.h"      // is_folk_kind — людская ли голова
#include "tables/world_row.h"  // creature_of_world_row — род головы

namespace sm {

namespace {

// ── МОДУЛЬ City ─────────────────────────────────────────────────────────
// Всё отсюда до `city_think` включительно — тело одного модуля города,
// написанное целиком; совпадение с телами деревни и столицы (каждая в своём
// файле) — законная копия, а не общий код (AGENTS §4 п.5).

// ДУШИ СВОБОДНЫ ТОЛЬКО ТЕ, БЕЗ КОТОРЫХ ГОРОД ОСТАЁТСЯ СТОЯТЬ, — отсюда
// строгое «больше», а не «не меньше». Это не потолок численности (ЗАКОН
// КЛАМПА): предел рождения даёт сама плата душами (аватар и лот голов крю),
// а здесь сказано лишь то, что последнюю душу из дома не выписывают — иначе
// город с ровно восемью головами ушёл бы в крю ЦЕЛИКОМ и остался бы с одним
// рабочим юнитом — аватаром: тот же бар покупал бы ему в девять раз меньше
// работы, и производить больше он собрался бы решением, которым производить
// перестал.
bool city_has_spare_souls_(const AiSelf& s) {
    return count_human_souls(s.bag) > kCityCrewSouls;
}

// КРЮ — ТЕ ЖЕ ГОЛОВЫ ДОМА, И КАКИЕ ИМЕННО, РЕШАЕТ ГОРОД (вердикт владельца
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
int city_crew_lot_(const Inventory& bag, int souls,
                   DealLine (&lot)[kCityCrewSouls]) {
    int lines = 0;
    int given = 0;   // голов уже в лоте — счёт вверх, до `souls`
    for (int k = bag.creature_first();
         k < kMaxInventorySlots && given < souls && lines < kCityCrewSouls;
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

// РОДИТЬ СКВАД ТИПА `t` — строка и аватар дверью рождения, затем крю дома
// лотом голов. Счёт душ точен по построению: рожает город только при
// душах сверх крю (`city_has_spare_souls_` — гейт звонящего), аватар
// снимает ровно одну, и после него дома людей не меньше `крю − 1`, так что
// лот набирает их полностью; передача атомарна (exact-строки, исполнение на
// копиях), а сумка рождённого пуста (`make_npc@src/macro/npc_spawn.cpp`),
// и принять лот ей нечем отказать. Дом теряет ровно крю, рождённый несёт
// аватара и `крю − 1` голов. Возврат — хэндл рождённого (невалидный —
// рождения не было).
MacroHandle city_raise_crew_(AiSelf& s, AiPult& pult, SquadType t) {
    const MacroHandle born = pult.spawn(s, t);
    if (born.slot == kMacroNoSlot) return born;
    DealLine lot[kCityCrewSouls];
    const int lines = city_crew_lot_(s.bag, kCityCrewSouls - 1, lot);
    if (lines > 0)
        pult.hand(s.h.slot, born.slot,
                  std::span<DealLine>(lot, std::size_t(lines)),
                  kNoCarryLimitG);
    return born;
}

// КОГДА МОДУЛЬ ГОРОДА ДУМАЕТ О КРАФТЕ — ТРИ ВОПРОСА, ТРИ НОСИТЕЛЯ.
// «Занят ли» после крафта назначает АКТ: он вешает восстановление по
// сделанному (ЗАКОН СПОСОБНОСТИ п.4), и модуль лишь спрашивает гейт. «Есть ли
// что решать» отвечает счётчик изменений своего инвентаря и свёртка счёта
// нужды: решение зависит только от них (навыки неизменны, спина и юниты
// выводятся из того же инвентаря), поэтому пока они не сдвинулись, прежнее
// «крафтить нечего» верно, и думка стоит одного сравнения. Таймера нет
// (вердикт владельца 2026-11-13, ряд M-497). «Круг решений о рождении и
// погрузке» идёт раз в сутки своим словом (`AiView::has_child` — холодный
// проход, и его шапка требует дневного каденса).
//
// Истина — «ещё жду»: слово убывает на думку.
bool city_waits_(AiSelf& s, int word) {
    std::uint32_t& left = s.rt.mem[word];
    if (left == 0) return false;
    left = sub_sat(left, std::uint32_t(1));   // угасание счёта ожидания
    return true;
}

// Свёртка счёта нужды в слово памяти: порядок строк в неё входит, поэтому
// перестановка долга между товарами её сдвигает.
std::uint32_t city_need_word_(const std::int32_t* needDebt) {
    if (!needDebt) return 0;
    std::uint32_t h = 0;
    for (int c = 0; c < kCommodityCount; ++c)
        h = h * 31u + std::uint32_t(needDebt[c]);
    return h;
}

} // namespace — решение о крафте судит свидетель, ему нужна внешняя связь

// ── ЧТО И СКОЛЬКО КРАФТИТЬ — РЕШЕНИЕ МОДУЛЯ ГОРОДА ──────────────────────────
// Вердикты владельца (CANON S25, 2026-11-13; п.4 вердиктов 2026-10-22):
// решает модуль, механика акта одна на всех. Это решение города; у деревни и
// у столицы своё, каждое в своём файле, и копия законна (AGENTS §4 п.5).
//
// ПРАВИЛО: крафт есть сделка с самим собой (вердикт 2026-11-13, «Б»). Выгода
// лота — во сколько сквад ценит пришедший выход минус во сколько ценил ушедшие
// входы, той же оценкой запаса, какой меряет торговля
// (`lot_take@src/macro/economy.h`, `lot_loss@src/macro/economy.h`; нужду несёт
// счёт). Из всего, на что есть право, берётся лот с наибольшей выгодой;
// объём — не больше, чем кормят материалы и чем покупает нынешний бар SP (в
// долг город не трудится). Ни один лот оценку не растит — крафтить нечего.
// Потолка числом нет: выход копится — масса против спины роняет его оценку;
// вход тает — его оценка растёт.
//
// ПОИСК ОБЪЁМА: выгода как функция числа партий вогнута (каждая следующая
// даёт не больше предыдущей; ревью 2026-11-13 — 16 817 состояний точной
// арифметикой), поэтому максимум один и ищется делением отрезка на трети с
// перебором остатка. Оценка округлена до кванта, так что на плато поиск
// вправе встать в пределах шума округления от максимума.
CraftPick city_craft_pick(const Inventory& bag,
                          const std::int32_t* needDebt,
                          const Skills& skills, std::int64_t backG,
                          int mintFactionIdx, int maxSp, int sp, int units) {
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

// КРАФТ-ДУМКА ГОРОДА: модуль решает по СВОЕЙ строке (инвентарь, счёт,
// навыки, спина, бар, юниты) и зовёт глагол. Истина — акт состоялся.
bool city_craft_(AiSelf& s, const AiView& view, AiPult& pult) {
    const CraftPick pick = city_craft_pick(
        s.bag, view.need_account(s.h.slot), s.sheet.skills,
        carry_capacity_g(s.rt, s.bag), view.coin_faction(s.h.slot),
        int(s.pools.maxSp), int(s.pools.sp), work_units(s.bag));
    return pick.outIdx >= 0 && pult.craft(s, pick.outIdx, pick.batches) > 0;
}

// «Думать ли о крафте на этой думке» и сама думка. Запоминается состояние, в
// котором крафтить было НЕЧЕГО; состоявшийся акт сам сдвигает счётчик
// инвентаря, и помнить после него нечего. Пустой бар не запоминается: он
// наполняется сам, без единой записи в инвентарь.
void city_craft_think_(AiSelf& s, const AiView& view, AiPult& pult) {
    if (squad_recovering(s.rt)) return;
    const std::uint32_t seen = std::uint32_t(s.bag.revision() + 1);
    const std::uint32_t debt = city_need_word_(view.need_account(s.h.slot));
    if (s.rt.mem[kCityMemCraftSeen] == seen
        && s.rt.mem[kCityMemCraftDebt] == debt) {
        return;
    }
    if (city_craft_(s, view, pult) || s.pools.sp <= 0) return;
    s.rt.mem[kCityMemCraftSeen] = seen;
    s.rt.mem[kCityMemCraftDebt] = debt;
}

// ── ГРУЗ ДОМА — ГОРОД ОТДАЁТ РЕБЁНКУ ТО, ЧТО ТОТ ЦЕНИТ ВЫШЕ (CANON S10) ──
// Вердикты владельца: «город грузит в путь ВСЕ товары, то есть у нас просто
// система, что всё в инвентаре это товар» (2026-09-22) и «город рождает его и
// загружает» (2026-11-12). Закон выбора — единая ценомодель (2026-11-12):
// семья выигрывает ровно на тех единицах, которые ребёнок с его свободной
// спиной ценит выше родителя, — это ЗАВАЛ родителя, излишек, забивший спину
// его жителей. Их дом и отдаёт, выгоднейшее на грамм — первым, пока спина
// ребёнка свободна и оценки не сошлись. Перенос даровой: внутри семьи
// делить нечего.
//
// ЗДЕСЬ СТОЯЛ ОБХОД ПО ПЛОТНОСТИ СТОИМОСТИ, и он грузил МОНЕТУ ПЕРВОЙ: после
// первого же рейса дом набивал спину ребёнка своей казной, а корован с одной
// монетой не может ни отдать (её все ценят одинаково), ни взять (спина
// полна) — рейсы шли пустыми (свидетель рейса, 2026-11-12: 47 пустых стоянок
// из 82). Монета грузилась затем, чтобы дом мог КУПИТЬ недостающее своим же
// корованом (шрам Х-13: «еда 0 при 3 682 монетах»); теперь голодному дому
// еду привозит всякий купец, и платит дом из своего склада.
//
// Нужда дома неприкосновенна по построению: строку, которой ему не хватает,
// он ценит выше ребёнка, и выгоды в её вывозе нет.
//
// ЭТО РЕШЕНИЕ ГОРОДА, И ЖИВЁТ ОНО В ЕГО МОДУЛЕ (ряд M-499 (8)): прежде оно
// стояло в окне глаголом `load_child` с планом `plan_home_load_`. От окна
// модулю нужны только двери, которые вправе позвать всякий сквад: оценка
// строки стороны (`AiView::row_of`), свободная спина ребёнка
// (`AiView::free_back_g`) и передача лотом (`AiPult::hand`).
void city_load_child_(const AiSelf& s, const AiView& view, AiPult& pult,
                      std::uint16_t child) {
    const Inventory& store = s.bag;
    const std::int64_t budgetG = view.free_back_g(child);   // спина этого рейса
    // Строки склада, в которых семье есть что выиграть: лот выгоды под всю
    // свободную спину рейса и выгода на нём — насколько ребёнок ценит взятое
    // выше, чем дом отданное. Порядок — выгода лота на грамм лота. Единицей
    // выгоду не мерить: завал меньше шага оценки на одной штуке не виден, а
    // на лоте виден (ряд M-490).
    struct Row {
        std::uint16_t def;
        std::int64_t gain; std::int64_t lotG; std::int64_t massG;
    };
    Row rows[kMaxInventorySlots];
    int rowCount = 0;
    for (int i = 0; i < int(store.slots.size()); ++i) {
        const ItemRef& sl = store.slots[std::size_t(i)];
        const ItemDef* d = payable_row(sl);
        if (!d) continue;
        bool seen = false;
        for (int k = 0; k < rowCount && !seen; ++k)
            seen = rows[k].def == sl.def;
        if (seen) continue;
        if (d->value <= 0) continue;
        const std::int64_t massG = item_mass_g(*d);
        const RowSide home = view.row_of(s.h.slot, int(sl.def));
        const RowSide theirs = view.row_of(child, int(sl.def));
        // Внутри семьи делить нечего: доля ноль, голое сравнение оценок.
        const std::int64_t units = gain_units(
            home, theirs,
            std::min<std::int64_t>(store.count_of(int(sl.def)),
                                   budgetG > 0 ? budgetG / massG : 0),
            /*strict=*/true, /*standingPower=*/0, /*visitorPower=*/1);
        const std::int64_t gain =
            lot_take(theirs, units) - lot_loss(home, units);
        if (units <= 0 || gain <= 0) continue;
        rows[rowCount++] = Row{sl.def, gain, units * massG, massG};
    }
    std::sort(rows, rows + rowCount, [](const Row& a, const Row& b) {
        return gain_denser(a.gain, a.lotG, b.gain, b.lotG);
    });
    std::int64_t loadedG = 0;   // масса, уже положенная в лот
    DealLine lot[kMaxInventorySlots];
    int lines = 0;
    for (int r = 0; r < rowCount && loadedG < budgetG; ++r) {
        const std::int64_t fit = (budgetG - loadedG) / rows[r].massG;
        const std::int64_t cap =
            std::min<std::int64_t>(store.count_of(int(rows[r].def)), fit);
        if (cap <= 0) continue;
        const std::int64_t units = gain_units(
            view.row_of(s.h.slot, int(rows[r].def)),
            view.row_of(child, int(rows[r].def)),
            cap, /*strict=*/true, /*standingPower=*/0, /*visitorPower=*/1);
        if (units <= 0) continue;
        loadedG += units * rows[r].massG;
        lot[lines] = DealLine{};
        lot[lines].want = int(units);
        lot[lines].def = rows[r].def;
        ++lines;
    }
    if (lines > 0)
        pult.hand(s.h.slot, child,
                  std::span<DealLine>(lot, std::size_t(lines)), budgetG);
}

void city_think(AiSelf& s, const AiView& view, AiPult& pult) {
    // КРАФТ: что и сколько — решает модуль, акт вешает восстановление по
    // сделанному. Решение пересчитывается, только когда изменились инвентарь
    // или счёт нужды.
    city_craft_think_(s, view, pult);
    // КРУГ РЕШЕНИЙ О РОЖДЕНИИ — РАЗ В СУТКИ, и от занятости крафтом он не
    // зависит: рождение — не способность.
    if (city_waits_(s, kCityMemBirthWait)) return;
    s.rt.mem[kCityMemBirthWait] = kAiThinksPerDay;

    // СБОРЩИК — ПРИ ВАССАЛЕ, ОДИН РАЗ ЗА СЕЗОН (ряд M-495, вердикт владельца
    // 2026-11-14 «Пришёл — забрал 1/8, без книги долгов»). «Есть ли у меня
    // вассал» задаётся СВОИМИ рёбрами, а не поиском по миру
    // (`AiView::vassal_at` — обход своих `interests` по `Stance::Vassal`):
    // вассала нет — сборщику некого обходить. Живой сборщик-ребёнок уже
    // ходит — второго не нужно (`has_child`). И в сезоне сборщик поднимается
    // ОДИН раз — своё слово памяти: замена погибшего посреди сезона начала бы
    // обход с первого вассала и обошла бы первых второй раз.
    const std::uint32_t season = std::uint32_t(view.season()) + 1u;
    if (city_has_spare_souls_(s)
        && view.vassal_at(s.h.slot, 0) != kMacroNoSlot
        && !view.has_child(s.h, SquadType::Collector)
        && s.rt.mem[kCityMemCollectorSeason] != season) {
        const MacroHandle born =
            city_raise_crew_(s, pult, SquadType::Collector);
        if (born.slot != kMacroNoSlot) s.rt.mem[kCityMemCollectorSeason] = season;
    }
    // КОРОВАН — БЕЗ ГЕЙТА НУЖДЫ, И ЭТО НЕ ПРОПУСК: куда ехать и что брать на
    // рынке, рейс решает сам, НА МЕСТЕ и своей же анкетой (`caravan_think`:
    // рынок называет он, сделку судит ценой рынка). Спрашивать нужду здесь
    // значило бы решить за него дважды — вторым носителем одного решения.
    // Город решает одно — ЧТО ОТДАТЬ: свой излишек (погрузка ниже).
    // Свободные души читаются ЗАНОВО: сборщик выше мог только что увести
    // свои восемь, и вчерашний ответ о них уже неверен.
    if (city_has_spare_souls_(s) && !view.has_child(s.h, SquadType::Caravan))
        (void)city_raise_crew_(s, pult, SquadType::Caravan);
    // ГОРОД ГРУЗИТ СВОЙ КОРОВАН (владелец 2026-11-12: «город рождает его и
    // загружает»). Груз отдаёт тот, чей склад: ребёнок, стоящий дома
    // свободным и пустым, получает излишек родителя ОДНОЙ сделкой. Путь
    // один и на рождение, и на возвращение — новорождённый стоит дома
    // пустым ровно так же, как вернувшийся и отдавший наторгованное.
    // Кого грузить, судит модуль сам: из детей у порога — первый корован с
    // пустым трюмом (тем же вопросом о пустоте, каким сам рейс судит
    // «нечего везти»). Непустой не догружается: груз ему уже дан.
    std::uint16_t waiting = kMacroNoSlot;
    view.each_child_here(s.h, [&](std::uint16_t child) {
        if (waiting != kMacroNoSlot
            || view.type_of(child) != SquadType::Caravan)
            return;
        const Inventory& hold = view.bag_of(child);
        if (inventory_mass_g(hold) > 0 || inventory_value(hold) > 0) return;
        waiting = child;
    });
    if (waiting != kMacroNoSlot) city_load_child_(s, view, pult, waiting);
}

}  // namespace

// Переходники пятёрки — те же, что у трёх образцов, МИНУС преамбула
// рефлексов (причина названа в шапке модулей выше).
void ai_city(MacroHandle e, ecs::MacroNpcRuntime& rt,
             ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    AiSelf self{e, store_ctx(ctx).cell[e.slot], rt, pools,
                store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    city_think(self, view, pult);
}

// ГОРОД ОСТАВЛЯЕТ ПО СЕБЕ РУИНУ (вердикт владельца: «например город
// переписывает фичу под собой в руины»). Руина — ЖИВАЯ строка оси и
// фичи, то есть мир честно говорит «здесь ЖИЛИ», а не «здесь город».
void city_death(MacroHandle self, AiPult& pult) {
    pult.reshape_feature(self, FT_Ruin);
}

}  // namespace sm
