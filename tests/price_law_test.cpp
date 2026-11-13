// ЕДИНАЯ ЦЕНОМОДЕЛЬ (CANON S25, вердикты владельца 2026-11-12) и смерть
// насоса. Свидетели ядра стоят под компилятором у самих дверей
// (`src/macro/economy.h`); здесь — то, что компилятору не по силам: свип по
// состояниям рынка и силам сторон, негативный контроль на МЁРТВОМ законе
// наценки и связь ядра с листом. Утверждается:
//   · кривая производства — отсутствие дорого, завал дёшев, коридора нет
//     (её читают только решения о труде; сделка её не спрашивает);
//   · нужда идёт с ОДНОЙ лестницы нужд;
//   · ОЦЕНКА: нужда поднимает, завал роняет, и «много» мерит масса против
//     спины — тысяча лёгких монет идёт почти по номиналу, тысяча возов нет;
//   · НАСОСА НЕТ: круг «отдал — забрал» не приносит ничего при ЛЮБЫХ силах
//     и в любом состоянии рынка — в обе стороны круга. Тот же детектор
//     ВИДИТ насос у закона наценки за кривую, которым мир торговал до
//     2026-11-12 (351 монета за круг в точке, найденной ревью M-478);
//   · торговая сила — одна дверь листа, и делит она выгоду ОТНОШЕНИЕМ.
#include "macro/characters.h"   // squad_birth — руки рода
#include "check.h"

#include "macro/economy.h"
#include "macro/econ_day.h"

