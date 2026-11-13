// ЕДИНАЯ ЦЕНОМОДЕЛЬ: ЯДРО БАРТЕРА И СТОИМОСТЕЙ — и рядом закон возврата рейса
// да дверь нужды (нужда — НЕ ядро: её называет модуль стороны, см. ниже).
//
// ЗАМЫСЕЛ — СЛОВАМИ ВЛАДЕЛЬЦА (2026-11-12, дословно — CANON S25, «ЕДИНАЯ
// ЦЕНОМОДЕЛЬ»): «надо обобщить в обще единую ценомодель»; «у нас же есть
// замысел 3 столпа экономики 1) абсолютная стоимость 2) цена по спросу
// (контекстно разные двери и механизмы) 3) цена/массу»; и о разделе слоёв:
// «у нас есть единая система бартера и стоиосмтей там явно стоимость масса
// пок способность и запас а нужда уже идёт из модуля и как я понимаю время и
// дорога тоже»; «модуль отпраавляет в единую систему НУЖДА типа коректирует
// цену а сама коректировка и процесс торгволи бартера уже по единому
// агностичному механизму».
//
// ЧТО ЯДРО ЗНАЕТ — ПЯТЬ ЧИСЕЛ, И БОЛЬШЕ НИЧЕГО:
//   ВЕЩЬ даёт два  — СТОИМОСТЬ (абсолют, ориентир порядка) и МАССУ
//                    (обобщённое количество: «много» мерит она, а не счёт);
//   СТОРОНА даёт три — НУЖДУ (число, которое называет её модуль; по
//                    умолчанию нужды нет, и ядро работает без неё), СПИНУ
//                    (что она способна унести) и СИЛУ (итог листа).
// Масса решает ДВАЖДЫ: в оценке (завал — излишек против спины) и в выборе —
// чем платить и что брать (`densest_value_slot@src/macro/currency.h`).
// Покупательная способность — не вход, а следствие равного бартера: вторая
// корзина собирается из того, что у стороны есть. Время и дорога — не цена,
// а решение модуля «стоит ли» (закон возврата рейса ниже).
//
// ЧТО ЗДЕСЬ УМЕРЛО, И ПОЧЕМУ (ряд M-483; разбор — CANON, там же):
//   · НАЦЕНКА ЗА КРИВУЮ (`trade_price` и пара обёрток): сильный продавал на
//     (1+e) дороже оценки рынка и покупал на (1−e) дешевле — рынок торговал
//     себе в убыток по собственной мерке, и круг «продал — выкупил» качал из
//     него монету при ЛЮБОМ перевесе;
//   · ДВЕ ОЦЕНКИ ОДНОЙ ВЕЩИ и «МНОГО» ПО СЧЁТУ ШТУК: товар шёл по кривой
//     `(спрос+1)/(запас+1)`, платёж им же — по стоимости, и монете с грузом
//     ходока требовались исключения (куча в 1000 крон по кривой стоила
//     стоимость/1001 за штуку);
//   · ЛОТ ОДНОЙ ЦЕНОЙ ПО ПОЛКЕ ПОСЛЕ СДЕЛКИ (`lot_unit_price`) и лот «до
//     среднего геометрического» (`trader_lot`): дробить лот было выгодно;
//   · правило «строка за визит идёт в одну сторону» — заплатка над наценкой.
// Производство по-прежнему читает кривую в обе стороны (`stock_price` ниже):
// его вопрос другой — «стоит ли делать ещё одну», — и он ждёт своего ряда.
#pragma once
#include <algorithm>
#include <cstdint>

