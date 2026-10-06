// СТОЛ АНКЕТ (macro/characters.h) — свидетель конвейера дизайн-персонажей.
//
// Что обещано и чем держится:
//   1. Строка стола рождается в мир ОДНИМ телом: тег с ординалом, владеемый
//      лист (индивид, «он как игрок»), дом из контекста мира, маршрут
//      «дом ↔ ближайший ландмарк рода из агенды» приказом в SquadOrders.
//   2. Лестница untyped_squad_behaviour: приказ > анкета > строка типа — и
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
#include "macro/world_row.h"   // raise_flock_into_container — души головами

#include "core/rng.h"
#include "macro/characters.h"
#include "tables/faction.h"
#include "macro/macro_snapshot.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/place_birth.h"  // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/squad.h"
#include "macro/store.h"

#include <cstdio>

namespace {

using namespace sm;

constexpr int kW = 64, kH = 64;

// Сухая равнина целиком — find_valid_spawn нужна честная суша.
// САМЫЙ ВЫСОКИЙ УРОВЕНЬ, КОТОРЫЙ ЕЩЁ НЕ ГОРА — вывод из `kMountainBiomeLevel`,
// а не литерал. Здесь стояло 180 (0.706): под линией 0.75 это была высокая
// суша, а когда линия переехала на 0.625 (бескламповый синтез, 2026-10-01),
// та же фикстура молча стала ГОРОЙ целиком и унесла с собой то, что тест
// проверяет. Выведенный уровень не может разойтись с линией по построению.
// Середина между плоскостью моря и горной линией: суша БЕЗ ДВУСМЫСЛЕННОСТИ —
// и выше воды, и заведомо не массив. Уровень у самой линии фикстуре не годится:
// он делает утверждение «мир без дома строки никого не рождает» заложником
// одного шага словаря карты.
// В канал едет СЛОВО (`field_word_of`), а не байт: карта перешла на unorm16, и
// байтовый литерал означал бы в ней 1/257 своей прежней высоты, молча.
constexpr float kFlatLand01 =
    0.5f * (sm::kDefaultSeaLevel + sm::kMountainBiomeLevel);

TerrainData make_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW) * kH * 4u, 0);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        t.rgba[i * 4u + 0] = sm::field_word_of(kFlatLand01);
                                    // суша выше моря, но НЕ гора
        t.rgba[i * 4u + 3] = std::uint16_t(sm::kFieldWordMax);   // маска суши
    }
    return t;
}

// ОСНОВАНИЕ МЕСТА — ОДНА ДВЕРЬ: ТЕЛО в store (ломтик F; ординал выдаёт
// ЕДИНЫЙ эмитент M-37 внутри двери, поэтому «подвинуть выдачу выше id»
// фикстуре больше нечем и незачем — выдача монотонна по построению).
// Души — ГОЛОВАМИ в инвентарь ТЕЛА (v122): фабрика мира их не видит,
// поэтому пасту (worked-число фичи) ставит звонящий, если она ему нужна.
sm::MacroHandle settle(GameState& gs, sm::MacroStore& st, SquadType kind,
                       int x, int y, std::int16_t factionIdx = -1) {
    const sm::MacroHandle h =
        birth_place(gs, st, kind, x, y, factionIdx);
    raise_flock_into_container(st.inventory[h.slot].inv, 100);
    return h;
}

// Мир пробы: город и ДВЕ деревни — ближняя и дальняя, чтобы «ближайшая»
// была утверждением, а не совпадением единственности.
GameState make_world(sm::MacroStore& st) {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    settle(gs, st, SquadType::City, 10, 10);
    settle(gs, st, SquadType::Village, 20, 10);
    settle(gs, st, SquadType::Village, 40, 40);
    return gs;
}

