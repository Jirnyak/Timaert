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
// ── ВТОРАЯ ПОЛОВИНА ПРИБОРА: КТО ВСТАЁТ НА КЛЕТКЕ ОСОБОЙ ФИЧИ (M-391) ─────
//
// Первая половина мерит ЗЕМЛЮ и спрашивает «какого ТИРА тварь водится в клетке
// такой опасности». У наряда M-391 вопрос другой и сечение другое: «КТО встаёт
// в шпиле и в руине», то есть распределение по РОДАМ на клетках, чью полосу
// спавна объявляет МОДУЛЬ (M-371), а не биом. Среднего тира тут не хватает по
// построению — он не отличает мир, где в башне стоят демоны, от мира, где там
// стоят горные козлы той же ступени.
//
// ТРИ СЦЕНЫ, И ОНИ РАЗНЫЕ ПО ПРИРОДЕ: улица шпиля и улица руины (полоса из
// `feature_fauna_band@src/sub/gens/dispatch.cpp` — дверь МОДУЛЯ байта фичи) и
// ЯРУСЫ башни (полоса из `dungeon_kind_row@src/sub/dgn/dispatch.cpp` — дверь
// рода норы). Прибор спрашивает ровно эти двери, а не пересказывает их числа.
//
// КАП ЗАПАСОМ ПРИБОР ПРИМЕНЯЕТ, И ЭТО НЕ КОПИЯ ЗАКОНА, А ЕГО ОДНА СТРОКА:
// оба спавнера обрывают укладку по `if (placed >= budget) break`
// (`spawn_cell_npcs`/`spawn_dungeon_vermin@src/sub/spawn.cpp`), запас берётся
// пультом полей (`resource_field_read`, ряд Fauna). Без капа прибор мерил бы
// ПРЕДЛОЖЕНИЕ ролла, а вопрос наряда — что ВСТАЁТ.
//
// ЗАПУСК: `./build/fauna_spread_census [сид ...]` (по умолчанию три сида).

#include "core/caps.h"
#include <memory>
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
#include "macro/macro_world.h"
#include "macro/resource_field.h"
#include "macro/spires.h"       // spire_orb — тир шпиля, он же число ярусов
#include "bus/store.h"
#include "sub/gens/dispatch.h"  // feature_fauna_band — полоса модуля байта фичи
#include "sub/dgn/dispatch.h"   // dungeon_kind_row — полоса рода норы
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

// ── ВТОРАЯ ПОЛОВИНА: ГИСТОГРАММА ПО РОДАМ ОДНОЙ СЦЕНЫ (M-391) ────────────
// Счётчик на СТРОКУ каталога, не на ступень: вопрос «кто встал», а ступень на
// него не отвечает (демон и козёл бывают одного тира).
struct Scene {
    std::uint64_t scenes = 0;   // сцен опрошено
    std::uint64_t stock  = 0;   // сумма запасов ряда Fauna (кап голов)
    std::uint64_t heads  = 0;   // голов ВСТАЛО (после капа запасом)
    std::uint64_t hist[std::size_t(sm::NPCType::Count)] = {};
};

// Один визит одной сцены: продакшен-ролл, потом ровно та одна строка капа,
// которой оба спавнера обрывают укладку.
void visit_scene(Scene& sc, const sm::SpawnContext& ctx, int budget,
                 std::uint32_t seed) {
    ++sc.scenes;
    sc.stock += std::uint64_t(std::max(0, budget));
    if (budget <= 0) return;
    std::uint32_t rs = seed;
    const std::vector<sm::FaunaPick> picks = sm::roll_spawns(ctx, rs);
    int placed = 0;
    for (const sm::FaunaPick& pk : picks) {
        if (placed >= budget) break;
        ++sc.hist[std::size_t(pk.entry->row)];
        ++sc.heads;
        ++placed;
    }
}

void print_scene(const char* title, const Scene& sc, const sm::FaunaBand& band) {
    int kinds = 0;
    for (std::uint64_t h : sc.hist) kinds += h != 0 ? 1 : 0;
    std::printf("               %-28s полоса bit=0x%04X %u..%u · сцен %llu · "
                "запас %llu · голов %llu · родов %d\n",
                title, unsigned(band.bit), unsigned(band.minCount),
                unsigned(band.maxCount), (unsigned long long)sc.scenes,
                (unsigned long long)sc.stock, (unsigned long long)sc.heads,
                kinds);
    // Поимённо, по убыванию — иначе «родов 7» не говорит, КАКИЕ семеро.
    std::size_t order[std::size_t(sm::NPCType::Count)];
    for (std::size_t i = 0; i < std::size_t(sm::NPCType::Count); ++i) order[i] = i;
    std::sort(std::begin(order), std::end(order),
              [&](std::size_t a, std::size_t b) {
                  return sc.hist[a] > sc.hist[b];
              });
    std::printf("                 ");
    int shown = 0;
    for (std::size_t i : order) {
        if (sc.hist[i] == 0) break;
        std::printf("%s%s %llu", shown ? " · " : "",
                    sm::kNpcTypeDefs[i].id, (unsigned long long)sc.hist[i]);
        if (++shown == 10) { std::printf(" · …"); break; }
    }
    if (shown == 0) std::printf("(никого)");
    std::printf("\n");
    // ЛИЧ — ОТДЕЛЬНЫМ ЧИСЛОМ, потому что это ОТДЕЛЬНЫЙ вопрос: его ареал есть
    // `kHabSpire` ЕДИНСТВЕННЫМ, то есть он единственная строка каталога, у
    // которой ДРУГОЙ земли нет вовсе (перепись M-391: 14 строк носят бит, 13
    // достижимы иначе). «Родов стало больше» его появления не доказывает.
    std::printf("                 ЛИЧ: %llu\n",
                (unsigned long long)sc.hist[std::size_t(sm::NPCType::Lich)]);
}

