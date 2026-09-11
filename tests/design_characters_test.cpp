// СТОЛ АНКЕТ (macro/characters.h) — свидетель конвейера дизайн-персонажей.
//
// Что обещано и чем держится:
//   1. Строка стола рождается в мир ОДНИМ телом: тег с ординалом, владеемый
//      лист (индивид, «он как игрок»), дом из контекста мира, маршрут
//      «дом ↔ ближайший ландмарк рода из агенды» приказом в SquadOrders.
//   2. Лестница effective_behaviour: приказ > анкета > строка типа — и
//      ступень анкеты ПРОВЕРЯЕМО отличается от строки типа (Merchant.ai =
//      Trader, анкета говорит Waypoints).
//   3. Снапшот несёт ординал (designOrdinal, v92) и восстанавливает тег;
//      обычный сквад едет с −1 (негативный контроль).
//   4. Мир без дома анкету честно НЕ рождает (смерть навсегда держится
//      генезисом — ресспавнить некому, и этот тест демонстрирует «нет
//      контекста — нет тела» тем же скипом).
// (Авторский лист — authoredSheet — намеренно не заасерчен: первая строка
// стола катается броском; колонка получит свидетеля с первой авторской
// анкетой. Testing law 7 — говорим это вслух.)
#include "check.h"

#include "core/rng.h"
#include "macro/characters.h"
#include "macro/faction.h"
#include "macro/macro_snapshot.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/squad.h"

#include <cstdio>

namespace {

using namespace sm;

constexpr int kW = 64, kH = 64;

// Сухая равнина целиком — find_valid_spawn нужна честная суша.
TerrainData make_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW) * kH * 4u, 0);
    t.riverData.assign(std::size_t(kW) * kH, 0);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        t.rgba[i * 4u + 0] = 180;   // суша выше уровня моря
        t.rgba[i * 4u + 3] = 255;   // маска суши
    }
    return t;
}

Landmark make_landmark(int id, LandmarkType type, int x, int y) {
    Landmark lm;
    lm.id = id;
    lm.type = type;
    lm.x = x;
    lm.y = y;
    lm.population = 100;
    return lm;
}

// Мир пробы: город и ДВЕ деревни — ближняя и дальняя, чтобы «ближайшая»
// была утверждением, а не совпадением единственности.
GameState make_world() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    gs.landmarks.push_back(make_landmark(1, LandmarkType::City, 10, 10));
    gs.landmarks.push_back(make_landmark(2, LandmarkType::Village, 20, 10));
    gs.landmarks.push_back(make_landmark(3, LandmarkType::Village, 40, 40));
    return gs;
}

entt::entity find_design(ecs::World& w, std::int16_t ord) {
    for (auto e : w.reg.view<ecs::DesignCharacterTag>()) {
        if (w.reg.get<ecs::DesignCharacterTag>(e).ordinal == ord) return e;
    }
    return entt::null;
}

void test_table_rows_resolve() {
    // Каждая строка стола обязана называть живые ключи — authoring-строка,
    // не резолвящаяся в ординал, была бы тихим пропуском спавна.
    int checked = 0;
    for (const DesignCharacterDef& row : kDesignCharacterDefs) {
        CHECK(row.body < NPCType::Count, "row body names a registry row");
        // nullptr = фракция дома (царь) — авторская строка обязана
        // резолвиться только когда она есть.
        CHECK(row.factionId == nullptr || faction_index(row.factionId) >= 0,
              "row faction resolves in THE one faction registry");
        CHECK(row.id != nullptr && row.id[0] != '\0', "row has an id key");
        ++checked;
    }
    CHECK(checked > 0, "the table is not empty (the preacher is row 0)");
}

