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
// держит. Насыщается СУММА ОТРЕЗКА, а не оценка запаса от нуля: одна единица
// и малый лот считаются точно при любой глубине нужды, и суммы согласны с
// точным сравнением единиц ниже.
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

// ТОЧНОЕ СРАВНЕНИЕ ДВУХ ДРОБЕЙ a/b и c/d (числители неотрицательны,
// знаменатели положительны): −1, 0, +1. Ход алгоритма Евклида — целые части,
// затем обратные остаткам: умножения нет, значит нет и переполнения.
constexpr int frac_cmp(std::int64_t a, std::int64_t b, std::int64_t c,
                       std::int64_t d) {
    for (;;) {
        const std::int64_t qa = a / b, qc = c / d;
        if (qa != qc) return qa < qc ? -1 : 1;
        const std::int64_t ra = a % b, rc = c % d;
        if (ra == 0 || rc == 0) return ra == rc ? 0 : (ra == 0 ? -1 : 1);
        // ra/b против rc/d  ⇔  d/rc против b/ra.
        a = d;
        c = b;
        b = rc;
        d = ra;
    }
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
// k-ю единицу берущий ценит ВЫШЕ отдающего (`strict`) либо НЕ НИЖЕ. Оценка
// берущего с каждой единицей не растёт, отдающего — не падает, поэтому граница
// одна и ищется делением пополам; сравнение точное.
constexpr std::int64_t gain_units(const RowSide& giver, const RowSide& taker,
                                  std::int64_t cap, bool strict) {
    using namespace price_detail;
    const std::int64_t from = at_guard(giver.net), to = at_guard(taker.net);
    const auto gainful = [&](std::int64_t k) {
        const Worth tw = edge_worth(taker, to + k);
        const Worth gw = edge_worth(giver, from - k + 1);
        const int c = frac_cmp(tw.num, tw.den, gw.num, gw.den);
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
    return price_detail::frac_cmp(gainA, massGA, gainB, massGB) > 0;
}

// ── ЯДРО: ЦЕНА ───────────────────────────────────────────────────────────
// Единицы едут от отдающего к берущему, и ОДНА формула называет цену:
//
//     цена = оценка НЕ ПРЕДЛАГАВШЕГО ± его доля × max(0, разница оценок)
//     разница = оценка берущего − оценка отдающего
//     доля    = его сила / (сила отдающего + сила берущего), остаток — ему
//
// Плюс — когда не предлагал отдающий (он получает свою оценку и долю сверх),
// минус — когда не предлагал берущий (он платит свою оценку без своей доли).
// Продавца, покупателя, «товара» и «платежа» в законе нет — есть единицы,
// едущие в две стороны, и у каждой стороны одна оценка на все случаи. Из
// одного сравнения разницы с нулём выходит всё остальное:
//   · разница БОЛЬШЕ нуля — цена лежит между двумя оценками, и каждая сторона
//     в выигрыше по собственной мерке; делит выгоду ОТНОШЕНИЕ сил (вердикт
//     владельца 2026-11-12): всегда между нулём и единицей без клампа;
//   · разница НОЛЬ — цена ровно эта оценка, сила тут не при чём;
//   · разница МЕНЬШЕ нуля («против выгоды») — делить нечего, доля ноль, и
//     цена есть ровно оценка не предлагавшего: предложивший отдать получает
//     оценку берущего, предложивший взять платит оценку отдающего. Такая
//     единица едет только волей предложившего, и платит за это он. Модули
//     сквадов такого не предлагают; предложить может рука игрока;
//   · НЕДЕЛИМЫЙ ОСТАТОК доли достаётся не предлагавшему — тем же законом
//     «ПЛАТИТ ТОТ, КОМУ НАДО». Владелец 2026-11-13, дословно: «если можно
//     элегантно без проблем совместить это с модельюю лапатит тот мому надо
//     (когда знак меньшу нуля там где разница цен ичтот чтот) то также и в
//     пользу того кому меньше надо по сили там чтобы не выдаивали не
//     вымарщивали монеточки»; и о форме: «это идёт по той же формуле … чемр з
//     сранвнеи еразнице цен с нулем? или ещё один костыль это важно». По той
//     же: «кто предложил» читается в ОДНОМ месте — чья оценка якорь.
// Не предлагавший никогда не теряет по своей оценке, поэтому круг «отдал n и
// забрал n» не приносит ничего при ЛЮБЫХ силах, а дробить лот предложившему
// в убыток: каждая часть стоит ему своего остатка (свидетели ниже).
enum class Offer : std::uint8_t { Giver, Taker };

// ДОЛЯ стороны с силой `own` против `other` от выгоды `gain` — ВВЕРХ: это доля
// НЕ ПРЕДЛАГАВШЕГО, и неделимый остаток её. Выгоды нет — доли нет. Доля не
// больше самой выгоды. Страж области определения: у двух сторон без силы
// делить нечем — пополам.
constexpr std::int64_t gain_share(std::int64_t gain, std::int64_t own,
                                  std::int64_t other) {
    if (gain <= 0) return 0;
    const std::int64_t p = own < 0 ? 0 : own;
    const std::int64_t q = other < 0 ? 0 : other;
    if (p + q == 0) return gain / 2 + gain % 2;
    // Деление прежде умножения: произведение gain × p не обязано лечь в
    // int64, а частное и остаток — ложатся всегда.
    const std::int64_t tail = gain % (p + q) * p;
    return gain / (p + q) * p + tail / (p + q) + (tail % (p + q) != 0 ? 1 : 0);
}

// ЦЕНА ПО ДВУМ СУММАМ — что теряет отдающий (`loss`) и что приобретает
// берущий (`take`): формула выше, целиком. Не убывает, когда обе суммы растут.
constexpr std::int64_t span_price(std::int64_t loss, std::int64_t take,
                                  std::int64_t giverPower,
                                  std::int64_t takerPower, Offer offeredBy) {
    return offeredBy == Offer::Taker
        ? loss + gain_share(take - loss, giverPower, takerPower)
        : take - gain_share(take - loss, takerPower, giverPower);
}

// ЦЕНА ЛОТА из `n` единиц — СУММА ПО ЕДИНИЦАМ (вердикт владельца
// 2026-11-12). Разница оценок с каждой единицей не растёт, поэтому лот есть
// выгодное начало и остаток против выгоды, и формула берётся от каждого из
// двух: выгода начала не покрывает убытка остатка — остаток оплачивает
// предложивший целиком.
constexpr std::int64_t lot_price(const RowSide& giver, const RowSide& taker,
                                 std::int64_t giverPower,
                                 std::int64_t takerPower, std::int64_t n,
                                 Offer offeredBy) {
    if (n <= 0) return 0;
    const std::int64_t head = gain_units(giver, taker, n, true);
    const std::int64_t headLoss = lot_loss(giver, head);
    const std::int64_t headTake = lot_take(taker, head);
    return span_price(headLoss, headTake, giverPower, takerPower, offeredBy)
         + span_price(lot_loss(giver, n) - headLoss,
                      lot_take(taker, n) - headTake, giverPower, takerPower,
                      offeredBy);
}

// ЛОТ ВЫГОДЫ — наибольшее число единиц (не больше `cap`), КАЖДУЮ из которых
// берущий ценит строго выше отдающего и которые он способен оплатить
// (`wallet` — его покупательная способность). Это ответ ядра на вопрос
// «сколько тут есть что делить»; ВЕЗТИ ЛИ и ПРОДАВАТЬ ЛИ — решает модуль
// звонящего.
struct LotQuote {
    std::int64_t units = 0;   // сколько единиц едет
    std::int64_t price = 0;   // сколько стоимости отдающий за них получает
};
constexpr LotQuote gainful_lot(const RowSide& giver, const RowSide& taker,
                               std::int64_t giverPower,
                               std::int64_t takerPower, std::int64_t cap,
                               std::int64_t wallet, Offer offeredBy) {
    const auto price = [&](std::int64_t n) {
        return lot_price(giver, taker, giverPower, takerPower, n, offeredBy);
    };
    std::int64_t lo = 0, hi = gain_units(giver, taker, cap, true);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (price(mid) <= wallet) lo = mid;
        else hi = mid - 1;
    }
    return lo > 0 ? LotQuote{lo, price(lo)} : LotQuote{};
}

// ВСТРЕЧНАЯ КОРЗИНА — ТЕМ ЖЕ ЗАКОНОМ: сколько единиц строки (не больше
// `avail`) отдающий должен отдать, чтобы их цена покрыла `target`. Едут только
// единицы, которые берущий ценит НЕ НИЖЕ отдающего: отданное из-под своей
// нужды или в забитую спину берущего в корзину не идёт — это не платёж, а
// убыток одной из сторон. Не набралось — идёт всё, что годится. Последняя
// единица вправе переплатить (бартер без сдачи — ряд M-487).
constexpr LotQuote cover_lot(const RowSide& giver, const RowSide& taker,
                             std::int64_t giverPower, std::int64_t takerPower,
                             std::int64_t avail, std::int64_t target,
                             Offer offeredBy) {
    if (target <= 0) return {};
    const auto price = [&](std::int64_t n) {
        return lot_price(giver, taker, giverPower, takerPower, n, offeredBy);
    };
    std::int64_t lo = 1, hi = gain_units(giver, taker, avail, false);
    if (hi <= 0) return {};
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo) / 2;
        if (price(mid) >= target) hi = mid;
        else lo = mid + 1;
    }
    return LotQuote{lo, price(lo)};
}

