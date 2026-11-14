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
//   · право — гейт занятости свободен и пара «навык + минранг» строки против
//     анкеты сквада;
//   · цена — плоские колонки строки (HP/MP/SP) одной дверью «всё или ничего»
//     и цена труда: бар, делённый на темп строки, на объём акта, делённая на
//     рабочих юнитов сквада (`act_sp_price@src/tables/econ_laws.h`);
//   · восстановление — РОВНО СТОЛЬКО, СКОЛЬКО ЗАНЯЛА РАБОТА: база строки
//     (время одной единицы действия, эффективное — дверью
//     `recovery_steps@src/bus/anketa.h`) на объём акта, делённая на тех же
//     рабочих юнитов, — в гейт занятости субъекта. Владелец, дословно: «он будет
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
// СУБЪЕКТОВ ЧАСОВ ЗАНЯТОСТИ ДВА, ЗАКОН ОДИН (у каждого одна монотонная дверь
// заряда — стена R9 `arch_guard_test`): на карте акт занимает СКВАД
// (`SquadGate` ниже), в сцене — ТЕЛО его аватара
// (`BodyGate@src/sub/ability.h`). Механика акта — право, материя, цена и тики
// восстановления — одна и не знает, чей гейт: гейт — пара «занят ли / занять
// на N тиков» над МОНОТОННОЙ дверью заряда своего субъекта, и называет его
// звонящий. Обратного включения macro→sub нет (§11), поэтому гейт тела
// приходит ТИПОМ, а не веткой здесь.
//
// ЗДЕСЬ СТОЯЛО ТРИ ПРАВОПИСАНИЯ. Артель списывала SP и ставила восстановление
// в теле своего модуля. Поселение крафтило общей лестницей `econ_produce_day`:
// SP не платился вовсе, руками был один из восьми, выпуск множился на log2
// населения, а «что и сколько» решала сама лестница. Сняты рядом M-486.
// Третье — окно крафта игрока — звало дверь материи и списывало цену партии
// само: души сумки в счёт не шли, восстановления не было, партий за кадр
// выходило столько, сколько кликов. Снято рядом M-493: окно зовёт
// `craft_act` ниже с гейтом своего субъекта.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

#include "bus/anketa.h"            // CharacterSheet, recovery_steps, recipe_known,
                                   // craft_item, pay_action_cost
#include "bus/squad_row.h"         // MacroNpcRuntime, charge_squad_recovery,
                                   // squad_recovering
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

// ГЕЙТ СКВАДА — субъект часов занятости на карте: его колонка
// `recoverySteps@src/bus/squad_row.h`, вопрос и заряд — двери того же файла.
// Вид над колонкой, не её копия: носитель один.
struct SquadGate {
    ecs::MacroNpcRuntime& rt;
    [[nodiscard]] bool busy() const { return ecs::squad_recovering(rt); }
    void charge(std::uint32_t ticks) const {
        ecs::charge_squad_recovery(rt, ticks);
    }
};

// ГОТОВ ЛИ СУБЪЕКТ К АКТУ: гейт свободен, право строки и её плоская цена.
// Спрашивается ДО эффекта — акт либо состоялся и оплачен целиком, либо мир
// не тронут. Занятый субъект отказывает ЗДЕСЬ, а не дисциплиной звонящего:
// «решил чтото сделать … и ждёшь рековери прежде чем сделать ещё».
template <class Gate>
[[nodiscard]] inline bool act_ready(const ActSelf& s, const ActionDef& act,
                                    const Gate& gate) {
    return !gate.busy()
        && recipe_known(s.sheet.skills, act.gateSkill, int(act.minRank))
        && can_pay_action_cost(s.pools, act);
}
[[nodiscard]] inline bool squad_act_ready(const ActSelf& s,
                                          const ActionDef& act) {
    return act_ready(s, act, SquadGate{s.rt});
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
// Тики считает акт, записывает их дверь субъекта.
template <class Gate>
inline void charge_act(ActSelf& s, const ActionDef& act, std::int64_t volume,
                       const Gate& gate) {
    (void)pay_action_cost(s.pools, act);   // готовность спрошена до эффекта
    const std::int64_t labour = squad_act_cost(s, act, volume);
    apply_stamina_cost(
        s.pools,
        int(std::min<std::int64_t>(labour, std::numeric_limits<int>::max())));
    gate.charge(squad_act_recovery(s, act, volume));
}
inline void charge_squad_act(ActSelf& s, const ActionDef& act,
                             std::int64_t volume) {
    charge_act(s, act, volume, SquadGate{s.rt});
}

// СКРАФТИТЬ `batches` ПАРТИЙ СТРОКИ `outIdx` — акт крафта. Материалы — из
// своего инвентаря, право — по своей анкете (обе проверки и «всё или ничего»
// держит дверь материи `craft_item@src/bus/anketa.cpp`); цена и
// восстановление — дверью акта, часы — гейтом субъекта. Возвращает сделанные
// партии: 0 — акта не было, и тогда не тронуты ни инвентарь, ни бар, ни гейт
// занятости.
template <class Gate>
inline int craft_act(ActSelf& s, const Gate& gate, int outIdx, int batches) {
    if (batches <= 0) return 0;
    const int recipe = recipe_of_item(outIdx);
    if (recipe < 0) return 0;
    const std::uint8_t action = kRecipes[recipe].action;
    const ActionDef& act = action_def(int(action));
    if (!act_ready(s, act, gate)) return 0;
    if (!craft_item(s.bag, outIdx, batches, s.sheet.skills, action)) return 0;
    charge_act(s, act, batches, gate);
    return batches;
}
// Акт крафта сквада на карте: часы — свои (`SquadGate`). Им крафтят модули
// поселений (`AiPult::craft@src/macro/npc_ai.cpp`); окно игрока на карте зовёт
// `craft_act` с тем же гейтом (`craft_as_player@src/ui/overlays.cpp`).
inline int squad_craft(ActSelf& s, int outIdx, int batches) {
    return craft_act(s, SquadGate{s.rt}, outIdx, batches);
}

} // namespace sm
