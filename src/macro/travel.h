// ПЕРЕГРУЗ — БЕЗРАЗМЕРНЫЙ МНОЖИТЕЛЬ ЧАСОВОГО ЖЖЕНИЯ, А НЕ ЦЕНА В SP. Один
// закон на любую спину на карте.
//
// ВЕРДИКТ ВЛАДЕЛЬЦА 2026-10-24, ДОСЛОВНО: «надо сделать элегантно минимально
// чтобы перевес жог только движение - у нас и так есть вес клетки и жигаение
// sp за движение так что перегруз будет множитель этому жиганию так что все
// кто стоят не смогут получить негатив по перегрузу по построениею - то есть
// город может быть "технически перегружен" но ему всё равно потому что он не
// ходит (зато если какоето метсо будет ходить то сразу перегруз будет жечь
// сп!».
//
// ЧТО МЕНЯЕТ ФОРМА, И ЗАЧЕМ ОНА ИМЕННО МНОЖИТЕЛЬ. Здесь стояла `int cost` —
// АБСОЛЮТНЫЕ SP в час, которые `burn_stamina_per_hour` добавляла СЛАГАЕМЫМ
// ВНЕ всех множителей. Слагаемое вне множителей жжёт даже там, где жжения
// нет вовсе: на нулевом весе клетки, у тела, которое СТОИТ. Множитель этого
// не умеет по построению — ноль, умноженный на что угодно, остаётся нулём, и
// «стоящий не получает негатива» становится свойством АРИФМЕТИКИ, а не
// гейтом, который кто-то обязан не забыть поставить.
//
// ЦЕНА СЛАГАЕМОГО НАЗВАНА ЧИСЛОМ (M-310, замер `balance_run` сид 12345):
// город-склад несёт 379 361 кг против спины 99 560 кг, прежняя `cost` давала
// 279 801 SP В ЧАС при планке 110, свип считал его 256 думок в день, лагерь
// такой час не отбивал — и мир терял МЕСТА: 2275 → 90, население 1 090 311 →
// 52 445. Множитель на том же городе равен 4.81 и не стоит ему НИЧЕГО, пока
// он стоит.
//
// It is all that is left of this file: until 2026-10-06 it also owned the
// per-cell travel price, and that whole question died with the hour quantum
// (macro/movement_cost.h — the burn and the two processes it belongs to).
#pragma once

#include "macro/movement_cost.h"
#include "macro/anketa.h"
#include <cmath>

namespace sm {

struct CharacterSheet;
struct Inventory;

// THE overload law, for any back on the map. It was called
// `player_overload_charge` and only the player was ever charged by it, so a
// caravan hauling a ton of iron marched as briskly as an empty scout — the
// weight was a number in a panel, not a cost. Owner's ruling, 2026-08-27:
// «да, перегруз универсальный всем».
float overload_burden(const CharacterSheet& sheet, const Inventory& inventory);

// The same law from the CACHED capacity a macro leader carries on his runtime
// (ecs::MacroNpcRuntime::carryPerSoulG + the bag's haul grams), so a think
// prices its load without rebuilding a derived sheet it does not store.
// Inline: the macro AI is the caller, and it must not have to link the travel
// translation unit (and its terrain/feature world) to ask what a pack weighs.
//
// ПРОПОРЦИОНАЛЬНОСТЬ НАЗВАНА ВСЛУХ: вдвое сверх спины — вдвое дороже час.
// `1 + перегруз/спина`, то есть РОВНО отношение несомого к спине, пока оно
// больше единицы. Один грузовой грамм на честной спине даёт 1.00000001 —
// прежний закон «любой перегруз болит» (целый SP за первый же грамм) УМЕР
// вместе со слагаемым, и умер правильно: он делал первый грамм дороже
// следующих 999, то есть был ступенькой там, где владелец просит множитель.
//
// ЕДИНСТВЕННЫЙ СТРАЖ — ОБЛАСТЬ ОПРЕДЕЛЕНИЯ ДЕЛЕНИЯ (ЗАКОН КЛАМПА: законное
// лицо клампа — защита формулы): спина 0 означает «тело ещё не собрано
// дверью листа» (`carryPerSoulG` — кэш `refresh_body_from_sheet`), и делить
// на неё нечего. У собранного тела спина не бывает нулевой — `kBaseCarryG`
// плюс Str, а `Weightlifting` не cost-down строка, значит множитель ≥ 1.
inline float overload_burden_from_capacity(std::int64_t capacityG,
                                           const Inventory& inventory) {
    if (capacityG <= 0) return 1.0f;
    const std::int64_t overloadG =
        get_overload_penalty(inventory_mass_g(inventory), capacityG);
    if (overloadG <= 0) return 1.0f;
    return 1.0f + float(double(overloadG) / double(capacityG));
}

// (No MacroTravelCost, no macro_travel_cost_for_cell, no
// drain_player_sp_for_macro_cell. All three answered «what does CROSSING THIS
// CELL cost», and on 2026-10-06 that question stopped existing: a body pays
// for HOURS, not for cells (movement_cost.h burn_stamina_per_hour). Their one
// production caller — the player's per-cell charge in app/main.cpp — went with
// them, and the ground under his feet is now read where every squad reads it:
// one weight from the one baked cost grid, no second resolve out of terrain +
// features + trees that could disagree with it at the coast.
//
// What survived is the OVERLOAD law above, because it priced the body's LOAD
// rather than its step, and the hourly burn carries it as its own term.)

} // namespace sm