// ТА ЖЕ КОРЗИНА, НО БЕЗ ПЕРЕПЛАТЫ: наибольшее число единиц (не больше
// `avail`), чья цена НЕ ВЫШЕ `budget`. Плательщик зовёт её там, где покрыть
// сумму с запасом значило бы отдать больше, чем он сам ценит взятое.
constexpr LotQuote fit_lot(const RowSide& giver, const RowSide& taker,
                           std::int64_t giverPower, std::int64_t takerPower,
                           std::int64_t avail, std::int64_t budget,
                           Offer offeredBy) {
    if (budget <= 0) return {};
    const auto price = [&](std::int64_t n) {
        return lot_price(giver, taker, giverPower, takerPower, n, offeredBy);
    };
    std::int64_t lo = 0, hi = gain_units(giver, taker, avail, false);
    while (lo < hi) {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (price(mid) <= budget) lo = mid;
        else hi = mid - 1;
    }
    return lo > 0 ? LotQuote{lo, price(lo)} : LotQuote{};
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
// Круг «отдал n — забрал те же n», и обе ноги предлагает первая сторона:
// выручка минус выкуп.
constexpr std::int64_t round_trip(const RowSide& a, const RowSide& b,
                                  std::int64_t aPower, std::int64_t bPower,
                                  std::int64_t n) {
    return lot_price(a, b, aPower, bPower, n, Offer::Giver)
         - lot_price(after(b, n), after(a, -n), bPower, aPower, n,
                     Offer::Taker);
}
// Точное «берущий ценит k-ю единицу выше отдающего».
constexpr bool unit_gainful(const RowSide& giver, const RowSide& taker,
                            std::int64_t k) {
    return gain_units(giver, taker, k, true) == k;
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
              && gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true) == 49
              && lot_take(price_witness::kCheapChild,
                          gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true))
                 > lot_loss(price_witness::kCheapHome,
                            gain_units(price_witness::kCheapHome, price_witness::kCheapChild, 25000, true)),
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
              && !gain_denser(0, 7, 1, std::int64_t(1) << 61),
              "выбор по массе: та же выгода на меньшей массе плотнее, равное не "
              "плотнее равного; сравнение не переполняется у края int64");
