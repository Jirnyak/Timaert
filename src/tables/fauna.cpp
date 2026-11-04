// ТЕЛО ЗАКОНА ДИКОГО РОЛЛА — чистая арифметика над строками каталога.
//
// ПЕРЕЕХАЛО ИЗ `macro/fauna.cpp` ЛОМТЁМ 3 ЭПИКА ПАКЕТОВ v1 вместе со своим
// заголовком: ни одна строка здесь не трогает состояние мира — ни слоя, ни
// `GameState`, ни стора, — а спрашивают закон ОБА мира. `.cpp` в нейтральном
// слое законен и не нов (`tables/items.cpp` — прецедент): «таблица
// ЧИТАЕТСЯ, и только» запрещает состояние, а не единицу трансляции.
//
// Что НЕ переехало и не могло: `fauna_cell_capacity_at` (ей нужен конверт
// слоёв мира) и виды над каталогом существ — они остались в `macro/fauna.cpp`.
#include "tables/fauna.h"
#include "tables/faction.h"   // faction_index — имя → ординал НА ГРАНИЦЕ ролла
#include "tables/forest.h"    // is_forest_cell — класс леса по счёту деревьев
#include "core/rng.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sm {
namespace {

// ── АРЕАЛ ПЕРЕЕХАЛ В СТРОКУ (CANON S26 «Одна строка на род») ────────────
// Здесь стояла ШЕСТАЯ и последняя таблица-спутник `kSpawnHabitats` с тремя
// колонками (маска ареала, гейт жилы, дикое знамя). Она была хуже прочих
// одним: жила в .cpp, то есть из самой строки существа была НЕВИДИМА, и
// «где этот род водится» приходилось искать во втором файле — при том что
// её собственная шапка объявляла «the table IS the system». Влита
// 2026-09-22 в NpcTypeDef колонками `habitat` (`depositGate` умер 2026-10-23
// сиротой) / `wildFaction`; биты kHab* переехали туда же (tables/npc.h), и
// заголовок цитирует их теперь как читатель. Свидетель порядка ушёл с
// таблицей: колонке строки не нужен свидетель порядка, она И ЕСТЬ строка.
// Транскрипция сверена машиной — 46 рядов × 3 колонки, ноль расхождений.

// How many heads a place rolls — one row per biome plus the derived-class
// overrides, holding the old thirteen tables' min/max counts verbatim.
struct SpawnCounts { std::uint8_t minCount, maxCount; };
// Row-per-biome with the enum as a COLUMN — the guard below is what makes a
// grown Biome a compile error rather than a silently empty meadow.
struct BiomeSpawnCountRow { Biome biome; SpawnCounts counts; };
constexpr BiomeSpawnCountRow kBiomeSpawnCounts[std::size_t(Mountain) + 1] = {
    {Tundra,  {1, 4}}, {Taiga,   {2, 5}}, {Snow,     {1, 3}}, {Valley, {2, 6}},
    {Meadow,  {2, 6}}, {Swamp,   {2, 6}}, {Desert,   {1, 4}}, {Steppe, {2, 5}},
    // ВОДА СТАЛА РОЛЛИТЬ ГОЛОВЫ (M-382, вердикт владельца 2026-11-04 «в воде
    // пусть живут акулы какулы и морские змеи … кальмары всякие киты»). Здесь
    // стоял `{0, 0}`, и это была ВТОРАЯ половина дыры воды: ряд обвинял только
    // пустой каталог, но даже с пятью новыми строками `hab(Water)` мир не
    // менялся НИ НА ГОЛОВУ — потому что счёт голов нулевой, и бит ареала воды
    // не спрашивался ни разу. Два замка на одной двери, и открывать надо ОБА.
    // Число 1..4 — калибровка, а не вывод: та же разреженная ступень, что у
    // пустыни, тундры и горы, потому что открытая вода пуста НА КЛЕТКУ, а
    // берёт она охватом — 21 % мира.
    {Tropics, {2, 6}}, {Water,   {1, 4}}, {Mountain, {1, 4}},
};
static_assert(rows_in_enum_order(kBiomeSpawnCounts, &BiomeSpawnCountRow::biome),
              "kBiomeSpawnCounts row order must mirror Biome");
constexpr SpawnCounts kForestSpawnCounts{3, 8};

// МОДУЛЬНАЯ ПОЛОСА ПОБЕЖДАЕТ, ЗЕМЛЯ ОТВЕЧАЕТ ПО УМОЛЧАНИЮ (M-371): счёт
// голов за визит объявляет band модуля клетки (нора данжа, фича субмира);
// band.maxCount 0 = лес/биом. Колонки рода места мертвы вместе с place_def.
SpawnCounts spawn_counts(const SpawnContext& ctx) {
    if (ctx.band.maxCount != 0)
        return {ctx.band.minCount, ctx.band.maxCount};
    if (ctx.forest) return kForestSpawnCounts;
    const auto b = std::size_t(ctx.biome);
    return b <= std::size_t(Mountain) ? kBiomeSpawnCounts[b].counts
                                      : SpawnCounts{0, 0};
}

// The habitat bit a context asks for — the module's band when it states one
// (a den, a ruin's ground), else the ground's own forest/biome bit.
std::uint16_t context_bit(const SpawnContext& ctx) {
    if (ctx.band.bit != 0) return ctx.band.bit;
    if (ctx.forest) return kHabForest;
    return hab(ctx.biome);
}

} // namespace

