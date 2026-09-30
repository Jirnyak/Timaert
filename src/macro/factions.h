// РОД 6 ФРЕЙМА — СТРОКА ФРАКЦИИ (вердикт владельца, ШИНА род 6: «будет
// сткруткра фракций (аналогия со сквадом) у неё им (плоский ЧАРЫ! никаких
// стрингов и говна) цвет RGB отношния со всеми щдругими и тд расширим по
// мере развития пока надо заложить»). Гладкий массив строк: ОДНА строка на
// фракцию, и на ней всё — id плоскими чарами, цвет, отношения КОЛОНКОЙ
// СВОЕЙ СТРОКИ, отрезок феодальных рёбер в общем пуле.
//
// ЧТО ЭТО УБИЛО (2026-09-30, ход 2 ломтик C): RelationMatrix c хвостовыми
// рантайм-именами (вердикт «уничтожить» — с ними ушёл единственный
// житель-строка внутри симуляции) и одно из ТРЁХ имён числа 64
// (kMaxWorldFactions); осталось kMaxFactions@src/tables/faction.h и алиас
// kMaxCrowdFactions в субмире.
//
// ФЕОДАЛЬНЫЙ ГРАФ И ДАНЬ — ЗДЕСЬ, А НЕ У СКВАДОВ (вердикт владельца
// 2026-09-30: «дань и феодальный граф — система фракций (род 6): плоский
// массив на строке фракции, „кто сколько кому должен“; дань уходит из
// сквадов»). Ребро несёт ЛЕТОПИСЬ ДОЛГА; а «кого Я считаю феодалом» — роль,
// колонка строки СКВАДА (реестр интересов, род 2): два разных вопроса.
//
// ФОРМА ПУЛА — КАНОНИЧЕСКИЙ COUNTING-SORT (ЗАКОН КЛЕТОЧНОГО КАРКАСА п.2,
// одобрено владельцем: «и это в системе фракций и на каждую фракцию по
// такому? хорошо давай попробуем»): ОДИН пул рёбер мира, сгруппированный по
// (фракция, ординал вассала); строка фракции владеет ОТРЕЗКОМ [fiefStart,
// fiefStart+fiefCount). Капа «рёбер на фракцию» НЕ СУЩЕСТВУЕТ как числа —
// фракции делят общий выведенный потолок, перекос компенсируется.
#pragma once
#include "macro/memory.h"        // WorldMemory — память склада вассала
#include "tables/faction.h"      // kMaxFactions, kFactionDefs, шкала отношений

#include <cstdint>
#include <cstdio>    // snprintf: id реестра → плоские чары строки
#include <cstring>

