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
//   · правило «строка за визит идёт в одну сторону» — заплатка над наценкой;
//   · ОКРУГЛЕНИЕ КАЖДОЙ ЕДИНИЦЫ ДО КВАНТА (`unit_worth`) и оценка уходящей
//     единицы «по состоянию после ухода» (ряд M-490): первое прятало завал
//     меньше шага оценки и держало торговлю между счетами сезона, второе
//     делало из одной единицы две оценки — «взять» и «отдать» расходились на
//     шаг кривой;
//   · остаток доли «всегда берущему»: покупая по одной, берущий не платил
//     отдающему его долю вовсе.
//   · КРИВАЯ ПРОИЗВОДСТВА `(спрос+1)/(запас+1)` с производной нуждой под
//     рецепт (ряд M-488): вторая оценка той же вещи у той же стороны.
// Что и сколько крафтить, модуль поселения решает той же оценкой запаса (ряд
// M-486); доска «принеси» и порядок груза дани читают нужду тем же мостом,
// что и сделка (`side_row@src/macro/deal.h`).
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

// СТРОКА У ОДНОЙ СТОРОНЫ — числа, которые называет звонящий (ядро не знает
// ни сквада, ни модуля):
//   base      — стоимость единицы, в квантах стоимости;
//   net       — запас минус нужда. Отрицательное — непогашенная нужда при
//               пустой полке: приход её ГАСИТ (счёт сквада — пришедшее
//               съедается в уплату, `econ_pay_debt@src/macro/econ_day.cpp`),
//               поэтому одно знаковое число держит обе половины, а верхняя
//               ветвь при пустой полке есть `стоимость × (недостающее + 1)`;
//   unitMassG — масса единицы; backG — спина стороны.
struct RowSide {
    std::int64_t base = 0;
    std::int64_t net = 0;
    std::int64_t unitMassG = 1;
    std::int64_t backG = 0;
};

// ЕДИНИЦА ЦЕНИТСЯ ПО СОСТОЯНИЮ, В КОТОРОМ ОНА У СТОРОНЫ ЕСТЬ. Единица,
// приводящая `net` к числу j (j-я по счёту), стоит стороне:
//
//     j ≤ 0        стоимость × (1 − j)        нужда: недостаёт −j штук и её самой
//     0 < j ≤ J    стоимость × (F − j) / F    завал: F — спина в штуках строки
//     j > J        1                          пол («ничто не бесплатно», S10)
//
// F = спина / масса единицы (вниз): обе ветви суть отношения счётов, флоат не
// нужен. J — последняя единица излишка, чья оценка не ниже пола. Оценка не
// растёт с j на всей оси.
//
// СТРАЖ ОБЛАСТИ ОПРЕДЕЛЕНИЯ (ЗАКОН КЛАМПА, страж — охраняет формулу, не
// поведение). Положения запаса и нужды — в пределах ±2^31 штук (счётчик стака
// и счёт нужды — int32), стоимость единицы — до 2^29, спина в штуках — до
// 2^31. В этой области ни одно произведение и ни одна сумма ядра не покидают
// int64. Нужда растит оценку ОТРЕЗКА квадратом своей глубины, поэтому сумма
// нужды насыщается на 2^60: при стоимости 12 800 (наибольшая в каталоге) —
// начиная с нужды в 13 миллионов штук, которых сезонный счёт города не
// держит. Насыщается СУММА ОТРЕЗКА: одна единица и малый лот считаются точно
// при любой глубине нужды и согласны с точным сравнением единиц ниже. У
// самого насыщения (отрезок дороже 2^60) части лота перестают складываться в
// целое — это край стража, а не поведение мира.
namespace price_detail {
inline constexpr std::int64_t kCountGuard = std::int64_t(1) << 31;
inline constexpr std::int64_t kBaseGuard = std::int64_t(1) << 29;
inline constexpr std::int64_t kNeedGuard = std::int64_t(1) << 60;

constexpr std::int64_t at_guard(std::int64_t count) {
    return count < -kCountGuard ? -kCountGuard
                                : (count > kCountGuard ? kCountGuard : count);
}
constexpr std::int64_t row_base(const RowSide& r) {
    return r.base < 0 ? 0 : (r.base > kBaseGuard ? kBaseGuard : r.base);
}
// Спина стороны в ШТУКАХ этой строки.
constexpr std::int64_t row_fits(const RowSide& r) {
    if (r.backG <= 0) return 0;
    const std::int64_t f = r.backG / (r.unitMassG < 1 ? 1 : r.unitMassG);
    return f > kCountGuard ? kCountGuard : f;
}
// Последняя единица излишка, чья точная оценка не ниже пола:
// стоимость × (F − j) ≥ F  ⇔  j ≤ F − ⌈F / стоимость⌉.
constexpr std::int64_t glut_span(std::int64_t base, std::int64_t fits) {
    return fits <= 0 ? 0 : fits - (fits + base - 1) / base;
}
// 1 + 2 + … + m.
constexpr std::int64_t triangle(std::int64_t m) { return m * (m + 1) / 2; }

// ТОЧНАЯ оценка единицы — дробь; до кванта она здесь НЕ округляется.
struct Worth {
    std::int64_t num = 0;
    std::int64_t den = 1;
};
constexpr Worth edge_worth(const RowSide& r, std::int64_t j) {
    const std::int64_t base = row_base(r);
    if (base == 0) return {0, 1};
    if (j <= 0) return {base * (-at_guard(j) + 1), 1};
    const std::int64_t fits = row_fits(r);
    if (j > glut_span(base, fits)) return {1, 1};
    return {base * (fits - j), fits};
}

// Оценка ИЗЛИШКА от нуля до `spare` штук: по завалу —
// Σ стоимость × (F − j)/F = стоимость × t − стоимость × t(t+1) / (2F), до
// ближайшего кванта (вычитаемое делится с остатком, чтобы произведение не
// покидало int64); забившее спину — по единице за штуку.
constexpr std::int64_t glut_worth(std::int64_t base, std::int64_t fits,
                                  std::int64_t spare) {
    const std::int64_t span = glut_span(base, fits);
    const std::int64_t t = spare < span ? spare : span;
    std::int64_t worth = spare - t;
    if (t > 0) {
        const std::int64_t tri = triangle(t);
        const std::int64_t whole = tri / fits, rest = tri % fits;
        worth += base * t - (base * whole + (2 * base * rest + fits) / (2 * fits));
    }
    return worth;
}

// Оценка ОТРЕЗКА запаса — единиц с (lo+1)-й по hi-ю. По нужде —
// Σ стоимость × (1 − j) = стоимость × (сумма глубин нужды), точно; по излишку —
// разность двух оценок излишка.
constexpr std::int64_t span_worth(const RowSide& r, std::int64_t lo,
                                  std::int64_t hi) {
    const std::int64_t base = row_base(r);
    lo = at_guard(lo);
    hi = at_guard(hi);
    if (base == 0 || hi <= lo) return 0;
    std::int64_t worth = 0;
    if (lo < 0) {
        const std::int64_t depths = triangle(-lo) - triangle(hi < 0 ? -hi : 0);
        worth = depths > kNeedGuard / base ? kNeedGuard : base * depths;
    }
    if (hi > 0) {
        const std::int64_t fits = row_fits(r);
        worth += glut_worth(base, fits, hi)
               - glut_worth(base, fits, lo > 0 ? lo : 0);
    }
    return worth;
}

// ТОЧНОЕ СРАВНЕНИЕ ДВУХ ПРОИЗВЕДЕНИЙ a × b и c × d (все неотрицательны):
// −1, 0, +1. Произведение двух int64 в int64 не ложится, поэтому оно считается
// в две половины по 64 бита (`__int128` у MSVC нет). Этим сравниваются дроби
// — a/c против d/b — без деления и без переполнения.
struct Wide {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};
constexpr Wide wide_mul(std::uint64_t a, std::uint64_t b) {
    constexpr std::uint64_t kLow = 0xffffffffu;
    const std::uint64_t a0 = a & kLow, a1 = a >> 32;
    const std::uint64_t b0 = b & kLow, b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0;
    const std::uint64_t mid = (p00 >> 32) + (p01 & kLow) + (p10 & kLow);
    return {a1 * b1 + (p01 >> 32) + (p10 >> 32) + (mid >> 32),
            (mid << 32) | (p00 & kLow)};
}
constexpr int wide_cmp(std::int64_t a, std::int64_t b, std::int64_t c,
                       std::int64_t d) {
    const Wide x = wide_mul(std::uint64_t(a), std::uint64_t(b));
    const Wide y = wide_mul(std::uint64_t(c), std::uint64_t(d));
    if (x.hi != y.hi) return x.hi < y.hi ? -1 : 1;
    return x.lo == y.lo ? 0 : (x.lo < y.lo ? -1 : 1);
}

// СИЛЫ ДВУХ СТОРОН. Страж области определения: сила — до 2^30 (итог листа —
// сотни); у двух сторон без силы делить нечем — пополам.
struct Powers {
    std::int64_t standing = 1;
    std::int64_t visitor = 1;
};
constexpr Powers powers(std::int64_t standingPower, std::int64_t visitorPower) {
    constexpr std::int64_t kPowerGuard = std::int64_t(1) << 30;
    const std::int64_t s = standingPower < 0 ? 0
        : (standingPower > kPowerGuard ? kPowerGuard : standingPower);
    const std::int64_t v = visitorPower < 0 ? 0
        : (visitorPower > kPowerGuard ? kPowerGuard : visitorPower);
    return s + v == 0 ? Powers{1, 1} : Powers{s, v};
}
}  // namespace price_detail

