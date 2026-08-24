// Faithful port of `src/game/subworld/fauna.ts`.
//   - 1:1 stat lines / weights / colours / counts.
//   - factionId is the registry id string (macro/faction.h) verbatim.
//   - String AI       → the ONE `AIBehaviour` column (macro/behaviour.h).
//   - Tables stored as null-terminated arrays of `const FaunaEntry*` so
//     they live in `.rodata` and never allocate.
#include "macro/fauna.h"
#include "macro/deposit_layer.h"
#include "macro/landmark_grid.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {


// The creature rows live in THE one body table (macro/npc.h). They used to be
// nineteen `static const FaunaEntry` objects right here — a second table of
// bodies, addressed by a second id encoding. What is left is nineteen names
// for rows of the first one, so the spawn tables below still read like a
// bestiary while there is only one bestiary in the program.
static const FaunaEntry& kRabbit = kNpcTypeDefs[std::size_t(NPCType::Rabbit)];
static const FaunaEntry& kDeer = kNpcTypeDefs[std::size_t(NPCType::Deer)];
static const FaunaEntry& kFox = kNpcTypeDefs[std::size_t(NPCType::Fox)];
static const FaunaEntry& kWolf = kNpcTypeDefs[std::size_t(NPCType::Wolf)];
static const FaunaEntry& kBear = kNpcTypeDefs[std::size_t(NPCType::Bear)];
static const FaunaEntry& kBoar = kNpcTypeDefs[std::size_t(NPCType::Boar)];
static const FaunaEntry& kSnake = kNpcTypeDefs[std::size_t(NPCType::Snake)];
static const FaunaEntry& kHawk = kNpcTypeDefs[std::size_t(NPCType::Hawk)];
static const FaunaEntry& kFrog = kNpcTypeDefs[std::size_t(NPCType::Frog)];
static const FaunaEntry& kGoat = kNpcTypeDefs[std::size_t(NPCType::Goat)];
static const FaunaEntry& kEagle = kNpcTypeDefs[std::size_t(NPCType::Eagle)];
static const FaunaEntry& kCroc = kNpcTypeDefs[std::size_t(NPCType::Croc)];
static const FaunaEntry& kGoblin = kNpcTypeDefs[std::size_t(NPCType::Goblin)];
static const FaunaEntry& kSkeleton = kNpcTypeDefs[std::size_t(NPCType::Skeleton)];
static const FaunaEntry& kTroll = kNpcTypeDefs[std::size_t(NPCType::Troll)];
static const FaunaEntry& kSwampThing = kNpcTypeDefs[std::size_t(NPCType::SwampThing)];
static const FaunaEntry& kIceWraith = kNpcTypeDefs[std::size_t(NPCType::IceWraith)];
static const FaunaEntry& kSandScorpion = kNpcTypeDefs[std::size_t(NPCType::SandScorpion)];
static const FaunaEntry& kStoneGolem = kNpcTypeDefs[std::size_t(NPCType::StoneGolem)];