namespace sm {

// Слот фракции = индекс строки. Реестровые строки занимают первые
// kFactionCount слотов; хвост зарезервирован под будущие фракции мира
// (владелец: «просто зарезервировать слоты под новые фракции») — но БЕЗ
// рантайм-имён: имя будущей строке выдаст её собственный закон, когда
// фракции начнут рождаться в игре.
using FactionSlot = int;
inline constexpr FactionSlot kNoFactionSlot = -1;

// ── ФЕОДАЛЬНОЕ РЕБРО — «КТО СКОЛЬКО КОМУ ДОЛЖЕН» ────────────────────────
// Одно ребро на вассала: сюзерен у сквада РОВНО ОДИН (вердикт владельца
// 2026-09-30 «сюзерен один»; та же аксиома, что держит дверь set_suzerain),
// значит рёбер в мире ≤ субъектов — кап пула ВЫВЕДЕН, а не назначен.
// Вассалов у сюзерена — сколько угодно: веер ограничен только числом
// субъектов («сотни городов в стране» работают по построению).
struct TitheEdge {
    std::int32_t vassal = 0;    // субъектный ординал; 0 = пустой слот
                                //   (закон нуля-ординала)
    std::int32_t suzerain = 0;  // субъектный ординал получателя дани
    // Долг СТОИМОСТЬЮ — закон v108 живёт: «всё в инвентаре — товар», долг
    // одним числом, платится по нужде дома → по плотности.
    std::int64_t owedValue = 0;
    // База начисления — среднее стоимости склада вассала за сезон
    // (macro/memory.h, значение × горизонт; закон v74/v104 живёт).
    WorldMemory avgValue = 0;
};
static_assert(sizeof(TitheEdge) == 24,
              "ребро = 2 ординала (8) + долг (8) + память (8)");

// Кап пула = кап макро-субъектов (kMacroEntityCap@src/core/stacks.h;
// равенство прибито static_assert там, где видны оба — включать сюда
// stacks.h нельзя, он сам стоит над state.h). Замер 2026-09-30, 4 сида:
// живых рёбер ~2000 (5 % пула), худшая фракция 644, худший веер 55.
inline constexpr int kMaxTitheEdges = 32768;

// ── СТРОКА ФРАКЦИИ ───────────────────────────────────────────────────────
struct FactionRow {
    // Плоские чары; 24 длиннее любого id реестра (прецедент прежнего
    // kMaxIdLen). Пустая строка = слот не занят.
    char id[24] = {};
    // Цвет реестра (ARGB) — колонка строки, чтобы будущая рантайм-фракция
    // несла свой цвет тем же местом, что реестровая.
    std::uint32_t color = 0;
    // ОТНОШЕНИЯ — КОЛОНКОЙ СВОЕЙ СТРОКИ (вердикт: «каждая фаркция хранит
    // отношения с другими»). Одна шкала мира -127..127 (граница ТИПА);
    // симметрия — ЗАКОН: пишет только set_relation, обе строки разом.
    std::int8_t rel[kMaxFactions] = {};
    std::uint8_t used = 0;
    // Отрезок феодальных рёбер этой фракции в общем пуле (см. шапку).
    // Ребро лежит в строке фракции СЮЗЕРЕНА.
    std::int32_t fiefStart = 0;
    std::int32_t fiefCount = 0;
};
static_assert(sizeof(FactionRow) == 104,
              "24 id + 4 цвет + 64 отношений + 1 клеймо + 3 паддинга + 8 отрезок");

struct FactionState {
    FactionRow rows[kMaxFactions] = {};
    // Пул рёбер, сгруппирован по (фракция, ординал вассала); внутри отрезка
    // строки — сортировка по вассалу, бинарный поиск.
    TitheEdge fief[kMaxTitheEdges] = {};
    std::int32_t fiefTotal = 0;
    // Сезон, за который начисление уже прошло (-1 = никогда): один штамп на
    // мир, потому что граница сезона одна и проход по пулу один.
    std::int32_t fiefSeasonAssessed = -1;
};
static_assert(sizeof(FactionState) == 104 * 64 + 24 * 32768 + 8,
              "строки (6656) + пул (786432) + счёт и штамп (8) = 793096 Б");

// ── ДВЕРИ СТРОК ──────────────────────────────────────────────────────────

// Заполнить реестровые строки. Зовётся при создании мира и после загрузки:
// скомпилированный реестр — истина id/цвета своих строк, сейв не может
// «разобъявить» фракцию, с которой игра собрана.
inline void claim_registry_rows(FactionState& f) {
    for (int i = 0; i < kFactionCount; ++i) {
        FactionRow& r = f.rows[i];
        std::snprintf(r.id, sizeof r.id, "%s", kFactionDefs[i].id);
        r.color = kFactionDefs[i].color;
        r.used = 1;
        r.rel[i] = std::int8_t(kRelationMax);   // сам себе — верх шкалы
    }
}

// Слот по id: только реестр (рантайм-имена уничтожены вердиктом).
inline FactionSlot faction_slot(const FactionState&, const char* id) {
    if (!id || id[0] == '\0') return kNoFactionSlot;
    const int reg = faction_index(id);
    return reg >= 0 ? reg : kNoFactionSlot;
}

// Отношение по слотам — горячая форма: два чтения массива, ноль строк.
inline int relation_of(const FactionState& f, FactionSlot a, FactionSlot b) {
    if (a < 0 || b < 0 || a >= kMaxFactions || b >= kMaxFactions) {
        return 0;     // fail-closed: неразмещённая фракция нейтральна
    }
    if (a == b) return kRelationMax;   // верх шкалы, а не круглое число
    return int(f.rows[a].rel[b]);
}

// Симметричная запись — ЕДИНСТВЕННЫЙ способ сдвинуть отношение: одна дверь
// пишет ОБЕ строки, невзаимного отношения народов не бывает (вердикт «у
// фракций все равны и чистая симметрия»).
inline void set_relation(FactionState& f, FactionSlot a, FactionSlot b,
                         int value) {
    if (a < 0 || b < 0 || a >= kMaxFactions || b >= kMaxFactions || a == b) {
        return;
    }
    // Одна шкала мира: кламп по границе ТИПА (страж упаковки в int8).
    const int v = value < kRelationMin ? kRelationMin
                                       : (value > kRelationMax ? kRelationMax
                                                               : value);
    f.rows[a].rel[b] = std::int8_t(v);
    f.rows[b].rel[a] = std::int8_t(v);
}

// ── ДВЕРИ ФЕОДАЛЬНЫХ РЁБЕР ───────────────────────────────────────────────
// Инвариант пула, который держат все три двери: отрезки лежат В ПОРЯДКЕ
// ИНДЕКСА СТРОКИ (fiefStart[i] = Σ fiefCount[j<i], дыр нет), внутри отрезка
// рёбра отсортированы по ординалу вассала. Пустой мир: все начала 0.

// Ребро вассала в строке фракции (nullptr — ребра нет). Бинарный поиск по
// отрезку — тот же закон, что landmark_by_id над монотонным эмитентом.
inline TitheEdge* tithe_edge(FactionState& f, FactionSlot fac, int vassalId) {
    if (fac < 0 || fac >= kMaxFactions || vassalId <= 0) return nullptr;
    const FactionRow& r = f.rows[fac];
    int lo = r.fiefStart, hi = r.fiefStart + r.fiefCount;
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (f.fief[mid].vassal < vassalId) lo = mid + 1;
        else hi = mid;
    }
    if (lo < r.fiefStart + r.fiefCount && f.fief[lo].vassal == vassalId)
        return &f.fief[lo];
    return nullptr;
}
inline const TitheEdge* tithe_edge(const FactionState& f, FactionSlot fac,
                                   int vassalId) {
    return tithe_edge(const_cast<FactionState&>(f), fac, vassalId);
}