// ОЦЕНКА ЗАПАСА — ФУНКЦИЯ СОСТОЯНИЯ, И ДО КВАНТА ОКРУГЛЯЕТСЯ ОНА, А НЕ
// ЕДИНИЦА (ряд M-490). Владелец 2026-11-13, дословно: «да это же просто
// округление до ближайшего целокго числа квантово (я даже готов просто до
// меньшего/большего или любого где ращзница один квант если это технчиески
// оправано) просто универсально минимально типа доли там минмиалнього квант
// считать это же глупость». Стоимость — минимальный квант, долей у него нет.
//
// Но округлять до кванта КАЖДУЮ единицу значит повторять ошибку на каждой
// штуке: у строки стоимостью 5 оценка шагала 5, 4, 3, 2, 1, первый шаг вниз
// наступал на десятой доле спины, и завал поменьше оценке был не виден вовсе
// — между счетами сезона сделки стояли. Поэтому до кванта округляется СУММА:
// во сколько сторона ценит весь запас строки при данном `net` — замкнутой
// формой, до ближайшего. Лот есть РАЗНОСТЬ двух таких сумм, единица — разность
// соседних. Ошибка на лоте любой длины не больше кванта и не копится.
//
// ЧТО ИЗ ЭТОГО СЛЕДУЕТ САМО, без отдельного правила:
//   · единица стоит стороне ОДИНАКОВО, приходит она или уходит (одна оценка
//     на все случаи): круг «взял — вернул» возвращает оценку запаса ровно в
//     прежнее число;
//   · суммы частей лота складываются в сумму целого точно — дробить нечего;
//   · оценка единицы в квантах — пол или потолок точной, разница один квант;
//   · цель сквада «растить свою оценку своего инвентаря» есть буквально это
//     число, сложенное по строкам.
// А РЕШЕНИЕ «ценит ли выше» принимается ТОЧНО, сравнением дробей
// (`gain_units` ниже), а не по округлённым единицам: иначе шаг округления
// выдавал бы себя за выгоду.
constexpr std::int64_t stock_worth(const RowSide& r, std::int64_t net) {
    return net >= 0 ? price_detail::span_worth(r, 0, net)
                    : -price_detail::span_worth(r, net, 0);
}

// СКОЛЬКО ОТДАЮЩИЙ ТЕРЯЕТ ПО СВОЕЙ МЕРКЕ, отдав `n` единиц, и сколько берущий
// приобретает по своей, взяв их, — отрезки той же оценки запаса. Ими сторона
// мерит, выросла ли её оценка своего инвентаря (цель сквада).
constexpr std::int64_t lot_loss(const RowSide& giver, std::int64_t n) {
    const std::int64_t at = price_detail::at_guard(giver.net);
    return n <= 0 ? 0
                  : price_detail::span_worth(giver, at - price_detail::at_guard(n), at);
}
constexpr std::int64_t lot_take(const RowSide& taker, std::int64_t n) {
    const std::int64_t at = price_detail::at_guard(taker.net);
    return n <= 0 ? 0
                  : price_detail::span_worth(taker, at, at + price_detail::at_guard(n));
}
// Оценка k-й единицы лота в квантах — «по одной с пересчётом» (слова
// владельца о защите от спекуляции, CANON «НАСОС МОНЕТЫ»: «как по додной с
// учётом пересчёта»): каждая меряется своим состоянием, а не полкой до
// сделки. Ими закон единицы называют свидетели; решения о лоте идут через
// `gain_units`, суммы — через `lot_loss` и `lot_take`.
constexpr std::int64_t worth_arriving(const RowSide& r, std::int64_t k) {
    return lot_take(r, k) - lot_take(r, k - 1);
}
constexpr std::int64_t worth_leaving(const RowSide& r, std::int64_t k) {
    return lot_loss(r, k) - lot_loss(r, k - 1);
}

