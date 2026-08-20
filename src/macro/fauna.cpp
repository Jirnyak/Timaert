// Faithful port of `src/game/subworld/fauna.ts`.
//   - 1:1 stat lines / weights / colours / counts.
//   - factionId is the registry id string (macro/faction.h) verbatim.
//   - String AI       → the ONE `AIBehaviour` column (macro/behaviour.h).
//   - Tables stored as null-terminated arrays of `const FaunaEntry*` so
//     they live in `.rodata` and never allocate.
#include "macro/fauna.h"
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

// ── Per-table entry arrays (null-terminated) ─────────────────────────
//
// Using `const FaunaEntry* const[]` lets each entry be referenced from
// many tables without copying. Order matches TS.

static const FaunaEntry* const kMeadow []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kHawk, &kBoar };
static const FaunaEntry* const kForest []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kBear, &kBoar, &kGoblin };
static const FaunaEntry* const kTaiga  []  = { &kRabbit, &kDeer, &kWolf, &kBear, &kFox };
static const FaunaEntry* const kTundra []  = { &kRabbit, &kWolf, &kFox, &kIceWraith };
static const FaunaEntry* const kSnow   []  = { &kRabbit, &kWolf, &kIceWraith };
static const FaunaEntry* const kDesert []  = { &kSnake, &kHawk, &kSandScorpion };
static const FaunaEntry* const kSteppe []  = { &kRabbit, &kDeer, &kFox, &kHawk, &kSnake, &kBoar };
static const FaunaEntry* const kSwamp  []  = { &kSnake, &kFrog, &kCroc, &kSwampThing };
static const FaunaEntry* const kTropics[]  = { &kSnake, &kBoar, &kCroc, &kDeer };
static const FaunaEntry* const kValley []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kHawk, &kBoar };
static const FaunaEntry* const kMountain[] = { &kGoat, &kEagle, &kWolf, &kStoneGolem };
static const FaunaEntry* const kRuin   []  = { &kSkeleton, &kGoblin, &kTroll, &kSnake };
static const FaunaEntry* const kSpire  []  = { &kSkeleton, &kIceWraith, &kTroll, &kStoneGolem, &kGoblin };

// Pointer + count + counts + override.
static constexpr FaunaTable kTblMeadow   { kMeadow,   6, 2, 6, nullptr };
static constexpr FaunaTable kTblForest   { kForest,   7, 3, 8, nullptr };
static constexpr FaunaTable kTblTaiga    { kTaiga,    5, 2, 5, nullptr };
static constexpr FaunaTable kTblTundra   { kTundra,   4, 1, 4, nullptr };
static constexpr FaunaTable kTblSnow     { kSnow,     3, 1, 3, nullptr };
static constexpr FaunaTable kTblDesert   { kDesert,   3, 1, 4, nullptr };
static constexpr FaunaTable kTblSteppe   { kSteppe,   6, 2, 5, nullptr };
static constexpr FaunaTable kTblSwamp    { kSwamp,    4, 2, 6, nullptr };
static constexpr FaunaTable kTblTropics  { kTropics,  4, 2, 6, nullptr };
static constexpr FaunaTable kTblValley   { kValley,   6, 2, 6, nullptr };
static constexpr FaunaTable kTblMountain { kMountain, 4, 1, 4, nullptr };
static constexpr FaunaTable kTblRuin     { kRuin,     4, 2, 6, "demons"  };
static constexpr FaunaTable kTblSpire    { kSpire,    5, 4, 9, "demons"  };
static constexpr FaunaTable kTblEmpty    { nullptr,   0, 0, 0, nullptr };

const FaunaTable& get_fauna_table(Biome biome, int treeCount,
                                  LandmarkKind landmark) {
    // Landmark beats everything (cities have no wild fauna; ruins / spires
    // have their own monster tables).
    switch (landmark) {
        case LandmarkKind::City:    return kTblEmpty;
        case LandmarkKind::Village: return kTblEmpty;
        case LandmarkKind::Ruin:    return kTblRuin;
        case LandmarkKind::Spire:   return kTblSpire;
        default: break;
    }
    // Forest override: forest fauna wherever the tree count reaches the
    // forest class (including on a mountain). Mountain fauna otherwise comes
    // from the Mountain biome below.
    if (is_forest_cell(treeCount)) return kTblForest;
    // Biome default.
    switch (biome) {
        case Biome::Tundra:   return kTblTundra;
        case Biome::Taiga:    return kTblTaiga;
        case Biome::Snow:     return kTblSnow;
        case Biome::Valley:   return kTblValley;
        case Biome::Meadow:   return kTblMeadow;
        case Biome::Swamp:    return kTblSwamp;
        case Biome::Desert:   return kTblDesert;
        case Biome::Steppe:   return kTblSteppe;
        case Biome::Tropics:  return kTblTropics;
        case Biome::Water:    return kTblEmpty;
        case Biome::Mountain: return kTblMountain;
    }
    return kTblMeadow;
}

