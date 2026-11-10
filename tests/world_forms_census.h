// ПЕРЕПИСЬ ФОРМ МИРА ПО ДВУМ ОСЯМ — прибор первого ломтя эпика инварианта
// (ряд M-441; замысел — CANON S10 «ИНВАРИАНТ МИРА: СТОИМОСТЬ И ДУШИ —
// ЭНЕРГИЯ, КОТОРАЯ ТОЛЬКО МЕНЯЕТ ФОРМУ»).
//
// ЧТО ОН СЧИТАЕТ. Две оси, которые не смешиваются: СТОИМОСТЬ в квантах
// (колонка `value` строки предмета) и ДУШИ головами. По каждой оси — формы,
// у которых в мире сегодня ЕСТЬ носитель:
//   · ЗАЛЕЖИ   — ряды реестра полей (`resource_field_def@src/macro/
//                resource_field.h`), клетка за клеткой по всему тору;
//   · ЛОКАЛЬНОЕ — контейнеры живых слотов стора (сводка инвентаря у двери
//                записи) и аватары сквадов.
// ТРЕТЬЕЙ ФОРМЫ — РЕЗЕРВА — ПРИБОР НЕ ПЕЧАТАЕТ, потому что носителя у неё в
// мире нет ни одного (ряд M-442 его заводит). Прибор не печатает того, чего
// в мире нет: колонка с назначенным нулём не меряет, а успокаивает.
//
// ЧЕМ ОЦЕНИВАЕТСЯ ЗАЛЕЖЬ — ТОЙ ЖЕ ДВЕРЬЮ, КОТОРОЙ МИР ЕЁ ВЫДАЁТ. Единица поля
// стоит ровно то, что транзакция `field_harvest@src/macro/resource_field.h`
// положила бы в руки на ЭТОЙ клетке: ординал выхода называет
// `field_yield_ordinal@src/macro/resource_field.h` (фича клетки старше
// умолчания ряда), количество — `resource_field_read@src/macro/
// resource_field.h`. Своей таблицы «ряд → предмет» прибор не заводит, и ось
// залежи называет НЕ ОН, а строка реестра:
//   · выход-ПРЕДМЕТ  → ось стоимости, единицы × `value` строки предмета;
//   · выход-СУЩЕСТВО → ось душ, единица есть голова;
//   · выхода НЕТ     → единицы без цены (дикая жила: рода у неё нет, род
//                      ресурса называет фича шахты, без шахты добычи не
//                      существует) — печатаются отдельно и ни в одну сумму
//                      не входят.
// Рядом с каждым рядом печатается, сколько его единиц оценено СТРОКОЙ ФИЧИ, а
// не умолчанием ряда (`FeatureDef::yieldItem@src/tables/feature_def.h`): дверь
// выхода ставит предмет фичи старше ряда и НЕ спрашивает, тот ли это ряд.
// Лён поверх пашни и металл шахты поверх жилы — замысел; дерево или зверь на
// клетке шахты, отданные её металлом, — вопрос, и число показывает, сколько
// его в мире (ряд M-460).
//
// ЧЕГО ПРИБОР НЕ ВИДИТ, НАЗВАНО ВСЛУХ:
//   · субмир (сундуки, брошенное на земле, тела окна 3×3) — в безголовом
//     прогоне его нет вовсе, в живой игре он лежит мимо стора;
//   · стоимость САМИХ существ (`valueSouls` сводки): курса между осями нет
//     (CANON S10 п.4), душа считается головой и только головой;
//   · число поля фич под местом («паства») в сумму душ НЕ входит: те же люди
//     уже посчитаны головами в контейнерах, второй раз их складывать нельзя.
//     Оно печатается рядом как сверка двух носителей одной величины.
//
// ДОМ ФАЙЛА — tests/, а не src/: читателей у переписи сегодня два, и оба
// приборы (`balance_run` и свидетель `world_forms_census_test`). Дверь мира
// с нулём прод-звонящих заводить запрещено; когда генерация начнёт
// запоминать сумму форм числом мира (ряд M-442), перепись переедет вниз
// вместе со своим первым прод-читателем.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "bus/store.h"
#include "macro/features.h"
#include "macro/macro_world.h"
#include "macro/resource_field.h"
#include "macro/rooted_iter.h"
#include "macro/state.h"
#include "tables/items.h"
#include "tables/resource_field_id.h"
#include "tables/squad_type.h"

