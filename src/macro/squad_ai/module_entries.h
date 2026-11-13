// ТОЧКИ ВХОДА ИИ-МОДУЛЕЙ СКВАДОВ — для таблицы
// `kAiModuleRows@src/macro/npc_ai.cpp`, и только для неё. Каждая строка —
// свой файл `src/macro/squad_ai/<имя>.cpp` (CANON «КАЖДЫЙ МОДУЛЬ — ОДИН
// ФАЙЛ»). Модуль этот заголовок не включает: назвать чужую точку входа ему
// нечем. Подпись думки — `AiModuleFn`, подпись смерти — `AiDeathFn`
// (`npc_ai.cpp`).
#pragma once

#include "core/macro_handle.h"

namespace sm {

struct TickContext;
class AiPult;
namespace ecs {
struct MacroNpcRuntime;
struct Pools;
}  // namespace ecs

// `squad_ai/untyped.cpp`
void ai_untyped(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                const TickContext& ctx);

// ═════ МОДУЛИ Village / City / Capital — ТЕ ЖЕ ОБЫЧНЫЕ МОДУЛИ СКВАДОВ ════
// Вердикт владельца 2026-10-24, дословно: «нет это снова фигня какаято - у
// нас место это же АРХЕТИП сквадов и всё а система сквадов едины у них ест
// ьвсе системы - архетип эоо как ТЕБЯ видят другие а не какой ты (город
// деревню и столицу видят как места наприме корвоаны - что за бред». Поэтому
// ниже — тот же `AiSelf` + const-`AiView` + `AiPult`, что у трёх образцов
// выше, та же ось ступеней `NPCState` и тот же гейт занятости
// `recoverySteps`. Архетип в этих телах не упомянут ни разу, и законов
// «места» среди них нет ни одного.
//
// ЧЕГО В НИХ НЕТ — И ЭТО ИХ ВНУТРЕННЕЕ ДЕЛО, А НЕ СВОЙСТВО РОДА: ни `march`,
// ни преамбулы рефлексов. Ногами эти два модуля сегодня не пользуются;
// рефлексы (след, порог угрозы, охота по запаху) стоят в головах ЧЕТЫРЁХ
// ходоков и сюда не скопированы. Прежде они стояли в `dispatch`, и ровно
// поэтому строки Village/City/Capital нельзя было заполнить: ветка бегства
// `squad_threat_step` предиката бойца не имеет вовсе, у места
// `traits.count == 0` ⇒ `bravery_of` = 1.5, — то есть город, увидевший в
// шести клетках что-то в полтора раза сильнее, ПОСТАВИЛ БЫ ЦЕЛЬ В ВОСЬМИ
// КЛЕТКАХ И УШЁЛ, оставив свою фичу позади. Страж, не пускавший рефлексы к
// неподвижному сквадy, был САМ `nullptr` в строке диспетча — заполнение
// строк его снимало, поэтому преамбула уехала в головы ходоков ПЕРВОЙ
// (первая половина M-307, ломоть 5а M-72).
//
// (Размеры крю `kVillageCrewSouls` и `kCityCrewSouls` объявлены в `npc_ai.h`
// — свидетель читает их оттуда.)

// `squad_ai/city.cpp`
void ai_city(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
             const TickContext& ctx);
void city_death(MacroHandle self, AiPult& pult);
// `squad_ai/village.cpp`
void ai_village(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                const TickContext& ctx);
void village_death(MacroHandle self, AiPult& pult);

// ═════ МОДУЛИ АРТЕЛЕЙ ═══════════════════════════════════════════════════════
// Вердикт владельца 2026-11-13 (CANON S25, блок «СЫРЬЁ ТЯНЕТ МОДУЛЬ ДЕРЕВНИ
// ТИПАМИ АРТЕЛЕЙ»): тип артели — отдельный тип сквада со СВОИМ модулем; что
// тянуть, решает модуль деревни, поднимая артель нужного типа; модуль артели
// делает только своё дело — идёт к месту работы, добывает, везёт домой.
// Сменить тип — логика внутри её же модуля: в каждом прямо прописано, кем он
// может стать и когда. Двери «сменить тип» в ядре нет; смена зануляет память.
//
// КАЖДЫЙ МОДУЛЬ НИЖЕ НАПИСАН ЦЕЛИКОМ (AGENTS §4 п.5: под модули общих систем
// не создаётся). Одинаковые куски у соседей — законная копия, а не долг:
// общее у них только то, что есть у всякого сквада, — дверь акта
// (`AiPult::gather`), дверь сделки (`AiPult::hand`), вопросы к миру (`AiView`).
//
// ХАРЧ: артель отдаёт добычу дому сама, а едой её грузит модуль дома своим
// суточным кругом — каждый модуль распоряжается только своим инвентарём.
// Артель ждёт погрузки дома, только если дом еду ДАСТ (ценит её ниже, чем
// она: семья судит голым сравнением оценок, силы (0, 1)); дом сам голоден —
// не ждёт и работает.

// `squad_ai/artel.cpp`
void ai_artel(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
              const TickContext& ctx);
// `squad_ai/ploughmen.cpp`
void ai_ploughmen(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                  const TickContext& ctx);
// `squad_ai/hunters.cpp`
void ai_hunters(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                const TickContext& ctx);
// `squad_ai/flaxers.cpp`
void ai_flaxers(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                const TickContext& ctx);
// `squad_ai/miners.cpp`
void ai_miners(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx);
// `squad_ai/woodcutters.cpp`
void ai_woodcutters(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                    const TickContext& ctx);

// `squad_ai/caravan.cpp`
void ai_vendor(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx);

// `squad_ai/collector.cpp`
void ai_collector(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                  const TickContext& ctx);

// `squad_ai/dragon.cpp`
void ai_dragon(MacroHandle e, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx);

}  // namespace sm
