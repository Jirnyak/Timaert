// Округи, порталы, граф — запекание и походка (nav_field.h).
#include "macro/nav_field.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "core/torus.h"
#include "macro/features.h"
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
    if (pc && pc->height8.size() == pc->costGrid.size()) w += pc->climb(from, to);
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

std::uint64_t count_bridge_builts(const GameState& gs) {
    std::uint64_t n = 0;
    for (const BuiltFeature& b : gs.builtFeatures)
        if (b.ft == std::uint8_t(FT_Bridge) || b.ft == std::uint8_t(FT_WoodBridge))
            ++n;
    return n;
}

} // namespace

std::size_t NavWorld::cell(int x, int y) const {
    return std::size_t(wrapi(y, mapH)) * std::size_t(mapW)
         + std::size_t(wrapi(x, mapW));
}

bool nav_can_stand(const MacroWorld& mw, int x, int y) {
    const PathCostData* pc = mw.pathCost;
    if (!pc || pc->width <= 0 || pc->height <= 0
        || pc->water.size() != std::size_t(pc->width) * std::size_t(pc->height)) {
        return true;   // без слоя воды весь мир — суша (нулевой вклад)
    }
    const int wx = wrapi(x, pc->width);
    const int wy = wrapi(y, pc->height);
    if (!pc->water[std::size_t(wy) * std::size_t(pc->width) + wx]) return true;
    if (!mw.features) return false;
    const FeatureType ft = FeatureType(mw.features->at(x, y));
    return ft == FT_Bridge || ft == FT_WoodBridge;
}

std::uint16_t nav_region_at(const NavWorld& nv, int x, int y) {
    if (!nv.baked()) return kNavNoRegion;
    return nv.regionOf[nv.cell(x, y)];
}