// СКОЛЬКО ТУТ ЕСТЬ ЧТО ДЕЛИТЬ: наибольшее k (не больше `cap`), при котором
// k-ю единицу берущий ценит ВЫШЕ (`strict`) либо НЕ НИЖЕ, чем отдающий,
// помноженный на наценку стоящего (1 + его доля) — с какой бы стороны
// стоящий ни стоял, порог один. Силы (0, 1) дают долю ноль: голое сравнение
// двух оценок — так судит семья (дом и его ребёнок) и корзина оплаты. Оценка
// берущего с каждой единицей не растёт, отдающего — не падает, поэтому
// граница одна и ищется делением пополам; сравнение точное.
constexpr std::int64_t gain_units(const RowSide& giver, const RowSide& taker,
                                  std::int64_t cap, bool strict,
                                  std::int64_t standingPower,
                                  std::int64_t visitorPower) {
    using namespace price_detail;
    const Powers p = powers(standingPower, visitorPower);
    const std::int64_t sum = p.standing + p.visitor, full = sum + p.standing;
    const std::int64_t from = at_guard(giver.net), to = at_guard(taker.net);
    const auto gainful = [&](std::int64_t k) {
        const Worth tw = edge_worth(taker, to + k);
        const Worth gw = edge_worth(giver, from - k + 1);
        // tw > gw × full/sum  ⇔  tw.num × (gw.den × sum) > gw.num × (tw.den × full)
        const int c = wide_cmp(tw.num, gw.den * sum, gw.num, tw.den * full);
        return strict ? c > 0 : c >= 0;
    };
    std::int64_t lo = 0;
    std::int64_t hi = cap < 0 ? 0 : (cap > kCountGuard ? kCountGuard : cap);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (gainful(mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

// ВЫБОР ПО МАССЕ: выгода лота на грамм лота у первого выше, чем у второго
// (выгоды неотрицательны, массы положительны). Этим модуль решает, что брать
// первым, пока спина свободна. Мерить выгоду ЕДИНИЦЕЙ нельзя — завал меньше
// шага оценки на одной штуке не виден; меряется лот. Сравнение точное.
constexpr bool gain_denser(std::int64_t gainA, std::int64_t massGA,
                           std::int64_t gainB, std::int64_t massGB) {
    // Страж области определения: убыток не выгода, вещь без массы весит квант.
    const auto gain = [](std::int64_t v) { return v < 0 ? std::int64_t(0) : v; };
    const auto mass = [](std::int64_t v) { return v < 1 ? std::int64_t(1) : v; };
    return price_detail::wide_cmp(gain(gainA), mass(massGB), gain(gainB),
                                  mass(massGA)) > 0;
}

// ── ЯДРО: ЦЕНА ───────────────────────────────────────────────────────────
// В сделке есть СТОЯЩИЙ (к нему пришли) и ПРИШЕДШИЙ (он сделку предложил).
// Цену называет оценка СТОЯЩЕГО, и закон один:
//
//     взять у стоящего:   его оценка × (1 + его доля)
//     отдать стоящему:    по его оценке
//     доля стоящего = его сила / (его сила + сила пришедшего)
//
// Замысел — словами владельца (2026-11-13, дословно — CANON S25, «ВЕРДИКТ
// 2026-11-13 — ФОРМА ЦЕНЫ»): «если слабай харизма и скил то почти всегда
// тороговля в убыток это дефолт это норм но если гдето есть большой градиент
// типа в одном городе оч дешево типа ломится рынок а на другом краю страшный
// дефицит то тогда прибыль получает даже полный 0 в торговле … в пределе такой
// градиент цен будет перебивать пропрость в скиле … а всреднем скил решает как
// раз таки на сделаках где разница не велика то есть смылс такой что как в
// любой торговле купил где дешевле продал где дороже». Из формулы это выходит
// само:
//   · доля лежит между нулём (пришёл мастер) и единицей (пришёл полный ноль),
//     поэтому купить стоит от ×1 до ×2 оценки стоящего, а продать — от ÷1 до
//     ÷2. Заваленный город дёшев для всех, голодный дорог для всех;
//   · рейс «купил — привёз — продал» прибылен, когда оценки двух рынков
//     расходятся больше чем в (1 + доля)² раз: у мастера порог 1, у полного
//     нуля — 4. Разница в порядок перебивает любой скилл, на малой разнице
//     решает он, а без разницы слабый в убытке;
//   · СТОЯЩИЙ НИКОГДА НЕ ТЕРЯЕТ ПО СВОЕЙ ОЦЕНКЕ: за отданное он получает не
//     меньше, чем ценил его сам. Поэтому круг «взял и вернул» пришедшему в
//     убыток при любых силах, и мастер нуба не обувает;
//   · неделимый остаток доли достаётся стоящему (доля — вверх), а дробить лот
//     пришедшему в убыток: каждая часть стоит ему своего остатка;
//   · ОЦЕНКА ПРИШЕДШЕГО В ЦЕНУ НЕ ВХОДИТ. Она решает, едет ли вещь и сколько
//     её едет: пришедший берёт, пока сам ценит единицу выше её цены.
// ЗДЕСЬ СТОЯЛО «сила делит разницу двух оценок» (2026-11-12): слабый платил
// почти СВОЮ оценку, заваленный город для него дёшев не был, прибыль рейса
// равнялась «доля × градиент», и у полного нуля была нулём при любом
// градиенте (ряд M-492). Формы со степенью или отношением оценок перебраны и
// отклонены: «лот — сумма по единицам, округлить один раз» выживает только у
// форм, ЛИНЕЙНЫХ по оценкам.

// КТО В ЭТОЙ КОРЗИНЕ СТОЯЩИЙ — тот, кто отдаёт, или тот, кто берёт.
enum class Standing : std::uint8_t { Giver, Taker };

// ДОЛЯ СТОЯЩЕГО от суммы `worth` — ВВЕРХ: неделимый остаток его. Деление
// прежде умножения — произведение не обязано лечь в int64, частное и остаток
// ложатся всегда.
constexpr std::int64_t standing_share(std::int64_t worth,
                                      std::int64_t standingPower,
                                      std::int64_t visitorPower) {
    if (worth <= 0) return 0;
    const price_detail::Powers p =
        price_detail::powers(standingPower, visitorPower);
    const std::int64_t sum = p.standing + p.visitor;
    const std::int64_t tail = worth % sum * p.standing;
    return worth / sum * p.standing + tail / sum + (tail % sum != 0 ? 1 : 0);
}

// ЦЕНА ВЗЯТОГО: во что пришедшему обходится то, что стоящий ценит в `worth`.
constexpr std::int64_t ask_price(std::int64_t worth, std::int64_t standingPower,
                                 std::int64_t visitorPower) {
    return worth <= 0
        ? 0 : worth + standing_share(worth, standingPower, visitorPower);
}
// ОБРАТНЫЙ ВОПРОС: стоящему дали на `credit` его оценки — на сколько своей
// оценки он отдаст взамен. Наибольшее `worth`, чья цена взятия не выше
// `credit`: credit × (сила обоих) / (сила обоих + сила стоящего), вниз.
constexpr std::int64_t ask_reach(std::int64_t credit, std::int64_t standingPower,
                                 std::int64_t visitorPower) {
    if (credit <= 0) return 0;
    const price_detail::Powers p =
        price_detail::powers(standingPower, visitorPower);
    const std::int64_t sum = p.standing + p.visitor;
    const std::int64_t full = sum + p.standing;
    return credit / full * sum + credit % full * sum / full;
}

// ЦЕНА ЛОТА, ВЗЯТОГО У СТОЯЩЕГО: `n` единиц его строки. Что стоящему ОТДАЛИ,
// он ценит своей `lot_take` — отдельной двери у этого нет.
constexpr std::int64_t take_price(const RowSide& standing, std::int64_t n,
                                  std::int64_t standingPower,
                                  std::int64_t visitorPower) {
    return ask_price(lot_loss(standing, n), standingPower, visitorPower);
}

// ── ДВЕРИ ЛОТА ───────────────────────────────────────────────────────────
// Сколько единиц едет — решает ПРИШЕДШИЙ своей оценкой (`gain_units` с
// наценкой: берущий ценит единицу больше чем в (1 + доля) раз выше
// отдающего); сколько за них причитается — называет оценка СТОЯЩЕГО. ВЕЗТИ ЛИ
// и ПРОДАВАТЬ ЛИ — решает модуль звонящего.
struct LotQuote {
    std::int64_t units = 0;   // сколько единиц едет
    std::int64_t price = 0;   // сколько это стоит в оценке стоящего
};

// ВЗЯТЬ У СТОЯЩЕГО: наибольшее число единиц (не больше `cap`), каждую из
// которых пришедший ценит выше её цены и которые он способен оплатить
// (`credit` — на сколько оценки стоящего он может дать взамен). Цена — цена
// взятия.
constexpr LotQuote take_lot(const RowSide& standing, const RowSide& visitor,
                            std::int64_t standingPower,
                            std::int64_t visitorPower, std::int64_t cap,
                            std::int64_t credit) {
    std::int64_t lo = 0;
    std::int64_t hi = gain_units(standing, visitor, cap, true, standingPower,
                                 visitorPower);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (take_price(standing, mid, standingPower, visitorPower) <= credit)
            lo = mid;
        else hi = mid - 1;
    }
    return lo > 0
        ? LotQuote{lo, take_price(standing, lo, standingPower, visitorPower)}
        : LotQuote{};
}

// ОТДАТЬ СТОЯЩЕМУ: наибольшее число единиц (не больше `cap`), каждую из
// которых стоящий ценит выше пришедшего с наценкой и за которые он способен
// расплатиться (`reach` — на сколько своей оценки он может отдать взамен).
// Цена — на сколько оценки стоящего пришедший вправе взять за них.
constexpr LotQuote give_lot(const RowSide& visitor, const RowSide& standing,
                            std::int64_t standingPower,
                            std::int64_t visitorPower, std::int64_t cap,
                            std::int64_t reach) {
    const auto price = [&](std::int64_t n) {
        return ask_reach(lot_take(standing, n), standingPower, visitorPower);
    };
    std::int64_t lo = 0;
    std::int64_t hi = gain_units(visitor, standing, cap, true, standingPower,
                                 visitorPower);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (price(mid) <= reach) lo = mid;
        else hi = mid - 1;
    }
    return lo > 0 ? LotQuote{lo, price(lo)} : LotQuote{};
}

// ── КОРЗИНА ОПЛАТЫ — ТЕМ ЖЕ ЗАКОНОМ, СТРОКА ЗА СТРОКОЙ ───────────────────
// Едут только единицы, которые берущий ценит НЕ НИЖЕ отдающего: отданное
// из-под своей нужды или в забитую спину берущего в корзину не идёт — это не
// платёж, а убыток одной из сторон.
//
// ПЛАТИТ ПРИШЕДШИЙ. Его единицы стоящий засчитывает по своей оценке; покрыть
// надо `target`. `cover_lot` — наименьшее число единиц, покрывающее цель (не
// набралось — всё, что годится); последняя единица вправе переплатить (бартер
// без сдачи — ряд M-487). `fit_lot` — без переплаты: наибольшее число единиц,
// засчитанных не выше `budget`.
constexpr LotQuote cover_lot(const RowSide& visitor, const RowSide& standing,
                             std::int64_t avail, std::int64_t target) {
    if (target <= 0) return {};
    std::int64_t lo = 1, hi = gain_units(visitor, standing, avail, false, 0, 1);
    if (hi <= 0) return {};
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo) / 2;
        if (lot_take(standing, mid) >= target) hi = mid;
        else lo = mid + 1;
    }
    return LotQuote{lo, lot_take(standing, lo)};
}
constexpr LotQuote fit_lot(const RowSide& visitor, const RowSide& standing,
                           std::int64_t avail, std::int64_t budget) {
    if (budget <= 0) return {};
    std::int64_t lo = 0, hi = gain_units(visitor, standing, avail, false, 0, 1);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (lot_take(standing, mid) <= budget) lo = mid;
        else hi = mid - 1;
    }
    return lo > 0 ? LotQuote{lo, lot_take(standing, lo)} : LotQuote{};
}
// ПЛАТИТ СТОЯЩИЙ. Его единицы обходятся пришедшему в цену взятия, и стоящий
// не отдаёт больше, чем на `budget`: наибольшее число единиц, чья цена
// взятия не выше бюджета. Переплаты у стоящего не бывает.
constexpr LotQuote reach_lot(const RowSide& standing, const RowSide& visitor,
                             std::int64_t standingPower,
                             std::int64_t visitorPower, std::int64_t avail,
                             std::int64_t budget) {
    if (budget <= 0) return {};
    std::int64_t lo = 0, hi = gain_units(standing, visitor, avail, false, 0, 1);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (take_price(standing, mid, standingPower, visitorPower) <= budget)
            lo = mid;
        else hi = mid - 1;
    }
    return lo > 0
        ? LotQuote{lo, take_price(standing, lo, standingPower, visitorPower)}
        : LotQuote{};
}

