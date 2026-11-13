// ТИПЫ АРТЕЛЕЙ: СЫРЬЁ ТЯНЕТ МОДУЛЬ ДЕРЕВНИ, У КАЖДОГО ТИПА СВОЙ МОДУЛЬ (M-488).
//
// Замысел — вердикт владельца 2026-11-13 (CANON S25, блок «СЫРЬЁ ТЯНЕТ МОДУЛЬ
// ДЕРЕВНИ ТИПАМИ АРТЕЛЕЙ»): «деревне чтото надо она создаёт артель этого типа
// и она идёт по своему модулю (и для фолбека … артель может менять свой тип
// на другой (внутри модуля логика пишется)». Прибор держит пять законов, по
// одному на тест; каждый судится ФАКТОМ МИРА (тип ребёнка, смена типа, нули
// памяти, груз ушёл дому, еда пришла не больше счёта), а не числом:
//   З1. деревня поднимает артель НУЖНОГО типа — товар в нужде (еда) либо
//       сырьё вещи с неоплаченным счётом, когда руки её умеют и сырья на
//       полке меньше партии; место работы обязано быть; по одной на тип и
//       одной за круг; живая неопределённая новых не пускает; без нужды —
//       никого;
//   З2. артель типа делает своё дело: к месту — акт двери акта (цена SP и
//       восстановление) — домой — ВСЁ добытое дому сделкой, и уходит снова
//       той же думкой (паузы после сдачи нет);
//   З3. тип становится неопределённым ДОМА, когда места работы нет или дом
//       ценит товар по полу; память модуля при смене занулена;
//   З4. неопределённая раз в сутки становится тем типом, чей груз одного
//       выхода дом ценит выше всего, — место есть, оценка выше пола, такой
//       сестры у дома нет; иначе остаётся собой;
//   З5. харч: дом грузит едой всякого своего ребёнка, стоящего у порога без
//       дела (тип и трюм не спрашиваются), не больше его счёта и только пока
//       он ценит единицу выше дома; артель ждёт погрузки, только если дом еду
//       даст.
// И МЕСТО РАБОТЫ ЛЕСОРУБОВ — ЖИВОЙ ЛЕС: точка сетки деревьев на клетке с
// одними пнями местом работы не является (З1–З3 на пнях).
//
// ПРЕДУСЛОВИЯ ПРИБОР РОЖАЕТ СЕБЕ САМ (§8 п.11): пашня ставится дверью фичи
// на фертильную клетку, лес — счётом слоя и точкой сетки деревьев, нужда —
// счётом места, артель — тем же набором колонок, каким её рождает мир. Что
// поле ЖИВО, утверждается дверью ряда до первой думки.
//
// ДОМ «ЗАНЯТ РЕМЕСЛОМ» ТАМ, ГДЕ ПРЕДМЕТ — НЕ КРАФТ. Деревня крафтит в той же
// думке, где решает о рождении и харче, и съела бы сырьё, на котором стоит
// проверка (полку дерева, завал товара). Висящее восстановление — законное
// состояние сквада после долгого акта (ЗАКОН СПОСОБНОСТИ), и решению о
// рождении и харче оно не мешает: рождение не способность. Так закон
// проверяется сам по себе, а не в паре с решением о крафте.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (§8 п.6; дерево ломтя 2 ряда M-488 с правками
// леса и харча, собирался только этот таргет, 224 проверки):
//   З1  village_think: снят гейт `has_child(s.h, t)`      → КРАСНЫЙ 2
//       (второй круг поднял вторых пахарей вместо лесорубов)
//   З1  village_think: снят гейт живой неопределённой    → КРАСНЫЙ 2
//   З1  village_think: `wants` без правила сырья вещи     → КРАСНЫЙ 3
//   З2  woodcutters_think: снят `pult.hand`               → КРАСНЫЙ 4
//   З3  woodcutters_think: снято зануление `s.rt.mem`     → КРАСНЫЙ 3
//   З3  woodcutters_think: снято условие пола             → КРАСНЫЙ 2
//   З4  artel_think: снят `has_sibling`                   → КРАСНЫЙ 1
//   З4  artel_think: снято условие пола                   → КРАСНЫЙ 1
//       (первая редакция плеча судила конец прогона и прошла ЗЕЛЁНОЙ: без
//       пола тип качается артель ↔ пахари каждую думку, и чётная думка
//       видела «собой»; теперь судится каждый круг решения)
//   З4  artel_think: снято зануление памяти при смене     → КРАСНЫЙ 1
//   З5  village_think: кап `-theirs.net` снят             → КРАСНЫЙ 4
//   З5  woodcutters_think: снято «жду погрузки харча»     → КРАСНЫЙ 1
//   лес find_nearest_tree_grid: снята проверка живого поля → КРАСНЫЙ 6
//       (к пням вместо живого леса; пни держат тип; деревня поднимает к пням)
//   харч each_child_here: суд над трюмом (масса > 0)      → КРАСНЫЙ 2
//   харч each_child_here: обход оборван после первого     → КРАСНЫЙ 1
//   МОЛЧИТ  artel_think: обратный порядок `consider`      → ЗЕЛЁНЫЙ (выбор
//           судится оценкой груза, а не порядком строк)
#include "check.h"
#include <memory>

#include "macro/npc_ai.h"
#include "macro/birth_squad.h"   // birth_squad — деревня родится ТЕЛОМ
#include "macro/features.h"      // feature_put — пашня ставится дверью мира
#include "macro/labour.h"        // settle_souls — души в контейнер места
#include "macro/deal.h"          // side_row — строка стороны глазами ядра
#include "macro/economy.h"       // worth_arriving — оценка единицы стороной
#include "macro/econ_day.h"      // hunger_commodity_ordinal, рецепты
#include "macro/squad.h"         // carry_capacity_g — спина стороны
#include "macro/settlement_score.h"  // kSettlementReach — бокс округи дома
#include "macro/chronicle.h"
#include "macro/resource_field.h"
#include "macro/spawners.h"      // TreePoint — точка сетки деревьев
#include "macro/tree_layer.h"
#include "bus/store.h"           // parent_of / set_relation
#include "bus/world_row.h"       // count_human_souls, raise_flock_into_container
#include "tables/faction.h"
#include "tables/npc.h"
#include "core/time.h"

