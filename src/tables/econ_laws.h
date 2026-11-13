// ЗАКОНЫ ТРУДА — ЯКОРЬ ТЕМПА и ЦЕНА ДЕЙСТВИЯ, чистые constexpr без состояния
// мира. ПЕРЕЕХАЛИ из macro/econ_day.h ломтём 1б эпика пакетов v1: оба читает
// субмир (цена SP за срубленный объект — `sub/engine.cpp`, `sub/ability.h`), а
// §11 запрещает ему включать macro/. Пара неразделима по смыслу: цена ЕСТЬ
// полоса, делённая на темп, и канонический темп — якорь ниже.
//
// Ведомость дня, реестр рецептов, голодная дверь и прочее ИЗМЕНЯЕМОЕ остались
// в macro/econ_day.h — здесь только законы (тот же шов, которым словарь уехал
// в tables/, а слой остался в macro/: M-181, ТИП против ЭКЗЕМПЛЯРА).
#pragma once

#include <cstdint>

namespace sm {

// THE productivity anchor (owner 2026-08-30/31, CANON S10): one worker at ANY
// link of the chain covers the needs of ~32 souls — «1 добытчик кормит 32
// душ» extended through the whole chain (поле → возка → рот) on 2026-08-31.
// ЧИТАТЕЛИ ЖИВЫЕ И ВНЕ ЭТОГО ФАЙЛА: цена SP за срубленный/сжатый объект
// (sub/engine.cpp, sub/ability.h — одна добыча = полоса / этот якорь), темпы
// добывающих нарядов макро-ИИ (npc_ai.cpp) и полоса в оверлее. Строка хлеба
// из него НЕ выводится — её нет и быть не может (CANON S10 «ЕДА НЕ
// ПРОИЗВОДИТСЯ»); таблица рецептов (macro/econ_day.h) задаёт тот же темп «за
// рабочий день» и обязана читаться рядом с якорем.
inline constexpr int kGatherPerWorkerDay = 32;

// ── THE PRICE OF AN ACTION (CANON S14.1) ─────────────────────────────────
// Every action costs SP, and the price is one sentence: THE BAR DIVIDED BY
// THE RATE — how many such actions the body does in a day. Marching, felling,
// mining, ploughing, building, crafting: one purse, one formula, no second
// labour law.
//
// It is a DOOR because the sentence was written five times in three dialects
// — `maxSp / kWorkCyclesPerBar` (three copies in npc_ai.cpp), `maxSp /
// kGatherPerWorkerDay` (the player's harvest), `maxSp / item_labour` (a craft
// batch; the column died 2026-11-06 into the recipe's action row) — each with
// its own `max(1, …)`. The numbers agreed; the spelling did not, and the
// sixth author would have got the divisor wrong in silence.
//
// The floor of 1 is the law, not a guard: an action a body can perform is an
// action it can be tired by, so no rate however generous makes work free.
//
// `ratePerDay` is a COLUMN of whatever is acting — cycles in a bar, objects in
// a day, batches in a person-day. A new action names its rate; it does not
// rewrite this.
inline constexpr int sp_price(int maxSp, int ratePerDay) {
    return ratePerDay > 0 ? (maxSp / ratePerDay > 1 ? maxSp / ratePerDay : 1)
                          : maxSp;
}

// ── ЦЕНА АКТА СКВАДА: ЦЕНА СТРОКИ, ДЕЛЁННАЯ НА РАБОЧИХ ЮНИТОВ ─────────────
// Вердикт владельца 2026-11-13, дословно: «затраты SP на любые дейтсвия
// сквада в макромире делятся на число его юниьов в инвентаре … и поэтому
// город может больше произвести за меньшее sp потому чт в нёём много юнитов
// такой замысел (добыча артелью тоже есть способность добыть и затрыт sp
// тожеделятся на число в скваде … работников)»; и о КПД многолюдного сквада:
// «НАСЕЛЕНИЕ ГОРОДА И ЕСТЬ ЮНИТЫ это и будет кпд».
//
// `volume` — объём акта в единицах его строки (партий, объектов): цена
// единицы есть `sp_price`, цена акта — её произведение на объём, делённое на
// рабочих юнитов сквада. Бар у сквада один и от ростера не растёт; растёт
// то, сколько работы он покупает. Юнитов не меньше одного: аватар — тоже
// юнит, одиночка не частный случай.
//
// Округление ВВЕРХ, и это закон, а не страж: пол `sp_price` («работа,
// которую тело способно сделать, способна его утомить») обязан пережить
// деление — иначе один колос на восемьсот рук стоил бы ноль.
//
// ЗДЕСЬ СТОЯЛИ ТРИ СЧЁТА РУК: у добычи цена акта была постоянной, а выход
// множился на руки (то же отношение, сказанное наоборот, и только для неё);
// у крафта поселения руками был один из восьми, выпуск множился на log2
// населения, а SP не платился вовсе; у руки в окне крафта души сумки в счёт
// не шли. Сняты вердиктом 2026-11-13 (ряд M-486).
inline constexpr std::int64_t act_sp_price(int maxSp, int ratePerDay,
                                           std::int64_t volume, int units) {
    if (volume <= 0) return 0;
    const std::int64_t one = sp_price(maxSp, ratePerDay);
    const std::int64_t n = units > 1 ? units : 1;
    return (volume * one + n - 1) / n;
}

// ОБРАТНЫЙ ВОПРОС ТОЙ ЖЕ ЦЕНЫ: какой объём покупают `sp` очков бара. Читает
// его модуль, решая «сколько» (трудиться ли в долг — его политика, у двери
// отказа нет). Вниз: купленное никогда не стоит больше названного.
inline constexpr std::int64_t act_sp_reach(int maxSp, int ratePerDay,
                                           std::int64_t sp, int units) {
    if (sp <= 0) return 0;
    const std::int64_t one = sp_price(maxSp, ratePerDay);
    const std::int64_t n = units > 1 ? units : 1;
    return sp * n / one;
}

// Свидетели закона — числами. Бар 110 (свежий лист), ткань — темп 4.
static_assert(act_sp_price(110, 4, 1, 1) == sp_price(110, 4),
              "одиночка платит за единицу цену строки");
static_assert(act_sp_price(110, 4, 801, 801) == sp_price(110, 4),
              "N юнитов делают N единиц по цене одной");
static_assert(act_sp_price(110, 4, 801, 1) == 801 * sp_price(110, 4),
              "тот же объём одиночке стоит в N раз дороже");
static_assert(act_sp_price(110, 32, 1, 801) == 1,
              "работа не бывает даровой: пол пережил деление");
static_assert(act_sp_price(110, 4, 0, 801) == 0,
              "нет объёма — нет акта и нет цены");
static_assert(act_sp_reach(110, 4, 110, 1) == 4,
              "полный бар одиночки покупает темп строки — человеко-день");
static_assert(act_sp_reach(110, 4, 110, 801)
                  >= 801 * act_sp_reach(110, 4, 110, 1),
              "тот же бар у восьмисот одного юнитов покупает не меньше чем в "
              "восемьсот один раз больше");
static_assert(act_sp_price(110, 4, act_sp_reach(110, 4, 37, 13), 13) <= 37,
              "купленное не стоит больше названного бюджета");
static_assert(act_sp_price(110, 4, act_sp_reach(110, 4, 37, 13) + 1, 13) > 37,
              "…и это самый большой такой объём");

} // namespace sm