namespace sm {

// ── ЯДРО: ОЦЕНКА ─────────────────────────────────────────────────────────
// ВЕЩЬ ДАЁТ ДВА ЧИСЛА — стоимость и массу; СТОРОНА ТРИ — нужду, спину и силу.
// Во сколько сторона ценит ЕДИНИЦУ строки:
//
//     запас НИЖЕ нужды:  стоимость × (нужда + 1) / (запас + 1)
//     иначе:             стоимость × (1 − масса излишка строки / спина)
//
// Верхняя ветвь — кривая S25: нужда поднимает. Нижняя — ЗАВАЛ: «много» мерит
// МАССА против того, что сторона способна унести, и никогда счёт штук. Одна
// формула для города, корована, игрока, монеты, меча и головы; исключений
// «это деньги» или «это ходок» нет — монета лёгкая и потому идёт почти по
// номиналу, руда в малой деревне дешевеет быстро, в большом городе медленно.
// Без нужды (её по умолчанию нет) модель работает одной нижней ветвью.
//
// Спина считается В ШТУКАХ ЭТОЙ СТРОКИ (`спина / масса единицы`, вниз): так
// обе ветви суть отношения счётов, и флоат не нужен. Округление — до
// ближайшего; пол — единица («ничто не бесплатно», S10). Страж области
// определения: счёты не больше 2^30 — произведение 2 × стоимость × счёт
// обязано лечь в int64 (2 × 2^31 × 2^30 = 2^62); мир таких счетов не держит
// (ЗАКОН КЛАМПА, страж).
constexpr std::int64_t unit_worth(std::int64_t base, std::int64_t have,
                                  std::int64_t need, std::int64_t unitMassG,
                                  std::int64_t backG) {
    if (base <= 0) return 0;
    constexpr std::int64_t kCountGuard = std::int64_t(1) << 30;
    const std::int64_t s = have < 0 ? 0 : (have > kCountGuard ? kCountGuard : have);
    const std::int64_t d = need < 0 ? 0 : (need > kCountGuard ? kCountGuard : need);
    if (d > s) return (2 * base * (d + 1) + (s + 1)) / (2 * (s + 1));
    const std::int64_t spare = s - d;
    if (spare == 0) return base;
    const std::int64_t m = unitMassG < 1 ? 1 : unitMassG;
    std::int64_t fits = backG <= 0 ? 0 : backG / m;
    if (fits > kCountGuard) fits = kCountGuard;
    if (spare >= fits) return 1;
    const std::int64_t w = (2 * base * (fits - spare) + fits) / (2 * fits);
    return w < 1 ? 1 : w;
}

// СТРОКА У ОДНОЙ СТОРОНЫ — числа, которые называет звонящий (ядро не знает
// ни сквада, ни модуля):
//   base      — стоимость единицы;
//   net       — запас минус нужда. Отрицательное — непогашенная нужда при
//               пустой полке: приход её ГАСИТ (счёт сквада — пришедшее
//               съедается в уплату, `econ_pay_debt@src/macro/econ_day.cpp`),
//               поэтому одно знаковое число держит обе половины;
//   unitMassG — масса единицы; backG — спина стороны.
struct RowSide {
    std::int64_t base = 0;
    std::int64_t net = 0;
    std::int64_t unitMassG = 1;
    std::int64_t backG = 0;
};
constexpr std::int64_t row_worth_at(const RowSide& r, std::int64_t net) {
    return net >= 0 ? unit_worth(r.base, net, 0, r.unitMassG, r.backG)
                    : unit_worth(r.base, 0, -net, r.unitMassG, r.backG);
}
// «ПО ОДНОЙ С ПЕРЕСЧЁТОМ» (слова владельца о защите от спекуляции): k-я
// единица меряется состоянием ПОСЛЕ своего перехода. Входящая — полкой после
// прихода, уходящая — полкой после ухода; поэтому у одной стороны в один миг
// «взять ещё одну» никогда не дороже, чем «отдать одну».
constexpr std::int64_t worth_arriving(const RowSide& r, std::int64_t k) {
    return row_worth_at(r, r.net + k);
}
constexpr std::int64_t worth_leaving(const RowSide& r, std::int64_t k) {
    return row_worth_at(r, r.net - k);
}

// ── ЯДРО: ЦЕНА ───────────────────────────────────────────────────────────
// Единица едет от отдающего к берущему по цене
//
//     оценка отдающего + его доля × max(0, оценка берущего − оценка отдающего)
//     доля = сила отдающего / (сила отдающего + сила берущего)
//
// Продавца, покупателя, «товара» и «платежа» в законе нет — есть единицы,
// едущие в две стороны, и у каждой стороны одна оценка на все случаи. Отсюда,
// а не из отдельного правила:
//   · где берущий ценит выше отдающего, цена лежит между их оценками, и
//     каждая сторона в выигрыше по собственной мерке;
//   · равные оценки — цена ровно эта оценка, сила тут не при чём;
//   · единица «ПРОТИВ ВЫГОДЫ» (берущий ценит ниже отдающего) взаимно
//     приемлемой цены не имеет вовсе. Она едет только волей того, кто её
//     ПРЕДЛОЖИЛ, и платит за это он: предложивший отдать получает оценку
//     берущего, предложивший взять платит оценку отдающего. Другая сторона
//     остаётся ровно при своём — поэтому круг «отдал n и забрал n» не
//     приносит ничего при ЛЮБЫХ силах (свидетели ниже). Модули сквадов такого
//     не предлагают; предложить может рука игрока.
// Доля — ОТНОШЕНИЕ сил (вердикт владельца 2026-11-12): всегда между нулём и
// единицей без клампа; равные силы делят выгоду пополам. Страж области
// определения: у двух сторон без силы делить нечем — пополам.
constexpr std::int64_t gain_share(std::int64_t gain, std::int64_t giverPower,
                                  std::int64_t takerPower) {
    if (gain <= 0) return 0;
    const std::int64_t p = giverPower < 0 ? 0 : giverPower;
    const std::int64_t q = takerPower < 0 ? 0 : takerPower;
    if (p + q == 0) return gain / 2;
    // Деление прежде умножения: произведение gain × p не обязано лечь в
    // int64, а частное и остаток — ложатся всегда.
    return gain / (p + q) * p + gain % (p + q) * p / (p + q);
}

// КТО ПРЕДЛОЖИЛ ЕДИНИЦЕ ЕХАТЬ — нужен закону в одном месте: у единицы против
// выгоды (см. выше). Где выгода есть, цена от этого не зависит.
enum class Offer : std::uint8_t { Giver, Taker };

// ЦЕНА ЛОТА из `n` единиц — СУММА ПО ЕДИНИЦАМ (вердикт владельца
// 2026-11-12): каждая идёт по своим оценкам после своего перехода, доля
// выгоды берётся от суммы выгод. Дробить лот бессмысленно: части дают в
// сумме не больше целого (округление доли — вниз, в пользу берущего).
constexpr std::int64_t lot_price(const RowSide& giver, const RowSide& taker,
                                 std::int64_t giverPower,
                                 std::int64_t takerPower, std::int64_t n,
                                 Offer offeredBy) {
    std::int64_t floor = 0, gain = 0;
    for (std::int64_t k = 1; k <= n; ++k) {
        const std::int64_t gw = worth_leaving(giver, k);
        const std::int64_t tw = worth_arriving(taker, k);
        if (tw >= gw) {
            floor += gw;
            gain += tw - gw;
        } else {
            floor += offeredBy == Offer::Giver ? tw : gw;
        }
    }
    return floor + gain_share(gain, giverPower, takerPower);
}

// ЛОТ ВЫГОДЫ — наибольшее число единиц (не больше `cap`), КАЖДУЮ из которых
// берущий ценит строго выше отдающего и которые он способен оплатить
// (`wallet` — его покупательная способность). Оценка берущего с каждой
// единицей не растёт, отдающего — не падает, поэтому граница одна и проход
// обрывается на ней. Это ответ ядра на вопрос «сколько тут есть что делить»;
// ВЕЗТИ ЛИ и ПРОДАВАТЬ ЛИ — решает модуль звонящего.
struct LotQuote {
    std::int64_t units = 0;   // сколько единиц едет
    std::int64_t price = 0;   // сколько стоимости отдающий за них получает
};
constexpr LotQuote gainful_lot(const RowSide& giver, const RowSide& taker,
                               std::int64_t giverPower,
                               std::int64_t takerPower, std::int64_t cap,
                               std::int64_t wallet) {
    LotQuote q{};
    std::int64_t floor = 0, gain = 0;
    for (std::int64_t k = 1; k <= cap; ++k) {
        const std::int64_t gw = worth_leaving(giver, k);
        const std::int64_t tw = worth_arriving(taker, k);
        if (tw <= gw) break;
        floor += gw;
        gain += tw - gw;
        const std::int64_t price =
            floor + gain_share(gain, giverPower, takerPower);
        if (price > wallet) break;
        q.units = k;
        q.price = price;
    }
    return q;
}

// СКОЛЬКО ОТДАЮЩИЙ ТЕРЯЕТ ПО СВОЕЙ МЕРКЕ, отдав `n` единиц, и сколько берущий
// приобретает по своей, взяв их, — суммы тех же оценок, что стоят в цене.
// Ими сторона мерит, выросла ли её оценка своего инвентаря (цель сквада).
constexpr std::int64_t lot_loss(const RowSide& giver, std::int64_t n) {
    std::int64_t sum = 0;
    for (std::int64_t k = 1; k <= n; ++k) sum += worth_leaving(giver, k);
    return sum;
}
constexpr std::int64_t lot_take(const RowSide& taker, std::int64_t n) {
    std::int64_t sum = 0;
    for (std::int64_t k = 1; k <= n; ++k) sum += worth_arriving(taker, k);
    return sum;
}

// ВСТРЕЧНАЯ КОРЗИНА — ТЕМ ЖЕ ЗАКОНОМ: сколько единиц строки (не больше
// `avail`) отдающий должен отдать, чтобы их цена покрыла `target`. Едут только
// единицы, которые берущий ценит НЕ НИЖЕ отдающего: отданное из-под своей
// нужды или в забитую спину берущего в корзину не идёт — это не платёж, а
// убыток одной из сторон. Последняя единица вправе переплатить (бартер без
// сдачи — ряд M-487).
// Ровный участок (обе оценки не меняются на всём стаке — монета, малый стак)
// считается без прохода по единицам: цена лота из n единиц там есть
// n × оценка отдающего + доля от n × разницы.
constexpr LotQuote cover_lot(const RowSide& giver, const RowSide& taker,
                             std::int64_t giverPower, std::int64_t takerPower,
                             std::int64_t avail, std::int64_t target) {
    LotQuote q{};
    if (avail <= 0 || target <= 0) return q;
    const std::int64_t gw1 = worth_leaving(giver, 1);
    const std::int64_t tw1 = worth_arriving(taker, 1);
    if (tw1 < gw1) return q;
    if (gw1 == worth_leaving(giver, avail)
        && tw1 == worth_arriving(taker, avail)) {
        const auto price = [&](std::int64_t n) {
            return n * gw1 + gain_share(n * (tw1 - gw1), giverPower, takerPower);
        };
        std::int64_t lo = 1, hi = avail;
        while (lo < hi) {
            const std::int64_t mid = lo + (hi - lo) / 2;
            if (price(mid) >= target) hi = mid; else lo = mid + 1;
        }
        q.units = lo;
        q.price = price(lo);
        return q;
    }
    std::int64_t floor = 0, gain = 0;
    for (std::int64_t k = 1; k <= avail; ++k) {
        const std::int64_t gw = worth_leaving(giver, k);
        const std::int64_t tw = worth_arriving(taker, k);
        if (tw < gw) break;
        floor += gw;
        gain += tw - gw;
        q.units = k;
        q.price = floor + gain_share(gain, giverPower, takerPower);
        if (q.price >= target) break;
    }
    return q;
}

// ТА ЖЕ КОРЗИНА, НО БЕЗ ПЕРЕПЛАТЫ: наибольшее число единиц (не больше
// `avail`), чья цена НЕ ВЫШЕ `budget`. Плательщик зовёт её там, где покрыть
// сумму с запасом значило бы отдать больше, чем он сам ценит взятое.
constexpr LotQuote fit_lot(const RowSide& giver, const RowSide& taker,
                           std::int64_t giverPower, std::int64_t takerPower,
                           std::int64_t avail, std::int64_t budget) {
    LotQuote q{};
    if (avail <= 0 || budget <= 0) return q;
    const std::int64_t gw1 = worth_leaving(giver, 1);
    const std::int64_t tw1 = worth_arriving(taker, 1);
    if (tw1 < gw1) return q;
    if (gw1 == worth_leaving(giver, avail)
        && tw1 == worth_arriving(taker, avail)) {
        const auto price = [&](std::int64_t n) {
            return n * gw1 + gain_share(n * (tw1 - gw1), giverPower, takerPower);
        };
        std::int64_t lo = 0, hi = avail;
        while (lo < hi) {
            const std::int64_t mid = lo + (hi - lo + 1) / 2;
            if (price(mid) <= budget) lo = mid; else hi = mid - 1;
        }
        q.units = lo;
        q.price = lo > 0 ? price(lo) : 0;
        return q;
    }
    std::int64_t floor = 0, gain = 0;
    for (std::int64_t k = 1; k <= avail; ++k) {
        const std::int64_t gw = worth_leaving(giver, k);
        const std::int64_t tw = worth_arriving(taker, k);
        if (tw < gw) break;
        floor += gw;
        gain += tw - gw;
        const std::int64_t price =
            floor + gain_share(gain, giverPower, takerPower);
        if (price > budget) break;
        q.units = k;
        q.price = price;
    }
    return q;
}

// ── СВИДЕТЕЛИ ЯДРА — ПОД КОМПИЛЯТОРОМ ────────────────────────────────────
namespace price_witness {
// Спина в сто штук строки по килограмму; стоимость строки — сто.
inline constexpr std::int64_t kKg = 1000;
inline constexpr std::int64_t kBack = 100 * kKg;
// Купец: десять штук на спине, нужды нет. Рынок голоден: непогашенная нужда
// 40 при пустой полке. Рынок сыт: излишек 25.
inline constexpr RowSide kTrader{100, 10, kKg, kBack};
inline constexpr RowSide kHungry{100, -40, kKg, kBack};
inline constexpr RowSide kSated{100, 25, kKg, kBack};
// Монета: десять граммов, стоимость сто; куча в тысячу штук.
inline constexpr RowSide kCoinPile{100, 1000, 10, kBack};
inline constexpr RowSide kCoinNone{100, 0, 10, kBack};
constexpr RowSide after(RowSide r, std::int64_t delta) {
    r.net += delta;
    return r;
}
// Круг «отдал n — забрал те же n», и обе ноги предлагает первая сторона:
// выручка минус выкуп.
constexpr std::int64_t round_trip(const RowSide& a, const RowSide& b,
                                  std::int64_t aPower, std::int64_t bPower,
                                  std::int64_t n) {
    return lot_price(a, b, aPower, bPower, n, Offer::Giver)
         - lot_price(after(b, n), after(a, -n), bPower, aPower, n,
                     Offer::Taker);
}
}  // namespace price_witness
static_assert(unit_worth(100, 10, 10, 1000, 100000) == 100
              && unit_worth(100, 0, 0, 1000, 100000) == 100,
              "нужда ровно закрыта или нет ни нужды, ни запаса — вещь стоит себя");
static_assert(unit_worth(100, 0, 40, 1000, 100000) == 4100
              && unit_worth(100, 9, 39, 1000, 100000) == 400
              && unit_worth(100, 9, 10, 1000, 100000) == 110,
              "нужда выше запаса поднимает оценку отношением (нужда+1)/(запас+1)");
static_assert(unit_worth(100, 25, 0, 1000, 100000) == 75
              && unit_worth(100, 50, 0, 1000, 100000) == 50
              && unit_worth(100, 60, 10, 1000, 100000) == 50
              && unit_worth(100, 100, 0, 1000, 100000) == 1
              && unit_worth(100, 5000, 0, 1000, 100000) == 1,
              "ЗАВАЛ: излишек роняет оценку долей спины, которую он занимает; "
              "нужда из излишка вычитается; забившее спину идёт по полу");
static_assert(unit_worth(100, 50, 0, 1000, 100000)
              < unit_worth(100, 50, 0, 1000, 1000000)
              && unit_worth(100, 50, 0, 4000, 100000)
              < unit_worth(100, 50, 0, 1000, 100000),
              "«много» мерит МАССА против спины: тот же запас дешевле у стороны "
              "со спиной поменьше и у вещи потяжелее");
static_assert(unit_worth(100, 1000, 0, 10, 100000) == 90
              && unit_worth(100, 1000, 0, 10, 100000000) == 100
              && unit_worth(100, 1000, 0, 1000, 100000) == 1,
              "МОНЕТА — ДЕНЬГИ БЕЗ ТЕГА: тысяча лёгких штук почти не занимает "
              "спину и идёт почти по номиналу, а тысяча тяжёлых — завал");
static_assert(unit_worth(100, 5, 40, 1000, 100000) >= unit_worth(100, 6, 40, 1000, 100000)
              && unit_worth(100, 5, 40, 1000, 100000) <= unit_worth(100, 5, 41, 1000, 100000)
              && unit_worth(100, 30, 0, 1000, 100000) >= unit_worth(100, 31, 0, 1000, 100000)
              && unit_worth(1, 50, 0, 1000, 100000) == 1
              && unit_worth(0, 0, 40, 1000, 100000) == 0
              && unit_worth(100, 5, 0, 1000, 0) == 1,
              "оценка не растёт с запасом и не падает с нуждой; пол — единица; "
              "вещь без стоимости не стоит ничего; у стороны без спины всякий "
              "излишек — завал");
static_assert(worth_arriving(price_witness::kHungry, 1) == 4000
              && worth_arriving(price_witness::kHungry, 40) == 100
              && worth_arriving(price_witness::kHungry, 41) == 99
              && worth_leaving(price_witness::kSated, 25) == 100
              && worth_leaving(price_witness::kSated, 26) == 200,
              "по одной с пересчётом: приход гасит счёт, оценка тает до "
              "стоимости и дальше — в завал; взятое у стороны сверх её излишка "
              "ценится уже как недостающее");
static_assert(gain_share(200, 1, 13) == 14 && gain_share(200, 13, 13) == 100
              && gain_share(200, 22, 13) == 125 && gain_share(200, 510, 13) == 195
              && gain_share(200, 0, 0) == 100 && gain_share(0, 5, 5) == 0
              && gain_share(-7, 5, 5) == 0 && gain_share(200, 5, 0) == 200,
              "доля выгоды — отношение сил: между нулём и всей выгодой без "
              "клампа, равные делят пополам, убыток не делится");
static_assert(gain_share(std::int64_t(1) << 61, 510, 13)
              < (std::int64_t(1) << 61),
              "доля не переполняется на выгоде у края int64");
static_assert(lot_price(price_witness::kCoinPile, price_witness::kCoinNone, 1, 510, 50, Offer::Giver)
              >= 50 * 90
              && lot_price(price_witness::kCoinPile, price_witness::kCoinNone, 510, 1, 50, Offer::Giver)
              <= 50 * 100,
              "монета идёт около номинала при любых силах: куча в десятую "
              "спины — всё, что на ней можно выторговать");
static_assert(lot_price(price_witness::kTrader, price_witness::kHungry, 13, 13, 1, Offer::Giver)
              == 91 + (4000 - 91) / 2
              && lot_price(price_witness::kTrader, price_witness::kHungry, 1, 13, 1, Offer::Giver)
              == 91 + (4000 - 91) / 14
              && lot_price(price_witness::kTrader, price_witness::kHungry, 510, 13, 1, Offer::Giver)
              < 4000
              && lot_price(price_witness::kTrader, price_witness::kHungry, 0, 13, 1, Offer::Giver)
              == 91
              && lot_price(price_witness::kTrader, price_witness::kHungry, 13, 13, 1, Offer::Taker)
              == lot_price(price_witness::kTrader, price_witness::kHungry, 13, 13, 1, Offer::Giver),
              "цена единицы лежит между оценкой отдающего и оценкой берущего; "
              "бессильный получает свою оценку, сильнейший — меньше оценки "
              "рынка; где выгода есть, цена не зависит от того, кто предложил");
static_assert(lot_price(price_witness::kSated, price_witness::kTrader, 13, 13, 1, Offer::Taker)
              > worth_leaving(price_witness::kSated, 1)
              && lot_price(price_witness::kSated, price_witness::kTrader, 13, 13, 1, Offer::Taker)
              < worth_arriving(price_witness::kTrader, 1)
              && lot_price(price_witness::kSated, price_witness::kTrader, 510, 1, 1, Offer::Taker)
              > lot_price(price_witness::kSated, price_witness::kTrader, 1, 510, 1, Offer::Taker),
              "ЗАВАЛ ТЕЧЁТ К СВОБОДНОЙ СПИНЕ: излишек рынка купец ценит выше "
              "рынка, цена лежит между их оценками, и сила рынка её поднимает");
static_assert(lot_price(price_witness::kHungry, price_witness::kTrader, 1, 510, 1, Offer::Taker)
              == worth_leaving(price_witness::kHungry, 1)
              && lot_price(price_witness::kHungry, price_witness::kTrader, 1, 510, 1, Offer::Giver)
              == worth_arriving(price_witness::kTrader, 1)
              && worth_leaving(price_witness::kHungry, 1)
              > worth_arriving(price_witness::kTrader, 1),
              "ПРОТИВ ВЫГОДЫ платит предложивший: взявший у голодного платит "
              "оценку голодного, а отдавший нужное сам получает лишь оценку "
              "берущего — сила тут не решает ничего");
static_assert(price_witness::round_trip(price_witness::kTrader, price_witness::kHungry, 510, 1, 1) <= 0
              && price_witness::round_trip(price_witness::kTrader, price_witness::kHungry, 510, 1, 9) <= 0
              && price_witness::round_trip(price_witness::kTrader, price_witness::kSated, 510, 1, 9) <= 0
              && price_witness::round_trip(price_witness::kSated, price_witness::kTrader, 510, 1, 20) <= 0
              && price_witness::round_trip(price_witness::kTrader, price_witness::kHungry, 1, 510, 9) <= 0
              && price_witness::round_trip(price_witness::kCoinPile, price_witness::kCoinNone, 510, 1, 500) <= 0
              && price_witness::round_trip(price_witness::kTrader, price_witness::kHungry, 13, 13, 9) <= 0,
              "НАСОСА НЕТ: круг «отдал n — забрал n» не приносит ничего при "
              "любом перевесе сил — на голодном и на сытом рынке, на завале и "
              "на монете");
static_assert(lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 10, Offer::Giver)
              >= lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 4, Offer::Giver)
               + lot_price(price_witness::after(price_witness::kTrader, -4),
                           price_witness::after(price_witness::kHungry, 4), 5, 13, 6, Offer::Giver)
              && lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 10, Offer::Giver)
              <= lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 4, Offer::Giver)
               + lot_price(price_witness::after(price_witness::kTrader, -4),
                           price_witness::after(price_witness::kHungry, 4), 5, 13, 6, Offer::Giver)
               + 1,
              "ДРОБИТЬ ЛОТ БЕССМЫСЛЕННО: две части дают не больше целого, и "
              "расходятся с ним не дальше одной монеты округления");