namespace {

// ── THE spawn law (see fauna.h) ──────────────────────────────────────
//
// The habitat COLUMN of the one body table, enum-ordered beside the law
// that reads it (the kNpcPurse / kGathererDefs idiom). Membership is the
// thirteen old list-tables, verbatim, folded into bits — the lists and the
// switch ladder that chose between them are gone (canon-audit F5).
struct SpawnHabitatRow {
    NPCType       type;
    std::uint16_t mask;
    // A profession stands in the crowd only where its ground does: the
    // DepositKind whose live vein within reach opens this row (-1 = no gate).
    // The same pairing the macro gatherer table works by (kGathererDefs).
    std::int8_t   depositGate = -1;
};
constexpr SpawnHabitatRow kSpawnHabitats[std::size_t(NPCType::Count)] = {
    {NPCType::Peasant,      kHabTown},
    {NPCType::Woodcutter,   kHabTown},
    {NPCType::Merchant,     kHabTown},
    {NPCType::Caravan,      0},
    {NPCType::Bandit,       0},
    {NPCType::Guard,        kHabTown},
    {NPCType::Witch,        kHabTown},
    {NPCType::Sorceress,    0},
    {NPCType::Miner,        kHabTown, std::int8_t(DepositKind::Iron)},
    {NPCType::Quarryman,    kHabTown, std::int8_t(DepositKind::Stone)},
    {NPCType::ClayDigger,   kHabTown, std::int8_t(DepositKind::Clay)},
    {NPCType::Rabbit,       hab(Meadow) | hab(Valley) | hab(Steppe) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | kHabForest},
    {NPCType::Deer,         hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | hab(Taiga) | kHabForest},
    {NPCType::Fox,          hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Taiga) | hab(Tundra) | kHabForest},
    {NPCType::Wolf,         hab(Meadow) | hab(Valley) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | hab(Mountain)
                          | kHabForest},
    {NPCType::Bear,         hab(Taiga) | kHabForest},
    {NPCType::Boar,         hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | kHabForest},
    {NPCType::Snake,        hab(Desert) | hab(Steppe) | hab(Swamp)
                          | hab(Tropics) | kHabRuin},
    {NPCType::Hawk,         hab(Meadow) | hab(Valley) | hab(Desert)
                          | hab(Steppe)},
    {NPCType::Frog,         hab(Swamp)},
    {NPCType::Goat,         hab(Mountain)},
    {NPCType::Eagle,        hab(Mountain)},
    {NPCType::Croc,         hab(Swamp) | hab(Tropics)},
    {NPCType::Goblin,       kHabForest | kHabRuin | kHabSpire},
    {NPCType::Skeleton,     kHabRuin | kHabSpire},
    {NPCType::Troll,        kHabRuin | kHabSpire},
    {NPCType::SwampThing,   hab(Swamp)},
    {NPCType::IceWraith,    hab(Tundra) | hab(Snow) | kHabSpire},
    {NPCType::SandScorpion, hab(Desert)},
    {NPCType::StoneGolem,   hab(Mountain) | kHabSpire},
};
static_assert(rows_in_enum_order(kSpawnHabitats, &SpawnHabitatRow::type),
              "every body row states its ground — the table IS the system");

// How many heads a place rolls — one row per biome plus the derived-class
// overrides, holding the old thirteen tables' min/max counts verbatim.
struct SpawnCounts { std::uint8_t minCount, maxCount; };
constexpr SpawnCounts kBiomeSpawnCounts[std::size_t(Mountain) + 1] = {
    /*Tundra*/ {1, 4}, /*Taiga*/ {2, 5}, /*Snow*/ {1, 3}, /*Valley*/ {2, 6},
    /*Meadow*/ {2, 6}, /*Swamp*/ {2, 6}, /*Desert*/ {1, 4}, /*Steppe*/ {2, 5},
    /*Tropics*/ {2, 6}, /*Water*/ {0, 0}, /*Mountain*/ {1, 4},
};
constexpr SpawnCounts kForestSpawnCounts{3, 8};
constexpr SpawnCounts kRuinSpawnCounts{2, 6};
constexpr SpawnCounts kSpireSpawnCounts{4, 9};

const SpawnCounts& spawn_counts(const SpawnContext& ctx) {
    switch (ctx.landmark) {
        case LandmarkType::Ruin:  return kRuinSpawnCounts;
        case LandmarkType::Spire: return kSpireSpawnCounts;
        default: break;
    }
    if (ctx.forest) return kForestSpawnCounts;
    const auto b = std::size_t(ctx.biome);
    static constexpr SpawnCounts kNone{0, 0};
    return b <= std::size_t(Mountain) ? kBiomeSpawnCounts[b] : kNone;
}

// The habitat bit a context asks for. A town rolls no wild fauna at all
// (its crowd is the kHabTown stripe, rolled by the settlement spawner).
std::uint16_t context_bit(const SpawnContext& ctx) {
    switch (ctx.landmark) {
        case LandmarkType::City:
        case LandmarkType::Village: return 0;
        case LandmarkType::Ruin:    return kHabRuin;
        case LandmarkType::Spire:   return kHabSpire;
        default: break;
    }
    if (ctx.forest) return kHabForest;
    return hab(ctx.biome);
}

} // namespace

