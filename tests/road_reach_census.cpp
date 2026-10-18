// ПЕРЕПИСЬ ДОСТУПНОСТИ МЕСТ ДОРОГОЙ — ПРИБОР ПОД bugs.md Б3, А НЕ СВИДЕТЕЛЬ.
//
// ЗАЧЕМ. Владелец доложил 2026-09-30 (кадры у него): «дороги рвутся у городов»
// — полотно обрывается, не доходя до места. Доклад пролежал в промте неделю и
// был стёрт; восстановлен в `bugs.md` Б3 нарядом M-264 2026-10-18. По правилу
// того файла `Б` без своей `Л` есть симптом, которому нечем ответить, и это
// обязано быть видно. Этот прибор переводит Б в Л: он не судит, а ПЕЧАТАЕТ
// числом, насколько далеко от каждого МЕСТА лежит ближайшая дорожная клетка.
//
// ПОЧЕМУ ЭТОГО НЕ ВИДЕЛ НИ ОДИН ЖИВОЙ ПРИБОР. `road_river_generation_test`
// судит СИНТЕТИЧЕСКИЕ фикстуры (отсев рёбер, ребро по воде) и настоящего мира
// не рождает вовсе; переписи рельефа (`height_census`, `relief_census`) зовут
// `generate_terrain`, то есть не доходят до фич и сквадов. Вопрос «дошла ли
// дорога до места» не задавал никто, и симптом жил только глазами владельца.
//
// ЧТО ИМЕННО МЕРЯЕТСЯ, ЧЕСТНО. Multi-source BFS по ТОРУ от ВСЕХ дорожных
// клеток (`FT_Road`, `FT_DirtRoad`, `FT_Bridge`) даёт поле расстояний в
// клетках; затем для каждого неподвижного сквада (`SquadArchetype::Place` —
// ось родов, не список имён) читается его клетка. Расстояние 0 значит «дорога
// НА клетке места», 1 — «в соседней», большое — обрыв. Прибор ОТЧИТЫВАЕТСЯ, а
// сюита НЕ СУДИТ: порога «сколько допустимо» никто не выводил, и выдумывать
// его здесь значило бы поставить число с потолка (ЗАКОН КОНСТАНТ) и получить
// свидетеля, охраняющего случай. Поэтому цель прибора — дать владельцу и
// следующему агенту ЧИСЛО и РАСПРЕДЕЛЕНИЕ, с которыми можно спорить.
//
// Шаг BFS — ВОСЕМЬ румбов через `cell_step` (ЗАКОН АДРЕСА: соседство есть
// арифметика над индексом, а заворот — маска). Дорога, пришедшая по диагонали,
// для глаза связна, поэтому мера обязана быть той же, что у глаза.
//
// ЗАПУСК: `./build/road_reach_census [сид ...]` (по умолчанию три сида).

#include "core/caps.h"
#include "core/torus.h"
#include "ecs/components.h"
#include "macro/features.h"
#include "macro/npc_ai.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "macro/store.h"
#include "macro/world_gen.h"
#include "macro/zones.h"
#include "macro/map_generator.h"
#include "macro/tree_layer.h"
#include "macro/deposit_layer.h"
#include "tables/squad_type.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

namespace {

struct PlaceReach {
    std::uint8_t  type = 0;
    std::uint32_t cell = 0;
    int           dist = -1;   // клеток до ближайшей дороги; -1 = дороги нет вовсе
};

bool is_road_byte(std::uint8_t raw) {
    const sm::FeatureType ft = sm::FeatureLayer::decode(raw);
    return ft == sm::FT_Road || ft == sm::FT_DirtRoad || ft == sm::FT_Bridge;
}

// Поле расстояний от дорожной сети по тору, восемь румбов, BFS без веса.
std::vector<std::int32_t> road_distance_field(const sm::FeatureLayer& f, int side) {
    const std::size_t n = std::size_t(side) * std::size_t(side);
    std::vector<std::int32_t> dist(n, -1);
    std::deque<std::uint32_t> q;
    for (std::size_t i = 0; i < n && i < f.data.size(); ++i) {
        if (is_road_byte(f.data[i])) { dist[i] = 0; q.push_back(std::uint32_t(i)); }
    }
    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!q.empty()) {
        const std::uint32_t cur = q.front();
        q.pop_front();
        const std::int32_t d = dist[cur];
        for (int k = 0; k < 8; ++k) {
            const std::uint32_t nb = sm::cell_step(cur, dx[k], dy[k], side);
            if (dist[nb] < 0) { dist[nb] = d + 1; q.push_back(nb); }
        }
    }
    return dist;
}

