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
// ЧТО ИМЕННО УТВЕРЖДАЕТСЯ (четыре закона, по одному на тест):
//   1. деревня ПРОИЗВОДИТ ЧЕРЕЗ ДУМКУ СВОЕГО МОДУЛЯ, и ровно РАЗ В СУТКИ —
//      второго пути выпуска в мире нет (вызов из `tick_squads_day_` умер);
//   2. деревня РОЖАЕТ АРТЕЛЬ и ПЛАТИТ ЗА НЕЁ ДУШАМИ своего контейнера;
//   3. город рожает СБОРЩИКА только при вассале-должнике;
//   4. ДУБЛЬ не рождается — гейт `has_child` держит одного ребёнка на тип.
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
//   · период производства = `kTicksPerDay / kAiTicks` думок, и оба числа
//     публичны: прибор считает его САМ, не спрашивая анонимную константу
//     модуля.
//
// ТАБЛИЦА МУТАЦИЙ ОБЕИХ ПОЛЯРНОСТЕЙ — в теле коммита (§8 п.6).
//
// ПРЕДУСЛОВИЯ ПРИБОР РОЖАЕТ СЕБЕ САМ (§8 п.11): ни одной строки надежды на
// генератор. Две ловушки рецепта фикстуры названы, потому что обе молчат:
//   · ТЕРРАИН-СУША ОБЯЗАТЕЛЕН — `AiPult::spawn` отказывает без
//     `mw.terrain` невалидным хэндлом, и «артель не родилась» выглядело бы
//     дефектом мира;
//   · `birth_place` С ЯВНОЙ ФРАКЦИЕЙ — при -1 минтовый рецепт глохнет
//     fail-closed (`faction_mint_rows(-1)` = `{-1,-1,-1}`).
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/pathfinding.h"   // PathCostData — вес земли под стоящим
#include "macro/movement_cost.h"  // burn/rest per hour — якорь предусловия
#include "macro/travel.h"         // overload_burden_from_capacity
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — одна дверь прихода душ
#include "macro/store.h"         // parent_of / tithe_of / set_relation
#include "macro/world_row.h"     // count_human_souls
#include "macro/chronicle.h"
#include "macro/econ_day.h"      // kCreaturesPerCityWorker, EconFact
#include "macro/interests.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/tree_layer.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "core/time.h"           // kTicksPerDay — период думки выводится

#include <cstdint>
#include <vector>

namespace {

using namespace sm;

constexpr int kMap = 32;

// РАЗМЕР КРЮ СЧИТАЕТСЯ ПРИБОРОМ САМ, ИЗ ТОЙ ЖЕ ПУБЛИЧНОЙ СТРОКИ, ИЗ КОТОРОЙ
// его берёт модуль (`kCreaturesPerCityWorker@src/macro/econ_day.h`). Копии
// числа здесь нет нарочно: прибитый литерал ломался бы на каждой
// перекалибровке и не доказывал бы замысла (§8 п.4).
constexpr int kCrew = kCreaturesPerCityWorker;

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

// ВЕДОМОСТЬ ФАКТОВ — `switch` ПО ВСЕМ `Kind` БЕЗ `default:`: новый род
// ведомости назовёт `-Wswitch` сам, и прибор не проглотит его молча
// (ЗАКОН СЛОВАРЯ п.4). Ординал 7 в оси пуст — перечислять нечего.
struct FactTally {
    int gathered = 0, produced = 0, starved = 0, consumed = 0;
    int minted = 0, scrapped = 0;
    int soulsBorn = 0, soulsDeserted = 0, soulsKilled = 0;
    int producedUnits = 0;
    int producedCommodity = -1;
};
void tally_sink(void* user, const EconFact& f) {
    FactTally& t = *static_cast<FactTally*>(user);
    switch (f.kind) {
        case EconFact::Kind::Gathered: ++t.gathered; break;
        case EconFact::Kind::Produced:
            ++t.produced;
            t.producedUnits += f.amount;
            t.producedCommodity = f.commodity;
            break;
        case EconFact::Kind::Starved:  ++t.starved;  break;
        case EconFact::Kind::Consumed: ++t.consumed; break;
        case EconFact::Kind::Minted:   ++t.minted;   break;
        case EconFact::Kind::Scrapped: ++t.scrapped; break;
        case EconFact::Kind::SoulsBorn: ++t.soulsBorn; break;
        case EconFact::Kind::SoulsDesertedUnpaid: ++t.soulsDeserted; break;
        case EconFact::Kind::SoulsKilled: ++t.soulsKilled; break;
    }
}

// Мир фикстуры — одна структура, чтобы все четыре теста рождали СВОИ
// предусловия одной и той же дверью, а не четырьмя списаниями друг у друга.
// ВЕС ЗЕМЛИ ФИКСТУРЫ — И БЕЗ НЕГО ЗАКОН ПЛАНКИ НЕ ПРОВЕРЯЕТСЯ ВОВСЕ.
// `cell_weight@src/macro/npc_ai.cpp` читает запечённую сетку, а при её
// отсутствии честно отвечает **1.0** — «мир без рельефа». На весе 1.0 жжение
// получается меньше регена, планка не садится НИ У КОГО, и всякая проверка
// про SP становится ТАВТОЛОГИЕЙ (§8 п.2: прибор обязан утверждать, что
// помЕрил). Поймала это ровно ТАБЛИЦА МУТАЦИЙ: снос строки цели в
// `birth_place` ронял только проверки про саму цель, а проверки про планку
// оставались зелёными — то есть не проверяли ничего.
//
// ЧИСЛО ВЗЯТО ИЗ ЗАМЕРА ЖИВОГО МИРА, А НЕ С ПОТОЛКА: зонд в самой точке
// ритма дал вес клетки под местами **3.48–5.59** (`balance_run`, сид 7,
// 40 дней). 5.0 — середина этой полосы, то есть земля фикстуры не легче и не
// тяжелее той, на которой стоят настоящие деревни.
constexpr float kFixtureCellWeight = 5.0f;

struct Fixture {
    GameState gs{};
    TerrainData terrain = all_land();
    TreeLayer trees;
    PathCostData pathCost;
    std::unique_ptr<MacroStore> storeOwner = make_macro_store();
    FactTally tally{};

