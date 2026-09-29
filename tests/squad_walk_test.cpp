// Свидетель ЗАКОНА ПОРЯДКА ОБХОДА (macro/squad_walk.h, эпик 2 шаг 1а/1в):
// обход сквадов идёт по ординалу рождения, а не по порядку слотов store
// (кластер 7: моста нет, население — сами слоты).
//
// Негативный контроль обязан РЕАЛЬНО стрелять (тестовый закон §8 п.6):
// смерть в середине + дорождение переиспользуют слоты (freelist LIFO), и
// сырой слот-порядок обязан выдать инверсию ординалов — иначе контроль
// ничего не детектирует и сортировка недоказуема.
#include "check.h"

#include "ecs/components.h"
#include "macro/squad_walk.h"
#include "macro/store.h"

#include <cstdint>
#include <vector>

int main() {
    using namespace sm;
    auto store = make_macro_store();
    MacroStore& st = *store;

    // Рождения 0..7 в порядке ординала — как единственная дверь make_npc.
    auto born_one = [&](std::uint32_t ord) {
        const MacroHandle h = store_birth(st);
        st.spawnId[h.slot] = ecs::MacroSpawnId{ord};
        return h;
    };
    std::vector<MacroHandle> born;
    for (std::uint32_t ord = 0; ord < 8; ++ord) born.push_back(born_one(ord));
    // Смерти в середине и в голове — freelist переиспользует их слоты, и
    // порядок рождения перестаёт совпадать с порядком слотов.
    store_death(st, born[0]);
    store_death(st, born[3]);
    // Дорождение после смертей (ординалы монотонны, слоты переиспользуются).
    for (std::uint32_t ord = 8; ord < 11; ++ord) born_one(ord);

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: сырой слот-порядок НЕ ординальный ──────────
    std::vector<std::uint32_t> raw;
    for (std::size_t s32 = 0; s32 < kMacroEntityCap; ++s32)
        if (st.alive[s32] != 0) raw.push_back(st.spawnId[s32].index);
    CHECK(raw.size() == 9, "в пуле 9 живых: 8 − 2 смерти + 3 дорождения");
    int rawInversions = 0;
    for (std::size_t i = 1; i < raw.size(); ++i)
        if (raw[i - 1] > raw[i]) ++rawInversions;
    CHECK(rawInversions > 0,
          "контроль обязан стрелять: переиспользование слота ломает "
          "слот-порядок, иначе сортировке нечего доказывать");

    // ── ЗАКОН: дверь выдаёт строго возрастающие ординалы ────────────────
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(st, order, [](std::uint16_t) { return true; });
    CHECK(order.size() == raw.size(), "дверь не теряет и не дублирует");
    int samples = 0, misordered = 0;
    for (std::size_t i = 1; i < order.size(); ++i) {
        ++samples;
        if (order[i - 1].ordinal >= order[i].ordinal) ++misordered;
    }
    CHECK(samples > 0 && misordered == 0,
          "обход по ординалу: строго возрастает, без равных");
    // Слоты двери — те же, что в store (ординал ведёт к своему слоту).
    int matched = 0;
    for (const SquadWalkEntry& sw : order)
        if (st.spawnId[sw.slot].index == sw.ordinal) ++matched;
    CHECK(matched == int(order.size()), "пара ординал↔слот не разъехалась");

    // ── Предикат фильтрует по слоту (замена exclude<Dead>) ──────────────
    st.dead[order[0].slot] = 1;
    std::vector<SquadWalkEntry> living;
    collect_squads_by_ordinal(
        st, living, [&](std::uint16_t slot) { return st.dead[slot] == 0; });
    CHECK(living.size() + 1 == order.size(),
          "предикат снял ровно одного мёртвого");

    // ── Повторный сбор в тот же скрэтч — идемпотентен (член рантайма) ───
    collect_squads_by_ordinal(st, order, [](std::uint16_t) { return true; });
    CHECK(order.size() == raw.size(), "повторный сбор не накапливает");

    return sm::test::report("squad_walk_test");
}
