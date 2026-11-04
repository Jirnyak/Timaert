// ПЕРЕПИСЬ РАССЕЛЕНИЯ ФАУНЫ ПО ПОЛОСАМ ОПАСНОСТИ — ПРИБОР ПОД M-387, А НЕ
// СВИДЕТЕЛЬ.
//
// ЗАЧЕМ. Наряд M-387 сводит отбор строки на ТИР (`creature_tier@src/tables/npc.h`)
// вместо выведенной мощи, и приёмка у него — ПЕРЕЗАМЕР распределения на трёх
// сидах ДО и ПОСЛЕ. Такого замера не делал ни один живой прибор: сюита
// `subworld_spawn_parity_test` судит ОДНУ клетку луга на синтетическом
// контексте, то есть ни настоящего поля опасности, ни настоящих биомов, ни
// ареалов реального мира не видит вовсе. Поэтому вопрос «какого тира тварь
// водится в клетке такой-то опасности» жил без числа, и спорить о сдвиге
// расселения было нечем.
//
// ЧТО ИМЕННО МЕРЯЕТСЯ, ЧЕСТНО. Мир поднимается ТОЙ ЖЕ дверью
// `generate_macro_world`, что и игра; затем КАЖДАЯ клетка тора прогоняется
// через ПРОДАКШЕН-дверь `roll_spawns@src/tables/fauna.cpp` со своим контекстом
// (биом из поля биомов, лес из счёта деревьев, опасность из `ZoneLayer`), и то,
// что дверь вернула, складывается в гистограмму ТИРОВ по полосам опасности.
// Прибор НЕ переписывает закон отбора: он спрашивает ту же функцию, которую
// зовёт мир, и тем измеряет мир, а не свою копию формулы (§8 п.5).
//
// ПОЛОС ВОСЕМЬ ПО ТРИДЦАТЬ ДВА БАЙТА, и это не число с потолка: 256 / 8 = 32
// есть РОВНО `kDangerHalfLife + 7`, то есть полоса шире одного ополовинивания,
// поэтому соседние полосы обязаны различаться средним тиром, если закон
// работает вообще. Полосы здесь — ПОКАЗ замера, и ни одна механика на них не
// ветвится (S12: «no mechanic may branch on a band»).
//
// ПОЧЕМУ ТИР, А НЕ «СИЛА». Мерить выведенную мощь нельзя по построению: она и
// есть то, что наряд сносит, и прибор, зовущий `spawn_strength`, перестал бы
// компилироваться ПОСЛЕ правки — то есть не смог бы дать вторую половину
// замера. Тир жив по обе стороны правки, значит ДО и ПОСЛЕ сравнимы ОДНИМ
// числом, снятым ОДНИМ прибором. Это и есть «прибор читать по имени колонки»
// (§5 п.7) в своей сильной форме.
//
// СОЗНАТЕЛЬНО НЕ ctest, по причине `balance_run` и прочих переписей: это ЗАМЕР,
// А НЕ ВЕРДИКТ. Порога «какой средний тир допустим в полосе» никто не выводил,
// и выдумывать его здесь значило бы поставить число с потолка (ЗАКОН КОНСТАНТ)
// и получить свидетеля, охраняющего случай (§8 п.5).
//
// ЗАПУСК: `./build/fauna_spread_census [сид ...]` (по умолчанию три сида).

#include "core/caps.h"
#include "core/torus.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "macro/deposit_layer.h"
#include "macro/world_gen.h"
#include "macro/zones.h"
#include "bus/store.h"
#include "tables/fauna.h"
#include "tables/forest.h"
#include "tables/npc.h"
#include "tables/progression.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// Полос показа — восемь по 32 байта опасности (вывод в шапке).
constexpr int kBands = 8;
constexpr int kBandWidth = 256 / kBands;

// Гистограмма тиров полосы: один счётчик на ступень лестницы. Точные
// перцентили, а не оценка по среднему — среднее спрятало бы ровно тот хвост,
// в котором и живёт ответ «переехала ли тварь».
struct Band {
    std::uint64_t cells = 0;
    std::uint64_t heads = 0;
    std::uint64_t tierSum = 0;
    std::uint64_t hist[std::size_t(sm::kMaxTier) + 1] = {};
};

int percentile(const Band& b, double q) {
    if (b.heads == 0) return -1;
    const std::uint64_t want = std::uint64_t(double(b.heads) * q);
    std::uint64_t seen = 0;
    for (std::size_t t = 0; t <= std::size_t(sm::kMaxTier); ++t) {
        seen += b.hist[t];
        if (seen > want) return int(t);
    }
    return int(sm::kMaxTier);
}

