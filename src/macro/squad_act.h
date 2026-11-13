// АКТ СКВАДА — ОДНА ДВЕРЬ ЦЕНЫ И ВОССТАНОВЛЕНИЯ МАКРОМИРА.
//
// Вердикты владельца 2026-11-13, дословно (полный текст — CANON S25, «СКВАД,
// МОДУЛЬ И ЕДИНАЯ МЕХАНИКА ДЕЙСТВИЯ»): «в макромире всё СКВАДЫ у них агностично
// у них анкеты ресурсы sp mp hp и инвентарь и они могут делать разные дейтсвия
// (любые) но РЕШАТЬ что реально делать это как раз МОДУЛЬ и нужен скваду»;
// «едина истема рековери - решил чтото сделать запалтитл
// сп/материалами/другими рескрвсамми и ждёшь рековери прежде чем сделать ещё»;
// «затраты SP на любые дейтсвия сквада в макромире делятся на число его юниьов
// в инвентаре».
//
// ЧТО ЗДЕСЬ: механика акта, одна на всякий сквад и всякую строку действия.
//   · право — пара «навык + минранг» строки против анкеты сквада;
//   · цена — плоские колонки строки (HP/MP/SP) одной дверью «всё или ничего»
//     и цена труда: бар, делённый на темп строки, на объём акта, делённая на
//     рабочих юнитов сквада (`act_sp_price@src/tables/econ_laws.h`);
//   · восстановление — РОВНО СТОЛЬКО, СКОЛЬКО ЗАНЯЛА РАБОТА: база строки
//     (время одной единицы действия, эффективное — дверью
//     `recovery_steps@src/bus/anketa.h`) на объём акта, делённая на тех же
//     рабочих юнитов, — в гейт занятости сквада. Владелец, дословно: «он будет
//     рековери ровно столько скольок требовалось за крафт … там большое
//     рековери если много кравитишь».
// ЧЕГО ЗДЕСЬ НЕТ: решения. Что делать и в каком объёме, называет ИИ-модуль
// сквада; дверь не знает ни рода сквада, ни зачем её позвали.
//
// ОТКАЗА ПО УСТАЛОСТИ У ДВЕРИ НЕТ («отказа у труда нет, пустой бар кусает
// HP» — колонка `spRatePerDay@src/tables/actions.h`): цена труда списывается
// и в минус, укус считает дверь траты. Трудиться ли в долг — политика модуля;
// для неё дверь отвечает на обратный вопрос (`squad_act_reach`).
//
// ЗДЕСЬ СТОЯЛО ТРИ ПРАВОПИСАНИЯ. Артель списывала SP и ставила восстановление
// в теле своего модуля. Поселение крафтило общей лестницей `econ_produce_day`:
// SP не платился вовсе, руками был один из восьми, выпуск множился на log2
// населения, а «что и сколько» решала сама лестница. Сняты рядом M-486.
// Третье — окно крафта игрока — ещё зовёт дверь материи и списывает SP само
// (ряд M-493).
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

#include "bus/anketa.h"            // CharacterSheet, recovery_steps, recipe_known,
                                   // craft_item, pay_action_cost
#include "bus/squad_row.h"         // MacroNpcRuntime, charge_squad_recovery
#include "bus/world_row.h"         // work_units — рабочие юниты сквада
#include "core/pools.h"
#include "macro/econ_day.h"        // kRecipes, recipe_of_item — строка рецепта
#include "tables/actions.h"
#include "tables/econ_laws.h"      // act_sp_price / act_sp_reach
#include "tables/movement_cost.h"  // apply_stamina_cost — дверь траты с укусом

