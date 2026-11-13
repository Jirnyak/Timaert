// ИИ-МОДУЛЬ СКВАДА РОДА Caravan — рейс к рынку: растит оценку своего
// трюма и везёт наторгованное домой. Строка `kAiModuleRows`; окно —
// `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>

#include "bus/anketa.h"      // inventory_mass_g
#include "core/torus.h"      // cell_x / cell_y — адрес факта
#include "macro/currency.h"  // inventory_value — пуст ли трюм
#include "macro/economy.h"   // trip_goes_on, leg_rate

namespace sm {

// ── The city's trading agent (W2b) ───────────────────────────────────────
// An honest caravan: no TradeRoute abstraction settles anything — the goods
// ride in the caravan's OWN bag between real inventories, so a robbery on
// the road takes REAL cargo. What to haul is decided by the HOME ITSELF —
// its own shelf and its own season's bill, read live at the moment of the
// deal (the cached ledger died 2026-09-30, ломтик E шаг 2): by the time the
// crew gets back the answer may be stale, and that is a trader's life.
//
// How much it hauls is its OWN carry law and nothing else: rt.carryCap =
// get_carry_capacity(sheet) × the row's haulMult (squad.h
// refresh_leader_travel_stats — a caravan is wagons and mules, ×32). A flat
// `kCaravanCapacityKg = 256` lived here until 2026-08-29: a SECOND capacity
// beside the legal one, so the hold neither grew with the avatar's back nor
// answered to the overload law that priced the very same cargo's march.

// (ЗДЕСЬ ЖИЛ ai_caravan — 163 строки рейса «город → город со станциями».
// Снесён 2026-09-21 вместе с родом NPCType::Caravan: караван оказался
// СКВАДОМ, притворившимся видом существа, и его обоз был вписан в породу
// лидера 32 спинами вопреки закону «обоз = сумма спин состава» (squad.h).
// Торговый канал мира держат рейсы сбыта артелей — ai_vendor, одна машина
// рейса на любые два места. Караван вернётся сквадом: люди плюс лошади.)

namespace {

// ═════ МОДУЛЬ Caravan — РЕЙС К РЫНКУ (форма образца) ════════════════════
// РЕЙС К РЫНКУ — одна машина для ЛЮБЫХ двух мест (владелец 2026-09-19:
// «добавим сквад горожан, которые идут в деревню закупаться»). Рынок НАЗЫВАЕТ
// САМ МОДУЛЬ — на выезде из дома и на каждом следующем плече одним вопросом
// (`next_station`: сосед по мембране от точки стояния), — а В ПУТИ его несёт
// ЦЕЛЬ: род `GoalKind::Squad`, ординал рынка в `goal.what`. Феодальное ребро
// рынка не называет: к сюзерену и в деревню домена едет ОДИН рейс, а не два ИИ.
//
// ПОРУЧЕНИЕ (`rt.errandObject`) ЗДЕСЬ УМЕРЛО (ломоть 4 M-72), и это был
// ВТОРОЙ НОСИТЕЛЬ ОДНОЙ ВЕЛИЧИНЫ (DOD п.6): из трёх мест, спрашивавших «где
// мой рынок», ДВА уже читали цель, а третье — колонку, и единственный её
// писатель стоял строкой рядом с `goal_set`, дублируя тот же ординал. Теперь
// носитель один, и ответ у трёх читателей общий (`AiView::goal_rooted`).
// «Откуда пришёл» — другая величина: ею рулетка станции исключает только что
// обслуженный рынок. Она лежит словом памяти модуля (`kCaravanMemPrevStation`).
// ЗАМЫСЕЛ — СЛОВАМИ ВЛАДЕЛЬЦА 2026-11-12 (дословно — CANON, блок «AI-МОДУЛИ»):
// «корован сам выбирает рынки и что ему делать (его аи модуль) город рождает
// его и загружает … корован сам идёт торговать получать прибыль (т оесть по
// сути его цель вырастить стоимость своего инвентаря …) … вернуть всё что
// наторговваал обогатить свой город». Отсюда рейс:
//   · ГРУЗИТ РОДИТЕЛЬ (`city_think` → `load_child`), корован ждёт груза;
//   · на рынке купец растит СВОЮ ОЦЕНКУ СВОЕГО ТРЮМА (S25, «ЕДИНАЯ
//     ЦЕНОМОДЕЛЬ») — отдаёт то, что рынок ценит выше него, берёт то, что сам
//     ценит выше рынка; склад дома в сделке не читается вовсе
//     (`trade_caravan_at_station`);
//   · домой — по ПРЕДЕЛЬНОЙ ОТДАЧЕ (ступень Working ниже), и дома отдаёт всё.
// Своя память рейса — пять слов памяти модуля (`kCaravanMem…@src/macro/npc_ai.h`).
void caravan_think(AiSelf& s, const AiView& view, AiPult& pult) {
    const std::uint32_t home = view.home(s.h);
    if (home == ecs::kNoCell) { pult.nomad(s); return; }
    ecs::SquadGoal& g = s.goal;
    // Плечо дома — ребро Parent (M-245): один резолв слота на такт. Ветки
    // «а если дома нет» здесь больше нет, и это не упрощение: `view.home`
    // выше спрашивает ТОТ ЖЕ слот ТОГО ЖЕ ребра и уже отказал бы — ветка
    // была недостижима с рождения (образец Artel её и не имеет).
    const std::uint16_t homeSlot = view.home_slot(s.h);
    auto& rt = s.rt;

    if (rt.state == std::uint8_t(NS::Idle)) {
        // ПРИШЁЛ (ноги назвали прибытие и сделали сквад Idle). ДОМА КОРОВАН
        // ОТДАЁТ ВСЁ НАТОРГОВАННОЕ (владелец 2026-11-12: «вернуть всё что
        // наторговваал обогатить свой город»): вылив сумки — ОДНА сделка
        // лотом по всему словарю, остаток стоимости — той же дверью цены.
        // «Дома» — клетка дома, тот же вопрос, что у ног (M-461).
        if (ecs::march_arrived(rt)) {
            if (s.cell.idx == home)
                pult.dump_bag_home(s, homeSlot);
            charge_squad_recovery(rt,
                std::uint32_t((10 + pult.rand(15)) * kAiTicks));
        }
        if (squad_recovering(rt)) return;
        // Idle ВНЕ ДОМА — пробуждение после привала в пути (Resting будит в
        // Idle): рейс ПРОДОЛЖАЕТСЯ к рынку, который несёт цель, а без него —
        // домой. Ни погрузки, ни выбора рынка вне дома нет («ничего не
        // телепортируется», S5).
        if (s.cell.idx != home) {
            const std::uint16_t m2 = view.goal_rooted(g);
            if (m2 != kMacroNoSlot && view.is_settlement(m2)) {
                ecs::goal_set(g, view.rooted_cell(m2), g.what, 0,
                              std::uint8_t(NS::Working));
                rt.state = std::uint8_t(NS::Traveling);
            } else {
                ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Returning);
            }
            return;
        }
        // ДОМА. ГРУЗИТ ГОРОД, А НЕ КОРОВАН (владелец 2026-11-12: «город
        // рождает его и загружает»): груз отдаёт модуль родителя своей
        // сделкой (`city_think` → `load_child`), а корован в склад дома не
        // лезет. Пустой трюм значит «дом ещё не дал груза» — ждать.
        // РЫНОК ВЫЕЗДА НАЗЫВАЕТ САМ МОДУЛЬ — тем же вопросом, которым он
        // выбирает следующее плечо рейса: сосед по мембране от точки
        // стояния, рулеткой по дням пути (`next_station`). Точка стояния
        // здесь — дом, «откуда пришёл» — никто. (Стояло «рынок — из цели»,
        // и у вопроса не было писателя: первый рынок ставил аукцион целей,
        // казнённый ломтём 4 M-72, — ряд M-477.)
        const bool loaded = inventory_mass_g(s.bag) > 0
                         || inventory_value(s.bag) > 0;
        const std::uint16_t market = !loaded ? kMacroNoSlot
            : view.rooted_slot(pult.next_station(
                  home, int(view.rooted_id(homeSlot)), 0, rt.moveMult));
        if (market == kMacroNoSlot || market == homeSlot) {
            // Нечего везти либо некуда ехать (соседа по мембране нет):
            // переждать — долгом восстановления, не чужим поведением.
            charge_squad_recovery(rt,
                std::uint32_t((40 + pult.rand(40)) * kAiTicks));
            return;
        }
        // ВЫЕЗД — РЕЙС НАЧИНАЕТ СЕБЯ С НУЛЯ: ни прибыли, ни пройденного пути.
        // По этим двум числам судится возврат.
        rt.mem[kCaravanMemTripGain] = 0;
        rt.mem[kCaravanMemTripTicks] = 0;
        rt.mem[kCaravanMemPrevLegRate] = 0;
        rt.mem[kCaravanMemDeals] = 0;
        rt.mem[kCaravanMemPrevStation] = 0;   // ноль = «никто»
        ecs::goal_set(g, view.rooted_cell(market),
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     view.rooted_id(market)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        // К рынку везут ноги свипа; прибытие сделает сквад Working само.
        // АКТ МГНОВЕНЕН: сделка случится первой же свободной думкой, её
        // длительность повиснет ПОСЛЕ как долг (инверсия castTime).
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        if (squad_recovering(rt)) return;
        if (const std::uint16_t market = view.goal_rooted(g);
            market != kMacroNoSlot && view.is_settlement(market)) {
            // СДЕЛКА РАСТИТ ОЦЕНКУ ТРЮМА, И ПРИБЫЛЬ ПЛЕЧА ЕСТЬ ЕЁ ПРИРОСТ
            // (замысел владельца 2026-11-12: «его цель вырастить стоимость
            // своего инвентаря»; мерка — своя оценка купца, единая
            // ценомодель). Прирост называет сама сделка: во сколько купец
            // ценит взятое минус во сколько ценил отданное. Склад дома здесь
            // не читается. Память насыщается на ширине слова — страж, не
            // поведение.
            const CaravanDeal leg = pult.trade_at(s, market);
            // Нога исполняется, только если оценка купца от неё растёт
            // (`barter_leg_`), так что прибыль сделки не отрицательна; ноль
            // здесь — страж упаковки слова, не поведение.
            const std::int64_t dealGain = leg.gain > 0 ? leg.gain : 0;
            rt.mem[kCaravanMemTripGain] = std::uint32_t(
                std::min<std::int64_t>(
                    std::int64_t(rt.mem[kCaravanMemTripGain]) + dealGain,
                    0xFFFFFFFFll));
            if (leg.movedTableValue > 0) {
                // СДЕЛКА СОСТОЯЛАСЬ — ДОЛГ ПОВИС («сделал → вишу»): тики
                // авторские, своей строки у сделки пока нет. Пустой визит
                // акта не делал — долга нет, развилка решается той же думкой.
                charge_squad_recovery(rt,
                    std::uint32_t((4 + pult.rand(4)) * kAiTicks));
                pult.traded_fact(s, cell_x(g.cell, view.map_w()),
                                 cell_y(g.cell, view.map_w()),
                                 leg.movedTableValue,
                                 int(ecs::goal_ordinal(g.what)));
                // СДЕЛКА СОВЕРШЕНА — вера в среднюю по рейсу растёт с каждой
                // (пустой рынок доверия не прибавляет).
                ++rt.mem[kCaravanMemDeals];
            }
            // ── ВОЗВРАТ ДОМОЙ — ПО ПРЕДЕЛЬНОЙ ОТДАЧЕ ─────────────────────
            // Закон и оба краевых наблюдения владельца (холодный старт,
            // дальняя сверхприбыль) — `trip_goes_on@src/macro/economy.h`, там
            // же свидетели под компилятором; здесь только его входы.
            const int prevId = int(rt.mem[kCaravanMemPrevStation]);
            const std::uint16_t from =
                prevId != 0 ? view.rooted_slot(prevId) : homeSlot;
            const std::int64_t legTicks = view.march_ticks(
                from != kMacroNoSlot ? view.rooted_cell(from) : home,
                s.cell.idx, rt.moveMult);
            const std::int64_t homeTicks =
                view.march_ticks(s.cell.idx, home, rt.moveMult);
            if (legTicks > 0)
                rt.mem[kCaravanMemTripTicks] += std::uint32_t(legTicks);
            // ОКНО В ДВА ПЛЕЧА: отдача предыдущего плеча лежит словом памяти;
            // у первого плеча рейса «предыдущим» служит оно само. В память
            // идёт отдача САМОЙ СДЕЛКИ — страх следующей дороги к прошлому
            // плечу отношения не имеет. Упаковка в слово насыщается: это
            // страж ширины, а не поведение.
            const std::int64_t dealRate =
                leg_rate(dealGain, legTicks, kTicksPerDay);
            const std::int64_t prevRate = prevId != 0
                ? std::int64_t(rt.mem[kCaravanMemPrevLegRate]) : dealRate;
            rt.mem[kCaravanMemPrevLegRate] = std::uint32_t(
                std::min<std::int64_t>(dealRate, 0xFFFFFFFFll));
            const std::uint16_t next = view.rooted_slot(pult.next_station(
                s.cell.idx, int(view.rooted_id(market)), prevId,
                rt.moveMult));
            // Трюм пуст — торговать нечем, искать незачем.
            const bool holdEmpty = inventory_mass_g(s.bag) <= 0
                                && inventory_value(s.bag) <= 0;
            if (next != kMacroNoSlot && next != homeSlot && !holdEmpty
                && legTicks > 0 && homeTicks >= 0) {
                const std::uint32_t nextCell = view.rooted_cell(next);
                // ОЖИДАНИЕ ОТ СЛЕДУЮЩЕГО ПЛЕЧА — ОТДАЧА ПОСЛЕДНЕГО, ЗА ВЫЧЕТОМ
                // СТРАХА ДОРОГИ К НАЗВАННОМУ РЫНКУ (поле угрозы): плечо, чью
                // прибыль съедает страх, не едут — отказ ценой, а не
                // запретом. Сегодня ожидание кормит только опыт; знание о
                // дальних ценах (поле цены, ряд M-479) встанет сюда же.
                const std::int64_t fear = view.route_fear(s.cell.idx, nextCell);
                const std::int64_t expectRate =
                    leg_rate(dealGain - fear, legTicks, kTicksPerDay);
                const std::int64_t tripGain =
                    std::int64_t(rt.mem[kCaravanMemTripGain]);
                // БЕЗ ПРИБЫЛИ страху противопоставить нечего: на опасную
                // дорогу рейс без единой сделки не сворачивает. «Первый
                // рынок рейса» — тот, куда пришли из дома (станции нет).
                if ((tripGain > 0 || fear == 0)
                    && trip_goes_on(expectRate, prevRate, tripGain,
                                    std::int64_t(rt.mem[kCaravanMemTripTicks])
                                        + homeTicks,
                                    std::int64_t(rt.mem[kCaravanMemDeals]),
                                    /*firstStop=*/prevId == 0,
                                    kTicksPerDay)) {
                    rt.mem[kCaravanMemPrevStation] = view.rooted_id(market);
                    // Рынок следующего плеча НАЗЫВАЕТ ЦЕЛЬ, и больше ничто.
                    ecs::goal_set(g, nextCell,
                                  ecs::goal_what(ecs::GoalKind::Squad,
                                                 view.rooted_id(next)),
                                  0, std::uint8_t(NS::Working));
                    rt.state = std::uint8_t(NS::Traveling);
                    return;
                }
            }
        }
        rt.mem[kCaravanMemPrevStation] = 0;   // ноль = «никто»
        ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    // Wandering/Returning: в пути решать нечего — везут ноги свипа, вылив
    // дома разбирает ступень Idle по прибытии.
}

}  // namespace

// The sell-run machine (defined with the trade behaviours below): the
// peasant crew whose errand is Sell walks the SAME machine the vendor
// walked — reuse, not a second copy (CANON S26).

// Переходник пятёрки — тот же, что у образца: собирает СВОЮ СТРОКУ, ВИД и
// ПУЛЬТ. Фолбэк «мира нет» ведёт бродягой — ровно куда вёл прежний отказ
// `!ctx.mw.store` в теле (а `gs` здесь добавлен не ради формы: летопись
// сделки разыменовывала его без проверки).
void ai_vendor(MacroHandle e, ecs::MacroNpcRuntime& rt,
               ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) {
        ai_nomad(e, rt, pools, ctx);
        return;
    }
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel).
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    scent_squad_deposit(e, kind, ctx);
    if (squad_threat_step(e, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, kind, rt, pools, ctx)) return;
    AiSelf self{e, store_ctx(ctx).cell[e.slot], rt, pools,
                store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    caravan_think(self, view, pult);
}

}  // namespace sm