// ── СВИДЕТЕЛИ ЯДРА — ПОД КОМПИЛЯТОРОМ ────────────────────────────────────
namespace price_witness {
// Спина в сто штук строки по килограмму; стоимость строки — сто. На этих
// числах оценка единицы излишка есть целое `100 − j`: якоря ниже посчитаны
// рукой по формуле закона, а не кодом ядра.
inline constexpr std::int64_t kKg = 1000;
inline constexpr std::int64_t kBack = 100 * kKg;
// Купец: десять штук на спине, нужды нет. Рынок голоден: непогашенная нужда
// 40 при пустой полке. Рынок сыт: излишек 25. Пусто: ни нужды, ни запаса.
inline constexpr RowSide kTrader{100, 10, kKg, kBack};
inline constexpr RowSide kHungry{100, -40, kKg, kBack};
inline constexpr RowSide kSated{100, 25, kKg, kBack};
inline constexpr RowSide kEmpty{100, 0, kKg, kBack};
// Монета: десять граммов, стоимость сто; куча в тысячу штук.
inline constexpr RowSide kCoinPile{100, 1000, 10, kBack};
inline constexpr RowSide kCoinNone{100, 0, 10, kBack};
// ДЕШЁВАЯ СТРОКА (стоимость 5 — еда, дерево, камень, глина, волокно): дом со
// спиной в полмиллиона штук завален на двадцатую её долю, ребёнок со спиной в
// тысячу штук пуст.
inline constexpr RowSide kCheapHome{5, 25000, kKg, 500000 * kKg};
inline constexpr RowSide kCheapChild{5, 0, kKg, 1000 * kKg};
constexpr RowSide after(RowSide r, std::int64_t delta) {
    r.net += delta;
    return r;
}
// ЗАВАЛ ПО ПОЛУ: руда стоимостью 15 по четыре килограмма, у рынка её
// четыреста штук при спине на шестнадцать — каждую он ценит в единицу. У
// пустого купца спина на сто таких штук.
inline constexpr RowSide kFlood{15, 400, 4 * kKg, 16 * 4 * kKg};
inline constexpr RowSide kPicker{15, 0, 4 * kKg, 100 * 4 * kKg};
// Круги пришедшего со стоящим `s`: получил минус уплатил, в оценке стоящего.
// «Взял n и вернул»: уплатил цену взятия, за возврат причитается оценка
// стоящего без его доли. «Отдал n и забрал»: наоборот.
constexpr std::int64_t take_then_return(const RowSide& s, std::int64_t sp,
                                        std::int64_t vp, std::int64_t n) {
    return ask_reach(lot_take(after(s, -n), n), sp, vp) - take_price(s, n, sp, vp);
}
constexpr std::int64_t give_then_retake(const RowSide& s, std::int64_t sp,
                                        std::int64_t vp, std::int64_t n) {
    return ask_reach(lot_take(s, n), sp, vp) - take_price(after(s, n), n, sp, vp);
}
// Точное «берущий ценит k-ю единицу выше отдающего» — без наценки.
constexpr bool unit_gainful(const RowSide& giver, const RowSide& taker,
                            std::int64_t k) {
    return gain_units(giver, taker, k, true, 0, 1) == k;
}
}  // namespace price_witness
static_assert(worth_arriving(price_witness::after(price_witness::kEmpty, -1), 1) == 100
              && worth_leaving(price_witness::kEmpty, 1) == 100
              && stock_worth(price_witness::kEmpty, 0) == 0,
              "нет ни нужды, ни запаса — вещь стоит себя; пустой запас не стоит ничего");