void test_spawn_births_the_row() {
    GameState gs = make_world();
    const TerrainData terrain = make_terrain();
    ecs::World w;
    Rng rng(1234u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);

    const entt::entity e = find_design(w, 0);
    CHECK_OR_RETURN(e != entt::null, "the preacher row became one body");

    const DesignCharacterDef& row = kDesignCharacterDefs[0];
    // Индивид владеет листом (посадки А/Б) — и уровень строки дошёл.
    const CharacterSheet* own = owned_sheet(w, e);
    CHECK_OR_RETURN(own != nullptr, "the design character OWNS his sheet");
    CHECK(own->levelData.level == int(row.level),
          "the row's level reached the owned sheet");
    CHECK(w.reg.get<ecs::NpcLevel>(e).value == row.level,
          "...and the map-side level column agrees");
    CHECK(int(w.reg.get<ecs::NPCKind>(e).factionIdx)
              == faction_index(row.factionId),
          "the row's faction reached the body");
    CHECK(w.reg.get<ecs::MacroNpcRuntime>(e).homeSettlementId == 1,
          "home resolved to the world's city (row: City #0)");

    // Маршрут: дом ↔ БЛИЖАЙШАЯ деревня (20,10), не дальняя (40,40).
    const auto* orders = w.reg.try_get<ecs::SquadOrders>(e);
    CHECK_OR_RETURN(orders != nullptr && orders->waypointCount == 2,
                    "the circuit order landed: two waypoints");
    CHECK(orders->waypoints[0] == 10 && orders->waypoints[1] == 10,
          "waypoint A is home (the city cell)");
    CHECK(orders->waypoints[2] == 20 && orders->waypoints[3] == 10,
          "waypoint B is the NEAREST village, not the far one");

    // Лестница: приказ первым; без приказа — ступень анкеты, и она
    // ПРОВЕРЯЕМО не строка типа (Merchant.ai = Trader).
    const auto& kind = w.reg.get<ecs::NPCKind>(e);
    CHECK(effective_behaviour(w.reg, e, kind) == AIBehaviour::Waypoints,
          "with the route present, the order rung answers");
    w.reg.remove<ecs::SquadOrders>(e);
    CHECK(effective_behaviour(w.reg, e, kind) == row.behaviour,
          "without the route, the design-row rung answers");
    CHECK(kNpcTypeDefs[std::uint16_t(row.body)].ai != row.behaviour,
          "negative control: the row rung provably differs from the type "
          "row for this body — the ladder step is real");
}

void test_snapshot_carries_the_ordinal() {
    GameState gs = make_world();
    const TerrainData terrain = make_terrain();
    ecs::World w;
    Rng rng(777u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);
    // Обычный сквад рядом — негативный контроль на −1. Через ту же одну
    // дверь создания (spawn_squad → make_npc).
    SquadSpec plain{};
    plain.leaderType = NPCType::Bandit;
    plain.x = 30;
    plain.y = 30;
    plain.factionIndex = faction_index("bandits");
    CHECK_OR_RETURN(spawn_squad(gs, w, terrain, plain) != entt::null,
                    "the plain control squad spawned");

    const std::vector<MacroNpcRecord> snap = snapshot_macro_ecs(w);
    int designRecords = 0, plainRecords = 0;
    for (const MacroNpcRecord& r : snap) {
        if (r.designOrdinal >= 0) {
            ++designRecords;
            CHECK(r.designOrdinal == 0, "the record names the table row");
        } else {
            ++plainRecords;
        }
    }
    CHECK(designRecords == 1, "exactly one design record in the snapshot");
    CHECK(plainRecords >= 1,
          "negative control: the plain squad rides with -1");

    ecs::World w2;
    GameState gs2 = make_world();
    restore_macro_ecs(snap, w2, gs2);
    const entt::entity back = find_design(w2, 0);
    CHECK_OR_RETURN(back != entt::null,
                    "restore re-stamped the design tag from the record");
    CHECK(owned_sheet(w2, back) != nullptr,
          "...and his owned sheet came back with it (hasSheet)");
}

void test_king_peasant_births_by_home_faction() {
    // Мир с ВАРВАРСКИМ городом: город несёт фракцию barbarian_north СВОЕЙ
    // колонкой (королевства вырезаны 2026-09-11) — и фикстурные
    // freefolk-города рядом, чтобы фильтр префикса был утверждением, а не
    // единственностью.
    GameState gs = make_world();   // города 1 (freefolk) хватает для Варнавы
    Landmark barbCity = make_landmark(9, LandmarkType::City, 50, 20);
    barbCity.factionIdx = std::int16_t(faction_index("barbarian_north"));
    gs.landmarks.push_back(barbCity);

    const TerrainData terrain = make_terrain();
    ecs::World w;
    Rng rng(555u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);

    const entt::entity king = find_design(w, 1);
    CHECK_OR_RETURN(king != entt::null,
                    "the king row became one body near the barbarian city");
    CHECK(w.reg.get<ecs::MacroNpcRuntime>(king).homeSettlementId == 9,
          "his home is the BARBARIAN city, not the freefolk one — the "
          "faction-prefix filter picked the row's home");
    // Фракция — ОН САМ (вердикт владельца): своя строка одной матрицы,
    // как у игрока, а не знамя города, где он живёт.
    CHECK(int(w.reg.get<ecs::NPCKind>(king).factionIdx)
              == faction_index("king_peasant"),
          "his faction is his OWN registry row");
    const CharacterSheet* own = owned_sheet(w, king);
    CHECK_OR_RETURN(own != nullptr, "the king OWNS his sheet");
    CHECK(own->levelData.level == 70, "the level-70 roll reached the sheet");
    // Лестница: приказов нет — ступень анкеты, доказуемо не строка типа
    // (Peasant.ai = Gatherer).
    const auto& kind = w.reg.get<ecs::NPCKind>(king);
    CHECK(effective_behaviour(w.reg, king, kind) == AIBehaviour::MageHunt,
          "the design rung answers MageHunt for the king");
    CHECK(kNpcTypeDefs[std::uint16_t(NPCType::Peasant)].ai
              != AIBehaviour::MageHunt,
          "negative control: the peasant type row does not hunt mages");
}