static_assert(gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1 << 30).units == 10
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 7, 1 << 30).units == 7
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 0).units == 0
              && gainful_lot(price_witness::kHungry, price_witness::kTrader, 1, 13, 10, 1 << 30).units == 0,
              "лот выгоды — все единицы, которые берущий ценит выше отдающего, "
              "не больше груза; пустому кошельку продавать нечего, а из-под "
              "чужой нужды брать невыгодно");
static_assert(gainful_lot(price_witness::kSated, price_witness::kTrader, 13, 1, 25, 1 << 30).units > 0
              && gainful_lot(price_witness::kSated, price_witness::kTrader, 13, 1, 25, 1 << 30).units < 25
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1000).units
                 < gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 5000).units
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1000).price <= 1000,
              "завал уходит к свободной спине, пока оценки не сойдутся, — не "
              "весь; покупательная способность берущего режет лот");
static_assert(cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).price >= 500
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).units <= 6
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 3, 5000).units == 3
              && cover_lot(price_witness::kHungry, price_witness::kTrader, 13, 13, 40, 500).units == 0
              && cover_lot(price_witness::kSated, price_witness::kTrader, 13, 13, 25, 300).price >= 300,
              "встречная корзина: монета покрывает сумму малым числом штук, "
              "тощий стак отдаётся весь, нужное себе в платёж не идёт, а "
              "излишек — идёт");