std::uint8_t spawn_strength(NPCType t) {
    // Normalized once over the table itself: L = log₂(hp × damage/cooldown),
    // weakest row → 0, strongest → 255. The derivation is the point — there
    // is no strength column to drift from the numbers that actually fight.
    static const std::array<std::uint8_t, std::size_t(NPCType::Count)> table =
        [] {
            std::array<double, std::size_t(NPCType::Count)> L{};
            double lo = 1e30, hi = -1e30;
            for (std::size_t i = 0; i < L.size(); ++i) {
                const CombatTemplate& c = kNpcTypeDefs[i].combat;
                const double dps =
                    double(c.damage) / std::max(0.25, double(c.cooldown));
                const double power = std::max(1.0, double(c.hp) * dps);
                L[i] = std::log2(power);
                lo = std::min(lo, L[i]);
                hi = std::max(hi, L[i]);
            }
            std::array<std::uint8_t, std::size_t(NPCType::Count)> out{};
            const double span = std::max(1e-6, hi - lo);
            for (std::size_t i = 0; i < out.size(); ++i) {
                out[i] = std::uint8_t(
                    std::lround(255.0 * (L[i] - lo) / span));
            }
            return out;
        }();
    const auto i = std::size_t(t);
    return i < table.size() ? table[i] : 0;
}

std::uint32_t danger_match_weight(std::uint8_t strength,
                                  std::uint8_t danger) {
    const int miss = std::abs(int(strength) - int(danger));
    const int halvings = std::min(10, miss / kDangerHalfLife);
    return std::max<std::uint32_t>(1u, 1024u >> halvings);
}

std::vector<FaunaPick> roll_spawns(const SpawnContext& ctx,
                                   std::uint32_t& rngState) {
    std::vector<FaunaPick> out;
    const SpawnCounts& counts = spawn_counts(ctx);
    const std::uint16_t bit = context_bit(ctx);
    if (counts.maxCount == 0 || bit == 0) return out;

    Rng r(rngState);
    const int span = int(counts.maxCount) - int(counts.minCount) + 1;
    const int count = int(counts.minCount)
        + int(std::floor(r.next_f01() * float(span)));

    // The landmark's own creatures wear its colours (spawnFaction column of
    // the place registry — a ruin's wolves ARE demons); open land keeps each
    // row's own faction.
    const char* placeFaction = landmark_def(ctx.landmark).spawnFaction;

    std::uint64_t total = 0;
    std::uint32_t w[std::size_t(NPCType::Count)] = {};
    for (std::size_t i = 0; i < std::size_t(NPCType::Count); ++i) {
        if (!(kSpawnHabitats[i].mask & bit)) continue;
        const NpcTypeDef& row = kNpcTypeDefs[i];
        if (row.weight == 0) continue;   // never rolled blind (the row's law)
        w[i] = std::uint32_t(row.weight)
             * danger_match_weight(spawn_strength(row.type), ctx.danger);
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
                out.push_back({&row, placeFaction ? placeFaction
                                                  : row.factionId});
                break;
            }
        }
    }
    rngState = r.state;
    return out;
}

NPCType pick_town_row(const SpawnContext& ctx, std::uint32_t& rngState) {
    Rng r(rngState);
    std::uint64_t total = 0;
    std::uint32_t w[std::size_t(NPCType::Count)] = {};
    for (std::size_t i = 0; i < std::size_t(NPCType::Count); ++i) {
        const SpawnHabitatRow& habRow = kSpawnHabitats[i];
        if (!(habRow.mask & kHabTown)) continue;
        const NpcTypeDef& row = kNpcTypeDefs[i];
        if (row.weight == 0) continue;
        if (habRow.depositGate >= 0
            && !(ctx.depositsNear & (1u << habRow.depositGate))) {
            continue;   // no vein in reach — this trade has no ground here
        }
        w[i] = std::uint32_t(row.weight)
             * danger_match_weight(spawn_strength(row.type), ctx.danger);
        total += w[i];
    }
    NPCType out = NPCType::Peasant;   // the crowd's fail-closed default
    if (total > 0) {
        double roll = double(r.next_f01()) * double(total);
        for (std::size_t i = 0; i < std::size_t(NPCType::Count); ++i) {
            if (w[i] == 0) continue;
            roll -= double(w[i]);
            if (roll <= 0.0) { out = NPCType(i); break; }
        }
    }
    rngState = r.state;
    return out;
}

