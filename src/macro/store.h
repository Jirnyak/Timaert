// ── MacroStore — ГЛАДКАЯ ПАМЯТЬ МАКРОМИРА (эпик 2, M-106; шаг 1б) ──────────
//
// ЗАКОН ГЛАДКОЙ ПАМЯТИ (AGENTS §3, владелец 2026-09-22): память сквадов —
// преаллоцированный массив фиксированной длины, у ВСЕХ энтити один набор
// компонент и один размер; род — колонка, пустота оплачена осознанно.
// Раскладка — SoA-колонки (вердикт 2026-09-25): горячий тик читает ~150 Б
// контекста решения на агента, не таща 41 КБ инвентаря; каждая система
// ходит только по своим колонкам.
//
// ФОРМА — `std::array<T, кап>` в ОДНОЙ heap-структуре, не векторы (вердикт
// владельца 2026-09-25): тип физически не умеет ресайзиться (инвариант в
// ТИПЕ, а не в дисциплине), sizeof закреплён ассертом целиком, смещения
// колонок — константы компиляции, аллокация одна — при рождении мира.
//
// СПИСОК КОЛОНОК — ОДИН ИСТОЧНИК (ЗАКОН СЛОВАРЯ п.3): X-macro ниже рождает
// и члены, и обнуление слота при рождении — забытая при переиспользовании
// колонка (призрак мёртвого) невозможна ПО ПОСТРОЕНИЮ, а не по памяти
// ревьюера. Условных компонент нет: CharacterSheet, SquadOrders,
// BodyEquipment, DesignCharacterTag — колонки ВСЕХ слотов (вердикт
// 2026-09-25: «полностью пользоваться анкетой своего сквада» — лорд НОСИТ
// артефактный меч, и авто-бой судит его тем же листом).
//
// ЧЕГО ЗДЕСЬ НЕТ, СОЗНАТЕЛЬНО:
//  · PlayerTag/PlayerSquadTag — ноль-или-один на мир, это индекс слота в
//    GameState (проводка — шаг 1в), не колонка на 32768;
//  · эмитента ординалов — он у GameState (nextMacroSpawnOrdinal), store
//    агностичен к тому, кто и зачем рождает (ЗАКОН АГНОСТИЧНОСТИ);
//  · порядка обхода — закон порядка (squad_walk.h) живёт НАД хранилищем:
//    слоты переиспользуются, потому «по слотам» ≠ «по ординалу».
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "core/stacks.h"
#include "ecs/components.h"
#include "macro/agent_memory.h"
#include "macro/character_sheet.h"
#include "macro/spell_book_state.h"

namespace sm {

// Колонка на компоненту; порядок — от горячих к холодным не важен (SoA:
// каждая колонка — свой сплошной блок), список отсортирован по смыслу.
// dead — БАЙТ СУДЬБЫ, не занятость слота: мёртвый сквад стоит трупом до
// слива (AI-2), занятость держит служебный alive.
#define SM_MACRO_STORE_COLUMNS(X)                                            \
    X(spawnId,   ecs::MacroSpawnId)                                          \
    X(cell,      ecs::MacroCell)                                             \
    X(kind,      ecs::NPCKind)                                               \
    X(visual,    ecs::MacroVisual)                                           \
    X(character, ecs::NpcCharacter)                                          \
    X(level,     ecs::NpcLevel)                                              \
    X(traits,    ecs::NpcTraits)                                             \
    X(pools,     ecs::Pools)                                                 \
    X(runtime,   ecs::MacroNpcRuntime)                                       \
    X(spellBook, SpellBook)                                                  \
    X(memory,    AgentMemory)                                                \
    X(roster,    ecs::SquadRoster)                                           \
    X(inventory, ecs::NpcInventory)                                          \
    X(sheet,     CharacterSheet)                                             \
    X(orders,    ecs::SquadOrders)                                           \
    X(gear,      ecs::BodyEquipment)                                         \
    X(designTag, ecs::DesignCharacterTag)                                    \
    X(dead,      std::uint8_t)

// Хэндл слота: «нет элемента» — последнее значение типа (закон узкого
// индекса, S26; кап 32768 в u16 умещается с запасом). Поколение — та же
// защита от протухшей ссылки, что версия в хэндле EnTT, только своя.
inline constexpr std::uint16_t kMacroNoSlot = 0xFFFFu;
struct MacroHandle {
    std::uint16_t slot = kMacroNoSlot;
    std::uint16_t gen  = 0;
};
static_assert(kMacroEntityCap < kMacroNoSlot,
              "кап обязан умещаться в u16 с местом под «нет элемента»");

struct MacroStore {
#define SM_X(name, T) std::array<T, kMacroEntityCap> name;
    SM_MACRO_STORE_COLUMNS(SM_X)
#undef SM_X

