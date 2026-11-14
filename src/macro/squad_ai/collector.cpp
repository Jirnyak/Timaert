// ИИ-МОДУЛЬ СКВАДА РОДА Collector — сборщик дани: раз в сезон обходит
// вассалов своего дома по одному за рейс, у каждого берёт на восьмую часть
// того, во сколько тот ценит свою полку, и везёт взятое домой. Строка
// `kAiModuleRows`; окно — `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>
#include <span>

#include "bus/anketa.h"      // Inventory, ItemRef, kMaxInventorySlots
#include "core/subtract.h"   // sub_sat — остаток квоты на плане груза
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
// (Поручение ставил аукцион — он казнён ломтём 4 M-72; к кому ехать, модуль
//  решает сам, обходом вассалов дома в `collector_think` ниже.)
//
// ── ДАНЬ — ВОСЬМАЯ ЧАСТЬ ОЦЕНКИ ПОЛКИ, НА МЕСТЕ, БЕЗ КНИГИ ДОЛГОВ ──────────
// Владелец, дословно (CANON S25, «ВЕРДИКТЫ 2026-11-14»): «сборщик просто в
// своем модули зебирает 1/8 стоимости склада деревни и всё»; на варианты —
// «Пришёл — забрал 1/8, без книги долгов». Значит КВОТУ сборщик считает НА
// МЕСТЕ, В МОМЕНТ ПРИХОДА: восьмая часть того, во сколько вассал ценит свою
// полку сейчас (`AiView::shelf_worth` — `side_worth@src/macro/deal.h`). Не
// унёс (спина мала, полка бедна) — остаток ПРОЩЁН: его нигде не помнят,
// должников в мире нет.
//
// БАРТЕР ОСТАЛСЯ (владелец, дословно: «даннь также идеёт через бартер так
// задумано»; CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ» п.2): цену называет
// стоящий — вассал, наценки нет, как в семье (доля ноль). Квоту мерит ОЦЕНКА
// ВАССАЛА: `n` взятых единиц его строки стоят ему `lot_loss` на `n`
// (`lot_loss@src/macro/economy.h`). Что грузить, выбирает оценка ДОМА
// сюзерена — что он ценит плотнее на грамм лота (`lot_take` строки дома
// против массы лота, `gain_denser@src/macro/economy.h`).
//
// ОДИН МЕХАНИЗМ, БЕЗ СТАДИЙ. Строка полки вассала даёт столько единиц, сколько
// разом позволяют полка, свободная спина сборщика и остаток квоты. Форма — та
// же, какой город грузит корован (`city_load_child_@src/macro/squad_ai/city.cpp`):
// первый проход считает лоты и сортирует, второй пересчитывает их в пределах
// остатка. Гейта «берущий ценит выше» у дани нет — дань берётся, а не
// выменивается. Нужда дома сама поднимает его оценку строки — тем выше, чем
// глубже нехватка, — поэтому такие строки встают первыми без отдельного
// правила.
//
// (ЧТО ЗДЕСЬ УМЕРЛО. В глаголе пульта `collect_tithe` — стадия «сперва по
//  нужде дома», долг НОМИНАЛОМ и добор по плотности стоимости (ряд M-495,
//  первый шаг). 2026-11-14 — КНИГА ДОЛГОВ: общий проход начисления мира,
//  средняя оценки полки и колонка долга на ребре, а с ними вопросы окна «сколько
//  должен» и «мой должник» и глагол зачёта.)

// ДОЛЯ ДАНИ — ВОСЬМАЯ ЧАСТЬ ОЦЕНКИ ПОЛКИ ВАССАЛА. Восьмая — слово владельца
// («1/8 стоимости склада»), и она по-двойка: доля берётся сдвигом, без
// деления. Число модуля сборщика, а не мира: у другого сборщика оно может
// быть своим.
constexpr int kCollectorTitheShift = 3;