// ── Creature catalog — a VIEW over the one body table ────────────────
//
// Flat enumeration of every distinct creature, exactly once, in the creature
// stripe's order. ECS `NPCKind.type` is the ONE table's ordinal (macro/npc.h);
// this list only serves id-string lookups and the death/loot path. Order is
// append-only: never reorder (would silently re-key live entities); add new
// creatures at the end.
static const FaunaEntry* const kCreatureCatalog[] = {
    &kRabbit, &kDeer, &kFox, &kWolf, &kBear, &kBoar, &kSnake, &kHawk,
    &kFrog, &kGoat, &kEagle, &kCroc,
    &kGoblin, &kSkeleton, &kTroll, &kSwampThing, &kIceWraith,
    &kSandScorpion, &kStoneGolem,
};

std::span<const FaunaEntry* const> creature_catalog() {
    return std::span<const FaunaEntry* const>(kCreatureCatalog,
                                              std::size(kCreatureCatalog));
}

const FaunaEntry* creature_def(std::string_view id) {
    for (const FaunaEntry* e : kCreatureCatalog) {
        if (id == e->id) return e;
    }
    return nullptr;
}

// A row's index IS its kind — it is a member of the one table, so the answer
// is where it sits in that table. This used to be a linear search through a
// second array, which is what having a second array costs.
int creature_index(const FaunaEntry* entry) {
    if (!entry) return -1;
    const std::size_t i = std::size_t(entry - &kNpcTypeDefs[0]);
    return i < std::size_t(NPCType::Count) ? int(i) : -1;
}

const FaunaEntry* creature_def_from_kind(std::uint16_t kindType) {
    if (!valid_npc_kind(kindType)) return nullptr;      // names no row at all
    if (!is_creature_row(NPCType(kindType))) return nullptr; // a man, not a beast
    // The kind IS the row. There is no index to mask and no second array to
    // index into — that masking (`kindType & 0xFF` into the catalog) was the
    // old encoding's last hiding place, and it silently answered with the
    // WRONG creature the moment the catalog stopped being the id space.
    return &kNpcTypeDefs[std::size_t(kindType)];
}

// ── The honest headcount (Session 16) ────────────────────────────────

int fauna_cell_capacity(Biome biome, int treeCount,
                        LandmarkType landmark) {
    // A town is the poorest hunting ground that still is one (the old ruin
    // table's minCount); everywhere else the place's counts row caps it.
    if (landmark == LandmarkType::City || landmark == LandmarkType::Village) {
        return int(kRuinSpawnCounts.minCount);
    }
    SpawnContext ctx{};
    ctx.biome = biome;
    ctx.forest = is_forest_cell(treeCount);
    ctx.landmark = landmark;
    return int(spawn_counts(ctx).maxCount);
}

// (A hand-written City/Village/Spire scan named `landmark_kind_at` lived here
// until 2026-08-24 — the drifted second implementation of "what stands on
// this cell". The baked index answers now, in the same priority order.)

int fauna_cell_capacity_at(const MacroWorld& w, int x, int y) {
    const TerrainData* terrain = w.terrain;
    if (!terrain || !terrain->has_rgba_storage()
        || terrain->width <= 0 || terrain->height <= 0) {
        return 0;
    }
    const int wx = FeatureLayer::wrap_coord(x, terrain->width);
    const int wy = FeatureLayer::wrap_coord(y, terrain->height);
    // THE cell cascade (map_generator.h biome_at_cell). This function used to
    // classify by float threshold while the subworld read the mask — the same
    // coastal cell fed a wolf and refused a boot (canon-audit C5).
    const Biome biome = biome_at_cell(*terrain, wx, wy);
    const int treeCount = (w.trees && w.trees->has_complete_storage())
        ? int(w.trees->at(wx, wy)) : 0;
    const LandmarkType landmark =
        w.landmarks ? w.landmarks->at(wx, wy).type : LandmarkType::None;
    return fauna_cell_capacity(biome, treeCount, landmark);
}

} // namespace sm
