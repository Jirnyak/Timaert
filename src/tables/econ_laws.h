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
// batch) — each with its own `max(1, …)`. The numbers agreed; the spelling did
// not, and the sixth author would have got the divisor wrong in silence.
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

} // namespace sm