#include <cstdint>
#include <vector>

namespace {

using namespace sm;

constexpr int kMap = 32;
constexpr int kHomeX = 10, kHomeY = 10;
// Места работы — внутри бокса округи дома (`kSettlementReach`), и НЕ на
// клетке самой деревни: её число есть паства.
constexpr int kFieldX = 12, kFieldY = 10;
constexpr int kForestX = 14, kForestY = 10;

constexpr int kThinksPerDay = int(kAiThinksPerDay);

// Суша под всей картой (без неё дверь рождения и дверь фичи отказывают).
TerrainData all_land() {
    TerrainData td;
    td.width = kMap;
    td.height = kMap;
    td.rgba.assign(std::size_t(kMap) * std::size_t(kMap) * 4u, 0);
    td.seaLevel16 = field_word_of(kDefaultSeaLevel);
    for (std::size_t c = 0; c < std::size_t(kMap) * std::size_t(kMap); ++c)
        td.rgba[c * 4u + 0] = field_word_of(140.0f / 255.0f);
    return td;
}

int food_item() { return commodity_item_index(hunger_commodity_ordinal()); }

bool is_artel_type(std::uint8_t t) {
    return t >= std::uint8_t(SquadType::Artel)
        && t <= std::uint8_t(SquadType::Woodcutters);
}

struct Fixture {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    TerrainData terrain = all_land();
    std::vector<TreePoint> treePts;
    TreeGrid grid;
    std::unique_ptr<MacroStore> storeOwner = make_macro_store();
    MacroNpcAiRuntime ai{};

    Fixture() {
        gs.mapW = kMap;
        gs.mapH = kMap;
        chronicle_init(gs.chronicle, kMap, kMap);
        tree_layer_reset(gs.treeLayer, kMap, kMap);
        gs.features.resize(kMap, kMap);
        build_tree_grid(grid, treePts, kMap, kMap);
        reset_macro_npc_ai_runtime(ai, 88u);
    }
    MacroStore& st() { return *storeOwner; }
    MacroWorld world() {
        return MacroWorld{.gs = &gs, .store = storeOwner.get(),
                          .terrain = &terrain, .treeGrid = &grid};
    }
    // Одна думка всякого сквада: `kAiTicks` честных тиков (фаза каждого
    // копится тиками, созревшая думает ровно раз).
    void think(int times = 1) {
        for (int i = 0; i < times; ++i) {
            MacroWorld mw = world();
            tick_macro_npc_ai(mw, ai, kAiTicks);
        }
    }

    // ДЕРЕВНЯ. `busy` вешает ей восстановление долгого акта — см. шапку.
    MacroHandle village(int souls, bool busy) {
        const MacroHandle h = birth_squad(
            gs, nullptr, st(), SquadType::Village, kHomeX, kHomeY,
            std::int16_t(faction_index("timaert")), nullptr, &terrain);
        CHECK(st().valid(h), "деревня родилась");
        CHECK(settle_souls(gs, st(), h.slot, souls) == souls,
              "все души деревни встали в её контейнер");
        if (busy) st().runtime[h.slot].recoverySteps = 0xFFFF'FFFFu;
        return h;
    }
    int id_of(MacroHandle h) { return int(st().spawnId[h.slot].index); }

    // ПАШНЯ: фича дверью мира на клетку с полной фертильностью (канал G —
    // ёмкость ряда пашни), и ряд обязан ответить живым урожаем.
    void plough(int x, int y) {
        const std::uint32_t c = cell_of(x, y, kMap);
        terrain.rgba[std::size_t(c) * 4u + 1u] = std::uint16_t(kFieldWordMax);
        CHECK(feature_put(gs.features, terrain, c, FT_Field) == FT_Field,
              "пашня встала дверью мира");
        const MacroWorld mw = world();
        CHECK(resource_field_read(mw, ResourceFieldId::Wheat, c) > 0,
              "на пашне стоит урожай — место работы пахарей живое");
    }
    // ЛЕС: счёт слоя и точка сетки деревьев (её спрашивает поиск леса).
    void forest(int x, int y, int trees) {
        const std::uint32_t c = cell_of(x, y, kMap);
        set_tree_count(gs.treeLayer, c, trees);
        treePts.push_back(TreePoint{x, y});
        build_tree_grid(grid, treePts, kMap, kMap);
        const MacroWorld mw = world();
        CHECK(resource_field_read(mw, ResourceFieldId::Trees, c) == trees,
              "лес встал счётом слоя — место работы лесорубов живое");
    }
    // ПНИ: точка сетки деревьев есть (генерация её поставила), а живое поле
    // клетки — ноль: лес срублен дочиста.
    void stumps(int x, int y) {
        const std::uint32_t c = cell_of(x, y, kMap);
        set_tree_count(gs.treeLayer, c, 0);
        treePts.push_back(TreePoint{x, y});
        build_tree_grid(grid, treePts, kMap, kMap);
        const MacroWorld mw = world();
        CHECK(resource_field_read(mw, ResourceFieldId::Trees, c) == 0,
              "ПРЕДУСЛОВИЕ: точка сетки стоит, а на клетке одни пни");
    }