namespace sm {

// СВОЯ СТРОКА СКВАДА ГЛАЗАМИ АКТА: ресурсы, гейт занятости, анкета, инвентарь.
// Четыре колонки одного слота; мира здесь нет.
struct ActSelf {
    ecs::Pools& pools;
    ecs::MacroNpcRuntime& rt;
    const CharacterSheet& sheet;
    Inventory& bag;
};

// ЦЕНА ТРУДА АКТА объёмом `volume` для этого сквада. Строка без темпа труда
// (`spRatePerDay` = 0) темповой цены не имеет — правят её плоские колонки.
inline std::int64_t squad_act_cost(const ActSelf& s, const ActionDef& act,
                                   std::int64_t volume) {
    if (act.spRatePerDay <= 0) return 0;
    return act_sp_price(int(s.pools.maxSp), act.spRatePerDay, volume,
                        work_units(s.bag));
}

// КАКОЙ ОБЪЁМ ПОКУПАЕТ НЫНЕШНИЙ БАР — вопрос модуля «сколько». У строки без
// темпа труда предела по бару нет.
inline std::int64_t squad_act_reach(const ActSelf& s, const ActionDef& act) {
    if (act.spRatePerDay <= 0) return std::numeric_limits<std::int64_t>::max();
    return act_sp_reach(int(s.pools.maxSp), act.spRatePerDay,
                        std::int64_t(s.pools.sp), work_units(s.bag));
}

// ГОТОВ ЛИ СКВАД К АКТУ: право строки и её плоская цена. Спрашивается ДО
// эффекта — акт либо состоялся и оплачен целиком, либо мир не тронут.
[[nodiscard]] inline bool squad_act_ready(const ActSelf& s,
                                          const ActionDef& act) {
    return recipe_known(s.sheet.skills, act.gateSkill, int(act.minRank))
        && can_pay_action_cost(s.pools, act);
}

// ВОССТАНОВЛЕНИЕ АКТА объёмом `volume`, в тиках: эффективное время одной
// единицы (база строки через Spd и генерик её домена — одна дверь) на объём,
// делённое на рабочих юнитов; вверх, не меньше тика. Юниты работают разом:
// артель из N за один замах берёт N объектов.
inline std::uint32_t squad_act_recovery(const ActSelf& s, const ActionDef& act,
                                        std::int64_t volume) {
    if (volume <= 0) return 0;
    const std::int64_t one = recovery_steps(
        act.recoveryTicks, s.sheet.attributes, s.sheet.skills,
        action_tempo_generic(act));
    const std::int64_t units = work_units(s.bag);
    const std::int64_t ticks = (one * volume + units - 1) / units;
    // Страж ширины гейта занятости.
    return std::uint32_t(std::min<std::int64_t>(
        ticks, std::numeric_limits<std::uint32_t>::max()));
}

// АКТ СОСТОЯЛСЯ — ЦЕНА И ВОССТАНОВЛЕНИЕ В ОДНОМ МЕСТЕ (ЗАКОН СПОСОБНОСТИ п.4).
// Зовётся после эффекта, объёмом ФАКТА: поле отдало меньше просимого — оплачено
// и отвисено взятое. Страж ширины: цена укладывается в тип бара насыщением.
inline void charge_squad_act(ActSelf& s, const ActionDef& act,
                             std::int64_t volume) {
    (void)pay_action_cost(s.pools, act);   // готовность спрошена до эффекта
    const std::int64_t labour = squad_act_cost(s, act, volume);
    apply_stamina_cost(
        s.pools,
        int(std::min<std::int64_t>(labour, std::numeric_limits<int>::max())));
    charge_squad_recovery(s.rt, squad_act_recovery(s, act, volume));
}

// СКРАФТИТЬ `batches` ПАРТИЙ СТРОКИ `outIdx` — акт крафта сквада. Материалы —
// из своего инвентаря, право — по своей анкете (обе проверки и «всё или
// ничего» держит дверь материи `craft_item@src/bus/anketa.cpp`); цена и
// восстановление — дверью акта. Возвращает сделанные партии: 0 — акта не было,
// и тогда не тронуты ни инвентарь, ни бар, ни гейт занятости.
inline int squad_craft(ActSelf& s, int outIdx, int batches) {
    if (batches <= 0) return 0;
    const int recipe = recipe_of_item(outIdx);
    if (recipe < 0) return 0;
    const std::uint8_t action = kRecipes[recipe].action;
    const ActionDef& act = action_def(int(action));
    if (!squad_act_ready(s, act)) return 0;
    if (!craft_item(s.bag, outIdx, batches, s.sheet.skills, action)) return 0;
    charge_squad_act(s, act, batches);
    return batches;
}

} // namespace sm