void collector_take_tithe_(AiSelf& s, const AiView& view, AiPult& pult,
                           std::uint16_t vassal, std::uint16_t homeSlot) {
    // Оценка полки неотрицательна по построению (`side_worth`), поэтому доля
    // ложится в беззнаковую квоту как есть.
    const std::uint64_t quota =
        std::uint64_t(view.shelf_worth(vassal)) >> kCollectorTitheShift;
    const std::int64_t backG = view.free_back_g(s.h.slot);   // спина этого рейса
    if (quota == 0 || backG <= 0) return;
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
            quota);
        if (units <= 0) continue;
        rows[rowCount++] = Row{sl.def,
                               lot_take(view.row_of(homeSlot, int(sl.def)), units),
                               units * massG, massG};
    }
    std::sort(rows, rows + rowCount, [](const Row& a, const Row& b) {
        return gain_denser(a.gain, a.lotG, b.gain, b.lotG);
    });
    std::uint64_t left = quota;  // остаток квоты на плане, в оценке вассала
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
    // считается его потеря. Мерится ФАКТ: сделка вправе отдать меньше
    // заказанного.
    std::uint64_t worth = 0;
    for (int i = 0; i < lines; ++i)
        worth += std::uint64_t(lot_loss(theirsAt[i], lot[i].done));
    // Факт дани — «вассал заплатил сюзерену» (`Taxed@src/bus/fact.h`):
    // субъект — вассал, объект — дом сборщика, число — во сколько вассал
    // ценит отданное. Ширину числа строки летописи стережёт страж упаковки.
    if (worth > 0)
        pult.fact(int(view.rooted_id(vassal)), FactKind::Taxed, s.cell.idx,
                  int(std::min<std::uint64_t>(worth, std::uint64_t(1) << 30)),
                  int(view.rooted_id(homeSlot)));
}

