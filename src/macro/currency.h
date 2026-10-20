// VALUE settlement over the one item catalog — the barter machinery.
//
// A COIN IS NOT A CONCEPT HERE (owner verdict №1 of the second audit,
// 2026-09-17, дословно: «монета это просто товар со стоимостью 10 для
// серебра 100 для золота 1 для медяка — никаких особых механик и ворот,
// всё через бартер»). The kCurrencyDefs list, the is_currency_item gate and
// the wallet trio (wallet_value / transfer_value / wallet_spend_up_to) that
// walked that list died 2026-09-18. What a faction mints is the faction
// registry's own mint columns (macro/faction.h faction_coins); everything
// in THIS header prices and moves stacks by the one contextual value law,
// and a coin wins purely by arithmetic — minimal weight at maximal value.
#pragma once
#include <string>
#include <vector>

#include "macro/econ_day.h"   // Depot + econ_pay_debt — приход гасит долг (S10)
#include "tables/faction.h"
#include "macro/anketa.h"

namespace sm {

// ── The package deal: two bundles swap whole or not at all ───────────────

// One staged line of a deal: the SLOT of the source shelf, not an id — a
// rolled sword and its bare twin are two stacks of one id, and an id-keyed
// line priced the plain while shipping whichever the bag found first (the
// exact class Inventory::remove_at was built against, owner verdict
// 2026-09-07 «торговля пер-стак»). `def` is the row the slot held when the
// line was staged — a shelf slot re-filled by the economy day under an open
// panel refuses the stale line instead of shipping strangers.
struct BarterLine {
    int           slot  = -1;
    int           count = 0;
    std::uint16_t def   = 0;
};
using BarterPackage = std::vector<BarterLine>;

// The GENERAL settlement of a barter (owner ruling 2026-08-07): the trade
// screens stage a package on EACH side and ONE button settles both, all-or-
// nothing — lines are checked against the PRE-DEAL bags, then everything
// travels. What travels is the slot's WHOLE identity (seed, affixes,
// material, quality) — add_ref re-stacks by the one stacking law, so a
// rolled instance arrives as itself and never merges into a plain pile.
// Coin is not special here: a currency row is just another line, which is
// how a deal balances. VALUE fairness (given covers taken) is the caller's
// law — the screens price both sides and gate the button.
inline bool barter_swap(Inventory& a, Inventory& b,
                        const BarterPackage& fromA,
                        const BarterPackage& fromB) {
    const auto covered = [](const Inventory& bag, const BarterPackage& pkg) {
        for (const BarterLine& line : pkg) {
            if (line.count <= 0) return false;
            if (line.slot < 0 || line.slot >= kMaxInventorySlots) return false;
            const ItemRef& s = bag.slots[std::size_t(line.slot)];
            if (s.empty() || s.def != line.def) return false;
            int need = 0;
            for (const BarterLine& l : pkg)
                if (l.slot == line.slot) need += l.count;
            if (s.count < need) return false;
        }
        return true;
    };
    if (!covered(a, fromA) || !covered(b, fromB)) return false;
    // Settle on COPIES and commit whole: `add_ref` can refuse a full bag
    // mid-deal, and a half-settled swap would burn the goods already
    // removed. All-or-nothing stays literal — a refused deal leaves both
    // pre-deal bags untouched (CANON S5: nothing evaporates).
    Inventory na = a, nb = b;
    const auto ship = [](Inventory& from, Inventory& to,
                         const BarterPackage& pkg) {
        for (const BarterLine& line : pkg) {
            ItemRef payload = from.slots[std::size_t(line.slot)];
            payload.count = line.count;
            if (!from.remove_at(line.slot, line.count)) return false;
            if (!to.add_ref(payload)) return false;
        }
        return true;
    };
    if (!ship(na, nb, fromA) || !ship(nb, na, fromB)) return false;
    a = na;
    b = nb;
    return true;
}

// ── Payment by value — the ONE law of every debit ────────────────────────

// ── ЧТО АВТОМАТИЧЕСКИЙ ПЛАТЁЖ ВПРАВЕ ОТГРУЗИТЬ — ОДИН ПРЕДИКАТ НА ДВУХ
// ЧИТАТЕЛЕЙ (M-292, 2026-10-20) ──────────────────────────────────────────
// ПРИЧИНА — ДВЕРЬ ПАСТВЫ, А НЕ ОТСУТСТВИЕ МАССЫ. Душа, покинувшая контейнер
// места, обязана покинуть и его паству, и носителей у этого события ДВА:
// число фичи и голова инвентаря (ЗАКОН КОНТЕЙНЕРА п.3). Писать их порознь
// запрещено, поэтому душа переходит только НАЗВАННОЙ дверью — найм
// (`hire_npc`), прилавок, смерть (`kill_fallen_squad_creatures`), — и каждая
// из них паству пишет. У платежа по плотности стоимости такой двери нет и
// быть не может: он зовётся из пятнадцати мест живой экономики (десятина,
// жалованье, откуп от засады, сделка каравана), и ни одно из них не знает,
// чей дом считает проданного. Значит он торгует ИМУЩЕСТВОМ, а душу не носит.
//
// ДО M-292 ЭТО УСЛОВИЕ БЫЛО НЕЯВНЫМ, И НА НЁМ СТОЯЛ ВЕСЬ ПЛАТЁЖ МИРА:
// `value_of` на строке существа отдавала НОЛЬ (она резолвила только
// предметный каталог), и `v <= 0` ниже отсекал души сам. Как только дверь
// стоимости стала отвечать за обе половины, ноль отсекать перестал — а
// колонку массы у строки существа платёжный закон не находит, то есть
// `w == 0`, что в законе плотности читается как «бесконечно плотен и
// ВЫИГРЫВАЕТ ВСЕГДА». Первый же платёж мира отгружал бы крестьян ПЕРЕД
// монетой и мимо двери паствы: место продолжало бы кормить и облагать данью
// проданного — ровно тот дефект, который у найма уже чинили. Поэтому условие
// названо вслух и ровно одно на обоих читателей: разъехаться им больше нечем.
//
// СИММЕТРИЯ КОЛОНОК НАЗВАНА ОТДЕЛЬНЫМ НАРЯДОМ, И ЭТО ВЕРДИКТ ВЛАДЕЛЬЦА
// 2026-10-20 (дословно: «просто есть же предмет телега как тогда оон
// увеличивает грузопереносимость - тогда нужна коолнка и масса и грузоперенос
// … у существ и у предметов есть и масса и грузоперенос в табликце 0 это норм
// дефолт»). Он прав, и дыра настоящая: сегодня `ItemDef` несёт массу без
// грузопереноса, а `NpcTypeDef` — грузоперенос (`haulMult`) без массы, то
// есть таблицы половинчаты обе, и телега это обнажает. Но предикат ниже от
// той правки НЕ ЗАВИСИТ: он про дверь паствы, а не про взвешивание, и
// переживёт обе колонки.
inline const ItemDef* payable_row(const ItemRef& s) noexcept {
    return s.empty() ? nullptr : item_def_at(int(s.def));
}

// The densest stack of the bag: the slot the value law reaches for first.
// Плотнее — раньше: v/w больше ⇔ v·bestW > bestV·w (перекрёстно, без
// деления; вес 0 бесконечно плотен и выигрывает всегда). -1 = nothing of
// value left to pay with.
inline int densest_value_slot(const Inventory& inv, int& outUnitValue) {
    int best = -1;
    int bestV = 0;
    float bestW = 0.0f;
    for (int i = 0; i < int(inv.slots.size()); ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        const ItemDef* def = payable_row(s);
        if (!def) continue;       // нечем взвесить — не платёжная материя
        const int v = value_of(s);
        if (v <= 0) continue;
        const float w = def->weight > 0.0f ? def->weight : 0.0f;
        const bool denser = best < 0
            || float(v) * bestW > float(bestV) * w
            || (float(v) * bestW == float(bestV) * w && v > bestV);
        if (denser) { best = i; bestV = v; bestW = w; }
    }
    outUnitValue = bestV;
    return best;
}

// ОПЛАТА СТОИМОСТЬЮ В СТОК (владелец, 2026-09-18, дословно: «оплата может
// списываться по единой системе стоимости — натурой, по механике бартера…
// и в чём особенны монеты? никакого хардкода: у них минимальный вес при
// макс стоимости, поэтому дефолт — списание в монетах, но если монет нет,
// списывается что-то другое, что выгодно по весу»). Никакого списка валют:
// стаки уходят в порядке ПЛОТНОСТИ ЦЕННОСТИ (value/kg), и монета выигрывает
// арифметикой веса, не веткой. Последняя единица может переплатить —
// бартерный закон: отданное покрывает взятое, излишек — щедрость
// плательщика. Возвращает уплаченную стоимость; на тощем контейнере платит
// сколько есть — судья «покрыто/не покрыто» остаётся у вызывающего
// (окно сравнивает inventory_value ДО платежа).
inline int pay_value_dense(Inventory& inv, int value) {
    int left = value < 0 ? 0 : value;
    int paid = 0;
    while (left > 0) {
        int unitV = 0;
        const int best = densest_value_slot(inv, unitV);
        if (best < 0) break;   // платить больше нечем
        const ItemRef& s = inv.slots[std::size_t(best)];
        const int want = (left + unitV - 1) / unitV;
        const int take = want < int(s.count) ? want : int(s.count);
        if (take <= 0 || !inv.remove_at(best, take)) break;
        paid += take * unitV;
        left -= take * unitV;
    }
    return paid;
}

// ОПЛАТА СТОИМОСТЬЮ КОНТРАГЕНТУ — the same density law with a receiver: the
// stacks TRAVEL (whole identity, add_ref), nothing is minted and nothing is
// burned. Credit-before-debit per stack: what the receiver's full bag
// refuses simply STAYS with the payer (CANON S5). Returns the value actually
// moved — callers compare it against `value` to know the deal settled whole.
// Приёмник — Depot (CANON S10): платёж, упавший в МЕСТО, гасит его долг
// СРАЗУ — город, взявший хлебом, хлеб тут же проел. Сумки проходят как
// раньше (неявная конверсия, долга нет).
inline int transfer_value_dense(Inventory& from, Depot to, int value) {
    int left = value < 0 ? 0 : value;
    int moved = 0;
    while (left > 0) {
        int unitV = 0;
        const int best = densest_value_slot(from, unitV);
        if (best < 0) break;
        const ItemRef& s = from.slots[std::size_t(best)];
        const int want = (left + unitV - 1) / unitV;
        const int take = want < int(s.count) ? want : int(s.count);
        if (take <= 0) break;
        ItemRef payload = s;
        payload.count = std::uint16_t(take);
        if (!to.inv.add_ref(payload)) break;   // refused: the stack stays put
        if (!from.remove_at(best, take)) break;
        moved += take * unitV;
        left -= take * unitV;
    }
    if (moved > 0 && to.needDebt) {
        econ_pay_debt(to.inv, to.needDebt, to.sink, to.user);
    }
    return moved;
}

// ── ВЫДАЧИ МОНЕТ ИЗ ВОЗДУХА ЗДЕСЬ БОЛЬШЕ НЕТ (M-139, вердикт владельца
// 2026-09-26, дословно: «ВЫДАЧА МОНЕТ СНЕМСТИ … не нужно никакого костыль
// гольд выдачи платят тем что эквиваельнто по сстоимости в миневантаре»).
// Здесь стояла `add_value_in_coins` — второй путь появления СТОИМОСТИ рядом
// с чеканкой: она печатала номиналы фракции прямо в сумку, и ею жили посев
// казны при рождении места, кошелёк NPC при спавне, золото трупа, награда
// без дарителя и четыре выдачи игроку. Деньги входят в мир ОДНИМ путём —
// чеканкой рецептом `coin_*` из серебра (`items.cpp`), — а платят по трём
// столпам: абсолютная стоимость + локальная цена + стоимость/масса, то есть
// `densest_value_slot` выше. Открытые концы (пустая казна новорождённого
// места, пустой кошелёк NPC, награда без дарителя) названы в
// `macro-registry.md` M-139 и ждут ПУЛА ЛУТА; времянки на их месте
// запрещены (AGENTS §1).

// VALUE of the COIN rows of a bag — a census over the faction registry's
// mint columns (deduped: culture groups fold onto their realm's family).
// This is ANALYTICS for assessments that are defined over coin — the tithe's
// coin half averages it so it never double-counts the goods half it stands
// beside. No payment law branches on it: debits go by density, always.
inline int coin_census_value(const Inventory& inv) {
    struct CoinRow { int idx; int value; };
    static const std::vector<CoinRow> rows = [] {
        std::vector<CoinRow> r;
        for (const FactionDef& f : kFactionDefs) {
            for (const char* c : f.mint) {
                if (!c || !c[0]) continue;
                const int idx = item_index(c);
                if (idx < 0) continue;
                bool seen = false;
                for (const CoinRow& e : r) seen = seen || e.idx == idx;
                if (seen) continue;
                const ItemDef* def = item_def_at(idx);
                r.push_back({idx, def && def->value > 0 ? def->value : 1});
            }
        }
        return r;
    }();
    int total = 0;
    for (const CoinRow& e : rows) total += inv.count_of(e.idx) * e.value;
    return total;
}

// The whole bag in universal VALUE — what every formula converts to
// (owner: «внутри всё равно учитывается стоимость»). Per stack through THE
// contextual price (value_of — row + affixes; material/quality wake here
// with their tables): a bandit hauling a rolled blade is worth hunting for
// exactly what the blade would fetch, not for its bare row.
//
// ЧТО ИМЕННО ОНА СЧИТАЕТ — СКАЗАНО ВСЛУХ С M-292 (2026-10-20), И СМЫСЛ ЕЙ
// СОХРАНЁН, А НЕ ИЗМЕНЁН. Пятнадцать её читателей спрашивают «сколько
// контейнер может ОТДАТЬ», и каждый из них немедленно платит дверью
// плотности: десятина (`world_tick.cpp` — `owedValue += avg >> 3`), гашение
// жалованья (`upkeep_window.h` — `wageDebt -= canPay` ПОСЛЕ
// `pay_value_dense`), кошелёк найма, откуп от засады, «нечего продать» у
// корована, `max_affordable_lot_` в четырёх местах. До M-292 это держалось
// немотой `value_of` на строке существа; теперь держится ОДНИМ предикатом с
// платёжной дверью (`payable_row` выше), и разъехаться им нечем.
//
// ЕСЛИ БЫ ОНА ПОСЧИТАЛА ДУШИ, СЛОМАЛОСЬ БЫ НЕ ОТОБРАЖЕНИЕ, А МИР: город с
// 6272 крестьянами даёт +188 160, то есть вассалу начислялась бы неоплатная
// по построению дань, а жалованье ГАСИЛОСЬ БЫ БЕЗ ОПЛАТЫ — долг из воздуха.
// Вопрос «сколько стоит контейнер ЦЕЛИКОМ, с душами» — другой вопрос, и у
// него своя дверь ниже.
//
// СЧИТАЕТСЯ СВОДКОЙ, А НЕ ПРОХОДОМ (M-296): колонка `valueItems` и есть эта
// сумма, и слагаемое у неё ТО ЖЕ — `slot_tally` кладёт в неё ровно строки,
// которые резолвит предметный каталог, то есть ровно те, что пропускал
// `payable_row`. Предикат остался жив над ОДНИМ слотом (`densest_value_slot`
// выше), и двум читателям по-прежнему нечем разъехаться.
inline int inventory_value(const Inventory& inv) {
    return int(inv.sum().valueItems);
}

// ── СКОЛЬКО СТОИТ КОНТЕЙНЕР ЦЕЛИКОМ — ОБЕ ПОЛОВИНЫ ОДНИМ ПРОХОДОМ
// (M-292, 2026-10-20) ────────────────────────────────────────────────────
// Это ВТОРОЙ ВОПРОС, а не второй ответ (DOD п.6): «что он может отдать»
// спрашивает платёж, «сколько он стоит весь» — оценщик. Разница ровно в
// душах, и она законна: крестьянин города есть ценность мира, но не монета
// в его кошельке.
//
// ДВЕРЬ УБИВАЕТ РУЧНОЕ СЛОЖЕНИЕ ПОЛОВИН, КОТОРОЕ УЖЕ СУЩЕСТВОВАЛО. Запах
// богатства считал `creatures_worth(…) + inventory_value(…)` —
// `creatures_worth@src/macro/npc_ai.cpp` была написана ровно потому, что
// дверь стоимости была полуслепа, и несла свою половину САМА, причём мимо
// закона уровня (она читала `hireGold` без `soldier_level_factor`). Теперь
// половина одна, и закон уровня у неё тот же, что у найма.
//
// ЧИТАТЕЛИ НАЗВАНЫ (DOD п.9): запах богатства (`scent_squad_deposit`),
// «Their value» прилавка и «Carries (value)» обзора субъекта — все три
// отвечают на «сколько тут всего добра», и душа в этом счёте законна.
//
// И ДВЕ ПОЛОВИНЫ СВОДКИ ЗДЕСЬ СКЛАДЫВАЮТСЯ ОБРАТНО (M-296) — это не возврат
// снесённого ручного сложения, а его противоположность: там складывались
// два РАЗНЫХ закона цены (`creatures_worth` читала `hireGold` мимо закона
// уровня), здесь — два слагаемых ОДНОЙ `slot_tally`, разложенные по
// половинам ровно затем, чтобы платёж мог взять только свою.
inline int container_worth(const Inventory& inv) {
    return int(inv.sum().valueItems + inv.sum().valueSouls);
}

} // namespace sm