    // АРТЕЛЬ — ребёнок дома, тем же набором колонок, каким её рождает мир:
    // анкета, бар, тип, ребро `Parent`, крю душами в контейнере.
    MacroHandle artel(SquadType t, int x, int y, int parentId, int crew) {
        const MacroHandle h = store_birth(st());
        st().cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
        st().runtime[h.slot].sub = {};
        st().kind[h.slot] = ecs::NPCKind{
            std::uint16_t(NPCType::Peasant),
            std::uint16_t(faction_index("timaert"))};
        ecs::MacroNpcRuntime rt{};
        rt.state = std::uint8_t(NPCState::Idle);
        const CharacterSheet sheet = make_character_sheet(
            NPCType::Peasant, 3,
            avatar_sheet_seed(st().spawnId[h.slot].index));
        ecs::Pools pools{};
        refresh_body_from_sheet(pools, &rt, sheet, NPCType::Peasant);
        // Дверь бара мёртвого не воскрешает (ноль hp остаётся нулём), и
        // нулевой бар новорождённого — значение, которое ставит фикстура:
        // живой сквад рождается с полными барами.
        pools.hp = pools.maxHp;
        pools.mp = pools.maxMp;
        pools.sp = pools.maxSp;
        CHECK(pools.hp > 0 && pools.sp > 0,
              "артель рождена живой и с полной планкой");
        rt.squadType = std::uint8_t(t);
        st().runtime[h.slot] = rt;
        st().sheet[h.slot] = sheet;
        st().pools[h.slot] = pools;
        set_relation(st(), int(st().spawnId[h.slot].index), parentId,
                     Stance::Parent);
        CHECK(raise_flock_into_container(st().inventory[h.slot].inv, crew)
                  == crew,
              "крю артели встало душами в её контейнер");
        CHECK(parent_of(st(), h.slot) == parentId,
              "дом артели назван ребром `Parent`");
        return h;
    }

    int heads(std::uint16_t slot) {
        return count_human_souls(st().inventory[slot].inv);
    }
    int children_of(int parentId, SquadType t) {
        int n = 0;
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (st().alive[s] == 0 || st().dead[s] != 0) continue;
            if (st().runtime[s].squadType != std::uint8_t(t)) continue;
            if (parent_of(st(), std::uint16_t(s)) == parentId) ++n;
        }
        return n;
    }
    int artels_of(int parentId) {
        int n = 0;
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (st().alive[s] == 0 || st().dead[s] != 0) continue;
            if (!is_artel_type(st().runtime[s].squadType)) continue;
            if (parent_of(st(), std::uint16_t(s)) == parentId) ++n;
        }
        return n;
    }
    // Во сколько дом ценит ПРИШЕДШУЮ единицу строки — той же дверью
    // стороны, какой её берёт сделка (`side_row@src/macro/deal.h`).
    std::int64_t home_worth_of_one(MacroHandle home, int itemIdx) {
        const ItemDef* d = item_def_at(itemIdx);
        return worth_arriving(
            side_row(st(), home.slot, itemIdx, d ? d->value : 0), 1);
    }
    // ЗАВАЛ ДОМА: на полку ложится вдвое больше единиц строки, чем вмещает
    // его спина, — оценка пришедшей единицы падает до пола (ядро оценки:
    // излишек сверх спины стоит единицу). Число выводится спиной и массой
    // строки, а не пишется с потолка.
    void flood(MacroHandle home, int itemIdx) {
        Inventory& shelf = st().inventory[home.slot].inv;
        const std::int64_t back =
            carry_capacity_g(st().runtime[home.slot], shelf);
        const std::int64_t unitG =
            std::max<std::int64_t>(1, unit_mass_g(item_def_at(itemIdx)));
        const int n = int(std::min<std::int64_t>(2 * back / unitG + 2,
                                                 1 << 28));
        CHECK(shelf.add_of(itemIdx, n), "завал лёг на полку дома");
        CHECK(home_worth_of_one(home, itemIdx) <= 1,
              "ПРЕДУСЛОВИЕ: дом ценит пришедшую единицу по полу");
    }
};

bool mem_is_zero(const ecs::MacroNpcRuntime& rt) {
    for (const std::uint32_t w : rt.mem)
        if (w != 0) return false;
    return true;
}
void fill_mem(ecs::MacroNpcRuntime& rt) {
    for (std::uint32_t& w : rt.mem) w = 0xA5A5'0001u;
}