// ═════ МОДУЛЬ Collector — форма образца ═════════════════════════════════
// ОБХОД — РАЗ В СЕЗОН, ПО ОДНОМУ ВАССАЛУ ЗА РЕЙС (вердикт владельца
// 2026-11-14, ряд M-495). Своя память модуля
// (`MacroNpcRuntime::mem@src/bus/squad_row.h`, номера слов — у `npc_ai.h`):
// ординал сезона текущего обхода и курсор — сколько вассалов дома уже
// посещено. Дома, без дела: сезон мира сменился — курсор в ноль; цель — вассал дома с
// номером курсора (`AiView::vassal_at`); такого нет — обход сезона кончен, и
// сборщик стоит дома до следующего. Пришёл к вассалу — взял дань — курсор
// плюс один — домой — отдал дому всё привезённое — следующий.
//
// ВАССАЛ РЕЙСА — ИЗ ЦЕЛИ (род Squad), как рынок у рейса сбыта: выезд пишет
// его ординал в цель, и второго носителя «к кому я еду» нет (DOD п.6).
void collector_think(AiSelf& s, const AiView& view, AiPult& pult) {
    const std::uint32_t home = view.home(s.h);
    ecs::SquadGoal& g = s.goal;
    // КЕМ СТАНОВИТСЯ СБОРЩИК БЕЗ ДОМА: БРОДЯГОЙ (ряд M-499, раскладка (з)).
    // Взыскивать не для кого и везти некуда — обход кончен; свой тип он
    // меняет сам, память модуля зануляет, цель бросает и встаёт без дела, а
    // дальше его ведёт модуль бродяги (`squad_ai/nomad.cpp`). Двери «сменить
    // тип» в ядре нет. Назад в сборщика бродяга не превращается: дома нет.
    if (home == ecs::kNoCell) {
        s.rt.squadType = std::uint8_t(SquadType::Nomad);
        for (std::uint32_t& w : s.rt.mem) w = 0;
        ecs::goal_drop(g);
        s.rt.state = std::uint8_t(NS::Idle);
        return;
    }
    // Плечо дома — ребро Parent (M-245); вассала — колонки его ТЕЛА.
    // (Ветка «дома нет» снята как недостижимая: `view.home` выше спрашивает
    // тот же слот того же ребра.)
    const std::uint16_t homeSlot = view.home_slot(s.h);
    auto& rt = s.rt;

    // ДОРОГА ДОМОЙ ВАССАЛА НЕ СПРАШИВАЕТ, И ЭТА СТУПЕНЬ СТОИТ ПЕРВОЙ
    // НАМЕРЕННО. Взыскание кончается тем, что цель перенацеливается на ДОМ, а
    // `what` обнуляется — вассал в ней больше не нужен. Пока ступень стояла
    // ПОСЛЕ гейта, читавшего вассала из цели, гружёный сборщик на первой же
    // думке обратного плеча получал «вассала нет» и уходил бродить — ветка
    // `Returning` с `dump_bag_home` была НЕДОСТИЖИМА: товар уезжал со склада
    // вассала и навсегда оставался в спине (замер того прибора: у вассала
    // 40 → 0, дома 0, В СПИНЕ 40).
    //
    // `what` отвечает на вопрос «к чему я иду», и на обратном плече честный
    // ответ — ДОМ. Какое плечо идёт, знает `state`, и спрашивать надо его.
    // Домой везут ноги свипа, они же делают сборщика Idle по прибытии.
    if (rt.state == std::uint8_t(NS::Returning)) return;

    if (rt.state == std::uint8_t(NS::Idle)) {
        // Вылив дани домой — ОДНА сделка (ломоть 2 M-72). ТОЛЬКО ДОМА: в
        // клетке дома, тем же вопросом, каким «дома» судит рейс сбыта.
        // ВЫЛИВ И ВЗЫСКАНИЕ — ВЗАИМОДЕЙСТВИЕ ПАРЫ, А НЕ СПОСОБНОСТЬ: ни цены,
        // ни восстановления (вердикт владельца 2026-11-13, дословно — CANON
        // S25 «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ»: «ДО ОТКУДА ВАЩЕ ВРЕМЯ это НЕ
        // СПОСОБНОСТИ ЖЕ»), поэтому следующего вассала сборщик выбирает этой
        // же думкой. (Здесь и после взыскания стояли паузы «сделал → вишу»
        // гейтом восстановления; сняты рядом M-494, пункт (1).)
        if (ecs::march_arrived(rt) && s.cell.idx == home)
            pult.dump_bag_home(s, homeSlot);
        // Свой акт сборщик не делает, и гейт восстановления заряжают ему
        // чужие двери — ступень угрозы после автобоя
        // (`squad_threat_step@src/macro/npc_ai.cpp`) и хвост этого модуля
        // ниже; пока он не пуст, сборщик стоит.
        if (squad_recovering(rt)) return;
        // Проснулся НЕ дома (привал в пути будит в Idle): рейс к вассалу,
        // названному целью, продолжается; цель вассала не называет (остаток
        // чужой погони, M-459 — резолвер гейтит ось типа названного) — домой.
        if (s.cell.idx != home) {
            const std::uint16_t vassal = view.goal_rooted(g);
            if (vassal != kMacroNoSlot && vassal != homeSlot) {
                ecs::goal_set(g, view.rooted_cell(vassal),
                              ecs::goal_what(ecs::GoalKind::Squad,
                                             view.rooted_id(vassal)),
                              0, std::uint8_t(NS::Working));
                rt.state = std::uint8_t(NS::Traveling);
            } else {
                ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Returning);
            }
            return;
        }
        // ДОМА, БЕЗ ДЕЛА. Сезон мира сменился — новый обход с первого
        // вассала. Слово хранит ординал сезона плюс один: ноль у слова памяти
        // значит «обхода ещё не было», а ординал через год не повторяется.
        std::uint32_t& tour = rt.mem[kCollectorMemSeason];
        std::uint32_t& cursor = rt.mem[kCollectorMemCursor];
        const std::uint32_t season = std::uint32_t(view.season()) + 1u;
        if (tour != season) {
            tour = season;
            cursor = 0;
        }
        const std::uint16_t vassal = view.vassal_at(homeSlot, int(cursor));
        // Вассала с таким номером нет — обход сезона кончен (или дом никому
        // не сюзерен): сборщик стоит дома до следующего сезона.
        if (vassal == kMacroNoSlot) return;
        ecs::goal_set(g, view.rooted_cell(vassal),
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     view.rooted_id(vassal)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        // К вассалу везут ноги свипа; прибытие сделает сборщика Working
        // само, и взыскание случится первой же свободной думкой.
        // Длительности у него нет: взыскание не способность.
        // (Здесь стояла строка ног с шрамом 2026-09-22: без неё машина
        //  СТОЯЛА, вечно числясь «в пути» — Traveling 799 563 при Working
        //  РОВНО НОЛЬ. Забыть ноги модулю больше нечем: их у него нет.)
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        if (squad_recovering(rt)) return;
        // ПРИШЁЛ К ВАССАЛУ — ВЗЯЛ — КУРСОР ПЛЮС ОДИН. Визит засчитывается
        // ЖИВОМУ вассалу, названному целью: умерший в пути не посещён, и номер
        // его места в обходе займёт следующий живой.
        const std::uint16_t vassal = view.goal_rooted(g);
        if (vassal != kMacroNoSlot && vassal != homeSlot) {
            collector_take_tithe_(s, view, pult, vassal, homeSlot);
            ++rt.mem[kCollectorMemCursor];
        }
        // Взыскание времени не стоит — домой этой же думкой.
        ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    // Состояние, которого этот модуль не ведёт (его оставил рефлекс):
    // встать и перерешить. Встать — значит отказаться от адреса.
    ecs::goal_drop(g);
    rt.state = std::uint8_t(NS::Idle);
    charge_squad_recovery(rt, std::uint32_t(4 * kAiTicks));
}

}  // namespace

// Переходник пятёрки — тот же, что у образца и у рейса сбыта. Без мира думка
// не идёт. Здесь стоял «фолбэк мира нет» бродягой — ветка НЕДОСТИЖИМАЯ:
// переходник зовёт только `dispatch`, а его — только драйвер думок
// `tick_macro_npc_ai@src/macro/npc_ai.cpp`, который без `gs` и стора не идёт
// вовсе; без стора бродяга к тому же разыменовал бы пустоту.
void ai_collector(MacroHandle e, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
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
