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
//     уровень×размер контейнера уже внутри, второй формулы силы не существует
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
// не из чего, threat-реплей здесь не работает).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/caps.h"    // kWorldCellCap — шаг плана и кап клеток
#include "core/torus.h"   // cell_of — адрес клетки внутри плана
#include "tables/faction.h"

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

// ── ПОЛЕ СЛЕДОВ — ДВА ПЛОСКИХ КАНАЛА ПОД КАПОМ (ломоть 4 сейв-эпика, M-180,
// 2026-11-10) ────────────────────────────────────────────────────────────
// Здесь стояли два `std::vector<std::uint16_t>` длиной «фракций реестра ×
// клеток мира» и член `factions` — копия числа `kFactionCount`, известного на
// сборке. Теперь у поля есть РАЗМЕР: план на КАЖДУЮ строку рода 6 под его
// единственным потолком (`kMaxFactions@src/tables/faction.h`, 64), в плане —
// клетка на клетку капа мира (`kWorldCellCap@src/core/caps.h`). Память одна у
// мира 8×8 фикстуры и у мира 1024×1024 с полным реестром (ЗАКОН ГЛАДКОЙ
// ПАМЯТИ), и снимается она одним куском.
//
// АДРЕС: `[f × kWorldCellCap + cell_of(x, y, сторона)]`. Шаг плана — КАП, а не
// площадь мира: план фракции лежит на одном месте при любой стороне мира, и
// шаг не делит и не умножает на рантайм-число.
//
// ЦЕНА НАЗВАНА: 64 × 2^20 × 2 Б = 128 МиБ на канал, 256 МиБ на поле. Планы
// выше `kFactionCount` (строк реестра сегодня меньше потолка) стоят нулём:
// писать в них некому — индекса такой фракции не существует, — а физика дня
// обходит только планы живых строк реестра, то есть пустота оплачена
// памятью, но не тактами.
inline constexpr std::size_t kScentPlaneCells = kWorldCellCap;
inline constexpr std::size_t kScentChannelCells =
    std::size_t(kMaxFactions) * kScentPlaneCells;
struct ScentField {
    std::int32_t w = 0, h = 0;
    std::uint16_t strength[kScentChannelCells] = {};
    std::uint16_t wealth[kScentChannelCells] = {};

    // «Скроено ли поле под этот мир». Мир вне капа клеток или не законный
    // тор (сторона — степень двойки, квадрат) поля не получает вовсе: оно
    // остаётся холодным, и все двери ниже на нём молчат (fail-closed).
    bool sized_for(int mapW, int mapH) const {
        return w == mapW && h == mapH && w > 0;
    }
};
static_assert(sizeof(ScentField)
                  == 2u * sizeof(std::int32_t)
                         + 2u * kScentChannelCells * sizeof(std::uint16_t),
              "поле следов: шапка 8 Б + два канала по u16 на клетку капа "
              "каждого плана, без хвоста");

// Пересбор под мир (генезис, загрузка): оба канала в ноль ПО ВСЕМУ КАПУ —
// холодный старт, мир протаптывает следы сам, а хвост за миром и планы выше
// реестра стоят нулём. Мир, которому поле не положено (не тор или больше
// капа), оставляет поле холодным с нулевой шапкой.
inline void scent_reset(ScentField& sf, int mapW, int mapH) {
    const bool legal = world_shape_ok(mapW, mapH)
                       && world_cells_within_cap(mapW, mapH) > 0u;
    sf.w = legal ? mapW : 0;
    sf.h = legal ? mapH : 0;
    std::memset(sf.strength, 0, sizeof(sf.strength));
    std::memset(sf.wealth, 0, sizeof(sf.wealth));
}

// Свежесть поля — как nav_ensure: несовпадение размеров = холодный пересбор.
// Поле кроит рождение мира (`generate_macro_world@src/macro/world_gen.cpp`) и
// загрузка; оба владельца ритма — think-свип (перед вкладами) и world_tick
// (перед физикой дня) — зовут эту дверь как сверку, и на скроенном поле она
// не делает ничего. Мир без права на поле (см. `scent_reset`) держит его
// холодным и пересбором каждый тик не платит.
inline void scent_ensure(ScentField& sf, int mapW, int mapH) {
    if (sf.sized_for(mapW, mapH)) return;
    const bool legal = world_shape_ok(mapW, mapH)
                       && world_cells_within_cap(mapW, mapH) > 0u;
    if (!legal && sf.w == 0 && sf.h == 0) return;
    scent_reset(sf, mapW, mapH);
}

// Адрес клетки канала. Зовётся только на скроенном поле и с индексом плана
// внутри реестра — оба условия держат двери ниже.
inline std::size_t scent_idx(const ScentField& sf, int f, int x, int y) {
    return std::size_t(f) * kScentPlaneCells + std::size_t(cell_of(x, y, sf.w));
}

inline std::uint16_t scent_strength_at(const ScentField& sf, int f,
                                       int x, int y) {
    if (f < 0 || f >= kFactionCount || sf.w <= 0) return 0u;
    return sf.strength[scent_idx(sf, f, x, y)];
}

inline std::uint16_t scent_wealth_at(const ScentField& sf, int f,
                                     int x, int y) {
    if (f < 0 || f >= kFactionCount || sf.w <= 0) return 0u;
    return sf.wealth[scent_idx(sf, f, x, y)];
}

// Вклад сквада в СВОЮ клетку (квант po2, сатурация по потолку uint16).
// `power` и `worth` приходят сырыми — закон боя и таблица цен, — квантует
// само поле, чтобы ни один писатель не изобрёл свой сдвиг.
inline void scent_deposit(ScentField& sf, int f, int x, int y,
                          std::uint32_t power, std::uint32_t worth) {
    if (f < 0 || f >= kFactionCount) return;
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

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-180, ломоть 4 сейв-эпика) ───────────────────
// Судит КОМПИЛЯТОР, а не ревью: структура состояния мира, в которую заложили
// вектор, строку, карту, умный указатель, функтор или виртуальный метод, отсюда
// НЕ СОБЕРЁТСЯ, и сообщение назовёт тип поимённо (`core/row_law.h`, наряд
// M-169). Новая структура в этом файле обязана появиться и в этом списке — за
// полнотой списка следит `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::ScentField);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::ScentField, &sm::ScentField::w, &sm::ScentField::h,
    &sm::ScentField::strength, &sm::ScentField::wealth);