// ═════ З1. ДЕРЕВНЯ ПОДНИМАЕТ АРТЕЛЬ НУЖНОГО ТИПА ═════════════════════════
void test_village_raises_the_type_its_need_names() {
    const int foodCom = hunger_commodity_ordinal();
    const int woodIdx = item_index("wood");
    const int furnitureIdx = item_index("furniture");
    const int furnitureCom = commodity_index("furniture");
    CHECK(foodCom >= 0 && woodIdx >= 0 && furnitureIdx >= 0
              && furnitureCom >= 0,
          "словарь знает еду, дерево и мебель");
    // Сколько дерева уходит на ОДНУ партию мебели — из авторской строки
    // состава, а не литералом.
    int woodPerBatch = 0;
    for (const ItemPart& part : item_parts(furnitureIdx))
        if (int(part.def) == woodIdx) woodPerBatch = int(part.count);
    CHECK(woodPerBatch > 0, "дерево — сырьё мебели по строке состава");

    // ── БЕЗ НУЖДЫ — НИКОГО, хотя пашня и лес у дома есть ──────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        f.plough(kFieldX, kFieldY);
        f.forest(kForestX, kForestY, 64);
        f.think(2 * kThinksPerDay + 2);
        CHECK(f.artels_of(f.id_of(vil)) == 0,
              "БЕЗ НУЖДЫ АРТЕЛИ НЕТ: двое суток, места работы есть, нужды нет");
        CHECK(f.heads(vil.slot) == 64, "и души дома все на месте");
    }

    // ── ЕДА В НУЖДЕ → ПАХАРИ; ПО ОДНОЙ ЗА КРУГ; ПО ОДНОЙ НА ТИП ─────────
    // Нужны и еда, и мебель (сырьё — дерево, на полке его нет): деревне
    // надо ДВА типа, и законы «одна за круг» и «одна на тип» видны только
    // тут — первый круг поднимает одну артель, второй — вторую, а не
    // дубль первой.
    {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        const int vilId = f.id_of(vil);
        f.plough(kFieldX, kFieldY);
        f.forest(kForestX, kForestY, 4096);
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        f.st().upkeep[vil.slot].needDebt[furnitureCom] = 64;
        f.think(1);
        CHECK(f.artels_of(vilId) == 1
                  && f.children_of(vilId, SquadType::Ploughmen) == 1,
              "ПЕРВЫЙ КРУГ — ОДНА АРТЕЛЬ, И ЭТО ПАХАРИ: еда в нужде, пашня "
              "у дома есть");
        f.think(kThinksPerDay + 1);
        CHECK(f.children_of(vilId, SquadType::Ploughmen) == 1
                  && f.children_of(vilId, SquadType::Woodcutters) == 1
                  && f.artels_of(vilId) == 2,
              "ВТОРОЙ КРУГ — ЛЕСОРУБЫ, А НЕ ВТОРЫЕ ПАХАРИ: мебель в нужде, "
              "дерева на полке нет, и тип, что уже есть, не дублируется");
        f.think(kThinksPerDay + 1);
        CHECK(f.artels_of(vilId) == 2,
              "ТРЕТИЙ КРУГ — НИКОГО НОВОГО: оба нужных типа живы");
    }

    // ── СЫРЬЁ ВЕЩИ: ЛЕСОРУБЫ ТОЛЬКО ПОКА ДЕРЕВА МЕНЬШЕ ПАРТИИ ─────────
    for (const bool shelfHasBatch : {false, true}) {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        const int vilId = f.id_of(vil);
        f.forest(kForestX, kForestY, 4096);
        // Руки деревни умеют мебель — право спрашивается той же парой
        // «ремесло + минранг» строки действия рецепта, что у двери акта.
        const ActionDef& act =
            action_def(int(kRecipes[recipe_of_item(furnitureIdx)].action));
        CHECK(recipe_known(f.st().sheet[vil.slot].skills, act.gateSkill,
                           int(act.minRank)),
              "ПРЕДУСЛОВИЕ: руки деревни умеют мебель");
        f.st().upkeep[vil.slot].needDebt[furnitureCom] = 64;
        if (shelfHasBatch) {
            CHECK(f.st().inventory[vil.slot].inv.add_of(woodIdx,
                                                        woodPerBatch),
                  "на полку легла ровно одна партия дерева");
        }
        // Судится ПЕРВЫЙ круг: дальше лесорубы вправе завалить занятый дом
        // деревом и стать неопределёнными (З3) — это уже не рождение.
        f.think(1);
        if (!shelfHasBatch) {
            CHECK(f.children_of(vilId, SquadType::Woodcutters) == 1
                      && f.artels_of(vilId) == 1,
                  "СЫРЬЁ ВЕЩИ С НЕОПЛАЧЕННЫМ СЧЁТОМ ТЯНЕТ ЛЕСОРУБОВ: мебель "
                  "нужна, руки её умеют, дерева на полке нет");
        } else {
            f.think(kThinksPerDay + 1);
            CHECK(f.artels_of(vilId) == 0,
                  "ПАРТИЯ НА ПОЛКЕ ЕСТЬ — ЛЕСОРУБОВ НЕ ПОДНИМАЮТ ни в первый, "
                  "ни во второй круг: сырьё тянут, только когда его меньше "
                  "одной партии");
        }
    }

    // ── МЕСТА РАБОТЫ НЕТ — НИКОГО, ДАЖЕ ПРИ НУЖДЕ ───────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        const MacroWorld mw = f.world();
        bool faunaBare = true;
        const std::uint32_t home = f.st().cell[vil.slot].idx;
        for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy)
            for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx)
                faunaBare = faunaBare
                    && resource_field_read(mw, ResourceFieldId::Fauna,
                                           cell_step(home, dx, dy, kMap))
                           == 0;
        CHECK(faunaBare, "ПРЕДУСЛОВИЕ: зверя в округе дома нет");
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        f.think(2 * kThinksPerDay + 2);
        CHECK(f.artels_of(f.id_of(vil)) == 0,
              "МЕСТА РАБОТЫ НЕТ — АРТЕЛИ НЕТ: еда нужна, но ни пашни, ни "
              "зверя, ни леса у дома нет");
        CHECK(f.heads(vil.slot) == 64, "и души дома все на месте");
    }

    // ── ЖИВАЯ НЕОПРЕДЕЛЁННАЯ АРТЕЛЬ НОВОЙ НЕ ПУСКАЕТ ────────────────────
    // Обе половины в одной форме: неопределённая стоит НЕ дома (идёт
    // домой и решить, кем стать, в этот круг не успевает), а без неё тот же
    // мир в тот же круг поднимает пахарей.
    for (const bool withUndetermined : {true, false}) {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        const int vilId = f.id_of(vil);
        f.plough(kFieldX, kFieldY);
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        if (withUndetermined)
            f.artel(SquadType::Artel, kHomeX + 3, kHomeY, vilId,
                    kVillageCrewSouls - 1);
        f.think(1);
        if (withUndetermined) {
            CHECK(f.children_of(vilId, SquadType::Ploughmen) == 0
                      && f.artels_of(vilId) == 1,
                  "НЕОПРЕДЕЛЁННАЯ ЖИВА — НОВОЙ АРТЕЛИ НЕТ: она сама возьмёт "
                  "нужду");
        } else {
            CHECK(f.children_of(vilId, SquadType::Ploughmen) == 1,
                  "КОНТРОЛЬ: без неё тот же мир в тот же круг поднял пахарей");
        }
    }
}