std::vector<FaunaPick> roll_fauna(const FaunaTable& table,
                                  std::uint32_t& rngState) {
    std::vector<FaunaPick> out;
    if (table.entryCount == 0 || table.maxCount == 0) return out;

    Rng r(rngState);
    const int span  = int(table.maxCount) - int(table.minCount) + 1;
    const int count = int(table.minCount)
        + int(std::floor(r.next_f01() * float(span)));

    std::uint32_t total = 0;
    for (int i = 0; i < table.entryCount; ++i) total += table.entries[i]->weight;
    if (total == 0) { rngState = r.state; return out; }

    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        float roll = r.next_f01() * float(total);
        for (int e = 0; e < table.entryCount; ++e) {
            roll -= float(table.entries[e]->weight);
            if (roll <= 0.0f) {
                const char* fac = table.factionOverride
                    ? table.factionOverride
                    : table.entries[e]->factionId;
                out.push_back({table.entries[e], fac});
                break;
            }
        }
    }
    rngState = r.state;
    return out;
}

// ── Global monster registry ──────────────────────────────────────────
//
// Flat enumeration of every distinct creature, exactly once. The catalog
// index IS the stable creature id baked into ECS `NPCKind.type` as
// by the one table's ordinal. Order is append-only: never
// reorder (would silently re-key live entities); add new creatures at the end.
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

int fauna_cell_capacity(Biome biome, int treeCount, LandmarkKind landmark) {
    // A settled cell's wild heads live UNDER it, not on its square: the
    // street table is deliberately empty (get_fauna_table above), but the
    // cellars behind its doors are the one place vermin still hold (sub/dgn
    // reads the Ruin family for them). Without this the stock a cellar
    // borrows from would be zero and every town cellar would be swept clean
    // by definition. The allowance is the Ruin table's own FLOOR — the least
    // a den of that family ever holds — so a town is the poorest hunting
    // ground that still is one.
    if (landmark == LandmarkKind::City || landmark == LandmarkKind::Village) {
        return int(kTblRuin.minCount);
    }
    return int(get_fauna_table(biome, treeCount, landmark).maxCount);
}

namespace {
// The named place standing on a cell — the same City/Village/Spire scan
// resolve_context runs (Ruin has no macro registry yet, there as here).
LandmarkKind landmark_kind_at(const GameState& gs, int wx, int wy) {
    for (const auto& s : gs.settlements)
        if (s.x == wx && s.y == wy) return LandmarkKind::City;
    for (const auto& v : gs.villages)
        if (v.x == wx && v.y == wy) return LandmarkKind::Village;
    for (const auto& sp : gs.spires)
        if (sp.x == wx && sp.y == wy) return LandmarkKind::Spire;
    return LandmarkKind::None;
}
} // namespace

int fauna_cell_capacity_at(const GameState* gs, const TerrainData* terrain,
                           const TreeLayer* trees, int x, int y) {
    if (!gs || !terrain || !terrain->has_rgba_storage()
        || terrain->width <= 0 || terrain->height <= 0) {
        return 0;
    }
    const int wx = FeatureLayer::wrap_coord(x, terrain->width);
    const int wy = FeatureLayer::wrap_coord(y, terrain->height);
    const std::size_t src =
        (std::size_t(wy) * std::size_t(terrain->width) + std::size_t(wx)) * 4u;
    if (src + 3u >= terrain->rgba.size()) return 0;
    const float height      = float(terrain->rgba[src + 0u]) / 255.0f;
    const float moisture    = float(terrain->rgba[src + 1u]) / 255.0f;
    const float temperature = float(terrain->rgba[src + 2u]) / 255.0f;
    const Biome biome = biome_at(temperature, moisture, height,
                                 gs->mapParams.seaLevel, kMountainBiomeLevel);
    const int treeCount = (trees && trees->has_complete_storage())
        ? int(trees->at(wx, wy)) : 0;
    return fauna_cell_capacity(biome, treeCount, landmark_kind_at(*gs, wx, wy));
}

} // namespace sm
