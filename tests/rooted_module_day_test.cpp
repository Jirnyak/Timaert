// МИР ПРОИЗВОДИТ, И ЭТО ТЕПЕРЬ ЕСТЬ У КОГО СПРОСИТЬ (И3, ломоть 5а M-72).
//
// ЗАЧЕМ ЭТОТ ПРИБОР СУЩЕСТВУЕТ — НАЗВАНО ЧИСЛОМ, А НЕ ВКУСОМ. Закон «мир
// производит» не держал НИ ОДИН из 134 приборов сюиты: `check` 134/134 и
// смоуки 63/63 были ЗЕЛЕНЫ поверх мира, где `bricks_produced` упал
// 169 578 → 0, а `stone_stock` вырос в 53 раза. Ни один свидетель не
// спрашивал, вышла ли из мира хоть одна вещь, — поэтому прибор пишется
// ВМЕСТЕ с модулями типов, а не после них: дописанный потом, он охранял бы
// уже сложившееся поведение вместо закона (§8 п.5).
//
// ЧТО ИМЕННО УТВЕРЖДАЕТСЯ (шесть законов, по одному на тест):
//   1. деревня ПРОИЗВОДИТ ЧЕРЕЗ ДУМКУ СВОЕГО МОДУЛЯ, и ровно РАЗ В СУТКИ —
//      второго пути выпуска в мире нет (вызов из `tick_squads_day_` умер);
//   2. деревня РОЖАЕТ АРТЕЛЬ НУЖНОГО ТИПА (при нужде в еде и пашне у дома —
//      пахарей, M-488) и ПЛАТИТ ЗА НЕЁ ДУШАМИ своего контейнера;
//   3. город рожает СБОРЩИКА только при вассале и один раз за сезон (ряд
//      M-495: замены погибшего в том же сезоне нет);
//   (законы думки города — сборщик, последняя смена рук, руина, погрузка
//    корована — судятся над ДВУМЯ модулями, городом и столицей: столица —
//    свой модуль, вердикт владельца 2026-11-13);
//   4. ДУБЛЬ не рождается — гейт `has_child` держит одну артель на тип, а
//      живая неопределённая артель новой не пускает вовсе;
//   5. СТОЯЩЕЕ МЕСТО НЕ ПЛАТИТ ЗА ДОРОГУ (M-313: цель на свою клетку);
//   6. ПОСЛЕДНЮЮ СМЕНУ РУК ИЗ ДОМА НЕ ВЫПИСЫВАЮТ — гейт свободных душ
//      (M-314: до 2026-10-25 этот гейт стоял БЕЗ СВИДЕТЕЛЯ ВОВСЕ).
//   (и закон памяти крафта — «крафтить нечего» запомнено, будит его только
//    сдвиг инвентаря или счёта нужды — у деревни судится в законе 1, у
//    города и столицы своим свидетелем, ряд M-519).
//
// ЯКОРИ НЕ ПЕРЕСЧИТЫВАЮТ ПРОДАКШН-ФОРМУЛУ (§8 п.5-6: копия логики проверяет
// умение копировать, а тождество двух вычислений с одной формулой слепо к
// самой формуле). Каждый якорь — независимо известный факт мира либо
// ОТНОШЕНИЕ, выведенное из той же АВТОРСКОЙ строки, которую читает код:
//   · на полке лежит ТОЛЬКО глина, а `bricks` — единственный рецепт деревни,
//     чью материю глина кормит ({clay 1} → 8, items.cpp): значит кирпичей
//     обязано стать РОВНО ВОСЕМЬ НА КАЖДУЮ УШЕДШУЮ ГЛИНУ, и число 8 взято
//     из строки каталога, а не из двери выпуска;
//   · рождение стоит `1 + (crew-1)` душ родителя, а в контейнере рождённого
//     стоит `crew-1`: разница в единицу есть ЛИДЕРСКОЕ ТЕЛО, и утверждается
//     она С ДВУХ КОНЦОВ — у родителя и у рождённого;
//   · период производства — ЭФФЕКТИВНЫЙ день из той же двери
//     `recovery_steps`, что читает глагол (ломоть stateTimer M-369): база —
//     «рабочий день тиками» Craft-строк, анкета места его только
//     укорачивает; прибор считает период САМ из закона, не из константы.
//
// ТАБЛИЦА МУТАЦИЙ ОБЕИХ ПОЛЯРНОСТЕЙ — в теле коммита (§8 п.6).
//
// ПРЕДУСЛОВИЯ ПРИБОР РОЖАЕТ СЕБЕ САМ (§8 п.11): ни одной строки надежды на
// генератор. Две ловушки рецепта фикстуры названы, потому что обе молчат:
//   · ТЕРРАИН-СУША ОБЯЗАТЕЛЕН — `AiPult::spawn` отказывает без
//     `mw.terrain` невалидным хэндлом, и «артель не родилась» выглядело бы
//     дефектом мира;
//   · `birth_squad` С ЯВНОЙ ФРАКЦИЕЙ — при -1 минтовый рецепт глохнет
//     fail-closed (`faction_mint_rows(-1)` = `{-1,-1,-1}`).
#include "check.h"
#include <memory>

#include "macro/npc_ai.h"
#include "macro/pathfinding.h"   // PathCostData — вес земли под стоящим
#include "tables/movement_cost.h"  // burn/rest per hour — якорь предусловия
#include "macro/travel.h"         // overload_burden_from_capacity
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "tables/squad_type.h"  // rooted_is_settlement — дверь различения (M-333)
#include "macro/world_tick.h"    // settle_rooted_day — дневной проход места
#include "macro/features.h"      // FeatureLayer — фича ПОД местом
#include "macro/labour.h"        // settle_souls — одна дверь прихода душ
#include "macro/upkeep_window.h" // upkeep_bill — сезонный счёт рынка
#include "macro/deal.h"          // side_row — строка стороны глазами ядра
#include "macro/squad.h"         // carry_capacity_g — спина стороны
#include "macro/squad_ai/module_window.h"  // AiPult, AiSelf — дверь рождения напрямую (закон 2б (в))
#include "bus/store.h"         // parent_of / suzerain_of / set_relation / store_death
#include "bus/world_row.h"     // count_human_souls
#include "macro/chronicle.h"
#include "macro/econ_day.h"      // hunger_commodity_ordinal — счёт нужды
#include "bus/interests.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/tree_layer.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "tables/world_row.h"   // world_row_of_creature — строка головы-метки
#include "core/time.h"           // kTicksPerDay — период думки выводится; world_time_at — сезон мира
#include "tables/seasons.h"       // kDaysPerSeason — следующий сезон фикстуры

#include <cstdint>
#include <vector>

namespace {

using namespace sm;

constexpr int kMap = 32;

// РАЗМЕР КРЮ СЧИТАЕТСЯ ПРИБОРОМ САМ, ИЗ ТОЙ ЖЕ ПУБЛИЧНОЙ СТРОКИ, ИЗ КОТОРОЙ
// его берёт модуль, — у деревни, у города и у столицы своей
// (`kVillageCrewSouls@src/macro/npc_ai.h`, `kCityCrewSouls@src/macro/npc_ai.h`,
// `kCapitalCrewSouls@src/macro/npc_ai.h`). Копии числа здесь нет нарочно:
// прибитый литерал ломался бы на каждой перекалибровке и не доказывал бы
// замысла (§8 п.4).
constexpr int kVillageCrew = kVillageCrewSouls;

// ГОРОД И СТОЛИЦА — ДВА МОДУЛЯ (вердикт владельца 2026-11-13: «столица
// отдельный модуль»), и всякий закон думки города стоит над обоими: тело
// столицы — законная копия городского (AGENTS §4 п.5), значит и свидетель у
// каждой копии свой — мутация одной копии другую не задевает. Фикстура сама
// рожает своё место нужного типа (§8 п.11).
struct TownModule {
    SquadType kind;
    int crew;          // своя публичная строка крю модуля
    const char* who;
    // Свои слова памяти крафта модуля (`src/macro/npc_ai.h`): «при каком
    // инвентаре» и «при каком счёте нужды» крафтить было нечего.
    int memCraftSeen;
    int memCraftDebt;
};
constexpr TownModule kTownModules[] = {
    {SquadType::City, kCityCrewSouls, "город", kCityMemCraftSeen,
     kCityMemCraftDebt},
    {SquadType::Capital, kCapitalCrewSouls, "столица", kCapitalMemCraftSeen,
     kCapitalMemCraftDebt},
};

// ПЕРИОД ПРОИЗВОДСТВЕННОГО АКТА — ТОЖЕ ВЫВОД, А НЕ ЦИТАТА: думка идёт раз в
// `kAiTicks` мировых тиков, в сутках `kTicksPerDay` тиков.
constexpr int kThinksPerDay = int(kTicksPerDay / kAiTicks);

// Суша под всей картой: без неё дверь рождения отказывает молча.
// Значения кладутся ТОЛЬКО через `field_word_of` — байты руками не считаются.
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

// (ВЕДОМОСТЬ ФАКТОВ `FactTally` + `tally_sink` СТОЯЛИ ЗДЕСЬ — `switch` по
//  всем девяти родам `EconFact::Kind` без `default:`. Умерли 2026-11-03 вместе
//  со вторым фактным путём мира: некролог и вердикт владельца — в шапке
//  `src/macro/econ_day.h`. Что с ними ушло из ЭТОГО прибора, названо у
//  утверждений о выпуске ниже.)

// Мир фикстуры — одна структура, чтобы все четыре теста рождали СВОИ
// предусловия одной и той же дверью, а не четырьмя списаниями друг у друга.
// ВЕС ЗЕМЛИ ФИКСТУРЫ — И БЕЗ НЕГО ЗАКОН ПЛАНКИ НЕ ПРОВЕРЯЕТСЯ ВОВСЕ.
// `cell_weight@src/macro/npc_ai.cpp` читает запечённую сетку, а при её
// отсутствии честно отвечает **1.0** — «мир без рельефа». На весе 1.0 жжение
// получается меньше регена, планка не садится НИ У КОГО, и всякая проверка
// про SP становится ТАВТОЛОГИЕЙ (§8 п.2: прибор обязан утверждать, что
// помЕрил). Поймала это ровно ТАБЛИЦА МУТАЦИЙ: снос строки цели в
// `birth_squad` ронял только проверки про саму цель, а проверки про планку
// оставались зелёными — то есть не проверяли ничего.
//
// ЧИСЛО ВЗЯТО ИЗ ЗАМЕРА ЖИВОГО МИРА, А НЕ С ПОТОЛКА: зонд в самой точке
// ритма дал вес клетки под местами **3.48–5.59** (`balance_run`, сид 7,
// 40 дней). 5.0 — середина этой полосы, то есть земля фикстуры не легче и не
// тяжелее той, на которой стоят настоящие деревни.
constexpr float kFixtureCellWeight = 5.0f;

struct Fixture {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    TerrainData terrain = all_land();
    TreeLayer& trees = gs.treeLayer;
    PathCostData pathCost;
    // СЛОЙ ФИЧ — НЕ УКРАШЕНИЕ ФИКСТУРЫ: без него `birth_squad` фичу не
    // ставит вовсе (её постановка гейтится `fl && td`), и закон «умирающий
    // переписывает фичу под собой» проверять было бы НЕ НА ЧЁМ.
    FeatureLayer& features = gs.features;
    std::unique_ptr<MacroStore> storeOwner = make_macro_store();

    Fixture() {
        gs.mapW = kMap;
        gs.mapH = kMap;
        chronicle_init(gs.chronicle, kMap, kMap);
        tree_layer_reset(trees, kMap, kMap);
        // Сетка весов — плоская и НЕ водная: предмет проверки есть цена
        // ДОРОГИ у стоящего, и подмешивать сюда воду значило бы мерить две
        // величины одной проверкой. Высоты НЕ печём нарочно: `climb`
        // возвращает 0 при незапечённых высотах (его собственный закон), и
        // подъёма в цене стоящего быть не должно вовсе.
        pathCost.width = kMap;
        pathCost.height = kMap;
        pathCost.costGrid.assign(std::size_t(kMap) * kMap,
                                 kFixtureCellWeight);
        pathCost.water.assign(std::size_t(kMap) * kMap, 0u);
        features.resize(kMap, kMap);
    }
    MacroStore& st() { return *storeOwner; }

    MacroWorld world() {
        return MacroWorld{.gs = &gs, .store = storeOwner.get(),
                          .terrain = &terrain,
                          .pathCost = &pathCost};
    }

    // ПЛЕМЯ НАЗЫВАЕТСЯ ЯВНО: -1 глушит минтовый рецепт fail-closed.
    MacroHandle rooted(SquadType kind, int x, int y, int souls) {
        const MacroHandle h = birth_squad(
            gs, nullptr, st(), kind, x, y,
            std::int16_t(faction_index("timaert")), /*name*/nullptr,
            &terrain);   // ФИЧА СТАВИТСЯ ДО ТЕЛА (ЗАКОН ГЕНЕРАЦИИ)
        CHECK(st().valid(h), "фикстура родила место (дверь рождения дала "
                             "валидный хэндл)");
        // ДВЕРЬ ДУШ ВОЗВРАЩАЕТ ФАКТ, И ИГНОРИРОВАТЬ ЕГО НЕЛЬЗЯ: нулевая
        // паства держит НОЛЬ верстаков, то есть прибор замерил бы
        // отсутствие РУК, а не отсутствие выпуска.
        const int stood = settle_souls(gs, st(), h.slot, souls);
        CHECK(stood == souls,
              "все заявленные души ВСТАЛИ в контейнер места — иначе прибор "
              "мерил бы нехватку рук, а не закон выпуска");
        return h;
    }

    void think(int times) {
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 50u);
        for (int i = 0; i < times; ++i) {
            MacroWorld mw = world();
            tick_macro_npc_ai(mw, rt, kAiTicks);
        }
    }
    // Тот же прогон, но продолжающий ОДИН поток думки: пересоздавать
    // runtime значило бы сбрасывать квант накопления.
    void think_on(MacroNpcAiRuntime& rt, int times) {
        for (int i = 0; i < times; ++i) {
            MacroWorld mw = world();
            tick_macro_npc_ai(mw, rt, kAiTicks);
        }
    }

    int heads(std::uint16_t slot) {
        return count_human_souls(st().inventory[slot].inv);
    }
    // ПАШНЯ У ДОМА — РАБОЧЕЕ МЕСТО ПАХАРЕЙ, И СТАВИТ ЕЁ ПРИБОР (§8 п.11):
    // деревня поднимает артель только того типа, чьё место работы есть в её
    // округе (M-488, вердикт владельца 2026-11-13), а генератора здесь нет.
    // Фича ставится дверью мира, ёмкость пашни есть фертильность клетки —
    // канал G карты, который ряд пашни читает сам; числа руками не кладутся.
    void plough(int x, int y) {
        const std::uint32_t c = cell_of(x, y, kMap);
        terrain.rgba[std::size_t(c) * 4u + 1u] = std::uint16_t(kFieldWordMax);
        CHECK(feature_put(features, terrain, c, FT_Field) == FT_Field,
              "пашня встала дверью мира на сушу фикстуры");
        const MacroWorld mw = world();
        CHECK(resource_field_read(mw, ResourceFieldId::Wheat, c) > 0,
              "и на ней стоит урожай — место работы пахарей живое");
    }
    // Артелей ЭТОГО родителя всех шести типов — отрезок оси
    // `Artel..Woodcutters`: закон «одна на тип, и неопределённая держит
    // новых» судится их суммой, какой бы тип артель ни носила сегодня.
    int artels_of(int parentId) {
        int n = 0;
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (st().alive[s] == 0 || st().dead[s] != 0) continue;
            const std::uint8_t t = st().runtime[s].squadType;
            if (t < std::uint8_t(SquadType::Artel)
                || t > std::uint8_t(SquadType::Woodcutters))
                continue;
            if (parent_of(st(), std::uint16_t(s)) == parentId) ++n;
        }
        return n;
    }
    // Слот единственной артели родителя (любого из шести типов); -1 — нет.
    int artel_slot_of(int parentId) {
        int found = -1;
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (st().alive[s] == 0 || st().dead[s] != 0) continue;
            const std::uint8_t t = st().runtime[s].squadType;
            if (t < std::uint8_t(SquadType::Artel)
                || t > std::uint8_t(SquadType::Woodcutters))
                continue;
            if (parent_of(st(), std::uint16_t(s)) == parentId) found = int(s);
        }
        return found;
    }
    // Детей ЭТОГО родителя ЭТОГО типа — вопрос прибора, не копия двери:
    // `AiView::has_child` отвечает «есть ли», а свидетелю нужно «сколько».
    int children_of(int parentId, SquadType t) {
        int n = 0;
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (st().alive[s] == 0 || st().dead[s] != 0) continue;
            if (st().runtime[s].squadType != std::uint8_t(t)) continue;
            if (parent_of(st(), std::uint16_t(s)) == parentId) ++n;
        }
        return n;
    }
};