// ═════ З2. АРТЕЛЬ ТИПА ДЕЛАЕТ СВОЁ ДЕЛО ══════════════════════════════════
void test_woodcutters_fell_haul_and_hand_everything_home() {
    Fixture f;
    // ДОМ БОЛЬШОЙ, ЧТОБЫ ОДИН ГРУЗ ЕГО НЕ ЗАВАЛИЛ: спина места растёт его
    // душами, и занятый дом в сорок душ один рейс лесорубов доводит до пола —
    // а закон «завален — стань неопределённой» судится в З3, не здесь.
    const MacroHandle vil = f.village(400, /*busy*/true);
    const int vilId = f.id_of(vil);
    constexpr int kTrees = 4096;
    f.forest(kForestX, kForestY, kTrees);
    const std::uint32_t site = cell_of(kForestX, kForestY, kMap);
    const std::uint32_t home = f.st().cell[vil.slot].idx;
    const MacroHandle wc =
        f.artel(SquadType::Woodcutters, kHomeX, kHomeY, vilId, 7);
    const int woodIdx = item_index("wood");
    Inventory& bag = f.st().inventory[wc.slot].inv;
    Inventory& shelf = f.st().inventory[vil.slot].inv;
    CHECK(shelf.count_of(woodIdx) == 0, "дерева дома до первой думки нет");

    bool sawAct = false, actHung = false, actPaid = false;
    bool sawHandOver = false, handExact = false, leftAtOnce = false;
    for (int i = 0; i < 8 * kThinksPerDay && !sawHandOver; ++i) {
        const int treesBefore = int(f.gs.treeLayer.at(site));
        const int bagBefore = bag.count_of(woodIdx);
        const int shelfBefore = shelf.count_of(woodIdx);
        const int spBefore = int(f.st().pools[wc.slot].sp);
        const bool homeBefore = f.st().cell[wc.slot].idx == home;
        f.think(1);
        const int felled = treesBefore - int(f.gs.treeLayer.at(site));
        if (!sawAct && felled > 0) {
            sawAct = true;
            actHung = f.st().runtime[wc.slot].recoverySteps > 0;
            actPaid = int(f.st().pools[wc.slot].sp) < spBefore;
        }
        // Сдача — думка, НАЧАТАЯ дома с грузом: та же думка и уводит артель
        // (ноги идут после модуля), поэтому «дома ли» судится по входу.
        if (homeBefore && bagBefore > 0 && bag.count_of(woodIdx) == 0) {
            sawHandOver = true;
            handExact = shelf.count_of(woodIdx) - shelfBefore == bagBefore;
            const std::uint8_t state = f.st().runtime[wc.slot].state;
            leftAtOnce = f.st().goal[wc.slot].cell == site
                && (state == std::uint8_t(NPCState::Traveling)
                    || state == std::uint8_t(NPCState::Working));
        }
    }
    CHECK(sawAct, "лесорубы ДОБЫЛИ: счёт леса на месте работы упал");
    CHECK(actHung,
          "АКТ ПОВЕСИЛ ВОССТАНОВЛЕНИЕ — его вешает дверь акта, а не модуль");
    CHECK(actPaid, "И АКТ ЗАПЛАЧЕН SP — бар после акта ниже, чем до");
    CHECK(sawHandOver, "лесорубы вернулись домой с грузом и сдали его");
    CHECK(handExact,
          "ВСЁ ДОБЫТОЕ УШЛО ДОМУ СДЕЛКОЙ: полка выросла ровно на груз спины");
    CHECK(leftAtOnce,
          "ПАУЗЫ ПОСЛЕ СДАЧИ НЕТ: той же думкой лесорубы снова в пути к лесу");
    const int felledTotal = kTrees - int(f.gs.treeLayer.at(site));
    CHECK(felledTotal == shelf.count_of(woodIdx) + bag.count_of(woodIdx),
          "СОХРАНЕНИЕ: лес потерял ровно то, что лежит дома и в спине");
    CHECK(f.st().runtime[wc.slot].squadType
              == std::uint8_t(SquadType::Woodcutters),
          "и тип у работающих лесорубов не сменился");
}

// ═════ З3. ТИП СТАНОВИТСЯ НЕОПРЕДЕЛЁННЫМ ДОМА ════════════════════════════
void test_typed_artel_turns_undetermined_at_home() {
    const int woodIdx = item_index("wood");
    // ── МЕСТА РАБОТЫ НЕТ ─────────────────────────────────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        fill_mem(f.st().runtime[wc.slot]);
        f.think(1);
        const ecs::MacroNpcRuntime& rt = f.st().runtime[wc.slot];
        CHECK(rt.squadType == std::uint8_t(SquadType::Artel),
              "ЛЕСА НЕТ — ЛЕСОРУБЫ СТАЛИ НЕОПРЕДЕЛЁННОЙ АРТЕЛЬЮ");
        CHECK(mem_is_zero(rt), "ПРИ СМЕНЕ ТИПА ПАМЯТЬ МОДУЛЯ ЗАНУЛЕНА");
        CHECK(f.st().goal[wc.slot].cell == ecs::kNoCell
                  && rt.state == std::uint8_t(NPCState::Idle),
              "и новый модуль начинает с чистого Idle дома, без адреса");
    }
    // ── ДОМ ЗАВАЛЕН ЕЁ ТОВАРОМ — и его контроль: не завален ─────────────
    for (const bool flooded : {true, false}) {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.forest(kForestX, kForestY, 64);
        if (flooded) f.flood(vil, woodIdx);
        else
            CHECK(f.home_worth_of_one(vil, woodIdx) > 1,
                  "ПРЕДУСЛОВИЕ: пустой дом ценит дерево выше пола");
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        fill_mem(f.st().runtime[wc.slot]);
        f.think(1);
        const ecs::MacroNpcRuntime& rt = f.st().runtime[wc.slot];
        if (flooded) {
            CHECK(rt.squadType == std::uint8_t(SquadType::Artel),
                  "ДОМ ЦЕНИТ ДЕРЕВО ПО ПОЛУ — ЛЕСОРУБЫ СТАЛИ НЕОПРЕДЕЛЁННЫМИ, "
                  "хотя лес есть");
            CHECK(mem_is_zero(rt), "и память модуля занулена");
        } else {
            CHECK(rt.squadType == std::uint8_t(SquadType::Woodcutters)
                      && f.st().goal[wc.slot].cell
                             == cell_of(kForestX, kForestY, kMap),
                  "КОНТРОЛЬ: дом ценит дерево — лесорубы остались собой и "
                  "ушли к лесу");
        }
    }
    // ── ВНЕ ДОМА ТИП НЕ МЕНЯЕТСЯ ─────────────────────────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX + 2,
                                       kHomeY, f.id_of(vil), 7);
        f.think(1);
        const ecs::MacroNpcRuntime& rt = f.st().runtime[wc.slot];
        CHECK(rt.squadType == std::uint8_t(SquadType::Woodcutters)
                  && rt.state == std::uint8_t(NPCState::Returning),
              "ВНЕ ДОМА ТИП НЕ МЕНЯЕТСЯ: леса нет — лесорубы сперва идут домой");
    }
}