static_assert(worth_arriving(price_witness::kHungry, 1) == 4000
              && worth_arriving(price_witness::kHungry, 40) == 100
              && worth_arriving(price_witness::kHungry, 41) == 99
              && worth_leaving(price_witness::kHungry, 1) == 4100
              && price_witness::unit_gainful(
                     price_witness::after(price_witness::kHungry, 37),
                     price_witness::kHungry, 1),
              "нужда поднимает оценку: приход гасит счёт, оценка тает до "
              "стоимости и дальше — в завал; отнятое у голодного он ценит ещё "
              "выше; и в точном сравнении большая нужда ценит единицу выше малой");
static_assert(worth_arriving(price_witness::after(price_witness::kEmpty, 24), 1) == 75
              && worth_arriving(price_witness::after(price_witness::kEmpty, 49), 1) == 50
              && worth_arriving(price_witness::after(price_witness::kEmpty, 98), 1) == 1
              && worth_arriving(price_witness::after(price_witness::kEmpty, 99), 1) == 1
              && worth_arriving(price_witness::after(price_witness::kEmpty, 4999), 1) == 1,
              "ЗАВАЛ: излишек роняет оценку долей спины, которую он занимает; "
              "забившее спину идёт по полу");
static_assert(lot_take(price_witness::kEmpty, 50)
              < lot_take(RowSide{100, 0, price_witness::kKg, 10 * price_witness::kBack}, 50)
              && lot_take(RowSide{100, 0, 4 * price_witness::kKg, price_witness::kBack}, 20)
              < lot_take(price_witness::kEmpty, 20),
              "«много» мерит МАССА против спины: тот же запас дешевле у стороны "
              "со спиной поменьше и у вещи потяжелее");
static_assert(worth_leaving(price_witness::kCoinPile, 1) == 90
              && lot_take(RowSide{100, 0, 10, 1000 * price_witness::kBack}, 1000) == 100 * 1000 - 5
              && lot_take(price_witness::kEmpty, 1000) == 4950 + 901,
              "МОНЕТА — ДЕНЬГИ БЕЗ ТЕГА: тысяча лёгких штук почти не занимает "
              "спину и идёт почти по номиналу, а тысяча тяжёлых — завал");
