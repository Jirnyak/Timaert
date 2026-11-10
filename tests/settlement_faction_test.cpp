// Settlement allegiance contract — a town's people belong to its FACTION
// (owner 2026-09-11: «королевств нет, только фракции — одна система»; the
// column is Landmark::factionIdx, persisted in the save since v94).
//
// The bug this pins: both the subworld citizen spawn and the procedural quest
// generator hardcoded faction_index("empire"), so a city of Old Magica fielded
// imperial guards on foot while the very same city fielded Magica guards on the
// map, and a quest for a Magica town paid reputation to the empire.
//
// The resolver is faction_or_freefolk over the stored index (the ownerless
// fallback law, macro/faction.h) and faction_index_for_cell for the ground;
// this test comes at both from both ends:
//
//   1. The fallback law — a real index, an unowned -1, an out-of-range byte.
//   2. The shipping subworld spawn path (spawn_cell_npcs → citizens): a city
//      handed a non-empire faction produces citizens of THAT faction and NOT
//      ONE imperial body. That negative control is the whole point — a test
//      that only checked "guards exist" passed happily while every guard in the
//      world served the empire.
//
// Pure ECS + data assertions: no Vulkan, no window, no GameState needed beyond a
// hand-built ownership field. Manager construction mirrors subworld_spawn_parity_test.
#include <memory>
#include "check.h"
#include "scene_objects_fixture.h"  // арена сцены фикстурам (кусок 1)
#include "ecs/components.h"
#include "tables/faction.h"
#include "macro/politik.h"
#include "bus/world_row.h"   // raise_flock_into_container — души головами
#include "sub/seamless_manager.h"
#include "sub/spawn.h"
#include "sub/record.h"   // body_kind — род тела из колонки арены (кусок 1)
#include "bus/store.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// A flat meadow cell — the resolver the manager calls for each window cell.
sm::sub::CellContext meadow_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.62f;
    c.biome = sm::Biome::Meadow;
    c.feature = sm::FT_None;
    c.rooted.id = -1;
    c.rooted.size = 0;
    c.rooted.kind = sm::SquadType::None;
    c.seed = 0x13570000u
        ^ sm::sub::cell_seed(0u, cx, cy);
    return c;
}

// ── 1. The fallback law over the stored column ──────────────────────────────
void run_resolver_contract() {
    // An ownerless place belongs to the free folk — never quietly to the empire.
    const int freeIdx = sm::faction_index("freefolk");
    CHECK_OR_RETURN(freeIdx >= 0, "the free folk are a row of the ONE registry");
    const std::uint16_t freefolk = std::uint16_t(freeIdx);

    const int magica = sm::faction_index("old_magica");
    CHECK(sm::faction_or_freefolk(magica) == std::uint16_t(magica),
          "a place with an owner keeps that owner");
    CHECK(sm::faction_or_freefolk(-1) == freefolk,
          "an UNOWNED place belongs to the free folk");
    CHECK(sm::faction_or_freefolk(9999) == freefolk,
          "a garbage owner byte fails closed to the free folk, never to a crown");
    CHECK(sm::faction_or_freefolk(int(sm::kNoFaction)) == freefolk,
          "the explicit no-faction sentinel resolves to the free folk too");
    // The unruled are their own realm, not an alias of a crown.
    CHECK(freeIdx != sm::faction_index("empire"),
          "the free folk are their OWN realm, not an alias of the empire");
    CHECK(magica != sm::faction_index("empire"),
          "old_magica is its own realm — the registry has no two rows for one");
}