// ═════ З4. НЕОПРЕДЕЛЁННАЯ СТАНОВИТСЯ ТЕМ, ЧТО ДОМ ЦЕНИТ ВЫШЕ ВСЕГО ════════
void test_undetermined_artel_becomes_what_home_values_most() {
    const int foodCom = hunger_commodity_ordinal();
    const int foodIdx = food_item();
    const int woodIdx = item_index("wood");
    // ── ЕДА В НУЖДЕ ДОРОЖЕ ДЕРЕВА → ПАХАРИ ──────────────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.plough(kFieldX, kFieldY);
        f.forest(kForestX, kForestY, 64);
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        CHECK(f.home_worth_of_one(vil, foodIdx) > 1
                  && f.home_worth_of_one(vil, woodIdx) > 1,
              "ПРЕДУСЛОВИЕ: оба места работы есть, оба товара выше пола");
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      f.id_of(vil), 7);
        f.think(1);
        const ecs::MacroNpcRuntime& rt = f.st().runtime[a.slot];
        CHECK(rt.squadType == std::uint8_t(SquadType::Ploughmen),
              "НЕОПРЕДЕЛЁННАЯ СТАЛА ПАХАРЯМИ: груз еды дом ценит выше груза "
              "дерева");
        CHECK(mem_is_zero(rt) && rt.state == std::uint8_t(NPCState::Idle),
              "при смене память занулена, новый модуль начнёт с Idle");
    }
    // ── ЕДА ПО ПОЛУ → ЛЕСОРУБЫ (то же место, другой дом) ─────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.plough(kFieldX, kFieldY);
        f.forest(kForestX, kForestY, 64);
        f.flood(vil, foodIdx);
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      f.id_of(vil), 7);
        f.think(1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Woodcutters),
              "ДОМ ЗАВАЛЕН ЕДОЙ — НЕОПРЕДЕЛЁННАЯ СТАЛА ЛЕСОРУБАМИ");
    }
    // ── ЕДИНСТВЕННЫЙ ТОВАР ПО ПОЛУ → ОСТАЁТСЯ СОБОЙ ──────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.plough(kFieldX, kFieldY);
        f.flood(vil, foodIdx);
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      f.id_of(vil), 7);
        // Судится КАЖДЫЙ круг решения, а не конец прогона: без гейта пола
        // артель стала бы пахарями, а пахари у заваленного дома той же
        // следующей думкой — снова неопределённой (З3), и на чётной думке
        // прибор увидел бы «собой» поверх нарушения (так прошла зелёной
        // первая редакция этого плеча — запись в таблице мутаций шапки).
        f.think(1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Artel),
              "ЕДИНСТВЕННОЕ МЕСТО ДАЁТ ТОВАР ПО ПОЛУ — НЕОПРЕДЕЛЁННАЯ СТОИТ "
              "СОБОЙ в первый круг");
        f.think(kThinksPerDay + 1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Artel),
              "и во второй круг тоже");
    }
    // ── ТАКАЯ СЕСТРА У ДОМА ЕСТЬ → ОСТАЁТСЯ СОБОЙ ────────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        const int vilId = f.id_of(vil);
        f.plough(kFieldX, kFieldY);
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        f.artel(SquadType::Ploughmen, kFieldX, kFieldY, vilId, 7);
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      vilId, 7);
        f.think(1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Artel)
                  && f.children_of(vilId, SquadType::Ploughmen) == 1,
              "ПАХАРИ У ДОМА УЖЕ ЕСТЬ — НЕОПРЕДЕЛЁННАЯ ИМИ НЕ СТАЛА (контроль "
              "— первое плечо: без сестры тот же мир делает её пахарями)");
    }
    // ── РАЗ В СУТКИ: место появилось — решение в следующий круг ──────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      f.id_of(vil), 7);
        f.think(1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Artel),
              "МЕСТ РАБОТЫ НЕТ — НЕОПРЕДЕЛЁННАЯ ОСТАЁТСЯ СОБОЙ");
        f.forest(kForestX, kForestY, 64);
        f.think(1);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Artel),
              "ЛЕС ПОЯВИЛСЯ, НО КРУГ РЕШЕНИЯ — РАЗ В СУТКИ: следующая думка "
              "ещё не решает");
        f.think(kThinksPerDay);
        CHECK(f.st().runtime[a.slot].squadType
                  == std::uint8_t(SquadType::Woodcutters),
              "СЛЕДУЮЩИЙ КРУГ — ЛЕСОРУБЫ");
    }
}

