// СТРОКА МИРА — ОДНО ПРОСТРАНСТВО ОРДИНАЛОВ (CANON:5566, вердикт владельца
// 2026-09-22; наряд M-73, шаг 1 эпика единой таблицы объектов).
//
// Всякая строка мира — предмет или существо — несёт ОДИН сквозной номер:
//
//   предметы   [0, world_item_rows())
//   существа   [world_item_rows(), world_row_count())
//
// Имена в коде НЕ переезжают: `NPCType::Peasant` остаётся собой, предметный
// ординал остаётся индексом каталога. Сдвиг базы живёт ЗДЕСЬ, в двери, и
// только здесь — контейнер (шаг 2) хранит строку мира, а спрашивает её
// всегда через эти функции.
//
// ЛОВУШКА, НАЗВАННАЯ КАНОНОМ ЗАРАНЕЕ: граница диапазона ВЫВОДИТСЯ из размера
// каталога (`item_catalog().size()`), вписанная константа была бы возвратом
// второго словаря. Поэтому здесь нет ни одного литерала.
//
// Авторских таблиц внутри по-прежнему две (kCatalog предметов, kNpcTypeDefs
// существ) — это удобство авторинга, а не второй словарь: на вопрос «что за
// строка мира N» отвечает ровно эта дверь, одна на проект (вердикт владельца
// 2026-09-24: «технически это единая таблица объектов»).
#pragma once
#include <cstdint>

#include "macro/army.h"    // SoldierRecord — монета переноса души
#include "macro/items.h"   // item_catalog() — предметная половина строк
#include "macro/npc.h"     // NPCType — половина существ