// ═════ 1. ДЕРЕВНЯ КРАФТИТ ЧЕРЕЗ ДУМКУ, И ВИСИТ РОВНО ПО СДЕЛАННОМУ ═══════
// Второго пути выпуска в мире нет: дневной такт мира здесь не крутится вовсе
// — крутится ТОЛЬКО думка. Значит всякий кирпич, найденный на полке, пришёл
// через модуль: он решил, что и сколько, а акт исполнила дверь.
void test_village_produces_through_its_own_think() {
    Fixture f;
    const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 64);

    // ТОЛЬКО ГЛИНА НА ПОЛКЕ — ЭТО И ДЕЛАЕТ ЯКОРЬ ОДНОЗНАЧНЫМ. Деревня знает
    // шесть товарных рецептов (Masonry 30, Tailoring 30, Blacksmith 20,
    // Alchemy 10), но материю имеет РОВНО ОДИН: `bricks` = {clay 1} → 8.
    // Остальные кандидаты честно дают ноль партий — «нет материи», не «нет
    // рецепта», — поэтому прирост кирпича сравнивается с убылью глины без
    // единой оговорки.
    Inventory& shelf = f.st().inventory[vil.slot].inv;
    const int clayIdx = item_index("clay");
    const int bricksIdx = item_index("bricks");
    CHECK(clayIdx >= 0 && bricksIdx >= 0,
          "каталог знает глину и кирпич — ординалы резолвятся на границе");
    constexpr int kClayStart = 512;
    CHECK(shelf.add_of(clayIdx, kClayStart),
          "вся глина фикстуры легла на полку (кап слотов не отказал)");
    CHECK(shelf.count_of(clayIdx) == kClayStart,
          "и легла она ЦЕЛИКОМ — иначе убыль ниже мерилась бы от неверной "
          "базы");
    CHECK(shelf.count_of(bricksIdx) == 0,
          "кирпича на полке НЕТ до первой думки — иначе прирост ниже был бы "
          "тавтологией");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // ПЕРВАЯ ЖЕ ДУМКА ЕСТЬ НОВЫЙ ДЕНЬ: счётчик восстановления рождён нулём,
    // поэтому акт случается сразу, а не через сутки простоя.
    f.think_on(rt, 1);
    const int clayAfterOne = shelf.count_of(clayIdx);
    const int bricksAfterOne = shelf.count_of(bricksIdx);
    const int clayGone = kClayStart - clayAfterOne;

    CHECK(bricksAfterOne > 0,
          "МИР ПРОИЗВЁЛ: деревня выпустила кирпич СВОЕЙ думкой, и другого "
          "пути выпуска в фикстуре нет вовсе");
    CHECK(clayGone > 0, "выпуск СЪЕЛ материю: глина ушла с полки");
    // ЯКОРЬ ОТНОШЕНИЯ — из АВТОРСКОЙ строки каталога, не из двери выпуска.
    CHECK(bricksAfterOne == clayGone * item_yield(bricksIdx),
          "СОХРАНЕНИЕ ПО СТРОКЕ КАТАЛОГА: кирпичей ровно `yield` на каждую "
          "ушедшую глину — число взято из composition, а не пересчитано "
          "формулой ранжирования");
    // (ЗДЕСЬ СТОЯЛИ ДВА УТВЕРЖДЕНИЯ О ВЕДОМОСТИ — «факт Produced назвал
    //  ТОВАРНУЮ строку кирпича» и «доложенные единицы равны тому, что РЕАЛЬНО
    //  легло на полку». Оба были законами О САМОМ КАНАЛЕ — «ведомость не врёт
    //  о вещах, которых нет», — и умерли вместе с ним 2026-11-03 (§8 п.6,
    //  сказано вслух). Переводить их на живой носитель нечем и не нужно: ЧТО
    //  легло на полку и СКОЛЬКО, утверждают три проверки ВЫШЕ, и верхняя из
    //  них — якорь по АВТОРСКОЙ строке каталога (`item_yield`), то есть
    //  утверждение более сильное, чем согласие двери с самой собой.)

    // ВОССТАНОВЛЕНИЕ — РОВНО ПО СДЕЛАННОМУ (вердикт владельца 2026-11-13,
    // CANON S25: «он будет рековери ровно столько скольок требовалось за
    // крафт»): время одной партии на число партий, делённое на рабочих
    // юнитов. Ожидание выводится из СТРОКИ действия и из инвентаря, не из
    // литерала (§8 п.4).
    const CharacterSheet& sheet = f.st().sheet[vil.slot];
    const ActionDef& act =
        action_def(int(kRecipes[recipe_of_item(bricksIdx)].action));
    const int oneBatch = recovery_steps(act.recoveryTicks, sheet.attributes,
                                        sheet.skills,
                                        action_tempo_generic(act));
    // ЯКОРЬ ИЗВНЕ ФОРМУЛЫ (§8 п.6 «тождество одной формулы слепо к самой
    // формуле»): анкета поселения время не укорачивает (делитель ровно 1.00),
    // а время партии на темп строки даёт рабочий день. Поплывёт дверь, анкета
    // или строка — краснеет ЭТОТ якорь, а не молчаливое согласие.
    CHECK(oneBatch == int(act.recoveryTicks)
              && std::int64_t(oneBatch) * act.spRatePerDay
                     == std::int64_t(kTicksPerDay),
          "якорь: время партии кирпича на темп строки — ровно рабочий день, "
          "и анкета поселения его не укорачивает");
    const std::int64_t units = work_units(shelf);
    const std::int64_t batches = clayGone;   // кирпич = {глина 1}
    const std::int64_t hangExpected =
        (std::int64_t(oneBatch) * batches + units - 1) / units;
    const std::int64_t hang = f.st().runtime[vil.slot].recoverySteps;
    CHECK(units == 65 && hang == hangExpected,
          "ВОССТАНОВЛЕНИЕ РАВНО СДЕЛАННОМУ: время партии × партии ÷ рабочие "
          "юниты (аватар и 64 души)");
    CHECK(hang > 0 && hang < std::int64_t(batches) * oneBatch,
          "и оно меньше, чем у одиночки за те же партии: юниты работают разом");
    std::printf("  [акт деревни] партий %lld, юнитов %lld, восстановление %lld "
                "тиков (одиночке было бы %lld)\n",
                (long long)batches, (long long)units, (long long)hang,
                (long long)(batches * oneBatch));
    const int hangThinks = int((hang + kAiTicks - 1) / kAiTicks);
    CHECK(hangThinks > 1,
          "восстановление длиннее одной думки — иначе гейт ниже не проверяется");

    // ВОССТАНОВЛЕНИЕ ЕСТЬ ЗАКОН, А НЕ ПОЖЕЛАНИЕ: пока оно висит, второго акта
    // не бывает.
    f.think_on(rt, hangThinks - 1);
    CHECK(shelf.count_of(bricksIdx) == bricksAfterOne,
          "ПОКА ВИСИТ ВОССТАНОВЛЕНИЕ — НИ ОДНОЙ ЛИШНЕЙ ПАРТИИ: гейт занятости "
          "заряжает сама дверь акта");

    // А дальше модуль думает снова: либо новый акт (кирпича прибыло), либо
    // крафтить уже нечего, и он ЗАПОМНИЛ, при каком инвентаре это решил.
    // Гейт восстановления ожиданием не заряжается: его заряжает только
    // способность (вердикт владельца 2026-11-13).
    int guard = 0;
    while (f.st().runtime[vil.slot].mem[kVillageMemCraftSeen]
               != std::uint32_t(shelf.revision() + 1)
           && guard < 64 * kThinksPerDay) {
        f.think_on(rt, 1);
        ++guard;
    }
    const ecs::MacroNpcRuntime& vrt = f.st().runtime[vil.slot];
    CHECK(vrt.mem[kVillageMemCraftSeen]
              == std::uint32_t(shelf.revision() + 1),
          "ПОСЕЛЕНИЕ ДОШЛО ДО «КРАФТИТЬ НЕЧЕГО» И ЗАПОМНИЛО ЭТО СОСТОЯНИЕМ "
          "ИНВЕНТАРЯ, а не таймером");
    CHECK(vrt.recoverySteps == 0 || shelf.count_of(bricksIdx) > bricksAfterOne,
          "ОЖИДАНИЕ НЕ ЗАРЯЖАЕТ ГЕЙТ ВОССТАНОВЛЕНИЯ: без акта сквад свободен");
    const int bricksIdle = shelf.count_of(bricksIdx);
    const int clayIdle = shelf.count_of(clayIdx);
    f.think_on(rt, 3);
    CHECK(shelf.count_of(bricksIdx) == bricksIdle
              && shelf.count_of(clayIdx) == clayIdle,
          "ИНВЕНТАРЬ НЕ МЕНЯЛСЯ — РЕШЕНИЕ ТО ЖЕ: думки идут, полка стоит");

    // ПРИВЕЗЛИ СЫРЬЁ — ПОСЕЛЕНИЕ ВИДИТ ЕГО НА СЛЕДУЮЩЕЙ ЖЕ ДУМКЕ. Волокна на
    // полке не было; ткань — второй крафт, на который у деревни есть право.
    const int fibreIdx = item_index("fibre");
    const int clothIdx = item_index("cloth");
    CHECK(shelf.count_of(fibreIdx) == 0 && shelf.count_of(clothIdx) == 0,
          "предусловие: ни волокна, ни ткани на полке нет");
    CHECK(shelf.add_of(fibreIdx, 64), "волокно легло на полку");
    CHECK(std::uint32_t(shelf.revision() + 1)
              != vrt.mem[kVillageMemCraftSeen],
          "запись в инвентарь сдвинула его счётчик изменений");
    f.think_on(rt, 1);
    CHECK(shelf.count_of(clothIdx) > 0,
          "НОВОЕ СЫРЬЁ ПОШЛО В ДЕЛО СРАЗУ: ни суток, ни иной задержки");
    std::printf("  [привезли волокно] ткани после одной думки: %d\n",
                shelf.count_of(clothIdx));
}

// ═════ 1б. ГОРОД И СТОЛИЦА ПОМНЯТ, ЧТО КРАФТИТЬ НЕЧЕГО (ряд M-519) ═══════
// Закон памяти тот же, что у деревни выше, а носитель у каждого модуля свой
// (AGENTS §4 п.5): `city_craft_think_@src/macro/squad_ai/city.cpp` со
// словами `kCityMemCraftSeen`/`kCityMemCraftDebt` и
// `capital_craft_think_@src/macro/squad_ai/capital.cpp` со словами
// `kCapitalMemCraftSeen`/`kCapitalMemCraftDebt` (`src/macro/npc_ai.h`).
// Мутация одной копии другую не задевает — поэтому плечи стоят над обоими.
// Три закона, по плечу на каждый и по своему миру на плечо:
//   (а) при том же инвентаре и том же счёте решение второй раз не
//       считается: слова стоят, акта нет — при полном баре и свободном теле,
//       иначе «акта нет» не доказывало бы ничего;
//   (б) сдвиг ИНВЕНТАРЯ, делающий лот выгодным, будит — следующей думкой акт;
//   (в) сдвиг СЧЁТА НУЖДЫ при том же инвентаре будит — тоже акт.
//
// ФИКСТУРА СВОЯ, И ДУШ В НЕЙ РОВНО НА КРЮ: в общей город с первой думки
// рождает корован, и счёт рабочих юнитов перестаёт держаться. Здесь
// свободных душ нет (гейт свободных душ — закон 6), рождений нет, и
// инвентарь места двигает только прибор.
//
// «КРАФТИТЬ НЕЧЕГО» ПРИБОР РОЖАЕТ НУЖДОЙ, А НЕ ПУСТОЙ ПОЛКОЙ: на полке глина,
// и счёт места требует глины вдвое больше, чем лежит. Глину кормит один
// рецепт — кирпич ({clay 1}, `kPartsAuthoring@src/tables/items.cpp`), — а
// отдать недостающую глину за кирпич значит потерять в оценке: единица в
// нужде стоит стоимость × (глубина + 1) (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ»
// п.2). Пустая полка не годилась бы: при ней сдвиг счёта не будит ничего, и
// плечо (в) было бы не на чем судить. Что рецепт под рукой ЕСТЬ и мира
// хватает на акт, доказывают плечи (б) и (в): оба кончаются кирпичом.
constexpr int kRestClay = 64;                  // глины на полке
constexpr int kRestClayDebt = 2 * kRestClay;   // счёт глины вдвое глубже

struct TownAtRest {
    MacroHandle h;
    std::uint32_t seenWord = 0;   // слова памяти крафта после первой думки
    std::uint32_t debtWord = 0;
};

// Рожает место модуля `m` в «крафтить нечего» и даёт ему одну думку; что
// модуль запомнил это и не сделал ничего — утверждается здесь же, потому что
// это предусловие всех трёх плеч.
TownAtRest town_with_nothing_to_craft(Fixture& f, MacroNpcAiRuntime& rt,
                                      const TownModule& m) {
    TownAtRest out{};
    out.h = f.rooted(m.kind, 10, 10, m.crew);
    const std::uint16_t slot = out.h.slot;
    Inventory& shelf = f.st().inventory[slot].inv;
    const ecs::MacroNpcRuntime& trt = f.st().runtime[slot];
    const int clayIdx = item_index("clay");
    const int bricksIdx = item_index("bricks");
    const int clayCol = commodity_index("clay");
    CHECK(clayIdx >= 0 && bricksIdx >= 0 && clayCol >= 0,
          "каталог знает глину и кирпич, словарь товаров — глину");
    CHECK(shelf.add_of(clayIdx, kRestClay), "глина легла на полку места");
    f.st().upkeep[slot].needDebt[clayCol] = kRestClayDebt;

    // ПРАВО НА РЕЦЕПТ — ВОПРОС АНКЕТЫ, И ОНО ЕСТЬ: иначе «нечего» значило бы
    // «не умею», и плечи (б) и (в) судили бы не память, а неграмотность.
    const ActionDef& act =
        action_def(int(kRecipes[recipe_of_item(bricksIdx)].action));
    CHECK(recipe_known(f.st().sheet[slot].skills, act.gateSkill,
                       int(act.minRank)),
          "место знает рецепт кирпича своей анкетой");
    CHECK(trt.mem[m.memCraftSeen] == 0 && trt.mem[m.memCraftDebt] == 0,
          "до первой думки память крафта пуста — запись ниже есть запись "
          "этой думки, а не наследство");

    f.think_on(rt, 1);
    out.seenWord = trt.mem[m.memCraftSeen];
    out.debtWord = trt.mem[m.memCraftDebt];
    CHECK(shelf.count_of(bricksIdx) == 0
              && shelf.count_of(clayIdx) == kRestClay,
          "КРАФТИТЬ НЕЧЕГО: нужную глину место за кирпич не отдало");
    CHECK(out.seenWord == std::uint32_t(shelf.revision() + 1),
          "И ЗАПОМНИЛО ЭТО СОСТОЯНИЕМ ИНВЕНТАРЯ — счётчиком изменений своего "
          "контейнера плюс один, а не таймером");
    CHECK(out.debtWord != 0,
          "и счёт нужды лёг в своё слово (ноль у слова значит «не помню»)");
    const int townId = int(f.st().spawnId[slot].index);
    CHECK(f.heads(slot) == m.crew
              && f.children_of(townId, SquadType::Caravan) == 0
              && f.children_of(townId, SquadType::Collector) == 0,
          "рождений нет: душ ровно на крю, и рабочих юнитов столько же, "
          "сколько было");
    return out;
}

// Сколько кирпича и глины на полке — наблюдение акта.
struct Shelf {
    int bricks = 0;
    int clay = 0;
};
Shelf shelf_of(Fixture& f, std::uint16_t slot) {
    const Inventory& inv = f.st().inventory[slot].inv;
    return Shelf{inv.count_of(item_index("bricks")),
                 inv.count_of(item_index("clay"))};
}

void test_the_town_remembers_it_has_nothing_to_craft() {
    const int clayIdx = item_index("clay");
    const int clayCol = commodity_index("clay");
    for (const TownModule& m : kTownModules) {
        std::printf("  [память крафта: %s]\n", m.who);

        // ── (а) НИЧЕГО НЕ СДВИНУЛОСЬ — РЕШЕНИЕ ТО ЖЕ, АКТА НЕТ ─────────────
        // Это и негативный контроль двух других плеч: без сдвига кирпича нет.
        {
            Fixture f;
            MacroNpcAiRuntime rt{};
            reset_macro_npc_ai_runtime(rt, 50u);
            const TownAtRest t = town_with_nothing_to_craft(f, rt, m);
            const ecs::MacroNpcRuntime& trt = f.st().runtime[t.h.slot];
            const ecs::Pools& bar = f.st().pools[t.h.slot];
            // Сутки и ещё две думки — через круг рождений и погрузки, чтобы
            // утверждение покрывало всю думку модуля, а не её первую строку.
            f.think_on(rt, kThinksPerDay + 2);
            CHECK(trt.mem[m.memCraftSeen] == t.seenWord
                      && trt.mem[m.memCraftDebt] == t.debtWord,
                  "(а) СЛОВА СТОЯТ: инвентарь и счёт те же — помнить нечего "
                  "нового");
            const Shelf s = shelf_of(f, t.h.slot);
            CHECK(s.bricks == 0 && s.clay == kRestClay,
                  "(а) АКТА НЕТ: ни кирпича, ни убыли глины за сутки думок");
            CHECK(int(bar.sp) == int(bar.maxSp) && bar.maxSp > 0
                      && !ecs::squad_recovering(trt),
                  "(а) предусловие живо: бар полон и тело свободно — значит "
                  "акт держит решение, а не усталость и не восстановление");
        }

        // ── (б) СДВИГ ИНВЕНТАРЯ БУДИТ ─────────────────────────────────────
        {
            Fixture f;
            MacroNpcAiRuntime rt{};
            reset_macro_npc_ai_runtime(rt, 50u);
            const TownAtRest t = town_with_nothing_to_craft(f, rt, m);
            Inventory& shelf = f.st().inventory[t.h.slot].inv;
            const ecs::MacroNpcRuntime& trt = f.st().runtime[t.h.slot];
            // Привезли глины на весь счёт: запас перерос нужду, и излишек
            // глины место ценит ниже кирпича из него.
            CHECK(shelf.add_of(clayIdx, kRestClayDebt),
                  "(б) привезённая глина легла на полку");
            CHECK(std::uint32_t(shelf.revision() + 1) != t.seenWord,
                  "(б) предусловие: запись сдвинула счётчик изменений "
                  "инвентаря");
            CHECK(f.st().upkeep[t.h.slot].needDebt[clayCol] == kRestClayDebt
                      && trt.mem[m.memCraftDebt] == t.debtWord,
                  "(б) предусловие: счёт нужды тот же — будить вправе только "
                  "инвентарь");
            CHECK(int(f.st().pools[t.h.slot].sp)
                          == int(f.st().pools[t.h.slot].maxSp)
                      && !ecs::squad_recovering(trt),
                  "(б) предусловие: бар полон и тело свободно");
            f.think_on(rt, 1);
            const Shelf s = shelf_of(f, t.h.slot);
            CHECK(s.bricks > 0 && s.clay < kRestClay + kRestClayDebt,
                  "(б) СДВИГ ИНВЕНТАРЯ РАЗБУДИЛ РЕШЕНИЕ: на следующей же "
                  "думке глина пошла в кирпич");
            std::printf("    (б) кирпича после одной думки: %d\n", s.bricks);
        }

        // ── (в) СДВИГ СЧЁТА НУЖДЫ БУДИТ ПРИ ТОМ ЖЕ ИНВЕНТАРЕ ────────────────
        {
            Fixture f;
            MacroNpcAiRuntime rt{};
            reset_macro_npc_ai_runtime(rt, 50u);
            const TownAtRest t = town_with_nothing_to_craft(f, rt, m);
            const Inventory& shelf = f.st().inventory[t.h.slot].inv;
            const ecs::MacroNpcRuntime& trt = f.st().runtime[t.h.slot];
            // Счёт глины закрыт: нужды в ней нет, и вся полка — излишек.
            f.st().upkeep[t.h.slot].needDebt[clayCol] = 0;
            CHECK(std::uint32_t(shelf.revision() + 1) == t.seenWord
                      && trt.mem[m.memCraftSeen] == t.seenWord,
                  "(в) предусловие: инвентарь тот же — будить вправе только "
                  "счёт");
            CHECK(int(f.st().pools[t.h.slot].sp)
                          == int(f.st().pools[t.h.slot].maxSp)
                      && !ecs::squad_recovering(trt),
                  "(в) предусловие: бар полон и тело свободно");
            f.think_on(rt, 1);
            const Shelf s = shelf_of(f, t.h.slot);
            CHECK(s.bricks > 0 && s.clay < kRestClay,
                  "(в) СДВИГ СЧЁТА РАЗБУДИЛ РЕШЕНИЕ: при той же полке глина "
                  "пошла в кирпич на следующей же думке");
            std::printf("    (в) кирпича после одной думки: %d\n", s.bricks);
        }
    }
}