void census_seed(std::uint32_t seed) {
    std::unique_ptr<sm::GameState> gsOwn = sm::make_game_state();
    sm::GameState& gs = *gsOwn;
    sm::TerrainData terrain{};
    std::vector<sm::TreePoint> trees;
    sm::TreeLayer& treeLayer = gs.treeLayer;
    std::unique_ptr<sm::DepositLayer> depositsOwn{new sm::DepositLayer()};
    sm::DepositLayer& deposits = *depositsOwn;
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
            c < treeLayer.cell_count() ? int(treeLayer.data[c]) : 0;

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

    // ── КТО ВСТАЁТ НА КЛЕТКЕ ОСОБОЙ ФИЧИ (M-391) ─────────────────────────
    // Конверт слоёв — тот же, которым мир отвечает в игре: запас ряда Fauna
    // спрашивается ПУЛЬТОМ (`resource_field_read`), не гридом напрямую.
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.store = store.get();
    mw.terrain = &terrain;
    mw.deposits = &deposits;
    mw.features = &features;
    mw.zones = &zones;
    mw.pathCost = &pathCost;
    mw.treeGrid = &treeGrid;
    mw.squads = &ai.squadIndex;

    const sm::FaunaBand spireBand =
        sm::sub::feature_fauna_band(std::uint8_t(sm::FT_Spire));
    const sm::FaunaBand ruinBand =
        sm::sub::feature_fauna_band(std::uint8_t(sm::FT_Ruin));
    const sm::FaunaBand towerDen =
        sm::sub::dungeon_kind_row(sm::sub::DungeonRef::SpireTower).den;
    Scene spireStreet, spireTower, ruinStreet;
    int spireCells = 0, ruinCells = 0;

    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const sm::FeatureType ft = sm::feature_decode(features.at(x, y));
            if (ft != sm::FT_Spire && ft != sm::FT_Ruin) continue;
            const std::size_t c =
                std::size_t(y) * std::size_t(side) + std::size_t(x);
            const int treeCount =
                c < treeLayer.cell_count() ? int(treeLayer.data[c]) : 0;
            const int budget =
                sm::resource_field_read(mw, sm::ResourceFieldId::Fauna, x, y);
            sm::SpawnContext ctx{};
            ctx.biome = sm::biome_at_cell(terrain, std::uint32_t(c));
            ctx.forest = sm::is_forest_cell(treeCount);
            ctx.danger = c < zones.data.size() ? zones.data[c]
                                               : std::uint8_t(0);
            // Поток РНГ есть факт (сид, клетка) — тот же закон, что у ролла.
            const std::uint32_t cellSalt = std::uint32_t(c) * 2654435761u ^ seed;
            if (ft == sm::FT_Ruin) {
                ++ruinCells;
                ctx.band = ruinBand;
                visit_scene(ruinStreet, ctx, budget, cellSalt ^ 0xFAEAu);
                continue;
            }
            ++spireCells;
            ctx.band = spireBand;
            visit_scene(spireStreet, ctx, budget, cellSalt ^ 0xFAEAu);
            // ЯРУСЫ БАШНИ: их столько, каков ТИР шпиля (`ordinal` люка несёт
            // тир — `gen_spire`, `dungeon_spire_tower_floors`), и каждый ярус
            // есть отдельный визит норы со своим семенем сцены.
            sm::sub::DungeonRef ref{};
            ref.kind = sm::sub::DungeonRef::SpireTower;
            ref.ordinal = std::uint16_t(std::clamp(
                int(sm::spire_orb(mw, x, y).tier), 1, 5));
            const int storeys = sm::sub::dungeon_spire_tower_floors(ref);
            sm::SpawnContext tctx = ctx;
            tctx.band = towerDen;
            for (int lvl = 0; lvl < storeys; ++lvl) {
                visit_scene(spireTower, tctx, budget,
                            cellSalt ^ 0xCE11A5u ^ std::uint32_t(lvl * 7919));
            }
        }
    }
    std::printf("               ─── ЧТО ВСТАЁТ НА ОСОБОЙ ФИЧЕ (M-391): "
                "шпилей %d · руин %d ───\n", spireCells, ruinCells);
    print_scene("ШПИЛЬ, улица", spireStreet, spireBand);
    print_scene("ШПИЛЬ, ярусы башни", spireTower, towerDen);
    print_scene("РУИНА, улица (эталон)", ruinStreet, ruinBand);
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