static_assert(fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).price <= 500
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).units >= 5
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 50).units == 0
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 3, 5000).units == 3
              && fit_lot(price_witness::kSated, price_witness::kTrader, 13, 13, 25, 300).price <= 300
              && fit_lot(price_witness::kHungry, price_witness::kTrader, 13, 13, 40, 5000).units == 0,
              "корзина без переплаты: цена не выше бюджета, штука дороже "
              "бюджета не идёт вовсе, нужное себе не идёт");

// ── КРИВАЯ ПРОИЗВОДСТВА: «СТОИТ ЛИ ДЕЛАТЬ ЕЩЁ ОДНУ» ──────────────────────
//     scarcity = (спрос + 1) / (запас + 1)   — в обе стороны, без коридора
//     price    = база × scarcity, пол 1
// ЭТО НЕ ОЦЕНКА В СДЕЛКЕ. Её читают только решения о труде — аукцион рук и
// дневная ведомость (`econ_day.cpp`, `npc_ai.cpp`): полный амбар роняет её
// ниже базы, и руки уходят с дела, которое уже никому не нужно. Сделка её не
// спрашивает вовсе (ядро выше). Останется ли у производства своя величина —
// вопрос ряда к термодинамике (M-440); до него эта дверь не трогается.
float stock_scarcity(int supply, int demandSeason);
int stock_price(int baseValue, int supply, int demandSeason);