// Завести ребро (идемпотентно: существующее возвращается как есть).
// Вставка сдвигает хвост пула и начала последующих отрезков — феод меняется
// редко (генезис, завоевание), цена честно оплачена здесь, а не в тике.
inline TitheEdge* tithe_edge_add(FactionState& f, FactionSlot fac,
                                 int vassalId, int suzerainId) {
    if (fac < 0 || fac >= kMaxFactions || vassalId <= 0 || suzerainId <= 0)
        return nullptr;
    if (TitheEdge* e = tithe_edge(f, fac, vassalId)) return e;
    if (f.fiefTotal >= kMaxTitheEdges) return nullptr;   // громкий отказ выше
    FactionRow& r = f.rows[fac];
    // Точка вставки — конец «меньших» вассалов отрезка.
    int pos = r.fiefStart;
    const int end = r.fiefStart + r.fiefCount;
    while (pos < end && f.fief[pos].vassal < vassalId) ++pos;
    std::memmove(&f.fief[pos + 1], &f.fief[pos],
                 std::size_t(f.fiefTotal - pos) * sizeof(TitheEdge));
    f.fief[pos] = TitheEdge{vassalId, suzerainId, 0, 0};
    ++f.fiefTotal;
    ++r.fiefCount;
    for (int i = fac + 1; i < kMaxFactions; ++i) ++f.rows[i].fiefStart;
    return &f.fief[pos];
}

// Снять ребро (смена/потеря сюзерена). Непогашенный долг умирает вместе с
// ребром — прощение при смене феода, второго носителя долга не заводится.
inline void tithe_edge_remove(FactionState& f, FactionSlot fac, int vassalId) {
    TitheEdge* e = tithe_edge(f, fac, vassalId);
    if (!e) return;
    const int pos = int(e - f.fief);
    std::memmove(&f.fief[pos], &f.fief[pos + 1],
                 std::size_t(f.fiefTotal - pos - 1) * sizeof(TitheEdge));
    --f.fiefTotal;
    f.fief[f.fiefTotal] = TitheEdge{};
    --f.rows[fac].fiefCount;
    for (int i = fac + 1; i < kMaxFactions; ++i) --f.rows[i].fiefStart;
}

} // namespace sm
