// ИИ-МОДУЛЬ СКВАДА РОДОВ City и Capital — город: крафт своим решением,
// сборщик и корован, погрузка корована. Строки `kAiModuleRows`; окно —
// `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "bus/anketa.h"      // Inventory, Skills, count_human_souls
#include "core/subtract.h"   // sub_sat — угасание слова ожидания
#include "macro/econ_day.h"  // craft_outputs
#include "macro/economy.h"   // row_side, lot_take, lot_loss
#include "macro/squad.h"     // carry_capacity_g
#include "tables/actions.h"
#include "tables/items.h"    // item_parts, item_yield

namespace sm {

namespace {

// ── МОДУЛЬ City (его же зовёт Capital через `ai_city`) ──────────────────
// Всё отсюда до `city_think` включительно — тело одного модуля города,
// написанное целиком; совпадение с телами деревни выше — законная копия,
// а не общий код (AGENTS §4 п.5).

// ДУШИ СВОБОДНЫ ТОЛЬКО ТЕ, БЕЗ КОТОРЫХ ГОРОД ОСТАЁТСЯ СТОЯТЬ, — отсюда
// строгое «больше», а не «не меньше». Это не потолок численности (ЗАКОН
// КЛАМПА): предел рождения даёт сама плата душами (`bleed_flock` в `spawn`),
// а здесь сказано лишь то, что последнюю душу из дома не выписывают — иначе
// город с ровно восемью головами ушёл бы в крю ЦЕЛИКОМ и остался бы с одним
// рабочим юнитом — аватаром: тот же бар покупал бы ему в девять раз меньше
// работы, и производить больше он собрался бы решением, которым производить
// перестал.
bool city_has_spare_souls_(const AiSelf& s) {
    return count_human_souls(s.bag) > kCityCrewSouls;
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
// решает модуль, механика акта одна на всех. Это решение города; у деревни
// своё (`village_craft_pick` выше), копия законна (AGENTS §4 п.5).
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

void city_think(AiSelf& s, const AiView& view, AiPult& pult) {
    // КРАФТ: что и сколько — решает модуль, акт вешает восстановление по
    // сделанному. Решение пересчитывается, только когда изменились инвентарь
    // или счёт нужды.
    city_craft_think_(s, view, pult);
    // КРУГ РЕШЕНИЙ О РОЖДЕНИИ — РАЗ В СУТКИ, и от занятости крафтом он не
    // зависит: рождение — не способность.
    if (city_waits_(s, kCityMemBirthWait)) return;
    s.rt.mem[kCityMemBirthWait] = kAiThinksPerDay;

    // СБОРЩИК — ТОЛЬКО ПРИ ВАССАЛЕ-ДОЛЖНИКЕ, И ВОПРОС ЭТОТ ЗАДАЁТСЯ СВОИМИ
    // РЁБРАМИ, А НЕ ПОИСКОМ ПО МИРУ (`AiView::owing_vassal` — обход своих
    // `interests` по `Stance::Vassal`). Должника нет — сборщику некого доить,
    // и рождать его значило бы послать сквад в `home_wander` навсегда: ровно
    // так он и жил, пока его цель ставил казнённый аукцион.
    if (city_has_spare_souls_(s)
        && view.owing_vassal(s.h.slot) != kMacroNoSlot
        && !view.has_child(s.h, SquadType::Collector)) {
        pult.spawn(s, SquadType::Collector, kCityCrewSouls);
    }
    // КОРОВАН — БЕЗ ГЕЙТА НУЖДЫ, И ЭТО НЕ ПРОПУСК: куда ехать и что брать на
    // рынке, рейс решает сам, НА МЕСТЕ и своей же анкетой (`caravan_think`:
    // рынок называет он, сделку судит ценой рынка). Спрашивать нужду здесь
    // значило бы решить за него дважды — вторым носителем одного решения.
    // Город решает одно — ЧТО ОТДАТЬ: свой излишек (погрузка ниже).
    // Свободные души читаются ЗАНОВО: сборщик выше мог только что увести
    // свои восемь, и вчерашний ответ о них уже неверен.
    if (city_has_spare_souls_(s) && !view.has_child(s.h, SquadType::Caravan))
        pult.spawn(s, SquadType::Caravan, kCityCrewSouls);
    // ГОРОД ГРУЗИТ СВОЙ КОРОВАН (владелец 2026-11-12: «город рождает его и
    // загружает»). Груз отдаёт тот, чей склад: ребёнок, стоящий дома
    // свободным и пустым, получает излишек родителя ОДНОЙ сделкой. Путь
    // один и на рождение, и на возвращение — новорождённый стоит дома
    // пустым ровно так же, как вернувшийся и отдавший наторгованное.
    if (const std::uint16_t waiting =
            view.idle_child_at_home(s.h, SquadType::Caravan);
        waiting != kMacroNoSlot) {
        pult.load_child(s, waiting);
    }
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

// ГОРОД И СТОЛИЦА ОСТАВЛЯЮТ ПО СЕБЕ РУИНУ (вердикт владельца: «например
// город переписывает фичу под собой в руины»). Руина — ЖИВАЯ строка оси и
// фичи, то есть мир честно говорит «здесь ЖИЛИ», а не «здесь город».
void city_death(MacroHandle self, AiPult& pult) {
    pult.reshape_feature(self, FT_Ruin);
}

}  // namespace sm