int max_tier(const Band& b) {
    for (std::size_t t = std::size_t(sm::kMaxTier) + 1; t-- > 0;)
        if (b.hist[t] != 0) return int(t);
    return -1;
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
    const std::size_t n = std::size_t(side) * std::size_t(side);

    Band band[kBands];
    Band all;
    std::uint64_t emptyCells = 0;

    for (std::size_t c = 0; c < n; ++c) {
        const std::uint8_t danger =
            c < zones.data.size() ? zones.data[c] : std::uint8_t(0);
        const int treeCount =
            c < treeLayer.data.size() ? int(treeLayer.data[c]) : 0;

        sm::SpawnContext ctx{};
        ctx.biome = sm::biome_at_cell(terrain, std::uint32_t(c));
        ctx.forest = sm::is_forest_cell(treeCount);
        ctx.danger = danger;

        // Поток РНГ есть факт (сид, клетка) — тот же закон, что у ролла
        // («the roll is a fact of (seed, context) alone»), поэтому прогон
        // воспроизводим и сравним между ДО и ПОСЛЕ до последней головы.
        std::uint32_t rs = std::uint32_t(c) * 2654435761u ^ seed;
        const std::vector<sm::FaunaPick> picks = sm::roll_spawns(ctx, rs);

        Band& b = band[std::min(kBands - 1, int(danger) / kBandWidth)];
        ++b.cells;
        ++all.cells;
        if (picks.empty()) { ++emptyCells; continue; }
        for (const sm::FaunaPick& pk : picks) {
            // ТИР — ОДНОЙ ДВЕРЬЮ (`creature_tier`), своего клампа прибор не
            // пишет: посадка на лестницу у мира одна (M-382).
            const int tier = sm::creature_tier(pk.entry->row);
            const std::size_t t = std::size_t(std::clamp(tier, 0, sm::kMaxTier));
            ++b.heads;  b.tierSum += std::uint64_t(tier);  ++b.hist[t];
            ++all.heads; all.tierSum += std::uint64_t(tier); ++all.hist[t];
        }
    }

    std::printf("[fauna-spread] seed=%u side=%d cells=%zu heads=%llu "
                "пустых клеток=%llu\n",
                seed, side, n, (unsigned long long)all.heads,
                (unsigned long long)emptyCells);
    std::printf("               полоса опасности |   клеток |    голов | "
                "средний тир | p50 | p90 | макс\n");
    for (int i = 0; i < kBands; ++i) {
        const Band& b = band[i];
        std::printf("               %3d..%3d          | %8llu | %8llu | "
                    "%11.2f | %3d | %3d | %4d\n",
                    i * kBandWidth, (i + 1) * kBandWidth - 1,
                    (unsigned long long)b.cells, (unsigned long long)b.heads,
                    b.heads ? double(b.tierSum) / double(b.heads) : 0.0,
                    percentile(b, 0.50), percentile(b, 0.90), max_tier(b));
    }
    const double meanAll = all.heads ? double(all.tierSum) / double(all.heads)
                                     : 0.0;
    // КОНТРАСТ — ОТНОШЕНИЕ, А НЕ ПЕРЕСКАЗАННОЕ ЧИСЛО (§8 п.4): он и есть
    // ответ на «работает ли отбор вообще», и он сравним между ДО и ПОСЛЕ,
    // даже если абсолютная шкала тира сменилась под ним.
    const Band& lo = band[0];
    const Band& hi = band[kBands - 1];
    const double meanLo = lo.heads ? double(lo.tierSum) / double(lo.heads) : 0.0;
    const double meanHi = hi.heads ? double(hi.tierSum) / double(hi.heads) : 0.0;
    std::printf("               ВЕСЬ МИР: средний тир %.2f · p50 %d · p90 %d · "
                "макс %d\n",
                meanAll, percentile(all, 0.50), percentile(all, 0.90),
                max_tier(all));
    std::printf("               КОНТРАСТ полос 224..255 против 0..31: "
                "%.2f против %.2f = %+.2f тира (×%.2f)\n",
                meanHi, meanLo, meanHi - meanLo,
                meanLo > 0.0 ? meanHi / meanLo : 0.0);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::uint32_t> seeds;
    for (int i = 1; i < argc; ++i)
        seeds.push_back(std::uint32_t(std::strtoul(argv[i], nullptr, 10)));
    if (seeds.empty()) seeds = {12345u, 7u, 2026u};

    std::printf("ПЕРЕПИСЬ РАССЕЛЕНИЯ ФАУНЫ ПО ПОЛОСАМ ОПАСНОСТИ (M-387). "
                "Прибор ОТЧИТЫВАЕТСЯ, сюита НЕ судит: порога не выведено.\n");
    for (std::uint32_t s : seeds) census_seed(s);
    return 0;
}
