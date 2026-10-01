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
#include "macro/anketa.h"
#include "macro/spell_book_state.h"

namespace sm {

// Колонка на компоненту; порядок — от горячих к холодным не важен (SoA:
// каждая колонка — свой сплошной блок), список отсортирован по смыслу.
// dead — БАЙТ СУДЬБЫ, не занятость слота: мёртвый сквад стоит трупом до
// слива (AI-2), занятость держит служебный alive.
// playerFlag — «этот сквад (с этой анкетой) — игрок» (вердикт владельца
// 2026-09-29: «У НАС СИСТЕМА ИГРЫ ЧТО ЕСТЬ СКВАДЫ С АНКЕТАМИ И ЭТО ВСЁ и
// поэтому смена сквада это просто смена флажка»). Ровно один слот с 1 —
// инвариант держит дверь transfer_player_flag ниже; GameState несёт КЭШ
// (playerFlagBits), будущий стратегический скролл сквадов — ещё один
// звонящий той же двери.

// Обёртка флажка шириной в байт, а не голый u8: селектор колонки store_col
// ключуется ТИПОМ (одна колонка — один тип, список один), и второй голый
// u8 рядом с dead был бы переопределением специализации.
struct PlayerFlag {
    std::uint8_t on;
};

#define SM_MACRO_STORE_COLUMNS(X)                                            \
    X(spawnId,   ecs::MacroSpawnId)                                          \
    X(cell,      ecs::MacroCell)                                             \
    X(kind,      ecs::NPCKind)                                               \
    X(visual,    ecs::MacroVisual)                                           \
    X(character, ecs::NpcCharacter)                                          \
    X(name,      ecs::SquadName)                                             \
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
    X(playerFlag, PlayerFlag)                                                \
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

// КАРТИНА ПАМЯТИ ЗАКРЕПЛЕНА ЗАМЕРОМ (AGENTS п.10): 1 397 882 888 Б =
// 1333.1 МиБ по капу 32768 — резидентно с рождения мира, пустота оплачена
// (вердикт 2026-09-25). Из них инвентарь 1280 МиБ, гир 32 (M-183), имя 1
// (ход 2), интересы придут с M-90.
//
// ЗАПОЛНЕНИЕ КАПА ЗАМЕРЕНО (M-200, 2026-10-01, сид 12345): после генерации
// живых слотов 58, на границе первого сезона (день 32) разовый прыжок
// 54 → 11711 = 35.7 % капа, дальше плато. Прежние «~10.7k = 65 %, пик 78 %»
// этой шапки не воспроизвелись ни числом, ни долей — они мерились против
// другого капа. И это ЗАПАС, а не пустота (владелец, M-200): кап и есть
// место, куда мир растёт прыжками по одиннадцать тысяч слотов за тик.
static_assert(sizeof(MacroStore) == 1397882888ull,   // ход 2: +SquadName 32 Б = +1 МиБ; 5б: +playerFlag; M-183: gear 5124 → 1024
              "гладкая память макромира: новая колонка = новая цена, "
              "названная вслух (AGENTS п.10)");

// И ТА ЖЕ ЦЕНА, СВЕДЁННАЯ С ПЕРЕПИСЬЮ ШТАБЕЛЕЙ. `core/stacks.h` не может
// спросить `sizeof(MacroStore)` — он НИЖЕ по включению, — поэтому цену слота
// он СЧИТАЕТ из тех же типов (`kMacroStoreRowBytes`). Равенство ниже и есть
// шов между прозой переписи и правдой структуры: добавил колонку только сюда
// — красный ассерт назовёт забытую строку переписи; поправил только перепись
// — он же назовёт ложь в ней. Хвост 8 Б — два счётчика (`freeCount`,
// `aliveCount`), единственное в структуре, что не умножается на кап.
static_assert(sizeof(MacroStore)
                  == kMacroStoreRowBytes * kMacroEntityCap
                         + sizeof(std::uint32_t) * 2u,
              "перепись штабелей и гладкая память разъехались: цена слота "
              "kMacroStoreRowBytes (core/stacks.h) больше не равна колонкам");

// Рождение мира: один new, freelist убывающим порядком — первый рождённый
// получает слот 0 (читаемость дампов, ничего больше; законом порядок слотов
// не является — см. шапку).
//
// ПОЧЕМУ `s.reset(new …)`, А НЕ `auto s = std::make_unique<…>()` — ЦЕНА
// ЗАМЕРЕНА (2026-09-26). Именованная переменная С ИНИЦИАЛИЗАТОРОМ обязывает
// фронтенд ответить, не является ли инициализация константной; clang отвечает,
// ВЫЧИСЛЯЯ её — обходит всю структуру как дерево APValue и выбрасывает
// результат, потому что `new` константой не бывает. Это ПАРС тела, значит
// платит КАЖДЫЙ включивший заголовок, а не звонящий: `-ftime-trace` показал
// `EvaluateAsInitializer` = 53.24 с из 54.07 с (98.5 %), пустой TU с одним
// `#include "macro/store.h"` стоил 55.6 с и 6.58 ГиБ против 0.75 с и 194 МиБ
// без него, а `.o` выходил байт в байт тот же (1344 Б) — вся работа в мусор.
// Цена по дереву: 161 TU × ~90 с = 97 % машинного времени сборки, семь
// jetsam-смертей машины за 25.09. Без инициализатора вычислителю не за что
// зацепиться: те же 0.68 с и 208 МиБ. Рантайм идентичен — `make_unique<T>()`
// это и есть `new T()`. НЕ возвращать `auto s = …` (AGENTS §5 п.13).
//
// И ПОЧЕМУ СКОБКИ `()` ОСТАЮТСЯ, ХОТЯ ОНИ ПИШУТ 1333 МиБ НУЛЕЙ. ЭТО ЗАМЫСЕЛ,
// А НЕ НЕДОСМОТР (владелец, M-200, 2026-10-01, дословно): «как бы замысел
// был один ввыделить прям честно память под 32к сквада как будто они все
// есть и их инвентари полны - у нас по построениею не ддожно быть разниы для
// машины в контексте памяти пустой мир/забитый».
//
// Значит скобки — НОСИТЕЛЬ ЭТОГО ЗАКОНА, а не стиль: они делают профиль
// памяти ОДНИМ И ТЕМ ЖЕ у мира из 58 сквадов и у мира из 32 768. Отложенная
// материализация (взять страницу у ОС и нулей не писать) этот закон ломает
// ПО ПОСТРОЕНИЮ — она ровно и создаёт разницу «пустой дешёвый / забитый
// дорогой». Замерена она была честно и соблазн был числом: 476.6 МиБ вместо
// 1333.1 на плато, а отрава её РАЗРЕШАЛА — 34 дня с байтом 0xCC в
// непрожитых слотах дали побайтово тот же мир (M-200, замер 2). Отвергнута
// не риском, а законом, и второй довод того же знака — ПЕРЕНОС РАБОТЫ В
// ЦИКЛ: мир въезжает в кап прыжками (день 32, граница сезона: 54 → 11711
// слотов за ОДИН дневной тик), и те же страницы материализовались бы внутри
// тика — 11657 × 40960 Б инвентаря = 455.3 МиБ, со всеми колонками ≈ 474
// МиБ ≈ 30 300 ошибок страниц против бюджета тика 15.6 мс.
//
// Цена закона названа вслух: RSS 2.5 → 1335.6 МиБ, **201 мс**, ОДИН раз за
// запуск (звонок один — `src/app/main.cpp:6397`; новая игра и загрузка
// переиспользуют этот же блок через `store_reset` ниже), в тик и в кадр не
// попадает ни разу. Второй обход — растить колонку `resize` на спавне —
// закрыт вердиктом владельца 2026-09-25 (см. шапку: инвариант в ТИПЕ). Не
// переизобретать ни тот, ни другой.
inline std::unique_ptr<MacroStore> make_macro_store() {
    std::unique_ptr<MacroStore> s;
    s.reset(new MacroStore());
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

// СМЕРТЬ МИРА: store — память мира, и умирает вместе с ним (destroy_world).
// До этой двери store жил дольше мира: reg чистился, а слоты прошлого мира
// оставались живыми призраками без моста, и каждая загрузка рожала restore
// ПОВЕРХ них — счёт носителей ординала игрока честным store-сканом дал 2
// (смоук SAVE-5, 2026-09-29; entt-счёт был зелёным по слепоте — призрак
// моста не носил). Поколение выживших бампается, как в store_death: всякий
// хэндл прошлого мира мертвеет, даже если слот переиспользует новый.
inline void store_reset(MacroStore& s) {
    for (std::size_t i = 0; i < kMacroEntityCap; ++i) {
        if (s.alive[i] != 0) ++s.generation[i];
        s.alive[i] = 0;
        s.freeSlots[i] = std::uint16_t(kMacroEntityCap - 1u - i);
    }
    s.freeCount  = std::uint32_t(kMacroEntityCap);
    s.aliveCount = 0;
}

// Смерть слота: поколение растёт — всякий старый хэндл мертвеет мгновенно;
// колонки НЕ трутся здесь (их обнулит следующее рождение из списка) —
// значит читать мёртвый слот нельзя ничем, кроме valid()-гарды.
//
// ПРЕДЕЛ НАЗВАН ВСЛУХ: поколение 16-битное и ЗАВОРАЧИВАЕТСЯ. После 65 536
// смертей ОДНОГО слота древний хэндл снова станет валидным и покажет на
// постороннего. Насыщения здесь нет сознательно: насыщенное поколение
// сделало бы слот навсегда неперепользуемым (утечка слота — хуже), а
// сравнение с «мёртвым навсегда» значением требует второго словаря судьбы
// рядом с alive. Числом: 65 536 смертей одного слота при капе 32 768 — это
// ~2.1 млрд смертей сквадов на полном обороте массива, то есть вне
// достижимого горизонта партии; когда горизонт изменится, лечением будет
// ширина поколения, а не насыщение.
inline void store_death(MacroStore& s, MacroHandle h) {
    if (!s.valid(h)) return;   // двойная смерть — no-op, не порча freelist
    s.alive[h.slot] = 0;
    ++s.generation[h.slot];
    s.freeSlots[s.freeCount++] = h.slot;
    --s.aliveCount;
}

// Хэндл ЖИВОГО слота — для читателя, который уже держит слот (обход store,
// записи закона порядка): пара {slot, gen} годна для долгоживущей ссылки и
// для дверей squad.h. Слот обязан быть жив — это дверь обхода, не поиска.
inline MacroHandle handle_at(const MacroStore& s, std::uint16_t slot) {
    return MacroHandle{slot, s.generation[slot]};
}

// СЕЛЕКТОР КОЛОНКИ ПО ТИПУ (жилец, не мост): тип выбирает массив, ошибиться
// колонкой невозможно (типы колонок уникальны, список один — X-macro);
// двери body_state(st, h) ниже ходят им.
// store_attach/store_of — ctx-мост store для СУБМИРА (вердикт 4а):
// живёт до M-171 (фрейм); макро-сторона ctx не читает — макро-двери
// принимают MacroStore& параметром.
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

// ── ДВЕРЬ ПЕРЕНОСА ФЛАЖКА ИГРОКА (вердикт владельца 2026-09-29) ──────────
// «Смена сквада — это просто смена флажка, что этот сквад (с этой анкетой)
// игрок». Истина — колонка playerFlag анкеты (род 2 фрейма); `flagBits` —
// КЭШ GameState (state.h). Ровно-один держит сама дверь: старый носитель
// известен из кэша, запись нового = срыв старого, O(1), скана нет. Все
// перемещения флажка — вселение (sub/possess.h), пробуждение и починка
// осиротевшего флага (macro/player_entity.cpp), резолв загрузки, фикстуры
// свидетелей — ходят ЗДЕСЬ; будущий стратегический скролл сквадов — ещё
// один звонящий. Невалидный `to` — no-op: флажок не сжигается о протухший
// хэндл, мир не остаётся без игрока.
inline void transfer_player_flag(MacroStore& st, std::uint32_t& flagBits,
                                 MacroHandle to) {
    if (!st.valid(to)) return;
    const MacroHandle old = macro_handle_from_bits(flagBits);
    if (st.valid(old)) st.playerFlag[old.slot].on = 0;
    st.playerFlag[to.slot].on = 1;
    flagBits = macro_handle_bits(to);
}

// ── ДВЕРЬ СОСТОЯНИЯ ТЕЛА МАКРО-СКВАДА (кластер 7: моста нет) ─────────────
// Колонка под valid()-гардой — единственная форма чтения макро-сквада.
// Тело сцены читается своим компонентом (reg.try_get) — закон кластера 4.
template <typename C>
inline C* body_state(MacroStore& s, MacroHandle h) {
    return s.valid(h) ? &store_col<C>(s)[h.slot] : nullptr;
}
template <typename C>
inline const C* body_state(const MacroStore& s, MacroHandle h) {
    return body_state<C>(const_cast<MacroStore&>(s), h);
}

// Судьба макро-сквада — байт колонки (труп стоит до слива, AI-2); тело
// сцены отвечает тегом ecs::Dead своим путём (кластер 4). Протухший хэндл
// отвечает «мёртв» — fail-closed чтение: жилец слота сменился, и
// спрашивать о нём больше нечего.
inline bool macro_dead(const MacroStore& s, MacroHandle h) {
    return !s.valid(h) || s.dead[h.slot] != 0;
}
// Смерть по хэндлу: протухший хэндл — no-op (жилец уже сменился, мертвить
// некого); та же fail-closed пара к macro_dead выше.
inline void macro_mark_dead(MacroStore& s, MacroHandle h) {
    if (s.valid(h)) s.dead[h.slot] = 1;
}

} // namespace sm