static_assert(gain_share(200, 13, 13) == 100
              && gain_share(200, 1, 13) == 15
              && gain_share(200, 13, 1) == 186
              && gain_share(200, 0, 0) == 100
              && gain_share(3, 0, 0) == 2
              && gain_share(200, 5, 0) == 200
              && gain_share(200, 0, 5) == 0
              && gain_share(1, 1, 13) == 1
              && gain_share(7, 510, 1) <= 7
              && gain_share(0, 5, 5) == 0
              && gain_share(-7, 5, 5) == 0,
              "доля не предлагавшего — отношение сил, вверх (200/14 = 14.3 → 15, "
              "200·13/14 = 185.7 → 186): между нулём и всей выгодой без клампа, "
              "равные делят пополам, бессильный не получает ничего, неделимый "
              "квант — ему; убыток не делится");
static_assert(gain_share(std::int64_t(1) << 61, 510, 13)
              < (std::int64_t(1) << 61),
              "доля не переполняется на выгоде у края int64");
static_assert(span_price(100, 300, 1, 13, Offer::Taker) == 115
              && span_price(100, 300, 1, 13, Offer::Giver) == 114
              && span_price(100, 300, 13, 13, Offer::Taker)
                 == span_price(100, 300, 13, 13, Offer::Giver)
              && span_price(300, 100, 1, 510, Offer::Giver) == 100
              && span_price(300, 100, 510, 1, Offer::Taker) == 300
              && span_price(100, 100, 510, 1, Offer::Taker) == 100
              && span_price(100, 100, 1, 510, Offer::Giver) == 100,
              "ОДНА ФОРМУЛА ЦЕНЫ: якорь — оценка не предлагавшего. Выгода 200 "
              "при силах 1 против 13: слабый отдающий получает 115, если "
              "предложил берущий, и 114, если предложил сам, — квант остатка "
              "уходит не предлагавшему; где делится нацело, от предложившего не "
              "зависит. Против выгоды — ровно оценка не предлагавшего при "
              "любых силах; равные оценки — эта оценка");