namespace sm {

// Граница диапазонов — размер предметного каталога, посчитанный, не вписанный.
inline std::uint16_t world_item_rows() noexcept {
    return std::uint16_t(item_catalog().size());
}
inline std::uint16_t world_row_count() noexcept {
    return std::uint16_t(world_item_rows()
                         + std::uint16_t(NPCType::Count));
}

// world_row_is_item живёт У КАТАЛОГА (items.h/items.cpp): граница — размер
// его собственной таблицы, и контейнеру она нужна для закона размещения.
// Здесь — только вторая половина вопроса.
inline bool world_row_is_creature(std::uint16_t row) noexcept {
    return row >= world_item_rows() && row < world_row_count();
}

// Туда: род → строка мира.
inline std::uint16_t world_row_of_item(int itemOrdinal) noexcept {
    return std::uint16_t(itemOrdinal);
}
inline std::uint16_t world_row_of_creature(NPCType t) noexcept {
    return std::uint16_t(world_item_rows() + std::uint16_t(t));
}

// Обратно: строка мира → род. Fail-closed на чужом диапазоне: предмету — −1,
// существу — NPCType::Count; молча «почти правильного» ответа не бывает.
inline int item_of_world_row(std::uint16_t row) noexcept {
    return world_row_is_item(row) ? int(row) : -1;
}
inline NPCType creature_of_world_row(std::uint16_t row) noexcept {
    return world_row_is_creature(row)
        ? NPCType(row - world_item_rows())
        : NPCType::Count;
}

// ── ДВЕРИ СУЩЕСТВ НАД ЕДИНЫМ КОНТЕЙНЕРОМ (M-71, слияние 2026-09-24) ───────
//
// Ростер перестал быть отдельной вещью: существа лежат СТРОКАМИ МИРА в тех
// же 1024 слотах, что и предметы (закон двух областей — items.h: существа
// плотной областью сверху вниз, новейший слот — наименьший индекс; дыра
// затыкается новейшим, зеркало swap-with-last старого ростера). Эти функции
// — ЕДИНСТВЕННЫЙ путь существа в контейнер и из него: пересчёт род ↔ строка
// мира живёт ровно здесь; сдвиг базы наружу не выходит.
//
// СООТВЕТСТВИЕ СТАРОМУ ПОРЯДКУ РОСТЕРА — точное, ради закона ходока «свежие
// уходят первыми» и паритета мира: старый обход slots[0..n-1] (старейший →
// новейший) = здесь обход 1023 → creature_first().

// Слот существа из рода. level — единая колонка уровня (кап 255, army.h);
// именное (entityId != 0) не стакуется и несёт count == 1 по построению
// (К-3) — дверь это принуждает, как принуждал старый push_slot.
inline ItemRef creature_slot(NPCType kind, int level, std::int32_t n,
                             std::uint32_t entityId = 0) noexcept {
    ItemRef r{};
    r.def = world_row_of_creature(kind);
    r.level = std::uint8_t(normalize_soldier_level(level));
    r.count = entityId != 0 ? 1 : n;
    r.entityId = entityId;
    return r;
}

// Генерик-стак внутрь: сольётся со своим двойником (same_kind_as) или
// встанет новым слотом под областью. Отказ громкий: нет места у областей
// или int32-переполнение стака — единственный оставшийся кап.
inline bool creatures_push_stack(Inventory& inv, NPCType kind, int level,
                                 std::int32_t n) {
    // Отказ в точке РОЖДЕНИЯ: род, которого нет в каталоге, не входит —
    // прежний путь отмывал его в Peasant (soldier_npc_type), и порчу ловил
    // только писатель сейва.
    if (n <= 0 || !valid_npc_kind(std::uint16_t(kind))) return false;
    return inv.add_ref(creature_slot(kind, level, n));
}

// Одна душа или один генерик — монета всякого переноса (найм, ходок,
// строка сейва). entityId == 0 идёт законом стака.
inline bool creatures_push(Inventory& inv, const SoldierRecord& s) {
    if (!valid_npc_kind(s.kind)) return false;   // отказ, не отмывание
    return inv.add_ref(
        creature_slot(soldier_npc_type(s.kind), s.level, 1, s.entityId));
}

// Целый слот существа внутрь (свод ростера в гарнизон, пул поглощает
// отряд): генерик сольётся, именное возьмёт свой слот. Всё-или-ничего.
inline bool creatures_push_slot(Inventory& inv, const ItemRef& s) {
    if (s.count <= 0 || !world_row_is_creature(s.def)) return false;
    if (s.entityId != 0 && s.count != 1) return false;
    return inv.add_ref(s);
}

// СКОЛЬКО ГОЛОВ — души и генерики вместе, Σ count по области (старые
// Roster::souls() / total_soldiers / SoldierSquad::size()).
inline int creature_heads(const Inventory& inv) noexcept {
    int n = 0;
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        n += inv.slots[std::size_t(i)].count;
    }
    return n;
}
inline int creature_heads_of(const Inventory& inv, NPCType kind) noexcept {
    const std::uint16_t row = world_row_of_creature(kind);
    int n = 0;
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (s.def == row) n += s.count;
    }
    return n;
}
inline bool creatures_empty(const Inventory& inv) noexcept {
    return inv.creature_first() == kMaxInventorySlots;
}
inline int creature_slot_count(const Inventory& inv) noexcept {
    return kMaxInventorySlots - inv.creature_first();
}

// Одна душа ИЗ СЛОТА — монета переноса: именная уходит со своим entityId,
// генерик — как {род, уровень, 0}. Слот умирает с последней душой, дыру
// затыкает remove_at (ремонт области).
inline bool creatures_take_at(Inventory& inv, int slot, SoldierRecord& out) {
    if (slot < inv.creature_first() || slot >= kMaxInventorySlots) {
        return false;
    }
    const ItemRef& s = inv.slots[std::size_t(slot)];
    out = make_soldier(std::uint16_t(creature_of_world_row(s.def)), s.level,
                       s.entityId);
    return inv.remove_at(slot, 1);
}

// ЗАКОН ХОДОКА (дезертирство, пакеты патруля): свежие пришли — свежие
// уходят первыми; новейший слот области — наименьший индекс.
inline bool creatures_pop_back(Inventory& inv, SoldierRecord& out) {
    return creatures_take_at(inv, inv.creature_first(), out);
}

