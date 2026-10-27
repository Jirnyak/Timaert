// ДВЕРИ СУЩЕСТВ НАД ЕДИНЫМ КОНТЕЙНЕРОМ — операции, а не адрес.
//
// АДРЕСНОЕ ПРОСТРАНСТВО СТРОКИ МИРА УЕХАЛО В `tables/world_row.h` (M-292,
// 2026-10-20): обе его половины суть ТАБЛИЦЫ, то есть вопросы «какого рода
// строка N» и «как она называется» отвечаются общим слоем и состояния мира
// не знают. Здесь остались ровно те двери, которым нужен КОНТЕЙНЕР
// (`Inventory` — род 2 фрейма), и это и есть шов §11. Включившим ничего
// править не надо: адрес переэкспортируется включением ниже.
#pragma once
#include <cstdint>

#include "macro/anketa.h"       // SoldierRecord — монета переноса души
#include "tables/world_row.h"   // адрес строки мира: род, имя, ключ

namespace sm {

// ── ДВЕРИ СУЩЕСТВ НАД ЕДИНЫМ КОНТЕЙНЕРОМ (M-71, слияние 2026-09-24) ───────
//
// Отдельного носителя существ больше нет: они лежат СТРОКАМИ МИРА в тех
// же 1024 слотах, что и предметы (закон двух областей — items.h: существа
// плотной областью сверху вниз, новейший слот — наименьший индекс; дыра
// затыкается новейшим, зеркало swap-with-last прежнего «ростера»). Эти функции
// — ЕДИНСТВЕННЫЙ путь существа в контейнер и из него: пересчёт род ↔ строка
// мира живёт ровно здесь; сдвиг базы наружу не выходит.
//
// СООТВЕТСТВИЕ СТАРОМУ ПОРЯДКУ — точное, ради закона ходока «свежие
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

// ── ЗДЕСЬ ЖИЛА `creatures_push_slot`, И ОНА ПЕРЕЖИЛА ОБОИХ СВОИХ ЗВОНЯЩИХ ──
//
// Дверь брала ЦЕЛЫЙ слот существа внутрь, и её собственная шапка называла,
// зачем: «свод контейнера в гарнизон, пул поглощает отряд». Оба названных
// СНЕСЕНЫ — гарнизон как сословие умер с переворотом v122 (души лежат
// головами в ОДНОМ контейнере), слив в пул дезертиров умер 2026-10-06
// (M-228: существа ГИБНУТ, приёмника нет). Вызовов осталось НОЛЬ в `src/` и
// в `tests/`, подтверждено КОМПИЛЯТОРОМ (проба переименованием — сборка
// зелёная), и дверь стала памятником двум мёртвым системам (M-324).
//
// ПЕРЕНОС ОДНОЙ ДУШИ ИЛИ ГЕНЕРИКА ЖИВ И ОСТАЁТСЯ — это `creatures_push`
// выше, монета всякого переноса. Ничего не осиротело.

// СКОЛЬКО ГОЛОВ — души и генерики вместе, Σ count по области (старые
// Roster::souls() / total_soldiers / SoldierSquad::size()). С M-296 это
// колонка сводки: та же сумма, посчитанная у двери записи.
inline int creature_count(const Inventory& inv) noexcept {
    return int(inv.sum().heads);
}
inline int creature_count_of(const Inventory& inv, NPCType kind) noexcept {
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

// (`creatures_move` УМЕР ЗДЕСЬ в ломоть 2 M-72: пересадка области — лот
// двери `deal@src/macro/deal.h` строками родов существ; прод-звонящих у
// него не было — только тесты, и они переехали на дверь.)

// ВСЕ ГИБНУТ — ОДНА ДВЕРЬ СМЕРТИ ОБЛАСТИ, БЕЗ ЕДИНОЙ ВЕТКИ ПО РОДУ (вердикт
// владельца 2026-10-06: «просто убрать всё это просто универсально не важно
// кто умер и умер всё никаких»). Возвращает погибшие ГОЛОВЫ — ЛЮБОГО рода:
// человек, зверь, нежить уходят одинаково, потому что судьба у них одна.
// Отказа у этой двери быть не может по построению: приёмника нет, а значит
// нет и капа, который мог бы отказать, — в этом она противоположность
// `creatures_move` выше, и ровно поэтому павшие больше не «остаются стоять
// за капом пула» через тик.
//
// СПИСАНИЕ ПАСТВЫ И ВЕДОМОСТЬ ДУШ ЗДЕСЬ НЕ ЖИВУТ: эта дверь знает только
// контейнер, а «чей это был дом» и «кому доложить» знает звонящий
// (`kill_fallen_squad_creatures@src/macro/squad.h`). Иначе дверь области
// потребовала бы мира в сигнатуре.
inline int creatures_kill_all(Inventory& inv) {
    int killed = 0;
    for (int i = kMaxInventorySlots - 1; i >= inv.creature_first();) {
        const ItemRef s = inv.slots[std::size_t(i)];
        killed += s.count;
        inv.remove_at(i, s.count);   // ремонт: сюда встал непройденный
    }
    return killed;
}

// (`creatures_add` УМЕР ТАМ ЖЕ: «копия области» МИНТИТ головы и дверью
// мира не является — сохранение есть первый закон сделки; прод-звонящих
// ноль, тесты переехали на `deal`.)

// ── ГОЛОВЫ ПОШТУЧНО — адресный обход для воплощения тел ───────────────────
// Развёртка стаков: (slot, index) — адрес головы, по которому субмир вешает
// заём тела. Порядок — СТАРЫЙ порядок контейнера (старейший слот первым).
// ДУШИ (entityId != 0, count == 1) — фильтр по head.entityId у читателя:
// это и есть souls() слитого мира, отдельного контейнера у душ нет.
struct CreatureRef {
    std::uint16_t kind     = 0;   // сырой NPCType — монета SoldierRecord
    std::uint8_t  level    = 1;
    std::uint32_t entityId = 0;
    std::int32_t  slot     = 0;   // слот контейнера
    std::int32_t  index    = 0;   // [0, count) внутри стака
};

class CreatureIterator {
  public:
    CreatureIterator(const Inventory* inv, int slot)
        : inv_(inv), slot_(slot) {}
    CreatureRef operator*() const {
        const ItemRef& s = inv_->slots[std::size_t(slot_)];
        return CreatureRef{std::uint16_t(creature_of_world_row(s.def)),
                            s.level, s.entityId, slot_, index_};
    }
    CreatureIterator& operator++() {
        if (++index_ >= inv_->slots[std::size_t(slot_)].count) {
            index_ = 0;
            --slot_;
        }
        return *this;
    }
    bool operator!=(const CreatureIterator& o) const {
        return slot_ != o.slot_ || index_ != o.index_;
    }
  private:
    const Inventory* inv_;
    std::int32_t     slot_  = 0;
    std::int32_t     index_ = 0;
};

struct CreaturesRange {
    const Inventory* inv = nullptr;
    int              first = kMaxInventorySlots;
    CreatureIterator begin() const {
        return CreatureIterator(inv, kMaxInventorySlots - 1);
    }
    CreatureIterator end() const {
        return CreatureIterator(inv, first - 1);
    }
};
inline CreaturesRange creatures_range(const Inventory& inv) {
    return CreaturesRange{&inv, inv.creature_first()};
}

// ── ВЗГЛЯДЫ ПО КОЛОНКАМ СТРОКИ (переехали из npc.h слиянием M-71) ─────────
// Всё по ЗАКОНУ СТРОКИ КАТАЛОГА: не «это существо?», а колонка рода —
// природа Human, колонка жалованья, спина haulMult.

// ЛЮДИ области — руки и рты ведомости труда, подсудимые суда крю. Зверь —
// спина и рот, но не рука. Спрашивает АРХЕТИП строки (`npc_archetype`), не
// границу ординала —
// и спрашивает её `slot_tally` у двери записи, а не этот проход (M-296).
inline int count_human_souls(const Inventory& inv) {
    return int(inv.sum().folkHeads);
}

// УБЫЛЬ ДУШ — ДВЕРЬ drain (ЗАКОН ТРЁХ ДВЕРЕЙ): снять до n человеческих душ
// с фолк-стаков и ВЕРНУТЬ ФАКТ — голод, гибель и подъём в сквад берут
// сколько есть, скот остаётся имуществом (лошадь — не душа). Новейший
// людской слот первым (наименьший индекс области).
inline int bleed_flock(Inventory& inv, int n) {
    int taken = 0;
    // Это ПОИСК, а не сумма, поэтому сводка его не заменяет — но «людей нет
    // вовсе» она отвечает за ноль проходов, и это самый частый случай мира
    // (у всякого контейнера без паствы).
    if (inv.sum().folkHeads == 0) return 0;
    while (taken < n) {
        int si = -1;
        for (int k = inv.creature_first(); k < kMaxInventorySlots; ++k) {
            if (is_folk_kind(std::uint16_t(
                    creature_of_world_row(inv.slots[std::size_t(k)].def)))) {
                si = k;
                break;
            }
        }
        if (si < 0) break;
        const int take = std::min(n - taken,
                                  int(inv.slots[std::size_t(si)].count));
        if (take <= 0 || !inv.remove_at(si, take)) break;
        taken += take;
    }
    return taken;
}

// Та же дверь для ТОЛПЫ ДАНЖА (переворот населения, вердикт 3: души данжей
// — головами, и голова его толпы — Imp, не человек): снять до n голов
// ЛЮБОГО рода из области существ, вернуть факт.
//
// ЗДЕСЬ СТОЯЛО «у поселения этой дверью не ходят — там душа человеческая» —
// И ЭТО БЫЛО НЕВЕРНО (снято 2026-10-06). Ею ходили и у поселения: голод
// места снимал ЛЮБУЮ голову, включая скот, по вердикту «никаких
// особенностей лошадей» (все, кто в контейнере, едят по таблице). Запрет в
// прозе противоречил живому звонящему, и держался ровно на том, что табун у
// мест нулевой. Что ПРАВДА отличает поселение — не дверь голов, а ПАСТВА:
// её число людское, поэтому пара считается `count_human_souls`
// (`flock_left_home@src/macro/squad.h`), а не снятыми головами.
inline int bleed_creatures(Inventory& inv, int n) {
    int taken = 0;
    while (taken < n) {
        const int si = inv.creature_first();
        if (si >= kMaxInventorySlots) break;
        const int take = std::min(n - taken,
                                  int(inv.slots[std::size_t(si)].count));
        if (take <= 0 || !inv.remove_at(si, take)) break;
        taken += take;
    }
    return taken;
}

// Upkeep is MAINTENANCE, not a deal: один закон, одно число, чей бы
// контейнер ни был. С M-296 это колонка сводки `wagePerDay` — ТА ЖЕ, что
// читает сезонный счёт (`upkeep_bill@src/macro/upkeep_window.h`): два
// звонящих одного числа, а не два числа.
inline int calculate_squad_upkeep(const Inventory& inv) {
    return int(inv.sum().wagePerDay);
}

// Паства встаёт в область ОДНОЙ строкой (вердикт 2026-09-22: стражи нет,
// состав — факт). Возвращает СКОЛЬКО ВСТАЛО: отказ контейнера — вслух.
inline int raise_flock_into_container(Inventory& inv, int souls) {
    if (souls <= 0) return 0;
    const NpcTypeDef& row = npc_def(NPCType::Peasant);
    return creatures_push_stack(inv, NPCType::Peasant, row.baseLevel, souls)
               ? souls
               : 0;
}

// (`hire_npc` УМЕР ЗДЕСЬ в ломоть 2 M-72: найм — ОДНА сделка дверью
// `deal@src/macro/deal.h` — рекрут и оплата exact-лотами, атомарно. Его
// скретч-кошелёк int& «цену читал и НЕ списывал» — платила вторая,
// несвязанная дверь у звонящего с ручным откатом; теперь откат невыразим.
// Цена найма — закон звонящего: `hire_price_for` остаётся дверью цены.)

} // namespace sm