namespace sm {

inline constexpr std::size_t kFormRows = std::size_t(ResourceFieldId::Count);

struct WorldForms {
    // ── ЗАЛЕЖИ, по рядам реестра ─────────────────────────────────────────
    std::array<long long, kFormRows> depositValue{};   // кванты стоимости
    std::array<long long, kFormRows> depositHeads{};   // головы
    std::array<long long, kFormRows> depositBare{};    // единицы без выхода
    std::array<long long, kFormRows> depositByFeature{};   // единицы, чей
                                   // выход назвала фича клетки, а не ряд
    // ── ЛОКАЛЬНОЕ ────────────────────────────────────────────────────────
    long long localValue = 0;     // предметы всех контейнеров живых слотов
    long long localFolk = 0;      // людские головы в контейнерах
    long long localBeasts = 0;    // прочие головы в контейнерах
    long long localAvatars = 0;   // аватары ходячих сквадов (по одному)
    // ── СВЕРКА ВТОРОГО НОСИТЕЛЯ ДУШ (в суммы не входит) ──────────────────
    long long flockNumber = 0;    // Σ числа поля фич под клетками мест
    long long folkRooted = 0;     // людские головы в контейнерах мест

    long long value_deposits() const {
        long long v = 0;
        for (const long long x : depositValue) v += x;
        return v;
    }
    long long souls_deposits() const {
        long long s = 0;
        for (const long long x : depositHeads) s += x;
        return s;
    }
    long long souls_local() const {
        return localFolk + localBeasts + localAvatars;
    }
    // Сумма форм, у которых есть носитель. Это НЕ `V` и НЕ `S` канона:
    // резерва в ней нет, потому что его нет в мире.
    long long value_carried() const { return value_deposits() + localValue; }
    long long souls_carried() const { return souls_deposits() + souls_local(); }
};

// Перепись залежей: один проход по клеткам тора на ряд. Мир не правится.
inline void census_deposits(const MacroWorld& w, WorldForms& out) {
    if (!w.gs) return;
    const int W = w.gs->mapW;
    const int H = w.gs->mapH;
    for (std::size_t f = 0; f < kFormRows; ++f) {
        const ResourceFieldId row = ResourceFieldId(f);
        const bool creature =
            resource_field_def(row).yieldCreature != NPCType::Count;
        long long value = 0, heads = 0, bare = 0, byFeature = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int units = resource_field_read(w, row, x, y);
                if (units <= 0) continue;
                // Порядок тот же, что у транзакции: предмет фичи или ряда
                // старше существа ряда.
                const int ord = field_yield_ordinal(w, row, x, y);
                if (feature_def(w.gs->features.at(x, y)).yieldItem != nullptr)
                    byFeature += units;
                if (ord >= 0) {
                    const ItemDef* def = item_def_at(ord);
                    if (def) value += (long long)units * def->value;
                    else bare += units;
                } else if (creature) {
                    heads += units;
                } else {
                    bare += units;
                }
            }
        }
        out.depositValue[f] = value;
        out.depositHeads[f] = heads;
        out.depositBare[f] = bare;
        out.depositByFeature[f] = byFeature;
    }
}

// Перепись локального: контейнеры живых слотов — сводкой, которую держит
// дверь записи инвентаря, то есть без своего прохода по слотам контейнера.
inline void census_local(const MacroWorld& w, WorldForms& out) {
    if (!w.store) return;
    const MacroStore& st = *w.store;
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (st.alive[s] == 0) continue;
        const auto& sum = st.inventory[s].inv.sum();
        out.localValue += (long long)sum.valueItems;
        out.localFolk += (long long)sum.folkHeads;
        out.localBeasts += (long long)(sum.heads - sum.folkHeads);
        // Аватар — душа вне контейнера («одиночка = аватар с пустым
        // контейнером»). У места аватара-существа нет: его люди суть головы
        // его контейнера. Погребённый слот аватара уже не несёт.
        const bool rooted =
            squad_archetype(SquadType(st.runtime[s].squadType))
            == SquadArchetype::Rooted;
        if (!rooted && st.dead[s] == 0) out.localAvatars += 1;
    }
    if (!w.gs) return;
    for_each_rooted(st, [&](std::uint16_t slot) {
        const auto& c = st.cell[slot];
        out.flockNumber += feature_number_read(
            *w.gs, ecs::cell_x(c, w.gs->mapW), ecs::cell_y(c, w.gs->mapW));
        out.folkRooted += (long long)st.inventory[slot].inv.sum().folkHeads;
    });
}

inline WorldForms census_world_forms(const MacroWorld& w) {
    WorldForms f;
    census_deposits(w, f);
    census_local(w, f);
    return f;
}

} // namespace sm
