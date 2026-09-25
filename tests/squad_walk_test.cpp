// Свидетель ЗАКОНА ПОРЯДКА ОБХОДА (macro/squad_walk.h, эпик 2 шаг 1а):
// обход сквадов идёт по ординалу рождения, а не по внутренностям EnTT.
//
// Негативный контроль обязан РЕАЛЬНО стрелять (тестовый закон §8 п.6):
// смерть в середине пула перемешивает его swap-удалением, и сырой view
// обязан выдать инверсию ординалов — иначе контроль ничего не детектирует
// и сортировка недоказуема.
#include "check.h"

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/squad_walk.h"

#include <cstdint>
#include <vector>

int main() {
    using namespace sm;
    ecs::World w;

    // Рождения 0..7 в порядке ординала — как единственная дверь make_npc.
    std::vector<entt::entity> born;
    for (std::uint32_t ord = 0; ord < 8; ++ord) {
        auto e = w.reg.create();
        w.reg.emplace<ecs::MacroSpawnId>(e, ord);
        w.reg.emplace<ecs::MacroCell>(e, std::uint32_t(ord * 3u));
        born.push_back(e);
    }
    // Смерти в середине и в голове — swap-удаление тащит хвост пула на их
    // места, порядок вставки ломается.
    w.reg.destroy(born[0]);
    w.reg.destroy(born[3]);
    // Дорождение после смертей (ординалы монотонны, слоты entt переиспользуются).
    for (std::uint32_t ord = 8; ord < 11; ++ord) {
        auto e = w.reg.create();
        w.reg.emplace<ecs::MacroSpawnId>(e, ord);
        w.reg.emplace<ecs::MacroCell>(e, std::uint32_t(ord * 3u));
    }

    auto view = w.reg.view<ecs::MacroSpawnId, ecs::MacroCell>();

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: сырой порядок view НЕ ординальный ──────────
    std::vector<std::uint32_t> raw;
    for (auto e : view) raw.push_back(w.reg.get<ecs::MacroSpawnId>(e).index);
    CHECK(raw.size() == 9, "в пуле 9 живых: 8 − 2 смерти + 3 дорождения");
    int rawInversions = 0;
    for (std::size_t i = 1; i < raw.size(); ++i)
        if (raw[i - 1] > raw[i]) ++rawInversions;
    CHECK(rawInversions > 0,
          "контроль обязан стрелять: swap-удаление ломает порядок пула, "
          "иначе сортировке нечего доказывать");

    // ── ЗАКОН: дверь выдаёт строго возрастающие ординалы ────────────────
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(w.reg, view, order);
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
        if (w.reg.get<ecs::MacroSpawnId>(sw.e).index == sw.ordinal) ++matched;
    CHECK(matched == int(order.size()), "пара ординал↔энтити не разъехалась");

    // ── Повторный сбор в тот же скрэтч — идемпотентен (член рантайма) ───
    collect_squads_by_ordinal(w.reg, view, order);
    CHECK(order.size() == raw.size(), "повторный сбор не накапливает");

    return sm::test::report("squad_walk_test");
}