// ── ВОЗВРАТ РЕЙСА ДОМОЙ — ПО ПРЕДЕЛЬНОЙ ОТДАЧЕ ───────────────────────────
// Вердикт владельца 2026-11-12 (CANON, блок «AI-МОДУЛИ»), вариант, каким он
// был принят: «после каждой сделки сравнить два числа: прибыль последнего
// плеча за день пути и среднюю прибыль всего рейса за день, считая дорогу
// домой. Ехать дальше, пока первое не меньше второго» — правило оптимального
// фуражира (теорема предельной ценности). Срока и квоты нет: дальний рейс
// дорожает сам, дорогой назад в знаменателе средней.
//
// ДВА КРАЕВЫХ НАБЛЮДЕНИЯ ВЛАДЕЛЬЦА ТОГО ЖЕ ДНЯ, дословно: «1. Проблема
// холодного старта: в самом начале пути на первой сделке среднее нулевое и
// сильно флуктуирует — чтобы не случилось ложного раннего разворота домой до
// того, как торговля вообще развернулась. 2. Проблема дальней сверхприбыли:
// одношаговая жадная оценка рискует быть близорукой — как корован видит
// далёкий жирный рынок (например, через полмира за Шпилями), если следующий
// промежуточный пункт бедный и дешёвый?». КАК ОНИ УЧТЕНЫ В ЗАКОНЕ:
//   · ОКНО В ДВА ПЛЕЧА — ОДНО НА ВЕСЬ РЕЙС. Предельная отдача есть СРЕДНЕЕ
//     ДВУХ ПОСЛЕДНИХ ПЛЕЧ: одиночный замер шумит, и один мёртвый пункт рейс
//     не разворачивает — два подряд разворачивают. То же окно судит и СТАРТ:
//     пока рейс без прибыли (`tripGain` ≤ 0), сравнивать не с чем — средняя
//     нулевая, — и ПЕРВЫЙ пустой рынок рейс проходит насквозь; второй подряд
//     разворачивает его домой. Отдельного правила «поиска» нет, и вечного
//     блуждания нет: груз, не нужный двум рынкам подряд, едет домой, а
//     новый выезд есть новая проба. (Рынок без монеты мёртвым не является:
//     он платит своим излишком — по тому же закону цены, — и рейс везёт его
//     дальше.) ПРИБЫЛЬ РЕЙСА — сумма приростов оценки трюма в его сделках
//     (единая ценомодель): содержание сквада в неё не входит, поэтому ниже
//     нуля она не опускается; страж «прибыль ≤ 0» держит рейс без сделок.
//     ПЕРВАЯ РЕДАКЦИЯ искала без конца — «пока прибыли нет, рейс не судят
//     вовсе» — и ревью назвало цену: груз, не нужный никому, кружил бы до
//     пустого трюма, а рейс, чью прибыль съело сезонное содержание, снова
//     становился ищущим.
//   · ОЖИДАНИЕ — ОТДЕЛЬНЫЙ ВХОД. Отдачу последнего плеча подаёт звонящий, и
//     сегодня её кормит только опыт (сделка минус страх дороги). Знание о
//     дальней сверхприбыли — поле цены, ряд M-479 — встанет ВТОРЫМ источником
//     этого же входа; закон от этого не меняется.
// И ТРЕТЬЕ, тем же днём, дословно: «чем больще сделок тем точне среднее на
// малых числах сделок могут быть флкутуации так что это наверное тоже учесть
// элегатно типа чтобы формаула возрващения уркалпелясль в своей вере стреднего
// с кадым новым рынком»; и уточнение: «можно это вшить ввпорфулу как я вижу
// чтобы была просто зависиомтсь от числа уже совершеннных сделок и это
// усилялло доверие к среднему». КАК УЧТЕНО:
//   · ВЕРА В СРЕДНЮЮ РАСТЁТ С КАЖДОЙ СДЕЛКОЙ, И ВШИТА ОНА В САМУ ФОРМУЛУ.
//     Планка — не вся средняя, а её доля n/(n+1), где n — число сделок, уже
//     совершённых рейсом: после первой сделки средней верят наполовину,
//     после второй — на две трети, и планка сходится к полной средней. Это
//     сжатие оценки по малой выборке (тот же счёт, что у правила Лапласа):
//     замеру верят тем больше, чем их больше. Чисел с потолка в ней нет.
//
//   предельная:  (legRate + prevLegRate) / 2      — монет за `perTicks` пути
//   средняя:     tripGain × perTicks / tripTicksWithHome
//   едем дальше: предельная > средняя × n/(n+1)
//
// Счёт — в целых монетах за `perTicks`. Единица прибыли — монета абсолютной
// стоимости, единица пути — тик мира; закону всё равно, чей это рейс.