// ═════ 2. ДЕРЕВНЯ РОЖАЕТ АРТЕЛЬ НУЖНОГО ТИПА И ПЛАТИТ ДУШАМИ ═══════════
void test_village_raises_an_artel_and_pays_in_souls() {
    Fixture f;
    const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 64);
    const int vilId = int(f.st().spawnId[vil.slot].index);
    // Пашня у дома стоит В ОБОИХ плечах: негативное плечо обязано отказать
    // из-за НУЖДЫ, а не из-за того, что работать негде (M-488).
    f.plough(12, 10);

    // ЖИВАЯ НУЖДА — ПРЕДУСЛОВИЕ, И ПРИБОР РОЖДАЕТ ЕГО САМ. Счёт нужд есть
    // колонка места (`Upkeep::needDebt` по товарным ординалам), и ставится
    // он миром на границе сезона; фикстура сезонов не крутит, поэтому
    // непокрытую нужду называет прямо. Без неё деревня честно НЕ поднимает
    // крю — ровно это и утверждает негативное плечо ниже.
    const int foodCom = hunger_commodity_ordinal();
    CHECK(foodCom >= 0, "лестница нужд знает голодную строку");

    const int headsBefore = f.heads(vil.slot);
    CHECK(headsBefore == 64, "все 64 души стоят дома до первой думки");

    // ── НЕГАТИВНОЕ ПЛЕЧО: НУЖДЫ НЕТ — КРЮ НЕ ПОДНИМАЕТСЯ ────────────────
    // Оно же и есть контроль, который обязан КРАСНЕТЬ, если гейт нужды
    // снять: без него тест не отличал бы «решил» от «рождает всегда».
    f.think(kThinksPerDay + 2);
    CHECK(f.artels_of(vilId) == 0,
          "БЕЗ ЖИВОЙ НУЖДЫ АРТЕЛИ НЕТ: деревня, которой нечего просить, душ "
          "в поле не выписывает — хотя пашня у дома стоит");
    CHECK(f.heads(vil.slot) == headsBefore,
          "и душ она при этом не потеряла ни одной");

    // ── ПОЛОЖИТЕЛЬНОЕ ПЛЕЧО ────────────────────────────────────────────
    f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
    f.think(kThinksPerDay + 2);

    CHECK(f.children_of(vilId, SquadType::Ploughmen) == 1
              && f.artels_of(vilId) == 1,
          "ДЕРЕВНЯ РОДИЛА АРТЕЛЬ НУЖНОГО ТИПА — еды не хватает, пашня у дома "
          "есть, значит ПАХАРЕЙ, ровно одну, и родителем названа ОНА");
    const int headsAfter = f.heads(vil.slot);
    // ЯКОРЬ С ДВУХ КОНЦОВ, И РАЗНИЦА В ЕДИНИЦУ ЕСТЬ ЛИДЕРСКОЕ ТЕЛО.
    CHECK(headsBefore - headsAfter == kVillageCrew,
          "РОЖДЕНИЕ ЗАПЛАТИЛО ДУШАМИ РОДИТЕЛЯ: дома стало РОВНО на крю "
          "меньше (предел по построению — потолка детей колонкой нет)");

    // Второй конец того же якоря — контейнер рождённого.
    int bornSlot = -1;
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (f.st().alive[s] == 0 || f.st().dead[s] != 0) continue;
        if (f.st().runtime[s].squadType
            != std::uint8_t(SquadType::Ploughmen))
            continue;
        if (parent_of(f.st(), std::uint16_t(s)) == vilId)
            bornSlot = int(s);
    }
    CHECK(bornSlot >= 0, "рождённая артель нашлась слотом");
    CHECK(f.heads(std::uint16_t(bornSlot)) == kVillageCrew - 1,
          "В КОНТЕЙНЕРЕ РОЖДЁННОГО СТОИТ КРЮ МИНУС ОДИН: недостающая душа "
          "и есть ЛИДЕРСКОЕ ТЕЛО, которым сквад воплощается, — закон "
          "утверждён с ОБОИХ концов пары, а не пересчётом одной формулы");
}

// ═════ 2б. РОЖДЕНИЕ — СТРОКА МАССИВА ПЛЮС ТЕ ЖЕ ГОЛОВЫ РОДИТЕЛЯ (M-496) ═══
// Вердикт владельца 2026-11-13 (CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ»
// п.3): рождение — два шага двух систем. «Появился сквад» — дверь рождения
// `AiPult::spawn@src/macro/npc_ai.cpp`: строка, тип, связь с родителем и
// аватар, который есть ОДНА душа родителя (дверь убыли); «родитель отдал
// души» — лот голов дверью сделки, и едут ТЕ ЖЕ головы. Три закона:
//   (а) СЧЁТ ДУШ ТОЧЕН: души родителя до = души родителя после + головы
//       рождённых + их аватары, и плата — ровно крю;
//   (б) ЕДУТ ТЕ ЖЕ ГОЛОВЫ: людской стак иного рода, чем аватар, и не
//       первого уровня, стоящий первым в очереди отдачи, уходит из дома
//       целиком и стоит у рождённого тем же родом и уровнем;
//   (в) ЛЮДСКОЙ ДУШИ НЕТ — РОЖДЕНИЯ НЕТ: дверь отказывает невалидным
//       хэндлом, строки не встаёт, контейнер родителя не тронут; одна душа —
//       рождение есть, и счёт сходится (1 = 0 дома + 0 голов + аватар).
// (а) и (б) стоят над каждым из трёх модулей поселений — лот крю у каждого
// своей копией (AGENTS §4 п.5), и мутация одной копии другие не задевает;
// (в) — над самой дверью, мимо модуля: гейт свободных душ модуля до неё
// такого родителя не допускает (закон 6 ниже), и иначе её отказ остался бы
// без свидетеля.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (§8 п.6; собирался только этот таргет, 886
// проверок):
//   `AiPult::spawn`: снят `bleed_flock(s.bag, 1)` — аватар даром
//       → КРАСНЫЙ 12: (а) и (б) над тремя модулями, (в), законы 2 и 6;
//   `village_raise_crew_`: лот на `крю − 2` голов
//       → КРАСНЫЙ 4: плата не крю (здесь, в законах 2 и 6); «счёт точен»
//         ЗЕЛЁНЫЙ честно — недоданная душа не пропала, она осталась дома;
//   то же в `city_raise_crew_`, отдельно в `capital_raise_crew_`
//       → КРАСНЫЙ 1 каждая: плата не крю у своей копии, деревня зелёная;
//   прежний путь: ростер «из спецификации» (`Peasant` первого уровня,
//   `крю − 1` голов) и плата `bleed_flock` на всё крю, лот не едет ни у
//   одного из трёх модулей
//       → КРАСНЫЙ 4: (б) над тремя модулями и (в) — головы из воздуха;
//         «счёт точен» ЗЕЛЁНЫЙ — число душ то же, головы другие, и ловит
//         это ровно (б);
//   `AiPult::spawn`: снят отказ «людской души нет»
//       → КРАСНЫЙ 2: (в), обе проверки строки;
//   МОЛЧИТ `village_crew_lot_`: локаль `lines` переименована → ЗЕЛЁНЫЙ.

// Головы строки `kind` уровня `level` в контейнере — вопрос прибора к слотам.
int heads_of(const Inventory& inv, NPCType kind, int level) {
    const std::uint16_t row = world_row_of_creature(kind);
    int n = 0;
    for (const ItemRef& s : inv.slots)
        if (!s.empty() && s.def == row && int(s.level) == level) n += s.count;
    return n;
}

// Живых строк массива — «встал ли сквад» спрашивается у мира, не у хэндла.
int live_squads(const MacroStore& st) {
    int n = 0;
    for (std::size_t s = 0; s < kUnifiedCap; ++s)
        if (st.alive[s] != 0 && st.dead[s] == 0) ++n;
    return n;
}

void test_birth_hands_the_parents_own_heads() {
    // МЕТКА — людская голова иного рода, чем аватар двери рождения
    // (`Peasant`), и не первого уровня: «из спецификации» её не родить.
    constexpr NPCType kMarkKind = NPCType::Guard;
    const int markLevel = npc_def(kMarkKind).baseLevel;
    CHECK(is_folk_kind(std::uint16_t(kMarkKind)) && markLevel != 1,
          "метка — людская голова не первого уровня");
    // Одна метка уходит аватаром, остальные — головами лота: значит меток
    // не больше крю, иначе часть по праву осталась бы дома.
    constexpr int kMarks = 3;
    static_assert(kMarks <= kVillageCrewSouls && kMarks <= kCityCrewSouls
                      && kMarks <= kCapitalCrewSouls,
                  "все метки обязаны уйти в крю");

    struct Parent {
        const char* who;
        SquadType kind;
        int crew;
        SquadType child;
    };
    constexpr Parent kParents[] = {
        {"деревня", SquadType::Village, kVillageCrewSouls,
         SquadType::Ploughmen},
        {"город", SquadType::City, kCityCrewSouls, SquadType::Caravan},
        {"столица", SquadType::Capital, kCapitalCrewSouls,
         SquadType::Caravan},
    };
    for (const Parent& p : kParents) {
        std::printf("  [рождение: %s]\n", p.who);
        Fixture f;
        const MacroHandle home = f.rooted(p.kind, 10, 10, 64);
        const int homeId = int(f.st().spawnId[home.slot].index);
        if (p.kind == SquadType::Village) {
            // Нужда и пашня — без них деревня крю не поднимает (закон 2).
            f.plough(12, 10);
            f.st().upkeep[home.slot].needDebt[hunger_commodity_ordinal()] =
                4096;
        }
        Inventory& bag = f.st().inventory[home.slot].inv;
        CHECK(creatures_push_stack(bag, kMarkKind, markLevel, kMarks),
              "метки встали в контейнер родителя");
        CHECK(bag.slots[std::size_t(bag.creature_first())].def
                  == world_row_of_creature(kMarkKind),
              "метка — первая в очереди отдачи: новейший людской стак стоит "
              "у нижней границы области существ");
        const int before = f.heads(home.slot);

        f.think(kThinksPerDay + 2);

        int born = -1;
        int kids = 0;
        int kidSouls = 0;   // головы детей плюс по аватару на каждого
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (f.st().alive[s] == 0 || f.st().dead[s] != 0) continue;
            // Место — дом себе (self-связь `Parent`, шапка `set_relation` в
            // `src/bus/store.h`), и своим ребёнком оно не считается.
            if (s == home.slot) continue;
            if (parent_of(f.st(), std::uint16_t(s)) != homeId) continue;
            ++kids;
            kidSouls += f.heads(std::uint16_t(s)) + 1;
            if (f.st().runtime[s].squadType == std::uint8_t(p.child))
                born = int(s);
        }
        CHECK(born >= 0 && kids == 1,
              "родитель родил одного ребёнка нужного типа");
        const int after = f.heads(home.slot);
        CHECK(before == after + kidSouls,
              "(а) СЧЁТ ДУШ ТОЧЕН: души родителя до = души после + головы "
              "рождённого + его аватар — ни одна душа не возникла и не "
              "пропала");
        CHECK(before - after == p.crew,
              "(а) и плата — ровно крю: аватар одна душа, головы лота — "
              "остальные");
        CHECK(heads_of(bag, kMarkKind, markLevel) == 0,
              "(б) метки дома нет: первая в очереди, она ушла вся");
        if (born >= 0) {
            const Inventory& kid = f.st().inventory[std::size_t(born)].inv;
            CHECK(heads_of(kid, kMarkKind, markLevel) == kMarks - 1,
                  "(б) ЕДУТ ТЕ ЖЕ ГОЛОВЫ: у рождённого метки того же рода и "
                  "уровня — все, кроме одной, ставшей аватаром");
        }
    }

    // ── (в) ДВЕРЬ РОЖДЕНИЯ: ЛЮДСКОЙ ДУШИ НЕТ — СТРОКИ НЕТ ────────────────
    {
        Fixture f;
        const MacroHandle home = f.rooted(SquadType::City, 10, 10, 0);
        const int homeId = int(f.st().spawnId[home.slot].index);
        Inventory& bag = f.st().inventory[home.slot].inv;
        // Головы есть, людских нет: «пусто» значит именно душ, а не голов.
        CHECK(creatures_push_stack(bag, NPCType::Horse,
                                   npc_def(NPCType::Horse).baseLevel, 4)
                  && bag.sum().heads == 4 && count_human_souls(bag) == 0,
              "(в) предусловие: четыре головы скота, людских душ ноль");
        const int squadsBefore = live_squads(f.st());
        const std::uint64_t revBefore = bag.revision();
        TickContext ctx{};
        ctx.mw = f.world();
        ctx.mapW = kMap;
        ctx.mapH = kMap;
        AiPult pult(ctx);
        MacroStore& st = f.st();
        AiSelf self{home, st.cell[home.slot], st.runtime[home.slot],
                    st.pools[home.slot], st.goal[home.slot], bag,
                    st.sheet[home.slot]};
        const MacroHandle none = pult.spawn(self, SquadType::Caravan);
        CHECK(!st.valid(none) && live_squads(st) == squadsBefore,
              "(в) ЛЮДСКОЙ ДУШИ НЕТ — РОЖДЕНИЯ НЕТ: хэндл невалиден, строки "
              "в массиве не встало");
        CHECK(bag.revision() == revBefore,
              "(в) и контейнер родителя не тронут ни одной записью");

        // Противоположная полярность: одна душа — рождение есть.
        CHECK(settle_souls(f.gs, st, home.slot, 1) == 1,
              "(в) фикстура: одна людская душа встала дома");
        const MacroHandle one = pult.spawn(self, SquadType::Caravan);
        CHECK(st.valid(one) && live_squads(st) == squadsBefore + 1,
              "(в) одна душа — рождение есть: строка встала");
        CHECK(st.valid(one) && count_human_souls(bag) == 0
                  && f.heads(one.slot) == 0
                  && parent_of(st, one.slot) == homeId,
              "(в) и счёт сошёлся: душа дома ушла аватаром, голов у "
              "рождённого ноль (ростер пуст), родитель назван его домом");
    }
}

// ═════ 3. СБОРЩИК — ПРИ ВАССАЛЕ, ОДИН РАЗ ЗА СЕЗОН ═════════════════════
// Ряд M-495, вердикт владельца 2026-11-14 «Пришёл — забрал 1/8, без книги
// долгов»: должников в мире нет, поэтому сборщика поднимает не долг, а ВАССАЛ
// — и в сезоне ОДИН раз, иначе замена погибшего посреди сезона начала бы обход
// заново и обошла бы первых вассалов второй раз. Три плеча над каждым из двух
// модулей: без вассала — нет; с вассалом — есть; погиб посреди сезона — в том
// же сезоне замены нет, в следующем — есть (контроль того, что гейт сезона не
// «больше никогда»).
//
// Сезон мира — мировая переменная, и фикстура ставит его сама дверью часов
// (`world_time_at@src/core/time.h`): думки часов не двигают.
void test_city_raises_a_collector_for_a_vassal_once_a_season() {
    for (const TownModule& m : kTownModules) {
        std::printf("  [сборщик: %s]\n", m.who);
        Fixture f;
        f.gs.worldTime = world_time_at(1, 8, 0);
        const MacroHandle city = f.rooted(m.kind, 10, 10, 64);
        const MacroHandle vassal = f.rooted(SquadType::Village, 14, 10, 64);
        const int cityId = int(f.st().spawnId[city.slot].index);
        const int vassalId = int(f.st().spawnId[vassal.slot].index);

        // ── НЕГАТИВНОЕ ПЛЕЧО: ВАССАЛА НЕТ — СБОРЩИКА НЕТ ────────────────────
        f.think(kThinksPerDay + 2);
        CHECK(f.children_of(cityId, SquadType::Collector) == 0,
              "БЕЗ ВАССАЛА СБОРЩИКА НЕТ: обходить ему некого");
        // А КОРОВАН ПРИ ЭТОМ ЕСТЬ — ЭТО РАЗДЕЛЯЕТ ДВА РЕШЕНИЯ ГОРОДА. Без этой
        // проверки «сборщика нет» могло бы значить «город вообще не думает», и
        // негативное плечо выше оказалось бы вакуумным (§8 п.2).
        CHECK(f.children_of(cityId, SquadType::Caravan) == 1,
              "город ПРИ ТОМ ЖЕ ПРОГОНЕ поднял корован — значит думка шла, и "
              "ноль сборщиков есть РЕШЕНИЕ, а не молчание модуля");

        // ── ПОЛОЖИТЕЛЬНОЕ ПЛЕЧО: ВАССАЛ ПОЯВИЛСЯ ────────────────────────────
        // ФЕОД СТАВИТСЯ ДВЕРЬЮ МИРА: роль зовётся с НИЖНЕГО конца — вассал
        // получает `Suzerain`, сюзерен зеркало `Vassal` (store.h set_relation).
        set_relation(f.st(), vassalId, cityId, Stance::Suzerain);
        CHECK(suzerain_of(f.st(), vassal.slot) == cityId,
              "фикстура: феод встал дверью мира");
        f.think(kThinksPerDay + 2);
        CHECK(f.children_of(cityId, SquadType::Collector) == 1,
              "ВАССАЛ ПОЯВИЛСЯ — ГОРОД ПОДНЯЛ СБОРЩИКА, и вопрос этот задан "
              "СВОИМИ рёбрами, а не поиском по миру");

        // ── ПОГИБ ПОСРЕДИ СЕЗОНА — ЗАМЕНЫ В ЭТОМ СЕЗОНЕ НЕТ ─────────────────
        int collector = -1;
        for (std::size_t sl = 0; sl < kUnifiedCap; ++sl) {
            if (f.st().alive[sl] == 0 || f.st().dead[sl] != 0) continue;
            if (f.st().runtime[sl].squadType
                    == std::uint8_t(SquadType::Collector)
                && parent_of(f.st(), std::uint16_t(sl)) == cityId)
                collector = int(sl);
        }
        CHECK(collector >= 0, "фикстура: сборщик города нашёлся слотом");
        if (collector >= 0)
            store_death(f.st(), handle_at(f.st(), std::uint16_t(collector)));
        CHECK(f.children_of(cityId, SquadType::Collector) == 0,
              "фикстура: сборщик погиб");
        const int headsBefore = f.heads(city.slot);
        f.think(kThinksPerDay + 2);
        CHECK(f.children_of(cityId, SquadType::Collector) == 0,
              "В ТОМ ЖЕ СЕЗОНЕ ВТОРОГО СБОРЩИКА НЕТ: замена погибшего начала бы "
              "обход с первого вассала и обошла бы его второй раз");
        CHECK(f.heads(city.slot) == headsBefore,
              "и душ на него город не выписал — рождения не было вовсе");

        // ── НОВЫЙ СЕЗОН — СБОРЩИК СНОВА ПОДНИМАЕТСЯ ─────────────────────────
        f.gs.worldTime = world_time_at(1 + kDaysPerSeason, 8, 0);
        f.think(kThinksPerDay + 2);
        CHECK(f.children_of(cityId, SquadType::Collector) == 1,
              "В НОВОМ СЕЗОНЕ СБОРЩИК ПОДНЯТ СНОВА: гейт сезона держит один "
              "сезон, а не «больше никогда»");
    }
}

