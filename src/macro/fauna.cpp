// ФАУНА, ПОЛОВИНА КОТОРОЙ НУЖЕН МИР — виды над каталогом существ и ёмкость,
// снятая с КЛЕТКИ. Сам ЗАКОН дикого ролла (таблицы счёта, `roll_spawns`,
// `danger_match_weight`, `fauna_cell_capacity`) переехал в
// `tables/fauna.cpp` ломтём 3 эпика пакетов v1: он чист — ни слоя, ни
// `GameState`, — а спрашивают его ОБА мира.
//   - Tables stored as null-terminated arrays of `const FaunaEntry*` so
//     they live in `.rodata` and never allocate.
#include "macro/fauna.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/tree_layer.h"
#include "tables/forest.h"   // is_forest_cell — класс леса по счёту деревьев
#include "core/torus.h"      // wrap_axis — ось клетки мира (ЗАКОН АДРЕСА)
#include <algorithm>

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
// The populated bestiary (2026-09-11) — sixteen more names for rows of the
// same one table, so the catalog below still reads like a bestiary.
static const FaunaEntry& kGiantRat = kNpcTypeDefs[std::size_t(NPCType::GiantRat)];
static const FaunaEntry& kCaveBat = kNpcTypeDefs[std::size_t(NPCType::CaveBat)];
static const FaunaEntry& kKobold = kNpcTypeDefs[std::size_t(NPCType::Kobold)];
static const FaunaEntry& kCaveSpider = kNpcTypeDefs[std::size_t(NPCType::CaveSpider)];
static const FaunaEntry& kImp = kNpcTypeDefs[std::size_t(NPCType::Imp)];
static const FaunaEntry& kZombie = kNpcTypeDefs[std::size_t(NPCType::Zombie)];
static const FaunaEntry& kOrc = kNpcTypeDefs[std::size_t(NPCType::Orc)];
static const FaunaEntry& kGhoul = kNpcTypeDefs[std::size_t(NPCType::Ghoul)];
static const FaunaEntry& kHarpy = kNpcTypeDefs[std::size_t(NPCType::Harpy)];
static const FaunaEntry& kCultist = kNpcTypeDefs[std::size_t(NPCType::Cultist)];
static const FaunaEntry& kGargoyle = kNpcTypeDefs[std::size_t(NPCType::Gargoyle)];
static const FaunaEntry& kWraith = kNpcTypeDefs[std::size_t(NPCType::Wraith)];
static const FaunaEntry& kOgre = kNpcTypeDefs[std::size_t(NPCType::Ogre)];
static const FaunaEntry& kMinotaur = kNpcTypeDefs[std::size_t(NPCType::Minotaur)];
static const FaunaEntry& kBasilisk = kNpcTypeDefs[std::size_t(NPCType::Basilisk)];
static const FaunaEntry& kLich = kNpcTypeDefs[std::size_t(NPCType::Lich)];


// (pick_crowd_row stood here until 2026-10-04 — the D-tail verdict killed
// the last two callers: an interior resident and the dawn door both read
// HEADS of the place's inventory now, so there is nothing left to roll.)

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
    // Appended 2026-09-11 (the populated bestiary) — append-only, exactly as
    // the header promises: this list serves id-string lookups, and reordering
    // it would re-key nothing in the world but would break every content
    // file and console command that names a species.
    &kGiantRat, &kCaveBat, &kKobold, &kCaveSpider, &kImp, &kZombie, &kOrc,
    &kGhoul, &kHarpy, &kCultist, &kGargoyle, &kWraith, &kOgre, &kMinotaur,
    &kBasilisk, &kLich,
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
    // ПРИРОДУ ЗДЕСЬ НЕ СПРАШИВАЮТ (2026-09-21): каталог — это ВИД на ту же
    // таблицу тел, а «человек ли он» — отдельный вопрос с отдельной дверью
    // (is_folk_kind). Культист человек И живёт в подземелье; прежний гейт
    // «не человек» вычёркивал бы его из собственного каталога.
    // The kind IS the row. There is no index to mask and no second array to
    // index into — that masking (`kindType & 0xFF` into the catalog) was the
    // old encoding's last hiding place, and it silently answered with the
    // WRONG creature the moment the catalog stopped being the id space.
    return &kNpcTypeDefs[std::size_t(kindType)];
}

// ── The honest headcount (Session 16) ────────────────────────────────

// (A hand-written City/Village/Spire scan named `place_kind_at` lived here
// until 2026-08-24 — the drifted second implementation of "what stands on
// this cell". The baked index answers now, in the same priority order.)

int fauna_cell_capacity_at(const MacroWorld& w, int x, int y) {
    const TerrainData* terrain = w.terrain;
    if (!terrain || !terrain->has_rgba_storage()
        || terrain->width <= 0 || terrain->height <= 0) {
        return 0;
    }
    // Ось клетки мира сворачивается КАНОНИЧЕСКОЙ ДВЕРЬЮ (ЗАКОН АДРЕСА п.2):
    // здесь стоял `FeatureLayer::wrap_coord`, то есть ТОТ ЖЕ `wrap_axis`,
    // позванный через статик живого слоя фич, — а слой уехал из этого
    // заголовка вместе с законом ролла (ломоть 3 эпика пакетов v1). Число
    // бит-в-бит то же: `wrap_coord` и есть звонок к `wrap_axis`.
    const int wx = wrap_axis(x, terrain->width);
    const int wy = wrap_axis(y, terrain->height);
    // THE cell cascade (map_generator.h biome_at_cell). This function used to
    // classify by float threshold while the subworld read the mask — the same
    // coastal cell fed a wolf and refused a boot (canon-audit C5).
    const Biome biome = biome_at_cell(*terrain, wx, wy);
    const int treeCount = (w.trees && w.trees->has_complete_storage())
        ? int(w.trees->at(wx, wy)) : 0;
    // ЗДЕСЬ ЖИЛ СКАН СКВАДОВ (`settlement_at`) — второй носитель рода против
    // байта фичи у ролла, и он умер с род-развилками (M-371): природная
    // ёмкость спрашивает только ЗЕМЛЮ. Подавление у поселения — вычистка его
    // клетки в поле при рождении (birth_squad.h), охотой и ростом дальше
    // распоряжается само поле.
    return fauna_cell_capacity(biome, treeCount);
}

} // namespace sm
