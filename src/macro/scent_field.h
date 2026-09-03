// ПОЛЯ СЛЕДОВ ФРАКЦИЙ — клеточный ярус хемотаксиса (CANON S10, владелец
// 2026-09-03: «у нас мир связный тор из клеток локальный изотропный надо это
// использовать … на каждую фракцию своё поле следов и все будут видеть — это
// позволит и бандитов и масштабные войны … надо на века единую систему»).
//
// Threat живёт на ОКРУГАХ (страх места, threat_field.h); след живёт на
// КЛЕТКАХ (запах существа). ДВА канала на фракцию — две честные оси
// (владелец: «наверное надо поля цены типа там богато и поля силы (от уровня
// и размера)»; его примеры: гружёный караван слаб но вкусен, царь-крестьянин
// один но опасен, орава дезертиров — средние, но много):
//
//   · СИЛА — вклад = squad_power, ЕДИНСТВЕННЫЙ закон боя (auto_battle.h):
//     уровень×размер ростера уже внутри, второй формулы силы не существует
//     по построению. Поле отвечает «куда», закон при контакте — «бить или
//     бежать»: остывший след занижает, ошибки консервативны и
//     самокорректируются рефлексом.
//   · ЦЕНА — вклад = души × цена строки + ценность груза (inventory_value,
//     та же таблица цен, что найм/жалованье/threat/лут). Хищник идёт вверх
//     по градиенту ЦЕНЫ под фильтром силы — голод ведёт к вкусному.
//
// ФИЗИКА — как threat, ярусом ниже: диффузия po2-доля восьми соседям раз в
// день (тор, изотропия — конус запаха вокруг линии следа) + распад >>1 раз
// в N дней. Писатель один — think сквада (dispatch); читатели множатся
// бесплатно: рефлекс охоты сегодня, военная карта присутствия (сила) и
// экономическая (цена) для стратегического ИИ потом.
//
// ЕДЕТ В СЕЙВЕ (вердикт владельца; движение не фактируется — реплеить след
// не из чего, threat-реплей здесь не работает). Ёмкость = реестр фракций по
// построению (kFactionCount ≤ kMaxFactions=64 — существующий кап uint64
// враг-маски субмира, возведённый в единую константу мира).
#pragma once

#include <cstdint>
#include <vector>

#include "macro/faction.h"

namespace sm {

// Распад: >>1 раз в N дней. След остывает вдвое быстрее страха округи
// (kThreatDecayDays=8): дорожка недельной давности — уже не след, а слух.
inline constexpr int kScentDecayDays = 4;

// Диффузия: 1/8 клетки в день уходит восьми соседям поровну — конус запаха
// вокруг линии прохода; изотропия тора, ни направления, ни анизотропии.
inline constexpr int kScentDiffusionShift = 3;

// Квант вклада: деньги и мощь ложатся в uint16 сдвигом po2 (сумка каравана в
// тысячи серебра садится в поле сотнями единиц; сатурация = «очень жирный
// след», честный потолок, не баг).
inline constexpr int kScentQuantShift = 2;

struct ScentField {
    std::int32_t w = 0, h = 0;
    std::int32_t factions = 0;   // планов в векторах; == kFactionCount мира
    // [f*w*h + y*w + x] — план на фракцию, плотно; пустые планы почти
    // бесплатны (диффузия перешагивает нули).
    std::vector<std::uint16_t> strength;
    std::vector<std::uint16_t> wealth;

    bool sized_for(int mapW, int mapH) const {
        const std::size_t n =
            std::size_t(factions) * std::size_t(w) * std::size_t(h);
        return w == mapW && h == mapH && factions == kFactionCount
               && strength.size() == n && wealth.size() == n;
    }
};

// Пересбор под мир (генезис, загрузка чужого размера, рост реестра фракций):
// оба канала в ноль — холодный старт, мир протаптывает следы сам.
inline void scent_reset(ScentField& sf, int mapW, int mapH) {
    sf.w = mapW;
    sf.h = mapH;
    sf.factions = kFactionCount;
    const std::size_t n =
        std::size_t(kFactionCount) * std::size_t(mapW) * std::size_t(mapH);
    sf.strength.assign(n, 0u);
    sf.wealth.assign(n, 0u);
}

// Свежесть поля — как nav_ensure: несовпадение размеров (генезис, чужой
// сейв, рост реестра) = холодный пересбор. Зовут оба владельца ритма —
// think-свип (перед вкладами) и world_tick (перед физикой дня).
inline void scent_ensure(ScentField& sf, int mapW, int mapH) {
    if (!sf.sized_for(mapW, mapH)) scent_reset(sf, mapW, mapH);
}

inline std::size_t scent_idx(const ScentField& sf, int f, int x, int y) {
    return (std::size_t(f) * std::size_t(sf.h) + std::size_t(y))
               * std::size_t(sf.w)
           + std::size_t(x);
}

inline std::uint16_t scent_strength_at(const ScentField& sf, int f,
                                       int x, int y) {
    if (f < 0 || f >= sf.factions) return 0u;
    return sf.strength[scent_idx(sf, f, x, y)];
}

inline std::uint16_t scent_wealth_at(const ScentField& sf, int f,
                                     int x, int y) {
    if (f < 0 || f >= sf.factions) return 0u;
    return sf.wealth[scent_idx(sf, f, x, y)];
}

// Вклад сквада в СВОЮ клетку (квант po2, сатурация по потолку uint16).
// `power` и `worth` приходят сырыми — закон боя и таблица цен, — квантует
// само поле, чтобы ни один писатель не изобрёл свой сдвиг.
inline void scent_deposit(ScentField& sf, int f, int x, int y,
                          std::uint32_t power, std::uint32_t worth) {
    if (f < 0 || f >= sf.factions) return;
    if (x < 0 || x >= sf.w || y < 0 || y >= sf.h) return;
    const std::size_t i = scent_idx(sf, f, x, y);
    const std::uint32_t s =
        std::uint32_t(sf.strength[i]) + (power >> kScentQuantShift);
    const std::uint32_t v =
        std::uint32_t(sf.wealth[i]) + (worth >> kScentQuantShift);
    sf.strength[i] = s > 0xFFFFu ? 0xFFFFu : std::uint16_t(s);
    sf.wealth[i]   = v > 0xFFFFu ? 0xFFFFu : std::uint16_t(v);
}

// Дневной шаг физики: диффузия обоих каналов всех планов + распад по
// календарю мира (сейв/лоад не сдвигает день полураспада). Зовётся
// world_tick'ом рядом с threat_field_daily.
void scent_field_daily(ScentField& sf, int day);

} // namespace sm
