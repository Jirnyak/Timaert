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
#include <memory>
#include "bus/world_row.h"   // raise_flock_into_container — души головами

#include "core/rng.h"
#include "macro/characters.h"
#include "tables/faction.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/birth_squad.h"  // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/squad.h"
#include "bus/store.h"

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
// поэтому пасту (число фичи) ставит звонящий, если она ему нужна.
sm::MacroHandle settle(GameState& gs, sm::MacroStore& st, SquadType kind,
                       int x, int y, std::int16_t factionIdx = -1) {
    const sm::MacroHandle h =
        birth_squad(gs, nullptr, st, kind, x, y, factionIdx);
    raise_flock_into_container(st.inventory[h.slot].inv, 100);
    return h;
}

// Мир пробы: город и ДВЕ деревни — ближняя и дальняя, чтобы «ближайшая»
// была утверждением, а не совпадением единственности.
std::unique_ptr<GameState> make_world(sm::MacroStore& st) {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kW;
    gs.mapH = kH;
    settle(gs, st, SquadType::City, 10, 10);
    settle(gs, st, SquadType::Village, 20, 10);
    settle(gs, st, SquadType::Village, 40, 40);
    return gsOwn;
}

sm::MacroHandle find_design(sm::MacroStore& st, std::int16_t ord) {
    // Тег стола — колонка store (6.3: население — живые слоты, голый цикл).
    for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32) {
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
    auto wStore_ = sm::make_macro_store();
    std::unique_ptr<GameState> gsOwn = make_world(*wStore_);
    GameState& gs = *gsOwn;
    const TerrainData terrain = make_terrain();
    Rng rng(1234u);
    spawn_design_characters(gs, *wStore_, terrain, rng);

    const sm::MacroHandle e = find_design(*wStore_, 0);
    CHECK_OR_RETURN(wStore_->valid(e), "the preacher row became one body");

    const DesignCharacterDef& row = kDesignCharacterDefs[0];
    // Уровень АВТОРСКОЙ СТРОКИ дошёл до колонки листа. Проверяется ЗНАЧЕНИЕ,
    // а не наличие: колонка есть у каждого слота (предикат владения снесён
    // 2026-11-08 — РОДОВ АНКЕТЫ НЕ ДВА), и ненулевой указатель не доказывал
    // ничего о том, что строка доехала.
    const CharacterSheet& own = wStore_->sheet[e.slot];
    CHECK(sm::level_of(own.levelData) == int(row.level),
          "the row's level reached the sheet column");
    // Здесь стояла проверка СОГЛАСИЯ колонки `ecs::NpcLevel` с анкетой — она
    // охраняла сам дубль, и её субъект умер вместе с колонкой (M-246а).
    // Остался закон «уровень спрашивают ОДНОЙ дверью, и она читает анкету»:
    // заведи кто-нибудь второй носитель и подмени им дверь — покраснеет тут.
    CHECK(sm::squad_level(*wStore_, e.slot) == int(row.level),
          "...and THE level door answers from that same sheet");
    CHECK(int(wStore_->kind[e.slot].factionIdx)
              == faction_index(row.factionId),
          "the row's faction reached the body");
    CHECK(sm::parent_of(*wStore_, e.slot) == 1,
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
    CHECK(sm::order_squad_route(*wStore_, ord, ecs::SquadOrders{}),
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

void test_store_carries_the_ordinal() {
    auto wStore_ = sm::make_macro_store();
    std::unique_ptr<GameState> gsOwn = make_world(*wStore_);
    GameState& gs = *gsOwn;
    const TerrainData terrain = make_terrain();
    Rng rng(777u);
    spawn_design_characters(gs, *wStore_, terrain, rng);
    // Обычный сквад рядом — негативный контроль на −1. Через ту же одну
    // дверь создания (spawn_squad → make_npc).
    SquadSpec plain{};
    plain.avatarType = NPCType::Bandit;
    plain.x = 30;
    plain.y = 30;
    plain.factionIndex = faction_index("bandits");
    CHECK_OR_RETURN(wStore_->valid(
                        spawn_squad(gs, *wStore_, terrain, plain)),
                    "the plain control squad spawned");

    // АНКЕТА СТОЛА ЕДЕТ КОЛОНКОЙ СТОРА: сейв есть прямой дамп гладкой памяти
    // (M-439), поэтому вопрос «несёт ли запись строку стола» задаётся самой
    // колонке `designTag`, а «пережила ли она загрузку» — её байтам.
    int designSlots = 0, plainSlots = 0;
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (wStore_->alive[s] == 0) continue;
        const int ord = wStore_->designTag[s].ordinal;
        if (ord >= 0) {
            ++designSlots;
            CHECK(ord == 0, "the column names the table row");
        } else {
            ++plainSlots;
        }
    }
    CHECK(designSlots == 1, "exactly one design tag in the store");
    CHECK(plainSlots >= 1,
          "negative control: the plain squad rides with -1");

    // Загрузка кладёт байты стора как они есть — ровно это копирование и
    // делает дверь загрузки игры (`boot_world_from_save@src/app/main.cpp`).
    auto w2Store_ = sm::make_macro_store();
    *w2Store_ = *wStore_;
    const sm::MacroHandle back = find_design(*w2Store_, 0);
    CHECK_OR_RETURN(w2Store_->valid(back),
                    "the design tag rode the store's bytes");
    CHECK(sm::level_of(w2Store_->sheet[back.slot].levelData)
              == int(kDesignCharacterDefs[0].level),
          "...и уровень его листа вернулся С НИМ — колонка анкеты едет в "
          "сейв, опт-ина нет с M-246а");
}

void test_king_peasant_births_by_home_faction() {
    // Мир с ВАРВАРСКИМ городом: город несёт фракцию barbarian_north СВОЕЙ
    // колонкой (королевства вырезаны 2026-09-11) — и фикстурные
    // freefolk-города рядом, чтобы фильтр префикса был утверждением, а не
    // единственностью.
    const TerrainData terrain = make_terrain();
    auto wStore_ = sm::make_macro_store();
    std::unique_ptr<GameState> gsOwn = make_world(*wStore_);  // freefolk-города хватает Варнаве
    GameState& gs = *gsOwn;
    const sm::MacroHandle barb =
        settle(gs, *wStore_, SquadType::City, 50, 20,
               std::int16_t(faction_index("barbarian_north")));
    const int barbId = int(wStore_->spawnId[barb.slot].index);
    Rng rng(555u);
    spawn_design_characters(gs, *wStore_, terrain, rng);

    sm::MacroStore& stk = *wStore_;
    const sm::MacroHandle king = find_design(stk, 1);
    CHECK_OR_RETURN(stk.valid(king),
                    "the king row became one body near the barbarian city");
    CHECK(sm::parent_of(stk, king.slot) == barbId,
          "his home is the BARBARIAN city, not the freefolk one — the "
          "faction-prefix filter picked the row's home");
    // Фракция — ОН САМ (вердикт владельца): своя строка одной матрицы,
    // как у игрока, а не знамя города, где он живёт.
    CHECK(int(stk.kind[king.slot].factionIdx)
              == faction_index("king_peasant"),
          "his faction is his OWN registry row");
    CHECK(sm::level_of(stk.sheet[king.slot].levelData) == 70,
          "the level-70 roll reached the sheet column");
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
    auto wStore_ = sm::make_macro_store();
    std::unique_ptr<GameState> gsOwn = make_world(*wStore_);
    GameState& gs = *gsOwn;
    const TerrainData terrain = make_terrain();
    Rng rng(556u);
    spawn_design_characters(gs, *wStore_, terrain, rng);
    CHECK(wStore_->valid(find_design(*wStore_, 0)),
          "Varnava is born in a world without barbarians");
    CHECK(!wStore_->valid(find_design(*wStore_, 1)),
          "the king is honestly NOT born without a barbarian city");
}

void test_dragons_nest_on_mountain_peaks() {
    // Мир с ГОРНЫМ МАССИВОМ: пятно высоты 0.902 (выше kMountainBiomeLevel)
    // с вершиной 0.980 в (48,48) — и плоская равнина вокруг. Плоские фикстуры
    // остальных тестов драконов честно НЕ рождают (порог биома).
    // Уровни, а не байты: прежние 250/230 канала означали ровно эти доли.
    constexpr float kPeak01 = 250.0f / 255.0f;
    constexpr float kMassif01 = 230.0f / 255.0f;
    auto wStore_ = sm::make_macro_store();
    std::unique_ptr<GameState> gsOwn = make_world(*wStore_);
    GameState& gs = *gsOwn;
    TerrainData terrain = make_terrain();
    for (int y = 44; y <= 52; ++y) {
        for (int x = 44; x <= 52; ++x) {
            terrain.rgba[(std::size_t(y) * kW + x) * 4u + 0] =
                sm::field_word_of(x == 48 && y == 48 ? kPeak01 : kMassif01);
        }
    }
    Rng rng(999u);
    spawn_design_characters(gs, *wStore_, terrain, rng);

    // Один массив = одна вершина: Dragon1 рождается, №2/№3 (вершины с
    // разносом ≥ mapW/8) — честно нет.
    const sm::MacroHandle d1 = find_design(*wStore_, 2);
    sm::MacroStore& std_ = *wStore_;
    CHECK_OR_RETURN(std_.valid(d1), "Dragon1 nests on the one massif");
    CHECK(!std_.valid(find_design(std_, 3)) && !std_.valid(find_design(std_, 4)),
          "one massif births one dragon — spacing is honest");

    const auto& rt = std_.runtime[d1.slot];
    CHECK(sm::dragon_lair(rt) == sm::ecs::cell_index(48, 48, kW),
          "his LAIR is the massif's highest cell (one number, M-3)");
    CHECK(rt.flying == 1,
          "the row's cruiseM cached as the march-side flying byte (v93)");
    CHECK(int(std_.kind[d1.slot].factionIdx)
              == faction_index("dragons"),
          "dragons share the ONE dragons faction row (owner verdict)");
    // Дракон идёт ТОЙ ЖЕ колонкой листа, что всякий авторский, и ожидание
    // выводится из ЕГО строки каталога (§8 п.4: литерал тут ломался бы на
    // каждой перекалибровке контента).
    CHECK(sm::level_of(std_.sheet[d1.slot].levelData)
              == int(kDesignCharacterDefs[2].level),
          "уровень строки дракона дошёл до колонки листа — та же дверь, что "
          "у проповедника и короля");
    // РОД СКВАДА — АВТОРСКАЯ КОЛОНКА СТРОКИ (ломоть 6 M-72): дракон думает
    // модулем строки Dragon таблицы kAiModuleRows, а не лестницей None.
    CHECK(SquadType(std_.runtime[d1.slot].squadType) == SquadType::Dragon,
          "the spawn door writes the row's squad kind: the dragon thinks "
          "as the Dragon MODULE row, not the None ladder");
    CHECK(kDesignCharacterDefs[2].type == SquadType::Dragon
              && kDesignCharacterDefs[0].type == SquadType::None,
          "negative control: kind is a ROW column — the untyped preacher "
          "honestly stays None (the ladder is still his path)");
}

// ПРИБОР ЭМЕРДЖЕНТНОСТИ ЭПИКА M-72 (владелец: «будет ли он есть крестьян
// и тд эмерджентно») — модуль дракона не знает ни крестьян, ни деревень:
// добычу называет сила (nearest_weaker_squad), смерть — авто-бой, и ни
// одной строки, связывающей ai_dragon с модулем жертвы, не существует.
// Свидетель рожает СВОЁ предусловие (§8 п.11): массив с вершиной — дракону,
// слабый крестьянский сквад на клетке логова — в добычу.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (§8 п.6, прогнана вживую 2026-11-02):
//   тело ai_dragon → `return;` первой строкой ⇒ КРАСНЫЙ 1/61
//                                               («the dragon ate…»)
//   спавнер не пишет row.type в squadType     ⇒ КРАСНЫЙ 2/61 (колонка рода
//                                               + прибор: None-бродяга)
//   kDragonLeashCells 10 → 8                  ⇒ ЗЕЛЁНЫЙ 61/61 (добыча на
//                                               логове — внутри любого
//                                               поводка; детектор не красит
//                                               невиновного)
void test_dragon_eats_peasants_emergently() {
    constexpr float kPeak01 = 250.0f / 255.0f;
    constexpr float kMassif01 = 230.0f / 255.0f;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    std::unique_ptr<GameState> gsOwn = make_world(st);
    GameState& gs = *gsOwn;
    TerrainData terrain = make_terrain();
    for (int y = 44; y <= 52; ++y)
        for (int x = 44; x <= 52; ++x)
            terrain.rgba[(std::size_t(y) * kW + x) * 4u + 0] =
                sm::field_word_of(x == 48 && y == 48 ? kPeak01 : kMassif01);
    Rng rng(999u);
    spawn_design_characters(gs, st, terrain, rng);
    const sm::MacroHandle dragon = find_design(st, 2);
    CHECK_OR_RETURN(st.valid(dragon), "the massif births Dragon1");

    // Добыча — уровень 1, без ростера: слабее дракона по squad_power
    // ЗАВЕДОМО; стоит на клетке логова, внутри любого поводка.
    SquadSpec spec;
    spec.avatarType = NPCType::Peasant;
    spec.avatarLevel = 1;
    spec.x = 48;
    spec.y = 48;
    spec.factionIndex = faction_index("timaert");
    const sm::MacroHandle prey = spawn_squad(gs, st, terrain, spec);
    CHECK_OR_RETURN(st.valid(prey), "the prey squad is born");

    MacroNpcAiRuntime air{};
    reset_macro_npc_ai_runtime(air, 7u);
    // Срок — с запасом и до события: свидетель судит ЗАКОН (дракон съел
    // слабого), а не темп полёта. Здесь стояло ровно 16 думок — число,
    // прибитое к прежней цене диагонали (дракон летит к логову наискось, и с
    // парой 2 и 3 тот же путь занимает семнадцать думок, M-463).
    for (int i = 0; i < 64 && st.valid(prey) && st.dead[prey.slot] == 0; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, air, kAiTicks);
    }
    CHECK(!st.valid(prey) || st.dead[prey.slot] != 0,
          "the dragon ate the peasant — no line names the peasant in "
          "ai_dragon, the interaction is emergent (epic M-72 acceptance)");
    CHECK(st.valid(dragon) && st.dead[dragon.slot] == 0,
          "the dragon survives his own meal");

    // Негативный контроль, и он УТВЕРЖДАЕТСЯ: плоский мир дракона не
    // рождает, и та же добыча те же думки живёт.
    auto flatStore_ = sm::make_macro_store();
    sm::MacroStore& fst = *flatStore_;
    std::unique_ptr<GameState> fgsOwn = make_world(fst);
    GameState& fgs = *fgsOwn;
    const TerrainData flat = make_terrain();
    Rng frng(999u);
    spawn_design_characters(fgs, fst, flat, frng);
    CHECK(!fst.valid(find_design(fst, 2)),
          "flat world: no dragon (precondition of the control is real)");
    SquadSpec fspec = spec;
    const sm::MacroHandle fprey = spawn_squad(fgs, fst, flat, fspec);
    CHECK_OR_RETURN(fst.valid(fprey), "control prey is born");
    MacroNpcAiRuntime fair{};
    reset_macro_npc_ai_runtime(fair, 7u);
    for (int i = 0; i < 16; ++i) {
        MacroWorld mw{.gs = &fgs, .store = &fst};
        tick_macro_npc_ai(mw, fair, kAiTicks);
    }
    CHECK(fst.valid(fprey) && fst.dead[fprey.slot] == 0,
          "no dragon — the same peasant lives the same thinks: the death "
          "above was the dragon's doing, not the world's");
}

void test_no_home_no_birth() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData terrain = make_terrain();
    auto wStore_ = sm::make_macro_store();
    Rng rng(42u);
    spawn_design_characters(gs, *wStore_, terrain, rng);
    int tags = 0;
    for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32)
        if (wStore_->alive[s32] != 0
            && wStore_->designTag[s32].ordinal >= 0)
            ++tags;
    CHECK(tags == 0, "a world without the row's home births nobody");
}

} // namespace

int main() {
    test_table_rows_resolve();
    test_spawn_births_the_row();
    test_store_carries_the_ordinal();
    test_king_peasant_births_by_home_faction();
    test_king_needs_a_barbarian_city();
    test_dragons_nest_on_mountain_peaks();
    test_dragon_eats_peasants_emergently();
    test_no_home_no_birth();
    return sm::test::report("design_characters_test");
}