// ═════ З2/З3/З1. МЕСТО РАБОТЫ ЛЕСОРУБОВ — ЖИВОЙ ЛЕС, А НЕ ТОЧКА СЕТКИ ═════
// Сетка деревьев — точки генерации, лес — живое поле слоя: клетка, срубленная
// до нуля, местом работы не является (как выеденная пашня и выработанная
// жила). Без этого лесорубы вечно ходили к пню, не видели живого леса дальше и
// никогда не становились неопределёнными (зонд ломтя 2: трое суток, живой лес
// в восьми клетках не тронут ни разу).
void test_woodcutters_see_only_living_forest() {
    const int woodIdx = item_index("wood");
    const int furnitureCom = commodity_index("furniture");
    // ── (а) БЛИЖНИЕ ПНИ, ДАЛЬШЕ ЖИВОЙ ЛЕС — ЛЕСОРУБЫ ИДУТ К ЖИВОМУ ────────
    {
        Fixture f;
        const MacroHandle vil = f.village(400, /*busy*/true);
        constexpr int kLiveX = kForestX + 8;
        // Живого леса больше, чем вырубят за сутки: работа у лесорубов не
        // кончается, и тип обязан устоять.
        constexpr int kTrees = 4096;
        f.stumps(kForestX, kForestY);
        f.forest(kLiveX, kForestY, kTrees);
        const std::uint32_t live = cell_of(kLiveX, kForestY, kMap);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        f.think(1);
        CHECK(f.st().goal[wc.slot].cell == live,
              "ПНИ БЛИЖЕ, НО ЛЕСОРУБЫ ИДУТ К ЖИВОМУ ЛЕСУ");
        f.think(kThinksPerDay);
        CHECK(int(f.gs.treeLayer.at(live)) < kTrees,
              "и рубят его: за сутки живой лес убыл");
        CHECK(f.st().runtime[wc.slot].squadType
                  == std::uint8_t(SquadType::Woodcutters),
              "и остаются лесорубами — работа у них есть");
        CHECK(f.st().inventory[vil.slot].inv.count_of(woodIdx) > 0,
              "дерево дошло до дома");
    }
    // ── (б) ОДНИ ПНИ — ЛЕСОРУБЫ ДОМА СТАНОВЯТСЯ НЕОПРЕДЕЛЁННЫМИ ─────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.stumps(kForestX, kForestY);
        CHECK(f.home_worth_of_one(vil, woodIdx) > 1,
              "ПРЕДУСЛОВИЕ: дом ценит дерево выше пола — менять тип велят "
              "только пни");
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        fill_mem(f.st().runtime[wc.slot]);
        f.think(1);
        CHECK(f.st().runtime[wc.slot].squadType
                  == std::uint8_t(SquadType::Artel)
                  && mem_is_zero(f.st().runtime[wc.slot]),
              "ОДНИ ПНИ — ЛЕСОРУБЫ СТАЛИ НЕОПРЕДЕЛЁННОЙ АРТЕЛЬЮ, память "
              "занулена");
        f.think(kThinksPerDay + 1);
        CHECK(f.st().runtime[wc.slot].squadType
                  == std::uint8_t(SquadType::Artel),
              "и через круг лесорубами обратно не стали: пни — не работа");
    }
    // ── (в) ДЕРЕВНЯ НЕ ПОДНИМАЕТ ЛЕСОРУБОВ К ПНЯМ ────────────────────────
    // Контроль — плечо «сырьё вещи» в З1: тот же мир с живым лесом в тот же
    // круг лесорубов поднимает.
    {
        Fixture f;
        const MacroHandle vil = f.village(64, /*busy*/true);
        f.stumps(kForestX, kForestY);
        f.st().upkeep[vil.slot].needDebt[furnitureCom] = 64;
        f.think(kThinksPerDay + 2);
        CHECK(f.artels_of(f.id_of(vil)) == 0,
              "ДЕРЕВНЯ НЕ ПОДНЯЛА ЛЕСОРУБОВ К ПНЯМ: мебель нужна, дерева нет, "
              "но живого леса в округе нет тоже — два круга");
    }
}

