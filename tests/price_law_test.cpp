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
void test_worth_is_lifted_by_need_and_dropped_by_glut() {
    int seen = 0, notMonotone = 0, belowFloor = 0, liftedWithoutNeed = 0;
    int droppedUnderNeed = 0, heavierDearer = 0, widerCheaper = 0;
    for (int base : {1, 2, 10, 100, 12800}) {
        for (int have : {0, 1, 9, 100, 1000, 1 << 20}) {
            for (int need : {0, 1, 9, 100, 1000, 1 << 20}) {
                for (std::int64_t mass : {10, 1000, 4000}) {
                    for (std::int64_t back :
                         {std::int64_t(0), std::int64_t(100000),
                          std::int64_t(1) << 30}) {
                        const std::int64_t w =
                            unit_worth(base, have, need, mass, back);
                        ++seen;
                        if (w < 1) ++belowFloor;
                        if (need <= have && w > base) ++liftedWithoutNeed;
                        if (need > have && w < base) ++droppedUnderNeed;
                        if (unit_worth(base, have + 1, need, mass, back) > w)
                            ++notMonotone;
                        if (unit_worth(base, have, need + 1, mass, back) < w)
                            ++notMonotone;
                        if (unit_worth(base, have, need, mass * 2, back) > w)
                            ++heavierDearer;
                        if (unit_worth(base, have, need, mass, back * 2) < w)
                            ++widerCheaper;
                    }
                }
            }
        }
    }
    CHECK(seen > 1000, "the sweep actually swept");
    CHECK(belowFloor == 0, "пол оценки — единица: «ничто не бесплатно»");
    CHECK(liftedWithoutNeed == 0 && droppedUnderNeed == 0,
          "выше стоимости оценку поднимает только нужда сверх запаса, ниже "
          "роняет только излишек сверх нужды");
    CHECK(notMonotone == 0,
          "оценка не растёт с запасом и не падает с нуждой");
    CHECK(heavierDearer == 0 && widerCheaper == 0,
          "«много» мерит масса против спины: вещь потяжелее не ценится выше, "
          "сторона со спиной пошире не ценит ниже");
    // Негативный контроль свипа: обе ветви ДВИЖУТСЯ, свип не утверждает
    // константу. И якорь, ради которого счёт штук заменён массой: тысяча
    // лёгких монет идёт почти по номиналу, тысяча возов — завал.
    CHECK(unit_worth(100, 0, 100, 1000, 100000) > unit_worth(100, 50, 100, 1000, 100000)
              && unit_worth(100, 50, 100, 1000, 100000) > unit_worth(100, 100, 100, 1000, 100000)
              && unit_worth(100, 100, 0, 1000, 1000000) > unit_worth(100, 500, 0, 1000, 1000000),
          "негативный контроль: нужда и завал оба двигают оценку");
    CHECK(unit_worth(100, 1000, 0, 10, 1000000) == 99
              && unit_worth(100, 1000, 0, 4000, 1000000) == 1,
          "МОНЕТА — ДЕНЬГИ БЕЗ ТЕГА: тысяча штук по десять граммов занимает "
          "сотую спины, тысяча штук по четыре килограмма забивает её");
}