// ── 1b. The same question asked of the GROUND ───────────────────────────────
// A body that appears with no owner of its own (a scripted encounter, a console
// spawn) inherits the realm that holds the cell it stands on. This is the answer
// to "no context", so it must be exactly as honest as the settlement rule — and
// must degrade, never guess, when the world has no ownership map yet.
void run_ground_owner_contract() {
    const std::uint16_t freefolk = std::uint16_t(sm::faction_index("freefolk"));

    // БАЙТ ПОЛЯ — ОРДИНАЛ ФРАКЦИИ + 1, НОЛЬ — ДИКАЯ ЗЕМЛЯ (ломоть 3
    // сейв-эпика): поле стало плоским массивом под капом клеток мира, и
    // «ничья» говорит сам байт, а не длина контейнера. Поле весит 1 МиБ и
    // рождается в куче — на стеке слои мира не живут.
    // ПОЛЕ, А НЕ ОБЁРТКА (M-90): дверь берёт само поле и сторону мира —
    // своей копии размера у поля нет, её держит мир.
    const std::unique_ptr<sm::CellOwnerField> fieldOwn{new sm::CellOwnerField()};
    sm::CellOwnerField& cellOwner = *fieldOwn;
    cellOwner.data[0] = std::uint8_t(sm::faction_index("old_magica") + 1);  // (0,0)
    cellOwner.data[5] = std::uint8_t(sm::faction_index("timaert") + 1);     // (1,1)

    // Мир фикстуры — 4×4: законный тор (сторона — степень двойки, квадрат).
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, 0, 0)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a claimed cell answers with the realm that holds it");
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, 1, 1)
              == std::uint16_t(sm::faction_index("timaert")),
          "a second claimed cell answers with ITS realm, not the first's");
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, 2, 0) == freefolk,
          "the unclaimed wilds belong to the free folk");
    // ЗАКОН АДРЕСА: the map is a torus — coordinates WRAP, they never read
    // out of bounds, and the far side of the seam is the same cell.
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, 4, 4)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a coordinate past the far edge wraps to the same cell");
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, -4, -4)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a negative coordinate wraps the same way — the torus has no edge");
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 4, 5, -3)
              == std::uint16_t(sm::faction_index("timaert")),
          "обе оси заворачиваются независимо: (5,-3) есть клетка (1,1)");

    // ПОЛЕ, КОТОРОГО ГЕНЕЗИС ЕЩЁ НЕ КАСАЛСЯ (мир посреди генерации, голая
    // фикстура), есть нулевая память — и она отвечает НИЧЕЙНОЙ землёй по
    // построению. Прежде это держала пустая длина вектора; нулевой байт в
    // прежней кодировке назвал бы фракцию со строкой 0.
    const std::unique_ptr<sm::CellOwnerField> bornOwn{new sm::CellOwnerField()};
    CHECK(sm::faction_index_for_cell(*bornOwn, 4, 4, 0, 0) == freefolk,
          "a world with no ownership flood yet answers UNCLAIMED, not row 0");
    // Байт, не называющий строки реестра (порча), — тоже ничья земля, а не
    // чтение за концом таблицы фракций.
    bornOwn->data[0] = 0xffu;
    CHECK(sm::faction_index_for_cell(*bornOwn, 4, 4, 0, 0) == freefolk,
          "a byte naming no registry row degrades to unclaimed");
    // Мир без сторон и мир ВНЕ капа клеток поле не адресуют вовсе: ответ —
    // ничья земля, а не индекс мимо массива.
    CHECK(sm::faction_index_for_cell(cellOwner, 0, 4, 0, 0) == freefolk,
          "мир без стороны отвечает НИЧЕЙНЫМ");
    CHECK(sm::faction_index_for_cell(cellOwner, 4, 2, 0, 0) == freefolk
              && sm::faction_index_for_cell(cellOwner, 6, 6, 0, 0) == freefolk,
          "неквадратный мир и сторона не степенью двойки — не тор: НИЧЬЯ");
    CHECK(sm::faction_index_for_cell(cellOwner, 2048, 2048, 0, 0) == freefolk,
          "мир вне капа клеток отвечает НИЧЕЙНЫМ, а не читает мимо поля");
    // (Проверки «обрезанный вектор» и «длина не совпала со стороной» умерли
    // вместе с вектором: у плоского поля своей длины нет, и разойтись со
    // стороной мира ему нечем.)
}