// (`spawn_strength` — нормированный по каталогу `log₂(hp × dps)` — жила здесь
//  до 2026-11-04 и уничтожена нарядом M-387 вместе с `row_power`. Полный
//  некролог стоит в `tables/fauna.h`, у места объявления: там и второй ответ
//  на вопрос «насколько тварь высоко» (DOD п.6), и плавающая нормировка,
//  из-за которой одна новая строка сдвигала расселение всего мира.)

std::uint32_t danger_match_weight(std::uint8_t rung,
                                  std::uint8_t danger) {
    const int miss = std::abs(int(rung) - int(danger));
    const int halvings = std::min(10, miss / kDangerHalfLife);
    return std::max<std::uint32_t>(1u, 1024u >> halvings);
}

std::vector<FaunaPick> roll_spawns(const SpawnContext& ctx,
                                   std::uint32_t& rngState) {
    std::vector<FaunaPick> out;
    const SpawnCounts counts = spawn_counts(ctx);
    const std::uint16_t bit = context_bit(ctx);
    if (counts.maxCount == 0 || bit == 0) return out;

    Rng r(rngState);
    const int span = int(counts.maxCount) - int(counts.minCount) + 1;
    const int count = int(counts.minCount)
        + int(std::floor(r.next_f01() * float(span)));

    // A wild roll belongs to the LAND, and the land's answer is the species
    // row's own wildFaction — one rung, no place column above it. (M-39: the
    // registry's spawnFaction used to override this for a ruin and a spire,
    // which is how a spire's slope deer became Demonic Hordes — the very
    // garrison-vs-game ambiguity place_registry.h claimed to have killed.
    // A place's banner reaches its CROWD and its GARRISON, which are its
    // population; it does not reach the game on the hillside. The demons of a
    // ruin still roll demons because 18 of the 22 rows that a ruin or spire
    // can draw carry wildFaction="demons" themselves.)
    std::uint64_t total = 0;
    std::uint32_t w[std::size_t(NPCType::Count)] = {};
    for (std::size_t i = 0; i < std::size_t(NPCType::Count); ++i) {
        if (!(npc_def(NPCType(i)).habitat & bit)) continue;
        const NpcTypeDef& row = kNpcTypeDefs[i];
        if (row.weight == 0) continue;   // never rolled blind (the row's law)
        // СТУПЕНЬ СТРОКИ — ЕЁ АВТОРСКИЙ ТИР, ОДНОЙ ДВЕРЬЮ (M-387). Посадку на
        // лестницу делает `creature_tier` сама (`clamp_to_ladder` внутри), и
        // своего клампа здесь нет ни одного — граница лестницы у мира одна
        // (M-382). Байт прямой: тир уже 1..kMaxTier, то есть 1..255.
        w[i] = std::uint32_t(row.weight)
             * danger_match_weight(std::uint8_t(creature_tier(row.row)),
                                   ctx.danger);
        total += w[i];
    }
    if (total == 0) { rngState = r.state; return out; }

    out.reserve(std::size_t(count));
    for (int n = 0; n < count; ++n) {
        double roll = double(r.next_f01()) * double(total);
        for (std::size_t i = 0; i < std::size_t(NPCType::Count); ++i) {
            if (w[i] == 0) continue;
            roll -= double(w[i]);
            if (roll <= 0.0) {
                const NpcTypeDef& row = kNpcTypeDefs[i];
                // Имя → ординал ЗДЕСЬ, на границе: ниже, в заселении,
                // число уже готово (M-187).
                out.push_back({&row, std::uint16_t(faction_index(
                    npc_def(NPCType(i)).wildFaction))});
                break;
            }
        }
    }
    rngState = r.state;
    return out;
}

// ── The honest headcount (Session 16) ────────────────────────────────

int fauna_cell_capacity(Biome biome, int treeCount) {
    // ПРИРОДНАЯ ёмкость — счётная строка земли (лес/биом), и больше ничего
    // (M-371): колонка faunaCap рода места мертва, подавление у поселения —
    // вычистка ЕГО клетки в поле при рождении, не константа рода.
    SpawnContext ctx{};
    ctx.biome = biome;
    ctx.forest = is_forest_cell(treeCount);
    return int(spawn_counts(ctx).maxCount);
}

} // namespace sm