// ═════ 4. ДУБЛЬ НЕ РОЖДАЕТСЯ ════════════════════════════════════════════
// Гейт `has_child` и есть то, что делает колонку «число рождённых»
// ненужной: выводимое не хранится (ЗАКОН АНКЕТЫ п.2, шрам трёх носителей).
void test_no_duplicate_child_however_long_the_world_runs() {
    Fixture f;
    const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 200);
    const int vilId = int(f.st().spawnId[vil.slot].index);
    f.st().upkeep[vil.slot].needDebt[hunger_commodity_ordinal()] = 1 << 20;
    f.plough(12, 10);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    f.think_on(rt, kThinksPerDay + 2);
    CHECK(f.children_of(vilId, SquadType::Ploughmen) == 1
              && f.artels_of(vilId) == 1,
          "первая артель поднялась — пахари, единственное место работы");
    const int headsAfterFirst = f.heads(vil.slot);

    // ЧЕТВЕРО СУТОК СВЕРХУ, И ДУШ В ДОМЕ С ИЗБЫТКОМ (200 против крю): если
    // бы гейт дубля не держал, их хватило бы ещё на двадцать артелей.
    f.think_on(rt, 4 * kThinksPerDay);
    // Судится СУММА по шести типам: артель вправе сменить тип своим модулем
    // (пашню выели — стала неопределённой), и рождением это не является.
    CHECK(f.artels_of(vilId) == 1,
          "ПЯТЫЕ СУТКИ — АРТЕЛЬ ВСЁ ЕЩЁ ОДНА: дубль гасит `has_child`, и "
          "хранить «сколько я родил» колонкой незачем");
    CHECK(f.heads(vil.slot) == headsAfterFirst,
          "ЯКОРЬ ПЛАТЫ: за четверо суток дом не заплатил НИ ОДНОЙ лишней "
          "душой — то есть рождения действительно не было, а не было "
          "рождение с возвратом");
}

// ═════ 5. СТОЯЩЕЕ МЕСТО НЕ ЖЖЁТ ПЛАНКУ И НЕ ЛОЖИТСЯ В ПРИВАЛ ════════════
// M-313, ветка Б (вердикт владельца 2026-10-25). Закон: тело, которое никуда
// не шло, за дорогу не платит. Носитель — цель на СВОЮ клетку, которую пишет
// `birth_squad@src/macro/birth_squad.h`: `at_goal` делается истинным по
// построению, `resting` — истинным, и половина жжения в
// `settle_pools_over_time@src/macro/recovery.cpp` не берётся вовсе.
//
// ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ОБЯЗАН БЫЛ ПОЯВИТЬСЯ: дефект прошёл мимо ВСЕХ 135
// приборов и нашёлся только замером приёмки (`placeResting` 1505 из 2458).
// Ни один прибор не спрашивал, цела ли планка у того, кто стоит.
void test_a_standing_rooted_pays_nothing_for_the_road() {
    Fixture f;
    const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 64);
    const MacroHandle city = f.rooted(SquadType::City, 20, 20, 64);

    // ── ИНВЕНТАРЬ СКВАДА ПОЛОН, И БЕЗ ЭТОГО ПРОВЕРКА БЫЛА БЫ ТАВТОЛОГИЕЙ ─
    // «Склада» как рода вещей НЕ СУЩЕСТВУЕТ (владелец, дословно: «никакого
    // склада нет универсальная система сквадов с инвентареём плоские
    // массивы») — есть ОДИН контейнер сквада в единой системе, и ровно его
    // массу ритм марша читает как носимый ПЕРЕГРУЗ (шрам M-310). Он и
    // поднимает жжение выше регена: у живых мест `overload` замерен **3.6**,
    // потому что масса их инвентаря кратно обходит спину. Пустой контейнер
    // даёт перегруз **1.0**, жжение 5.0 × 2.0 = 10.0 SP/час против регена
    // 13.75 — планка НЕ САДИТСЯ НИ У КОГО, и всё, что ниже, зеленеет само.
    // Поймала это таблица мутаций, а не глаза: снос строки цели в
    // `birth_squad` ронял только проверки про саму цель.
    for (const MacroHandle h : {vil, city}) {
        CHECK(f.st().inventory[h.slot].inv.add_of(item_index("stone"), 4096),
              "контейнер сквада набит камнем — предусловие рождено прибором, а не "
              "выпрошено у генератора (§8 п.11)");
    }

    // ЯКОРЬ ПРЕДУСЛОВИЯ, И ОН СЧИТАЕТСЯ ПУБЛИЧНЫМИ ДВЕРЬМИ ЗАКОНА, А НЕ
    // ПЕРЕСКАЗОМ ЕГО ФОРМУЛЫ: «на этой земле с этим грузом МАРШИРУЮЩЕЕ тело
    // теряло бы планку». Без этого утверждения закон ниже («а стоящее —
    // не теряет») не отличался бы от «тут вообще никто ничего не теряет».
    for (const MacroHandle h : {vil, city}) {
        const ecs::Pools& hp = f.st().pools[h.slot];
        const Inventory& inv = f.st().inventory[h.slot].inv;
        const float burn = burn_stamina_per_hour(
            kFixtureCellWeight,
            overload_burden_from_capacity(
                carry_capacity_g(f.st().runtime[h.slot], inv), inv));
        const float rest = rest_stamina_per_hour(
            int(hp.maxSp), int(f.st().runtime[h.slot].marathonRank));
        CHECK(burn > rest,
              "ПРЕДУСЛОВИЕ ЖИВО: жжение ПУТИ на этой земле с этим грузом "
              "обгоняет реген — значит «планка цела» ниже есть РЕЗУЛЬТАТ "
              "закона, а не свойство беззубой фикстуры");
    }

    // ЯКОРЬ ПЕРВЫЙ — ЦЕЛИ У МЕСТА НЕТ ВОВСЕ, И ЗАКОН ДЕРЖИТСЯ БЕЗ НЕЁ. Здесь
    // утверждалось обратное — «цель есть, и она своя клетка»: тогда отдых
    // судился «причиной стоять», и адрес на своей клетке был его носителем
    // (ветка Б ряда M-313). С M-465 отдых есть думка без хода, носитель снят
    // (вердикт владельца 2026-11-12), и свидетель обязан держать закон на
    // том, что осталось: пустой цели довольно.
    std::uint32_t bornAt[2] = {};
    int who = 0;
    for (const MacroHandle h : {vil, city}) {
        CHECK(f.st().goal[h.slot].cell == ecs::kNoCell,
              "у новорождённого места АДРЕСА НЕТ — колонку цели рождение "
              "оставило пустой");
        CHECK(f.st().goal[h.slot].underway == 0,
              "и оно никуда не в пути");
        CHECK(ecs::goal_kind(f.st().goal[h.slot].what) == ecs::GoalKind::None,
              "род цели — None: предиката у пустой цели тоже нет");
        bornAt[who++] = f.st().cell[h.slot].idx;
    }

    const int vilSpStart = int(f.st().pools[vil.slot].sp);
    const int citySpStart = int(f.st().pools[city.slot].sp);
    CHECK(vilSpStart == int(f.st().pools[vil.slot].maxSp) && vilSpStart > 0,
          "место рождено с ПОЛНОЙ планкой — иначе просадка ниже мерилась бы "
          "от неизвестной базы");

    // ЧЕТВЕРО СУТОК СТОЯНИЯ. Горизонт не с потолка: при жжении 25–37 SP/час
    // против регена 13.75 планка 110 выедается до камп-порога (1/8 = 13.75)
    // за ~2.5 игровых часа, то есть первые же сутки обязаны были бы уронить
    // её в привал. Четверо суток — запас вчетверо.
    f.think(4 * kThinksPerDay);

    for (const MacroHandle h : {vil, city}) {
        CHECK(int(f.st().pools[h.slot].sp) == int(f.st().pools[h.slot].maxSp),
              "ПЛАНКА СТОЯЩЕГО МЕСТА ЦЕЛА ЧЕТВЕРО СУТОК: за дорогу, которой "
              "не было, оно не заплатило ничем");
        CHECK(f.st().runtime[h.slot].state
                  != std::uint8_t(NPCState::Resting),
              "и в ПРИВАЛ оно не легло — а лёгшее перестаёт ДУМАТЬ "
              "(`ThinkGate::Rest`), то есть перестаёт и производить");
        CHECK(f.st().runtime[h.slot].state
                  != std::uint8_t(NPCState::Traveling),
              "и в путь не ушло: без адреса ног звать некуда");
    }
    CHECK(f.st().cell[vil.slot].idx == bornAt[0]
              && f.st().cell[city.slot].idx == bornAt[1],
          "и с места не сдвинулись — клетки те же, что при рождении");
    CHECK(citySpStart == int(f.st().pools[city.slot].sp),
          "у города та же планка, что в начале — закон один на оба рода, "
          "ветки «а городу иначе» нет");
}

// ═════ 6. ПОСЛЕДНЮЮ СМЕНУ РУК ИЗ ДОМА НЕ ВЫПИСЫВАЮТ ═════════════════════
// Наряд M-314, заведён НЕЗАВИСИМЫМ РЕВЬЮ 2026-10-25. Закон:
// `village_has_spare_souls_@src/macro/squad_ai/village.cpp` и
// `city_has_spare_souls_@src/macro/squad_ai/city.cpp` (у каждого модуля свой) требуют
// СТРОГО больше крю, а не
// «не меньше», — потому что дом, отдавший последнюю смену рук, встаёт с
// НУЛЁМ работников у своих же верстаков, то есть перестаёт производить тем
// самым решением, которым собирался производить больше.
//
// ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ОБЯЗАН БЫЛ ПОЯВИТЬСЯ — ЧИСЛОМ, А НЕ ВКУСОМ. Гейт
// НЕСУЩИЙ, и собственная дверь рождения его не дублирует: `AiPult::spawn`
// требует лишь одной людской души — на аватара, — а лот голов крю уводит
// остальные семь, то есть при РАВЕНСТВЕ дом уходит в крю весь. Зонд ревью
// назвал цену: деревня РОВНО с `kVillageCrewSouls` душами и живой нуждой
// давала `heads` **8 → 0** и `artels` **0 → 1** со снятым гейтом. При этом ТРИ мутации гейта — снят у деревни,
// снят у города, строгое `>` заменено на `>=` — проходили **ЗЕЛЁНЫМИ 62/62**:
// закон стоял в дереве без единого свидетеля (§8 п.6).
void test_the_last_shift_of_hands_is_never_written_out() {
    const int clayIdx = item_index("clay");
    const int bricksIdx = item_index("bricks");
    CHECK(clayIdx >= 0 && bricksIdx >= 0,
          "каталог знает глину и кирпич — ординалы резолвятся на границе");
    constexpr int kClay = 512;

    // ── ПЛЕЧО ОТКАЗА: ДУШ РОВНО НА ОДНО КРЮ ─────────────────────────────
    {
        Fixture f;
        const MacroHandle vil =
            f.rooted(SquadType::Village, 10, 10, kVillageCrew);
        const int vilId = int(f.st().spawnId[vil.slot].index);
        f.st().upkeep[vil.slot].needDebt[hunger_commodity_ordinal()] = 1 << 20;
        // Нужда и место работы есть — отказать вправе ТОЛЬКО гейт душ.
        f.plough(12, 10);
        Inventory& shelf = f.st().inventory[vil.slot].inv;
        CHECK(shelf.add_of(clayIdx, kClay),
              "глина легла на полку — иначе «дом всё ещё производит» ниже "
              "нечем было бы проверить");

        // ЯКОРЬ ВРЕДА — ПУБЛИЧНОЙ ДВЕРЬЮ РЕШЕНИЯ, А НЕ ПЕРЕСКАЗОМ ЕЁ ФОРМУЛЫ
        // (§8 п.5): цена акта делится на рабочих юнитов, и дом, выписавший
        // всех людей, остаётся с одним аватаром — тот же бар покупает ему
        // меньше работы НА ТОЙ ЖЕ материи и с ТОЙ ЖЕ анкетой. Без этого
        // утверждения «артели нет» не отличалось бы от «а и не надо».
        // (Здесь стояло «без рук — ноль партий»: руками была квота станков,
        // и дом без душ не крафтил вовсе. Квота снята 2026-11-13, ряд M-486.)
        {
            const ecs::Pools& bar = f.st().pools[vil.slot];
            const Skills& skills = f.st().sheet[vil.slot].skills;
            // Решение о крафте у деревни, у города и у столицы своё (AGENTS
            // §4 п.5), и якорь стоит над всеми тремя.
            using PickFn = CraftPick (*)(const Inventory&,
                                         const std::int32_t*, const Skills&,
                                         std::int64_t, int, int, int, int);
            for (const PickFn pick : {PickFn(&village_craft_pick),
                                      PickFn(&city_craft_pick),
                                      PickFn(&capital_craft_pick)}) {
                const auto pick_with = [&](int units) {
                    return pick(shelf, nullptr, skills, /*backG*/1000000,
                                faction_index("timaert"), int(bar.maxSp),
                                int(bar.maxSp), units);
                };
                const CraftPick staffed = pick_with(1 + kVillageCrew);
                const CraftPick emptied = pick_with(1);
                CHECK(emptied.batches > 0 && staffed.batches > emptied.batches,
                      "ВОТ ЧЕМ ГРОЗИТ ВЫПИСАННАЯ ПОСЛЕДНЯЯ СМЕНА: та же полка, "
                      "та же анкета и тот же бар без людей покупают меньше "
                      "партий — гейт охраняет способность дома работать");
            }
        }

        f.think(kThinksPerDay + 2);

        CHECK(f.artels_of(vilId) == 0,
              "АРТЕЛИ НЕТ: душ ровно на крю, и последнюю смену рук дом не "
              "выписывает — строгое «больше», а не «не меньше»");
        CHECK(f.heads(vil.slot) == kVillageCrew,
              "и душ он не потерял НИ ОДНОЙ — отказ случился ДО платы, а не "
              "рождением с возвратом");
        CHECK(shelf.count_of(bricksIdx) > 0,
              "А ДОМ ПРИ ЭТОМ ПРОИЗВОДИТ — то есть отказ сохранил именно то, "
              "ради чего он и стоит, и «артели нет» не значит «дом умер»");
    }

    // ── ПЛЕЧО СОГЛАСИЯ: ДУШ НА ОДНУ БОЛЬШЕ ──────────────────────────────
    // Оно же негативный контроль ПРОТИВОПОЛОЖНОЙ полярности: гейт, ставший
    // «отказывать всегда», ронял бы ровно эти три проверки, а плечо отказа
    // выше оставлял бы зелёным (§8 п.6 — детектор, краснеющий на всё, не
    // детектор).
    {
        Fixture f;
        const MacroHandle vil = f.rooted(SquadType::Village, 10, 10,
                                         kVillageCrew + 1);
        const int vilId = int(f.st().spawnId[vil.slot].index);
        f.st().upkeep[vil.slot].needDebt[hunger_commodity_ordinal()] = 1 << 20;
        f.plough(12, 10);
        Inventory& shelf = f.st().inventory[vil.slot].inv;
        CHECK(shelf.add_of(clayIdx, kClay), "и здесь глина на полке");

        f.think(kThinksPerDay + 2);

        CHECK(f.artels_of(vilId) == 1,
              "ОДНОЙ ДУШИ СВЕРХ КРЮ ДОСТАТОЧНО: гейт считает ИЗБЫТОК, а не "
              "запас, и при избытке в одну душу крю уходит");
        CHECK(f.heads(vil.slot) == 1,
              "ЯКОРЬ ПЛАТЫ С ОБОИХ ПЛЕЧ: дома осталась РОВНО та одна душа, "
              "которая и сделала избыток, — значит заплачено ровно крю");
        CHECK(shelf.count_of(bricksIdx) > 0,
              "и дом с одной душой ВСЁ ЕЩЁ производит: порог гейта стоит "
              "там, где кончается способность работать, а не произвольно");
    }

    // ── ПЛЕЧО ГОРОДА И СТОЛИЦЫ: ЗАКОН ОДИН НА ВСЕ РОДА, И САЙТОВ ГЕЙТА У
    // КАЖДОГО МОДУЛЯ ДВА ─────────────────────────────────────────────────
    // Без этого плеча два вызова гейта в `city_think` (сборщик и корован)
    // остаются без свидетеля: мутация, снимающая гейт ТОЛЬКО у города,
    // проходит зелёной по деревенским плечам выше. То же у `capital_think` —
    // своё тело, свои два сайта. Вассал назван нарочно — иначе ветка
    // сборщика не дошла бы до гейта вовсе, и плечо доказывало бы один сайт из
    // двух.
    for (const TownModule& m : kTownModules) {
        std::printf("  [последняя смена: %s]\n", m.who);
        Fixture f;
        const MacroHandle city = f.rooted(m.kind, 10, 10, m.crew);
        const MacroHandle vassal =
            f.rooted(SquadType::Village, 14, 10, kVillageCrew);
        const int cityId = int(f.st().spawnId[city.slot].index);
        const int vassalId = int(f.st().spawnId[vassal.slot].index);
        set_relation(f.st(), vassalId, cityId, Stance::Suzerain);
        CHECK(suzerain_of(f.st(), vassal.slot) == cityId,
              "вассал у города есть — значит ветка сборщика ДОХОДИТ до гейта "
              "свободных душ, а не отсекается раньше");

        f.think(kThinksPerDay + 2);

        CHECK(f.children_of(cityId, SquadType::Collector) == 0,
              "ГОРОД НЕ ПОДНЯЛ СБОРЩИКА при душах ровно на крю — хотя "
              "вассал есть, то есть отказал именно гейт свободных душ");
        CHECK(f.children_of(cityId, SquadType::Caravan) == 0,
              "и КОРОВАНА не поднял — второй сайт того же гейта, у которого "
              "гейта нужды нет вовсе");
        CHECK(f.heads(city.slot) == m.crew,
              "и душ город не потерял ни одной");
    }
}

