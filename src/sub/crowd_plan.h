// ПЛАН ТОЛПЫ ПОСЕЛЕНИЯ — МОДУЛЬ НА РОД, ДИСПЕТЧ ТАБЛИЦЕЙ (M-414).
//
// Вердикт владельца 2026-11-06, дословно: «агностично модуль под каждую
// фичу, никаких проверок; условия по роду внутри модуля ЭТО ПЛОХО» — выбор
// ПОВЕДЕНИЯ по роду есть работа ДИСПЕТЧЕРА, и диспетчер есть ТАБЛИЦА С
// УКАЗАТЕЛЕМ (образец — `open_air_gen_for@src/sub/gens/dispatch.h`; та же
// форма у пола данжа `dungeon_floor_tile@src/sub/dgn/dispatch.cpp`). До
// M-414 спавнер решал обе вещи сравнением `== SquadType::City`: радиус
// каменного диска (род был СВЁРНУТ В bool на входе снесённой
// settlement_population_radius — третьему роду она ответить не умела по
// построению) и наличие верхнего квартала с донжоном; Capital — СТАРШИЙ
// род поселения — получал деревенский диск и никакого квартала, молча.
//
// if ВНУТРИ модуля про ЕГО СОБСТВЕННЫЕ данные — норма (граница вердикта);
// модуль не знает ни рода звонящего, ни соседей по таблице.
#pragma once
#include <algorithm>
#include <span>

#include "sub/city_layout.h"
#include "sub/map_data.h"       // Structure — донжон ищется по крышам
#include "tables/squad_type.h"