// ПУТЬ ЗАГРУЗКИ ПРИВОЗИТ МЕСТА ЗАПИСЯМИ БЛОКА МАКРО-СКВАДОВ (ломтик F):
// место есть неподвижный сквад, его тело едет тем же снапшотом, что и всякое
// другое, и сшивать строку с телом больше нечего. Поэтому мир загрузки —
// ПУСТОЙ: всё, что в нём стоит, приносит restore_macro_ecs (он же поднимает
// эмитент выше всякого приехавшего ординала).
GameState make_loaded_world() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    return gs;
}

sm::MacroHandle find_design(ecs::World& w, std::int16_t ord) {
    // Тег стола — колонка store (6.3: население — живые слоты, голый цикл).
    const sm::MacroStore& st = sm::store_of(w);
    for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32) {
        const std::uint16_t slot = std::uint16_t(s32);
        if (st.alive[slot] == 0) continue;
        if (st.designTag[slot].ordinal == ord)
            return sm::handle_at(st, slot);
    }
    return {};
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
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    GameState gs = make_world(*wStore_);
    const TerrainData terrain = make_terrain();
    Rng rng(1234u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);

    const sm::MacroHandle e = find_design(w, 0);
    CHECK_OR_RETURN(wStore_->valid(e), "the preacher row became one body");

    const DesignCharacterDef& row = kDesignCharacterDefs[0];
    // Индивид владеет листом (посадки А/Б) — и уровень строки дошёл.
    const CharacterSheet* own = owned_sheet(*wStore_, e);
    CHECK_OR_RETURN(own != nullptr, "the design character OWNS his sheet");
    CHECK(own->levelData.level == int(row.level),
          "the row's level reached the owned sheet");
    CHECK(wStore_->level[e.slot].value == row.level,
          "...and the map-side level column agrees");
    CHECK(int(wStore_->kind[e.slot].factionIdx)
              == faction_index(row.factionId),
          "the row's faction reached the body");
    CHECK(wStore_->runtime[e.slot].homeSettlementId == 1,
          "home resolved to the world's city (row: City #0)");

    // Маршрут: дом ↔ БЛИЖАЙШАЯ деревня (20,10), не дальняя (40,40).
    const auto* orders = &wStore_->orders[e.slot];
    CHECK_OR_RETURN(orders->waypointCount == 2,
                    "the circuit order landed: two waypoints");
    CHECK(orders->waypoints[0] == 10 && orders->waypoints[1] == 10,
          "waypoint A is home (the city cell)");
    CHECK(orders->waypoints[2] == 20 && orders->waypoints[3] == 10,
          "waypoint B is the NEAREST village, not the far one");

    // Лестница: приказ первым; без приказа — ступень анкеты, и она
    // ПРОВЕРЯЕМО не строка типа (Merchant.ai = Trader).
    const auto& kind = wStore_->kind[e.slot];
    CHECK(untyped_squad_behaviour(*wStore_, e, kind)
              == AIBehaviour::Waypoints,
          "with the route present, the order rung answers");
    // ПРИКАЗ СНИМАЕТСЯ ТОЙ ЖЕ ДВЕРЬЮ, ЧТО ЕГО СТАВИТ (M-136). Здесь стояло
    // `w.reg.remove<ecs::SquadOrders>(e)` — НО-ОП с флипа 1в: приказ лежит
    // колонкой store, а entt-компоненты на этом скводе нет вовсе. Значит
    // проверка ниже все эти дни исполняла НЕ ТУ ступень (отвечала первая, с
    // целым маршрутом), и совпадала с ожиданием лишь потому, что у строки 0
    // `behaviour` тоже `Waypoints`.
    // Ординал СПРАШИВАЕТСЯ у сущности, а не угадывается порядком спавна:
    // допущение «проповедник родился нулевым» и есть тот род надежды, который
    // §8 п.11 запрещает свидетелю.
    const std::uint32_t ord = wStore_->spawnId[e.slot].index;
    CHECK(sm::order_squad_route(sm::store_of(w), ord, ecs::SquadOrders{}),
          "приказ снят дверью мира, а не компонентой мимо колонки");
    CHECK(wStore_->orders[e.slot].waypointCount == 0,
          "и снятие видно читателю: маршрута в колонке больше нет");
    CHECK(untyped_squad_behaviour(*wStore_, e, kind)
              == row.behaviour,
          "without the route, the design-row rung answers");
    // ЧЕГО ЭТА СЕКЦИЯ НЕ ДОКАЗЫВАЕТ, СКАЗАНО ВСЛУХ (§8 п.7): у строки 0
    // `behaviour == Waypoints`, поэтому ступени 1 и 3 дают ОДИН ответ, и
    // проверка выше их не различает — она свидетельствует лишь то, что после
    // снятия маршрута дверь лестницы не падает и отвечает строкой. Различающий
    // свидетель ступени 3 требует строки, чей `behaviour` НЕ `Waypoints`, и её
    // дома в этой фикстуре нет; дыра названа в реестре (M-133), а не
    // замаскирована подгонкой ожидания.
    CHECK(kNpcTypeDefs[std::uint16_t(row.body)].ai != row.behaviour,
          "negative control: the row rung provably differs from the type "
          "row for this body — the ladder step is real");
}