static_assert(lot_price(price_witness::kCoinPile, price_witness::kCoinNone, 1, 510, 50, Offer::Giver)
              >= 50 * 90
              && lot_price(price_witness::kCoinPile, price_witness::kCoinNone, 510, 1, 50, Offer::Giver)
              <= 50 * 100,
              "монета идёт около номинала при любых силах: куча в десятую "
              "спины — всё, что на ней можно выторговать");
static_assert(lot_price(price_witness::kTrader, price_witness::kHungry, 13, 13, 1, Offer::Giver)
              == worth_leaving(price_witness::kTrader, 1)
               + (worth_arriving(price_witness::kHungry, 1)
                  - worth_leaving(price_witness::kTrader, 1)) / 2
              && lot_price(price_witness::kTrader, price_witness::kHungry, 1, 13, 1, Offer::Giver)
              == worth_leaving(price_witness::kTrader, 1)
               + (worth_arriving(price_witness::kHungry, 1)
                  - worth_leaving(price_witness::kTrader, 1)) / 14
              && lot_price(price_witness::kTrader, price_witness::kHungry, 510, 13, 1, Offer::Giver)
              < worth_arriving(price_witness::kHungry, 1)
              && lot_price(price_witness::kTrader, price_witness::kHungry, 0, 13, 1, Offer::Giver)
              == worth_leaving(price_witness::kTrader, 1)
              && worth_leaving(price_witness::kTrader, 1) == 90,
              "цена единицы лежит между оценкой отдающего и оценкой берущего; "
              "бессильный получает свою оценку, сильнейший — меньше оценки рынка");
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
              && price_witness::round_trip(price_witness::kCheapHome, price_witness::kCheapChild, 510, 1, 40) <= 0
              && price_witness::round_trip(price_witness::kTrader, price_witness::kHungry, 13, 13, 9) <= 0,
              "НАСОСА НЕТ: круг «отдал n — забрал n» не приносит ничего при "
              "любом перевесе сил — на голодном и на сытом рынке, на завале и "
              "на монете");