// ── 2. The shipping spawn path, with the negative control ───────────────────
// Populate a City cell for a non-empire realm and demand that EVERY citizen
// wears that realm's colours. Fauna is excluded: a creature's faction is its own
// FaunaEntry row, not the town's (NPCKind.type < NPCType::Count is the
// humanoid/creature discriminator used everywhere else).
void run_citizens_wear_their_realm(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t magica = std::uint16_t(sm::faction_index("old_magica"));
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    // Арена сцены — предусловие рождения тел (кусок 1, §8 п.11), и она НА
    // ТЕСТ, а не на реестр (ломоть E): дверь спавна берёт её явно.
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;
    // ВОПЛОЩАЮТСЯ ГОЛОВЫ (v122), поэтому предусловие свидетеля — не число,
    // а контейнер душ места: он рождает его САМ (§8 п.11), тем же законом,
    // которым его наполняет генезис.
    sm::Inventory homeSouls{};
    sm::raise_flock_into_container(homeSouls, 2000);
    sm::sub::spawn_cell_npcs(objs,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::SquadType::City,
                             /*danger*/0,
                             /*faunaBand: земля*/sm::FaunaBand{},
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A11u,
                             /*worldSeed*/0xB105A11u,
                             /*settlementFaction*/magica,
                             /*rootedPop*/2000,
/*rootedSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount: клетка города вычищена полем (M-371)*/0, &homeSouls,
                             sm::world_time_at(1, 12, 0));

    int citizens = 0;
    int wrongFaction = 0;
    int imperial = 0;
    // СВИП ПЛОСКОЙ АРЕНЫ (ломоть D): жилец сцены — бит kObjAlive. Отбор
    // ГОРОЖАН не изменился ни на строку: род — колонка слота, «рода нет» =
    // kObjNoKind, поэтому ни снаряд, ни пламя пропа (у обоих рода нет вовсе)
    // в счёт не входят — ровно как они не входили и во вьюхе.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* kind = sm::sub::slot_kind(objs, s);
        if (kind == nullptr) continue;
        if (kind->type >= std::uint16_t(sm::NPCType::Count)) continue;  // fauna
        ++citizens;
        if (kind->factionIdx != magica) ++wrongFaction;
        if (kind->factionIdx == empire) ++imperial;
    }

    // The count is asserted FIRST: without it the two zero-checks below pass
    // on an empty street, which is the §8 п.3 trap exactly.
    CHECK_OR_RETURN(citizens >= 10,
                    "a 2000-soul city actually fielded a crowd to inspect");
    CHECK(wrongFaction == 0, "every citizen wears the realm that owns the town");
    // NEGATIVE CONTROL, and now its own verdict: the bug this test was written
    // for spawned IMPERIAL bodies in a Magica town. Folded into the bool above
    // it could not say which half had broken.
    CHECK(imperial == 0,
          "not one imperial body stands in a Magica town — the bug this test "
          "exists for");
}

// The empire itself must still work — the fix must not invert the bug.
void run_imperial_city_still_imperial(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    // Та же арена-на-тест (ломоть E): свой носитель тел у каждого прогона.
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;
    sm::Inventory homeSouls{};
    sm::raise_flock_into_container(homeSouls, 400);
    sm::sub::spawn_cell_npcs(objs,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::SquadType::Village,
                             /*danger*/0,
                             /*faunaBand: земля*/sm::FaunaBand{},
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A12u,
                             /*worldSeed*/0xB105A12u,
                             /*settlementFaction*/empire,
                             /*rootedPop*/400,
                             /*rootedSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount: клетка поселения вычищена полем (M-371)*/0, &homeSouls,
                             sm::world_time_at(1, 12, 0));

    int citizens = 0;
    int foreign = 0;
    // Тот же свип арены по kObjAlive (ломоть D); гейт рода тот же.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* kind = sm::sub::slot_kind(objs, s);
        if (kind == nullptr) continue;
        if (kind->type >= std::uint16_t(sm::NPCType::Count)) continue;
        ++citizens;
        if (kind->factionIdx != empire) ++foreign;
    }
    CHECK_OR_RETURN(citizens > 0, "the imperial village fielded citizens at all");
    CHECK(foreign == 0,
          "and every one of them is imperial — the fix did not INVERT the bug");
}

}  // namespace

int main() {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty_cells();   // сброс грязи: ответ не читается

    // Each contract asserts for itself. They used to hand `main` a bool that
    // became ONE message listing the three things it might have meant — and
    // the file's only COUNTED check was the un-failable `CHECK(true, "every
    // gate above held")` at the bottom (§8 ЗАКОН НУЛЕВОЙ п.6).
    run_resolver_contract();
    run_ground_owner_contract();
    run_citizens_wear_their_realm(mgr);
    run_imperial_city_still_imperial(mgr);

    return sm::test::report("settlement_faction_test");
}