void test_snapshot_carries_the_ordinal() {
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    GameState gs = make_world(*wStore_);
    const TerrainData terrain = make_terrain();
    Rng rng(777u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);
    // Обычный сквад рядом — негативный контроль на −1. Через ту же одну
    // дверь создания (spawn_squad → make_npc).
    SquadSpec plain{};
    plain.leaderType = NPCType::Bandit;
    plain.x = 30;
    plain.y = 30;
    plain.factionIndex = faction_index("bandits");
    CHECK_OR_RETURN(sm::store_of(w).valid(
                        spawn_squad(gs, sm::store_of(w), terrain, plain)),
                    "the plain control squad spawned");

    const std::vector<MacroNpcRecord> snap = snapshot_macro_ecs(*wStore_);
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

    auto w2Store_ = sm::make_macro_store();

    sm::store_attach(w2, w2Store_.get());
    GameState gs2 = make_loaded_world();
    // Тела мест приехали теми же записями блока — ровно так же, как на
    // загрузке игры (main.cpp): второй двери у них больше нет.
    restore_macro_ecs(snap, *w2Store_, gs2);
    const sm::MacroHandle back = find_design(w2, 0);
    CHECK_OR_RETURN(w2Store_->valid(back),
                    "restore re-stamped the design tag from the record");
    CHECK(owned_sheet(*w2Store_, back) != nullptr,
          "...and his owned sheet came back with it (hasSheet)");
}

void test_king_peasant_births_by_home_faction() {
    // Мир с ВАРВАРСКИМ городом: город несёт фракцию barbarian_north СВОЕЙ
    // колонкой (королевства вырезаны 2026-09-11) — и фикстурные
    // freefolk-города рядом, чтобы фильтр префикса был утверждением, а не
    // единственностью.
    const TerrainData terrain = make_terrain();
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    GameState gs = make_world(*wStore_);  // freefolk-города хватает Варнаве
    const sm::MacroHandle barb =
        settle(gs, *wStore_, SquadType::City, 50, 20,
               std::int16_t(faction_index("barbarian_north")));
    const int barbId = int(wStore_->spawnId[barb.slot].index);
    Rng rng(555u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);

    sm::MacroStore& stk = sm::store_of(w);
    const sm::MacroHandle king = find_design(w, 1);
    CHECK_OR_RETURN(stk.valid(king),
                    "the king row became one body near the barbarian city");
    CHECK(stk.runtime[king.slot].homeSettlementId == barbId,
          "his home is the BARBARIAN city, not the freefolk one — the "
          "faction-prefix filter picked the row's home");
    // Фракция — ОН САМ (вердикт владельца): своя строка одной матрицы,
    // как у игрока, а не знамя города, где он живёт.
    CHECK(int(stk.kind[king.slot].factionIdx)
              == faction_index("king_peasant"),
          "his faction is his OWN registry row");
    const CharacterSheet* own = owned_sheet(stk, king);
    CHECK_OR_RETURN(own != nullptr, "the king OWNS his sheet");
    CHECK(own->levelData.level == 70, "the level-70 roll reached the sheet");
    // Лестница: приказов нет — ступень анкеты, доказуемо не строка типа
    // (Peasant.ai = Gatherer).
    const auto& kind = stk.kind[king.slot];
    CHECK(untyped_squad_behaviour(stk, king, kind)
              == AIBehaviour::MageHunt,
          "the design rung answers MageHunt for the king");
    CHECK(kNpcTypeDefs[std::uint16_t(NPCType::Peasant)].ai
              != AIBehaviour::MageHunt,
          "negative control: the peasant type row does not hunt mages");
}