// ═════ 7. УМИРАЮЩИЙ ОСТАВЛЯЕТ ПО СЕБЕ ПРАВДУ В ФИЧЕ ═════════════════════
// Вердикт владельца 2026-10-25: строка смерти есть ДАННЫЕ рода, а не общий
// закон («это не какотой общий закон а чисто внутри каждого модуля своё»).
// Город → РУИНА, деревня → СНЯТЬ ФИЧУ (поле заросло), подвижный род → ничего.
//
// ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ОБЯЗАН БЫЛ ПОЯВИТЬСЯ — ЧИСЛОМ: замер 128 дней (сид 7)
// нашёл 35 ушедших строк мест, и ФИЧА ОСТАВАЛАСЬ ЖИВА **35 из 35**. То есть
// мир держал фичу деревни, на которой не живёт никто, — а фича есть РЕСУРС
// клетки (ЗАКОН ПОЛЯ п.3), и соврать ею дороже, чем потерять строку.
void test_a_dying_rooted_rewrites_the_feature_under_itself() {
    const auto feature_of = [](Fixture& f, const MacroHandle h) {
        return f.features.at(f.st().cell[h.slot].idx);
    };
    // Смерть ОДНОЙ дверью мира, той же, которой кончает автобой.
    const auto kill = [](Fixture& f, const MacroHandle h) {
        settle_avatar_fraction(f.st(), h, 0.0f);
    };

    // ── ГОРОД → РУИНА, СТОЛИЦА → РУИНА ──────────────────────────────────
    // Строка смерти у каждого модуля своя (`city_death`, `capital_death`), и
    // свидетель стоит над обеими.
    for (const TownModule& m : kTownModules) {
        std::printf("  [смерть: %s]\n", m.who);
        Fixture f;
        const MacroHandle city = f.rooted(m.kind, 10, 10, 64);
        CHECK(feature_of(f, city) == feature_of_rooted_kind(m.kind)
                  && feature_of(f, city) != FT_None
                  && feature_of(f, city) != FT_Ruin,
              "ПРЕДУСЛОВИЕ: под местом стоит ЕГО ФИЧА (города — города, "
              "столицы — столицы) — иначе проверка ниже не отличала бы "
              "«переписал» от «тут и так было»");
        kill(f, city);
        f.think(1);
        CHECK(!f.st().valid(city) || f.st().dead[city.slot] != 0,
              "строка города ушла из мира — свип её прибрал");
        CHECK(feature_of(f, city) == FT_Ruin,
              "ГОРОД ОСТАВИЛ ПО СЕБЕ РУИНУ: фича говорит «здесь ЖИЛИ», а не "
              "«здесь город», и это строка ЕГО рода, а не закон свипа");
    }

    // ── ДЕРЕВНЯ → ФИЧИ НЕТ ВОВСЕ ────────────────────────────────────────
    // Второе плечо И ЕСТЬ контроль первого: если бы строка смерти была ОДНА
    // на всех, здесь стояла бы руина, и тест покраснел бы.
    {
        Fixture f;
        const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 64);
        CHECK(feature_of(f, vil) == FT_Village,
              "ПРЕДУСЛОВИЕ: под деревней стоит ФИЧА ДЕРЕВНИ");
        kill(f, vil);
        f.think(1);
        CHECK(feature_of(f, vil) == FT_None,
              "ДЕРЕВНЮ ЗАТЯНУЛО: фича снята ВОВСЕ, и это ДРУГОЙ ответ, не "
              "копия городского — руин деревня по себе не оставляет");
    }

    // ── ПОДВИЖНЫЙ РОД НА СМЕРТИ НЕ ПИШЕТ НИЧЕГО ─────────────────────────
    // Строка `nullptr` обязана МОЛЧАТЬ, и проверяется это там, где молчание
    // видно: артель умирает НА КЛЕТКЕ СВОЕЙ ДЕРЕВНИ, и фича деревни обязана
    // остаться деревней. Без этого плеча «фича переписана» могло бы значить
    // «её переписывает кто угодно, кто тут умер».
    {
        Fixture f;
        const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, 200);
        const int vilId = int(f.st().spawnId[vil.slot].index);
        f.st().upkeep[vil.slot].needDebt[hunger_commodity_ordinal()] = 1 << 20;
        f.plough(12, 10);
        f.think(kThinksPerDay + 2);
        CHECK(f.artels_of(vilId) == 1,
              "деревня подняла артель — предусловие рождено прибором");
        const int artel = f.artel_slot_of(vilId);
        CHECK(artel >= 0, "артель нашлась слотом");
        const MacroHandle ah = handle_at(f.st(), std::uint16_t(artel));
        // АРТЕЛЬ СТАВИТСЯ НА КЛЕТКУ ДЕРЕВНИ ПРИБОРОМ, И ЭТО НЕ ПОДГОНКА, А
        // ПРЕДУСЛОВИЕ (§8 п.11). Рождается она действительно у родителя
        // (`AiPult::spawn` берёт его клетку), но к этому дню УЖЕ УШЛА —
        // она ходок с целью добычи, и это правда мира, а не дефект. Чтобы
        // молчание строки `nullptr` было ВИДНО, умирать она обязана там, где
        // фича есть: на пустой клетке «фича не изменилась» зелено само собой.
        f.st().cell[ah.slot] = f.st().cell[vil.slot];
        CHECK(f.st().cell[ah.slot].idx == f.st().cell[vil.slot].idx
                  && f.features.at(f.st().cell[ah.slot].idx) == FT_Village,
              "артель поставлена НА КЛЕТКУ ДЕРЕВНИ, и под ней ЕСТЬ фича — "
              "значит её молчание проверяется там, где оно было бы видно");
        kill(f, ah);
        f.think(1);
        CHECK(feature_of(f, vil) == FT_Village,
              "ФИЧА ДЕРЕВНИ ЦЕЛА: у подвижного рода строка смерти `nullptr`, "
              "и `nullptr` значит «не пишет НИЧЕГО», а не «пишет пустоту»");
    }
}

// ═════ 8. ЧИСЛО ПОД МЕСТОМ: У ПОСЕЛЕНИЯ — ЛЮДИ, У ШАХТЫ — РУДА (M-333) ═══
//
// ЧТО ЗА ЗАКОН. Смысл числу поля придаёт ЧИТАЮЩИЙ МОДУЛЬ, а не мир (ЗАКОН
// ПОЛЯ п.3, вердикт владельца 2026-10-26: «ЧИСЛО ФИЧИ ЭТО ПРОСТО ЧИСЛО! ЕГО
// СМЫСЛ ПРИДАЁТ МОДУЛЬ СКВАДА… для артели модуль крестьян поле фич шахт это
// ресурс что он добывает»). Дневной проход `settle_rooted_day@src/macro/world_tick.cpp`
// читает это число под клеткой ВСЯКОГО места и кормит им
// `population_delta_per_day@src/macro/econ_day.h`, то есть растит его КАК
// ЛЮДЕЙ, не спросив, что оно значит у ЭТОГО рода. Гейт стоит по УМОЛЧАНИЮ —
// «рода, у которого число значит не людей, просто не бывает».
//
// ПОЧЕМУ СЕГОДНЯ НЕ СТРЕЛЯЕТ: сквадами-местами рождаются только City и
// Village — `birth_squad@src/macro/birth_squad.h` зовут ДВЕ строки, обе в
// `populate_rooted_from_politik@src/macro/state.cpp`. Шахта строку реестра
// имеет, а сквадом не рождается; мина заряжена, спускового крючка нет.
//
// ЧТО ЭТОТ СВИДЕТЕЛЬ УТВЕРЖДАЕТ — ТОЛЬКО ЗАКОНЫ, ВЕРНЫЕ И ДО, И ПОСЛЕ
// ПОЧИНКИ: (1) сытое поселение растёт, и оба носителя души ходят парой;
// (2) голодное СТОИТ (ноль поглощающий — тот же закон, не кламп);
// (3) дверь, различающая смысл числа, В МИРЕ ЕСТЬ и отвечает врозь
// (`rooted_is_settlement@src/tables/squad_type.h`: деревня — поселение,
// шахта — нет), значит день починки не потребует новой системы;
// (4) число под шахтой НЕ ЕСТЬ счёт её голов — 512 руды против нуля людей.
//
// ЧЕГО ОН СОЗНАТЕЛЬНО НЕ УТВЕРЖДАЕТ, И ЭТО СКАЗАНО ВСЛУХ (§8 п.7): дрейф
// рудного числа ПЕЧАТАЕТСЯ, но не судится. Утвердить сегодняшнее поведение
// значило бы охранять дефект, а утвердить закон («руда не растёт по
// благополучию и людей из себя не родит») значило бы оставить сюиту красной
// на роде, которого мир не рождает, — тогда как ФОРМА гейта (колонка строки
// рода против предиката) есть вопрос владельцу и уедет с модулями родов
// (M-235). Печать и есть «число названо»: она показывает, ЧТО случится в день,
// когда третья строка `birth_squad` назовёт не поселение.
void test_the_number_under_a_mine_is_ore_and_not_people() {
    Fixture f;
    const auto number_at = [](Fixture& fx, const MacroHandle h) {
        return feature_number_read(fx.gs, fx.st().cell[h.slot].idx);
    };
    const auto one_day = [](Fixture& fx, const MacroHandle h) {
        settle_rooted_day(fx.gs, fx.st(), h.slot, /*day*/1);
    };
    // ЧИСЛО ОДНО НА ОБА РОДА НАРОЧНО: под деревней это паства, под шахтой —
    // руда, и ровно в том, что проход их не различает, всё дело.
    constexpr int kNumber = 512;

    // ── ПОЛОЖИТЕЛЬНЫЙ КОНТРОЛЬ: СЫТОЕ ПОСЕЛЕНИЕ РАСТЁТ, И ПАРА СХОДИТСЯ ──
    const MacroHandle vil = f.rooted(SquadType::Village, 10, 10, kNumber);
    CHECK(number_at(f, vil) == kNumber && f.heads(vil.slot) == kNumber,
          "ПРЕДУСЛОВИЕ: у деревни оба носителя души согласны — число клетки "
          "и головы контейнера по " "512");
    one_day(f, vil);
    const int flockGrew = number_at(f, vil) - kNumber;
    const int headsGrew = f.heads(vil.slot) - kNumber;
    CHECK(flockGrew > 0,
          "СЫТОЕ ПОСЕЛЕНИЕ РАСТЁТ (CANON S25): благополучие рождено 255, и "
          "сутки прохода дали приплод — иначе всё ниже мерило бы НЕРАБОТАЮЩИЙ "
          "проход, то есть ничего");
    CHECK(flockGrew == headsGrew,
          "ЗАКОН ПАРЫ: приплод лёг в ОБА носителя ровно одинаково — число "
          "выросло на тех же, кто встал головами");

    // ── ВТОРОЙ КОНТРОЛЬ: ГОЛОДНОЕ СТОИТ. Ноль благополучия — ЗНАЧЕНИЕ, и
    // рост от него нулевой по самому закону, а не обрезан клампом.
    const MacroHandle hungry = f.rooted(SquadType::Village, 20, 20, kNumber);
    f.st().wellbeing[hungry.slot].seasonWellbeing = 0;
    one_day(f, hungry);
    CHECK(number_at(f, hungry) == kNumber
              && f.heads(hungry.slot) == kNumber,
          "ГОЛОДНОЕ СТОИТ: при нулевом благополучии ни число, ни головы не "
          "двинулись — рост есть ДОЛЯ достатка, а не плоская прибавка");

    // ── ШАХТА: ТОТ ЖЕ ПРОХОД, НО ПОД НЕЙ РУДА ───────────────────────────
    // Предусловие прибор рождает САМ (§8 п.11): мир шахту сквадом не
    // рождает, поэтому её рождает фикстура — БЕЗ ДУШ, как и положено роду,
    // у которого паствы не бывает. Число под ней кладётся дверью мира
    // (`feature_number_write@src/macro/state.h`), ровно как его кладёт генерация жил.
    const MacroHandle mine = f.rooted(SquadType::Mine, 30, 30, /*souls*/0);
    feature_number_write(f.gs, f.st().cell[mine.slot].idx, kNumber);
    CHECK(number_at(f, mine) == kNumber && f.heads(mine.slot) == 0,
          "ПРЕДУСЛОВИЕ ШАХТЫ: под ней 512 РУДЫ и НОЛЬ голов — число и счёт "
          "душ здесь расходятся по построению");
    CHECK(rooted_is_settlement(SquadType::Village)
              && !rooted_is_settlement(SquadType::Mine),
          "ДВЕРЬ РАЗЛИЧЕНИЯ СУЩЕСТВУЕТ И ОТВЕЧАЕТ ВРОЗЬ: деревня — "
          "поселение, шахта — нет. Значит гейт, которого дневному проходу не "
          "хватает, не требует новой системы — он требует ВОПРОСА");

    const int oreBefore = number_at(f, mine);
    one_day(f, mine);
    const int oreAfter = number_at(f, mine);
    const int mintedInMine = f.heads(mine.slot);
    CHECK(oreAfter != mintedInMine,
          "ЯКОРЬ: число под шахтой НЕ ЕСТЬ счёт её голов — 512 руды против "
          "горстки тел, и свести их в одну величину мир не вправе");
    // ПЕЧАТЬ, А НЕ ПРОВЕРКА — довод в комментарии к тесту. Это и есть
    // «мина заряжена» числом: что проход делает с рудой, если род, у
    // которого число значит не людей, всё-таки родится сквадом.
    std::printf("  [M-333] шахта: руда %d → %d (%+d), людей народилось %d\n",
                oreBefore, oreAfter, oreAfter - oreBefore, mintedInMine);

    // ТА ЖЕ ОБЛАСТЬ У СВОДА M-325, И ЕГО ЦИФРА ПЕЧАТАЕТСЯ ТОЖЕ: свод
    // приводит число к головам контейнера, а у шахты голов нет вовсе.
    std::vector<std::int32_t> buf(kUnifiedCap, 0);
    reconcile_rooted_flock(f.gs, f.st(), buf.data());
    std::printf("  [M-333] свод модуля места: руда шахты %d → %d, "
                "паства деревни %d при %d головах\n",
                oreAfter, number_at(f, mine), number_at(f, vil),
                f.heads(vil.slot));
}

}  // namespace

// ═════ 9. КОРОВАН ПО ЗАМЫСЛУ: ГОРОД РОЖДАЕТ И ГРУЗИТ, РЕЙС РАСТИТ СТОИМОСТЬ
// СВОЕГО ТРЮМА И ВОЗВРАЩАЕТ НАТОРГОВАННОЕ ДОМОЙ ═══════════════════════════
// Замысел владельца 2026-11-12 (CANON, блок «AI-МОДУЛИ»): «корован сам
// выбирает рынки … город рождает его и загружает … его цель вырастить
// стоимость своего инвентаря … вернуть всё что наторговваал обогатить свой
// город». Свидетель ничего не ставит рукой: корован рождает сам город, груз
// даёт сам город, рынок называет сам рейс. Крутится только думка.

// Слот единственного ребёнка-корована этого родителя; kMacroNoSlot = нет.
std::uint16_t caravan_of(Fixture& f, int parentId) {
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (f.st().alive[s] == 0 || f.st().dead[s] != 0) continue;
        if (f.st().runtime[s].squadType != std::uint8_t(SquadType::Caravan))
            continue;
        if (parent_of(f.st(), std::uint16_t(s)) == parentId)
            return std::uint16_t(s);
    }
    return kMacroNoSlot;
}

// РЕЙС СТОИТ НА ЭТОЙ КЛЕТКЕ, А НЕ ПРОЕЗЖАЕТ ЕЁ: клетка сквада равна клетке
// его цели. Первая редакция свидетелей пути судила одним равенством клеток и
// принимала ПРОЕЗД СКВОЗЬ рынок, лежащий на прямой к дальнему, за стоянку на
// нём — мутация «поиск выключен» прошла зелёной.
bool stands_at(Fixture& f, std::uint16_t slot, std::uint32_t cell) {
    return f.st().cell[slot].idx == cell && f.st().goal[slot].cell == cell;
}

// Сколько фактов сделки записано на клетке рынка.
int traded_facts_at(const GameState& gs, int x, int y) {
    struct Ctx { int x, y, n; } ctx{x, y, 0};
    const std::unique_ptr<ChronicleLinks> links = make_chronicle_links();
    chronicle_near(gs.chronicle, *links, x, y, /*radiusCells*/0,
                   /*sinceDay*/0,
                   [](void* u, const WorldFact& fact) {
                       Ctx& t = *static_cast<Ctx*>(u);
                       // Клетка факта сверяется САМА: дверь окрестности
                       // отвечает крупнее клетки, и без этой сверки три
                       // рынка в пяти клетках друг от друга читались одним.
                       if (fact.kind == std::uint16_t(FactKind::Traded)
                           && fact.x == t.x && fact.y == t.y)
                           ++t.n;
                   }, &ctx);
    return ctx.n;
}

// Первый факт сделки на клетке рынка целиком — кто его субъект, кто объект и
// какое число; пустой факт — сделки там не было.
WorldFact first_traded_fact_at(const GameState& gs, int x, int y) {
    struct Ctx { int x, y; bool found; WorldFact fact; } ctx{x, y, false, {}};
    const std::unique_ptr<ChronicleLinks> links = make_chronicle_links();
    chronicle_near(gs.chronicle, *links, x, y, /*radiusCells*/0,
                   /*sinceDay*/0,
                   [](void* u, const WorldFact& fact) {
                       Ctx& t = *static_cast<Ctx*>(u);
                       if (!t.found
                           && fact.kind == std::uint16_t(FactKind::Traded)
                           && fact.x == t.x && fact.y == t.y) {
                           t.found = true;
                           t.fact = fact;
                       }
                   }, &ctx);
    return ctx.fact;
}

// ЯКОРЬ ПРИБЫЛИ СДЕЛКИ — НЕ ПЕРЕСЧЁТ ЕЁ ФОРМУЛЫ (§8 п.5-6). Прибыль сделки
// есть «во сколько рейс ценит взятое минус во сколько ценил отданное», и у
// стороны без нужды всякая единица излишка ценится не ниже пола (единица) и
// не выше своей стоимости, а медь стоимостью в единицу — ровно в единицу
// (свидетели оценки запаса в `src/macro/economy.h` под компилятором). Значит
// прибыль ЗАЖАТА разностью трюма до и после сделки: снизу — пришедшее по полу
// минус ушедшее по стоимости, сверху — пришедшее по стоимости минус ушедшее
// по полу. Строки — словарь товаров и монета мира; что разность стоимости
// трюма ими исчерпана, утверждается (иначе якорь молча не видел бы строку).
struct GainAnchor {
    long long low = 0, high = 0;
    bool complete = false;   // все сменившие руки строки учтены
};
GainAnchor gain_anchor(const Inventory& before, const Inventory& after,
                       int coinIdx) {
    GainAnchor a{};
    long long valueDelta = 0;
    const auto tally = [&](int idx) {
        const long long d = (long long)after.count_of(idx) - before.count_of(idx);
        const long long base = item_def_at(idx)->value;
        valueDelta += d * base;
        if (d > 0) { a.low += d; a.high += d * base; }
        if (d < 0) { a.low += d * base; a.high += d; }
    };
    for (int c = 0; c < kCommodityCount; ++c) tally(commodity_item_index(c));
    tally(coinIdx);
    a.complete = valueDelta
        == (long long)inventory_value(after) - inventory_value(before);
    return a;
}

