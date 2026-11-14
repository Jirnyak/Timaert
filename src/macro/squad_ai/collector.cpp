// ИИ-МОДУЛЬ СКВАДА РОДА Collector — сборщик дани: идёт вниз к
// вассалу-должнику и везёт взысканное домой. Строка `kAiModuleRows`;
// окно — `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>
#include <span>

#include "bus/anketa.h"      // Inventory, ItemRef, kMaxInventorySlots
#include "core/subtract.h"   // sub_sat — остаток долга и спины на плане груза
#include "macro/currency.h"  // payable_row — что из полки идёт в счёт
#include "macro/deal.h"      // DealLine — строка лота дани
#include "macro/economy.h"   // lot_loss, lot_take, gain_denser — ядро оценки
#include "tables/items.h"    // item_mass_g

namespace sm {

namespace {

// The SUZERAIN place a town owes — a place's Stance::Suzerain row in its
// (`capital_of_` СНЕСЁН 2026-09-22 вместе с дорогой дани ВВЕРХ: он отвечал
//  «кому этот город платит», а платить наверх больше некому — сюзерен сам
//  посылает сборщика вниз. Наряд сессии 7 требовал этого сноса, и вот он.)

// ── СБОРЩИК ИДЁТ ВНИЗ (CANON S4 «Сборщик идёт вниз», Б-4) ───────────────
// Здесь стоял `ai_taxrun` — 145 строк, и он носил дань ВВЕРХ: вассал сам
// снаряжал курьера к сюзерену. Канон говорит обратное, и владелец повторил
// это дословно: «город рождает… сборщика налогов, который ходит в
// вассальные деревни». Машина перевёрнута 2026-09-22.
//
// (Поручение ставил аукцион — он казнён ломтём 4 M-72; должника модуль
//  находит сам, разбор у гейта должника в `collector_think` ниже.)
//
// ── ДАНЬ — БАРТЕРОМ: ДОЛГ МЕРЯЕТ ОЦЕНКА ВАССАЛА, ГРУЗ ВЫБИРАЕТ ОЦЕНКА ДОМА ──
// Владелец, дословно: «даннь также идеёт через бартер так задумано»; разбор
// одобрен (CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ» п.2): «Цену называет
// стоящий — вассал, к которому пришёл сборщик; наценки нет, как в семье (доля
// ноль). Вассал в своей мерке теряет ровно долг: излишек он ценит дёшево — его
// уходит много единиц; нужное ему ценит дорого — его берут мало. Что из
// разрешённого грузить, выбирает оценка сюзерена (нужда дома сборщика)».
//
// ОДИН МЕХАНИЗМ, БЕЗ СТАДИЙ. Строка полки вассала даёт столько единиц, сколько
// разом позволяют полка, свободная спина сборщика и остаток долга, — а долг
// мерит ОЦЕНКА ВАССАЛА: `n` взятых единиц гасят `lot_loss` его строки на `n`
// (`lot_loss@src/macro/economy.h`). Порядок строк — что дом сюзерена ценит
// плотнее на грамм лота (`lot_take` строки дома против массы лота,
// `gain_denser@src/macro/economy.h`). Форма — та же, какой город грузит
// корован (`city_load_child_@src/macro/squad_ai/city.cpp`): первый проход
// считает лоты и сортирует, второй пересчитывает их в пределах остатка. Гейта
// «берущий ценит выше» у дани нет — дань берётся, а не выменивается.
//
// ЧТО ЗДЕСЬ УМЕРЛО (стояло в глаголе пульта `collect_tithe`): стадия «сперва
// по нужде дома» с нехваткой дома отдельной веткой, долг НОМИНАЛОМ
// (`owed / base`, `owed -= moved * base`) и добор остатка по плотности
// стоимости `transfer_value_dense` — вторая оценка той же вещи у той же
// стороны (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.5). Нужда дома сама поднимает
// его оценку строки — тем выше, чем глубже нехватка, — поэтому такие строки
// встают первыми без отдельного правила.
//
// Долг гасится ФАКТОМ: сделка вправе отдать меньше заказанного, поэтому
// зачитывается `lot_loss` по взятому, дверью пульта `credit_tithe`.
void collector_take_tithe_(AiSelf& s, const AiView& view, AiPult& pult,
                           std::uint16_t vassal, std::uint16_t homeSlot) {
    const std::uint64_t owed = view.tithe_owed(vassal);
    const std::int64_t backG = view.free_back_g(s.h.slot);   // спина этого рейса
    if (owed == 0 || backG <= 0) return;
    const Inventory& shelf = view.bag_of(vassal);
    // Наибольшее n ≤ cap, которое вассал в своей мерке отдаёт не дороже
    // `budget`: его потеря растёт с каждой единицей (оценка запаса — функция
    // состояния), поэтому граница одна и ищется делением пополам.
    const auto units_within = [](const RowSide& theirs, std::int64_t cap,
                                 std::uint64_t budget) {
        std::int64_t lo = 0, hi = cap < 0 ? 0 : cap;
        while (lo < hi) {
            const std::int64_t mid = lo + (hi - lo + 1) / 2;
            if (std::uint64_t(lot_loss(theirs, mid)) <= budget) lo = mid;
            else hi = mid - 1;
        }
        return lo;
    };
    struct Row {
        std::uint16_t def;
        std::int64_t gain; std::int64_t lotG; std::int64_t massG;
    };
    Row rows[kMaxInventorySlots];
    int rowCount = 0;
    // Строка судится один раз, сколько бы стаков её ни лежало: лот берёт весь
    // запас строки (`count_of`), и стаки её сделка снимает сама.
    std::uint16_t judged[kMaxInventorySlots];
    int judgedCount = 0;
    for (int i = 0; i < int(shelf.slots.size()); ++i) {
        const ItemRef& sl = shelf.slots[std::size_t(i)];
        const ItemDef* d = payable_row(sl);
        if (!d) continue;
        bool seen = false;
        for (int k = 0; k < judgedCount && !seen; ++k) seen = judged[k] == sl.def;
        if (seen) continue;
        judged[judgedCount++] = sl.def;
        if (d->value <= 0) continue;
        const std::int64_t massG = item_mass_g(*d);
        const std::int64_t units = units_within(
            view.row_of(vassal, int(sl.def)),
            std::min<std::int64_t>(shelf.count_of(int(sl.def)), backG / massG),
            owed);
        if (units <= 0) continue;
        rows[rowCount++] = Row{sl.def,
                               lot_take(view.row_of(homeSlot, int(sl.def)), units),
                               units * massG, massG};
    }
    std::sort(rows, rows + rowCount, [](const Row& a, const Row& b) {
        return gain_denser(a.gain, a.lotG, b.gain, b.lotG);
    });
    std::uint64_t left = owed;   // остаток долга на плане, в оценке вассала
    std::int64_t loadedG = 0;    // масса, уже положенная в лот
    DealLine lot[kMaxInventorySlots];
    RowSide theirsAt[kMaxInventorySlots];   // строка вассала ДО сделки
    int lines = 0;
    for (int r = 0; r < rowCount && left > 0 && loadedG < backG; ++r) {
        const RowSide theirs = view.row_of(vassal, int(rows[r].def));
        const std::int64_t units = units_within(
            theirs,
            std::min<std::int64_t>(shelf.count_of(int(rows[r].def)),
                                   (backG - loadedG) / rows[r].massG),
            left);
        if (units <= 0) continue;
        left = sub_sat(left, std::uint64_t(lot_loss(theirs, units)));
        loadedG += units * rows[r].massG;
        lot[lines] = DealLine{};
        lot[lines].want = int(units);
        lot[lines].def = rows[r].def;
        theirsAt[lines] = theirs;
        ++lines;
    }
    if (lines == 0) return;
    pult.hand(vassal, s.h.slot, std::span<DealLine>(lot, std::size_t(lines)),
              backG);
    // Строки лота — разные строки каталога, и сделка снимает только с
    // вассала, поэтому оценка каждой строки ДО сделки и есть та, от которой
    // считается его потеря.
    std::uint64_t worth = 0;
    for (int i = 0; i < lines; ++i)
        worth += std::uint64_t(lot_loss(theirsAt[i], lot[i].done));
    const std::uint64_t credited = pult.credit_tithe(vassal, worth);
    // Факт дани — «вассал заплатил сюзерену» (`Taxed@src/bus/fact.h`):
    // субъект — вассал, объект — дом сборщика, число — зачтённое в долг.
    if (credited > 0)
        pult.fact(int(view.rooted_id(vassal)), FactKind::Taxed, s.cell.idx,
                  int(std::min<std::uint64_t>(credited, std::uint64_t(1) << 30)),
                  int(view.rooted_id(homeSlot)));
}

// ═════ МОДУЛЬ Collector — форма образца ═════════════════════════════════
// ДОЛЖНИК — ИЗ ЦЕЛИ (род Squad), как рынок у рейса сбыта: поручение
// (`rt.errandObject`) здесь умерло тем же ломтём и по той же причине — цель
// УЖЕ несла ординал вассала в обеих ветках Idle, то есть колонка была
// вторым носителем одной величины (DOD п.6). Взыскание целиком — тело этого
// модуля (`collector_take_tithe_` выше): «сколько увёз, столько и списал»
// есть одна думка, и долг гасится по факту сделки.
void collector_think(AiSelf& s, const AiView& view, AiPult& pult) {
    const std::uint32_t home = view.home(s.h);
    if (home == ecs::kNoCell) { pult.nomad(s); return; }
    ecs::SquadGoal& g = s.goal;
    // Плечо дома — ребро Parent (M-245); вассала — колонки его ТЕЛА.
    // (Ветка «дома нет» снята как недостижимая: `view.home` выше спрашивает
    // тот же слот того же ребра.)
    const std::uint16_t homeSlot = view.home_slot(s.h);
    auto& rt = s.rt;

    // ДОРОГА ДОМОЙ ДОЛЖНИКА НЕ СПРАШИВАЕТ, И ЭТА СТУПЕНЬ СТОИТ ВЫШЕ ГЕЙТА
    // НАМЕРЕННО. Взыскание заканчивается тем, что цель перенацеливается на
    // ДОМ, а `what` обнуляется — должник в ней больше не нужен. Но гейт
    // ниже читает должника ИЗ ЦЕЛИ, и пока эта ступень стояла после него,
    // гружёный сборщик на первой же думке обратного плеча получал
    // «должника нет» и уходил в `home_wander` — а ветка `Returning` с
    // `dump_bag_home` была НЕДОСТИЖИМА.
    //
    // ЧЕМ ЭТО БЫЛО В МИРЕ: долг ребра списывался, товар уезжал со склада
    // вассала — и НАВСЕГДА оставался в спине бродящего вокруг дома
    // сборщика. Дань не доезжала НИ РАЗУ; дом беднел, вассал платил,
    // материя зависала в кармане. Замер этого прибора: долг 200 → 0, у
    // вассала 40 → 0, дома 0, В СПИНЕ 40, сквад жив и бродит на (14,2).
    //
    // ОТКУДА ВЗЯЛОСЬ: ломоть 4 M-72 увёл должника из колонки
    // `rt.errandObject` в колонку ЦЕЛИ — верно по форме (второй носитель
    // одной величины умер), но цель несёт ещё и «куда иду», и обратное
    // плечо затирало ею же идентичность поручения. Свидетеля у рода
    // `Collector` не было ни одного, поэтому дефект приехал зелёным.
    //
    // ПОЧЕМУ ИМЕННО ТАК, А НЕ «СОХРАНИТЬ ДОЛЖНИКА В `what` НА ОБРАТНОМ
    // ПЛЕЧЕ»: `what` отвечает на вопрос «к чему я иду», и на обратном плече
    // честный ответ — ДОМ. Вписать туда должника значило бы заставить
    // колонку врать ради удобства гейта. Какое плечо идёт, знает `state`, и
    // спрашивать надо его.
    //
    // С M-2 СТУПЕНЬ СТАЛА КОРОЧЕ, А ЕЁ МЕСТО — ТЕМ ЖЕ: домой везут ноги свипа,
    // и они же делают сборщика Idle по прибытии, поэтому здесь остались две
    // строки — «в пути домой решать нечего» и «пришёл — вылил». Обе стоят
    // ВЫШЕ гейта должника по той же причине, что и прежде.
    if (rt.state == std::uint8_t(NS::Returning)) return;
    if (rt.state == std::uint8_t(NS::Idle) && ecs::march_arrived(rt)) {
        // Вылив дани домой — ОДНА сделка (ломоть 2 M-72). ТОЛЬКО ДОМА: в
        // клетке дома, тем же вопросом, каким «дома» судит рейс сбыта. Прежняя
        // ветка блуждания выливала сумку и по прибытии в точку прогулки — до
        // двенадцати клеток от склада, мимо закона «ничего не
        // телепортируется» (S5).
        if (s.cell.idx == home)
            pult.dump_bag_home(s, homeSlot);
        charge_squad_recovery(rt,
            std::uint32_t((10 + pult.rand(15)) * kAiTicks));
        return;
    }

    // ДОЛЖНИКА СБОРЩИК НАХОДИТ САМ, И ЭТО ПОЧИНКА МЁРТВОГО ПИСАТЕЛЯ, А НЕ
    // НОВАЯ МЕХАНИКА (ломоть 5а M-72). Цель сборщика ставил АУКЦИОН, а он
    // казнён ломтём 4 — то есть у колонки цели этого рода не осталось
    // писателя ни одного, и всякий рождённый сборщик падал в `home_wander`
    // НАВСЕГДА: род был жив, канал дани пуст. Спрашивать «кто мне должен»
    // есть вопрос о СВОИХ рёбрах (`AiView::owing_vassal` — обход своих
    // `interests` по `Stance::Vassal`, не скан мира), поэтому решение
    // принадлежит МОДУЛЮ, а не внешнему распределителю.
    //
    // ФОРМА ЦЕЛИ — ТА ЖЕ, ЧТО У ДВУХ ВЕТОК Idle НИЖЕ (род Squad, ординал
    // вассала, `onArrival = Working`): второго способа назвать должника не
    // заводится, иначе у одной величины станет два носителя (DOD п.6).
    //
    // ЗДЕСЬ ДОЛЖНИК ТОЛЬКО НАЗЫВАЕТСЯ, В ЦЕЛЬ ЕГО ПИШЕТ ВЫЕЗД (M-2). Прежде
    // эта ступень писала цель сразу — и это было «назвать», а не «идти»,
    // пока ноги спрашивали состояние. Названный адрес теперь значит «везти»
    // (`goal_set@src/bus/squad_row.h`), и запись на этой ступени погнала бы
    // к вассалу сборщика, который ещё отдыхает дома. Обе ветки Idle ниже
    // пишут ту же цель в момент выезда — второго носителя не появилось.
    std::uint16_t vassal = view.goal_rooted(g);
    if (vassal == kMacroNoSlot || vassal == homeSlot) {
        const std::uint16_t owing = view.owing_vassal(homeSlot);
        if (owing == kMacroNoSlot || owing == homeSlot) {
            // Дом никому не сюзерен (или все рассчитались) — ждать нечего.
            pult.home_wander(s);
            return;
        }
        vassal = owing;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        if (squad_recovering(rt)) return;
        // Проснулся НЕ дома (привал в пути будит в Idle) — рейс к должнику
        // продолжается. Дома — решает ветка ниже: должен ли он ещё.
        if (s.cell.idx != home) {
            ecs::goal_set(g, view.rooted_cell(vassal),
                          ecs::goal_what(ecs::GoalKind::Squad,
                                         view.rooted_id(vassal)),
                          0, std::uint8_t(NS::Working));
            rt.state = std::uint8_t(NS::Traveling);
            return;
        }
        if (view.tithe_owed(vassal) == 0) {
            // Должник рассчитался (собрали или простили) — ждать нечего,
            // ротация завтра переторгует эту строку заново.
            charge_squad_recovery(rt,
                std::uint32_t((8 + pult.rand(8)) * kAiTicks));
            return;
        }
        ecs::goal_set(g, view.rooted_cell(vassal),
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     view.rooted_id(vassal)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        // К вассалу везут ноги свипа; прибытие сделает сборщика Working
        // само. АКТ МГНОВЕНЕН: взыскание случится первой же свободной
        // думкой, его длительность повиснет ПОСЛЕ как долг (инверсия
        // castTime).
        // (Здесь стояла строка ног с шрамом 2026-09-22: без неё машина
        //  СТОЯЛА, вечно числясь «в пути» — Traveling 799 563 при Working
        //  РОВНО НОЛЬ. Забыть ноги модулю больше нечем: их у него нет.)
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        if (squad_recovering(rt)) return;
        collector_take_tithe_(s, view, pult, vassal, homeSlot);
        // ВЗЫСКАНИЕ СЛУЧИЛОСЬ — ДОЛГ ПОВИС («сделал → вишу»): тики
        // авторские, своей строки у взыскания пока нет (придёт с M-307).
        charge_squad_recovery(rt,
            std::uint32_t((2 + pult.rand(3)) * kAiTicks));
        ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    // Блуждание с живым должником: в пути решать нечего — прогулку довезут
    // ноги, прибытие разобрано выше гейта должника.
    if (rt.state == std::uint8_t(NS::Wandering)) return;
    // Состояние, которого этот модуль не ведёт (его оставил рефлекс):
    // встать и перерешить. Встать — значит отказаться от адреса.
    ecs::goal_drop(g);
    rt.state = std::uint8_t(NS::Idle);
    charge_squad_recovery(rt, std::uint32_t(4 * kAiTicks));
}

}  // namespace

// Переходник пятёрки — тот же, что у образца и у рейса сбыта.
void ai_collector(MacroHandle e, ecs::MacroNpcRuntime& rt,
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
    collector_think(self, view, pult);
}

}  // namespace sm
