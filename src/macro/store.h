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
#include "ecs/world.h"
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

// Сам MacroHandle живёт в ecs/components.h (шаг 2 1е, вердикт Б с.19):
// его несёт через шов миров компонента ecs::MacroOrigin. Здесь — его законы.
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

// Слот макро-сквада по entt-мосту (шаг 1в; мост умирает в 1е вместе с этой
// дверью). Сущность без MacroSlot здесь незаконна — get громко падает в
// дебаге, как и всякий доступ мимо закона рождения.
inline std::uint16_t slot_of(entt::registry& reg, entt::entity e) {
    return reg.get<ecs::MacroSlot>(e).slot;
}

// Store из контекста реестра — МОСТ ПЕРЕЕЗДА, как PlayerSquadCache: живёт
// в ctx мира (не глобальное состояние — умирает с миром), чтобы ~30 дверей
// с сигнатурой (World&) не рябили параметром на время флипа. Ставится
// одной точкой на рождении мира; умирает в 1е вместе с MacroSlot.
// Колонка по ТИПУ компоненты — мост 1в: тип выбирает массив, ошибиться
// колонкой невозможно (типы колонок уникальны, список один — X-macro).
template <typename C> inline auto& store_col(MacroStore& s) = delete;
#define SM_X(name, T)                                                        \
    template <> inline auto& store_col<T>(MacroStore& s) { return s.name; }
SM_MACRO_STORE_COLUMNS(SM_X)
#undef SM_X

inline void store_attach(ecs::World& w, MacroStore* st) {
    w.reg.ctx().insert_or_assign(std::move(st));
}
inline MacroStore& store_of(ecs::World& w) {
    return *w.reg.ctx().get<MacroStore*>();
}
inline const MacroStore& store_of(const ecs::World& w) {
    return *w.reg.ctx().get<MacroStore*>();
}
inline MacroStore& store_of(entt::registry& reg) {
    return *reg.ctx().get<MacroStore*>();
}

// Хэндл из entt-моста (шаг 1г). slot_of отвечает голым слотом БЕЗ поколения —
// долгоживущей ссылке этого мало: слот переиспользуется, и только пара
// {slot,gen} мертвеет вместе с жильцом. Сущность без MacroSlot незаконна,
// как и в slot_of.
inline MacroHandle handle_of(entt::registry& reg, entt::entity e) {
    const std::uint16_t slot = slot_of(reg, e);
    return MacroHandle{slot, store_of(reg).generation[slot]};
}

// Упаковка хэндла в 32 бита для POD-конвертов (BattleFact, GameEvent):
// слот в нижних 16, поколение в верхних. «Никого» — ВСЕ ЕДИНИЦЫ, и это
// закрывает молчаливую ловушку: прежний сентинель 0 был ЛЕГАЛЬНЫМ слотом
// (первый рождённый — слот 0), и сквад слота 0 при смерти молча становился
// безымянным. Низшие 16 бит == kMacroNoSlot — единственный признак «нет».
inline constexpr std::uint32_t kMacroHandleNoneBits = 0xFFFFFFFFu;
inline constexpr std::uint32_t macro_handle_bits(MacroHandle h) {
    return h.slot == kMacroNoSlot
        ? kMacroHandleNoneBits
        : (std::uint32_t(h.slot) | (std::uint32_t(h.gen) << 16));
}
inline constexpr MacroHandle macro_handle_from_bits(std::uint32_t bits) {
    return (bits & 0xFFFFu) == kMacroNoSlot
        ? MacroHandle{}
        : MacroHandle{std::uint16_t(bits & 0xFFFFu),
                      std::uint16_t(bits >> 16)};
}

// ОБРАТНАЯ ДВЕРЬ МОСТА (шаг 1г; умирает в 1е вместе с MacroSlot): entt-тело
// носителя слота. Линейный скан моста — законен только ВНЕ тика (клик UI,
// вход в бой); в 1е двери принимают слот, и нужда в скане исчезает.
inline entt::entity macro_entity_of(entt::registry& reg, MacroHandle h) {
    if (!store_of(reg).valid(h)) return entt::null;
    for (auto [e, ms] : reg.view<ecs::MacroSlot>().each())
        if (ms.slot == h.slot) return e;
    return entt::null;
}

// ── ДВОЙНАЯ ДВЕРЬ СОСТОЯНИЯ ТЕЛА (закон записи, sub/record.h) ─────────────
// Макро-сквад (несёт MacroSlot) отвечает КОЛОНКОЙ store; тело сцены без
// бэклинка — «само себе запись» — своей entt-компонентой. Одна дверь на оба
// рода читателя: двери листа/полос/сумки зовутся с обоими.
template <typename C>
inline C* body_state(entt::registry& reg, entt::entity e) {
    if (const auto* ms = reg.try_get<ecs::MacroSlot>(e))
        return &store_col<C>(store_of(reg))[ms->slot];
    return reg.try_get<C>(e);
}
template <typename C>
inline const C* body_state(const entt::registry& reg, entt::entity e) {
    return body_state<C>(const_cast<entt::registry&>(reg), e);
}

// Та же дверь по хэндлу — БЕЗ entt вовсе: колонка под valid()-гардой.
// Это ЦЕЛЕВАЯ форма чтения макро-сквада; entt-перегрузки выше умирают в 1е.
template <typename C>
inline C* body_state(MacroStore& s, MacroHandle h) {
    return s.valid(h) ? &store_col<C>(s)[h.slot] : nullptr;
}
template <typename C>
inline const C* body_state(const MacroStore& s, MacroHandle h) {
    return body_state<C>(const_cast<MacroStore&>(s), h);
}

// Судьба — та же двойная дверь: у макро-сквада смерть лежит байтом колонки
// (труп стоит до слива, AI-2), у тела сцены — прежним тегом ecs::Dead.
inline bool macro_dead(entt::registry& reg, entt::entity e) {
    if (const auto* ms = reg.try_get<ecs::MacroSlot>(e))
        return store_of(reg).dead[ms->slot] != 0;
    return reg.all_of<ecs::Dead>(e);
}
inline bool macro_dead(const entt::registry& reg, entt::entity e) {
    return macro_dead(const_cast<entt::registry&>(reg), e);
}
// Судьба по хэндлу: протухший хэндл отвечает «мёртв» — fail-closed чтение,
// жилец слота сменился, и спрашивать о нём больше нечего.
inline bool macro_dead(const MacroStore& s, MacroHandle h) {
    return !s.valid(h) || s.dead[h.slot] != 0;
}
inline void macro_mark_dead(entt::registry& reg, entt::entity e) {
    if (const auto* ms = reg.try_get<ecs::MacroSlot>(e)) {
        store_of(reg).dead[ms->slot] = 1;
        return;
    }
    reg.emplace_or_replace<ecs::Dead>(e);
}
// Смерть по хэндлу: протухший хэндл — no-op (жилец уже сменился, мертвить
// некого); это та же fail-closed пара к macro_dead(store, h) выше.
inline void macro_mark_dead(MacroStore& s, MacroHandle h) {
    if (s.valid(h)) s.dead[h.slot] = 1;
}

} // namespace sm