void test_king_needs_a_barbarian_city() {
    // Мир Варнавы (freefolk-город + деревни), варварского города НЕТ: царь
    // честно не рождается, проповедник рождается — фильтр режет ровно
    // одну строку, не весь стол.
    GameState gs = make_world();
    const TerrainData terrain = make_terrain();
    ecs::World w;
    Rng rng(556u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);
    CHECK(find_design(w, 0) != entt::null,
          "Varnava is born in a world without barbarians");
    CHECK(find_design(w, 1) == entt::null,
          "the king is honestly NOT born without a barbarian city");
}

void test_dragons_nest_on_mountain_peaks() {
    // Мир с ГОРНЫМ МАССИВОМ: пятно высоты 250 (выше kMountainBiomeLevel)
    // с вершиной в (48,48) — и плоская равнина вокруг. Плоские фикстуры
    // остальных тестов драконов честно НЕ рождают (порог биома).
    GameState gs = make_world();
    TerrainData terrain = make_terrain();
    for (int y = 44; y <= 52; ++y) {
        for (int x = 44; x <= 52; ++x) {
            terrain.rgba[(std::size_t(y) * kW + x) * 4u + 0] =
                std::uint8_t(x == 48 && y == 48 ? 250 : 230);
        }
    }
    ecs::World w;
    Rng rng(999u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);

    // Один массив = одна вершина: Dragon1 рождается, №2/№3 (вершины с
    // разносом ≥ mapW/8) — честно нет.
    const entt::entity d1 = find_design(w, 2);
    CHECK_OR_RETURN(d1 != entt::null, "Dragon1 nests on the one massif");
    CHECK(find_design(w, 3) == entt::null && find_design(w, 4) == entt::null,
          "one massif births one dragon — spacing is honest");

    const auto& rt = w.reg.get<ecs::MacroNpcRuntime>(d1);
    CHECK(rt.lairX == 48 && rt.lairY == 48,
          "his LAIR is the massif's highest cell");
    CHECK(rt.flying == 1,
          "the row's cruiseM cached as the march-side flying byte (v93)");
    CHECK(int(w.reg.get<ecs::NPCKind>(d1).factionIdx)
              == faction_index("dragons"),
          "dragons share the ONE dragons faction row (owner verdict)");
    CHECK(owned_sheet(w, d1) != nullptr,
          "the dragon OWNS his sheet like every design character");
    const auto& kind = w.reg.get<ecs::NPCKind>(d1);
    CHECK(effective_behaviour(w.reg, d1, kind) == AIBehaviour::LairSorties,
          "the design rung answers LairSorties");
    CHECK(kNpcTypeDefs[std::uint16_t(NPCType::Dragon)].ai
              != AIBehaviour::LairSorties,
          "negative control: the type row does not sortie — the rung is real");
}

void test_no_home_no_birth() {
    GameState gs{};   // мир вовсе без ландмарков
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData terrain = make_terrain();
    ecs::World w;
    Rng rng(42u);
    gs.nextMacroSpawnOrdinal = 0;
    spawn_design_characters(gs, w, terrain, rng, gs.nextMacroSpawnOrdinal);
    int tags = 0;
    for ([[maybe_unused]] auto e : w.reg.view<ecs::DesignCharacterTag>())
        ++tags;
    CHECK(tags == 0, "a world without the row's home births nobody");
}

} // namespace

int main() {
    test_table_rows_resolve();
    test_spawn_births_the_row();
    test_snapshot_carries_the_ordinal();
    test_king_peasant_births_by_home_faction();
    test_king_needs_a_barbarian_city();
    test_dragons_nest_on_mountain_peaks();
    test_no_home_no_birth();
    return sm::test::report("design_characters_test");
}