void test_king_needs_a_barbarian_city() {
    // Мир Варнавы (freefolk-город + деревни), варварского города НЕТ: царь
    // честно не рождается, проповедник рождается — фильтр режет ровно
    // одну строку, не весь стол.
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    GameState gs = make_world(*wStore_);
    const TerrainData terrain = make_terrain();
    Rng rng(556u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);
    CHECK(sm::store_of(w).valid(find_design(w, 0)),
          "Varnava is born in a world without barbarians");
    CHECK(!sm::store_of(w).valid(find_design(w, 1)),
          "the king is honestly NOT born without a barbarian city");
}

void test_dragons_nest_on_mountain_peaks() {
    // Мир с ГОРНЫМ МАССИВОМ: пятно высоты 0.902 (выше kMountainBiomeLevel)
    // с вершиной 0.980 в (48,48) — и плоская равнина вокруг. Плоские фикстуры
    // остальных тестов драконов честно НЕ рождают (порог биома).
    // Уровни, а не байты: прежние 250/230 канала означали ровно эти доли.
    constexpr float kPeak01 = 250.0f / 255.0f;
    constexpr float kMassif01 = 230.0f / 255.0f;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    GameState gs = make_world(*wStore_);
    TerrainData terrain = make_terrain();
    for (int y = 44; y <= 52; ++y) {
        for (int x = 44; x <= 52; ++x) {
            terrain.rgba[(std::size_t(y) * kW + x) * 4u + 0] =
                sm::field_word_of(x == 48 && y == 48 ? kPeak01 : kMassif01);
        }
    }
    Rng rng(999u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);

    // Один массив = одна вершина: Dragon1 рождается, №2/№3 (вершины с
    // разносом ≥ mapW/8) — честно нет.
    const sm::MacroHandle d1 = find_design(w, 2);
    sm::MacroStore& std_ = sm::store_of(w);
    CHECK_OR_RETURN(std_.valid(d1), "Dragon1 nests on the one massif");
    CHECK(!std_.valid(find_design(w, 3)) && !std_.valid(find_design(w, 4)),
          "one massif births one dragon — spacing is honest");

    const auto& rt = std_.runtime[d1.slot];
    CHECK(rt.lairX == 48 && rt.lairY == 48,
          "his LAIR is the massif's highest cell");
    CHECK(rt.flying == 1,
          "the row's cruiseM cached as the march-side flying byte (v93)");
    CHECK(int(std_.kind[d1.slot].factionIdx)
              == faction_index("dragons"),
          "dragons share the ONE dragons faction row (owner verdict)");
    CHECK(owned_sheet(std_, d1) != nullptr,
          "the dragon OWNS his sheet like every design character");
    const auto& kind = std_.kind[d1.slot];
    CHECK(untyped_squad_behaviour(std_, d1, kind)
              == AIBehaviour::LairSorties,
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
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    Rng rng(42u);
    spawn_design_characters(gs, w, sm::store_of(w), terrain, rng, gs.nextMacroSpawnOrdinal);
    int tags = 0;
    for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32)
        if (sm::store_of(w).alive[s32] != 0
            && sm::store_of(w).designTag[s32].ordinal >= 0)
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