// ── НАСОСА НЕТ: НИ ОДИН КРУГ НЕ ПРИНОСИТ МОНЕТЫ ──────────────────────────
// Закон мира: у стороны ОДНА оценка вещи, и цена лежит между оценками двух
// сторон; единица против выгоды едет только волей предложившего, и платит за
// неё он. Отсюда — круг «отдал n и забрал n» (или «забрал и отдал») убыточен
// либо нулевой, какой бы ни была разница сил, спин и нужд.
void test_no_pump_at_any_power() {
    // Круг глазами стороны `a`, которая предлагает ОБЕ ноги.
    const auto give_then_take = [](const RowSide& a, const RowSide& b,
                                   int aPower, int bPower, int n) {
        RowSide b2 = b, a2 = a;
        b2.net += n;
        a2.net -= n;
        return lot_price(a, b, aPower, bPower, n, Offer::Giver)
             - lot_price(b2, a2, bPower, aPower, n, Offer::Taker);
    };
    const auto take_then_give = [](const RowSide& a, const RowSide& b,
                                   int aPower, int bPower, int n) {
        RowSide b2 = b, a2 = a;
        b2.net -= n;
        a2.net += n;
        return lot_price(a2, b2, aPower, bPower, n, Offer::Giver)
             - lot_price(b, a, bPower, aPower, n, Offer::Taker);
    };
    {
        int trips = 0, pumps = 0, movedPrice = 0;
        for (int base : {1, 5, 100, 12800}) {
            for (int net : {-512, -8, -1, 0, 1, 8, 64, 512}) {
                for (std::int64_t mass : {10, 1000, 4000}) {
                    for (int n : {1, 2, 13, 64}) {
                        for (int mine : {0, 1, 13, 510}) {
                            for (int theirs : {0, 1, 13, 510}) {
                                for (std::int64_t back :
                                     {std::int64_t(50000),
                                      std::int64_t(100000000)}) {
                                    for (int cargo : {0, 40, 400}) {
                                        const RowSide a{base, cargo, mass,
                                                        50000};
                                        const RowSide b{base, net, mass,
                                                        back};
                                        ++trips;
                                        if (give_then_take(a, b, mine, theirs,
                                                           n) > 0) ++pumps;
                                        if (take_then_give(a, b, mine, theirs,
                                                           n) > 0) ++pumps;
                                        if (lot_price(a, b, mine, theirs, n,
                                                      Offer::Giver)
                                                != std::int64_t(base) * n)
                                            ++movedPrice;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        CHECK(trips > 5000, "the sweep actually swept");
        CHECK(pumps == 0,
              "НАСОСА НЕТ: круг не приносит ничего при любых силах, спинах, "
              "массах и нуждах, в обе стороны круга");
        CHECK(movedPrice > 1000,
              "негативный контроль свипа: в нём ЕСТЬ сделки не по стоимости "
              "— круг убыточен не потому, что всё идёт по номиналу");
    }
    // НЕГАТИВНЫЙ КОНТРОЛЬ НА МЁРТВОМ ЗАКОНЕ. Точка найдена ревью M-478: база
    // 100, полка 89, нужда 99, перевес сил +20. Закон наценки за кривую
    // (лот одной ценой по полке после сделки × (1 ± перевес)) давал в ней
    // +351 монету за круг; тот же круг по ядру — в минус. Формула мёртвого
    // закона выписана здесь нарочно: это мутация, которую детектор обязан
    // видеть, а не вторая копия живой логики.
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
        // нужда десять.
        const RowSide trader{base, 400, 1000, 1000000};
        const RowSide market{base, have - need, 1000, 100000000};
        CHECK(give_then_take(trader, market, 20, 0, n) <= 0
                  && give_then_take(trader, market, 510, 0, n) <= 0,
              "тот же круг в той же точке по ядру не приносит ничего — при "
              "том же и при предельном перевесе");
    }
    // ВТОРОЙ МЁРТВЫЙ ЗАКОН — единица против выгоды по оценке ОТДАЮЩЕГО, кто бы
    // её ни предложил: купец с забитой спиной «продаёт» рынку штуку по своей
    // оценке, хотя рынок ценит её ниже, и выкупает по оценке рынка. Точка:
    // купец 10 штук, рынок 25 излишка, спины по сто штук. Именно этот круг
    // закрывает правило «платит предложивший».
    {
        const RowSide trader{100, 10, 1000, 100000};
        const RowSide market{100, 25, 1000, 100000};
        RowSide m2 = market, t2 = trader;
        m2.net += 9;
        t2.net -= 9;
        const std::int64_t deadGain =
            lot_price(trader, market, 510, 1, 9, Offer::Taker)
          - lot_price(m2, t2, 1, 510, 9, Offer::Taker);
        CHECK(deadGain > 0,
              "детектор видит насос закона «против выгоды всегда платит "
              "берущий»");
        CHECK(give_then_take(trader, market, 510, 1, 9) <= 0,
              "по закону «платит предложивший» тот же круг не приносит ничего");
    }
}

// ── ТОРГОВАЯ СИЛА — ОДНА ДВЕРЬ, И ОБЕ СТОРОНЫ НАЗЫВАЮТ ЕЮ СЕБЯ ────────────
// Свидетель заведён 2026-09-21 на дефект, проживший в панели ИГРОКА всё
// время существования двусторонней цены: цена звалась как «(база, харизма,
// ранг Торговли)» там, где подпись была «(база, МОЯ сила, ЕГО сила)». Два
// симптома, оба видны игроку, оба утверждаются здесь — теперь на ядре
// единой ценомодели (сила делит выгоду отношением двух анкет):
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

    // Сделка, в которой ЕСТЬ что делить: ходок отдаёт штуку рынку, которому
    // её не хватает.
    constexpr std::int64_t kWideBack = std::int64_t(1) << 40;
    const RowSide roamer{100, 50, 1, kWideBack};
    const RowSide hungry{100, -9, 1, kWideBack};
    const auto gives = [&](const CharacterSheet& mine,
                           const CharacterSheet& theirs) {
        return lot_price(roamer, hungry, trade_power_of(mine),
                         trade_power_of(theirs), 1, Offer::Giver);
    };
    // 2. Цену двигает ОТНОШЕНИЕ анкет, значит контрагент обязан быть
    //    спрошен: сильной стороне та же штука достаётся дешевле.
    CHECK(gives(weak, skilled) < gives(weak, weak),
          "у сильной стороны слабый получает меньше — анкета контрагента "
          "участвует в цене");
    // 3. Прокачка Торговли улучшает цену при ТОМ ЖЕ контрагенте. Это прямое
    //    утверждение симптома №1.
    CHECK(gives(skilled, weak) > gives(weak, weak),
          "прокачанный торговец получает БОЛЬШЕ — скилл работает НА игрока");
    // 4. Негативный контроль: равные анкеты делят выгоду ровно пополам,
    //    каковы бы они ни были. Без него проверки выше прошли бы и на законе
    //    «цена всегда в пользу того, кто спросил».
    CHECK(gives(skilled, skilled) == gives(weak, weak)
              && gives(weak, weak) == 100 + (worth_arriving(hungry, 1) - 100) / 2,
          "негативный контроль: равные анкеты дают ровно середину между "
          "двумя оценками");
}

} // namespace

int main() {
    test_scarcity_shape();
    test_worth_is_lifted_by_need_and_dropped_by_glut();
    test_no_pump_at_any_power();
    test_trade_power_is_one_door();
    return sm::test::report("price_law_test");
}