static_assert(lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 10, Offer::Giver)
              >= lot_price(price_witness::kTrader, price_witness::kHungry, 5, 13, 4, Offer::Giver)
               + lot_price(price_witness::after(price_witness::kTrader, -4),
                           price_witness::after(price_witness::kHungry, 4), 5, 13, 6, Offer::Giver)
              && lot_price(price_witness::kSated, price_witness::kTrader, 5, 13, 7, Offer::Taker)
              <= lot_price(price_witness::kSated, price_witness::kTrader, 5, 13, 3, Offer::Taker)
               + lot_price(price_witness::after(price_witness::kSated, -3),
                           price_witness::after(price_witness::kTrader, 3), 5, 13, 4, Offer::Taker)
              && lot_price(price_witness::kSated, price_witness::kTrader, 5, 13, 7, Offer::Taker)
              < lot_price(price_witness::kSated, price_witness::kTrader, 5, 13, 1, Offer::Taker)
               + lot_price(price_witness::after(price_witness::kSated, -1),
                           price_witness::after(price_witness::kTrader, 1), 5, 13, 1, Offer::Taker)
               + lot_price(price_witness::after(price_witness::kSated, -2),
                           price_witness::after(price_witness::kTrader, 2), 5, 13, 5, Offer::Taker),
              "ДРОБИТЬ ЛОТ — В УБЫТОК ПРЕДЛОЖИВШЕМУ: предложивший отдать "
              "получает за части не больше, чем за целое; предложивший взять "
              "платит за части не меньше — монетку дроблением не выжать");
static_assert(lot_price(price_witness::kSated, price_witness::kTrader, 13, 13, 20, Offer::Taker)
              == lot_price(price_witness::kSated, price_witness::kTrader, 13, 13, 7, Offer::Taker)
               + lot_price(price_witness::after(price_witness::kSated, -7),
                           price_witness::after(price_witness::kTrader, 7), 13, 13, 13, Offer::Taker)
              && lot_price(price_witness::kSated, price_witness::kTrader, 13, 13, 20, Offer::Taker)
              > lot_loss(price_witness::kSated, 20)
              && lot_take(price_witness::kTrader, 20) < lot_loss(price_witness::kSated, 20),
              "ЛОТ — СУММА ПО ЕДИНИЦАМ: смешанный лот (семь единиц выгодны, "
              "остальные против выгоды) стоит ровно как его выгодное начало "
              "плюс остаток. Убыток остатка выгоду начала не съедает: взявший "
              "двадцать платит и долю на семи, и полную оценку отдающего на "
              "тринадцати — больше, чем оценка отдающего за весь лот");
static_assert(worth_leaving(RowSide{12800, -100000000, price_witness::kKg, price_witness::kBack}, 1)
              == 12800 * std::int64_t(100000001)
              && lot_price(RowSide{12800, -100000000, price_witness::kKg, price_witness::kBack},
                           RowSide{12800, 0, price_witness::kKg, 10 * price_witness::kBack},
                           0, 13, 1, Offer::Taker)
                 == 12800 * std::int64_t(100000001)
              && lot_price(RowSide{12800, 0, price_witness::kKg, price_witness::kBack},
                           RowSide{12800, -(std::int64_t(1) << 27), price_witness::kKg, price_witness::kBack},
                           1, 0, std::int64_t(1) << 27, Offer::Taker) > 0
              && worth_arriving(RowSide{100, (std::int64_t(1) << 31) - 2, 1, 1000}, 1) == 1,
              "СТРАЖ СОГЛАСОВАН С ТОЧНЫМ СРАВНЕНИЕМ: единица из-под нужды в сто "
              "миллионов штук стоит стороне ровно стоимость × (нужда + 1), и "
              "взять её даром нельзя; лот через глубокую нужду считается без "
              "переполнения (иначе это выражение не было бы константой); у края "
              "счётчика стака пол цел");