static_assert(lot_take(RowSide{5, 0, price_witness::kKg, 1000 * price_witness::kKg}, 100) == 475
              && lot_take(RowSide{5, 0, price_witness::kKg, 1000 * price_witness::kKg}, 100)
                 < 5 * 100,
              "ОКРУГЛЯЕТСЯ СУММА, А НЕ ЕДИНИЦА: сто штук стоимостью 5 при спине в "
              "тысячу точно стоят 474.75 — оценка 475; округление каждой "
              "единицы дало бы 500, по пять за штуку");
static_assert(worth_arriving(price_witness::kCheapChild, 1)
              == worth_leaving(price_witness::kCheapHome, 1)
              && gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true, 0, 1) == 49
              && lot_take(price_witness::kCheapChild,
                          gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true, 0, 1))
                 > lot_loss(price_witness::kCheapHome,
                            gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true, 0, 1)),
              "ЗЕРНИСТОСТЬ (M-490): завал в двадцатую долю спины на одной "
              "единице не виден — обе стороны ценят её в те же 5 квантов, — а "
              "на лоте виден: ребёнок ценит взятое выше, чем дом отданное. "
              "Граница посчитана рукой: (1000−k)/1000 > (474999+k)/500000 "
              "⇔ 501·k < 25001 ⇔ k ≤ 49");
static_assert(lot_take(price_witness::kTrader, 30) == lot_loss(price_witness::after(price_witness::kTrader, 30), 30)
              && lot_take(price_witness::kHungry, 70) == lot_loss(price_witness::after(price_witness::kHungry, 70), 70)
              && lot_take(price_witness::kCheapChild, 777) == lot_loss(price_witness::after(price_witness::kCheapChild, 777), 777)
              && lot_take(price_witness::kSated, 500) == lot_loss(price_witness::after(price_witness::kSated, 500), 500),
              "ОЦЕНКА ЗАПАСА — ФУНКЦИЯ СОСТОЯНИЯ: единица стоит стороне одинаково, "
              "приходит она или уходит; взятое и возвращённое возвращает оценку "
              "ровно в прежнее число — через нужду, завал и пол");
static_assert(lot_take(price_witness::kCheapChild, 300)
              == lot_take(price_witness::kCheapChild, 111)
               + lot_take(price_witness::after(price_witness::kCheapChild, 111), 189)
              && lot_loss(price_witness::kCheapHome, 300)
              == lot_loss(price_witness::kCheapHome, 111)
               + lot_loss(price_witness::after(price_witness::kCheapHome, -111), 189),
              "суммы частей лота складываются в сумму целого ТОЧНО — ошибка "
              "округления не копится");
static_assert(lot_take(price_witness::kEmpty, 31) > lot_take(price_witness::kEmpty, 30)
              && worth_arriving(price_witness::kCheapChild, 400) >= 1
              && !price_witness::unit_gainful(price_witness::kEmpty, price_witness::kEmpty, 1)
              && price_witness::unit_gainful(price_witness::kSated, price_witness::kEmpty, 1)
              && !price_witness::unit_gainful(price_witness::kEmpty, price_witness::kSated, 1)
              && stock_worth(RowSide{0, -40, price_witness::kKg, price_witness::kBack}, -40) == 0
              && worth_arriving(RowSide{1, 50, price_witness::kKg, price_witness::kBack}, 1) == 1
              && worth_arriving(RowSide{100, 5, price_witness::kKg, 0}, 1) == 1,
              "оценка запаса растёт с запасом, оценка единицы не растёт; пол — "
              "единица; вещь без стоимости не стоит ничего; у стороны без спины "
              "всякий излишек — завал");
static_assert(gain_denser(6, 49 * price_witness::kKg, 6, 50 * price_witness::kKg)
              && !gain_denser(6, 49 * price_witness::kKg, 6, 49 * price_witness::kKg)
              && gain_denser(std::int64_t(1) << 61, 3, (std::int64_t(1) << 61) - 1, 3)
              && !gain_denser(0, 7, 1, std::int64_t(1) << 61)
              && gain_denser((std::int64_t(1) << 33) - 1, std::int64_t(1) << 32,
                             std::int64_t(1) << 33, (std::int64_t(1) << 33) - 1)
              && !gain_denser(-5, 10, 1, 10),
              "выбор по массе: та же выгода на меньшей массе плотнее, равное не "
              "плотнее равного; сравнение не переполняется у края int64 и не "
              "теряет перенос средних разрядов ((2^33−1)² против 2^65); убыток "
              "не плотнее выгоды");
static_assert(standing_share(200, 13, 13) == 100
              && standing_share(200, 13, 1) == 186
              && standing_share(200, 1, 13) == 15
              && standing_share(1, 13, 510) == 1
              && standing_share(200, 0, 0) == 100
              && standing_share(3, 0, 0) == 2
              && standing_share(200, 13, 0) == 200
              && standing_share(200, 0, 13) == 0
              && standing_share(0, 13, 13) == 0
              && standing_share(-7, 13, 13) == 0
              && standing_share(std::int64_t(1) << 61, 13, 510) < (std::int64_t(1) << 61),
              "доля стоящего — отношение сил, вверх (200·13/14 = 185.7 → 186, "
              "200/14 = 14.3 → 15): между нулём и всей суммой без клампа, равные "
              "— пополам, бессильный стоящий не берёт ничего, неделимый квант — "
              "ему; у края int64 не переполняется");
static_assert(ask_price(100, 13, 13) == 150 && ask_price(100, 13, 0) == 200
              && ask_price(100, 0, 13) == 100 && ask_price(100, 13, 130) == 110
              && ask_price(100, 13, 1) == 193 && ask_price(1, 13, 510) == 2
              && ask_price(0, 13, 13) == 0,
              "ЦЕНА ВЗЯТОГО — оценка стоящего × (1 + его доля): от ×1 у мастера "
              "до ×2 у полного нуля; остаток — стоящему, поэтому и единственный "
              "квант у него стоит два");
static_assert(ask_reach(150, 13, 13) == 100 && ask_reach(149, 13, 13) == 99
              && ask_reach(200, 13, 0) == 100 && ask_reach(100, 0, 13) == 100
              && ask_reach(0, 13, 13) == 0
              && ask_price(ask_reach(1000, 13, 7), 13, 7) <= 1000
              && ask_price(ask_reach(1000, 13, 7) + 1, 13, 7) > 1000
              && ask_reach(std::int64_t(1) << 62, 510, 13) < (std::int64_t(1) << 62),
              "ОБРАТНЫЙ ВОПРОС: на сколько своей оценки стоящий отдаст за данное "
              "ему — наибольшая сумма, чья цена взятия не выше данного; от ÷1 у "
              "мастера до ÷2 у полного нуля");