void test_the_caravan_grows_its_hold_and_brings_it_home() {
    Fixture f;
    const MacroHandle city = f.rooted(SquadType::City, 10, 10, 64);
    const MacroHandle mkt = f.rooted(SquadType::Village, 14, 10, 4);
    const int cityId = int(f.st().spawnId[city.slot].index);
    Inventory& cityInv = f.st().inventory[city.slot].inv;
    Inventory& mktInv = f.st().inventory[mkt.slot].inv;
    const int foodCol = commodity_index("food");
    const int coinIdx = item_index("coin_timaert_copper");
    constexpr int kCityFood = 6000;
    constexpr int kMarketPurse = 60000;
    CHECK(cityInv.add("food", kCityFood), "фикстура: излишек еды города");
    CHECK(mktInv.add("coin_timaert_copper", kMarketPurse),
          "фикстура: рынок платёжеспособен");
    // РЫНОК ГОЛОДЕН СЧЁТОМ (CANON S10): непогашенный сезонный счёт и есть
    // его нужда — тем же числом, каким его выставляет мир.
    const int bill = upkeep_bill(mktInv).board;
    f.st().upkeep[mkt.slot].needDebt[foodCol] = bill;
    CHECK(bill > 0, "фикстура: у рынка есть непогашенный счёт еды");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 51u);
    const std::uint32_t homeCell = f.st().cell[city.slot].idx;
    int samples = 0;
    bool loadedAtHome = false;      // город дал груз, пока рейс стоял дома
    bool traded = false;
    int debtAfterFirstDeal = -1;    // счёт рынка сразу после первой сделки
    ecs::MacroNpcRuntime memAfterFirstDeal{};   // память рейса в тот же миг
    bool cameHomeEmpty = false;     // вернулся и отдал всё
    int cityCoinAfterReturn = 0;
    // ПРИБЫЛЬ РЕЙСА ЕСТЬ СУММА ПРИБЫЛЕЙ ЕГО СДЕЛОК: на каждой думке, где счёт
    // сделок вырос на одну, прирост слова прибыли обязан лечь в якорь этой
    // сделки, а на выезде слово обязано стоять нулём.
    Inventory holdBefore{};
    std::uint32_t gainBefore = 0, dealsBefore = 0;
    int dealThinks = 0, anchored = 0, incomplete = 0, needful = 0;
    // СДЕЛКА И ВЫЛИВ ВРЕМЕНИ НЕ СТОЯТ (ряд M-494, пункт (1); CANON S25
    // «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ»): в думку сделки и в думку вылива
    // гейт восстановления рейса пуст, и следующее решение принято той же
    // думкой — после сделки рейс уже в пути (к рынку или домой), после
    // вылива он уже ждёт своим словом памяти либо уже выехал.
    int dealsFree = 0, dumpThinks = 0, dumpsDecided = 0;
    bool homeBefore = false;
    bool wasHome = true;            // рейс стоял на клетке дома
    int departures = 0, departuresFromZero = 0;   // выезды после возврата
    const int kThinks = kThinksPerDay * 6;
    for (int i = 0; i < kThinks; ++i) {
        const std::uint16_t c0 = caravan_of(f, cityId);
        if (c0 != kMacroNoSlot) {
            holdBefore = f.st().inventory[c0].inv;
            gainBefore = f.st().runtime[c0].mem[kCaravanMemTripGain];
            dealsBefore = f.st().runtime[c0].mem[kCaravanMemDeals];
            homeBefore = f.st().cell[c0].idx == homeCell;
        }
        f.think_on(rt, 1);
        ++samples;
        const std::uint16_t c = caravan_of(f, cityId);
        if (c == kMacroNoSlot) continue;
        const Inventory& hold = f.st().inventory[c].inv;
        const ecs::MacroNpcRuntime& crt = f.st().runtime[c];
        const bool atHome = f.st().cell[c].idx == homeCell;
        const bool empty = inventory_mass_g(hold) <= 0
                        && inventory_value(hold) <= 0;
        if (atHome && !empty && !traded) loadedAtHome = true;
        // ВЫЛИВ: дома с грузом до думки — дома пустой после неё (выезд груз
        // уносит, а не теряет, так что пустота дома есть вылив и только он).
        if (c == c0 && homeBefore && atHome && empty
            && (inventory_mass_g(holdBefore) > 0
                || inventory_value(holdBefore) > 0)) {
            ++dumpThinks;
            if (!ecs::squad_recovering(crt)
                && (crt.mem[kCaravanMemWait] > 0
                    || crt.state == std::uint8_t(NPCState::Traveling)))
                ++dumpsDecided;
        }
        if (c == c0 && crt.mem[kCaravanMemDeals] == dealsBefore + 1) {
            ++dealThinks;
            if (!ecs::squad_recovering(crt)
                && (crt.state == std::uint8_t(NPCState::Traveling)
                    || crt.state == std::uint8_t(NPCState::Returning)))
                ++dealsFree;
            const GainAnchor a = gain_anchor(holdBefore, hold, coinIdx);
            const long long grew = (long long)crt.mem[kCaravanMemTripGain]
                                 - (long long)gainBefore;
            if (!a.complete) ++incomplete;
            bool need = false;
            for (int k = 0; k < kCommodityCount; ++k)
                need = need || f.st().upkeep[c].needDebt[k] != 0;
            if (need) ++needful;
            // Якорь двусторонний: прирост слова прибыли лежит между «пришедшее
            // по полу минус ушедшее по стоимости» и обратным.
            if (grew >= a.low && grew <= a.high) ++anchored;
        }
        if (!traded && traded_facts_at(f.gs, 14, 10) > 0) {
            traded = true;
            debtAfterFirstDeal = f.st().upkeep[mkt.slot].needDebt[foodCol];
            memAfterFirstDeal = crt;
        }
        if (traded && atHome && empty && !cameHomeEmpty) {
            cameHomeEmpty = true;
            cityCoinAfterReturn = cityInv.count_of(coinIdx);
        }
        if (cameHomeEmpty && wasHome && !atHome) {
            ++departures;
            if (crt.mem[kCaravanMemTripGain] == 0
                && crt.mem[kCaravanMemDeals] == 0)
                ++departuresFromZero;
        }
        wasHome = atHome;
    }
    const std::uint16_t c = caravan_of(f, cityId);
    CHECK(samples == kThinks && c != kMacroNoSlot,
          "прибор отработал: город САМ поднял корован (рукой его не ставили)");
    CHECK(loadedAtHome,
          "ГРУЗИТ ГОРОД: рейс стоял дома с грузом ДО первой сделки — груз ему "
          "дал модуль родителя");
    CHECK(traded,
          "РЫНОК НАЗВАЛ САМ РЕЙС и доехал: на клетке рынка лёг факт сделки");
    // ЧЕЙ ФАКТ — СКАЗАНО ДВЕРЬЮ ЗАПИСИ
    // (`AiPult::fact@src/macro/squad_ai/module_window.h`): деяние пишется на
    // ДОМ рейса, объект — рынок, число — стоимость сделки. Утверждается
    // ОРДИНАЛАМИ, рождёнными фикстурой, а не пересчётом двери: дом — город,
    // рынок — деревня на клетке сделки.
    {
        const WorldFact sale = first_traded_fact_at(f.gs, 14, 10);
        CHECK(sale.subjectKind == std::uint8_t(FactSubject::Rooted)
                  && sale.subject == std::uint32_t(cityId),
              "СУБЪЕКТ СДЕЛКИ — ДОМ РЕЙСА: факт записан на город, чьим "
              "ребёнком корован торгует, а не на сам корован");
        CHECK(sale.objectKind == std::uint8_t(FactSubject::Rooted)
                  && sale.object
                         == std::uint32_t(f.st().spawnId[mkt.slot].index)
                  && sale.amount > 0,
              "ОБЪЕКТ — РЫНОК, ЧИСЛО — СТОИМОСТЬ: деревня на клетке сделки и "
              "положительная стоимость сменившего руки");
    }
    // В НУЖДУ (единая ценомодель): рынок ценит недостающее выше стоимости, а
    // рейс без нужды — не выше, и первая же сделка кормит рынок.
    CHECK(debtAfterFirstDeal >= 0 && debtAfterFirstDeal < bill,
          "В НУЖДУ: первая сделка погасила счёт рынка — груз ушёл туда, где "
          "его ценят выше");
    // ПАМЯТЬ РЕЙСА ВЕДЁТСЯ: правило возврата судит по четырём числам, и после
    // первой сделки каждое из них обязано быть записано — иначе закон под
    // компилятором считал бы нули.
    CHECK(memAfterFirstDeal.mem[kCaravanMemTripGain] > 0
              && memAfterFirstDeal.mem[kCaravanMemTripTicks] > 0,
          "ПАМЯТЬ РЕЙСА: прибыль первой сделки и пройденный путь записаны");
    CHECK(memAfterFirstDeal.mem[kCaravanMemDeals] == 1
              && memAfterFirstDeal.mem[kCaravanMemPrevLegRate] > 0,
          "ПАМЯТЬ РЕЙСА: сделка сосчитана ровно одна, и её отдача легла в "
          "окно двух плеч");
    CHECK(dealThinks > 0 && incomplete == 0 && needful == 0,
          "предусловие якоря: сделки были, все сменившие руки строки учтены, "
          "и у рейса нет нужды — излишек он ценит между полом и стоимостью");
    CHECK(anchored == dealThinks,
          "ПРИБЫЛЬ РЕЙСА ЕСТЬ СУММА ПРИБЫЛЕЙ СДЕЛОК: на каждой сделке слово "
          "прибыли выросло ровно в пределах её якоря");
    CHECK(dealsFree == dealThinks,
          "СДЕЛКА ВРЕМЕНИ НЕ СТОИТ: в думку каждой сделки гейт восстановления "
          "рейса пуст, и рейс той же думкой уже в пути — к рынку или домой");
    CHECK(dumpThinks > 0,
          "предусловие: рейс хоть раз вылил наторгованное дома");
    CHECK(dumpsDecided == dumpThinks,
          "ВЫЛИВ ВРЕМЕНИ НЕ СТОИТ: в думку вылива гейт пуст, и рейс той же "
          "думкой решил дальше — ждёт своим словом памяти либо выехал");
    CHECK(cameHomeEmpty,
          "РЕЙС ВЕРНУЛСЯ И ОТДАЛ ВСЁ: после сделки корован стоял дома пустым");
    CHECK(cityCoinAfterReturn > 0,
          "ГОРОД ОБОГАЩЁН: монета, которой у города не было вовсе, легла на "
          "его склад — наторгованное вернулось домой");
    CHECK(departures > 0 && departuresFromZero == departures,
          "ВЫЕЗД НАЧИНАЕТ РЕЙС С НУЛЯ: после возврата новый рейс уехал без "
          "прибыли и без сделок прошлого");
    // Монета не чеканится и не сгорает: она ходит между тремя кошельками.
    CHECK(cityInv.count_of(coinIdx) + mktInv.count_of(coinIdx)
              + f.st().inventory[c].inv.count_of(coinIdx) == kMarketPurse,
          "СОХРАНЕНИЕ: монета только переходит из рук в руки");
    std::printf("  [рейс] счёт рынка %d → %d после первой сделки, прибыль "
                "первой сделки %u, сделок рейсов %d (без паузы %d), выливов "
                "%d (решено той же думкой %d), выездов после возврата %d\n",
                bill, debtAfterFirstDeal,
                memAfterFirstDeal.mem[kCaravanMemTripGain], dealThinks,
                dealsFree, dumpThinks, dumpsDecided, departures);
}

// ── ГОРОД ГРУЗИТ РЕЙС И ЛЁГКИМ ЗАВАЛОМ; ВЫГОДНЕЙШЕЕ НА ГРАММ — ПЕРВЫМ; В
// ПРЕДЕЛАХ СПИНЫ; ТОЛЬКО ПУСТОЙ ─────────────────────────────────────────────
// Закон мира (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.2; ряд M-490): завал обязан
// ронять оценку, а «ценит ли выше» решается точно. Дом отдаёт ребёнку то, что
// тот ценит выше него, — и тогда, когда на одной единице, округлённой до
// кванта, разницы не видно. Свидетель рейса выше грузит город завалом в пять
// спин, где старое правило работало тоже; таблица мутаций 2026-11-13 показала,
// что и возврат округления единицы, и перевёрнутый порядок погрузки проходили
// зелёными. Рукой здесь ставится только склад места; корован, груз и рейс —
// дело модулей.
//
// ПОГРУЗКА — РЕШЕНИЕ МОДУЛЯ МЕСТА, И У ГОРОДА И СТОЛИЦЫ ОНО СВОЁ
// (`city_load_child_@src/macro/squad_ai/city.cpp`,
// `capital_load_child_@src/macro/squad_ai/capital.cpp`; ряд M-499 (8)),
// поэтому каждое плечо ниже стоит над обоими модулями. Плечи 2 и 3 добавлены
// с переездом: груз ложится В СВОБОДНУЮ СПИНУ ребёнка (дверь передачи берёт
// её пределом), и грузят ТОЛЬКО пустого — корован, которому груз уже дан,
// дом не догружает.
void test_the_town_loads_its_caravan(const TownModule& m) {
    std::printf("  [погрузка: %s]\n", m.who);
    // МЁРТВЫЙ ЗАКОН — оценка единицы, округлённая до ближайшего кванта;
    // формула выписана нарочно (мутация, которую детектор обязан видеть).
    const auto dead_unit = [](std::int64_t base, std::int64_t fits,
                              std::int64_t spare) {
        if (spare <= 0) return base;
        if (spare >= fits) return std::int64_t(1);
        const std::int64_t w = (2 * base * (fits - spare) + fits) / (2 * fits);
        return w < 1 ? std::int64_t(1) : w;
    };
    const int foodIdx = item_index("food");
    const ItemDef* food = item_def_at(foodIdx);
    const std::int64_t foodG = item_mass_g(*food);
    const int ironIdx = item_index("iron");
    const int stoneIdx = item_index("stone");
    const ItemDef* iron = item_def_at(ironIdx);
    const ItemDef* stone = item_def_at(stoneIdx);
    constexpr int kFlood = 4000;
    // 1. ЛЁГКИЙ ЗАВАЛ: еда занимает восемь сотых спины места.
    {
        Fixture f;
        const MacroHandle city = f.rooted(m.kind, 10, 10, 64);
        const int cityId = int(f.st().spawnId[city.slot].index);
        Inventory& cityInv = f.st().inventory[city.slot].inv;
        const std::int64_t cityFits =
            carry_capacity_g(f.st().runtime[city.slot], cityInv) / foodG;
        const int glut = int(cityFits * 8 / 100);
        CHECK(glut > 0 && cityInv.add("food", glut),
              "фикстура: лёгкий завал еды места");
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 51u);
        int loaded = 0, thinks = 0;
        bool settled = false, deadLawBlind = false;
        for (int i = 0; i < kThinksPerDay * 4 && loaded == 0; ++i) {
            f.think_on(rt, 1);
            ++thinks;
            const std::uint16_t c = caravan_of(f, cityId);
            if (c == kMacroNoSlot) continue;
            loaded = f.st().inventory[c].inv.count_of(foodIdx);
            if (loaded <= 0) continue;
            const RowSide home = side_row(f.st(), city.slot, foodIdx, food->value);
            const RowSide child = side_row(f.st(), c, foodIdx, food->value);
            // Оценки сошлись: следующую единицу ребёнок уже не ценит выше
            // дома.
            settled = gain_units(home, child, 1, /*strict=*/true, 0, 1) == 0;
            deadLawBlind =
                dead_unit(food->value, child.backG / foodG, 1)
                    <= dead_unit(food->value, cityFits, glut - 1);
        }
        CHECK(thinks > 0 && loaded > 0,
              "МЕСТО ГРУЗИТ РЕЙС И ЛЁГКИМ ЗАВАЛОМ: завал в восемь сотых спины "
              "уехал к свободной спине ребёнка");
        CHECK(deadLawBlind,
              "предусловие: по округлённым единицам выгоды нет — мёртвый закон "
              "не погрузил бы ни единицы");
        CHECK(settled && loaded < glut,
              "груз — ровно выгодные единицы: оценки сошлись, и завал ушёл не "
              "весь");
        std::printf("  [погрузка] завал %d, погружено %d за %d думок\n", glut,
                    loaded, thinks);
    }
    // 2. ВЫБОР ПО МАССЕ И ПРЕДЕЛ СПИНЫ: место завалено рудой и камнем сверх
    //    своей спины; весят они одинаково, руда дороже — спину рейса обязана
    //    занять руда, и груз обязан в эту спину лечь.
    {
        Fixture f;
        const MacroHandle city = f.rooted(m.kind, 10, 10, 64);
        const int cityId = int(f.st().spawnId[city.slot].index);
        Inventory& cityInv = f.st().inventory[city.slot].inv;
        CHECK(cityInv.add("iron", kFlood) && cityInv.add("stone", kFlood),
              "фикстура: завал руды и завал камня");
        CHECK(item_mass_g(*iron) == item_mass_g(*stone)
                  && iron->value > stone->value
                  && worth_leaving(side_row(f.st(), city.slot, ironIdx, iron->value), 1) == 1
                  && worth_leaving(side_row(f.st(), city.slot, stoneIdx, stone->value), 1) == 1,
              "предусловие: руда и камень весят одинаково, руда дороже, и оба "
              "завала место ценит по полу — выгода на грамм у руды выше");
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 51u);
        int ore = 0, rock = 0;
        std::int64_t heldG = 0, backG = 0;
        for (int i = 0; i < kThinksPerDay * 4 && ore + rock == 0; ++i) {
            f.think_on(rt, 1);
            const std::uint16_t c = caravan_of(f, cityId);
            if (c == kMacroNoSlot) continue;
            const Inventory& hold = f.st().inventory[c].inv;
            ore = hold.count_of(ironIdx);
            rock = hold.count_of(stoneIdx);
            heldG = inventory_mass_g(hold);
            backG = carry_capacity_g(f.st().runtime[c], hold);
        }
        CHECK(ore > 0 && ore > rock,
              "ВЫГОДНЕЙШЕЕ НА ГРАММ — ПЕРВЫМ: спину рейса заняла руда, камню "
              "достался остаток");
        // ПРЕДЕЛ СПИНЫ — НЕ ТАВТОЛОГИЯ: завала КАЖДОЙ строки одной хватило
        // бы забить спину ребёнка, и ребёнок ценит обе выше пола, то есть без
        // предела руда и камень легли бы каждая почти на всю спину.
        CHECK(backG > 0 && std::int64_t(kFlood) * item_mass_g(*iron) > backG,
              "предусловие: завала каждой строки хватило бы на всю спину "
              "ребёнка — предел ниже судит то, что его держит");
        CHECK(heldG > 0 && heldG <= backG,
              "ГРУЗ ЛЁГ В СПИНУ РЕБЁНКА: масса трюма после погрузки не "
              "больше его обоза");
        std::printf("  [погрузка] руды %d, камня %d; трюм %lld г из %lld г "
                    "спины\n", ore, rock, (long long)heldG, (long long)backG);
    }
    // 3. ГРУЗЯТ ТОЛЬКО ПУСТОГО: рынка нет, корован с грузом ждёт дома
    //    свободным; новый завал руды у места ему не идёт — груз ему уже дан.
    {
        Fixture f;
        const MacroHandle city = f.rooted(m.kind, 10, 10, 64);
        const int cityId = int(f.st().spawnId[city.slot].index);
        const std::uint32_t homeCell = f.st().cell[city.slot].idx;
        Inventory& cityInv = f.st().inventory[city.slot].inv;
        const std::int64_t cityFits =
            carry_capacity_g(f.st().runtime[city.slot], cityInv) / foodG;
        const int glut = int(cityFits * 8 / 100);
        CHECK(glut > 0 && cityInv.add("food", glut),
              "фикстура: лёгкий завал еды места");
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 53u);
        std::uint16_t c = kMacroNoSlot;
        int food0 = 0;
        for (int i = 0; i < kThinksPerDay * 4 && food0 == 0; ++i) {
            f.think_on(rt, 1);
            c = caravan_of(f, cityId);
            if (c != kMacroNoSlot)
                food0 = f.st().inventory[c].inv.count_of(foodIdx);
        }
        CHECK(c != kMacroNoSlot && food0 > 0,
              "предусловие: место подняло корован и дало ему груз");
        CHECK(cityInv.add("iron", kFlood), "фикстура: завал руды — ПОСЛЕ погрузки");
        // ПРЕДУСЛОВИЕ НЕГАТИВНОГО ПЛЕЧА: пустому ребёнку руду дали бы — он
        // ценит её выше места, и спина для неё есть. Без этого «руды нет»
        // ниже было бы свойством фикстуры, а не закона.
        const Inventory& hold = f.st().inventory[c].inv;
        CHECK(gain_units(side_row(f.st(), city.slot, ironIdx, iron->value),
                         side_row(f.st(), c, ironIdx, iron->value), 1,
                         /*strict=*/true, 0, 1) == 1
                  && carry_capacity_g(f.st().runtime[c], hold)
                         - inventory_mass_g(hold) >= item_mass_g(*iron),
              "предусловие: ребёнок ценит руду выше места, и свободной спины "
              "на неё хватает");
        int samples = 0, idleAtHome = 0;
        for (int i = 0; i < kThinksPerDay * 2; ++i) {
            f.think_on(rt, 1);
            ++samples;
            if (f.st().cell[c].idx == homeCell
                && f.st().runtime[c].state == std::uint8_t(NPCState::Idle))
                ++idleAtHome;
        }
        CHECK(samples > 0 && idleAtHome == samples,
              "прибор помЕрил: двое суток (два круга решений места) корован "
              "стоял дома свободным — у порога, где его грузят");
        CHECK(f.st().inventory[c].inv.count_of(ironIdx) == 0
                  && f.st().inventory[c].inv.count_of(foodIdx) == food0,
              "НЕПУСТОЙ НЕ ДОГРУЖАЕТСЯ: руды корован не получил, его груз тот "
              "же — место грузит только пустой трюм");
    }
}