// Страж области определения: вера выше 255/256 от полной неотличима, а
// счётчик сделок, уходящий дальше, только раздувал бы произведение.
inline constexpr std::int64_t kTripTrustDeals = 255;

// ОТДАЧА ПЛЕЧА — монет за `perTicks` тиков пути; убыток и нулевой путь дают
// ноль (плечо без прибыли отдачи не несёт).
constexpr std::int64_t leg_rate(std::int64_t gain, std::int64_t legTicks,
                                std::int64_t perTicks) {
    return (gain <= 0 || legTicks <= 0) ? 0 : gain * perTicks / legTicks;
}
constexpr bool trip_goes_on(std::int64_t legRate, std::int64_t prevLegRate,
                            std::int64_t tripGain,
                            std::int64_t tripTicksWithHome,
                            std::int64_t dealsDone, bool firstStop,
                            std::int64_t perTicks) {
    // Рейс без прибыли судить нечем: первый рынок он проходит насквозь,
    // второй подряд разворачивает домой — то же окно в два плеча.
    if (tripGain <= 0) return firstStop;
    if (tripTicksWithHome <= 0) return false;
    const std::int64_t average = tripGain * perTicks / tripTicksWithHome;
    const std::int64_t n =
        dealsDone < 1 ? 1 : (dealsDone > kTripTrustDeals ? kTripTrustDeals
                                                         : dealsDone);
    return (legRate + prevLegRate) * (n + 1) > 2 * average * n;
}
// Свидетели закона — под компилятором. Счёт в сутках по 10 тиков; у первого
// плеча «предыдущим» служит оно само (звонящий подаёт его дважды).
static_assert(leg_rate(100, 10, 10) == 100 && leg_rate(100, 20, 10) == 50
              && leg_rate(0, 10, 10) == 0 && leg_rate(-7, 10, 10) == 0
              && leg_rate(100, 0, 10) == 0,
              "отдача плеча: монеты за сутки пути; убыток и нулевой путь — ноль");
