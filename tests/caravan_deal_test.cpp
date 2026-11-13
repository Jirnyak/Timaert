// Сделка сквада на рынке — МОДУЛЬ КОРОВАНА НАД ЯДРОМ БАРТЕРА (единая
// ценомодель, CANON S25: «ЕДИНАЯ ЦЕНОМОДЕЛЬ» 2026-11-12 и «ВЕРДИКТ 2026-11-13
// — ФОРМА ЦЕНЫ»). Рынок здесь СТОЯЩИЙ, купец ПРИШЕДШИЙ: взятое у рынка стоит
// его оценку плюс его долю от неё, отданное ему он засчитывает по своей
// оценке. Цену, лот и встречную корзину называет ядро (`src/macro/economy.h`,
// его свидетели стоят под компилятором у дверей); модуль решает одно — ОТДАТЬ
// всё, что рынок ценит выше купца больше чем на свою наценку, и ВЗЯТЬ всё,
// что купец ценит выше цены взятия.
// Здесь утверждается то, что видно только на настоящих складах, счетах,
// листах и спинах:
//
//   1. СОХРАНЕНИЕ — монета и всякий товар только переходят из рук в руки
//      (еда — сквозь счёт: проданное в нужду съедается в её уплату).
//   2. В НУЖДУ РЫНКА КУПЕЦ ОТДАЁТ ГРУЗ — всё, что рынок ценит выше него
//      больше чем на свою наценку, и ни единицы сверх; цену называет оценка
//      рынка, и прибыль купца по его собственной мерке положительна.
//   3. БЕЗ ВЫГОДЫ НЕ ОТДАЁТСЯ НИЧЕГО — ни повторно, ни рынку без нужды; а с
//      выгодой отдаётся и без нужды: груз течёт из забитой спины в свободную.
//   4. ЗАВАЛ — излишек, забивший малую спину рынка, купец со свободной спиной
//      берёт дешевле стоимости и не больше, чем помещает спина; лёгкий против
//      спины излишек выгоды не несёт и остаётся.
//   5. ПОКУПАТЕЛЬНАЯ СПОСОБНОСТЬ — рынок, которому нечем заплатить, ничего не
//      получает и ничего не теряет; тонкая казна режет лот.
//   6. ВСТРЕЧНАЯ КОРЗИНА: только то, что берущий ценит не ниже плательщика,
//      и не больше, чем плательщик сам ценит взятое.
//   7. КРУГ ЧЕРЕЗ НАСТОЯЩИЙ РЫНОК УБЫТОЧЕН при предельном перевесе сил;
//      оценка строки зависит только от самой строки (счёт на инструмент не
//      поднимает оценку железа).
//   7в. ЗЕРНИСТОСТЬ И МАЛАЯ РАЗНИЦА — завал меньше шага оценки мастер берёт
//      лотом, а купец равной с рынком силы не берёт вовсе; 7г — спину
//      занимает то, что выгоднее на грамм.
//   8. ЛИСТ — сильная анкета получает за тот же лот больше монеты, слабая
//      меньше; за оценку рынка цену не выводит никто, ниже половины её не
//      опускает ничто.
//
// ГРАНИЦЫ ЦЕНЫ ЧИТАЮТСЯ У ДВЕРЕЙ ЯДРА, ФОРМУЛА ЗДЕСЬ НЕ ПЕРЕСЧИТЫВАЕТСЯ:
// строка стороны — `side_row@src/macro/deal.h` (тот же мост, каким её читает
// сделка), «ценит ли выше» — `gain_units` (ТОЧНО: округлённые до кванта
// единицы двух сторон сравнивать нельзя, M-490), сумма оценок лота —
// `lot_loss`/`lot_take` (§8 п.5). Прибыль купца (`CaravanDeal::gain`)
// держит ЯКОРЬ, а не пересчёт: монета меди стоит единицу у всякой стороны, а
// излишек без нужды купец ценит не ниже пола и не выше стоимости (свидетели
// ядра под компилятором) — значит прибыль зажата между полученной монетой
// минус стоимость отданного и ею же минус пол отданного.
//
// СИЛА И СПИНА СТОРОНЫ — ИЗ ЕЁ АНКЕТЫ, И ФИКСТУРА ИХ НАЗЫВАЕТ: сила — листом
// (харизма и ранг торговли; читает дверь `side_trade_power`), спина ходока —
// колонкой анкеты `carryPerSoulG` (читает `carry_capacity_g`). Обе
// утверждаются предусловием до сделки (§8 п.11): ходок без живого слота
// отвечал бы дефолтным листом, и сила фикстуры была бы невидима.
#include <climits>
#include <cstdio>
#include <memory>

#include "check.h"
#include "bus/anketa.h"
#include "bus/store.h"
#include "bus/world_row.h"          // count_human_souls — головы места
#include "macro/birth_squad.h"      // birth_squad — место родится ТЕЛОМ
#include "macro/currency.h"         // coin_census_value / inventory_value
#include "macro/deal.h"             // side_row / side_trade_power
#include "macro/economy.h"          // ядро: оценки и суммы оценок лота
#include "macro/npc_ai.h"           // trade_caravan_at_station / CaravanDeal
#include "macro/squad.h"            // carry_capacity_g — спина стороны
#include "macro/state.h"
#include "tables/commodity.h"
#include "tables/items.h"