void test_the_city_and_the_capital_load_their_caravans() {
    for (const TownModule& m : kTownModules) test_the_town_loads_its_caravan(m);
}

// ── ДВА СВИДЕТЕЛЯ ПУТИ РЕЙСА: ХОЛОДНЫЙ СТАРТ И ОКНО В ДВА ПЛЕЧА ──────────
// Наблюдения владельца 2026-11-12 (дословно — `trip_goes_on@src/macro/economy.h`).
// Сам закон стоит там под компилятором; здесь утверждается, что РЕЙС ЕМУ
// СЛЕДУЕТ, — по стоянкам. Стоянка — клетка, равная клетке цели рейса; была
// ли на ней сделка, говорит счёт сделок в памяти рейса (он обнуляется на
// выезде). Рынков три и дом, квадратом: ни одна дорога между двумя из них не
// идёт сквозь третий.
//
// КАЖДЫЙ МИР — ОДИН ПЕРВЫЙ РЕЙС, И МИРОВ МНОГО. Мир фикстуры неподвижен (ни
// выпуска, ни окна счёта), а единая ценомодель отдаёт всю выгоду сразу:
// первый рейс закрывает нужду живых рынков и увозит монету домой, и с этой
// монетой город грузит следующий рейс — она плотнейшая строка склада — до
// полной спины. Такой рейс ни продать (медь все ценят в единицу), ни купить
// (свободной спины ноль) не может, и все его стоянки пусты. Это замер, а не
// догадка: на прежней форме прибора (один мир, 48 дней) после первого рейса
// не было ни одной сделки, и предусловия окна не встречались. Поэтому рейсы
// с грузом берутся из разных миров — по одному на сид.
struct TripTrace {
    int worlds = 0;             // миров прогнано
    int tripsDone = 0;          // …и в скольких первый рейс вернулся домой
    int stops = 0;              // стоянок на рынках всего
    int emptyFirst = 0;         // рейсов, чей ПЕРВЫЙ рынок оказался пустым
    int emptyFirstThenOn = 0;   // …и с него рейс поехал на другой рынок
    int emptyAfterDeal = 0;     // пустых стоянок сразу после стоянки со сделкой
    int emptyAfterDealThenOn = 0;   // …и с неё рейс поехал на другой рынок
    int twoEmptyAfterDeal = 0;  // две пустые подряд после сделки
    int twoEmptyThenOn = 0;     // …и рейс всё равно поехал на третий рынок
    int twoEmptyAtStart = 0;    // рейсов, чьи ДВА первых рынка оказались пусты
    int twoEmptyStartThenOn = 0;    // …и рейс всё равно поехал на третий
};

// `coinless` — битовая маска рынков без монеты (бит m — рынок m): такому
// рынку платить нечем, и визит к нему пуст. Рейс ведётся до возвращения
// домой, но не дольше `days` суток.
void trace_first_trip(std::uint32_t seed, int days, unsigned coinless,
                      TripTrace& t) {
    Fixture f;
    ++t.worlds;
    const MacroHandle city = f.rooted(SquadType::City, 10, 10, 64);
    const int cityId = int(f.st().spawnId[city.slot].index);
    const int foodCol = commodity_index("food");
    CHECK(f.st().inventory[city.slot].inv.add("food", 6000),
          "фикстура: излишек еды города");
    // Три голодных рынка; рынку без монеты платить нечем — визит к нему
    // пуст, пока его собственная жизнь не даст ему товара.
    const int at[3][2] = {{14, 10}, {14, 14}, {10, 14}};
    for (int m = 0; m < 3; ++m) {
        const MacroHandle h = f.rooted(SquadType::Village, at[m][0], at[m][1], 8);
        Inventory& inv = f.st().inventory[h.slot].inv;
        if (((coinless >> m) & 1u) == 0)
            CHECK(inv.add("coin_timaert_copper", 60000),
                  "фикстура: рынок платёжеспособен");
        f.st().upkeep[h.slot].needDebt[foodCol] = upkeep_bill(inv).board;
    }
    const std::uint32_t homeCell = f.st().cell[city.slot].idx;
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, seed);
    // СТОЯНКА СУДИТСЯ, КОГДА РЕЙС С НЕЁ УШЁЛ, а не когда встал: сделка
    // случается думкой ПОЗЖЕ прибытия, и первая редакция прибора, судившая
    // в миг прибытия, записывала прибыльную стоянку пустой — мутация «поиск
    // выключен» прошла под ней зелёной. Пока рейс стоит, прибор только
    // помнит наибольший счёт сделок, виденный на этой стоянке.
    std::uint32_t cur = homeCell;       // клетка, на которой рейс стоит
    bool standing = true;               // …или он в пути
    std::uint32_t curDeals = 0;         // счёт сделок, виденный на стоянке
    std::uint32_t dealsLeft = 0;        // счёт сделок на уходе с прошлой
    int stopsInTrip = 0;
    bool prevHadDeal = false;
    enum Tail { None, EmptyFirst, EmptyAfterDeal, TwoEmptyAfterDeal,
                TwoEmptyAtStart };
    Tail tail = None;                   // хвост последней ОСУЖДЁННОЙ стоянки
    const auto judge_left_stop = [&]() {
        if (cur == homeCell) return;    // дом: рейс ещё не начался
        ++t.stops;
        ++stopsInTrip;
        const bool dealHere = curDeals > dealsLeft;
        const Tail was = tail;
        tail = None;
        if (!dealHere) {
            if (stopsInTrip == 1) { ++t.emptyFirst; tail = EmptyFirst; }
            else if (was == EmptyFirst) {
                ++t.twoEmptyAtStart;
                tail = TwoEmptyAtStart;
            }
            else if (prevHadDeal) { ++t.emptyAfterDeal; tail = EmptyAfterDeal; }
            else if (was == EmptyAfterDeal) {
                ++t.twoEmptyAfterDeal;
                tail = TwoEmptyAfterDeal;
            }
        }
        prevHadDeal = dealHere;
        dealsLeft = curDeals;
    };
    for (int i = 0; i < kThinksPerDay * days; ++i) {
        f.think_on(rt, 1);
        const std::uint16_t c = caravan_of(f, cityId);
        if (c == kMacroNoSlot) continue;
        const std::uint32_t cell = f.st().cell[c].idx;
        const bool stands = stands_at(f, c, cell);
        if (standing && (!stands || cell != cur)) {   // ушёл со стоянки
            // Сделка и уход случаются ОДНОЙ думкой (рейс торгует и той же
            // думкой называет следующую цель), поэтому счёт сделок стоянки
            // читается и в миг ухода: в пути он вырасти не может.
            if (cur != homeCell)
                curDeals = std::max(curDeals,
                                    f.st().runtime[c].mem[kCaravanMemDeals]);
            judge_left_stop();
            standing = false;
        }
        if (stands && !standing) {                    // встал на новую
            // Встал на РЫНОК после пустой стоянки — значит поехал дальше, а
            // не домой: хвост прошлой стоянки разрешился.
            if (cell != homeCell) {
                if (tail == EmptyFirst) ++t.emptyFirstThenOn;
                if (tail == EmptyAfterDeal) ++t.emptyAfterDealThenOn;
                if (tail == TwoEmptyAfterDeal) ++t.twoEmptyThenOn;
                if (tail == TwoEmptyAtStart) ++t.twoEmptyStartThenOn;
            } else if (stopsInTrip > 0) {
                ++t.tripsDone;                        // первый рейс окончен
                return;
            }
            cur = cell;
            standing = true;
            curDeals = 0;
        }
        if (standing && cur != homeCell)
            curDeals = std::max(curDeals,
                                f.st().runtime[c].mem[kCaravanMemDeals]);
    }
}

void test_the_trip_follows_the_return_law_at_its_edges() {
    // Два рода миров: в первом без монеты один рынок из трёх, во втором —
    // два; в каждом роде — по миру на сид, от каждого мира его первый рейс.
    // Каждое утверждение стоит на своём ПРЕДУСЛОВИИ, и предусловие
    // утверждается первым: закон, которому не на чем проявиться, свидетель
    // не подтверждает, а молчит — и это обязано быть видно.
    constexpr int kWorlds = 24;
    constexpr int kTripDays = 6;
    TripTrace t, u;
    for (int w = 0; w < kWorlds; ++w) {
        trace_first_trip(53u + std::uint32_t(w), kTripDays, 0b010u, t);
        trace_first_trip(553u + std::uint32_t(w), kTripDays, 0b110u, u);
    }
    for (const TripTrace* x : {&t, &u})
        std::printf("  [путь рейса] миров %d, стоянок %d: пустой первый %d "
                    "(дальше %d), пустая после сделки %d (дальше %d), две "
                    "пустые после сделки %d (дальше %d), две пустые с "
                    "начала %d (дальше %d)\n",
                    x->worlds, x->stops, x->emptyFirst, x->emptyFirstThenOn,
                    x->emptyAfterDeal, x->emptyAfterDealThenOn,
                    x->twoEmptyAfterDeal, x->twoEmptyThenOn,
                    x->twoEmptyAtStart, x->twoEmptyStartThenOn);
    CHECK(t.worlds == kWorlds && t.tripsDone == kWorlds
              && u.worlds == kWorlds && u.tripsDone == kWorlds
              && t.stops > 0 && u.stops > 0,
          "прибор померил все миры: в каждом первый рейс стоял на рынках и "
          "вернулся домой");
    // ХОЛОДНЫЙ СТАРТ: пустой первый рынок рейс домой не разворачивает…
    CHECK(t.emptyFirst > 0,
          "предусловие холодного старта встретилось: первый рынок рейса "
          "оказывался пустым");
    CHECK(t.emptyFirstThenOn > 0,
          "ХОЛОДНЫЙ СТАРТ: с пустого первого рынка рейс поехал на другой "
          "рынок, а не домой");
    // …а второй пустой подряд разворачивает: вечного поиска нет.
    CHECK(u.twoEmptyAtStart > 0,
          "предусловие встретилось: два первых рынка рейса оказывались пусты");
    CHECK(u.twoEmptyStartThenOn == 0,
          "ПОИСК КОНЕЧЕН: после двух пустых рынков подряд рейс едет домой, а "
          "не на третий");
    // ОКНО В ДВА ПЛЕЧА: одна пустая стоянка после прибыльной — насквозь…
    CHECK(t.emptyAfterDeal > 0,
          "предусловие окна встретилось: после стоянки со сделкой шла пустая");
    CHECK(t.emptyAfterDealThenOn > 0,
          "ОКНО В ДВА ПЛЕЧА: после прибыльного рынка один пустой рейс прошёл "
          "насквозь и поехал на следующий");
    // …а две пустые подряд разворачивают домой.
    CHECK(u.twoEmptyAfterDeal > 0,
          "предусловие встретилось: после сделки шли две пустые стоянки подряд");
    CHECK(u.twoEmptyThenOn == 0,
          "ДВЕ ПУСТЫЕ ПОДРЯД после прибыли разворачивают рейс домой");
}

// ВТОРАЯ ПОЛЯРНОСТЬ: соседа нет — рейс стоит дома. Город груз ему дал (он не
// знает и не должен знать, есть ли рынки: это решение рейса), а рейс никуда
// не поехал и ждёт.
//
// ЖДЁТ СВОЕЙ ПАМЯТЬЮ, А НЕ ГЕЙТОМ СПОСОБНОСТИ (ряд M-494, пункт (1); вердикт
// владельца 2026-11-13, CANON S25 «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ»: «ждать
// модуль вправе своей логикой и своей памятью, не чужим гейтом»). Утверждается
// форма ожидания, а не его длина: слово `kCaravanMemWait` за думку тает ровно
// на одну; пока оно не дотаяло, рейс заново не решает; дотаяло — решает и,
// раз ехать по-прежнему некуда, снова ждёт. Гейт восстановления за весь
// прогон не заряжен ни разу — акта у рейса нет, а боя фикстура не ставит.
// (Здесь стояло «ожидание есть долг восстановления своей строки» — тот самый
// второй писатель гейта, снятый рядом.)
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» РЯДА M-494, ПУНКТ (1) (прогнано 2026-11-14;
// контроль — OK 891 здесь и OK 70 у `collector_tithe_test`; собирались только
// эти два таргета). Свидетели — этот и плечи «сделка/вылив» рейса выше.
//   1) пауза гейтом после сделки на рынке вернулась (`(4 + rand(4))` думок)
//                                  →  КРАСНО, 1 из 891: гейт в думку сделки
//                                     не пуст
//   2) пауза гейтом после вылива дома вернулась (`(10 + rand(15))` думок)
//                                  →  КРАСНО, 1 из 891: в думку вылива гейт
//                                     не пуст и решения дальше нет
//   3) слово ожидания не тает      →  КРАСНО, 3 из 891: ожидание держит слово;
//                                     предусловие «дотаивало»; выезда после
//                                     возврата нет
//   4) ожидание не выдерживается (слово тает, но решение каждую думку)
//                                  →  КРАСНО, 2 из 891
//   5) ожидание снова гейтом восстановления (прежняя форма)
//                                  →  КРАСНО, 3 из 891: гейт не пуст весь
//                                     прогон; вылив не решён той же думкой
//   6) переименование локальной `loaded` в модуле корована
//                                  →  МОЛЧИТ, OK 891 и OK 70
// `collector_tithe_test` на 1–5 молчит (OK 70): корован — не его предмет.
void test_the_caravan_with_no_market_waits_at_home() {
    Fixture f;
    const MacroHandle city = f.rooted(SquadType::City, 10, 10, 64);
    const int cityId = int(f.st().spawnId[city.slot].index);
    Inventory& cityInv = f.st().inventory[city.slot].inv;
    constexpr int kCityFood = 6000;
    CHECK(cityInv.add("food", kCityFood), "фикстура: излишек еды города");
    const int foodIdx = item_index("food");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 52u);
    const std::uint32_t homeCell = f.st().cell[city.slot].idx;
    int samples = 0, away = 0, gated = 0;
    int held = 0, broken = 0, rewaits = 0;
    bool loaded = false;
    const int kThinks = kThinksPerDay * 3;
    for (int i = 0; i < kThinks; ++i) {
        const std::uint16_t c0 = caravan_of(f, cityId);
        const std::uint32_t before =
            c0 != kMacroNoSlot ? f.st().runtime[c0].mem[kCaravanMemWait] : 0u;
        f.think_on(rt, 1);
        ++samples;
        const std::uint16_t c = caravan_of(f, cityId);
        if (c == kMacroNoSlot) continue;
        if (f.st().cell[c].idx != homeCell) ++away;
        if (ecs::squad_recovering(f.st().runtime[c])) ++gated;
        if (inventory_value(f.st().inventory[c].inv) > 0) loaded = true;
        if (c != c0) continue;
        const std::uint32_t after = f.st().runtime[c].mem[kCaravanMemWait];
        if (before >= 2u) {
            // Ещё ждал: слово обязано стаять ровно на одну думку — ни
            // застыть, ни смениться новым решением.
            if (after + 1u == before) ++held;
            else ++broken;
        } else if (after >= 2u) {
            ++rewaits;   // дотаяло — решил заново и снова ждёт
        }
    }
    const std::uint16_t c = caravan_of(f, cityId);
    CHECK(samples == kThinks && c != kMacroNoSlot,
          "прибор отработал: корован рождён городом");
    CHECK(loaded, "город груз ДАЛ — о рынках он не спрашивает");
    CHECK(away == 0,
          "СОСЕДА НЕТ — РЕЙС НЕ ПОКИДАЕТ КЛЕТКУ ДОМА (прежде уходил бродить "
          "чужим поведением)");
    CHECK(held > 0 && rewaits > 1,
          "предусловие: рейс ждал, и ожидание дотаивало не один раз");
    CHECK(broken == 0,
          "ОЖИДАНИЕ ДЕРЖИТ СВОЁ СЛОВО ПАМЯТИ: пока оно не дотаяло, оно тает "
          "ровно на думку и нового решения нет");
    CHECK(gated == 0,
          "ГЕЙТ ВОССТАНОВЛЕНИЯ ПУСТ ВЕСЬ ПРОГОН: ожидание — решение модуля, "
          "а не долг способности");
    std::printf("  [рейс без рынка] думок ожидания %d, новых решений ждать %d\n",
                held, rewaits);
    CHECK(traded_facts_at(f.gs, 10, 10) == 0,
          "сделки не было — не с кем");
    // Груз не пропал: что ушло с полки, лежит в трюме.
    CHECK(cityInv.count_of(foodIdx)
              + f.st().inventory[c].inv.count_of(foodIdx) == kCityFood,
          "СОХРАНЕНИЕ: еда города либо на полке, либо в трюме его рейса");
}