// ═════ З5. ХАРЧ: ДОМ ГРУЗИТ ЕДОЙ СВОЮ АРТЕЛЬ ═════════════════════════════
void test_home_feeds_its_artel_no_more_than_owed() {
    const int foodCom = hunger_commodity_ordinal();
    const int foodIdx = food_item();
    constexpr int kOwed = 5;
    const std::uint32_t site = cell_of(kForestX, kForestY, kMap);

    // Дом с запасом еды в половину спины — выше пола, и без своей нужды.
    const auto stocked_home = [&](Fixture& f) {
        const MacroHandle vil = f.village(40, /*busy*/true);
        Inventory& shelf = f.st().inventory[vil.slot].inv;
        const std::int64_t back =
            carry_capacity_g(f.st().runtime[vil.slot], shelf);
        const int half = int(back / std::max<std::int64_t>(
                                        1, unit_mass_g(item_def_at(foodIdx)))
                             / 2);
        CHECK(half > 16 * kOwed && shelf.add_of(foodIdx, half),
              "на полке дома запас еды в половину спины");
        CHECK(f.home_worth_of_one(vil, foodIdx) > 1,
              "ПРЕДУСЛОВИЕ: такой запас — ещё не завал");
        return vil;
    };

    // ── ДОМ ДАЁТ ЕДУ: НЕ БОЛЬШЕ СЧЁТА, ВСЯ ПРИШЕДШАЯ — В УПЛАТУ ─────────
    {
        Fixture f;
        const MacroHandle vil = stocked_home(f);
        f.forest(kForestX, kForestY, 64);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        f.st().upkeep[wc.slot].needDebt[foodCom] = kOwed;
        const int shelfBefore = f.st().inventory[vil.slot].inv.count_of(foodIdx);
        f.think(1);
        const int given =
            shelfBefore - f.st().inventory[vil.slot].inv.count_of(foodIdx);
        const int paid = kOwed - f.st().upkeep[wc.slot].needDebt[foodCom];
        CHECK(given > 0, "ДОМ ДАЛ ЕДУ артели, стоящей дома без груза");
        CHECK(given <= kOwed, "НЕ БОЛЬШЕ ЕЁ СЧЁТА ХАРЧА");
        CHECK(paid == given
                  && f.st().inventory[wc.slot].inv.count_of(foodIdx) == 0,
              "ВСЯ ПРИШЕДШАЯ ЕДА УШЛА В УПЛАТУ: сверх счёта в спине не "
              "осталось ничего");
    }

    // ── ДОМ ДАСТ, НО НЕ В ЭТУ ДУМКУ — АРТЕЛЬ ЖДЁТ ДОМА ───────────────────
    // Круг дома взведён на сутки вперёд: эту думку он не грузит.
    {
        Fixture f;
        const MacroHandle vil = stocked_home(f);
        f.st().runtime[vil.slot].mem[kVillageMemBirthWait] = kAiThinksPerDay;
        f.forest(kForestX, kForestY, 64);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        f.st().upkeep[wc.slot].needDebt[foodCom] = kOwed;
        f.think(1);
        CHECK(f.st().upkeep[wc.slot].needDebt[foodCom] == kOwed,
              "предусловие плеча: в эту думку дом не грузил");
        CHECK(f.st().runtime[wc.slot].state == std::uint8_t(NPCState::Idle)
                  && f.st().cell[wc.slot].idx == f.st().cell[vil.slot].idx
                  && f.st().goal[wc.slot].cell != site,
              "АРТЕЛЬ ЖДЁТ ПОГРУЗКИ ДОМА: дом еду даст, и к лесу она не "
              "ушла");
    }

    // ── ДОМ САМ ГОЛОДЕН — НЕ ДАЁТ, И АРТЕЛЬ НЕ ЖДЁТ ─────────────────────
    {
        Fixture f;
        const MacroHandle vil = f.village(40, /*busy*/true);
        f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
        f.forest(kForestX, kForestY, 64);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        f.st().upkeep[wc.slot].needDebt[foodCom] = kOwed;
        f.think(1);
        CHECK(f.st().upkeep[wc.slot].needDebt[foodCom] == kOwed,
              "ГОЛОДНЫЙ ДОМ ЕДЫ НЕ ДАЛ: ребёнок ценит её не выше дома");
        const std::uint8_t state = f.st().runtime[wc.slot].state;
        CHECK(f.st().goal[wc.slot].cell == site
                  && (state == std::uint8_t(NPCState::Traveling)
                      || state == std::uint8_t(NPCState::Working)),
              "И АРТЕЛЬ НЕ ЖДЁТ: дом еды не даст — она ушла работать");
    }

    // ── (г) В ТРЮМЕ НЕ-ТОВАР — ДОМ ВСЁ РАВНО ГРУЗИТ, И АРТЕЛЬ УХОДИТ ─────
    // Сдаёт дому артель только товары, поэтому предмет вне словаря товаров
    // (шкура) остаётся в спине. Погрузка харча трюм не судит: голоден и стоит
    // дома — этого довольно (зонд ломтя 2: с судом над трюмом артель с одной
    // шкурой стояла дома трое суток, счёт не гасился).
    {
        Fixture f;
        const MacroHandle vil = stocked_home(f);
        f.forest(kForestX, kForestY, 64);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        const int hideIdx = item_index("mat_hide");
        CHECK(hideIdx >= 0 && commodity_of_item(hideIdx) < 0,
              "ПРЕДУСЛОВИЕ: шкура — предмет вне словаря товаров");
        CHECK(f.st().inventory[wc.slot].inv.add_of(hideIdx, 1),
              "шкура легла в спину артели");
        f.st().upkeep[wc.slot].needDebt[foodCom] = kOwed;
        const int shelfBefore =
            f.st().inventory[vil.slot].inv.count_of(foodIdx);
        f.think(2);
        const int given =
            shelfBefore - f.st().inventory[vil.slot].inv.count_of(foodIdx);
        CHECK(given > 0 && given <= kOwed
                  && kOwed - f.st().upkeep[wc.slot].needDebt[foodCom] == given,
              "ДОМ ПОГРУЗИЛ ХАРЧ АРТЕЛИ С ШКУРОЙ В ТРЮМЕ — не больше счёта, "
              "весь в уплату");
        const std::uint8_t state = f.st().runtime[wc.slot].state;
        CHECK(f.st().goal[wc.slot].cell == site
                  && (state == std::uint8_t(NPCState::Traveling)
                      || state == std::uint8_t(NPCState::Working)),
              "И АРТЕЛЬ УШЛА РАБОТАТЬ, а не стоит дома вечно");
    }

    // ── (д) У ПОРОГА ДВОЕ ГОЛОДНЫХ — ЗА ОДИН КРУГ НАКОРМЛЕНЫ ОБА ────────
    // Неопределённая и лесорубы одного дома, у каждого свой счёт: дом кормит
    // всякого голодного ребёнка у порога, тип не спрашивается.
    {
        Fixture f;
        const MacroHandle vil = stocked_home(f);
        f.forest(kForestX, kForestY, 64);
        constexpr int kOwedA = 3;
        const MacroHandle a = f.artel(SquadType::Artel, kHomeX, kHomeY,
                                      f.id_of(vil), 7);
        const MacroHandle wc = f.artel(SquadType::Woodcutters, kHomeX, kHomeY,
                                       f.id_of(vil), 7);
        f.st().upkeep[a.slot].needDebt[foodCom] = kOwedA;
        f.st().upkeep[wc.slot].needDebt[foodCom] = kOwed;
        const int shelfBefore =
            f.st().inventory[vil.slot].inv.count_of(foodIdx);
        f.think(1);
        const int paidA = kOwedA - f.st().upkeep[a.slot].needDebt[foodCom];
        const int paidW = kOwed - f.st().upkeep[wc.slot].needDebt[foodCom];
        const int given =
            shelfBefore - f.st().inventory[vil.slot].inv.count_of(foodIdx);
        CHECK(paidA > 0 && paidW > 0,
              "ЗА ОДИН КРУГ ДОМ НАКОРМИЛ ОБОИХ детей у порога");
        CHECK(paidA <= kOwedA && paidW <= kOwed && given == paidA + paidW
                  && f.st().inventory[a.slot].inv.count_of(foodIdx) == 0
                  && f.st().inventory[wc.slot].inv.count_of(foodIdx) == 0,
              "КАЖДЫЙ — НЕ БОЛЬШЕ СВОЕГО СЧЁТА: ушло с полки ровно то, что "
              "ушло в уплату, сверх счёта в спинах ничего");
    }
}

}  // namespace

int main() {
    test_village_raises_the_type_its_need_names();
    test_woodcutters_fell_haul_and_hand_everything_home();
    test_typed_artel_turns_undetermined_at_home();
    test_undetermined_artel_becomes_what_home_values_most();
    test_woodcutters_see_only_living_forest();
    test_home_feeds_its_artel_no_more_than_owed();
    return sm::test::report("artel_types_test");
}