static_assert(cover_lot(RowSide{3, 12, 10, 209}, RowSide{3, -2, 10, 29}, 2, 13, 3, 100, Offer::Giver).units == 3
              && cover_lot(RowSide{3, 12, 10, 209}, RowSide{3, -2, 10, 29}, 2, 13, 3, 100, Offer::Giver).price
                 == lot_price(RowSide{3, 12, 10, 209}, RowSide{3, -2, 10, 29}, 2, 13, 3, Offer::Giver)
              && fit_lot(RowSide{3, 56, 1, 50}, RowSide{3, 27, 1, 100}, 2, 0, 40, 1 << 20, Offer::Taker).price
                 == lot_price(RowSide{3, 56, 1, 50}, RowSide{3, 27, 1, 100}, 2, 0,
                              fit_lot(RowSide{3, 56, 1, 50}, RowSide{3, 27, 1, 100}, 2, 0, 40, 1 << 20, Offer::Taker).units,
                              Offer::Taker)
              && gainful_lot(price_witness::kSated, price_witness::kTrader, 13, 1, 25, 1 << 30, Offer::Taker).price
                 == lot_price(price_witness::kSated, price_witness::kTrader, 13, 1, 7, Offer::Taker),
              "КОРЗИНА И ЛОТ — ОДНА ЦЕНА: сколько бы единиц ни назвали двери "
              "лота, стоят они ровно столько, сколько называет цена лота, — и "
              "там, где в лот попали единицы «вровень» (точки найдены ревью "
              "дифа: своя копия формулы давала предложившему лишний квант)");
static_assert(gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1 << 30, Offer::Giver).units == 10
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 7, 1 << 30, Offer::Giver).units == 7
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 0, Offer::Giver).units == 0
              && gainful_lot(price_witness::kHungry, price_witness::kTrader, 1, 13, 10, 1 << 30, Offer::Giver).units == 0,
              "лот выгоды — все единицы, которые берущий ценит выше отдающего, "
              "не больше груза; пустому кошельку продавать нечего, а из-под "
              "чужой нужды брать невыгодно");
static_assert(gainful_lot(price_witness::kSated, price_witness::kTrader, 13, 1, 25, 1 << 30, Offer::Taker).units == 7
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1000, Offer::Giver).units
                 < gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 5000, Offer::Giver).units
              && gainful_lot(price_witness::kTrader, price_witness::kHungry, 1, 13, 10, 1000, Offer::Giver).price <= 1000,
              "завал уходит к свободной спине, пока оценки не сойдутся (75+k−1 "
              "против 89−k+1: семь единиц строго выгодны, восьмая — вровень), — "
              "не весь; покупательная способность берущего режет лот");
static_assert(cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500, Offer::Giver).price >= 500
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500, Offer::Giver).units <= 6
              && cover_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 3, 5000, Offer::Giver).units == 3
              && cover_lot(price_witness::kHungry, price_witness::kTrader, 13, 13, 40, 500, Offer::Giver).units == 0
              && cover_lot(price_witness::kSated, price_witness::kTrader, 13, 13, 25, 300, Offer::Giver).price >= 300,
              "встречная корзина: монета покрывает сумму малым числом штук, "
              "тощий стак отдаётся весь, нужное себе в платёж не идёт, а "
              "излишек — идёт");
static_assert(fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500, Offer::Giver).price <= 500
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 500, Offer::Giver).units >= 5
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 1000, 50, Offer::Giver).units == 0
              && fit_lot(price_witness::kCoinPile, price_witness::kCoinNone, 13, 13, 3, 5000, Offer::Giver).units == 3
              && fit_lot(price_witness::kSated, price_witness::kTrader, 13, 13, 25, 300, Offer::Giver).price <= 300
              && fit_lot(price_witness::kHungry, price_witness::kTrader, 13, 13, 40, 5000, Offer::Giver).units == 0,
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
// ЗАПИСАЛ МОДУЛЬ СТОРОНЫ: счёт сквада `needDebt`. Окно сезона выставляет
// его ВСЯКОМУ скваду с головами — и городу, и ходоку
// (`upkeep_season_window@src/macro/upkeep_window.h`); у сквада без голов счёт
// стоит нулями, и оценку ведёт одна масса против спины. Мост читает ЕГО И
// ТОЛЬКО ЕГО.
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
