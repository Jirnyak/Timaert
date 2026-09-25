// Свидетель ЗАКОНА ПОРЯДКА ОБХОДА (macro/squad_walk.h, эпик 2 шаг 1а/1в):
// обход сквадов идёт по ординалу рождения, а не по внутренностям EnTT.
// С флипа 1в ординал живёт колонкой store, фильтр — предикат по слоту.
//
// Негативный контроль обязан РЕАЛЬНО стрелять (тестовый закон §8 п.6):
// смерть в середине пула перемешивает его swap-удалением, и сырой view
// обязан выдать инверсию ординалов — иначе контроль ничего не детектирует
// и сортировка недоказуема.
#include "check.h"

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/squad_walk.h"
#include "macro/store.h"

#include <cstdint>
#include <vector>

int main() {
    using namespace sm;
    ecs::World w;
    auto store = make_macro_store();
    store_attach(w, store.get());
    MacroStore& st = *store;

    // Рождения 0..7 в порядке ординала — как единственная дверь make_npc.
    auto born_one = [&](std::uint32_t ord) {
        const MacroHandle h = store_birth(st);
        auto e = w.reg.create();
        w.reg.emplace<ecs::MacroSlot>(e, h.slot);
        st.spawnId[h.slot] = ecs::MacroSpawnId{ord};
        return e;
    };
    std::vector<entt::entity> born;
    for (std::uint32_t ord = 0; ord < 8; ++ord) born.push_back(born_one(ord));
    // Смерти в середине и в голове — swap-удаление тащит хвост пула на их
    // места, порядок вставки ломается. Слот умирает ВМЕСТЕ с мостом.
    auto kill = [&](entt::entity e) {
        const std::uint16_t slot = slot_of(w.reg, e);
        store_death(st, MacroHandle{slot, st.generation[slot]});
        w.reg.destroy(e);
    };
    kill(born[0]);
    kill(born[3]);
    // Дорождение после смертей (ординалы монотонны, слоты переиспользуются).
    for (std::uint32_t ord = 8; ord < 11; ++ord) born_one(ord);

    auto view = w.reg.view<ecs::MacroSlot>();

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: сырой порядок view НЕ ординальный ──────────
    std::vector<std::uint32_t> raw;
    for (auto e : view)
        raw.push_back(st.spawnId[w.reg.get<ecs::MacroSlot>(e).slot].index);
    CHECK(raw.size() == 9, "в пуле 9 живых: 8 − 2 смерти + 3 дорождения");
    int rawInversions = 0;
    for (std::size_t i = 1; i < raw.size(); ++i)
        if (raw[i - 1] > raw[i]) ++rawInversions;
    CHECK(rawInversions > 0,
          "контроль обязан стрелять: swap-удаление ломает порядок пула, "
          "иначе сортировке нечего доказывать");

    // ── ЗАКОН: дверь выдаёт строго возрастающие ординалы ────────────────
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(w.reg, st, view, order,
                              [](std::uint16_t) { return true; });
    CHECK(order.size() == raw.size(), "дверь не теряет и не дублирует");
    int samples = 0, misordered = 0;
    for (std::size_t i = 1; i < order.size(); ++i) {
        ++samples;
        if (order[i - 1].ordinal >= order[i].ordinal) ++misordered;
    }
    CHECK(samples > 0 && misordered == 0,
          "обход по ординалу: строго возрастает, без равных");
    // Сущности двери — те же, что в view (ординал ведёт к своей энтити).
    int matched = 0;
    for (const SquadWalkEntry& sw : order)
        if (st.spawnId[slot_of(w.reg, sw.e)].index == sw.ordinal) ++matched;
    CHECK(matched == int(order.size()), "пара ординал↔энтити не разъехалась");

    // ── Предикат фильтрует по слоту (замена exclude<Dead>) ──────────────
    st.dead[slot_of(w.reg, order[0].e)] = 1;
    std::vector<SquadWalkEntry> living;
    collect_squads_by_ordinal(
        w.reg, st, view, living,
        [&](std::uint16_t slot) { return st.dead[slot] == 0; });
    CHECK(living.size() + 1 == order.size(),
          "предикат снял ровно одного мёртвого");

    // ── Повторный сбор в тот же скрэтч — идемпотентен (член рантайма) ───
    collect_squads_by_ordinal(w.reg, st, view, order,
                              [](std::uint16_t) { return true; });
    CHECK(order.size() == raw.size(), "повторный сбор не накапливает");

    return sm::test::report("squad_walk_test");
}