int main() {
    using sm::Standing;
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& mstore = *storePtr;
    std::unique_ptr<sm::GameState> gsOwn = sm::make_game_state();
    sm::GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;

    const auto shelf = [&](std::uint16_t slot) -> sm::Inventory& {
        return mstore.inventory[slot].inv;
    };
    const auto debt = [&](std::uint16_t slot) -> std::int32_t* {
        return mstore.upkeep[slot].needDebt;
    };
    // РЫНОК — ГОРОД, РОЖДЁННЫЙ ДВЕРЬЮ МИРА: анкета рода, головы жителей в
    // контейнере (их спина — спина рынка), склад и счёт — колонки тела.
    const auto market = [&](int souls) {
        const std::uint16_t m =
            sm::birth_squad(gs, nullptr, mstore, sm::SquadType::City, 0, 0)
                .slot;
        CHECK(sm::raise_flock_into_container(shelf(m), souls) == souls,
              "фикстура: жители рынка встали в его контейнер");
        return m;
    };
    // ХОДОК — ЖИВОЙ СЛОТ СТОРА: его лист читает сила, колонка анкеты — спина.
    // Нужды у ходока нет (счёт нулями, домашних душ нет), и ядро ведёт его
    // оценку одной массой против спины.
    const auto trader = [&](int cha, int tradeRank, std::int64_t backG) {
        const sm::MacroHandle h = sm::store_birth(mstore);
        CHECK(mstore.valid(h), "фикстура: слот ходока рождён");
        mstore.sheet[h.slot].attributes[sm::AttributeId::Cha] =
            std::uint8_t(cha);
        mstore.sheet[h.slot].skills[sm::SkillId::Trade] =
            std::uint8_t(tradeRank);
        mstore.runtime[h.slot].carryPerSoulG = std::int32_t(backG);
        return h.slot;
    };
    const auto back = [&](std::uint16_t slot) {
        return sm::carry_capacity_g(mstore.runtime[slot], shelf(slot));
    };
    const auto power = [&](std::uint16_t slot) {
        return sm::side_trade_power(mstore, slot);
    };
    const auto row = [&](std::uint16_t slot, int idx) {
        return sm::side_row(mstore, slot, idx, sm::item_def_at(idx)->value);
    };
    // «Сторона `to` ценит k-ю единицу, едущую к ней от `from`, строго выше
    // (либо не ниже) отдающего» — точный ответ ядра, а не сравнение двух
    // округлённых единиц.
    const auto values_higher = [](const sm::RowSide& to, const sm::RowSide& from,
                                  std::int64_t k = 1) {
        return sm::gain_units(from, to, k, /*strict=*/true, 0, 1) == k;
    };
    const auto values_not_lower = [](const sm::RowSide& to,
                                     const sm::RowSide& from) {
        return sm::gain_units(from, to, 1, /*strict=*/false, 0, 1) == 1;
    };
    // «ЕДЕТ ЛИ»: k-ю единицу берущий ценит больше чем в (1 + доля стоящего)
    // раз выше отдающего — столько единиц и назовёт ядро. Силы — стоящего и
    // пришедшего.
    const auto worth_moving = [](const sm::RowSide& to, const sm::RowSide& from,
                                 int standingPower, int visitorPower,
                                 std::int64_t k = 1) {
        return sm::gain_units(from, to, k, /*strict=*/true, standingPower,
                              visitorPower) == k;
    };
    // ОЦЕНКА ВСЕГО ИНВЕНТАРЯ СТОРОНЫ — сумма оценок запаса по всем строкам
    // каталога: то самое число, которое растит сквад.
    const auto worth_of = [&](std::uint16_t slot) {
        std::int64_t sum = 0;
        for (int idx = 0; sm::item_def_at(idx) != nullptr; ++idx) {
            const sm::RowSide r = row(slot, idx);
            sum += sm::stock_worth(r, r.net);
        }
        return sum;
    };
    // Сделка — той же спиной, какой её зовёт рейс (`carry_capacity_g`).
    // КАЖДАЯ сделка этого файла судится двумя законами, мерой которых служит
    // оценка инвентаря, а не отчёт сделки: стоящий (рынок) не теряет, а
    // пришедший, если что-то уехало, свою оценку растит. (Свидетели круга под
    // компилятором — тождество одной формулы; насос через настоящие двери
    // сделки они не видят — ревью дифа 2026-11-13.)
    int dealsJudged = 0, dealsMoved = 0, standingLost = 0, visitorIdle = 0;
    const auto trade = [&](std::uint16_t t, std::uint16_t m) {
        const std::int64_t marketWorth = worth_of(m), traderWorth = worth_of(t);
        const sm::CaravanDeal d =
            sm::trade_caravan_at_station(mstore, t, back(t), m);
        ++dealsJudged;
        if (worth_of(m) < marketWorth) ++standingLost;
        if (d.movedTableValue > 0) {
            ++dealsMoved;
            if (worth_of(t) <= traderWorth) ++visitorIdle;
        }
        return d;
    };
    const auto coins = [&](std::uint16_t a, std::uint16_t b) {
        return (long long)sm::coin_census_value(shelf(a))
             + sm::coin_census_value(shelf(b));
    };

    const int foodIdx = sm::item_index("food");
    const int stoneIdx = sm::item_index("stone");
    const int ironIdx = sm::item_index("iron");
    const int coinIdx = sm::item_index("coin_empire_copper");
    const sm::ItemDef* foodDef = sm::item_def_at(foodIdx);
    const sm::ItemDef* stoneDef = sm::item_def_at(stoneIdx);
    const sm::ItemDef* ironDef = sm::item_def_at(ironIdx);
    const int foodBase = foodDef->value;
    const int stoneBase = stoneDef->value;
    const std::int64_t foodG = sm::item_mass_g(*foodDef);
    const std::int64_t stoneG = sm::item_mass_g(*stoneDef);
    const std::int64_t coinG = sm::item_mass_g(*sm::item_def_at(coinIdx));
    const int foodCol = sm::commodity_index("food");

    // ── 1–2. СТАНЦИЯ: ГРУЗ В НУЖДУ, СОХРАНЕНИЕ, ГРАНИЦЫ ЦЕНЫ, ПОЛНОТА ────
    // Город голоден СЧЁТОМ (CANON S10): непогашенный счёт и есть его нужда.
    // СЧЁТ — МАЛЫЙ НАРОЧНО, А НЕ СЕЗОННЫЙ: крутизна нужды открыта рядом M-485
    // (сезонный счёт 64 жителей — тысячи единиц, и первая из них стоит у
    // рынка тысячи стоимостей), и на сезонном счёте лот резала бы не оценка, а
    // спина купца под монетой оплаты — центнеры меди за килограмм еды. Казна
    // покрывает ВСЮ нужду по наивысшей оценке самого рынка (сумма его оценок —
    // дверь `lot_take`), поэтому лот режет закон, а не кошелёк. Анкеты РАВНЫ:
    // лист купца списан с листа рынка, и выгода делится пополам.
    constexpr int kBill = 64;
    const std::uint16_t city = market(64);
    debt(city)[foodCol] = kBill;
    const std::int64_t cityCeiling = sm::lot_take(row(city, foodIdx), kBill);
    CHECK(cityCeiling > std::int64_t(kBill) * foodBase
              && cityCeiling < INT_MAX / 4,
          "предусловие: нужда поднимает оценку рынка выше стоимости, и вся "
          "она ложится в счёт монет");
    CHECK(shelf(city).add_of(coinIdx, int(cityCeiling)),
          "фикстура: казна рынка покрывает всю нужду");
    const int cargo = kBill + 64;   // груза больше, чем нужды
    // Спина купца — десять его грузов: своя полка ему не завал, а монета
    // оплаты (масса лота, оценённого рынком) влезает с запасом.
    const std::int64_t bigBackG = std::int64_t(cargo) * foodG * 10;
    const std::uint16_t t1 = trader(
        mstore.sheet[city].attributes.of(sm::AttributeId::Cha),
        mstore.sheet[city].skills.of(sm::SkillId::Trade), bigBackG);
    CHECK(shelf(t1).add_of(foodIdx, cargo), "фикстура: трюм купца");
    CHECK(back(t1) == bigBackG,
          "предусловие: спина купца — ровно колонка его анкеты (груз спины "
          "не добавляет)");
    CHECK(cityCeiling * coinG < bigBackG - std::int64_t(cargo) * foodG,
          "предусловие: монета всей оплаты помещается в свободную спину");
    CHECK(power(t1) == power(city) && power(t1) > 0,
          "предусловие: анкеты равны — сила купца равна силе рынка");
    std::printf("  [фикстура] спина рынка %lld г, сила %d, потолок нужды %lld\n",
                (long long)back(city), power(city), (long long)cityCeiling);

    const sm::RowSide mBefore = row(city, foodIdx);
    const sm::RowSide tBefore = row(t1, foodIdx);
    const long long coinBefore = coins(t1, city);
    const long long purseBefore = sm::coin_census_value(shelf(t1));
    const int foodDebtBefore = debt(city)[foodCol];
    const sm::CaravanDeal d1 = trade(t1, city);
    const int sold1 = cargo - shelf(t1).count_of(foodIdx);
    const int paidDebt = foodDebtBefore - debt(city)[foodCol];
    // Сколько монеты купец получил — счётом из кошелька, а не из отчёта сделки.
    const long long coinGot = sm::coin_census_value(shelf(t1)) - purseBefore;

    CHECK(coins(t1, city) == coinBefore, "СОХРАНЕНИЕ: монета не чеканится");
    CHECK(shelf(t1).count_of(foodIdx) + shelf(city).count_of(foodIdx)
                  + paidDebt == cargo
              && paidDebt == sold1 && sold1 <= kBill,
          "СОХРАНЕНИЕ: еда либо в трюме, либо на полке, либо съедена в уплату "
          "счёта — съедено ровно проданное, и не больше счёта");
    // Излишек купца (десятая доля его спины) к свободной полке рынка НЕ течёт:
    // силы равны, наценка рынка — полтора раза, а оценки расходятся меньше.
    // На малой разнице решает скилл (замысел владельца 2026-11-13); мастер
    // такой же завал везёт — блок 7в.
    CHECK(shelf(city).count_of(foodIdx) == 0,
          "НА МАЛОЙ РАЗНИЦЕ РАВНЫЙ НЕ ТОРГУЕТ: сверх нужды рынка купец не отдал "
          "ни единицы");
    CHECK(sold1 > 0 && d1.soldValue > 0 && d1.movedTableValue > 0,
          "В НУЖДУ: купец отдал груз голодному рынку");
    CHECK(d1.soldValue <= sm::lot_take(mBefore, sold1)
              && d1.soldValue
                     == sm::ask_price(coinGot, power(city), power(t1)),
          "ЦЕНУ НАЗЫВАЕТ ОЦЕНКА СТОЯЩЕГО: рынок отдал монету по цене взятия — "
          "её число плюс его доля от неё — и не больше, чем засчитал груз");
    CHECK(coinGot < d1.soldValue && coinGot > sm::lot_loss(tBefore, sold1),
          "негативный контроль: доля рынка не ноль — купец получил монетой "
          "меньше засчитанного, — и купец в плюсе: получил больше, чем ценил "
          "отданное");
    CHECK(d1.gain > 0
              && d1.gain >= coinGot - (long long)sold1 * foodBase
              && d1.gain <= coinGot - (long long)sold1,
          "ПРИБЫЛЬ КУПЦА ПОЛОЖИТЕЛЬНА и зажата якорем: полученная монета минус "
          "отданный излишек, оценённый не выше стоимости и не ниже пола");
    // ПОЛНОТА — по тем же оценкам до сделки: последняя отданная единица была
    // выгодна (рынок ценил её строго выше купца), следующая — уже нет.
    CHECK(shelf(t1).count_of(foodIdx) > 0
              && sm::coin_census_value(shelf(city)) > 0,
          "предусловие полноты: лот не упёрся ни в трюм, ни в казну");
    // «Ценит выше» спрашивается у ядра ТОЧНО: округлённые до кванта единицы
    // сравнивать нельзя — они расходятся с точной оценкой на квант (M-490).
    CHECK(worth_moving(mBefore, tBefore, power(city), power(t1), sold1),
          "НИ ЕДИНИЦЫ СВЕРХ ВЫГОДЫ: последнюю отданную рынок ценил выше купца "
          "больше чем на свою наценку");
    CHECK(!worth_moving(mBefore, tBefore, power(city), power(t1), sold1 + 1),
          "ВСЁ, ЧТО ВЫГОДНО: следующую единицу рынок так высоко уже не ценил");
    std::printf("  [станция] продано %d из %d, засчитано %d, монеты %lld, "
                "прибыль %lld, остаток счёта %d\n",
                sold1, cargo, d1.soldValue, coinGot, (long long)d1.gain,
                debt(city)[foodCol]);

    // ── 3. БЕЗ ВЫГОДЫ НЕ ОТДАЁТСЯ НИЧЕГО; С ВЫГОДОЙ — И БЕЗ НУЖДЫ ─────────
    {
        // 3а. Второй визит к тому же рынку: делить больше нечего.
        CHECK(!worth_moving(row(city, foodIdx), row(t1, foodIdx), power(city),
                            power(t1)),
              "предусловие: после сделки рынок не ценит еду выше купца больше "
              "чем на свою наценку");
        const int holdBefore = shelf(t1).count_of(foodIdx);
        const sm::CaravanDeal again = trade(t1, city);
        CHECK(again.soldValue == 0 && again.movedTableValue == 0
                  && again.gain == 0
                  && shelf(t1).count_of(foodIdx) == holdBefore,
              "ВТОРОЙ ВИЗИТ ПУСТ: рынку, который не ценит груз выше купца, не "
              "отдаётся ни единицы — даже от купца с полным трюмом");

        // 3б. Рынок БЕЗ НУЖДЫ, и еда занимает у него ту же долю спины, что у
        // купца: оценки сошлись, выгоды нет ни в одну сторону. (Пустой рынок
        // сюда не годится: завал купца, даже малый, течёт к свободной спине —
        // это 3в и блок 1.)
        const std::uint16_t sated = market(64);
        CHECK(shelf(sated).add_of(coinIdx, 600000), "фикстура: казна");
        const std::uint16_t free = trader(13, 0, bigBackG);
        CHECK(shelf(free).add_of(foodIdx, 100), "фикстура: трюм купца");
        const std::int64_t marketFits = back(sated) / foodG;
        const std::int64_t traderFits = back(free) / foodG;
        const int level =
            int((100 * marketFits + traderFits - 1) / traderFits);
        CHECK(shelf(sated).add_of(foodIdx, level),
              "фикстура: полка рынка — та же доля спины, что трюм у купца");
        const sm::RowSide sRow = row(sated, foodIdx);
        CHECK(sRow.net >= 0,
              "предусловие: у рынка нет нужды в еде");
        CHECK(!values_higher(sRow, row(free, foodIdx))
                  && !values_higher(row(free, foodIdx), sRow),
              "предусловие: оценки сошлись — ни одна сторона не ценит единицу "
              "выше другой");
        const sm::CaravanDeal none = trade(free, sated);
        CHECK(none.soldValue == 0 && none.boughtValue == 0 && none.gain == 0
                  && shelf(free).count_of(foodIdx) == 100
                  && shelf(sated).count_of(foodIdx) == level,
              "БЕЗ НУЖДЫ И БЕЗ ВЫГОДЫ НЕ ЕДЕТ НИЧЕГО — ни туда, ни обратно");

        // 3в. ПРОТИВОПОЛОЖНАЯ ПОЛЯРНОСТЬ: тот же рынок без нужды, но груз
        // забил спину купца — купец ценит его ниже рынка и отдаёт.
        const std::uint16_t open = market(64);
        CHECK(shelf(open).add_of(coinIdx, 600000), "фикстура: казна");
        const std::uint16_t jam = trader(13, 0, 100 * foodG / 2);
        CHECK(shelf(jam).add_of(foodIdx, 100), "фикстура: трюм купца");
        CHECK(back(jam) < 100 * foodG,
              "предусловие: груз тяжелее спины купца — завал в своём трюме");
        const sm::RowSide oRow = row(open, foodIdx);
        const sm::RowSide jRow = row(jam, foodIdx);
        CHECK(oRow.net >= 0 && values_higher(oRow, jRow),
              "предусловие: у рынка нужды нет, но он ценит груз выше купца");
        const sm::CaravanDeal flow = trade(jam, open);
        const int flowed = 100 - shelf(jam).count_of(foodIdx);
        CHECK(flowed > 0 && flow.gain > 0
                  && flow.soldValue >= sm::lot_loss(jRow, flowed)
                  && shelf(open).count_of(foodIdx) == flowed,
              "ЗАВАЛ ТЕЧЁТ К СВОБОДНОЙ СПИНЕ: без единой нужды купец отдал "
              "груз в плюс по своей мерке");
    }

    // ── 4. ЗАВАЛ РЫНКА: КУПЕЦ БЕРЁТ ДЕШЕВЛЕ СТОИМОСТИ, НЕ СВЕРХ СПИНЫ ─────
    {
        // 4а. Малый город (малая спина жителей) завален камнем — тяжёлой
        // строкой. У купца спина почти занята монетой: камня в неё влезает
        // ровно `fit` штук, и на каждой из них (и на следующей) купец ценит
        // камень выше рынка — значит остановить лот может только спина.
        const std::uint16_t small = market(4);
        constexpr int kFit = 10;
        // Излишек вдвое тяжелее спины рынка — и останется завалом, даже
        // когда купец заберёт свою долю.
        const int glut = kFit + int(2 * back(small) / stoneG) + 1;
        CHECK(shelf(small).add_of(stoneIdx, glut), "фикстура: завал камня");
        // Монета занимает девять десятых спины, камню остаётся десятая.
        const int purse = int(9 * kFit * stoneG / coinG);
        const std::uint16_t taker = trader(13, 0, 10 * kFit * stoneG);
        CHECK(shelf(taker).add_of(coinIdx, purse), "фикстура: кошелёк купца");
        const std::int64_t capBefore = back(taker);
        const int fit =
            int((capBefore - sm::inventory_mass_g(shelf(taker))) / stoneG);
        CHECK(capBefore == 10 * kFit * stoneG && fit == kFit,
              "предусловие: спина купца — колонка анкеты, и свободной её "
              "ровно на kFit камней");
        const sm::RowSide sBefore = row(small, stoneIdx);
        const sm::RowSide kBefore = row(taker, stoneIdx);
        CHECK(sm::worth_leaving(sBefore, 1) < stoneBase,
              "предусловие: излишек забил спину рынка — он ценит камень ниже "
              "стоимости");
        CHECK(values_higher(kBefore, sBefore, fit + 1),
              "предусловие: и за пределом спины купец ценил бы камень выше "
              "рынка — лот обязана остановить спина, а не оценка");
        const long long coinsBefore = coins(taker, small);
        const sm::CaravanDeal took = trade(taker, small);
        const int got = shelf(taker).count_of(stoneIdx);
        CHECK(got == fit && took.boughtValue > 0 && took.gain > 0,
              "ЗАВАЛ БЕРЁТСЯ: купец взял всё, что поместила спина, в плюс по "
              "своей мерке");
        CHECK(sm::inventory_mass_g(shelf(taker)) <= capBefore,
              "НЕ СВЕРХ СПИНЫ: трюм после сделки не тяжелее спины купца");
        CHECK(took.boughtValue < (long long)got * stoneBase,
              "ДЕШЕВЛЕ СТОИМОСТИ: завал отдан ниже абсолютной стоимости");
        CHECK(took.boughtValue >= sm::lot_loss(sBefore, got)
                  && took.boughtValue <= sm::lot_take(kBefore, got),
              "и всё же между оценками: не дешевле оценки рынка, не дороже "
              "оценки купца");
        CHECK(shelf(taker).count_of(stoneIdx) + shelf(small).count_of(stoneIdx)
                      == glut
                  && coins(taker, small) == coinsBefore,
              "СОХРАНЕНИЕ: камень и монета только сменили руки");

        // 4б. ПРОТИВОПОЛОЖНАЯ ПОЛЯРНОСТЬ: большой город, камня мало — излишек
        // не тяжёл против спины его жителей, и выгоды брать его нет.
        const std::uint16_t big = market(64);
        constexpr int kLight = 10;
        CHECK(shelf(big).add_of(stoneIdx, kLight), "фикстура: немного камня");
        const std::uint16_t idle = trader(13, 0, bigBackG);
        CHECK(shelf(idle).add_of(coinIdx, 5000), "фикстура: кошелёк купца");
        CHECK(back(big) > 20 * kLight * stoneG,
              "предусловие: излишек рынка лёгок против его спины (меньше "
              "двадцатой её доли)");
        // Точно купец ценит камень чуть выше рынка (у него камня нет вовсе),
        // но не выше цены взятия: наценка рынка съедает такую разницу.
        CHECK(values_higher(row(idle, stoneIdx), row(big, stoneIdx))
                  && !worth_moving(row(idle, stoneIdx), row(big, stoneIdx),
                                   power(big), power(idle)),
              "предусловие: купец ценит камень выше рынка, но меньше чем на "
              "его наценку");
        const sm::CaravanDeal skip = trade(idle, big);
        CHECK(skip.boughtValue == 0 && shelf(idle).count_of(stoneIdx) == 0
                  && shelf(big).count_of(stoneIdx) == kLight,
              "ЛЁГКИЙ ИЗЛИШЕК ОСТАЁТСЯ: выгоды нет — купец не берёт, хотя "
              "монета и спина у него есть");
    }

    // ── 5. ПОКУПАТЕЛЬНАЯ СПОСОБНОСТЬ: НЕЧЕМ — НИЧЕГО; ТОНКО — МЕНЬШЕ ──────
    {
        // 5а. Рынку нечем заплатить: монеты нет, платёжной материи нет.
        const std::uint16_t broke = market(64);
        debt(broke)[foodCol] = kBill;
        const std::uint16_t t = trader(13, 0, bigBackG);
        CHECK(shelf(t).add_of(foodIdx, cargo), "фикстура: трюм купца");
        CHECK(sm::inventory_value(shelf(broke)) == 0,
              "предусловие: у рынка нет ничего, чем можно платить");
        CHECK(values_higher(row(broke, foodIdx), row(t, foodIdx)),
              "предусловие: рынок голоден — отказ будет не от сытости");
        const long long massBefore = sm::inventory_mass_g(shelf(broke));
        const int headsBefore = sm::count_human_souls(shelf(broke));
        const sm::CaravanDeal none = trade(t, broke);
        CHECK(none.soldValue == 0 && none.boughtValue == 0 && none.gain == 0
                  && shelf(t).count_of(foodIdx) == cargo,
              "НЕЧЕМ ЗАПЛАТИТЬ — НЕТ СДЕЛКИ: груз остался у купца");
        CHECK(sm::inventory_value(shelf(broke)) == 0
                  && sm::inventory_mass_g(shelf(broke)) == massBefore
                  && sm::count_human_souls(shelf(broke)) == headsBefore
                  && debt(broke)[foodCol] == kBill,
              "И НИЧЕГО НЕ ОТНЯТО: склад, жители и счёт рынка нетронуты");

        // 5б. Тонкая казна: та же нужда, тот же купец, но монеты — четверть
        // того, что заплатил полный рынок станции.
        const int thin = int(coinGot / 4);
        const std::uint16_t poor = market(64);
        debt(poor)[foodCol] = kBill;
        CHECK(shelf(poor).add_of(coinIdx, thin), "фикстура: тонкая казна");
        const std::uint16_t tp = trader(
            mstore.sheet[city].attributes.of(sm::AttributeId::Cha),
            mstore.sheet[city].skills.of(sm::SkillId::Trade), bigBackG);
        CHECK(shelf(tp).add_of(foodIdx, cargo), "фикстура: трюм купца");
        CHECK(thin > 0 && power(tp) == power(t1) && back(tp) == back(t1)
                  && sm::inventory_value(shelf(poor)) == thin,
              "предусловие: купец тот же, рынок тот же, кроме казны, и у "
              "рынка нет иной платёжной материи");
        const long long before = coins(tp, poor);
        const sm::CaravanDeal cut = trade(tp, poor);
        const int soldPoor = cargo - shelf(tp).count_of(foodIdx);
        CHECK(soldPoor > 0 && soldPoor < sold1,
              "ТОНКАЯ КАЗНА РЕЖЕТ ЛОТ: продано меньше, чем полному рынку");
        const long long coinsPaid = thin - sm::coin_census_value(shelf(poor));
        CHECK(coinsPaid > 0 && coinsPaid <= thin
                  && cut.soldValue
                         == sm::ask_price(coinsPaid, power(poor), power(tp))
                  && coins(tp, poor) == before,
              "и уплачено монетой не больше, чем у рынка было, по цене взятия; "
              "монета сохранилась");
    }

    // ── 5в. ОПЛАТА УПЁРЛАСЬ В СПИНУ КУПЦА: ЛОТ НЕ ТАЕТ ────────────────────
    // Купец гружён под завязку: под оплату освобождается только масса
    // отданного, а монета за лот, который рынок ценит втрое-вчетверо выше
    // стоимости, тяжелее самого лота. Рынку есть чем платить — мешает спина
    // купца. Меньший лот освободил бы ещё меньше места и принёс бы меньше,
    // поэтому купец отдаёт весь выгодный лот и берёт столько монеты, сколько
    // помещается. (Ревью дифа 2026-11-13: правило «режь лот до засчитанного
    // не ниже уплаченного» здесь съедало лот до одной штуки.)
    {
        constexpr int kTight = 20;
        const std::uint16_t hungry = market(64);
        debt(hungry)[foodCol] = kBill;
        CHECK(shelf(hungry).add_of(coinIdx, int(cityCeiling)),
              "фикстура: казна рынка покрывает всю нужду");
        const std::uint16_t packed = trader(
            mstore.sheet[hungry].attributes.of(sm::AttributeId::Cha),
            mstore.sheet[hungry].skills.of(sm::SkillId::Trade),
            std::int64_t(kTight) * foodG);
        CHECK(shelf(packed).add_of(foodIdx, kTight), "фикстура: трюм купца");
        CHECK(sm::inventory_mass_g(shelf(packed)) == back(packed),
              "предусловие: спина купца занята грузом целиком");
        const sm::RowSide mPacked = row(hungry, foodIdx);
        const sm::RowSide tPacked = row(packed, foodIdx);
        const std::int64_t wanted = sm::gain_units(
            tPacked, mPacked, kTight, /*strict=*/true, power(hungry),
            power(packed));
        CHECK(wanted == kTight
                  && sm::ask_reach(sm::lot_take(mPacked, wanted), power(hungry),
                                   power(packed)) * coinG
                         > wanted * foodG,
              "предусловие: рынок ценит весь груз выше купца с наценкой, а "
              "монета за него тяжелее самого груза — оплату режет спина купца");
        const sm::CaravanDeal d = trade(packed, hungry);
        const int soldPacked = kTight - shelf(packed).count_of(foodIdx);
        const long long coinPacked = sm::coin_census_value(shelf(packed));
        CHECK(soldPacked == wanted,
              "ЛОТ НЕ ТАЕТ: купец отдал все выгодные единицы, хотя оплата за "
              "них упёрлась в его спину");
        CHECK(sm::inventory_mass_g(shelf(packed)) <= back(packed)
                  && coinPacked * coinG <= soldPacked * foodG
                  && (coinPacked + 1) * coinG > soldPacked * foodG,
              "оплата легла в освободившуюся спину до отказа и не больше: "
              "купец не перегружен, и ещё одна монета уже не влезла бы");
        CHECK(d.soldValue == sm::ask_price(coinPacked, power(hungry),
                                           power(packed))
                  && d.soldValue < sm::lot_take(mPacked, soldPacked)
                  && d.gain > 0,
              "корзина — ровно та, что собрана под уехавший лот: рынок отдал "
              "монету по цене взятия, меньше, чем засчитал груз, и купец в "
              "плюсе");
        std::printf("  [спина] отдано %d из %lld выгодных, монеты %lld\n",
                    soldPacked, (long long)wanted, coinPacked);
    }

    // ── 5б. НОГА ИСПОЛНЯЕТСЯ, ТОЛЬКО ЕСЛИ ОЦЕНКА ПРЕДЛОЖИВШЕГО РАСТЁТ ─────
    // Цель сквада — своя оценка своего инвентаря. Рынок ценит единицу выше
    // слабейшего купца даже с наценкой, но заплатит за неё по своей оценке
    // без своей доли — не больше, чем купец сам ценит отданное. Такую ногу он
    // не предлагает.
    // (Дыра, найденная таблицей мутаций 2026-11-12: без этого стража сделки
    // шли с нулевой прибылью купца и считались рейсу сделками.)
    {
        const int clothIdx = sm::item_index("cloth");
        const int clothCol = sm::commodity_index("cloth");
        const auto strong_market = [&](int cha, int rank) {
            const std::uint16_t m = market(64);
            mstore.sheet[m].attributes[sm::AttributeId::Cha] = std::uint8_t(cha);
            mstore.sheet[m].skills[sm::SkillId::Trade] = std::uint8_t(rank);
            debt(m)[clothCol] = 2;
            CHECK(shelf(m).add_of(coinIdx, 600000), "фикстура: казна");
            return m;
        };
        const std::uint16_t top = strong_market(255, sm::kMaxSkillRank);
        const std::uint16_t weak = trader(1, 0, bigBackG);
        CHECK(shelf(weak).add_of(clothIdx, 10), "фикстура: трюм купца");
        // Сколько единиц едет — ядро; что купцу за них причитается — оценка
        // рынка без его доли (`ask_reach`), против того, во сколько купец
        // сам ценит отданное.
        const std::int64_t zoneUnits = sm::gain_units(
            row(weak, clothIdx), row(top, clothIdx), 10, /*strict=*/true,
            power(top), power(weak));
        CHECK(zoneUnits > 0
                  && sm::ask_reach(sm::lot_take(row(top, clothIdx), zoneUnits),
                                   power(top), power(weak))
                         <= sm::lot_loss(row(weak, clothIdx), zoneUnits),
              "предусловие: рынок ценит единицу достаточно высоко, чтобы она "
              "поехала, но причитается за неё не больше, чем купец ценит её "
              "сам");
        const sm::CaravanDeal none = trade(weak, top);
        CHECK(none.soldValue == 0 && none.gain == 0
                  && shelf(weak).count_of(clothIdx) == 10
                  && debt(top)[clothCol] == 2,
              "нога без прибыли для предложившего не исполняется: ничего не "
              "уехало, счёт рынка цел");
        // Негативный контроль: тот же купец против равного по силе рынка
        // получает две трети его оценки — отказ выше был от доли, а не от
        // лота.
        const std::uint16_t even = strong_market(1, 0);
        CHECK(power(even) == power(weak), "предусловие контроля: силы равны");
        const sm::CaravanDeal some = trade(weak, even);
        CHECK(some.soldValue > 0 && some.gain > 0
                  && shelf(weak).count_of(clothIdx) < 10,
              "негативный контроль: с равным по силе рынком та же нога "
              "прибыльна и исполняется");
    }

    // ── 5в. ОПЛАТУ КУПЕЦ ПРИНИМАЕТ ТОЛЬКО В СВОБОДНУЮ СПИНУ И В МЕСТО
    //        ОТДАННОГО ГРУЗА ──────────────────────────────────────────────────
    // Спина купца занята грузом целиком, рынок платит тяжёлым (монеты нет).
    // Места под оплату — ровно столько, сколько освободит проданное; считать
    // освобождённым ВЕСЬ стак значило бы принять оплату туда, где ещё лежит
    // непроданный груз. (Вторая дыра той же таблицы мутаций.)
    {
        constexpr int kCargo = 100;
        const auto heavy_market = [&] {
            const std::uint16_t m = market(512);
            debt(m)[foodCol] = 5;
            CHECK(shelf(m).add_of(stoneIdx, 1000), "фикстура: тяжёлый платёж");
            CHECK(sm::coin_census_value(shelf(m)) == 0,
                  "предусловие: монеты у рынка нет");
            return m;
        };
        const std::uint16_t full = trader(13, 0, kCargo * foodG);
        CHECK(shelf(full).add_of(foodIdx, kCargo), "фикстура: полный трюм");
        CHECK(sm::inventory_mass_g(shelf(full)) == back(full),
              "предусловие: спина купца занята грузом целиком");
        const std::uint16_t m1 = heavy_market();
        (void)trade(full, m1);
        CHECK(sm::inventory_mass_g(shelf(full)) <= back(full),
              "оплата не перегрузила купца: принято не больше, чем освободил "
              "проданный груз");
        // Негативный контроль: купцу с просторной спиной тот же рынок платит
        // камнем — отказ выше был от места, а не от того, что камень не идёт
        // в оплату.
        const std::uint16_t roomy = trader(13, 0, 100 * kCargo * foodG);
        CHECK(shelf(roomy).add_of(foodIdx, kCargo), "фикстура: трюм купца");
        const std::uint16_t m2 = heavy_market();
        const sm::CaravanDeal paid = trade(roomy, m2);
        CHECK(paid.soldValue > 0 && shelf(roomy).count_of(stoneIdx) > 0,
              "негативный контроль: просторной спине тот же рынок платит "
              "камнем");
    }

    // ── 6. ВСТРЕЧНАЯ КОРЗИНА: ТОЛЬКО ТО, ЧТО БЕРУЩИЙ ЦЕНИТ НЕ НИЖЕ, И НЕ
    //        БОЛЬШЕ, ЧЕМ ПЛАТЕЛЬЩИК САМ ЦЕНИТ ВЗЯТОЕ ──────────────────────────
    // Закон стоит у двери `stage_payment`, и судится он У НЕЁ: на уровне
    // целой сделки строка, которую плательщик ценит выше берущего, сама едет
    // к плательщику товаром, и нарушение корзины прячется за второй ногой.
    {
        // 6а. Плательщик богат рудой, берущий рудой ЗАБИТ: руда плотнее
        // камня, и порядок «плотным вперёд» взял бы её первой, — но берущий
        // ценит её ниже плательщика, и в корзину она не идёт. Платят камнем.
        const std::uint16_t payer = market(512);
        CHECK(shelf(payer).add_of(ironIdx, 20), "фикстура: руда плательщика");
        CHECK(shelf(payer).add_of(stoneIdx, 100), "фикстура: камень плательщика");
        const std::int64_t ironG = sm::item_mass_g(*ironDef);
        const std::uint16_t jammed = trader(13, 0, 100 * ironG);
        CHECK(shelf(jammed).add_of(ironIdx, 90), "фикстура: берущий забит рудой");
        CHECK(std::int64_t(ironDef->value) * stoneG
                  > std::int64_t(stoneBase) * ironG,
              "предусловие: руда плотнее камня — порядок плотности взял бы её "
              "первой");
        CHECK(!values_not_lower(row(jammed, ironIdx), row(payer, ironIdx))
                  && values_not_lower(row(jammed, stoneIdx),
                                      row(payer, stoneIdx)),
              "предусловие: руду берущий ценит ниже плательщика, камень — "
              "не ниже");
        sm::DealLine lines[8];
        const sm::StagedPayment sp = sm::stage_payment(
            mstore, payer, jammed, /*target=*/3 * stoneBase,
            /*ceiling=*/std::int64_t(1) << 40, sm::kNoCarryLimitG,
            sm::Standing::Taker, lines);
        bool paidIron = false, paidStone = false;
        for (int i = 0; i < sp.lines; ++i) {
            paidIron = paidIron || int(lines[i].def) == ironIdx;
            paidStone = paidStone || int(lines[i].def) == stoneIdx;
        }
        CHECK(sp.price >= 3 * stoneBase && paidStone && !paidIron,
              "корзина покрыта камнем; руда, которую берущий ценит ниже "
              "плательщика, в платёж не пошла");
        // Негативный контроль: тот же плательщик, берущий со свободной
        // спиной — руда идёт первой. Отказ выше был от оценки, а не от того,
        // что дверь руды не видит.
        const std::uint16_t open = trader(13, 0, 100000 * ironG);
        sm::DealLine lines2[8];
        const sm::StagedPayment sp2 = sm::stage_payment(
            mstore, payer, open, 3 * stoneBase, std::int64_t(1) << 40,
            sm::kNoCarryLimitG, sm::Standing::Taker, lines2);
        CHECK(sp2.lines > 0 && int(lines2[0].def) == ironIdx,
              "негативный контроль: свободной спине платят рудой — плотным "
              "вперёд");

        // 6б. ПОТОЛОК ПЛАТЕЛЬЩИКА. У плательщика одна крупная монета, а
        // покрыть надо малость: переплатить он вправе, только пока сам ценит
        // взятое не ниже отданного.
        const int goldIdx = sm::item_index("coin_empire_gold");
        const int goldBase = sm::item_def_at(goldIdx)->value;
        const std::uint16_t rich = market(64);
        CHECK(shelf(rich).add_of(goldIdx, 1), "фикстура: одна крупная монета");
        const std::uint16_t taker = trader(13, 0, bigBackG);
        const std::int64_t small = goldBase / 7;
        CHECK(small > 0 && small < goldBase,
              "предусловие: покрыть надо меньше одной монеты");
        sm::DealLine l3[4];
        const sm::StagedPayment tight = sm::stage_payment(
            mstore, rich, taker, small, /*ceiling=*/small + 1,
            sm::kNoCarryLimitG, sm::Standing::Taker, l3);
        CHECK(tight.lines == 0 && tight.price == 0,
              "ПОТОЛОК: плательщик не отдаёт монету за то, что сам ценит "
              "дешевле неё, — корзина остаётся непокрытой");
        sm::DealLine l4[4];
        const sm::StagedPayment loose = sm::stage_payment(
            mstore, rich, taker, small, /*ceiling=*/2 * goldBase,
            sm::kNoCarryLimitG, sm::Standing::Taker, l4);
        CHECK(loose.lines == 1 && loose.price >= small
                  && loose.giverWorth <= 2 * goldBase,
              "под высоким потолком он переплачивает монетой: сдачи у бартера "
              "нет, а взятое он ценит выше отданного");
    }

    // ── 7. КРУГ ЧЕРЕЗ НАСТОЯЩИЙ РЫНОК УБЫТОЧЕН ───────────────────────────
    // Насос монеты, которым мир болел до 2026-11-12: наценка за оценку рынка
    // давала сильному продать строку дороже, чем тут же выкупить. Выкупать
    // можно только то, что приход не съел, поэтому круг строится без счёта:
    // купец с ЗАБИТОЙ спиной отдаёт груз рынку со свободной, и берёт выгоду
    // сверх своей оценки — при предельном перевесе сил.
    {
        const std::uint16_t m = market(512);
        mstore.sheet[m].attributes[sm::AttributeId::Cha] = 1;
        mstore.sheet[m].skills[sm::SkillId::Trade] = 0;
        CHECK(shelf(m).add_of(coinIdx, 600000), "фикстура: казна");
        constexpr int kHold = 100;
        const std::uint16_t t =
            trader(255, sm::kMaxSkillRank, kHold * foodG / 2);
        CHECK(shelf(t).add_of(foodIdx, kHold), "фикстура: трюм купца");
        CHECK(power(t) > 100 * power(m) && power(m) > 0,
              "предусловие: перевес сил предельный — сильнейший лист "
              "против слабейшего");
        const sm::RowSide tBefore = row(t, foodIdx);
        const sm::CaravanDeal d = trade(t, m);
        const int sold = kHold - shelf(t).count_of(foodIdx);
        CHECK(sold > 0 && d.soldValue > sm::lot_loss(tBefore, sold)
                  && shelf(m).count_of(foodIdx) == sold,
              "предусловие круга: сделка состоялась, купец взял выгоду сверх "
              "своей оценки, и проданное лежит на полке рынка");
        // Выкуп тех же единиц у рынка — тем же ядром, по состоянию ПОСЛЕ
        // сделки, прочитанному дверью стороны: цена взятия.
        const std::int64_t buyBack =
            sm::take_price(row(m, foodIdx), sold, power(m), power(t));
        CHECK(buyBack >= d.soldValue,
              "КРУГ УБЫТОЧЕН: выкупить проданное стоит не меньше вырученного "
              "— при предельном перевесе сил");
        const sm::CaravanDeal again = trade(t, m);
        CHECK(again.soldValue == 0 && again.boughtValue == 0,
              "и второй заход в тот же рынок пуст в обе стороны: оценки "
              "сошлись, делить нечего");
    }

    // ── 7б. ОЦЕНКА СТРОКИ ЗАВИСИТ ТОЛЬКО ОТ САМОЙ СТРОКИ ─────────────────
    // Шрам ревью 2026-11-12: нужда в сырье вычислялась на лету из запаса
    // ВЫХОДА рецепта. Купец продавал деревне железо с премией, следом
    // инструменты — нужда в железе исчезала, — и выкупал железо из завала
    // дешевле. Нужда стороны — число её счёта, и больше ничего: счёт на
    // инструмент оценку железа не трогает ни до прихода инструмента, ни после.
    {
        const int toolsIdx = sm::item_index("tools");
        const int toolsCol = sm::commodity_index("tools");
        const std::uint16_t m = market(64);
        CHECK(shelf(m).add_of(coinIdx, 600000), "фикстура: казна");
        const std::int64_t ironPlain = sm::worth_arriving(row(m, ironIdx), 1);
        const std::int64_t toolsPlain = sm::worth_arriving(row(m, toolsIdx), 1);
        debt(m)[toolsCol] = 40;
        CHECK(sm::worth_arriving(row(m, toolsIdx), 1) > toolsPlain,
              "негативный контроль: счёт на инструмент поднял оценку "
              "ИНСТРУМЕНТА — счёт живой");
        CHECK(sm::worth_arriving(row(m, ironIdx), 1) == ironPlain,
              "счёт на инструмент оценку ЖЕЛЕЗА не тронул");
        constexpr int kEach = 10;
        const std::uint16_t t = trader(13, 0, bigBackG);
        CHECK(shelf(t).add_of(ironIdx, kEach) && shelf(t).add_of(toolsIdx, kEach),
              "фикстура: в трюме и сырьё, и выход рецепта");
        const sm::CaravanDeal d = trade(t, m);
        CHECK(d.soldValue > 0 && shelf(t).count_of(toolsIdx) < kEach,
              "предусловие: инструмент в счёт продан");
        CHECK(shelf(t).count_of(ironIdx) == kEach
                  && shelf(m).count_of(ironIdx) == 0,
              "железо за премию «под рецепт» не продано — и выкупать нечего");
        CHECK(sm::worth_arriving(row(m, ironIdx), 1) == ironPlain,
              "и после прихода инструмента оценка железа прежняя");
    }

    // ── 7в. ЗЕРНИСТОСТЬ (M-490) И МАЛАЯ РАЗНИЦА (M-492) ──────────────────
    // Закон мира (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.2): завал обязан ронять
    // оценку, и «много» мерит масса. У строки стоимостью 5 оценка единицы,
    // округлённая до кванта, шагала 5, 4, 3, 2, 1, и завал меньше десятой
    // доли спины ей был не виден. Теперь до кванта округляется сумма лота, а
    // «ценит ли выше» решается точно. И п.3, замысел владельца 2026-11-13:
    // «всреднем скил решает как раз таки на сделаках где разница не велика» —
    // тот же лёгкий завал мастер берёт, а купец равной с рынком силы нет:
    // наценка рынка в полтора раза съедает разницу оценок.
    {
        const std::uint16_t full = market(64);
        const std::int64_t fullFits = back(full) / foodG;
        const int glut = int(fullFits * 8 / 100);   // восемь сотых спины
        CHECK(glut > 0 && shelf(full).add_of(foodIdx, glut),
              "фикстура: лёгкий завал еды у рынка");
        const std::uint16_t buyer = trader(255, sm::kMaxSkillRank, bigBackG);
        CHECK(shelf(buyer).add_of(coinIdx, 5000), "фикстура: кошелёк купца");
        CHECK(power(buyer) > 20 * power(full),
              "предусловие: купец — мастер против этого рынка, наценка рынка "
              "меньше двадцатой доли");
        const sm::RowSide mBefore2 = row(full, foodIdx);
        const sm::RowSide tBefore2 = row(buyer, foodIdx);
        // МЁРТВЫЙ ЗАКОН — округление каждой единицы до ближайшего кванта,
        // формула выписана нарочно (это мутация, которую детектор обязан
        // видеть, а не копия живой логики): первой же единицы он не отдал бы.
        const auto dead_unit = [](std::int64_t base, std::int64_t fits,
                                  std::int64_t spare) {
            if (spare <= 0) return base;
            if (spare >= fits) return std::int64_t(1);
            const std::int64_t w = (2 * base * (fits - spare) + fits) / (2 * fits);
            return w < 1 ? std::int64_t(1) : w;
        };
        CHECK(dead_unit(foodBase, back(buyer) / foodG, 1)
                  <= dead_unit(foodBase, fullFits, glut - 1),
              "предусловие: по округлённым единицам выгоды нет — мёртвый закон "
              "не взял бы здесь ни единицы");
        const std::int64_t units = sm::gain_units(
            mBefore2, tBefore2, glut, /*strict=*/true, power(full),
            power(buyer));
        const std::int64_t ask =
            sm::take_price(mBefore2, units, power(full), power(buyer));
        CHECK(units > 0 && sm::lot_take(tBefore2, units) > ask,
              "предусловие: на лоте мастер ценит взятое выше его цены");
        const long long coinsBefore2 = coins(buyer, full);
        const sm::CaravanDeal flow = trade(buyer, full);
        const int bought = shelf(buyer).count_of(foodIdx);
        CHECK(bought == units && shelf(full).count_of(foodIdx) == glut - bought,
              "ЛЁГКИЙ ЗАВАЛ ТЕЧЁТ К СВОБОДНОЙ СПИНЕ: мастер взял ровно те "
              "единицы, которые ценит выше их цены, и ни одной сверх");
        CHECK(flow.boughtValue == ask
                  && flow.boughtValue > sm::lot_loss(mBefore2, bought)
                  && flow.boughtValue < sm::lot_take(tBefore2, bought)
                  && flow.gain > 0 && coins(buyer, full) == coinsBefore2,
              "цена лота — цена взятия: выше оценки рынка, ниже оценки купца; "
              "купец в плюсе по своей мерке, монета не чеканится");
        std::printf("  [зернистость] завал %d, мастер взял %d за %d, ему %lld\n",
                    glut, bought, flow.boughtValue, (long long)flow.gain);

        // ПРОТИВОПОЛОЖНАЯ ПОЛЯРНОСТЬ: такой же рынок с таким же завалом, купец
        // равной с ним силы.
        const std::uint16_t full2 = market(64);
        CHECK(shelf(full2).add_of(foodIdx, glut), "фикстура: тот же завал");
        const std::uint16_t even = trader(
            mstore.sheet[full2].attributes.of(sm::AttributeId::Cha),
            mstore.sheet[full2].skills.of(sm::SkillId::Trade), bigBackG);
        CHECK(shelf(even).add_of(coinIdx, 5000), "фикстура: кошелёк купца");
        CHECK(power(even) == power(full2)
                  && values_higher(row(even, foodIdx), row(full2, foodIdx))
                  && !worth_moving(row(even, foodIdx), row(full2, foodIdx),
                                   power(full2), power(even)),
              "предусловие: силы равны; купец ценит еду выше рынка, но меньше "
              "чем в полтора раза");
        const sm::CaravanDeal still = trade(even, full2);
        CHECK(still.boughtValue == 0 && still.gain == 0
                  && shelf(even).count_of(foodIdx) == 0
                  && shelf(full2).count_of(foodIdx) == glut,
              "НА МАЛОЙ РАЗНИЦЕ РЕШАЕТ СКИЛЛ: равному по силе купцу лёгкий "
              "завал не выгоден — он не берёт ни единицы");
    }

    // ── 7в′. ЛОТ С ВЫГОДОЙ МЕНЬШЕ КВАНТА НЕ ПОКУПАЕТСЯ ──────────────────
    // Цель сквада — растить свою оценку. Точно мастер ценит единицы тонкого
    // завала выше их цены, и ядро их называет; но на всём таком лоте выгода
    // меньше кванта — в целых купец отдал бы ровно столько, сколько получил.
    // Такую ногу он не исполняет: она стоила бы ему восстановления даром.
    // (Дыра, найденная таблицей мутаций 2026-11-13: «нога и без прибыли»
    // проходила зелёной.) Завал свидетель подбирает сам — наименьший, на
    // котором мастеру есть что взять.
    {
        const std::uint16_t probe = market(64);
        const std::uint16_t master = trader(255, sm::kMaxSkillRank, bigBackG);
        CHECK(shelf(master).add_of(coinIdx, 5000), "фикстура: кошелёк купца");
        int thinGlut = 0;
        std::int64_t thinUnits = 0;
        for (int tryGlut = 1; tryGlut <= 200 && thinGlut == 0; ++tryGlut) {
            sm::RowSide m = row(probe, foodIdx);
            m.net += tryGlut;
            const sm::RowSide t = row(master, foodIdx);
            const std::int64_t n = sm::gain_units(
                m, t, tryGlut, /*strict=*/true, power(probe), power(master));
            if (n > 0
                && sm::lot_take(t, n)
                       == sm::take_price(m, n, power(probe), power(master))) {
                thinGlut = tryGlut;
                thinUnits = n;
            }
        }
        CHECK(thinGlut > 0 && thinUnits > 0,
              "предусловие: нашёлся тонкий завал, на котором мастер ценит "
              "единицы выше их цены, а весь лот — ровно в его цену");
        CHECK(shelf(probe).add_of(foodIdx, thinGlut), "фикстура: тонкий завал");
        const sm::CaravanDeal idleLeg = trade(master, probe);
        CHECK(idleLeg.boughtValue == 0 && idleLeg.gain == 0
                  && shelf(master).count_of(foodIdx) == 0
                  && shelf(probe).count_of(foodIdx) == thinGlut,
              "нога без прибыли для пришедшего не исполняется: ничего не "
              "уехало");
        std::printf("  [тонкий завал] %d штук, выгодных единиц %lld, выгода "
                    "лота в целых — ноль\n", thinGlut, (long long)thinUnits);
    }

    // ── 7в″. ПЕРЕПЛАТА ДО СВОЕЙ ОЦЕНКИ — НЕ СДЕЛКА ───────────────────────
    // Сдачи у бартера нет: последняя монета вправе переплатить, пока купец не
    // отдаёт больше, чем сам ценит взятое. Лот выгоден — купец ценит его выше
    // цены, — но в кошельке одно серебро, и покрыть цену можно только ровно
    // до своей оценки: отдал бы столько же, сколько получил. Такую ногу он не
    // исполняет. (Дыра, найденная таблицей мутаций 2026-11-13: «нога и без
    // прибыли пришедшего» проходила зелёной — отбор строк купца отсекал
    // нулевую выгоду раньше ноги, и страж ноги был без свидетеля.)
    {
        const int silverIdx = sm::item_index("coin_empire_silver");
        const std::int64_t silver = sm::item_def_at(silverIdx)->value;
        const std::uint16_t probe = market(64);
        const std::uint16_t purse10 = trader(255, sm::kMaxSkillRank, bigBackG);
        CHECK(shelf(purse10).add_of(silverIdx, 500), "фикстура: одно серебро");
        int pickGlut = 0;
        std::int64_t pickUnits = 0;
        for (int tryGlut = 1; tryGlut <= 400 && pickGlut == 0; ++tryGlut) {
            sm::RowSide m = row(probe, foodIdx);
            m.net += tryGlut;
            const sm::RowSide t = row(purse10, foodIdx);
            const std::int64_t n = sm::gain_units(
                m, t, tryGlut, /*strict=*/true, power(probe), power(purse10));
            const std::int64_t take = sm::lot_take(t, n);
            const std::int64_t ask =
                sm::take_price(m, n, power(probe), power(purse10));
            if (n > 0 && take % silver == 0 && ask < take
                && ask > take - silver) {
                pickGlut = tryGlut;
                pickUnits = n;
            }
        }
        CHECK(silver > 1 && pickGlut > 0 && pickUnits > 0,
              "предусловие: нашёлся завал, лот которого купец ценит выше цены, "
              "а серебром цену можно покрыть только ровно до своей оценки");
        CHECK(shelf(probe).add_of(foodIdx, pickGlut), "фикстура: завал");
        const sm::CaravanDeal overpaid = trade(purse10, probe);
        CHECK(overpaid.boughtValue == 0
                  && shelf(purse10).count_of(foodIdx) == 0
                  && shelf(purse10).count_of(silverIdx) == 500,
              "нога, в которой купец отдал бы ровно столько, сколько ценит "
              "взятое, не исполняется");
        // Негативный контроль: тот же завал, тот же мастер, но медь — цена
        // покрывается точно, выгода остаётся, сделка идёт.
        const std::uint16_t probe2 = market(64);
        CHECK(shelf(probe2).add_of(foodIdx, pickGlut), "фикстура: тот же завал");
        const std::uint16_t purse1 = trader(255, sm::kMaxSkillRank, bigBackG);
        CHECK(shelf(purse1).add_of(coinIdx, 5000), "фикстура: медь");
        const sm::CaravanDeal exact = trade(purse1, probe2);
        CHECK(exact.boughtValue > 0 && exact.gain > 0
                  && shelf(purse1).count_of(foodIdx) == pickUnits,
              "негативный контроль: с медью тот же лот выгоден и покупается — "
              "отказ выше был от переплаты, а не от лота");
        std::printf("  [переплата] завал %d, лот %lld: серебром — отказ, медью "
                    "— сделка с выгодой %lld\n",
                    pickGlut, (long long)pickUnits, (long long)exact.gain);
    }

    // ── 7г. ВЫБОР ПО МАССЕ: ВЫГОДНЕЙШЕЕ НА ГРАММ — ПЕРВЫМ ────────────────
    // Закон ядра: масса решает дважды — в оценке и в выборе, что брать, пока
    // спина свободна (`gain_denser@src/macro/economy.h`). Рынок завален и
    // рудой, и камнем сверх своей спины; весят они одинаково, руда дороже.
    // Спины купца на оба завала не хватит, и занять её обязана руда. (Дыра,
    // найденная таблицей мутаций 2026-11-13: перевёрнутый порядок проходил
    // зелёным.)
    {
        const std::uint16_t flooded = market(4);
        const std::int64_t oreG = sm::item_mass_g(*ironDef);
        constexpr int kBackUnits = 100;
        const std::uint16_t picker = trader(13, 0, kBackUnits * oreG);
        CHECK(shelf(picker).add_of(coinIdx, 2000), "фикстура: кошелёк купца");
        const int flood = 2 * kBackUnits + int(2 * back(flooded) / oreG);
        CHECK(shelf(flooded).add_of(ironIdx, flood)
                  && shelf(flooded).add_of(stoneIdx, flood),
              "фикстура: завал руды и завал камня");
        CHECK(oreG == stoneG && ironDef->value > stoneBase
                  && sm::worth_leaving(row(flooded, ironIdx), 1) == 1
                  && sm::worth_leaving(row(flooded, stoneIdx), 1) == 1,
              "предусловие: руда и камень весят одинаково, руда дороже, и оба "
              "завала рынок ценит по полу — выгода на грамм у руды выше");
        const sm::CaravanDeal picked = trade(picker, flooded);
        const int gotOre = shelf(picker).count_of(ironIdx);
        const int gotStone = shelf(picker).count_of(stoneIdx);
        CHECK(picked.boughtValue > 0 && gotOre > kBackUnits / 2
                  && gotOre > gotStone,
              "ВЫГОДНЕЙШЕЕ НА ГРАММ — ПЕРВЫМ: спину купца заняла руда, камню "
              "достался остаток");
        std::printf("  [выбор по массе] руды %d, камня %d из спины на %d штук\n",
                    gotOre, gotStone, kBackUnits);
    }

    // ── 8. ЛИСТ: СИЛА ДВИГАЕТ ЦЕНУ ПРОТИВ ОЦЕНКИ СТОЯЩЕГО ────────────────
    // (владелец 2026-08-30: «караван торгует выгоднее, потому что у него в
    // таблице выше уровень и харизма»; 2026-11-13: наценку стоящего задаёт
    // отношение сил — от нуля у мастера до ещё одной его оценки у полного
    // нуля.) Три одинаковые фикстуры, различные ТОЛЬКО листом купца, обязаны
    // сойтись по-разному монетой. Груз меньше нужды, казна покрывает всю
    // нужду: лот не упирается ни в груз сверх выгоды, ни в кошелёк.
    struct EdgeRun {
        int value = 0;              // монета, полученная купцом за лот
        int units = 0;
        std::int64_t floor = 0;     // сумма оценок купца — его нижняя граница
        std::int64_t ceiling = 0;   // сумма оценок рынка — верхняя
        int power = 0;
    };
    constexpr int kLot = 20;
    static_assert(kBill > kLot, "предусловие: нужда рынка больше груза");
    const auto run_sheet = [&](int cha, int tradeRank) {
        const std::uint16_t m = market(64);
        debt(m)[foodCol] = kBill;
        CHECK(shelf(m).add_of(coinIdx, int(cityCeiling)), "фикстура: казна");
        const std::uint16_t t = trader(cha, tradeRank, bigBackG);
        CHECK(shelf(t).add_of(foodIdx, kLot), "фикстура: трюм купца");
        EdgeRun r{};
        r.floor = sm::lot_loss(row(t, foodIdx), kLot);
        r.ceiling = sm::lot_take(row(m, foodIdx), kLot);
        r.power = power(t);
        trade(t, m);
        // Что купец получил — монетой из кошелька: засчитанное рынком от силы
        // не зависит, зависит то, сколько за него причитается.
        r.value = int(sm::coin_census_value(shelf(t)));
        r.units = kLot - shelf(t).count_of(foodIdx);
        return r;
    };
    const EdgeRun weak = run_sheet(1, 0);
    const EdgeRun even = run_sheet(13, 0);
    const EdgeRun strong = run_sheet(255, sm::kMaxSkillRank);
    CHECK(weak.power < even.power && even.power < strong.power,
          "предусловие: листы купцов упорядочены по силе");
    CHECK(weak.units == kLot && even.units == kLot && strong.units == kLot,
          "В ГЛУБОКУЮ НУЖДУ ГРУЗ УХОДИТ ВЕСЬ У ЛЮБОГО ЛИСТА: рынок ценит каждую "
          "единицу выше купца больше чем вдвое");
    CHECK(weak.value < even.value && even.value < strong.value,
          "ЛИСТ ДВИГАЕТ ЦЕНУ: чем сильнее анкета купца против того же рынка, "
          "тем больше монеты он получает за тот же лот");
    CHECK(2 * std::int64_t(weak.value) >= weak.ceiling - 1
              && weak.value >= weak.floor && strong.value <= strong.ceiling,
          "и сила не выводит цену за оценку стоящего: сильнейший не получает "
          "больше, чем рынок ценит груз, а слабейший — меньше половины");

    CHECK(dealsJudged > 15 && dealsMoved > 8 && standingLost == 0,
          "СТОЯЩИЙ НЕ ТЕРЯЕТ ПО СВОЕЙ ОЦЕНКЕ — ни в одной сделке этого файла: "
          "сумма оценок всего инвентаря рынка после сделки не меньше, чем до");
    CHECK(visitorIdle == 0,
          "ПРИШЕДШИЙ РАСТИТ СВОЮ ОЦЕНКУ: всякая сделка, в которой что-то "
          "уехало, подняла сумму оценок инвентаря купца");

    return sm::test::report("caravan_deal_test");
}