// ═════ ЛЕСТНИЦА ДАНИ ИДЁТ ВНИЗ ПО СТУПЕНЯМ (ряд M-522) ═════════════════════
// Владелец, дословно (CANON S25 «СТОЛИЦА РОЖДАЕТСЯ СТОЛИЦЕЙ»): «и феодально
// тоже столица выслыает сборщика в гоород а город в деревню». Фикстура сама
// ставит три ступени и два ребра феода дверью мира (§8 п.11): столица —
// сюзерен города, город — сюзерен деревни. Утверждается, КУДА идут сборщики:
// сборщик столицы назвал целью город, сборщик города — деревню, и ни один не
// перешагнул ступень (столица деревню своего вассала не обходит, город к
// столице не ходит).
void test_the_tithe_ladder_runs_down() {
    Fixture f;
    f.gs.worldTime = world_time_at(1, 8, 0);
    const MacroHandle cap = f.rooted(SquadType::Capital, 4, 10, 64);
    const MacroHandle city = f.rooted(SquadType::City, 14, 10, 64);
    const MacroHandle vil = f.rooted(SquadType::Village, 24, 10, 64);
    const int capId = int(f.st().spawnId[cap.slot].index);
    const int cityId = int(f.st().spawnId[city.slot].index);
    const int vilId = int(f.st().spawnId[vil.slot].index);
    set_relation(f.st(), cityId, capId, Stance::Suzerain);
    set_relation(f.st(), vilId, cityId, Stance::Suzerain);
    CHECK(suzerain_of(f.st(), city.slot) == capId
              && suzerain_of(f.st(), vil.slot) == cityId,
          "фикстура: две ступени феода встали дверью мира");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 54u);
    bool capToCity = false, cityToVil = false;
    bool capToVil = false, cityToCap = false;
    int samples = 0;
    for (int i = 0; i < kThinksPerDay * 2; ++i) {
        f.think_on(rt, 1);
        for (std::size_t s = 0; s < kUnifiedCap; ++s) {
            if (f.st().alive[s] == 0 || f.st().dead[s] != 0) continue;
            if (f.st().runtime[s].squadType
                != std::uint8_t(SquadType::Collector))
                continue;
            ++samples;
            const std::uint32_t what = f.st().goal[s].what;
            if (ecs::goal_kind(what) != ecs::GoalKind::Squad) continue;
            const int target = int(ecs::goal_ordinal(what));
            const int home = parent_of(f.st(), std::uint16_t(s));
            capToCity = capToCity || (home == capId && target == cityId);
            capToVil = capToVil || (home == capId && target == vilId);
            cityToVil = cityToVil || (home == cityId && target == vilId);
            cityToCap = cityToCap || (home == cityId && target == capId);
        }
    }
    CHECK(samples > 0, "прибор померил: сборщики в мире были");
    CHECK(f.children_of(capId, SquadType::Collector) == 1
              && f.children_of(cityId, SquadType::Collector) == 1,
          "каждая ступень с вассалом подняла своего сборщика");
    CHECK(capToCity, "СТОЛИЦА ВЫСЫЛАЕТ СБОРЩИКА В ГОРОД");
    CHECK(cityToVil, "ГОРОД ВЫСЫЛАЕТ СБОРЩИКА В ДЕРЕВНЮ");
    CHECK(!capToVil && !cityToCap,
          "лестница не перешагивает ступень: столица у деревни вассала не "
          "берёт, город к своей столице сборщика не шлёт");
}

// ═════ СТОЛИЦА — СТАНЦИЯ КОРОВАНА, КАК ГОРОД (ряд M-522) ═══════════════════
// Корона рождается своим типом, и рынок мира она терять не вправе: станцию
// корована выбирает дверь `next_station` по строке оси («поселение ли») и
// числу поля под клеткой, а не по имени типа. Фикстура — город с излишком и
// столица рядом с монетой и нуждой, ЕДИНСТВЕННЫЙ сосед: рейс города обязан
// назвать её целью и встать на её клетке.
void test_the_capital_is_a_caravan_station() {
    Fixture f;
    const MacroHandle city = f.rooted(SquadType::City, 10, 10, 64);
    const MacroHandle cap = f.rooted(SquadType::Capital, 14, 10, 64);
    const int cityId = int(f.st().spawnId[city.slot].index);
    const int capId = int(f.st().spawnId[cap.slot].index);
    Inventory& cityInv = f.st().inventory[city.slot].inv;
    Inventory& capInv = f.st().inventory[cap.slot].inv;
    const int foodCol = commodity_index("food");
    CHECK(cityInv.add("food", 6000), "фикстура: излишек еды города");
    CHECK(capInv.add("coin_timaert_copper", 60000),
          "фикстура: столица платёжеспособна");
    f.st().upkeep[cap.slot].needDebt[foodCol] = upkeep_bill(capInv).board;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 55u);
    const std::uint32_t capCell = f.st().cell[cap.slot].idx;
    int samples = 0;
    bool aimed = false, stood = false;
    for (int i = 0; i < kThinksPerDay * 4; ++i) {
        f.think_on(rt, 1);
        const std::uint16_t c = caravan_of(f, cityId);
        if (c == kMacroNoSlot) continue;
        ++samples;
        const std::uint32_t what = f.st().goal[c].what;
        aimed = aimed || (ecs::goal_kind(what) == ecs::GoalKind::Squad
                          && int(ecs::goal_ordinal(what)) == capId);
        stood = stood || stands_at(f, c, capCell);
    }
    CHECK(samples > 0, "прибор померил: корован города рождён");
    CHECK(aimed, "СТОЛИЦА — СТАНЦИЯ КОРОВАНА: рейс города назвал её рынком");
    CHECK(stood, "и встал на её клетке, а не проехал мимо");
}

// ═════ БЕЗ ДОМА — БРОДЯГА: СВОЙ ТИП, СВОЙ МОДУЛЬ (ряд M-499, раскладка (з)) ═
// Владелец, дословно (CANON «РАЗНООБРАЗИЕ ИГРЫ ЕСТЬ НОВЫЕ ТИПЫ СО СВОИМИ
// МОДУЛЯМИ»): «под каждую профессию смвои агностичные дод инксаплурованный
// тип с моуделм»; раскладка (з): «бродяга без дома … становятся такими
// типами». Значит общей думки бродяги под модулями корована и сборщика нет:
// оставшись без дома, каждый САМ меняет свой тип на `Nomad` своим кодом —
// память модуля зануляет, цель бросает, встаёт без дела, — а дальше его ведёт
// модуль бродяги. Назад он не превращается: дома нет.
//
// ПРЕДУСЛОВИЯ ФИКСТУРА РОЖАЕТ САМА (§8 п.11): дом — город, ребёнок родится
// ТЕМ ЖЕ глаголом рождения, каким его рожает модуль города (закон 2б (в)),
// смерть дома — той же дверью, какой кончает автобой.
//
// ТАБЛИЦА МУТАЦИЙ ОБЕИХ ПОЛЯРНОСТЕЙ — в теле коммита (§8 п.6).
MacroHandle orphan_of(Fixture& f, SquadType type, int x, int y) {
    MacroStore& st = f.st();
    const MacroHandle home = f.rooted(SquadType::City, x, y, 64);
    TickContext ctx{};
    ctx.mw = f.world();
    ctx.mapW = kMap;
    ctx.mapH = kMap;
    AiPult pult(ctx);
    AiSelf self{home, st.cell[home.slot], st.runtime[home.slot],
                st.pools[home.slot], st.goal[home.slot],
                st.inventory[home.slot].inv, st.sheet[home.slot]};
    const MacroHandle child = pult.spawn(self, type);
    CHECK(st.valid(child)
              && parent_of(st, child.slot)
                     == int(st.spawnId[home.slot].index)
              && st.runtime[child.slot].squadType == std::uint8_t(type),
          "фикстура: город родил ребёнка своего типа дверью рождения");
    settle_avatar_fraction(st, home, 0.0f);
    CHECK(st.dead[home.slot] != 0, "фикстура: дом ребёнка умер");
    return child;
}

// Ординал живого города, на клетке которого стоит `slot`; 0 — ни на каком.
int city_under(Fixture& f, std::uint16_t slot) {
    const MacroStore& st = f.st();
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (st.alive[s] == 0 || st.dead[s] != 0) continue;
        if (st.runtime[s].squadType != std::uint8_t(SquadType::City)) continue;
        if (st.cell[s].idx == st.cell[slot].idx)
            return int(st.spawnId[s].index);
    }
    return 0;
}

void test_a_homeless_caravan_or_collector_becomes_a_nomad() {
    constexpr SquadType kChildren[] = {SquadType::Caravan,
                                       SquadType::Collector};
    for (const SquadType type : kChildren) {
        const bool caravan = type == SquadType::Caravan;
        std::printf("  [без дома: %s]\n", caravan ? "корован" : "сборщик");
        Fixture f;
        MacroStore& st = f.st();
        const MacroHandle away = f.rooted(SquadType::City, 20, 10, 64);
        const MacroHandle other = f.rooted(SquadType::City, 10, 20, 64);
        const int awayId = int(st.spawnId[away.slot].index);
        const int otherId = int(st.spawnId[other.slot].index);
        const MacroHandle child = orphan_of(f, type, 10, 10);
        // ПАМЯТЬ МОДУЛЯ НЕ ПУСТА И ЦЕЛЬ В ПУТИ — иначе «занулена» и «брошена»
        // были бы зелены сами собой (§8 п.2).
        for (std::uint32_t& w : st.runtime[child.slot].mem) w = 7u;
        ecs::goal_set(st.goal[child.slot], st.cell[away.slot].idx,
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     std::uint32_t(awayId)),
                      0, std::uint8_t(NPCState::Working));
        st.runtime[child.slot].state = std::uint8_t(NPCState::Traveling);

        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 56u);
        int samples = 0, became = -1, reverted = 0;
        bool memZero = false, dropped = false, idle = false, walks = false;
        for (int i = 0; i < kThinksPerDay; ++i) {
            const std::uint8_t before = st.runtime[child.slot].squadType;
            f.think_on(rt, 1);
            if (!st.valid(child)) break;
            ++samples;
            const ecs::MacroNpcRuntime& crt = st.runtime[child.slot];
            const ecs::SquadGoal& g = st.goal[child.slot];
            if (before == std::uint8_t(type)
                && crt.squadType == std::uint8_t(SquadType::Nomad)) {
                became = i;
                memZero = true;
                for (const std::uint32_t w : crt.mem) memZero = memZero && w == 0;
                dropped = g.underway == 0;
                idle = crt.state == std::uint8_t(NPCState::Idle);
            }
            if (became < 0) continue;
            if (crt.squadType != std::uint8_t(SquadType::Nomad)) ++reverted;
            const int target = ecs::goal_kind(g.what) == ecs::GoalKind::Squad
                ? int(ecs::goal_ordinal(g.what)) : 0;
            walks = walks
                || (crt.state == std::uint8_t(NPCState::Traveling)
                    && (target == awayId || target == otherId));
        }
        CHECK(samples == kThinksPerDay,
              "прибор отработал сутки: ребёнок жив весь прогон");
        CHECK(became >= 0,
              "БЕЗ ДОМА — БРОДЯГА: ребёнок сменил свой тип на Nomad сам");
        CHECK(memZero, "смена типа зануляет память модуля");
        CHECK(dropped && idle,
              "цель брошена и сквад встал без дела — дальше решает модуль "
              "бродяги");
        CHECK(reverted == 0, "назад бродяга не превращается: дома нет");
        CHECK(walks, "и бродяга пошёл к живому городу мира");
    }
}

// ═════ БРОДЯГА ХОДИТ ОТ ГОРОДА К ГОРОДУ И ЖДЁТ СВОЕЙ ПАМЯТЬЮ ══════════════
// Два закона модуля `nomad_think@src/macro/squad_ai/nomad.cpp`: (1) новый
// город — НЕ тот, на клетке которого бродяга стоит (откуда уходит); (2) ждать
// модуль вправе только своей памятью — слово `kNomadMemWait` тает ровно на
// думку, гейт восстановления весь прогон пуст (вердикт владельца
// 2026-11-13, CANON S25 «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ»). Два плеча: два
// города — бродяга ходит меж ними; один город — идти некуда, и бродяга стоит
// на нём, переждав своим словом не один раз.
void test_the_nomad_walks_city_to_city_and_waits_in_its_own_memory() {
    for (const int cities : {2, 1}) {
        std::printf("  [бродяга: городов %d]\n", cities);
        Fixture f;
        MacroStore& st = f.st();
        const MacroHandle a = f.rooted(SquadType::City, 20, 10, 64);
        if (cities == 2) f.rooted(SquadType::City, 20, 20, 64);
        const int aId = int(st.spawnId[a.slot].index);
        const MacroHandle n = orphan_of(f, SquadType::Caravan, 10, 10);

        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 57u);
        int samples = 0, legs = 0, repicks = 0, gated = 0;
        int held = 0, broken = 0, rewaits = 0, leftA = 0;
        bool arrivedA = false;
        for (int i = 0; i < kThinksPerDay * 3; ++i) {
            const ecs::MacroNpcRuntime pre = st.runtime[n.slot];
            const int standing = city_under(f, n.slot);
            f.think_on(rt, 1);
            if (!st.valid(n)) break;
            ++samples;
            const ecs::MacroNpcRuntime& crt = st.runtime[n.slot];
            if (pre.squadType != std::uint8_t(SquadType::Nomad)) continue;
            if (ecs::squad_recovering(crt)) ++gated;
            const std::uint32_t before = pre.mem[kNomadMemWait];
            const std::uint32_t after = crt.mem[kNomadMemWait];
            if (before >= 2u) {
                // Ещё ждал: слово тает ровно на думку, нового решения нет.
                if (after + 1u == before) ++held;
                else ++broken;
            } else if (after >= 2u) {
                ++rewaits;   // дотаяло или пришёл — решил ждать заново
            }
            if (pre.state == std::uint8_t(NPCState::Idle)
                && crt.state == std::uint8_t(NPCState::Traveling)) {
                ++legs;
                const ecs::SquadGoal& g = st.goal[n.slot];
                const int target =
                    ecs::goal_kind(g.what) == ecs::GoalKind::Squad
                        ? int(ecs::goal_ordinal(g.what)) : 0;
                if (standing != 0 && target == standing) ++repicks;
            }
            // ПЛЕЧО НУЛЕВОЙ ДЛИНЫ — ТОЖЕ ВЫБОР СТОЯЩЕГО ГОРОДА. Назвавший город
            // под собой приходит в ту же думку и снова стоит Idle, поэтому
            // строкой выше его не видно; видно прибытие, случившееся в думку,
            // начатую без дела: города фикстуры в десяти клетках друг от
            // друга, и честное плечо за одну думку не кончается.
            if (pre.state == std::uint8_t(NPCState::Idle)
                && ecs::march_arrived(crt) && standing != 0
                && city_under(f, n.slot) == standing)
                ++repicks;
            if (city_under(f, n.slot) == aId) arrivedA = true;
            else if (arrivedA) ++leftA;
        }
        CHECK(samples == kThinksPerDay * 3, "прибор отработал трое суток");
        CHECK(gated == 0,
              "ГЕЙТ ВОССТАНОВЛЕНИЯ ПУСТ ВЕСЬ ПРОГОН: ожидание бродяги — "
              "решение модуля, а не долг способности");
        CHECK(held > 0 && rewaits > 1,
              "предусловие: бродяга ждал своим словом, и ожидание "
              "назначалось не один раз");
        CHECK(broken == 0,
              "ОЖИДАНИЕ ДЕРЖИТ СВОЁ СЛОВО ПАМЯТИ: пока оно не дотаяло, оно "
              "тает ровно на думку и нового решения нет");
        CHECK(repicks == 0,
              "НЕ ТОТ ГОРОД, ГДЕ СТОИШЬ: новое плечо ни разу не назвало "
              "город, с клетки которого бродяга уходит");
        if (cities == 2) {
            CHECK(legs > 3, "предусловие: бродяга прошёл не одно плечо");
        } else {
            CHECK(arrivedA, "предусловие: бродяга дошёл до единственного "
                            "города мира");
            CHECK(leftA == 0,
                  "ИДТИ НЕКУДА — СТОИТ: с единственного города бродяга не "
                  "уходит и пережидает своим словом");
        }
        std::printf("  [бродяга: городов %d] плеч %d, думок ожидания %d, "
                    "новых ожиданий %d\n", cities, legs, held, rewaits);
    }
}

int main() {
    test_village_produces_through_its_own_think();
    test_the_town_remembers_it_has_nothing_to_craft();
    test_village_raises_an_artel_and_pays_in_souls();
    test_birth_hands_the_parents_own_heads();
    test_city_raises_a_collector_for_a_vassal_once_a_season();
    test_no_duplicate_child_however_long_the_world_runs();
    test_a_standing_rooted_pays_nothing_for_the_road();
    test_the_last_shift_of_hands_is_never_written_out();
    test_a_dying_rooted_rewrites_the_feature_under_itself();
    test_the_number_under_a_mine_is_ore_and_not_people();
    test_the_caravan_grows_its_hold_and_brings_it_home();
    test_the_city_and_the_capital_load_their_caravans();
    test_the_trip_follows_the_return_law_at_its_edges();
    test_the_caravan_with_no_market_waits_at_home();
    test_the_tithe_ladder_runs_down();
    test_the_capital_is_a_caravan_station();
    test_a_homeless_caravan_or_collector_becomes_a_nomad();
    test_the_nomad_walks_city_to_city_and_waits_in_its_own_memory();
    return sm::test::report("rooted_module_day_test");
}
