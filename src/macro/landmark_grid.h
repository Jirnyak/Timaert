// THE baked "cell → WHO LIVES HERE" index (CANON S6/S9, 2026-08-24).
//
// Before this grid, the question was answered by linear scans over
// settlements / villages / spires — written at least three times
// (resolve_context, fauna's landmark_kind_at, the spire placer), each with its
// own priority order, one of them already drifted (canon-audit C2). The scan
// order IS a world fact — who owns a cell two landmarks would share — so it
// must exist once. It exists in for_each_landmark (landmark_iter.h); this grid
// is that order, baked: first landmark yielded at a cell wins it.
//
// THE GRID ANSWERS ОДНО: «КТО ЗДЕСЬ ЖИВЁТ» — ординал личности клетки, и
// больше ничего (M-90, ломтик E шаг 4). Прежде оно отвечало парой
// `{type, id}`, и РОД в этой паре был колонкой-сиротой по DOD п.9: её
// единственный читатель (`cell_facts@src/macro/cell_facts.cpp`) сравнивал её
// с `None`, чтобы узнать «есть ли тут что-то», — вопрос, на который сам слот
// уже отвечает, — а РОД, который он кладёт в факты, брал из колонки САМОЙ
// ЗАПИСИ (`rec->type`), и правильно брал: байт сетки был КОПИЕЙ, которая
// обновляется только перепёком.
//
// «ЧТО СТОИТ НА КЛЕТКЕ» ОТВЕЧАЕТ БАЙТ ФИЧИ (владелец 2026-09-30:
// `FT_City/FT_Village/FT_Spire/FT_Ruin@src/macro/features.h`), «КТО ЗДЕСЬ
// ЖИВЁТ» — ЭТОТ ординал, а после шага 5 — неподвижный сквад, и тогда сетка
// умирает целиком вместе со списком мест.
//
// Живые факты — паства, тир спелла, фракция, выкачанность — плывут ежедневно
// и достаются из `GameState` по ординалу в момент спроса. Поэтому устаревшая
// сетка может соврать РОВНО об одном: кто стоит здесь, — а это меняется
// только когда место рождается, умирает или перерождается (CANON S9), то есть
// в точках перепёка: генезис и загрузка.
//
// u16 слот на клетку в плотный список ординалов: 2 МиБ на мир 1024², и кап
// 65534 мест говорит ВСЛУХ вместо молчаливого обрезания (S26).
#pragma once
#include "core/torus.h"
#include "macro/landmark_iter.h"

#include <cstdio>
#include <cstdint>
#include <vector>

namespace sm {

struct LandmarkGrid {
    static constexpr std::uint16_t kNoLandmark = 0xFFFF;

    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> slot;   // per cell: index into refs, or kNoLandmark
    // ОРДИНАЛЫ ЛИЧНОСТЕЙ (род 2), плотным списком. 0 здесь не встречается
    // вовсе: эмитент выдаёт с 1, а «никто» говорит СЛОТ, не строка списка
    // (ЗАКОН НУЛЯ-ОРДИНАЛА против ЗАКОНА УЗКОГО ИНДЕКСА — у них разные нули,
    // и здесь живут оба: `kNoLandmark` — предел типа ИНДЕКСА, 0 — «никто» у
    // ОРДИНАЛА, который возвращает дверь ниже).
    std::vector<std::int32_t>  refs;

    // Torus-wrapped, fail-closed: an unbuilt grid answers "nothing stands
    // here" — ординал 0, «никто», never a crash.
    std::int32_t at(int x, int y) const {
        if (width <= 0 || height <= 0
            || slot.size() != std::size_t(width) * std::size_t(height)) {
            return 0;
        }
        if (!world_shape_ok(width, height)) return 0;
        const std::uint16_t s = slot[cell_of(x, y, width)];
        return s == kNoLandmark ? 0 : refs[s];
    }
};

// СТОРОЖ РАЗМЕРА (AGENTS DOD п.10). Шапка выше называет цену — «2 МиБ на мир
// 1024²», — а названный размер обязан стоять под компилятором, иначе он проза
// (шрам: `MacroNpcRuntime` обещал «~36 bytes» при 96). `LandmarkGrid` = 56 Б:
// два `int` и два заголовка вектора, ни одного поля сверх. Третий вектор
// сдвинет число и потребует своей строки переписи (`core/stacks.h`), а не
// молчаливого роста. Сами 2 МиБ — это `slot` (u16 на клетку), и они стоят
// строкой там же.
//
// `sizeof(LandmarkRef) == 8` стоял здесь вторым сторожем и умер вместе со
// структурой: строка списка стала ОДНИМ ординалом, 8 Б → 4 Б, то есть список
// ссылок подешевел ВДВОЕ ровно потому, что из него ушёл дубль рода.
static_assert(sizeof(LandmarkGrid) == 56,
              "сетка мест: новый вектор = новая строка переписи штабелей");

inline LandmarkGrid build_landmark_grid(const GameState& gs) {
    LandmarkGrid g;
    g.width = gs.mapW;
    g.height = gs.mapH;
    if (g.width <= 0 || g.height <= 0) return g;
    g.slot.assign(std::size_t(g.width) * std::size_t(g.height),
                  LandmarkGrid::kNoLandmark);
    g.refs.clear();
    for_each_landmark(gs, [&](const LandmarkView& lv) {
        auto& s = g.slot[cell_of(lv.x, lv.y, g.width)];
        // First landmark yielded at a cell owns it — the iterator's order is
        // the ONE priority (it is the same order resolve_context used to scan).
        if (s != LandmarkGrid::kNoLandmark) return;
        // THE CAP SPEAKS OUT LOUD (CANON S26). This used to be an `assert`,
        // which is nothing at all in the build the player runs: past 65 534
        // landmarks the u16 slot would have taken kNoLandmark's own value and
        // every cell of that place would have answered «nothing stands here»
        // — a world silently missing a town, in release only. The refusal is
        // said and the cell is honestly left empty instead.
        if (g.refs.size() >= LandmarkGrid::kNoLandmark) {
            std::fprintf(stderr,
                         "[landmark-grid] slot space exhausted at %zu "
                         "landmarks — cell %d,%d left unowned\n",
                         g.refs.size(), cell_x(cell_of(lv.x, lv.y, g.width),
                                               g.width),
                         cell_y(cell_of(lv.x, lv.y, g.width), g.width));
            return;
        }
        s = std::uint16_t(g.refs.size());
        g.refs.push_back(lv.id);
    });
    return g;
}

} // namespace sm