// УДАР ВЕДОМОСТИ (одна дверь для ведомости и боя): именная смерть снимает
// РОВНО ту душу; генерик — того же рода и уровня, СТАРЕЙШИЙ первым (старый
// обход slots[0..n) = здесь 1023 → first).
inline bool creatures_remove_one_by_entity_id(Inventory& inv,
                                              std::uint32_t entityId) {
    if (entityId == 0) return false;   // безымянный удар никого не называет
    const int first = inv.creature_first();
    for (int i = kMaxInventorySlots - 1; i >= first; --i) {
        if (inv.slots[std::size_t(i)].entityId == entityId) {
            return inv.remove_at(i, 1);
        }
    }
    return false;
}
inline bool creatures_remove_one(Inventory& inv, const SoldierRecord& r) {
    if (r.entityId != 0) {
        return creatures_remove_one_by_entity_id(inv, r.entityId);
    }
    const std::uint16_t row = world_row_of_creature(soldier_npc_type(r.kind));
    const std::uint8_t lvl = std::uint8_t(normalize_soldier_level(r.level));
    const int first = inv.creature_first();
    for (int i = kMaxInventorySlots - 1; i >= first; --i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (s.entityId == 0 && s.def == row && s.level == lvl) {
            return inv.remove_at(i, 1);
        }
    }
    return false;
}

// Пересадить область целиком, послотно, сколько возьмёт цель: отказанный
// слот ОСТАЁТСЯ у истока (никто не уничтожается за то, что стоял за капом).
// Возвращает пересаженные ГОЛОВЫ. Зеркало старого move_squad: снятие слота
// приводит на его место непройденный новейший — индекс не шагает.
inline int creatures_move(Inventory& dst, Inventory& src) {
    int moved = 0;
    for (int i = kMaxInventorySlots - 1; i >= src.creature_first();) {
        const ItemRef s = src.slots[std::size_t(i)];
        if (!dst.add_ref(s)) {
            --i;                        // остаётся; следующий слот
            continue;
        }
        moved += s.count;
        src.remove_at(i, s.count);      // ремонт: сюда встал новейший
    }
    return moved;
}

// Скопировать область (исток не трогается — форма отката). Возвращает
// взятые головы; отказ капа рвёт цикл, как старый add_squad.
inline int creatures_add(Inventory& dst, const Inventory& src) {
    const int first = src.creature_first();   // читается ДО: src может БЫТЬ dst
    int taken = 0;
    for (int i = kMaxInventorySlots - 1; i >= first; --i) {
        const ItemRef s = src.slots[std::size_t(i)];   // копия
        if (!dst.add_ref(s)) break;
        taken += s.count;
    }
    return taken;
}

// ── ГОЛОВЫ ПОШТУЧНО — адресный обход для воплощения тел ───────────────────
// Развёртка стаков: (slot, index) — адрес головы, по которому субмир вешает
// заём тела. Порядок — СТАРЫЙ порядок ростера (старейший слот первым).
// ДУШИ (entityId != 0, count == 1) — фильтр по head.entityId у читателя:
// это и есть souls() слитого мира, отдельного контейнера у душ нет.
struct CreatureHead {
    std::uint16_t kind     = 0;   // сырой NPCType — монета SoldierRecord
    std::uint8_t  level    = 1;
    std::uint32_t entityId = 0;
    std::int32_t  slot     = 0;   // слот контейнера
    std::int32_t  index    = 0;   // [0, count) внутри стака
};

class CreatureHeadIterator {
  public:
    CreatureHeadIterator(const Inventory* inv, int slot)
        : inv_(inv), slot_(slot) {}
    CreatureHead operator*() const {
        const ItemRef& s = inv_->slots[std::size_t(slot_)];
        return CreatureHead{std::uint16_t(creature_of_world_row(s.def)),
                            s.level, s.entityId, slot_, index_};
    }
    CreatureHeadIterator& operator++() {
        if (++index_ >= inv_->slots[std::size_t(slot_)].count) {
            index_ = 0;
            --slot_;
        }
        return *this;
    }
    bool operator!=(const CreatureHeadIterator& o) const {
        return slot_ != o.slot_ || index_ != o.index_;
    }
  private:
    const Inventory* inv_;
    std::int32_t     slot_  = 0;
    std::int32_t     index_ = 0;
};

struct CreatureHeadsRange {
    const Inventory* inv = nullptr;
    int              first = kMaxInventorySlots;
    CreatureHeadIterator begin() const {
        return CreatureHeadIterator(inv, kMaxInventorySlots - 1);
    }
    CreatureHeadIterator end() const {
        return CreatureHeadIterator(inv, first - 1);
    }
};
inline CreatureHeadsRange creature_heads_range(const Inventory& inv) {
    return CreatureHeadsRange{&inv, inv.creature_first()};
}

