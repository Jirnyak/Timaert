// Округи, порталы, граф — запекание и походка (nav_field.h).
#include "macro/nav_field.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "core/torus.h"
#include "macro/features.h"
#include "macro/landmark_iter.h"   // for_each_place — места по слотам
#include "macro/pathfinding.h"
#include "macro/state.h"

namespace sm {

namespace {

// Кванты цены: вес клетки 1..10 × шаг (1/√2·2) + подъём ≤20 — шкала 1/16
// держит внутриокружные пути в uint16; глобальные суммы копятся в uint32.
inline std::uint16_t quant16(float g) {
    const long q = std::lround(g * 16.0f);
    return std::uint16_t(std::clamp(q, 0L, long(kNavUnreached) - 1L));
}

inline float cell_weight_of(const PathCostData* pc, std::size_t idx) {
    return pc && idx < pc->costGrid.size() ? pc->costGrid[idx] : 1.0f;
}

inline float edge_cost_of(const PathCostData* pc, std::size_t from,
                          std::size_t to, int d) {
    const float stepLen =
        (kNavDX[d] != 0 && kNavDY[d] != 0) ? 1.4142136f : 1.0f;
    float w = cell_weight_of(pc, to) * stepLen;
    if (pc && pc->height16.size() == pc->costGrid.size()) w += pc->climb(from, to);
    return w;
}

// Маленькая бинарная куча (g, узел) — рабочая память запекания.
struct BakeHeap {
    struct Node { float g; std::uint32_t idx; };
    std::vector<Node> items;
    void push(float g, std::uint32_t idx) {
        items.push_back({g, idx});
        std::size_t i = items.size() - 1;
        while (i > 0) {
            const std::size_t p = (i - 1) >> 1;
            if (items[p].g <= items[i].g) break;
            std::swap(items[p], items[i]);
            i = p;
        }
    }
    Node pop() {
        const Node top = items.front();
        items.front() = items.back();
        items.pop_back();
        std::size_t i = 0;
        for (;;) {
            const std::size_t l = 2 * i + 1, r = 2 * i + 2;
            std::size_t s = i;
            if (l < items.size() && items[l].g < items[s].g) s = l;
            if (r < items.size() && items[r].g < items[s].g) s = r;
            if (s == i) break;
            std::swap(items[s], items[i]);
            i = s;
        }
        return top;
    }
    bool empty() const { return items.empty(); }
};

} // namespace

std::size_t NavWorld::cell(int x, int y) const {
    return cell_of(x, y, mapW);   // ЗАКОН АДРЕСА: одна дверь, маска
}

// (Нет nav_can_stand. ВЕРДИКТ ВЛАДЕЛЬЦА 2026-10-06, дословно: «да уничтодить
// вторую стену она портит всё (ВЕСА БЫЛО ЕДИНОЕ РЕШЕНИЕ». Предикат отвечал на
// ТРИ вопроса — пройти, встать лагерем, залить округу, — и на все три уже
// отвечал ВЕС: вода есть самая дорогая строка прайс-листа
// (biome_sp_weight@src/macro/movement_cost.h). Цена этой второй стены названа
// числом: 4328–4701 клетка СУШИ оставалась вне всякой округи навигации в
// каждом замеренном мире, то есть остров без моста был недостижим не потому,
// что дорого, а потому, что запрещено. Теперь заливка замощает весь тор, и
// требование «от любого места до любого добраться» истинно ПО ПОСТРОЕНИЮ.)

std::uint16_t nav_region_at(const NavWorld& nv, int x, int y) {
    if (!nv.baked()) return kNavNoRegion;
    return nv.regionOf[nv.cell(x, y)];
}

const NavPortal* nav_region_portals(const NavWorld& nv, std::uint16_t region,
                                    int& outCount) {
    outCount = 0;
    if (!nv.baked()) return nullptr;
    const std::size_t r = std::size_t(region);
    if (r >= nv.portalBegin.size() || r >= nv.portalCount.size())
        return nullptr;
    const int n = int(nv.portalCount[r]);
    if (n <= 0) return nullptr;
    const std::uint32_t begin = nv.portalBegin[r];
    if (std::size_t(begin) + std::size_t(n) > nv.portals.size())
        return nullptr;
    outCount = n;
    return nv.portals.data() + begin;
}

bool nav_regions_adjacent(const NavWorld& nv, std::uint16_t a,
                          std::uint16_t b) {
    if (a == kNavNoRegion || b == kNavNoRegion) return false;
    if (a == b) return true;   // «своя округа» — тот же горизонт
    int n = 0;
    const NavPortal* p = nav_region_portals(nv, a, n);
    for (int i = 0; i < n; ++i)
        if (p[i].toRegion == b) return true;
    return false;
}

void nav_bake(const MacroWorld& mw, NavWorld& nv) {
    if (!mw.gs || !mw.store) return;
    const GameState& gs = *mw.gs;
    const MacroStore& st = *mw.store;
    const PathCostData* pc = mw.pathCost;
    nv.mapW = gs.mapW;
    nv.mapH = gs.mapH;
    const int W = nv.mapW, H = nv.mapH;
    const std::size_t cells = std::size_t(W) * std::size_t(H);

    // ── Сиды: каждое МЕСТО — своя округа ─────────────────────────────────
    // Гейт «род не None» больше не нужен: обход мест и есть ответ оси рода
    // (is_settlement_kind), а бестиповых мест в популяции не бывает.
    nv.regionLandmarkId.clear();
    nv.regionCell.clear();
    for_each_place(st, [&](std::uint16_t slot) {
        nv.regionLandmarkId.push_back(int(st.spawnId[slot].index));
        nv.regionCell.push_back(
            int(nv.cell(ecs::cell_x(st.cell[slot], gs.mapW),
                        ecs::cell_y(st.cell[slot], gs.mapW))));
    });
    const int R = int(nv.regionLandmarkId.size());
    if (R == 0 || R >= int(kNavNoRegion)) {
        nv.regionOf.clear();
        return;
    }

    // ── Разбиение: одна мультиисточниковая Дейкстра по становимым ────────
    nv.regionOf.assign(cells, kNavNoRegion);
    nv.distHome.assign(cells, kNavUnreached);
    nv.stepHome.assign(cells, kNavNoStep);
    std::vector<float> g(cells, 1e30f);
    BakeHeap heap;
    for (int r = 0; r < R; ++r) {
        const std::uint32_t c = std::uint32_t(nv.regionCell[std::size_t(r)]);
        // Гейт «ландмарк в воде не тянет округу» снят вместе со стеной: место
        // стоит там, где стоит, и его округа заливается от него всегда.
        if (g[c] == 0.0f) continue;   // два ландмарка на клетке: первый взял
        g[c] = 0.0f;
        nv.regionOf[c] = std::uint16_t(r);
        nv.distHome[c] = 0;
        heap.push(0.0f, c);
    }
    // Сосед волны — шаг ИНДЕКСА (cell_step, ЗАКОН АДРЕСА): здесь стояли
    // деление на клетку и две рукописные композиции wrapi.
    while (!heap.empty()) {
        const auto cur = heap.pop();
        const std::uint32_t c = cur.idx;
        if (cur.g > g[c]) continue;
        for (int d = 0; d < 8; ++d) {
            const std::uint32_t n = cell_step(c, kNavDX[d], kNavDY[d], W);
            // Ни одного фильтра: ЦЕНА и есть ответ. Вода просто дорога, и
            // заливка честно доходит до каждой клетки тора.
            const float ng = cur.g + edge_cost_of(pc, c, n, d);
            if (ng >= g[n]) continue;
            g[n] = ng;
            nv.regionOf[n] = nv.regionOf[c];
            nv.distHome[n] = quant16(ng);
            nv.stepHome[n] = std::uint8_t((d + 4) & 7);   // шаг назад к дому
            heap.push(ng, n);
        }
    }

    // (НЕТ ВОДНОГО ЯРУСА. M-237, вердикт владельца 2026-10-06 — знание
    // сохранено в CANON S7 «ВОДНАЯ НАВИГАЦИЯ — ЗНАНИЕ, СОХРАНЁННОЕ ПРИ
    // СНОСЕ» ДО этого реза, по его же условию: «главное чтобы навигация по
    // воде не стала утерянным знанием … потом в будущем мы её вернём».
    //
    // ОН УМЕР НЕ ПОТОМУ, ЧТО КОРАБЛЯ НЕТ, А ПОТОМУ, ЧТО ЕГО ВОПРОСА БОЛЬШЕ
    // НЕТ. Ярус сеялся от БЕРЕГОВ сухих округ и отвечал «чья эта вода» —
    // вопрос, осмысленный лишь пока у воды не было округи. После смерти
    // стены единая заливка замощает весь тор, `regionOf` не бывает
    // kNavNoRegion ни в одной клетке, и водная ветка `nav_step`
    // (`if (rt == kNavNoRegion) rt = waterRegionOf[t]`) стала недостижимым
    // кодом. Оставить ярус «на один наряд» значило бы вписать ему НОВОЕ
    // условие сида «клетка — суша», то есть вернуть стену под другим именем
    // (AGENTS §5 п.14).
    //
    // ВЕРНЁТСЯ ОН ГРАФОМ ПОРТОВ, а не вторым R²: 64 узла × 64 × 6 Б = 24 КБ
    // против 1536 МиБ морских таблиц при капе 16384. Новая стихия = новый
    // класс ребра + строка профиля; походка не меняется.)

    // ── Порталы: по связным СЕГМЕНТАМ границы (тор-закон: одна пара округ
    // может касаться двумя несвязными отрезками — каждому свой портал,
    // иначе режется цикл, шов-баг остовного дерева). Сухие — по границам
    // округ; ВОДНЫЕ — по границам водных зон тех же округ (стихия ребра). ─
    struct Crossing { std::uint32_t from, to; float cost; };
    std::unordered_map<std::uint64_t, std::vector<Crossing>> byPair;
    for (std::size_t c = 0; c < cells; ++c) {
        const std::uint16_t ra = nv.regionOf[c];
        if (ra == kNavNoRegion) continue;
        for (int d = 0; d < 8; ++d) {
            const std::size_t n =
                cell_step(std::uint32_t(c), kNavDX[d], kNavDY[d], W);
            const std::uint16_t rb = nv.regionOf[n];
            if (rb == kNavNoRegion || rb == ra) continue;
            const float cost = float(nv.distHome[c]) / 16.0f
                             + edge_cost_of(pc, c, n, d)
                             + float(nv.distHome[n]) / 16.0f;
            byPair[(std::uint64_t(ra) << 16) | rb].push_back(
                {std::uint32_t(c), std::uint32_t(n), cost});
        }
    }
    struct RawPortal { std::uint32_t from, to; std::uint16_t toRegion; float cost; };
    std::vector<std::vector<RawPortal>> perRegion;
    perRegion.resize(std::size_t(R));
    const auto collect_portals = [&](
        std::unordered_map<std::uint64_t, std::vector<Crossing>>& pairs) {
        std::unordered_set<std::uint32_t> segSeen;
        std::vector<std::uint32_t> stack;
        std::unordered_map<std::uint32_t, std::vector<int>> bySrc;
        for (auto& [key, edges] : pairs) {
            const std::uint16_t ra = std::uint16_t(key >> 16);
            const std::uint16_t rb = std::uint16_t(key & 0xFFFF);
            // Клетки нашей стороны границы + их рёбра.
            bySrc.clear();
            for (int i = 0; i < int(edges.size()); ++i)
                bySrc[edges[std::size_t(i)].from].push_back(i);
            segSeen.clear();
            for (auto& [start, idxs0] : bySrc) {
                (void)idxs0;
                if (segSeen.count(start)) continue;
                // Сегмент: 8-связная компонента клеток нашей стороны.
                segSeen.insert(start);
                stack.assign(1, start);
                int bestIdx = -1;
                float bestCost = 1e30f;
                while (!stack.empty()) {
                    const std::uint32_t c = stack.back();
                    stack.pop_back();
                    for (const int ei : bySrc[c]) {
                        const Crossing& e = edges[std::size_t(ei)];
                        if (e.cost < bestCost) {
                            bestCost = e.cost;
                            bestIdx = ei;
                        }
                    }
                    for (int d = 0; d < 8; ++d) {
                        const std::uint32_t n =
                            cell_step(c, kNavDX[d], kNavDY[d], W);
                        if (!bySrc.count(n) || segSeen.count(n)) continue;
                        segSeen.insert(n);
                        stack.push_back(n);
                    }
                }
                if (bestIdx >= 0) {
                    const Crossing& e = edges[std::size_t(bestIdx)];
                    perRegion[std::size_t(ra)].push_back(
                        {e.from, e.to, rb, e.cost});
                }
            }
        }
    };
    collect_portals(byPair);
    // Слоты: дешёвые первыми; переполнение капа говорит вслух.
    nv.portals.clear();
    nv.portalOverflows = 0;   // счётчик — за ЭТО запекание
    nv.portalBegin.assign(std::size_t(R), 0);
    nv.portalCount.assign(std::size_t(R), 0);
    nv.planeCount = 0;
    for (int r = 0; r < R; ++r) {
        auto& ps = perRegion[std::size_t(r)];
        std::sort(ps.begin(), ps.end(),
                  [](const RawPortal& a, const RawPortal& b) {
                      return a.cost < b.cost;
                  });
        if (int(ps.size()) > kNavMaxPortalsPerRegion) {
            std::fprintf(stderr,
                         "[nav] region %d: %d portals, cap %d — dropping "
                         "the dearest (loud by law)\n",
                         r, int(ps.size()), kNavMaxPortalsPerRegion);
            ++nv.portalOverflows;   // тест читает счётчик, не stderr
            ps.resize(std::size_t(kNavMaxPortalsPerRegion));
        }
        nv.portalBegin[std::size_t(r)] = std::uint32_t(nv.portals.size());
        nv.portalCount[std::size_t(r)] = std::uint8_t(ps.size());
        nv.planeCount = std::max(nv.planeCount, int(ps.size()));
        for (int s = 0; s < int(ps.size()); ++s) {
            nv.portals.push_back(NavPortal{
                std::int32_t(ps[std::size_t(s)].from),
                std::int32_t(ps[std::size_t(s)].to),
                ps[std::size_t(s)].toRegion, std::uint8_t(s)});
        }
    }

    // ── Планы полей порталов: приём статьи — поле обрезано округой, поля
    // разных округ делят одну плоскость. ────────────────────────────────
    nv.planes.assign(std::size_t(nv.planeCount) * cells, kNavUnreached);
    for (int r = 0; r < R; ++r) {
        const std::uint32_t begin = nv.portalBegin[std::size_t(r)];
        for (int s = 0; s < int(nv.portalCount[std::size_t(r)]); ++s) {
            const NavPortal& p = nv.portals[begin + std::uint32_t(s)];
            std::uint16_t* plane =
                nv.planes.data() + std::size_t(s) * cells;
            BakeHeap ph;
            std::vector<std::pair<std::uint32_t, float>> touched;
            const std::uint32_t src = std::uint32_t(p.cellFrom);
            plane[src] = 0;
            ph.push(0.0f, src);
            // g-очки локально: план хранит кванты, дубль отфильтрован
            // сравнением с уже записанным квантом (шаг ≥ 1 квант).
            while (!ph.empty()) {
                const auto cur = ph.pop();
                const std::uint32_t c = cur.idx;
                if (quant16(cur.g) > plane[c]) continue;
                for (int d = 0; d < 8; ++d) {
                    const std::uint32_t n =
                        cell_step(c, kNavDX[d], kNavDY[d], W);
                    // Обрезка ОКРУГОЙ — строка статьи: план льётся только по
                    // своей округе, поэтому планы разных округ дизъюнктны и
                    // делят одну плоскость.
                    if (nv.regionOf[n] != std::uint16_t(r)) continue;
                    const float ng = cur.g + edge_cost_of(pc, c, n, d);
                    const std::uint16_t q = quant16(ng);
                    if (q >= plane[n]) continue;
                    plane[n] = q;
                    ph.push(ng, n);
                }
            }
            (void)touched;
        }
    }

    // ── Граф округ, ТАБЛИЦЫ ПО ПРОФИЛЯМ: Дейкстра ПО ГРАФУ на узел
    // (никогда дерево — тор-закон gigahrush2), next = первая округа шага.
    // Пеший профиль — сухие рёбра; морской — все («агент в курсе» = читает
    // таблицу своего профиля, CANON S10). ───────────────────────────────
    nv.routeDist.assign(std::size_t(R) * std::size_t(R), kNavFar);
    nv.routeNext.assign(std::size_t(R) * std::size_t(R), kNavNoRegion);
    struct GEdge { std::uint16_t to; std::uint32_t w; };
    std::vector<std::vector<GEdge>> adjDry;
    adjDry.resize(std::size_t(R));
    for (int r = 0; r < R; ++r) {
        const std::uint32_t begin = nv.portalBegin[std::size_t(r)];
        for (int s = 0; s < int(nv.portalCount[std::size_t(r)]); ++s) {
            const NavPortal& p = nv.portals[begin + std::uint32_t(s)];
            // Вес ребра — дорога до портала по обе стороны (+ посадка 16).
            const std::uint32_t w =
                std::uint32_t(nv.distHome[std::size_t(p.cellFrom)])
                + std::uint32_t(nv.distHome[std::size_t(p.cellTo)]) + 16u;
            adjDry[std::size_t(r)].push_back({p.toRegion, std::max(1u, w)});
        }
    }
    struct QN { std::uint32_t d; std::uint16_t v; std::uint16_t first; };
    const auto run_tables = [&](std::vector<std::vector<GEdge>>& adj,
                                std::uint32_t* distBase,
                                std::uint16_t* nextBase) {
    for (int s = 0; s < R; ++s) {
        std::uint32_t* dist = distBase + std::size_t(s) * R;
        std::uint16_t* next = nextBase + std::size_t(s) * R;
        dist[s] = 0;
        std::vector<QN> q;
        const auto push = [&](QN n) {
            q.push_back(n);
            std::size_t i = q.size() - 1;
            while (i > 0) {
                const std::size_t p = (i - 1) >> 1;
                if (q[p].d <= q[i].d) break;
                std::swap(q[p], q[i]);
                i = p;
            }
        };
        const auto pop = [&]() {
            const QN top = q.front();
            q.front() = q.back();
            q.pop_back();
            std::size_t i = 0;
            for (;;) {
                const std::size_t l = 2 * i + 1, r2 = 2 * i + 2;
                std::size_t m = i;
                if (l < q.size() && q[l].d < q[m].d) m = l;
                if (r2 < q.size() && q[r2].d < q[m].d) m = r2;
                if (m == i) break;
                std::swap(q[m], q[i]);
                i = m;
            }
            return top;
        };
        for (const GEdge& e : adj[std::size_t(s)]) {
            if (e.w < dist[e.to]) {
                dist[e.to] = e.w;
                next[e.to] = e.to;   // первый шаг — сама соседняя округа
                push({e.w, e.to, e.to});
            }
        }
        while (!q.empty()) {
            const QN cur = pop();
            if (cur.d > dist[cur.v]) continue;
            for (const GEdge& e : adj[std::size_t(cur.v)]) {
                const std::uint32_t nd = cur.d + e.w;
                if (nd >= dist[e.to]) continue;
                dist[e.to] = nd;
                next[e.to] = cur.first;   // первая округа наследуется
                push({nd, e.to, cur.first});
            }
        }
    }
    };
    run_tables(adjDry, nv.routeDist.data(), nv.routeNext.data());

    nv.bakedSeed = gs.worldSeed;
    nv.bakedNavEpoch = gs.navEpoch;

    // Сводка запекания — вслух, как учит статья: «рисуйте промежуточные
    // данные»; запекание редкое, строка дешёвая.
    // КЛЕТОК БЕЗ ОКРУГИ ОБЯЗАН БЫТЬ НОЛЬ, и это измеряемый смысл сноса
    // стены: прежде тут считалась только СУША вне округ (4328–4701 в каждом
    // замеренном мире), потому что вода вне округ была нормой. Теперь нормы
    // нет — заливка замощает весь тор, и всякое ненулевое число здесь есть
    // дефект заливки, а не география.
    std::size_t unreached = 0;
    for (std::size_t c = 0; c < cells; ++c)
        if (nv.regionOf[c] == kNavNoRegion) ++unreached;
    std::fprintf(stderr,
                 "[nav] baked R=%d portals=%zu planes=%d "
                 "noRegion=%zu\n",
                 R, nv.portals.size(), nv.planeCount, unreached);
}

bool nav_ensure(const MacroWorld& mw, NavWorld& nv) {
    if (!mw.gs) return false;
    const GameState& gs = *mw.gs;
    // Четыре сравнения целых — и ни одного прохода по миру. Состав мест и
    // мосты объявляют себя СОБЫТИЕМ (gs.navEpoch); сид и размеры ловят
    // подмену мира под живым NavWorld.
    const bool stale = !nv.baked() || nv.bakedSeed != gs.worldSeed
                    || nv.bakedNavEpoch != gs.navEpoch
                    || nv.mapW != gs.mapW || nv.mapH != gs.mapH;
    if (stale) nav_bake(mw, nv);
    return nv.baked();
}

std::uint32_t nav_path_cost(const NavWorld& nv, int ax, int ay,
                            int bx, int by) {
    if (!nv.baked()) return kNavFar;
    const std::size_t R = nv.regionLandmarkId.size();
    const std::uint16_t ra = nav_region_at(nv, ax, ay);
    const std::uint16_t rb = nav_region_at(nv, bx, by);
    if (std::size_t(ra) >= R || std::size_t(rb) >= R) return kNavFar;
    const std::uint16_t da = nv.distHome[nv.cell(ax, ay)];
    const std::uint16_t db = nv.distHome[nv.cell(bx, by)];
    if (da == kNavUnreached || db == kNavUnreached) return kNavFar;
    if (ra == rb) return std::uint32_t(da) + std::uint32_t(db);
    const std::uint32_t mid = nv.routeDist[std::size_t(ra) * R + std::size_t(rb)];
    if (mid == kNavFar) return kNavFar;
    return std::uint32_t(da) + mid + std::uint32_t(db);
}

float nav_path_days(const NavWorld& nv, int ax, int ay, int bx, int by,
                    float cellsPerDay) {
    const std::uint32_t c = nav_path_cost(nv, ax, ay, bx, by);
    if (c == kNavFar) return -1.0f;
    if (!(cellsPerDay > 0.0f)) return -1.0f;
    return (float(c) / 16.0f) / cellsPerDay;
}

bool nav_step(const NavWorld& nv, int x, int y, int tx, int ty,
              int& sdx, int& sdy) {
    if (!nv.baked()) return false;
    const std::size_t c = nv.cell(x, y);
    const std::size_t t = nv.cell(tx, ty);
    if (c == t) return false;
    const std::uint16_t rc = nv.regionOf[c];
    const std::uint16_t rt = nv.regionOf[t];
    // Ни одной поправки «а если цель на воде»: у воды теперь СВОЯ округа, как
    // у всякой клетке тора. kNavNoRegion остался сентинелем незапечённого
    // мира и выхода за таблицу, а не именем стихии.
    if (rt == kNavNoRegion) return false;
    const int W = nv.mapW, H = nv.mapH;
    const auto step_to = [&](std::size_t n) {
        const int nx = cell_x(std::uint32_t(n), W);
        const int ny = cell_y(std::uint32_t(n), W);
        int dx = nx - wrap_axis(x, W);
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = ny - wrap_axis(y, H);
        if (dy > 1) dy = -1; else if (dy < -1) dy = 1;
        sdx = dx;
        sdy = dy;
        return true;
    };
    const auto step_dir = [&](std::uint8_t dir) {
        if (dir == kNavNoStep) return false;
        sdx = kNavDX[dir];
        sdy = kNavDY[dir];
        return true;
    };
    // (НЕТ ВОДНОЙ ВЕТКИ, и она была НЕДОСТИЖИМОЙ по построению ещё до реза.
    // Её вход — `rc == kNavNoRegion`, то есть «у клетки под ходоком нет
    // округи»; после смерти стены заливка замощает весь тор и такой клетки не
    // бывает. Что она делала живьём — работала ФОЛБЭКОМ пешего: когда сухой
    // маршрут молчал, а молчал он ровно к островам, брался морской next-hop,
    // после чего функция всё равно возвращала «не знаю», сквад падал в жадный
    // шаг и упирался в берег. Плечо должны были вести сервисные законы
    // dock/верфь — их в дереве не было ни одного. CANON S7 «ВОДНАЯ НАВИГАЦИЯ —
    // ЗНАНИЕ, СОХРАНЁННОЕ ПРИ СНОСЕ».)
    if (rc == rt) {
        // Своя округа. Цель — ландмарк: спуск. Иначе — цепочка родителей
        // цели, пройденная навстречу; сбился с цепочки — к ландмарку (в
        // нём начало всякой цепочки: прогресс гарантирован, аттрактора
        // нет — distHome строго падает).
        if (t == std::size_t(std::uint32_t(nv.regionCell[rc])))
            return step_dir(nv.stepHome[c]);
        std::size_t cur = t;
        for (int guard = 0; guard < 1 << 14; ++guard) {
            const std::uint8_t dir = nv.stepHome[cur];
            if (dir == kNavNoStep) break;   // дошли до ландмарка мимо нас
            const std::size_t parent =
                cell_step(std::uint32_t(cur), kNavDX[dir], kNavDY[dir], W);
            if (parent == c) return step_to(cur);
            cur = parent;
        }
        return step_dir(nv.stepHome[c]);
    }
    // Чужая округа: таблица → соседняя округа → портал → спуск. ОДНА
    // таблица: профилей в коде никогда и не было (`nav_step` профиля не
    // принимает), а морская работала фолбэком — см. надгробие выше.
    const std::size_t R2 = nv.regionLandmarkId.size();
    const std::uint16_t nr = nv.routeNext[std::size_t(rc) * R2 + rt];
    if (nr == kNavNoRegion) return false;   // честно недостижимо
    const std::uint32_t begin = nv.portalBegin[rc];
    int bestSlot = -1;
    std::uint16_t bestVal = kNavUnreached;
    for (int s = 0; s < int(nv.portalCount[rc]); ++s) {
        const NavPortal& p = nv.portals[begin + std::uint32_t(s)];
        if (p.toRegion != nr) continue;
        if (std::size_t(std::uint32_t(p.cellFrom)) == c)
            return step_to(std::size_t(std::uint32_t(p.cellTo)));
        const std::uint16_t v =
            nv.planes[std::size_t(p.plane)
                          * (std::size_t(W) * std::size_t(H))
                      + c];
        if (v < bestVal) {
            bestVal = v;
            bestSlot = s;
        }
    }
    // Плечо без плана — к дому: планы своей округи полны, так что спуск к
    // ландмарку гарантированно прогрессирует.
    if (bestSlot < 0) return step_dir(nv.stepHome[c]);
    const NavPortal& p = nv.portals[begin + std::uint32_t(bestSlot)];
    const std::uint16_t* plane =
        nv.planes.data()
        + std::size_t(p.plane) * (std::size_t(W) * std::size_t(H));
    std::uint16_t best = plane[c];
    std::size_t bestCell = c;
    for (int d = 0; d < 8; ++d) {
        const std::size_t n =
            cell_step(std::uint32_t(c), kNavDX[d], kNavDY[d], W);
        if (nv.regionOf[n] != rc) continue;
        if (plane[n] < best) {
            best = plane[n];
            bestCell = n;
        }
    }
    if (bestCell == c) return step_dir(nv.stepHome[c]);   // плато не бывает; защита
    return step_to(bestCell);
}

} // namespace sm