// ЗАМЫСЕЛ ВЛАДЕЛЬЦА ТРЕМЯ СВОЙСТВАМИ — числами: вещь, которую заваленный
// город ценит в 20, а голодный в 500; рейс покупает в первом и продаёт во
// втором. Сила города 13.
static_assert(ask_reach(500, 13, 0) - ask_price(20, 13, 0) == 250 - 40
              && ask_reach(500, 13, 1) - ask_price(20, 13, 1) == 259 - 39
              && ask_reach(500, 13, 13) - ask_price(20, 13, 13) == 333 - 30
              && ask_reach(400, 13, 0) == ask_price(100, 13, 0),
              "БОЛЬШОЙ ГРАДИЕНТ ПЕРЕБИВАЕТ СКИЛЛ: на разнице в двадцать пять раз "
              "прибыль получает и полный ноль (+210 против +303 у равного); его "
              "порог — ровно вчетверо");
static_assert(ask_reach(120, 13, 0) < ask_price(80, 13, 0)
              && ask_reach(120, 13, 13) < ask_price(80, 13, 13)
              && ask_reach(120, 13, 130) > ask_price(80, 13, 130),
              "НА МАЛОЙ РАЗНИЦЕ РЕШАЕТ СКИЛЛ: полтора раза между рынками — нулю и "
              "равному убыток, сильному прибыль");
static_assert(ask_reach(100, 13, 13) < ask_price(100, 13, 13)
              && ask_reach(100, 13, 1300) < ask_price(100, 13, 1300)
              && ask_reach(100, 0, 13) == ask_price(100, 0, 13),
              "БЕЗ РАЗНИЦЫ — В УБЫТОК: купить и продать по одной оценке стоит "
              "пришедшему долю стоящего дважды, и даже мастеру — квант; вровень "
              "только с бессильным стоящим");
static_assert(take_price(price_witness::kSated, 1, 13, 13) == 75 + 38
              && take_price(price_witness::kSated, 25, 13, 0)
                 == 2 * lot_loss(price_witness::kSated, 25)
              && take_price(price_witness::kHungry, 1, 13, 510)
                 > worth_leaving(price_witness::kHungry, 1)
              && ask_reach(lot_take(price_witness::kHungry, 1), 13, 510)
                 < worth_arriving(price_witness::kHungry, 1),
              "СТОЯЩИЙ НИКОГДА НЕ ТЕРЯЕТ ПО СВОЕЙ ОЦЕНКЕ: за отданное он получает "
              "больше, чем ценил его сам, а за полученное отдаёт меньше — даже "
              "против сильнейшего пришедшего и даже из-под своей нужды");
static_assert(price_witness::take_then_return(price_witness::kSated, 13, 510, 9) < 0
              && price_witness::take_then_return(price_witness::kHungry, 13, 510, 1) < 0
              && price_witness::take_then_return(price_witness::kCoinPile, 1, 510, 500) < 0
              && price_witness::take_then_return(price_witness::kCheapHome, 13, 0, 40) < 0
              && price_witness::give_then_retake(price_witness::kHungry, 13, 510, 9) < 0
              && price_witness::give_then_retake(price_witness::kSated, 1, 510, 20) < 0
              && price_witness::give_then_retake(price_witness::kCoinNone, 13, 13, 500) < 0
              && price_witness::take_then_return(price_witness::kSated, 0, 13, 9) == 0,
              "НАСОСА НЕТ: круг «взял n и вернул» и круг «отдал n и забрал» "
              "пришедшему в убыток при любом перевесе сил — на голодном и на "
              "сытом рынке, на завале и на монете; вровень только с бессильным "
              "стоящим");
static_assert(take_price(price_witness::kSated, 7, 5, 13)
              <= take_price(price_witness::kSated, 3, 5, 13)
               + take_price(price_witness::after(price_witness::kSated, -3), 4, 5, 13)
              && take_price(price_witness::kSated, 7, 5, 13)
              < take_price(price_witness::kSated, 1, 5, 13)
               + take_price(price_witness::after(price_witness::kSated, -1), 1, 5, 13)
               + take_price(price_witness::after(price_witness::kSated, -2), 5, 5, 13)
              && ask_reach(lot_take(price_witness::kHungry, 10), 5, 13)
              >= ask_reach(lot_take(price_witness::kHungry, 4), 5, 13)
               + ask_reach(lot_take(price_witness::after(price_witness::kHungry, 4), 6), 5, 13),
              "ДРОБИТЬ ЛОТ — В УБЫТОК ПРИШЕДШЕМУ: взятое частями стоит не "
              "меньше взятого разом, за отданное частями причитается не больше "
              "— монетку дроблением не выжать");
static_assert(gain_units(price_witness::kSated, price_witness::kTrader, 25, true, 13, 13) == 0
              && gain_units(price_witness::kSated, price_witness::kTrader, 25, true, 13, 13000) == 7
              && gain_units(price_witness::kTrader, price_witness::kHungry, 10, true, 13, 0) == 10
              && gain_units(price_witness::kFlood, price_witness::kPicker, 400, true, 13, 0) == 86
              && gain_units(price_witness::kFlood, price_witness::kPicker, 400, true, 0, 13) == 93
              && gain_units(price_witness::kHungry, price_witness::kTrader, 10, true, 0, 1) == 0,
              "СКОЛЬКО ЕДЕТ — РЕШАЕТ ОЦЕНКА ПРИШЕДШЕГО ПРОТИВ ЦЕНЫ: берущий берёт, "
              "пока ценит единицу больше чем в (1 + доля) раз выше отдающего. "
              "Малый завал (89 против 75) равному не выгоден, мастеру — семь "
              "единиц; голодному везут всё; из завала, оценённого по полу, "
              "полный ноль берёт 86 единиц (15·(100−k)/100 > 2), мастер — 93; "
              "из-под чужой нужды брать невыгодно никому");