static_assert(trip_goes_on(0, 0, 0, 30, 0, true, 10)
              && trip_goes_on(0, 0, -50, 30, 0, true, 10),
              "ХОЛОДНЫЙ СТАРТ: пустой ПЕРВЫЙ рынок рейс домой не разворачивает");
static_assert(!trip_goes_on(0, 0, 0, 60, 0, false, 10)
              && !trip_goes_on(0, 0, -50, 60, 2, false, 10),
              "два рынка подряд без прибыли разворачивают домой — и на "
              "старте, и когда прибыль рейса съедена: вечного поиска нет");
static_assert(trip_goes_on(100, 100, 100, 20, 1, true, 10),
              "первое прибыльное плечо едет дальше");
static_assert(trip_goes_on(0, 100, 100, 40, 1, false, 10),
              "ОДИН МЁРТВЫЙ ПУНКТ после хорошего плеча рейс проходит насквозь");
static_assert(!trip_goes_on(0, 0, 100, 60, 1, false, 10),
              "ДВА МЁРТВЫХ ПУНКТА ПОДРЯД разворачивают домой");
static_assert(trip_goes_on(40, 40, 100, 20, 1, false, 10)
              && trip_goes_on(40, 40, 100, 20, 3, false, 10)
              && !trip_goes_on(40, 40, 100, 20, 4, false, 10),
              "ВЕРА В СРЕДНЮЮ РАСТЁТ С КАЖДОЙ СДЕЛКОЙ: плечи на четыре пятых "
              "средней рейс терпит три сделки, а на четвёртой разворачивается");