namespace {
// Руки города и деревни — анкета их рода (characters.h), а не вид места.
static const sm::Skills& CITY =
    sm::squad_birth(sm::SquadType::City).sheet.skills;

using namespace sm;

void test_scarcity_shape() {
    CHECK(stock_scarcity(0, 0) == 1.0f, "empty shelf, no demand = base");
    CHECK(stock_scarcity(1000, 8) < stock_scarcity(10, 8),
          "glut is cheaper than a modest stock");
    // РАВНОВЕСИЕ БЕЗ КАЛЕНДАРЯ (CANON S10, долг 2026-09-19): спрос приходит
    // сезонным ЧИСЛОМ (остаток счёта), и «склад == непокрытая нужда» даёт
    // базу без единого множителя горизонта в этой двери.
    CHECK(stock_scarcity(100, 100) == 1.0f,
          "stock == the unpaid need = base price (the world's equilibrium)");
    CHECK(stock_scarcity(0, 100) == float(100 + 1),
          "hungry absence prices the whole unpaid need — no ceiling");
    CHECK(stock_scarcity(1 << 20, 0) < stock_scarcity(1 << 10, 0),
          "a deeper glut keeps getting cheaper — no floor on the curve");
    CHECK(stock_price(10, 0, 100) == 10 * (100 + 1),
          "price = base x scarcity");
    CHECK(stock_price(10, 1 << 20, 0) >= 1,
          "a price never reaches zero — «ничто не бесплатно» is the floor");
    // ── СПРОС БЕЗ СЧЁТА — ДОЛЯ БЮДЖЕТА ГОРОЖАНИНА (M-137) ────────────────
    // ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ПЕРЕПИСАН 2026-09-26: он прибивал ЧИСЛО умершей
    // лестницы (`cloth` = 4 единицы в день, потому что в таблице стояло
    // `popPerUnitDay = 32`) и краснел ровно на том, что было УЛУЧШЕНИЕМ —
    // §8 ЗАКОН НУЛЕВОЙ п.5/п.7: он охранял случай, а не закон. Теперь
    // утверждается сам закон, и ни одного прибитого числа в нём нет.
    {
        const int pop = 128;
        // 1. ГОЛОД — житель-дни: одна единица кроет один день одной души.
        CHECK(season_demand_for(hunger_item_index(), nullptr, pop, CITY,
                                nullptr) == pop * kDaysPerSeason,
              "голод спрашивается житель-днями, и это своя система");
        // 2. НЕ ТОВАР — НЕ НУЖДА: у кинжала прямого спроса нет вовсе.
        CHECK(season_demand_for(item_index("wpn_dagger"), nullptr, pop, CITY,
                                nullptr) == 0,
              "строка, товаром не являющаяся, прямой нужды не имеет");
        // 3. БЮДЖЕТ: сумма стоимости всех строк комфорта не превышает
        //    население × сезон × бюджет дня и не отстаёт от него больше, чем
        //    на усечение по строке (целое деление роняет остаток дешевле
        //    одной единицы каждой строки). Второй копии формулы здесь нет —
        //    только её ПОТОЛОК и граница усечения.
        long long spend = 0, truncation = 0;
        int comfortRows = 0, dearer = -1, dearerUnits = -1;
        for (int c = 0; c < kCommodityCount; ++c) {
            if (!commodity_is_comfort(c)) continue;
            ++comfortRows;
            const int idx = commodity_item_index(c);
            const int units = season_demand_for(idx, nullptr, pop, CITY,
                                                nullptr);
            const int value = item_def_at(idx)->value;
            spend += (long long)units * value;
            truncation += value;
            // 4. ДОРОЖЕ — РЕЖЕ: строгая обратная монотонность по стоимости.
            if (dearer < 0 || value > dearer) {
                if (dearer >= 0) {
                    CHECK(units <= dearerUnits,
                          "дорогая строка нужна не чаще дешёвой");
                }
                dearer = value;
                dearerUnits = units;
            }
            // 5. МАСШТАБ: вдвое больше душ — вдвое больше нужды (с точностью
            //    до того же усечения).
            const int twice = season_demand_for(idx, nullptr, pop * 2, CITY,
                                                nullptr);
            CHECK(twice >= units * 2 - 1 && twice <= units * 2 + 1,
                  "нужда линейна по населению");
        }
        const long long budget =
            (long long)pop * kDaysPerSeason * kComfortValuePerPopDay;
        CHECK(comfortRows > 0, "негативный контроль: строки комфорта найдены");
        CHECK(spend <= budget && spend > budget - truncation,
              "стоимость всей нужды сезона = бюджет горожанина, с точностью "
              "до усечения по строке");
    }

    // НИ ОДНО БЛАГО НЕ ВАРИТСЯ ИЗ ПИЩИ — сквозной закон по ВСЕЙ таблице
    // составов (владелец, 2026-09-20; ради него и заведено ВОЛОКНО со своей
    // парцеллой). ПОЧЕМУ ЭТО ЗАКОН, А НЕ ВКУС: пока ткань пряли из зерна,
    // любое давление труда в сторону благ съедало хлеб мира. Измерено в день,
    // когда хлеб вырезали и городские руки остались без дела: ткани ×40,
    // съедено ртами 13.8 М → 11.2 М при ВДВОЕ большей добыче, голодавших в
    // городах 306 → 92 901. Свидетель стоит на ВСЕЙ таблице, потому что
    // следующая заглушка («мясо в клей», «зерно в бумагу») придёт другой
    // строкой, и поймать её должен закон, а не проверка про ткань.
    {
        const int hungerIdx = hunger_item_index();
        bool anyGoods = false, foodFree = true;
        for (int c = 0; c < kCommodityCount; ++c) {
            const int idx = item_index(kCommodities[c].id);
            const ItemDef* d = item_def_at(idx);
            if (!d || sm::item_archetype(*d) != sm::ObjectArchetype::Goods) {
                continue;
            }
            anyGoods = true;
            for (const ItemPart& part : item_parts(idx)) {
                if (int(part.def) == hungerIdx) foodFree = false;
            }
        }
        CHECK(anyGoods, "the sweep actually saw goods rows (negative control)");
        CHECK(foodFree,
              "no comfort good is spun from the hunger row — the world must "
              "not be able to weave its food into shirts");
    }
    // СПРОС ЧИТАЕТ ДОЛГ: полупогашенный счёт хлеба — и спрос ровно он.
    {
        std::int32_t debt[kCommodityCount] = {};
        debt[commodity_index("food")] = 777;
        CHECK(season_demand_for(hunger_item_index(), debt, 128, CITY,
                                nullptr) == 777,
              "the direct demand IS the unpaid bill");
    }
}

// ── ОЦЕНКА: НУЖДА ПОДНИМАЕТ, ЗАВАЛ РОНЯЕТ, И «МНОГО» МЕРИТ МАССА ──────────
// Закон мира (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.2; ряд M-490): у стороны одна
// оценка вещи, и оценка ЗАПАСА строки есть функция состояния — до кванта
// округляется сумма, а не единица. Свип утверждает свойства, а не числа;
// «ценит выше» спрашивается у ядра ТОЧНО (`gain_units`), округлённые единицы
// сравнивать нельзя — они расходятся с точной оценкой на квант.
void test_worth_is_lifted_by_need_and_dropped_by_glut() {
    const auto at = [](RowSide r, std::int64_t net) {
        r.net = net;
        return r;
    };
    // «Единицу, приводящую запас к j, сторона `a` ценит строго выше, чем
    // сторона `b`» — точно: единица едет от `b` (она у него j-я) к `a`.
    const auto dearer = [&](const RowSide& a, const RowSide& b,
                            std::int64_t j) {
        return gain_units(at(b, j), at(a, j - 1), 1, /*strict=*/true, 0, 1) == 1;
    };
    int seen = 0, belowFloor = 0, liftedWithoutNeed = 0, droppedUnderNeed = 0;
    int notState = 0, notAdditive = 0, growsWithStock = 0;
    int heavierDearer = 0, widerCheaper = 0, offByMoreThanHalf = 0, exactSums = 0;
    // (offByMoreThanHalf: дальше половины кванта у оценки от нуля, дальше
    // кванта у лота.)
    for (int base : {1, 2, 5, 100, 12800}) {
        for (std::int64_t net :
             {std::int64_t(-(1 << 20)), std::int64_t(-1000), std::int64_t(-9),
              std::int64_t(-1), std::int64_t(0), std::int64_t(1),
              std::int64_t(9), std::int64_t(100), std::int64_t(1000),
              std::int64_t(1 << 20)}) {
            for (std::int64_t mass : {10, 1000, 4000}) {
                for (std::int64_t back :
                     {std::int64_t(0), std::int64_t(100000),
                      std::int64_t(1) << 30, std::int64_t(1) << 40}) {
                    const RowSide r{base, net, mass, back};
                    ++seen;
                    const std::int64_t in = worth_arriving(r, 1);
                    const std::int64_t out = worth_leaving(r, 1);
                    if (in < 1 || out < 1) ++belowFloor;
                    if (net >= 0 && in > base) ++liftedWithoutNeed;
                    if (net < 0 && in < base) ++droppedUnderNeed;
                    for (std::int64_t n : {1, 7, 500}) {
                        // Взятое и возвращённое возвращает оценку запаса в
                        // прежнее число; части складываются в целое точно.
                        if (lot_take(r, n) != lot_loss(at(r, net + n), n))
                            ++notState;
                        if (lot_take(r, n + 3)
                            != lot_take(r, n) + lot_take(at(r, net + n), 3))
                            ++notAdditive;
                    }
                    // ТОЧНАЯ СУММА — ПЕРЕБОРОМ ПО ЗАКОНУ, а не замкнутой формой
                    // ядра: каждая единица излишка стоит стоимость × (F − j)/F,
                    // не ниже единицы; копится числитель над общим знаменателем
                    // F. Оценка запаса от нуля округлена до ближайшего — она
                    // в половине кванта от точной; лот есть разность двух таких
                    // оценок и потому лежит в пределах одного кванта. (Нужду
                    // свип не суммирует: там оценка целая по закону, её держат
                    // якоря ядра.)
                    const std::int64_t fits = back / mass;
                    if (net >= 0 && net <= 1000 && fits > 0 && fits <= 100000) {
                        const std::int64_t n = 500;
                        std::int64_t numer = 0;   // сумма × F
                        for (std::int64_t j = net + 1; j <= net + n; ++j) {
                            const std::int64_t part =
                                j < fits ? std::int64_t(base) * (fits - j) : 0;
                            numer += part < fits ? fits : part;
                        }
                        ++exactSums;
                        const std::int64_t off = lot_take(r, n) * fits - numer;
                        const std::int64_t room = net == 0 ? fits : 2 * fits;
                        if (2 * off > room || 2 * off < -room) ++offByMoreThanHalf;
                    }
                    // Оценка единицы не растёт с запасом; потяжелее — не
                    // дороже; спина пошире — не дешевле. Точно.
                    if (gain_units(at(r, net), at(r, net), 1, true, 0, 1) != 0)
                        ++growsWithStock;
                    const RowSide heavy{base, net, mass * 2, back};
                    const RowSide wide{base, net, mass, back * 2};
                    if (dearer(heavy, r, net + 1)) ++heavierDearer;
                    if (dearer(r, wide, net + 1)) ++widerCheaper;
                }
            }
        }
    }
    CHECK(seen > 500, "the sweep actually swept");
    CHECK(belowFloor == 0, "пол оценки — единица: «ничто не бесплатно»");
    CHECK(liftedWithoutNeed == 0 && droppedUnderNeed == 0,
          "выше стоимости оценку поднимает только непогашенная нужда, ниже "
          "роняет только излишек");
    CHECK(notState == 0 && notAdditive == 0,
          "ОЦЕНКА ЗАПАСА — ФУНКЦИЯ СОСТОЯНИЯ: взятое и возвращённое возвращает "
          "её в прежнее число, части лота складываются в целое точно");
    CHECK(exactSums > 50 && offByMoreThanHalf == 0,
          "ДО КВАНТА ОКРУГЛЯЕТСЯ СУММА: оценка пятисот единиц от пустой полки "
          "лежит в половине кванта от точной суммы, посчитанной перебором по "
          "закону, а лот с непустой полки — в пределах одного кванта");
    CHECK(growsWithStock == 0,
          "оценка единицы не растёт с запасом: следующую единицу сторона не "
          "ценит выше той, что у неё уже есть");
    CHECK(heavierDearer == 0 && widerCheaper == 0,
          "«много» мерит масса против спины: вещь потяжелее не ценится выше, "
          "сторона со спиной пошире не ценит ниже");
    // Негативный контроль свипа: обе ветви ДВИЖУТСЯ, и детектор «ценит выше»
    // умеет сказать «да» — свип не утверждает константу.
    const RowSide row{100, 0, 1000, 1000000};
    CHECK(worth_arriving(at(row, -100), 1) > worth_arriving(at(row, -50), 1)
              && worth_arriving(at(row, -50), 1) > worth_arriving(row, 1)
              && worth_arriving(at(row, 99), 1) > worth_arriving(at(row, 499), 1),
          "негативный контроль: нужда и завал оба двигают оценку");
    CHECK(dearer(row, RowSide{100, 0, 4000, 1000000}, 100)
              && dearer(RowSide{100, 0, 1000, 2000000}, row, 100)
              && gain_units(at(row, 500), row, 1, true, 0, 1) == 1,
          "негативный контроль детектора: лёгкую вещь ценят выше тяжёлой, "
          "широкая спина ценит выше узкой, пустая сторона — выше заваленной");
    // ЯКОРЯ, посчитанные рукой по формуле закона. Монета: тысячная штука по
    // десять граммов занимает сотую спины — 100 × (1 − 0.01) = 99; тысяча
    // штук по четыре килограмма спину забивает — пол.
    CHECK(worth_arriving(RowSide{100, 999, 10, 1000000}, 1) == 99
              && worth_arriving(RowSide{100, 999, 4000, 1000000}, 1) == 1,
          "МОНЕТА — ДЕНЬГИ БЕЗ ТЕГА: тысяча штук по десять граммов занимает "
          "сотую спины, тысяча штук по четыре килограмма забивает её");
    // ЗЕРНИСТОСТЬ (M-490). Сто штук стоимостью 5 при спине в тысячу штук:
    // точно 5 × (100 − 5050/1000) = 474.75. Округление КАЖДОЙ единицы дало
    // бы 500 — по пять за штуку, завал не виден вовсе.
    CHECK(lot_take(RowSide{5, 0, 1000, 1000000}, 100) == 475,
          "до кванта округляется СУММА лота, а не единица: 474.75 → 475, а не "
          "500");
    // Дом завален на двадцатую долю спины, ребёнок пуст: одна единица у обоих
    // стоит те же 5 квантов, а лот выгоден. Граница рукой:
    // (1000−k)/1000 > (474999+k)/500000 ⇔ 501·k < 25001 ⇔ k ≤ 49.
    const RowSide home{5, 25000, 1000, std::int64_t(500000) * 1000};
    const RowSide child{5, 0, 1000, std::int64_t(1000) * 1000};
    const std::int64_t units = gain_units(home, child, 25000, true, 0, 1);
    CHECK(worth_arriving(child, 1) == worth_leaving(home, 1) && units == 49
              && lot_take(child, units) > lot_loss(home, units),
          "завал меньше шага оценки на одной единице не виден, а на лоте "
          "виден: дом отдаёт ребёнку 49 единиц, и семья на этом выигрывает");
}

// ── НАСОСА НЕТ: НИ ОДИН КРУГ НЕ ПРИНОСИТ МОНЕТЫ ──────────────────────────
// Закон мира (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.3, форма 2026-11-13): взятое
// у стоящего стоит его оценку плюс его долю от неё, отданное ему он
// засчитывает по своей оценке. Стоящий никогда не теряет по своей оценке,
// поэтому круг «взял n и вернул» (или «отдал и забрал») пришедшему в убыток
// либо в ноль, какой бы ни была разница сил, спин и нужд.
void test_no_pump_at_any_power() {
    const auto at = [](RowSide r, std::int64_t net) {
        r.net = net;
        return r;
    };
    // Круги пришедшего со стоящим `s`: получил минус уплатил, в оценке
    // стоящего. Силы — (стоящий, пришедший).
    const auto take_then_return = [&](const RowSide& s, int sp, int vp, int n) {
        return ask_reach(lot_take(at(s, s.net - n), n), sp, vp)
             - take_price(s, n, sp, vp);
    };
    const auto give_then_retake = [&](const RowSide& s, int sp, int vp, int n) {
        return ask_reach(lot_take(s, n), sp, vp)
             - take_price(at(s, s.net + n), n, sp, vp);
    };
    {
        int trips = 0, pumps = 0, losses = 0, movedPrice = 0, standingLost = 0;
        for (int base : {1, 5, 100, 12800}) {
            for (int net : {-512, -8, -1, 0, 1, 8, 64, 512}) {
                for (std::int64_t mass : {10, 1000, 4000}) {
                    for (int n : {1, 2, 13, 64}) {
                        for (int sp : {0, 1, 13, 510}) {
                            for (int vp : {0, 1, 13, 510}) {
                                for (std::int64_t back :
                                     {std::int64_t(50000),
                                      std::int64_t(100000000)}) {
                                    const RowSide s{base, net, mass, back};
                                    ++trips;
                                    const std::int64_t a =
                                        take_then_return(s, sp, vp, n);
                                    const std::int64_t b =
                                        give_then_retake(s, sp, vp, n);
                                    if (a > 0 || b > 0) ++pumps;
                                    if (a < 0 || b < 0) ++losses;
                                    // Стоящий не теряет: за отданное получает
                                    // не меньше своей оценки, за полученное
                                    // отдаёт не больше.
                                    if (take_price(s, n, sp, vp) < lot_loss(s, n)
                                        || ask_reach(lot_take(s, n), sp, vp)
                                               > lot_take(s, n))
                                        ++standingLost;
                                    if (take_price(s, n, sp, vp)
                                            != std::int64_t(base) * n)
                                        ++movedPrice;
                                }
                            }
                        }
                    }
                }
            }
        }
        CHECK(trips > 5000, "the sweep actually swept");
        CHECK(pumps == 0,
              "НАСОСА НЕТ: круг не приносит пришедшему ничего при любых силах, "
              "спинах, массах и нуждах, в обе стороны круга");
        CHECK(standingLost == 0,
              "СТОЯЩИЙ НЕ ТЕРЯЕТ ПО СВОЕЙ ОЦЕНКЕ ни на взятом у него, ни на "
              "отданном ему");
        CHECK(losses > 1000 && movedPrice > 1000,
              "негативный контроль свипа: в нём ЕСТЬ круги в убыток и сделки не "
              "по стоимости — круг не прибылен не потому, что всё идёт по "
              "номиналу");
    }
    // НЕГАТИВНЫЙ КОНТРОЛЬ НА МЁРТВОМ ЗАКОНЕ. Точка найдена ревью M-478: база
    // 100, полка 89, нужда 99, перевес сил +20. Закон наценки за кривую
    // (лот одной ценой по полке после сделки × (1 ± перевес)) давал в ней
    // +351 монету за круг: сильный продавал ДОРОЖЕ оценки рынка и выкупал
    // ДЕШЕВЛЕ. Формула мёртвого закона выписана здесь нарочно: это мутация,
    // которую детектор обязан видеть, а не вторая копия живой логики.
    {
        const auto dead_unit = [](int base, int have, int need, int n,
                                  bool selling, int edgePct) {
            const long long shelf = (selling ? have + n : have - n) + 1;
            const long long curve =
                (2LL * base * (need + 1) + shelf) / (2 * shelf);
            const long long k = selling ? 100 + edgePct : 100 - edgePct;
            const long long v = (curve * k + 50) / 100;
            return v < 1 ? 1LL : v;
        };
        const int base = 100, have = 89, need = 99, n = 13, edge = 20;
        const long long deadGain =
            n * dead_unit(base, have, need, n, true, edge)
          - n * dead_unit(base, have + n, need, n, false, edge);
        CHECK(deadGain == 351,
              "детектор видит насос мёртвого закона: +351 за круг");
        // Та же точка глазами ядра: полка 89 против нужды 99 — непогашенная
        // нужда десять. Наценка есть только у стоящего и только в его пользу.
        const RowSide market{base, have - need, 1000, 100000000};
        CHECK(give_then_retake(market, 13, 20, n) < 0
                  && give_then_retake(market, 13, 510, n) < 0
                  && give_then_retake(market, 0, 510, n) == 0,
              "тот же круг в той же точке по ядру пришедшему в убыток — при том "
              "же и при предельном перевесе; с бессильным стоящим — ровно ноль");
    }
}

// ── ДВЕРИ ЛОТА: ГРАНИЦЫ ЧЕСТНЫЕ, ЦЕНУ НАЗЫВАЕТ ОЦЕНКА СТОЯЩЕГО ────────────
// Свойства, а не числа (§8 п.4): что бы ни назвали двери лота, это обязано
// быть ровно тем, что обещает закон. (Ревью дифа 2026-11-13 нашло, что корзина
// держала свою копию формулы цены и расходилась с ценой лота на квант; с той
// поры все двери читают одну цену.)
void test_lot_doors_keep_their_promises() {
    const auto at = [](RowSide r, std::int64_t net) {
        r.net = net;
        return r;
    };
    int seen = 0, priceFell = 0, splitPaid = 0, wanted = 0;
    int takeBroken = 0, giveBroken = 0, coverBroken = 0, fitBroken = 0;
    int reachBroken = 0;
    for (int base : {1, 3, 5, 100}) {
        for (int sNet : {-40, -3, 0, 2, 12, 56, 400}) {
            for (int vNet : {-2, 0, 27, 300}) {
                for (std::int64_t sBack : {std::int64_t(50), std::int64_t(209),
                                           std::int64_t(100000)}) {
                    for (std::int64_t vBack : {std::int64_t(29), std::int64_t(100),
                                               std::int64_t(1000000)}) {
                        for (int sp : {0, 2, 13, 510}) {
                            for (int vp : {0, 13}) {
                                const RowSide s{base, sNet, 1, sBack};
                                const RowSide v{base, vNet, 1, vBack};
                                ++seen;
                                // Цена не убывает с числом единиц; дробить
                                // пришедшему не выгодно.
                                std::int64_t prevAsk = 0, prevReach = 0;
                                for (std::int64_t n = 1; n <= 40; ++n) {
                                    const std::int64_t ask =
                                        take_price(s, n, sp, vp);
                                    const std::int64_t reach =
                                        ask_reach(lot_take(s, n), sp, vp);
                                    if (ask < prevAsk || reach < prevReach)
                                        ++priceFell;
                                    prevAsk = ask;
                                    prevReach = reach;
                                    if (n >= 2
                                        && (take_price(s, 1, sp, vp)
                                                + take_price(at(s, sNet - 1), n - 1, sp, vp)
                                                < ask
                                            || ask_reach(lot_take(s, 1), sp, vp)
                                                + ask_reach(lot_take(at(s, sNet + 1), n - 1), sp, vp)
                                                > reach))
                                        ++splitPaid;
                                }
                                const std::int64_t wantTake =
                                    gain_units(s, v, 40, true, sp, vp);
                                const std::int64_t wantGive =
                                    gain_units(v, s, 40, true, sp, vp);
                                if (wantTake > 0 || wantGive > 0) ++wanted;
                                const std::int64_t payIn =
                                    gain_units(v, s, 40, false, 0, 1);
                                const std::int64_t payOut =
                                    gain_units(s, v, 40, false, 0, 1);
                                for (std::int64_t money : {0, 7, 60, 5000}) {
                                    const LotQuote t =
                                        take_lot(s, v, sp, vp, 40, money);
                                    if (t.units > wantTake || t.price > money
                                        || t.price != (t.units > 0
                                                           ? take_price(s, t.units, sp, vp)
                                                           : 0)
                                        || (t.units < wantTake
                                            && take_price(s, t.units + 1, sp, vp)
                                                   <= money))
                                        ++takeBroken;
                                    const LotQuote g =
                                        give_lot(v, s, sp, vp, 40, money);
                                    if (g.units > wantGive || g.price > money
                                        || g.price != (g.units > 0
                                                           ? ask_reach(lot_take(s, g.units), sp, vp)
                                                           : 0)
                                        || (g.units < wantGive
                                            && ask_reach(lot_take(s, g.units + 1), sp, vp)
                                                   <= money))
                                        ++giveBroken;
                                    const LotQuote c = cover_lot(v, s, 40, money);
                                    if (money <= 0 || payIn == 0) {
                                        if (c.units != 0) ++coverBroken;
                                    } else if (c.units < 1 || c.units > payIn
                                               || c.price != lot_take(s, c.units)
                                               || (c.price < money && c.units != payIn)
                                               || (c.units > 1
                                                   && lot_take(s, c.units - 1) >= money)) {
                                        ++coverBroken;
                                    }
                                    const LotQuote f = fit_lot(v, s, 40, money);
                                    if (f.units > payIn || f.price > money
                                        || f.price != lot_take(s, f.units)
                                        || (f.units < payIn
                                            && lot_take(s, f.units + 1) <= money))
                                        ++fitBroken;
                                    const LotQuote r =
                                        reach_lot(s, v, sp, vp, 40, money);
                                    if (r.units > payOut || r.price > money
                                        || r.price != (r.units > 0
                                                           ? take_price(s, r.units, sp, vp)
                                                           : 0)
                                        || (r.units < payOut
                                            && take_price(s, r.units + 1, sp, vp)
                                                   <= money))
                                        ++reachBroken;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(seen > 5000 && wanted > 500,
          "the sweep actually swept, and it holds lots the visitor wants");
    CHECK(priceFell == 0,
          "цена взятого и причитающееся за отданное не убывают с числом единиц");
    CHECK(splitPaid == 0,
          "ДРОБИТЬ ПРИШЕДШЕМУ НЕ ВЫГОДНО: первая единица отдельно и остаток "
          "отдельно не дают ему больше целого лота");
    CHECK(takeBroken == 0,
          "ВЗЯТЬ У СТОЯЩЕГО: не больше единиц, чем пришедший ценит выше цены; "
          "цена — цена взятия, по платёжеспособности, и следующая единица уже "
          "не по ней");
    CHECK(giveBroken == 0,
          "ОТДАТЬ СТОЯЩЕМУ: не больше единиц, чем стоящий ценит выше с "
          "наценкой; причитается его оценка без его доли, не больше, чем он "
          "способен отдать");
    CHECK(coverBroken == 0,
          "ПЛАТИТ ПРИШЕДШИЙ: засчитано по оценке стоящего; покрыто, либо отдано "
          "всё, что годится; ни единицы сверх покрытия");
    CHECK(fitBroken == 0,
          "ПЛАТИТ ПРИШЕДШИЙ БЕЗ ПЕРЕПЛАТЫ: засчитано не выше бюджета, и "
          "следующая единица в бюджет уже не влезает");
    CHECK(reachBroken == 0,
          "ПЛАТИТ СТОЯЩИЙ: его единицы идут по цене взятия, не выше бюджета, и "
          "следующая единица в бюджет уже не влезает");
}

// ── ТОРГОВАЯ СИЛА — ОДНА ДВЕРЬ, И ОБЕ СТОРОНЫ НАЗЫВАЮТ ЕЮ СЕБЯ ────────────
// Свидетель заведён 2026-09-21 на дефект, проживший в панели ИГРОКА всё
// время существования двусторонней цены: цена звалась как «(база, харизма,
// ранг Торговли)» там, где подпись была «(база, МОЯ сила, ЕГО сила)». Два
// симптома, оба видны игроку, оба утверждаются здесь — на ядре единой
// ценомодели (наценку стоящего задаёт отношение двух анкет):
//   1. ранг Торговли стоял на месте ЧУЖОЙ силы ⇒ прокачка скилла ухудшала
//      цену;
//   2. анкета контрагента не спрашивалась вовсе ⇒ торг со столицей и с
//      нищим бродягой стоил одинаково.
// Утверждается СВОЙСТВО, а не число: монотонность силы по обеим осям листа
// и то, что чужая анкета цену двигает. Литералов из таблицы здесь нет.
void test_trade_power_is_one_door() {
    // 1. Сила растёт по ОБЕИМ осям листа: атрибут — природная мощь, скилл её
    //    МНОЖИТ (attributes.h). Инверсия по второй оси и была дефектом.
    CharacterSheet weak{};
    weak.attributes[AttributeId::Cha] = 10;
    weak.skills[SkillId::Trade] = 0;
    CharacterSheet skilled = weak;
    skilled.skills[SkillId::Trade] = 30;
    CharacterSheet charming = weak;
    charming.attributes[AttributeId::Cha] = 30;
    CHECK(trade_power_of(skilled) > trade_power_of(weak),
          "ранг Торговли УСИЛИВАЕТ торговую силу, а не ослабляет её");
    CHECK(trade_power_of(charming) > trade_power_of(weak),
          "харизма усиливает торговую силу");

    // Рынку не хватает девяти штук; пришедший отдаёт ему одну и берёт одну.
    constexpr std::int64_t kWideBack = std::int64_t(1) << 40;
    const RowSide hungry{100, -9, 1, kWideBack};
    const auto gives = [&](const CharacterSheet& mine,
                           const CharacterSheet& theirs) {
        return ask_reach(lot_take(hungry, 1), trade_power_of(theirs),
                         trade_power_of(mine));
    };
    const auto takes = [&](const CharacterSheet& mine,
                           const CharacterSheet& theirs) {
        return take_price(hungry, 1, trade_power_of(theirs),
                          trade_power_of(mine));
    };
    // 2. Цену двигает ОТНОШЕНИЕ анкет, значит контрагент обязан быть
    //    спрошен: у сильной стороны слабый получает меньше и платит больше.
    CHECK(gives(weak, skilled) < gives(weak, weak)
              && takes(weak, skilled) > takes(weak, weak),
          "у сильной стороны слабый получает меньше и платит больше — анкета "
          "контрагента участвует в цене");
    // 3. Прокачка Торговли улучшает цену при ТОМ ЖЕ контрагенте. Это прямое
    //    утверждение симптома №1.
    CHECK(gives(skilled, weak) > gives(weak, weak)
              && takes(skilled, weak) < takes(weak, weak),
          "прокачанный торговец получает БОЛЬШЕ и платит МЕНЬШЕ — скилл "
          "работает НА игрока");
    // 4. Негативный контроль: равные анкеты дают одну цену, каковы бы они ни
    //    были, — закон читает отношение сил. Якорь рукой: доля равного
    //    стоящего — половина, значит за отданное причитается две трети его
    //    оценки, а взятое стоит полторы.
    CHECK(gives(skilled, skilled) == gives(weak, weak)
              && gives(weak, weak) == worth_arriving(hungry, 1) * 2 / 3
              && takes(weak, weak) == worth_leaving(hungry, 1) * 3 / 2,
          "негативный контроль: равные анкеты — две трети оценки стоящего за "
          "отданное и полторы за взятое, какими бы сильными ни были оба");
}

} // namespace

int main() {
    test_scarcity_shape();
    test_worth_is_lifted_by_need_and_dropped_by_glut();
    test_no_pump_at_any_power();
    test_lot_doors_keep_their_promises();
    test_trade_power_is_one_door();
    return sm::test::report("price_law_test");
}