void census_seed(std::uint32_t seed) {
    sm::GameState gs;
    sm::TerrainData terrain{};
    std::vector<sm::TreePoint> trees;
    sm::TreeLayer treeLayer;
    sm::DepositLayer deposits;
    sm::FeatureLayer features;
    sm::ZoneLayer zones;
    sm::TreeGrid treeGrid;
    sm::PathCostData pathCost;
    auto store = sm::make_macro_store();

    sm::MacroNpcAiRuntime ai;
    sm::reset_macro_npc_ai_runtime(ai, seed);

    sm::WorldGenParams gp{};
    gp.seed = seed;
    sm::WorldGenOut go{};
    go.gs = &gs;
    go.terrain = &terrain;
    go.trees = &trees;
    go.treeLayer = &treeLayer;
    go.deposits = &deposits;
    go.features = &features;
    go.zones = &zones;
    go.treeGrid = &treeGrid;
    go.squadIndex = &ai.squadIndex;
    go.pathCost = &pathCost;
    go.store = store.get();
    sm::generate_macro_world(go, gp);

    const int side = features.width > 0 ? features.width : terrain.width;
    std::size_t roadCells = 0;
    for (std::size_t i = 0; i < features.data.size(); ++i)
        if (is_road_byte(features.data[i])) ++roadCells;

    const std::vector<std::int32_t> dist = road_distance_field(features, side);

    std::vector<PlaceReach> places;
    for (std::size_t s = 0; s < sm::kUnifiedCap; ++s) {
        if (!store->alive[s]) continue;
        const auto type = sm::SquadType(store->runtime[s].squadType);
        if (sm::squad_archetype(type) != sm::SquadArchetype::Place) continue;
        PlaceReach pr;
        pr.type = store->runtime[s].squadType;
        pr.cell = store->cell[s].idx;
        pr.dist = (pr.cell < dist.size()) ? dist[pr.cell] : -1;
        places.push_back(pr);
    }

    // Распределение разрыва. Гистограмма, а не среднее: среднее по разрывам
    // спрятало бы ровно тот хвост, о котором доложил владелец.
    int onCell = 0, adjacent = 0, far = 0, unreached = 0, worst = 0;
    std::uint32_t worstCell = 0;
    std::uint8_t worstType = 0;
    for (const PlaceReach& p : places) {
        if (p.dist < 0) { ++unreached; continue; }
        if (p.dist == 0) ++onCell;
        else if (p.dist == 1) ++adjacent;
        else ++far;
        if (p.dist > worst) { worst = p.dist; worstCell = p.cell; worstType = p.type; }
    }

    std::printf("[road-reach] seed=%u side=%d roadCells=%zu places=%zu\n",
                seed, side, roadCells, places.size());
    std::printf("             дорога НА клетке места: %d · в соседней: %d · "
                "дальше: %d · не достигнуто вовсе: %d\n",
                onCell, adjacent, far, unreached);
    std::printf("             худший разрыв: %d клеток (род %u, клетка %u = %d,%d)\n",
                worst, unsigned(worstType), worstCell,
                sm::cell_x(worstCell, side), sm::cell_y(worstCell, side));

    // Раздельно по роду: город и деревня — разные потребители дорог, и
    // смешивать их в одном числе значит потерять ответ.
    for (int t = 1; t <= 8; ++t) {
        int cnt = 0, sum = 0, mx = 0, miss = 0;
        for (const PlaceReach& p : places) {
            if (p.type != std::uint8_t(t)) continue;
            ++cnt;
            if (p.dist < 0) { ++miss; continue; }
            sum += p.dist;
            mx = std::max(mx, p.dist);
        }
        if (cnt == 0) continue;
        const int reached = cnt - miss;
        std::printf("             род %d: мест %d · средний разрыв %.2f · "
                    "худший %d · без дороги %d\n",
                    t, cnt, reached > 0 ? double(sum) / double(reached) : 0.0,
                    mx, miss);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::uint32_t> seeds;
    for (int i = 1; i < argc; ++i)
        seeds.push_back(std::uint32_t(std::strtoul(argv[i], nullptr, 10)));
    if (seeds.empty()) seeds = {12345u, 7u, 2026u};

    std::printf("ПЕРЕПИСЬ ДОСТУПНОСТИ МЕСТ ДОРОГОЙ (bugs.md Б3). "
                "Прибор ОТЧИТЫВАЕТСЯ, сюита НЕ судит: порога не выведено.\n");
    for (std::uint32_t s : seeds) census_seed(s);
    return 0;
}