static_assert(trip_goes_on(100, 20, 100, 20, 2, false, 10)
              && trip_goes_on(20, 100, 100, 20, 2, false, 10)
              && !trip_goes_on(20, 20, 100, 20, 2, false, 10),
              "окно симметрично: слабое плечо после сильного и сильное после "
              "слабого судятся одинаково, а два слабых подряд разворачивают");
static_assert(trip_goes_on(60, 60, 100, 20, kTripTrustDeals, false, 10)
              && !trip_goes_on(40, 40, 100, 20, kTripTrustDeals, false, 10),
              "при полной вере: плечи быстрее средней — дальше, медленнее — "
              "домой");
static_assert(trip_goes_on(100, 100, 300, 31, kTripTrustDeals, false, 10)
              && !trip_goes_on(100, 100, 300, 29, kTripTrustDeals, false, 10),
              "те же плечи: чем дальше дом, тем ниже средняя — дальний рейс "
              "оправдан, пока плечи кормят");
static_assert(trip_goes_on(40, 40, 100, 20, 1 << 20, false, 10)
              == trip_goes_on(40, 40, 100, 20, kTripTrustDeals, false, 10),
              "счёт сделок за стражем ответа не меняет");

// A settlement's SEASON demand for an item (CANON S10 «спрос читается из
// ДОЛГА»): прямая часть = ОСТАТОК СЧЁТА места по этой строке лестницы —
// непогашенная нужда и есть спрос, — PLUS the derived demand of every
// recipe ITS HANDS can run (a city that weaves demands fibre; a place whose
// tailoring rank is zero does not — owner track 2026-08-30, and since
// 2026-09-18 the gate is the place's own ANKETA, not its kind).
//
// `needDebt` — счёт места (Landmark::needDebt). nullptr = читателя без
// счёта (снимок чужого дома, фикстура) — прямая часть честно падает на
// старую лестницу населения × сезон.
//
// `store` — ЭТОГО места склад: производная половина спроса гасится запасом
// ВЫХОДА (владелец 2026-09-18, «смотреть и на сезон, и на склад текущий»):
// зерно нужно только на НЕДОПЕЧЁННЫЙ остаток сезонной нужды хлеба — полный
// амбар не хочет зерна, пустой хочет в полную силу (выгода караванов с
// зерном, ради которой производный спрос строился, живёт ровно там, где
// она настоящая). nullptr = спрос без неттинга — для читателя, у которого
// есть только классовый снимок чужого дома, а не склад (память крю).
//
// Пол спроса («нулевого спроса не бывает», владелец 2026-09-18) выведен из
// слабейшей нужды лестницы, той же сезонной меркой.
struct Skills;
struct Inventory;
// ОРДИНАЛ КАТАЛОГА, А НЕ СТРОКА (ЗАКОН СЛОВАРЯ И ОРДИНАЛА): эта дверь стоит
// внутри ведомости (места × товары × день) и внутри каждого рыночного цикла,
// а рекурсия по рецептам множит её на глубину 4 — строковый ключ здесь был
// самым глубоким потребителем хеша в симуляции (замер PMU 2026-09-24).
int season_demand_for(int itemIdx, const std::int32_t* needDebt,
                      int population, const Skills& hands,
                      const Inventory* store);

// ── МОСТ «НУЖДА → ЯДРО»: СТРОКА СТОРОНЫ ЧИСЛАМИ ──────────────────────────
// Ядро выше берёт числа и не знает, откуда они; этот мост — единственное
// место, где нужда стороны становится входом ядра. НУЖДА — ЧИСЛО, КОТОРОЕ
// ЗАПИСАЛ МОДУЛЬ СТОРОНЫ: счёт сквада `needDebt` (у города его пишет окно
// сезона). Мост читает ЕГО И ТОЛЬКО ЕГО. У ходока счёт стоит нулями — нужды
// нет, оценку ведёт одна масса против спины.
// ЗДЕСЬ СТОЯЛА ЕЩЁ И НУЖДА ПОД РЕЦЕПТ, ВЫЧИСЛЯЕМАЯ НА ЛЕТУ ИЗ ЗАПАСА ДРУГОЙ
// СТРОКИ (`season_demand_for` выше), и это был насос: оценка сырья зависела
// от запаса выхода, поэтому купец продавал деревне железо с премией, следом
// инструменты — нужда в железе исчезала, — и выкупал железо из завала
// дешевле (ревью дифа 2026-11-12: −143 деревне за визит). Оценка строки
// обязана зависеть только от самой строки; нужду в сырье модуль места
// назовёт числом в счёт — ряд M-488. Решения о труде читают
// `season_demand_for` по-прежнему (ряд M-486).
// `base` — стоимость единицы (звонящий знает, спрашивает ли он строку
// каталога или стак с аффиксами); `unitMassG` и `backG` — масса единицы и
// спина стороны. Сквад целиком читает дверь `side_row@src/macro/deal.h`.
// ДОЛГ РАЗДЕЛА НАЗВАН (ряд M-488): счёт длиной в словарь товаров — нужда в
// вещи вне словаря (зелье раненого героя) сегодня невыразима.
RowSide row_side(int itemIdx, std::int64_t base, const std::int32_t* needDebt,
                 const Inventory& inv, std::int64_t unitMassG,
                 std::int64_t backG);

} // namespace sm