// ── ВЗГЛЯДЫ ПО КОЛОНКАМ СТРОКИ (переехали из npc.h слиянием M-71) ─────────
// Всё по ЗАКОНУ СТРОКИ КАТАЛОГА: не «это существо?», а колонка рода —
// ездовая спина, природа Human, колонка жалованья.

// Сколько ездовых стоит в области — вторая половина закона упряжки.
inline int count_mount_souls(const Inventory& inv) {
    int n = 0;
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (is_mount_kind(std::uint16_t(creature_of_world_row(s.def)))) {
            n += s.count;
        }
    }
    return n;
}

// ЗАКОН УПРЯЖКИ (владелец, 2026-09-19: «по лошадке на душу»): отряд ведёт
// столько ездовых, сколько в нём НЕ-ездовых душ — по одной на душу, и ни
// одной лишней. Лидер — своя душа, он тоже ведёт коня, поэтому +1.
//
// Это МЕРА ВЫДАЧИ, а не право собственности: табун принадлежит МЕСТУ
// (ДВУХТАКТНЫЙ ОБОЗ, вердикт владельца 2026-09-19) — на приходе отряд
// сдаёт в стойло ВСЁ ездовое, на выходе место выдаёт ему столько, сколько
// говорит эта мера и сколько стоит в стойле.
inline int mount_allowance(const Inventory& inv) {
    int riders = 1;   // лидер
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (!is_mount_kind(std::uint16_t(creature_of_world_row(s.def)))) {
            riders += s.count;
        }
    }
    return riders;
}

// ЛЮДИ области — руки и рты ведомости труда, подсудимые суда крю. Зверь —
// спина и рот, но не рука. Спрашивает ПРИРОДУ (kNpcNature), не границу.
inline int count_human_souls(const Inventory& inv) {
    int n = 0;
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (is_folk_kind(std::uint16_t(creature_of_world_row(s.def)))) {
            n += s.count;
        }
    }
    return n;
}

// Upkeep is MAINTENANCE, not a deal: один закон, одно число, чей бы
// контейнер ни был.
inline int calculate_squad_upkeep(const Inventory& inv) {
    int base = 0;
    for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        base += soldier_upkeep(std::uint16_t(creature_of_world_row(s.def)),
                               s.level)
                * s.count;
    }
    return base;
}

// Паства встаёт в область ОДНОЙ строкой (вердикт 2026-09-22: стражи нет,
// состав — факт). Возвращает СКОЛЬКО ВСТАЛО: отказ контейнера — вслух.
inline int raise_flock_into_roster(Inventory& inv, int souls) {
    if (souls <= 0) return 0;
    const NpcTypeDef& row = npc_def(NPCType::Peasant);
    return creatures_push_stack(inv, NPCType::Peasant, row.baseLevel, souls)
               ? souls
               : 0;
}

// Найм: рекрут ПЕРЕЕЗЖАЕТ между двумя контейнерами, и отказать может любой
// конец. Ищется старейший слот рода (старый обход slots[0..n) = 1023 →
// first). Возвращает цену или 0.
inline int hire_npc(Inventory& playerInv, Inventory& garrison,
                    NPCType kind, int& playerGold) {
    if (!npc_hireable(kind)) return 0;
    const std::uint16_t row = world_row_of_creature(kind);
    const int first = garrison.creature_first();
    for (int i = kMaxInventorySlots - 1; i >= first; --i) {
        if (garrison.slots[std::size_t(i)].def != row) continue;
        const int cost = hire_price_for(std::uint16_t(kind),
                                        garrison.slots[std::size_t(i)].level);
        if (playerGold < cost) return 0;
        SoldierRecord recruit{};
        if (!creatures_take_at(garrison, i, recruit)) return 0;
        if (!creatures_push(playerInv, recruit)) {
            creatures_push(garrison, recruit);   // no room: the man stays home
            return 0;
        }
        playerGold -= cost;
        return cost;
    }
    return 0;
}

} // namespace sm