    Fixture() {
        gs.mapW = kMap;
        gs.mapH = kMap;
        chronicle_init(gs.chronicle, kMap, kMap);
        trees.width = kMap;
        trees.height = kMap;
        trees.data.assign(std::size_t(kMap) * kMap, 0);
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
    }
    MacroStore& st() { return *storeOwner; }

    MacroWorld world() {
        return MacroWorld{.gs = &gs, .trees = &trees, .store = storeOwner.get(),
                          .terrain = &terrain,
                          .pathCost = &pathCost,
                          .econFacts = &tally_sink,
                          .econFactsUser = &tally};
    }

    // ПЛЕМЯ НАЗЫВАЕТСЯ ЯВНО: -1 глушит минтовый рецепт fail-closed.
    MacroHandle place(SquadType kind, int x, int y, int souls) {
        const MacroHandle h = birth_place(
            gs, st(), kind, x, y,
            std::int16_t(faction_index("timaert")));
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

// ═════ 1. ДЕРЕВНЯ ПРОИЗВОДИТ ЧЕРЕЗ ДУМКУ, И РОВНО РАЗ В СУТКИ ════════════
// Второго пути выпуска в мире нет: вызов `econ_produce_day` из
// `tick_squads_day_@src/macro/world_tick.cpp` УМЕР этим же ломтём, а здесь
// дневной такт мира не крутится вовсе — крутится ТОЛЬКО думка. Значит всякий
// кирпич, найденный на полке, пришёл через модуль.
void test_village_produces_through_its_own_think() {
    Fixture f;
    const MacroHandle vil = f.place(SquadType::Village, 10, 10, 64);

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
    CHECK(f.tally.produced > 0
              && f.tally.producedCommodity == commodity_of_item(bricksIdx),
          "ведомость услышала факт Produced, и он назвал ТОВАРНУЮ строку "
          "кирпича");
    CHECK(f.tally.producedUnits == bricksAfterOne,
          "ЯКОРЬ ВЕДОМОСТИ: доложенные единицы равны тому, что РЕАЛЬНО легло "
          "на полку — факт не врёт о вещах, которых нет");

    // ВОССТАНОВЛЕНИЕ ЕСТЬ ЗАКОН, А НЕ ПОЖЕЛАНИЕ: до конца суток второго акта
    // не бывает. Это и ловит «произвёл дважды за день» — то, чем кончались
    // бы ДВА живых пути выпуска.
    f.think_on(rt, kThinksPerDay - 1);
    CHECK(shelf.count_of(bricksIdx) == bricksAfterOne,
          "ЗА ОСТАТОК СУТОК НИ ОДНОЙ ЛИШНЕЙ ПАРТИИ: период акта заряжает сам "
          "глагол, поэтому дважды за день выпуск невыразим");

    // А на следующей думке сутки истекли — и акт повторяется.
    f.think_on(rt, 1);
    CHECK(shelf.count_of(bricksIdx) > bricksAfterOne,
          "НОВЫЕ СУТКИ — НОВЫЙ АКТ: восстановление истекло, выпуск пошёл "
          "снова (иначе прибор охранял бы ОДИН выпуск за всю жизнь мира)");
}

// ═════ 2. ДЕРЕВНЯ РОЖАЕТ АРТЕЛЬ И ПЛАТИТ ДУШАМИ ═════════════════════════
void test_village_raises_an_artel_and_pays_in_souls() {
    Fixture f;
    const MacroHandle vil = f.place(SquadType::Village, 10, 10, 64);
    const int vilId = int(f.st().spawnId[vil.slot].index);

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
    CHECK(f.children_of(vilId, SquadType::Artel) == 0,
          "БЕЗ ЖИВОЙ НУЖДЫ АРТЕЛИ НЕТ: деревня, которой нечего просить, душ "
          "в поле не выписывает");
    CHECK(f.heads(vil.slot) == headsBefore,
          "и душ она при этом не потеряла ни одной");

    // ── ПОЛОЖИТЕЛЬНОЕ ПЛЕЧО ────────────────────────────────────────────
    f.st().upkeep[vil.slot].needDebt[foodCom] = 4096;
    f.think(kThinksPerDay + 2);

    CHECK(f.children_of(vilId, SquadType::Artel) == 1,
          "ДЕРЕВНЯ РОДИЛА АРТЕЛЬ — ровно одну, и родителем названа ОНА");
    const int headsAfter = f.heads(vil.slot);
    // ЯКОРЬ С ДВУХ КОНЦОВ, И РАЗНИЦА В ЕДИНИЦУ ЕСТЬ ЛИДЕРСКОЕ ТЕЛО.
    CHECK(headsBefore - headsAfter == kCrew,
          "РОЖДЕНИЕ ЗАПЛАТИЛО ДУШАМИ РОДИТЕЛЯ: дома стало РОВНО на крю "
          "меньше (предел по построению — потолка детей колонкой нет)");

    // Второй конец того же якоря — контейнер рождённого.
    int bornSlot = -1;
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (f.st().alive[s] == 0 || f.st().dead[s] != 0) continue;
        if (f.st().runtime[s].squadType != std::uint8_t(SquadType::Artel))
            continue;
        if (parent_of(f.st(), std::uint16_t(s)) == vilId)
            bornSlot = int(s);
    }
    CHECK(bornSlot >= 0, "рождённая артель нашлась слотом");
    CHECK(f.heads(std::uint16_t(bornSlot)) == kCrew - 1,
          "В КОНТЕЙНЕРЕ РОЖДЁННОГО СТОИТ КРЮ МИНУС ОДИН: недостающая душа "
          "и есть ЛИДЕРСКОЕ ТЕЛО, которым сквад воплощается, — закон "
          "утверждён с ОБОИХ концов пары, а не пересчётом одной формулы");
}

// ═════ 3. СБОРЩИК — ТОЛЬКО ПРИ ВАССАЛЕ-ДОЛЖНИКЕ ═════════════════════════
void test_city_raises_a_collector_only_for_a_debtor() {
    Fixture f;
    const MacroHandle city = f.place(SquadType::City, 10, 10, 64);
    const MacroHandle vassal = f.place(SquadType::Village, 14, 10, 64);
    const int cityId = int(f.st().spawnId[city.slot].index);
    const int vassalId = int(f.st().spawnId[vassal.slot].index);

    // ФЕОД СТАВИТСЯ ДВЕРЬЮ МИРА: роль зовётся с НИЖНЕГО конца — вассал
    // получает `Suzerain`, сюзерен зеркало `Vassal` (store.h set_relation).
    set_relation(f.st(), vassalId, cityId, Stance::Suzerain);
    const Interest* edge = tithe_of(f.st(), vassal.slot);
    CHECK(edge != nullptr, "ребро дани легло в реестр вассала");
    CHECK(!owes_tithe(f.st(), vassal.slot),
          "и долга на нём пока НЕТ — значит плечо ниже есть настоящее "
          "условие, а не пустое");

    // ── НЕГАТИВНОЕ ПЛЕЧО: ФЕОД ЕСТЬ, ДОЛГА НЕТ — СБОРЩИКА НЕТ ───────────
    f.think(kThinksPerDay + 2);
    CHECK(f.children_of(cityId, SquadType::Collector) == 0,
          "БЕЗ ДОЛГА СБОРЩИКА НЕТ: рождать его значило бы послать сквад "
          "бродить вокруг дома навсегда");
    // А КОРОВАН ПРИ ЭТОМ ЕСТЬ — ЭТО РАЗДЕЛЯЕТ ДВА РЕШЕНИЯ ГОРОДА. Без этой
    // проверки «сборщика нет» могло бы значить «город вообще не думает», и
    // негативное плечо выше оказалось бы вакуумным (§8 п.2).
    CHECK(f.children_of(cityId, SquadType::Caravan) == 1,
          "город ПРИ ТОМ ЖЕ ПРОГОНЕ поднял корован — значит думка шла, и "
          "ноль сборщиков есть РЕШЕНИЕ, а не молчание модуля");

    // ── ПОЛОЖИТЕЛЬНОЕ ПЛЕЧО: ДОЛГ ПОЯВИЛСЯ ─────────────────────────────
    // Долг живёт СТОИМОСТЬЮ на ребре `Suzerain` самого вассала (M-245).
    tithe_of(f.st(), vassal.slot)->owedValue = 10000;
    CHECK(owes_tithe(f.st(), vassal.slot), "долг на ребре стал живым");
    f.think(kThinksPerDay + 2);
    CHECK(f.children_of(cityId, SquadType::Collector) == 1,
          "ДОЛЖНИК ПОЯВИЛСЯ — ГОРОД ПОДНЯЛ СБОРЩИКА, и вопрос этот задан "
          "СВОИМИ рёбрами, а не поиском по миру");
}

// ═════ 4. ДУБЛЬ НЕ РОЖДАЕТСЯ ════════════════════════════════════════════
// Гейт `has_child` и есть то, что делает колонку «число рождённых»
// ненужной: выводимое не хранится (ЗАКОН АНКЕТЫ п.2, шрам трёх носителей).
void test_no_duplicate_child_however_long_the_world_runs() {
    Fixture f;
    const MacroHandle vil = f.place(SquadType::Village, 10, 10, 200);
    const int vilId = int(f.st().spawnId[vil.slot].index);
    f.st().upkeep[vil.slot].needDebt[hunger_commodity_ordinal()] = 1 << 20;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    f.think_on(rt, kThinksPerDay + 2);
    CHECK(f.children_of(vilId, SquadType::Artel) == 1,
          "первая артель поднялась");
    const int headsAfterFirst = f.heads(vil.slot);

    // ЧЕТВЕРО СУТОК СВЕРХУ, И ДУШ В ДОМЕ С ИЗБЫТКОМ (200 против крю): если
    // бы гейт дубля не держал, их хватило бы ещё на двадцать артелей.
    f.think_on(rt, 4 * kThinksPerDay);
    CHECK(f.children_of(vilId, SquadType::Artel) == 1,
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
// `birth_place@src/macro/place_birth.h`: `at_goal` делается истинным по
// построению, `resting` — истинным, и половина жжения в
// `settle_pools_over_time@src/macro/recovery.cpp` не берётся вовсе.
//
// ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ОБЯЗАН БЫЛ ПОЯВИТЬСЯ: дефект прошёл мимо ВСЕХ 135
// приборов и нашёлся только замером приёмки (`placeResting` 1505 из 2458).
// Ни один прибор не спрашивал, цела ли планка у того, кто стоит.
void test_a_standing_place_pays_nothing_for_the_road() {
    Fixture f;
    const MacroHandle vil = f.place(SquadType::Village, 10, 10, 64);
    const MacroHandle city = f.place(SquadType::City, 20, 20, 64);

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
    // `birth_place` ронял только проверки про саму цель.
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

    // ЯКОРЬ ПЕРВЫЙ — ЦЕЛЬ ЕСТЬ, И ОНА СВОЯ КЛЕТКА. Утверждается НОСИТЕЛЬ, а
    // не следствие: пустая цель (`kNoCell`) и была тем, что предикат привала
    // читал как «тело в пути».
    for (const MacroHandle h : {vil, city}) {
        CHECK(f.st().goal[h.slot].cell != ecs::kNoCell,
              "у места ЦЕЛЬ ЕСТЬ — пустота заполнена ЗНАЧЕНИЕМ, а не "
              "оставлена умолчанием");
        CHECK(f.st().goal[h.slot].cell == f.st().cell[h.slot].idx,
              "и цель эта — РОВНО своя клетка: «стой тут», прибытие истинно "
              "по построению");
        // `what` = 0 есть `GoalKind::None`: целью-МЕСТОМ эту запись не примет
        // никто, и спор за колонку невыразим.
        CHECK(ecs::goal_kind(f.st().goal[h.slot].what) == ecs::GoalKind::None,
              "род цели — None, поэтому `AiView::goal_place` отвечает «нет "
              "места» и запись не путается с целью-местом");
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
              "и в путь не ушло: цель на своей клетке ног не зовёт");
        CHECK(f.st().cell[h.slot].idx == f.st().goal[h.slot].cell,
              "и с места не сдвинулось — клетка та же, что цель");
    }
    CHECK(citySpStart == int(f.st().pools[city.slot].sp),
          "у города та же планка, что в начале — закон один на оба рода, "
          "ветки «а городу иначе» нет");
}

}  // namespace

int main() {
    test_village_produces_through_its_own_think();
    test_village_raises_an_artel_and_pays_in_souls();
    test_city_raises_a_collector_only_for_a_debtor();
    test_no_duplicate_child_however_long_the_world_runs();
    test_a_standing_place_pays_nothing_for_the_road();
    return sm::test::report("place_module_day_test");
}