namespace sm::sub {

// Ответ модуля: каменный диск паствы (пол поиска места на улице — стены
// строились на всех, и город с полпаствой в артелях не сжимается до
// хутора) и верхний квартал (донжон + радиус; haveQuarter = false —
// квартала у рода нет, это ЗНАЧЕНИЕ, а не исключение).
struct CrowdPlan {
    float discRadius = 0.0f;
    float keepX = 0.0f;
    float keepY = 0.0f;
    float quarterR = 0.0f;
    bool  haveQuarter = false;
};

// Вход агностичен: ПОСТРОЕННАЯ геометрия клетки, паства и центр. Больше
// модулю не передаётся ничего — перебор чужого невыразим подписью.
struct CrowdPlanIn {
    std::span<const Structure> structures;
    float centerX = 0.0f;
    float centerY = 0.0f;
    int originX = 0;
    int originY = 0;
    int pop = 0;
};

using CrowdPlanFn = CrowdPlan (*)(const CrowdPlanIn&);

// ГОРОД: гарантированное ядро кладки (дома, срезанные худшей внутренней
// экскурсией кольца куртины) + верхний квартал. Донжон НАХОДИТСЯ, а не
// передаётся: самая высокая крыша, поднятая генератором (владелец
// 2026-09-13: «стража везде, просто в верхнем квартале её больше,
// например половина»); размер квартала — чистая функция паствы.
// Цена консервативного скаляра известна и названа ещё прежним домом: лепестки
// вдоль тракта держат дома и улицы, но не толпу — рим тише сердца. Закрыть
// это значит отдать популятору НАСТОЯЩИЙ контур города вместо числа
// (генератор его уже считает — sub/gens/kit/outline.h); это следующий
// инкремент, не скрытый долг.
inline CrowdPlan crowd_plan_city(const CrowdPlanIn& in) {
    CrowdPlan plan;
    plan.discRadius = std::min(
        city_house_radius(in.pop),
        wall_inner_bound(city_core_radius(in.pop), city_wall_roughness()));
    plan.keepX = in.centerX;
    plan.keepY = in.centerY;
    const Structure* keep = nullptr;
    for (const Structure& st : in.structures) {
        if (st.kind != Structure::House) continue;
        if (st.x < float(in.originX)
            || st.x >= float(in.originX + kCellSize)) continue;
        if (st.y < float(in.originY)
            || st.y >= float(in.originY + kCellSize)) continue;
        if (keep == nullptr || st.height > keep->height) keep = &st;
    }
    if (keep != nullptr) {
        plan.keepX = keep->x;
        plan.keepY = keep->y;
        plan.quarterR = city_upper_radius(in.pop);
        plan.haveQuarter = true;
    }
    return plan;
}

// СТОЛИЦА: свой модуль плана (вердикт владельца 2026-11-13 на развилку
// генератора — «Свой модуль — копия городского»; ряд M-522). Сегодня — полная
// копия городского плана под своим именем, и копия законна (AGENTS §4 п.5:
// под модули общего не создаётся); толпа столицы стоит в той геометрии,
// которую строит её генератор (`gen_capital@src/sub/gens/capital.cpp`), и
// различие столицы пишется здесь и там, город его не увидит.
inline CrowdPlan crowd_plan_capital(const CrowdPlanIn& in) {
    CrowdPlan plan;
    plan.discRadius = std::min(
        city_house_radius(in.pop),
        wall_inner_bound(city_core_radius(in.pop), city_wall_roughness()));
    plan.keepX = in.centerX;
    plan.keepY = in.centerY;
    const Structure* keep = nullptr;
    for (const Structure& st : in.structures) {
        if (st.kind != Structure::House) continue;
        if (st.x < float(in.originX)
            || st.x >= float(in.originX + kCellSize)) continue;
        if (st.y < float(in.originY)
            || st.y >= float(in.originY + kCellSize)) continue;
        if (keep == nullptr || st.height > keep->height) keep = &st;
    }
    if (keep != nullptr) {
        plan.keepX = keep->x;
        plan.keepY = keep->y;
        plan.quarterR = city_upper_radius(in.pop);
        plan.haveQuarter = true;
    }
    return plan;
}

// ДЕРЕВНЯ: ядро домов; при частоколе — срез его кольцом. Квартала нет.
inline CrowdPlan crowd_plan_village(const CrowdPlanIn& in) {
    CrowdPlan plan;
    const float core = village_core_radius(in.pop);
    plan.discRadius = !village_is_walled(in.pop)
        ? core
        : std::min(core,
                   wall_inner_bound(village_wall_radius(in.pop),
                                    kSettlementWallRing.villageRoughness));
    return plan;
}

// Таблица «род → модуль» по отрезку Rooted оси (squad_archetype: 1..9).
struct CrowdPlanRow {
    SquadType kind;
    CrowdPlanFn plan;
};
inline constexpr CrowdPlanRow kCrowdPlanRows[] = {
    // Столица — свой модуль плана, как и свой генератор (ряд M-522). Здесь
    // стоял nullptr — «nullptr, как в геометрии» (ответ владельца
    // 2026-11-07), пока генератора не было: род без модуля не получает чужой
    // стройки (шрам GEN-5). Генератор пришёл — пришёл и план.
    { SquadType::Capital, crowd_plan_capital },
    { SquadType::City,    crowd_plan_city },
    { SquadType::Village, crowd_plan_village },
    // Прочие рода стоят в деревенском ядре, «пока свой закон планировки не
    // заработал имени» (§42 Инк 5) — общий модуль есть ДАННЫЕ их строк, а
    // не ветка по роду (та же норма, что nullptr-строки kAiModuleRows).
    { SquadType::Spire,   crowd_plan_village },
    { SquadType::Ruin,    crowd_plan_village },
    { SquadType::Lair,    crowd_plan_village },
    { SquadType::Shrine,  crowd_plan_village },
    { SquadType::Mine,    crowd_plan_village },
    { SquadType::Tower,   crowd_plan_village },
};

// Порядок строк = номера оси (отрезок прибит static_assert-ами в
// tables/squad_type.h: Capital == 1 … Tower == 9); новый Rooted-род НЕ
// СОБЕРЁТСЯ без своей строки.
inline constexpr bool crowd_plan_rows_in_order() {
    for (std::size_t i = 0; i < std::size(kCrowdPlanRows); ++i)
        if (int(kCrowdPlanRows[i].kind) != int(SquadType::Capital) + int(i))
            return false;
    return true;
}
static_assert(crowd_plan_rows_in_order(),
              "kCrowdPlanRows обязана идти в порядке Rooted-отрезка оси");
static_assert(std::size(kCrowdPlanRows)
                  == std::size_t(SquadType::Tower)
                         - std::size_t(SquadType::Capital) + 1u,
              "у КАЖДОГО Rooted-рода обязана быть своя строка плана толпы");

// Дверь диспетча: род вне Rooted-отрезка плана не имеет (ходоки толпой
// поселения не встают) — nullptr, как и род, чья строка ещё не пришла.
inline CrowdPlanFn crowd_plan_for(SquadType rooted) {
    const int ord = int(rooted);
    if (ord < int(SquadType::Capital) || ord > int(SquadType::Tower))
        return nullptr;
    return kCrowdPlanRows[std::size_t(ord - int(SquadType::Capital))].plan;
}

} // namespace sm::sub