    // Служебное: поколение слота, занятость, freelist стеком.
    std::array<std::uint16_t, kMacroEntityCap> generation;
    std::array<std::uint8_t,  kMacroEntityCap> alive;
    std::array<std::uint16_t, kMacroEntityCap> freeSlots;
    std::uint32_t freeCount  = 0;
    std::uint32_t aliveCount = 0;

    bool valid(MacroHandle h) const {
        return h.slot < kMacroEntityCap && alive[h.slot] != 0
               && generation[h.slot] == h.gen;
    }
};

// КАРТИНА ПАМЯТИ ЗАКРЕПЛЕНА ЗАМЕРОМ (AGENTS п.10): 1 531 150 344 Б =
// 1460.2 МиБ по капу 32768 — резидентно с рождения мира, пустота оплачена
// (вердикт 2026-09-25; в замеренном мире живых ~10.7k слотов = 65 %, пик
// 78 %). Из них инвентарь 1280 МиБ, гир 160, интересы придут с M-90.
static_assert(sizeof(MacroStore) == 1531150344ull,
              "гладкая память макромира: новая колонка = новая цена, "
              "названная вслух (AGENTS п.10)");

// Рождение мира: один new, freelist убывающим порядком — первый рождённый
// получает слот 0 (читаемость дампов, ничего больше; законом порядок слотов
// не является — см. шапку).
inline std::unique_ptr<MacroStore> make_macro_store() {
    auto s = std::make_unique<MacroStore>();
    for (std::size_t i = 0; i < kMacroEntityCap; ++i)
        s->freeSlots[i] = std::uint16_t(kMacroEntityCap - 1u - i);
    s->freeCount = std::uint32_t(kMacroEntityCap);
    return s;
}

// Рождение слота. Отказ — в точке рождения и ВСЛУХ (метод §5 п.3): полный
// массив возвращает невалидный хэндл, и это печатается, а не глотается.
// Все колонки слота обнуляются ИЗ ЕДИНОГО СПИСКА — призрак прежнего жильца
// невозможен по построению.
inline MacroHandle store_birth(MacroStore& s) {
    if (s.freeCount == 0) {
        std::fprintf(stderr,
                     "[store] ОТКАЗ РОЖДЕНИЯ: все %zu слотов заняты\n",
                     kMacroEntityCap);
        return MacroHandle{};
    }
    const std::uint16_t slot = s.freeSlots[--s.freeCount];
#define SM_X(name, T) s.name[slot] = T{};
    SM_MACRO_STORE_COLUMNS(SM_X)
#undef SM_X
    s.alive[slot] = 1;
    ++s.aliveCount;
    return MacroHandle{slot, s.generation[slot]};
}

// Смерть слота: поколение растёт — всякий старый хэндл мертвеет мгновенно;
// колонки НЕ трутся здесь (их обнулит следующее рождение из списка) —
// значит читать мёртвый слот нельзя ничем, кроме valid()-гарды.
inline void store_death(MacroStore& s, MacroHandle h) {
    if (!s.valid(h)) return;   // двойная смерть — no-op, не порча freelist
    s.alive[h.slot] = 0;
    ++s.generation[h.slot];
    s.freeSlots[s.freeCount++] = h.slot;
    --s.aliveCount;
}

} // namespace sm