void nav_bake(const MacroWorld& mw, NavWorld& nv) {
    if (!mw.gs) return;
    const GameState& gs = *mw.gs;
    const PathCostData* pc = mw.pathCost;
    nv.mapW = gs.mapW;
    nv.mapH = gs.mapH;
    const int W = nv.mapW, H = nv.mapH;
    const std::size_t cells = std::size_t(W) * std::size_t(H);

    // ── Сиды: каждый ландмарк — своя округа ──────────────────────────────
    nv.regionLandmarkId.clear();
    nv.regionCell.clear();
    for (const Landmark& lm : gs.landmarks) {
        if (lm.type == LandmarkType::None) continue;
        nv.regionLandmarkId.push_back(lm.id);
        nv.regionCell.push_back(int(nv.cell(lm.x, lm.y)));
    }
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
        if (!nav_can_stand(mw, int(c % std::uint32_t(W)),
                           int(c / std::uint32_t(W))))
            continue;   // ландмарк в воде не тянет округу (честный ноль)
        if (g[c] == 0.0f) continue;   // два ландмарка на клетке: первый взял
        g[c] = 0.0f;
        nv.regionOf[c] = std::uint16_t(r);
        nv.distHome[c] = 0;
        heap.push(0.0f, c);
    }
    while (!heap.empty()) {
        const auto cur = heap.pop();
        const std::uint32_t c = cur.idx;
        if (cur.g > g[c]) continue;
        const int cx = int(c % std::uint32_t(W));
        const int cy = int(c / std::uint32_t(W));
        for (int d = 0; d < 8; ++d) {
            const int nx = wrapi(cx + kNavDX[d], W);
            const int ny = wrapi(cy + kNavDY[d], H);
            const std::uint32_t n =
                std::uint32_t(ny) * std::uint32_t(W) + std::uint32_t(nx);
            if (!nav_can_stand(mw, nx, ny)) continue;
            const float ng = cur.g + edge_cost_of(pc, c, n, d);
            if (ng >= g[n]) continue;
            g[n] = ng;
            nv.regionOf[n] = nv.regionOf[c];
            nv.distHome[n] = quant16(ng);
            nv.stepHome[n] = std::uint8_t((d + 4) & 7);   // шаг назад к дому
            heap.push(ng, n);
        }
    }

    // ── Порталы: по связным СЕГМЕНТАМ границы (тор-закон: одна пара округ
    // может касаться двумя несвязными отрезками — каждому свой портал,
    // иначе режется цикл, шов-баг остовного дерева). ────────────────────
    struct Crossing { std::uint32_t from, to; float cost; };
    std::unordered_map<std::uint64_t, std::vector<Crossing>> byPair;
    for (std::size_t c = 0; c < cells; ++c) {
        const std::uint16_t ra = nv.regionOf[c];
        if (ra == kNavNoRegion) continue;
        const int cx = int(c % std::size_t(W));
        const int cy = int(c / std::size_t(W));
        for (int d = 0; d < 8; ++d) {
            const int nx = wrapi(cx + kNavDX[d], W);
            const int ny = wrapi(cy + kNavDY[d], H);
            const std::size_t n =
                std::size_t(ny) * std::size_t(W) + std::size_t(nx);
            const std::uint16_t rb = nv.regionOf[n];
            if (rb == kNavNoRegion || rb == ra) continue;
            const float cost = float(nv.distHome[c]) / 16.0f
                             + edge_cost_of(pc, c, n, d)
                             + float(nv.distHome[n]) / 16.0f;
            const std::uint64_t key =
                (std::uint64_t(ra) << 16) | std::uint64_t(rb);
            byPair[key].push_back(
                {std::uint32_t(c), std::uint32_t(n), cost});
        }
    }
    struct RawPortal { std::uint32_t from, to; std::uint16_t toRegion; float cost; };
    std::vector<std::vector<RawPortal>> perRegion;
    perRegion.resize(std::size_t(R));
    {
        std::unordered_set<std::uint32_t> segSeen;
        std::vector<std::uint32_t> stack;
        std::unordered_map<std::uint32_t, std::vector<int>> bySrc;
        for (auto& [key, edges] : byPair) {
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
                    const int cx = int(c % std::uint32_t(W));
                    const int cy = int(c / std::uint32_t(W));
                    for (int d = 0; d < 8; ++d) {
                        const std::uint32_t n = std::uint32_t(
                            std::size_t(wrapi(cy + kNavDY[d], H))
                                * std::size_t(W)
                            + std::size_t(wrapi(cx + kNavDX[d], W)));
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
    }
    // Слоты: дешёвые первыми; переполнение капа говорит вслух.
    nv.portals.clear();
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
                const int cx = int(c % std::uint32_t(W));
                const int cy = int(c / std::uint32_t(W));
                for (int d = 0; d < 8; ++d) {
                    const int nx = wrapi(cx + kNavDX[d], W);
                    const int ny = wrapi(cy + kNavDY[d], H);
                    const std::uint32_t n = std::uint32_t(
                        std::size_t(ny) * std::size_t(W) + std::size_t(nx));
                    if (nv.regionOf[n] != std::uint16_t(r))
                        continue;   // обрезка округой — строка статьи
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

    // ── Граф округ: Дейкстра ПО ГРАФУ на узел (никогда дерево — тор-закон
    // gigahrush2), next = первая округа шага. ───────────────────────────
    nv.routeDist.assign(std::size_t(R) * std::size_t(R), kNavFar);
    nv.routeNext.assign(std::size_t(R) * std::size_t(R), kNavNoRegion);
    struct GEdge { std::uint16_t to; std::uint32_t w; };
    std::vector<std::vector<GEdge>> adj;
    adj.resize(std::size_t(R));
    for (int r = 0; r < R; ++r) {
        const std::uint32_t begin = nv.portalBegin[std::size_t(r)];
        for (int s = 0; s < int(nv.portalCount[std::size_t(r)]); ++s) {
            const NavPortal& p = nv.portals[begin + std::uint32_t(s)];
            const std::uint32_t w =
                std::uint32_t(nv.distHome[std::size_t(p.cellFrom)])
                + std::uint32_t(nv.distHome[std::size_t(p.cellTo)]) + 16u;
            adj[std::size_t(r)].push_back({p.toRegion, std::max(1u, w)});
        }
    }
    struct QN { std::uint32_t d; std::uint16_t v; std::uint16_t first; };
    for (int s = 0; s < R; ++s) {
        std::uint32_t* dist = nv.routeDist.data() + std::size_t(s) * R;
        std::uint16_t* next = nv.routeNext.data() + std::size_t(s) * R;
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

    nv.bakedSeed = gs.worldSeed;
    nv.bakedLandmarks = std::uint32_t(R);
    nv.bakedBridgeBuilts = count_bridge_builts(gs);
    nv.seenBuiltCount = gs.builtFeatures.size();

    // Сводка запекания — вслух, как учит статья: «рисуйте промежуточные
    // данные»; запекание редкое, строка дешёвая.
    std::size_t unreached = 0;
    for (std::size_t c = 0; c < cells; ++c)
        if (nv.regionOf[c] == kNavNoRegion
            && nav_can_stand(mw, int(c % std::size_t(W)),
                             int(c / std::size_t(W))))
            ++unreached;
    std::fprintf(stderr,
                 "[nav] baked R=%d portals=%zu planes=%d "
                 "dryUnreached=%zu\n",
                 R, nv.portals.size(), nv.planeCount, unreached);
}

bool nav_ensure(const MacroWorld& mw, NavWorld& nv) {
    if (!mw.gs) return false;
    const GameState& gs = *mw.gs;
    std::uint32_t live = 0;
    for (const Landmark& lm : gs.landmarks)
        if (lm.type != LandmarkType::None) ++live;
    bool stale = !nv.baked() || nv.bakedSeed != gs.worldSeed
              || nv.bakedLandmarks != live
              || nv.mapW != gs.mapW || nv.mapH != gs.mapH;
    if (!stale && nv.seenBuiltCount != gs.builtFeatures.size()) {
        // Дельту смотрим на МОСТЫ: только они меняют проходимость.
        const std::uint64_t bridges = count_bridge_builts(gs);
        if (bridges != nv.bakedBridgeBuilts) stale = true;
        else nv.seenBuiltCount = gs.builtFeatures.size();
    }
    if (stale) nav_bake(mw, nv);
    return nv.baked();
}

bool nav_step(const NavWorld& nv, int x, int y, int tx, int ty,
              int& sdx, int& sdy) {
    if (!nv.baked()) return false;
    const std::size_t c = nv.cell(x, y);
    const std::size_t t = nv.cell(tx, ty);
    if (c == t) return false;
    const std::uint16_t rc = nv.regionOf[c];
    const std::uint16_t rt = nv.regionOf[t];
    if (rc == kNavNoRegion || rt == kNavNoRegion) return false;
    const int W = nv.mapW, H = nv.mapH;
    const auto step_to = [&](std::size_t n) {
        const int nx = int(n % std::size_t(W));
        const int ny = int(n / std::size_t(W));
        int dx = nx - wrapi(x, W);
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = ny - wrapi(y, H);
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
            const int px = wrapi(int(cur % std::size_t(W)) + kNavDX[dir], W);
            const int py = wrapi(int(cur / std::size_t(W)) + kNavDY[dir], H);
            const std::size_t parent =
                std::size_t(py) * std::size_t(W) + std::size_t(px);
            if (parent == c) return step_to(cur);
            cur = parent;
        }
        return step_dir(nv.stepHome[c]);
    }
    // Чужая округа: таблица → соседняя округа → её портал → спуск по плану.
    const std::uint16_t nr =
        nv.routeNext[std::size_t(rc) * nv.regionLandmarkId.size() + rt];
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
    if (bestSlot < 0) return step_dir(nv.stepHome[c]);   // к дому — там планы полны
    const NavPortal& p = nv.portals[begin + std::uint32_t(bestSlot)];
    const std::uint16_t* plane =
        nv.planes.data()
        + std::size_t(p.plane) * (std::size_t(W) * std::size_t(H));
    const int cx = wrapi(x, W), cy = wrapi(y, H);
    std::uint16_t best = plane[c];
    std::size_t bestCell = c;
    for (int d = 0; d < 8; ++d) {
        const int nx = wrapi(cx + kNavDX[d], W);
        const int ny = wrapi(cy + kNavDY[d], H);
        const std::size_t n =
            std::size_t(ny) * std::size_t(W) + std::size_t(nx);
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