static_assert(take_lot(price_witness::kFlood, price_witness::kPicker, 13, 0, 400, 1 << 30).units == 86
              && take_lot(price_witness::kFlood, price_witness::kPicker, 13, 0, 400, 1 << 30).price == 2 * 86
              && take_lot(price_witness::kFlood, price_witness::kPicker, 13, 0, 400, 51).units == 25
              && take_lot(price_witness::kFlood, price_witness::kPicker, 13, 0, 400, 1).units == 0
              && take_lot(price_witness::kFlood, price_witness::kPicker, 13, 0, 30, 1 << 30).units == 30,
              "ВЗЯТЬ У СТОЯЩЕГО: все выгодные единицы по цене взятия (завал по "
              "полу у полного нуля — два кванта за штуку); платёжеспособность "
              "режет лот, груз — тоже; на один квант не купить ничего");
static_assert(give_lot(price_witness::kTrader, price_witness::kHungry, 13, 13, 10, std::int64_t(1) << 40).units == 10
              && give_lot(price_witness::kTrader, price_witness::kHungry, 13, 13, 10, std::int64_t(1) << 40).price
                 == ask_reach(lot_take(price_witness::kHungry, 10), 13, 13)
              && give_lot(price_witness::kTrader, price_witness::kHungry, 13, 13, 10, 3000).units == 1
              && give_lot(price_witness::kTrader, price_witness::kHungry, 13, 13, 10, 3000).price <= 3000
              && give_lot(price_witness::kTrader, price_witness::kSated, 13, 13, 10, std::int64_t(1) << 40).units == 0,
              "ОТДАТЬ СТОЯЩЕМУ: голодному везут всё, и причитается за это его "
              "оценка без его доли; стоящий, которому нечем расплатиться, "
              "получает меньше; сытому, ценящему груз ниже купца, не везут");
static_assert(cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 1000, 500).units == 5
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 1000, 500).price == 500
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 3, 5000).units == 3
              && cover_lot(price_witness::kHungry, price_witness::kTrader, 40, 500).units == 0
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 1000, 499).units == 4
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 1000, 50).units == 0
              && fit_lot(price_witness::kHungry, price_witness::kTrader, 40, 5000).units == 0,
              "ПЛАТИТ ПРИШЕДШИЙ: его монету стоящий засчитывает по своей оценке "
              "— пять монет покрывают пятьсот; тощий стак отдаётся весь; нужное "
              "себе в платёж не идёт; без переплаты — четыре монеты, а на "
              "бюджет меньше монеты не идёт ни одной");
static_assert(reach_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).units == 3
              && reach_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500).price <= 500
              && take_price(price_witness::kCoinPile, 4, 13, 13) > 500
              && reach_lot(price_witness::kCoinPile, price_witness::kCoinNone, 0, 13, 1000, 500).units == 5
              && reach_lot(price_witness::kHungry, price_witness::kTrader, 13, 13, 40, 1 << 30).units == 0,
              "ПЛАТИТ СТОЯЩИЙ: его монета обходится пришедшему в цену взятия, и "
              "больше бюджета он не отдаёт — на пятьсот равный получает три "
              "монеты из кучи (каждая стоит ему 90 и ещё половину), мастер — "
              "пять; нужным себе стоящий не платит");
static_assert(worth_leaving(RowSide{12800, -100000000, price_witness::kKg, price_witness::kBack}, 1)
              == 12800 * std::int64_t(100000001)
              && take_price(RowSide{12800, -100000000, price_witness::kKg, price_witness::kBack}, 1, 0, 13)
                 == 12800 * std::int64_t(100000001)
              && take_price(RowSide{12800, 0, price_witness::kKg, price_witness::kBack},
                            std::int64_t(1) << 27, 13, 0) > 0
              && ask_reach(lot_take(RowSide{12800, -(std::int64_t(1) << 27), price_witness::kKg, price_witness::kBack},
                                    std::int64_t(1) << 27), 13, 0) > 0
              && worth_arriving(RowSide{100, (std::int64_t(1) << 31) - 2, 1, 1000}, 1) == 1,
              "СТРАЖ СОГЛАСОВАН С ТОЧНЫМ СРАВНЕНИЕМ: единица из-под нужды в сто "
              "миллионов штук стоит стороне ровно стоимость × (нужда + 1), и "
              "взять её дешевле нельзя; лот через глубокую нужду считается без "
              "переполнения (иначе это выражение не было бы константой); у края "
              "счётчика стака пол цел");

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

struct Inventory;

// ── МОСТ «НУЖДА → ЯДРО»: СТРОКА СТОРОНЫ ЧИСЛАМИ ──────────────────────────
// Ядро выше берёт числа и не знает, откуда они; этот мост — единственное
// место, где нужда стороны становится входом ядра. НУЖДА — ЧИСЛО, КОТОРОЕ
// ЗАПИСАЛ МОДУЛЬ СТОРОНЫ: счёт сквада `needDebt`. Окно сезона выставляет
// его ВСЯКОМУ скваду с головами — и городу, и ходоку
// (`upkeep_season_window@src/macro/upkeep_window.h`); у сквада без голов счёт
// стоит нулями, и оценку ведёт одна масса против спины. Мост читает ЕГО И
// ТОЛЬКО ЕГО.
// ЗДЕСЬ СТОЯЛА ЕЩЁ И НУЖДА ПОД РЕЦЕПТ, ВЫЧИСЛЯЕМАЯ НА ЛЕТУ ИЗ ЗАПАСА ДРУГОЙ
// СТРОКИ (производная нужда, умерла рядом M-488), и это был насос: оценка
// сырья зависела от запаса выхода, поэтому купец продавал деревне железо с
// премией, следом инструменты — нужда в железе исчезала, — и выкупал железо
// из завала дешевле (ревью дифа 2026-11-12: −143 деревне за визит). Оценка
// строки обязана зависеть только от самой строки; нужды в сырье числом нет
// (вердикт владельца 2026-11-13): сырьё тянет модуль деревни. Второго ответа
// на «чего стороне не хватает» нет — сделка, доска «принеси» и порядок груза
// дани читают этот мост.
// `base` — стоимость единицы (звонящий знает, спрашивает ли он строку
// каталога или стак с аффиксами); `unitMassG` и `backG` — масса единицы и
// спина стороны. Сквад целиком читает дверь `side_row@src/macro/deal.h`.
// ДОЛГ РАЗДЕЛА НАЗВАН (ряд M-488): счёт длиной в словарь товаров — нужда в
// вещи вне словаря (зелье раненого героя) сегодня невыразима.
RowSide row_side(int itemIdx, std::int64_t base, const std::int32_t* needDebt,
                 const Inventory& inv, std::int64_t unitMassG,
                 std::int64_t backG);

} // namespace sm
