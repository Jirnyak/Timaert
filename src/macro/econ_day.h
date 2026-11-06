// The honest economy day-loop — костяк №1 (work_vector, owner-approved form).
//
// THE LAW this module exists to uphold: RESOURCE IS CONSERVED. Nothing is
// created from population formulas; every unit is gathered from a deposit by
// worker-days, transformed by a recipe, or consumed by a need. The self-play
// test (tests/econ_v1_test.cpp) proves the ledger balances to the unit —
// that test, run over years of game time, is the balancing arbiter.
//
// Anchor unit: the PERSON-DAY. 1 unit of the hunger row feeds 1 pop for 1
// day (the row is ASKED, never named — hunger_commodity_ordinal); a gatherer
// pulls kGatherPerWorkerDay raw units a day; each recipe names its
// output-per-worker-day. Round po2-family numbers by house style.
//
// ЗДЕСЬ ЖИЛ ВТОРОЙ ФАКТНЫЙ ПУТЬ МИРА — `EconFact`/`EconFactSink`, И ОН
// УНИЧТОЖЕН 2026-11-03 ВЕРДИКТОМ ВЛАДЕЛЬЦА (дословно): «эпика экономики же не
// будет ну в том смысле что у нас теперь ии модули внутри себя занимаются
// всеми вопросми / наверное убрать надо вообще сделать факты более
// абстрактными общими агностичнвми сборными типа если это факт убийство то да
// и число и кого это уже из таблиц и систем - то естьс системный полход».
//
// ЧЕМ ОН БЫЛ. POD-колбэк с девятью родами (Gathered/Produced/Starved/
// Consumed/Minted/Scrapped/SoulsBorn/SoulsDesertedUnpaid/SoulsKilled) и
// ведомостью склада душ на нём. Девять отправителей в проде, НОЛЬ читателей:
// `MacroWorld::econFacts` не ставил ни один путь ИГРЫ, то есть каждая эмиссия
// была no-op всегда, а читали канал только четыре прибора.
//
// ПОЧЕМУ УМЕР, А НЕ ПОЧИНЕН. Факт мира есть РОД 5 канонического фрейма
// (`WorldFact@src/bus/fact.h`), и шестого рода не бывает; канал рядом с ним
// был вторым ответом на один вопрос мира (DOD п.6) с собственным словарём
// родов, собственной формой строки и собственной дверью. Поток дня вернётся
// СТРОКАМИ РОДА 5, когда у него будет читатель в мире, а не в приборе.
//
// ЧТО ЭТИМ ПОТЕРЯНО, НАЗВАНО ВСЛУХ (AGENTS §1 — дыра стоит открытой, времянки
// на её место не ставим): у мира больше нет ведомости ПОТОКОВ — ни товарных
// (добыто/произведено/съедено/отчеканено), ни душевых (рождено/дезертировало/
// погибло). Остались УРОВНИ по контейнерам, а уровень есть сумма нескольких
// причин: место, потерявшее сто душ, и место, выплатившее сто душ в артель, к
// вечеру выглядят одинаково. Дыра названа нарядом в `macro-registry.md`.
//
// NOT WIRED into world_tick yet — deliberately additive. Wiring replaces
// EconomyState's twin float arrays with Stockpile and is its own increment.
#pragma once
#include <array>
#include <cstdint>
#include "bus/anketa.h"   // SkillId/Skills — ремесло открывает рецепт
#include "tables/actions.h"   // строка труда: темп, право, база — одна на оба мира
#include "tables/commodity.h"
#include "tables/econ_laws.h" // kGatherPerWorkerDay/sp_price — законы труда
                              // уехали в нейтральный слой (их читает субмир);
                              // макро-читатели переезда не замечают
#include "tables/seasons.h"

namespace sm {

// Строка рода — только по ССЫЛКЕ (§12: forward-declare в заголовке,
// include в .cpp): тянуть сюда весь реестр ради одного параметра
// значило бы заставить каждого включившего парсить его.
struct SquadBirthRow;   // таблица рождения рода (macro/characters.h)

// ── State ────────────────────────────────────────────────────────────────

// Integer units per commodity — discrete by house style; signed so a
// bookkeeping bug shows up as a negative number, not a silent wrap.
// THE store of a place is its INVENTORY — there is no second container and no
// second index space (owner's ruling, 2026-08-27: полное слияние). A flat
// `Stockpile` of 14 commodity counts used to live here and be converted to and
// from the place's inventory TWICE PER GAME DAY per place, through a
// string lookup each way, because the same noun was addressed by two different
// ordinals. Каждое существительное мира — одна строка одного каталога.
//
// The day's steps stay PURE (owner: «шаги остаются ЧИСТЫМИ»): they take data
// and tables, never the world.
//
// A commodity's CATALOG ordinal, resolved once and cached — the day loop asks
// this instead of carrying a second numbering of the same things.
int commodity_item_index(int commodityIdx);

// ТОТ ЖЕ МОСТ ДРУГИМ КОНЦОМ: товарный ординал строки каталога, или -1 у
// строки, товаром не являющейся (оружие, монета, существо). Нужен там, где
// система УЖЕ держит каталожный ординал (слот склада несёт `def`, строка
// цели — свой ординал), а сказать надо о ТОВАРЕ: факт ведомости, счёт нужды
// и прейскурант адресуются товарным ординалом, а не каталожным
// (ecs::SquadUpkeep). Второй таблицы не рождается — это тот же
// список kCommodities, прочитанный наоборот и кэшированный один раз.
int commodity_of_item(int itemIdx);

// КАТАЛОЖНЫЙ ОРДИНАЛ ВЫХОДА РЕЦЕПТА, РЕЗОЛВНУТЫЙ ОДИН РАЗ — то же число,
// которым день уже живёт (`resolved()` в econ_day.cpp). Кривая цены
// (economy.cpp) спрашивает его вместо хеша по каталогу на каждый вызов —
// второго кэша не рождается, это тот же массив, открытый дверью.
// -1 у строки, чей id каталогу неизвестен.
int recipe_out_item(int recipeRow);

// ОБРАТНЫЙ КОНЕЦ ТОГО ЖЕ МОСТА: строка каталога → ЕЁ рецепт (-1 у строки
// без состава). Монетные строки (архетип Currency) отвечают минтом: двенадцать
// номиналов делает ОДИН рецепт kMintOutput, это его разворот фракцией.
// Читатель — верстак руки (вкладка Craft): цена и право партии приходят из
// Craft-строки рецепта, второго носителя у них нет.
int recipe_of_item(int itemIdx);

// МОНЕТНАЯ СЕМЬЯ ФРАКЦИИ — КАТАЛОЖНЫМИ ОРДИНАЛАМИ, ЗОЛОТО ПЕРВЫМ (тот же
// порядок, которым чеканит день: старший номинал бьёт тем же темпом в 100×
// стоимости). Резолв один раз на фракцию: три хеш-поиска по строкам монет
// стояли на КАЖДОМ месте-дне производственного дня. Вне диапазона — имперская
// семья, как и у `faction_coins`; factionIdx < 0 — «двора нет», три -1.
const std::array<int, 3>& faction_mint_rows(int factionIdx);

// ── Data tables ──────────────────────────────────────────────────────────

// v1: buildings are ABSTRACT (owner ruling) — a recipe names the KIND of
// place it runs in, not a building.
// Рецепт открыт ВСЯКОМУ обжитому месту, чьи руки его умеют, — «деревня
// работает хуже уже потому, что её меньше» (владелец 2026-08-30, CANON S10):
// разницу города и деревни оценивает закон КПД по населению ниже, а не второй
// рецепт и не стена по виду места.
// (EconSite УМЕР 2026-09-18, вердикт владельца «сносим сайт». Он был стеной
// ПО ВИДУ: деревне открыт хлеб, всё прочее — City-only, потому что City. Что
// место УМЕЕТ, теперь говорит его анкета — те же скиллы, тот же лист, что у
// всех в макромире, — а рецепт называет ремесло и ранг. Деревня не чеканит не
// потому, что она деревня, а потому что её кузнечный ранг ноль.)

// Рецепт открыт этим рукам? Один вопрос, один ответ, ноль веток по виду.
// Пара «ремесло + минранг» приходит из СТРОКИ ДЕЙСТВИЯ рецепта
// (`gateSkill`/`minRank` его Craft-строки): город и рука спрашивают ОДНУ
// строку, второго гейта нет (M-146, «гейт руки и гейт города — один»).
inline bool recipe_known(const Skills& sk, SkillId craft, int minRank) {
    return sk.of(craft) >= minRank;
}

// РЕЦЕПТ — СПЕЦТАБЛИЦА-ПОЛОВИНА ТРУДА (ломоть «крафт + работа» M-369, та же
// форма, какой добыча делит строку с kGathererDefs): универсальная половина
// — база, цена-темп и ПРАВО — живёт Craft-строкой таблицы действий, ссылка
// ординалом; здесь остаётся только СВОЁ — что выходит из партии. МАТЕРИЯ
// стала составом строки каталога 2026-09-11; ТЕМП жил колонкой предмета
// (item_labour) с 2026-09-12 и уехал в строку действия 2026-11-06 — носитель
// темпа труда теперь ОДИН на добычу и крафт, на оба мира.
struct RecipeDef {
    const char*  output;    // id выходной строки (товар, монета, вещь)
    std::uint8_t action;    // ссылка-ординал Craft-строки таблицы действий
};

// The MINT output marker: not a commodity row — the produce scheduler
// resolves it into the town's own faction coin (CANON S10: чеканка = рецепт;
// выход монет из единицы металла = стоимость металла по единой таблице цен,
// «таблица цен и есть монетный двор»; сеньораж эмерджентен из рыночного
// спреда серебра).
inline constexpr const char* kMintOutput = "coin";
// The mint's matter is NOT stated here (owner verdict 2026-09-12, «привести
// к единой системе»): it is the COIN ROW'S OWN composition — {silver 1} →
// yield 32 (items.cpp kPartsAuthoring, «состав монеты и есть монетный
// двор») — so striking and MELTING coin are the one reaction every other
// item runs. What this row keeps is only the LINK: the right and the tempo
// are the mint_coin action row's own columns, like every other recipe's.

// (ЯКОРЬ ТЕМПА `kGatherPerWorkerDay` УЕХАЛ в tables/econ_laws.h ломтём 1б
// эпика пакетов v1 — его читает субмир, а §11 запрещает ему macro/. Таблица
// рецептов ниже задаёт тот же темп «за рабочий день» и читается рядом с ним.)

// РЕМЕСЛО И РАНГ — колонки Craft-СТРОКИ рецепта (tables/actions.h); ранги
// расставлены редко и без претензии: это РАННЯЯ ДЕМКА (владелец, 2026-09-18)
// — закладывается СИСТЕМА, а рецептов у ремесла со временем станет много, и
// тогда шаг между ними станет узким сам собой.
//
// ПОКРЫТИЕ ПОЛНОЕ (вердикт владельца 2026-09-27, «оружие и броня — товар
// производства… ВКЛЮЧАЕТСЯ СРАЗУ, И ПРИБОР — СУДЬЯ», включено ломтём
// 2026-11-06): всякая строка каталога с СОСТАВОМ имеет рецепт — оружие, кожа
// и щиты встали рядом с товарами, город кует их тем же argmax-ом дня.
// Биекцию «состав ⇔ рецепт» держит свидетель (tests/item_parts_test.cpp).
inline constexpr RecipeDef kRecipes[] = {
    // What each output CONSUMES lives on its catalog row (items.cpp
    // kPartsAuthoring); HOW FAST it turns and WHOSE hands may — on its
    // action row. ПИЩИ ЗДЕСЬ НЕТ И БЫТЬ НЕ МОЖЕТ (CANON S10 «ЕДА НЕ
    // ПРОИЗВОДИТСЯ — ЕДА ДОБЫВАЕТСЯ»): у пищевой строки нет состава, а
    // кандидат без состава отсеивается дверью производства (econ_day.cpp
    // item_parts().empty()).
    // ЧЕКАНКА — кузнечное дело высокого ранга (право — строкой mint_coin).
    // Права чеканки КОЛОНКОЙ не существует (канон, вердикт №12): чеканит
    // тот, чьи руки умеют.
    {kMintOutput, action_slot("mint_coin")},
    {"bricks",    action_slot("fire_bricks")},
    {"cloth",     action_slot("weave_cloth")},
    {"tools",     action_slot("forge_tools")},
    {"furniture", action_slot("join_furniture")},
    {"wagon",     action_slot("build_wagon")},
    {"jewelry",   action_slot("cut_jewelry")},
    {"carving",   action_slot("carve_figure")},
    {"statue",    action_slot("hew_statue")},
    // ЗЕЛЬЯ ВАРЯТ (владелец, 2026-09-18). Состав у них в каталоге был всегда
    // ({mat_herb 2} → 8), в производственном дне — не было. Пока травы в мир
    // не приходят, строка честно варит ноль партий: у рецепта нет материи, а
    // не нет рецепта.
    {"potion_hp", action_slot("brew_potion_hp")},
    {"potion_mp", action_slot("brew_potion_mp")},
    // СНАРЯЖЕНИЕ — рецепты пришли 2026-11-06 вместе с правом в строках
    // действий: прежде состав был у этих вещей всегда, а права не было ни у
    // одной («город не кует оружие НИКОГДА» — расхождение M-146, закрыто).
    {"wpn_dagger",      action_slot("forge_dagger")},
    {"wpn_sword",       action_slot("forge_sword")},
    {"wpn_spear",       action_slot("forge_spear")},
    {"wpn_axe",         action_slot("forge_axe")},
    {"wpn_mace",        action_slot("forge_mace")},
    {"wpn_staff",       action_slot("carve_staff")},
    {"wpn_bow",         action_slot("carve_bow")},
    {"arm_leather",     action_slot("sew_leather")},
    {"shield_buckler",  action_slot("forge_buckler")},
    {"shield_round",    action_slot("bind_round_shield")},
    {"shield_tower",    action_slot("bind_tower_shield")},
};
inline constexpr int kRecipeCount = int(sizeof(kRecipes) / sizeof(kRecipes[0]));

// ── СТЕНА СПЕЦТАБЛИЦЫ: ССЫЛКА МИМО КРАФТОВОГО ПОДОТРЕЗКА НЕСОБИРАЕМА ─────
// Та же стена, что у добычи (`gatherer_rows_point_at_labour@src/macro/
// npc_ai.cpp`): ордината, глядящая в чужой тип, означала бы право и темп из
// чужого закона. Плюс 1:1 — у двух рецептов не бывает одной строки действия
// (у каждого СВОЁ право и СВОЙ темп), и крафтовых строк без рецепта не
// бывает (строка без читателя — колонка-сирота, DOD п.9).
inline constexpr bool recipe_rows_point_at_craft() {
    if (kRecipeCount != kActionCraftCount) return false;
    for (int i = 0; i < kRecipeCount; ++i) {
        const int ord = int(kRecipes[i].action);
        if (!action_ordinal_valid(ord)) return false;
        if (action_def(ord).type != ActionType::Craft) return false;
        for (int j = 0; j < i; ++j)
            if (kRecipes[j].action == kRecipes[i].action) return false;
    }
    return true;
}
static_assert(recipe_rows_point_at_craft(),
              "спецтаблица рецептов: каждая строка ссылается на СВОЮ "
              "Craft-строку таблицы действий (1:1, без дублей), и крафтовых "
              "строк без рецепта не существует");

// ── НУЖДА ЕСТЬ ДОЛЯ БЮДЖЕТА ГОРОЖАНИНА, А НЕ СТРОКА АВТОРСКОЙ ЛЕСТНИЦЫ ────
//
// ВЕРДИКТ ВЛАДЕЛЬЦА 2026-09-26 (дословно, CANON блок «СПРОС ОТ СТОИМОСТИ»):
// «это ужасно ради частной системы растить колонку предметов»; «давай нужда
// скока надо в день будет эквивалента стоимости … тогда никакой колонки не
// надо просто на месте одна формулка»; «естественно фиксируем бюджет
// горожанина у нас мир с абсолютной стоимостью никаких инфляций так что мы
// просто решим сколько стоит день горожанина!»; «когда товаров мало
// горожанину надо фикс колво стоимости он их жожирает много мало товаров а
// когда товаров много расширим то гороажину надо чуть чуть того чуть суть
// сего но много разного».
//
// ЗДЕСЬ УМЕРЛА ТАБЛИЦА `kNeeds` (8 строк `{commodity, popPerUnitDay}`). Она
// была ВТОРЫМ СЛОВАРЁМ о нужде: рядом с ней стоял тег каталога, и новая
// строка товара нужды НЕ ПОЛУЧАЛА, пока кто-нибудь не вспомнит дописать ряд
// вручную. Замерено на живом мире: `wagon` носил тег `Goods`, имел рецепт и
// кузнеца, который его умеет, — и прямой спрос НОЛЬ НАВСЕГДА, потому что
// ряда лестницы у него не было. Её числа к тому же разъехались: стоимость
// на житель-день гуляла ×32 (кирпич 0.156, украшение 5.0), хотя ТРИ ряда из
// восьми (cloth, furniture, statue) стояли ровно на одной доле — то есть
// формула в таблице была и её потеряли.
//
// ТЕПЕРЬ ОДНА ФОРМУЛА НА МЕСТЕ:
//   доля строки за житель-день = kComfortValuePerPopDay / N стоимости,
//   нужда в ЕДИНИЦАХ            = доля / стоимость строки,
// где N — число строк словаря, чья категория ТОВАР; оно ВЫВОДИТСЯ проходом
// по словарю, а не пишется константой (ЗАКОН АГНОСТИЧНОСТИ: сколько строк
// поставят — столько и будет). Отсюда даром: дорогая вещь нужна редко,
// дешёвая часто; расширили словарь — доля каждой упала, и горожанин хочет
// «чуть-чуть того, чуть-чуть сего, но много разного»; строк мало — он
// доедает те, что есть.
inline constexpr int kComfortValuePerPopDay = 4;

// Строка словаря есть КОМФОРТ? Ответ — КАТЕГОРИЯ строки каталога (ЗАКОН
// СТРОКИ КАТАЛОГА: поведение из колонок, никогда из списка имён). Монета
// товаром не является с 2026-09-26 — у неё свой архетип
// `ObjectArchetype::Currency`,
// иначе горожанин НУЖДАЛСЯ БЫ В МОНЕТАХ по своей доле бюджета (M-138).
// Сырьё нужды не имеет по вердикту 2026-09-20 («у ресурсов да, нет нужды,
// они только на крафт»), еда — не комфорт, а ГОЛОД (своя система).
bool commodity_is_comfort(int commodityIdx);

// N — сколько строк комфорта в словаре. Выведено проходом, один раз.
int comfort_row_count();

// Сезонный счёт строки комфорта в ЕДИНИЦАХ: население × сезон × доля /
// стоимость. УМНОЖЕНИЕ ПЕРЕД ДЕЛЕНИЕМ и ОДНО деление на всё — целочисленный
// ноль у мелкого места был бы тем же дефектом, что убила прежняя правка
// `season_need_units` (хутор в 30 душ не хотел ни ткани, ни кирпича, и место
// с нулевой нуждой НЕЛЬЗЯ снабдить — караван выгребал его досуха).
int season_comfort_units(int population, int commodityIdx);

// ── ГОЛОДНАЯ СТРОКА ЛЕСТНИЦЫ — одна дверь на всех, кто ест ────────────────
//
// Одна строка отвечает не за неуют, а за ГОЛОД. Она НЕ НАЗВАНА словом — она
// УЗНАЁТСЯ, ровно по тем двум колонкам, по которым её и судит суд границы
// (`upkeep_season_window`): нужда, у которой одна единица кроет один житель-день
// (popPerUnitDay == 1). Что она из категории ЕДА — закон, который держит
// свидетель: ярус товара (CommodityTier) умер 2026-09-18, «что это за вещь»
// отвечает ОДИН архетип каталога (tables/object_archetype.h).
//
// ЗАЧЕМ ДВЕРЬ, А НЕ СЛОВО. Тот же харч списывают ТРОЕ, и до 2026-09-18 они
// спрашивали его тремя способами: население — лестницу (честно), гарнизон
// места (world_tick garrison_upkeep_) и сквад в поле (npc_ai
// squad_season_window) — литералом "bread". Литерал здесь хуже обычного
// дрейфа: он держит в руках ответ на вопрос, который УМЕЕТ меняться. Сделай
// голодной другую строку или переименуй эту — лестница поедет, а две армии
// молча продолжат искать в закромах слово, которого там больше нет. И это
// не выглядит как поломка: голод не наступит, наступит НЕсписание — армия
// перестанет есть, останется сытой и бесплатной, а зелёная суита ничего не
// заметит, потому что «съедено 0 из 0» ни одному инварианту не противоречит.
//
// АРМИЯ ЕСТ ТОЛЬКО ЭТУ СТРОКУ, И ЭТО НАМЕРЕННО (вердикт владельца
// 2026-09-18, спрошено прямо): горожанин судится по ВСЕЙ лестнице (хлеб
// голодит, остальное копится неудовлетворённым), а солдат — по харчу и
// плате, и только. «Армия хочет БОРД и ПЛАТУ» — это разница замысла, а не
// недоделка, и дверь названа «голодная строка» именно поэтому: она отдаёт
// ту единственную нужду, которую обязан покрыть всякий, кто кормит людей.
// ГОЛОДНАЯ СТРОКА УЗНАЁТСЯ КАТЕГОРИЕЙ, А НЕ ДЕЛИТЕЛЕМ (M-137, 2026-09-26).
// Прежде она узнавалась по колонке `popPerUnitDay == 1` умершей лестницы, и
// компилятор держал «ровно одна такая» через `static_assert`. Теперь признак
// — АРХЕТИП каталога (`ObjectArchetype::Food`), то есть то же свойство, что
// проверял свидетель, но названное там, где оно живёт. Цена честная и названа:
// категория видна только единице трансляции каталога, поэтому «ровно одна
// пищевая строка словаря» прибито `static_assert`ом ВНУТРИ `items.cpp`, а
// дверь ниже отвечает -1, если каталог этому закону противоречит.
// Товарным ординалом — то, ЧЕМ голод адресуется в СЧЁТЕ (`needDebt`
// индексируется товарным ординалом: ecs::SquadUpkeep/bus/upkeep.h).
int hunger_commodity_ordinal();

// Она же каталожным ОРДИНАЛОМ — то, ЧЕМ её списывают из закромов.
int hunger_item_index();

// СЕЗОННЫЙ СЧЁТ ГОЛОДА: одна единица кроет один житель-день, поэтому счёт И
// ЕСТЬ житель-дни (якорь мира, `kGatherPerWorkerDay` кормит 32 таких рта).
// Это НЕ доля бюджета комфорта: за еду отвечает своя система (вердикт
// владельца 2026-09-26: «ЗА ЕДУ У НАС ОТВЕЧАЕТ СИСТЕМА ГОЛОА УНИВЕРСАЛЬНАЯ
// ДЛЯ ВСЕХ И ГОРОДОВ И СВКАДОВ В ПОЛЕ»; сведение рта места и рта отряда в
// один — M-140, внутри эпика «место = сквад»).
constexpr int season_hunger_units(int population) {
    return population > 0 ? population * kDaysPerSeason : 0;
}

// One worker per this many heads (po2) — the BENCH quota: the share of
// hands staffing the recipes (econ_produce_day). Крю-половина закона труда
// 2026-08-31 (crews rows + labourShift) казнена ломтём 4 M-72, а колонка
// `labourShift` снесена 2026-11-01 (ломоть 5б: сирота со стражем) — квота
// станков осталась ЕДИНСТВЕННЫМ носителем этого числа; «сколько рук у рода»
// назовёт модуль рода своим числом, когда появится читатель.
inline constexpr int kCreaturesPerCityWorker = 8;

// (THE WORKING RHYTHM died here, 2026-09-16. `kWorkCyclesPerBar` = 4 declared
// how many hauls a rested worker made in a day, and `kGatherPerCycle` = 32/4
// derived how much one haul brought — a number honestly derived from a number
// nobody could derive. Both are gone: a worker now takes ONE object per act at
// the player's own price, an ARTEL takes one per hand, and the trip home
// happens when the backs are full, the bar is spent, or the ground gives
// nothing. How many trips that makes is a consequence of carrying capacity and
// stamina — a property of the world instead of a knob. CANON S14.1.)

// (ЦЕНА ДЕЙСТВИЯ `sp_price` УЕХАЛА в tables/econ_laws.h ломтём 1б эпика
// пакетов v1 — вместе с якорем темпа, которым она делит полосу: оба читает
// субмир, а §11 запрещает ему macro/. Закон не тронут, дверь одна.)

// HOW MANY HANDS a production has. Hands multiply the YIELD and never the
// price: the bar belongs to the SQUAD (it is the avatar body's own bar and
// does not grow with the creatures), so N souls do N workers' work for one
// action's price. Owner, verbatim: «каждый работник рубит по дереву, SP
// тратится как у игрока, просто деревьев в число людей больше».
//
// The avatar is a hand too — that is the +1, and it is why a lone walker is
// not a special case of anything.
inline constexpr int production_hands(int creatureCount) {
    return 1 + (creatureCount > 0 ? creatureCount : 0);
}

// ── ФАКТЫ ДНЯ — РОД 5 ФРЕЙМА, И ЗДЕСЬ ИХ БОЛЬШЕ НЕТ ──────────────
// (некролог — в шапке файла: `EconFact` и `EconFactSink` уничтожены
//  2026-11-03, факт мира есть `WorldFact@src/bus/fact.h`)


// ── СТОРОНА-ПРИЁМНИК с долгом места (CANON S10, вердикт 2026-09-19) ──────
//
// «Любая смена инвентаря, которая добавляет предметы» — одна форма для
// дверей прихода (haul_between, transfer_value_dense): сумка проходит
// голым инвентарём (needDebt == nullptr, неявная конверсия), МЕСТО отдаёт
// свой счёт — и дверь гасит долг СРАЗУ тем, что упало
// (econ_pay_debt после кредита). Дневной такт settle_rooted_day остаётся
// СТРАХОВКОЙ для путей мимо дверей (торговая панель игрока).
// (Пара `sink`/`user` — «куда докладывать Consumed» — умерла 2026-11-03
//  вместе со вторым фактным путём; некролог в шапке файла.)
struct Depot {
    Inventory& inv;
    std::int32_t* needDebt = nullptr;  // счёт места или null (сумка)
    Depot(Inventory& i) : inv(i) {}
    Depot(Inventory& i, std::int32_t* debt) : inv(i), needDebt(debt) {}
};

// ── The day, in three pure steps ─────────────────────────────────────────

// РАНЖИРОВАНИЕ РУК (CANON S10, владелец 2026-09-18: «руки идут туда, где выше
// стоимость выхода на рабочий день»). Кандидат — выходная СТРОКА КАТАЛОГА:
// одна на товарный рецепт, чьё ремесло и ранг открыты анкете места, плюс по
// одной на каждый номинал у монетного. Каждый рабочий день уходит в кандидата
// с наибольшей стоимостью выхода по ТЕКУЩЕЙ цене склада (цена × партий ×
// выход), где партий не больше, чем даёт труд строки на КПД места и чем
// кормит сырьё на полке. Цена пересчитывается ПОСЛЕ каждого назначения —
// слиппедж производства: полка растёт → цена падает → руки сами переходят
// дальше; сезон проедает склад → цена растёт → возвращаются. Ни порога
// «работать или нет», ни потолка выпуска колонкой: потолок ВЫВОДИТСЯ из цены.
// День кончается, когда ни один кандидат не кормится сырьём.
// Прежние три прохода «стол / честные доли / остатки» УМЕРЛИ 2026-09-18
// вместе с «нуждой» `1 << 30` (econ_day.cpp, там же замер: они были грубым
// ранжированием, где все рецепты равно ценны).
// Returns total units produced. Conservation: inputs leave the store as
// outputs enter.
// `mintFactionIdx`: whose coin FAMILY the kMintOutput recipe strikes — the
// town's faction (its three nominals run gold-first, each off its own metal;
// faction_coins resolves the free folk to the imperial family). -1 = this
// place has no mint and the row simply does not run.
// `needDebt` — счёт места (CANON S10): спрос, по которому ранжируются
// руки, читает ОСТАТОК ДОЛГА (season_demand_for); nullptr — фикстура без
// счёта, прямая часть спроса падает на лестницу населения.
// `dayBaseTicksOut` (ломоть stateTimer M-369): максимум `recoveryTicks`
// Craft-строк, чьи партии сегодня состоялись, — база «сделал → вишу» для
// гейта занятости места; 0 = партий не было. nullptr — звонящему (фикстуре)
// база не нужна.
int econ_produce_day(Inventory& store, const std::int32_t* needDebt,
                     const Skills& hands, int workers,
                     int population,
                     int mintFactionIdx = -1,
                     std::uint32_t* dayBaseTicksOut = nullptr);

// (`ConsumeOutcome` и `econ_debt_boundary` УНИЧТОЖЕНЫ 2026-10-06, M-233:
// это была ВТОРАЯ дверь содержания, и в день границы сезона поселение
// судилось ДВАЖДЫ — тут по старому счёту, потом `upkeep_season_window` по
// счёту, только что этой же дверью перезаписанному. Замер назвал цену: на
// дне 33 здешний суд убил НОЛЬ (целочисленное деление прощало хвост), а
// вторая дверь — 51 322 и 51 544 души, принимая СВЕЖИЙ сезонный счёт за
// недоимку. Четыре умения этой двери не потеряны, а переехали в выжившую и
// там РАСШИРИЛИСЬ, потому что та гейта рода не имеет: комфортные строки
// счёта, благополучие, прощение хвоста меньше одной рто-доли и закрытие
// ВСЕГО счёта у мёртвого. `wellbeing` был float 0…1, который звонящий тут
// же множил на 255 и округлял, — теперь величина считается целыми прямо в
// байт своей колонки, по вердикту владельца 2026-10-06: «флоат это плохо,
// он нужен только когда без него невозможно».)

// ГАШЕНИЕ ДОЛГА — одна дверь «склад платит по счёту» (CANON S10: «всё, что
// падает в него, идёт в уплату долга»). Съеденное СПИСЫВАЕТСЯ со склада —
// после вызова на складе лежит только излишек. Зовётся
// границей (шаг 3 выше) и днём места как страховочный такт; порция Б
// проведёт её через двери прихода (haul/craft/transfer) — «сразу» без лага.
// Возвращает единиц погашено — ЭТО И ЕСТЬ ОТВЕТ ДВЕРИ о съеденном, и после
// сноса второго фактного пути (шапка файла) он у неё единственный.
int econ_pay_debt(Inventory& store, std::int32_t* needDebt);

// Daily slot hygiene, split out of the old daily consume (CANON «Крафт/
// Скрап»): the store past half occupancy melts its cheapest non-fungible
// stacks back to matter. Returns stacks melted.
int econ_store_hygiene(Inventory& store);

// ── Population (owner's law, W2b-4) ──────────────────────────────────────
//
// PROPORTIONAL growth, not flat heads per day: dP = r·P·благополучие, where
// благополучие ∈ [0, 1] is THE one measure of how a place lives — the share
// of its needs it actually paid for (`Wellbeing::seasonWellbeing`, 0…255 —
// целая колонка, а не флоат-доля: вердикт владельца 2026-10-06).
//
// ОТРИЦАТЕЛЬНОГО РОСТА НЕТ, и это закон, а не пропуск (владелец 2026-09-19):
// усушку делает СМЕРТЬ на границе долга, поэтому второй, плавной усушки
// быть не должно — иначе голод карает дважды. Место либо растёт по мере
// своего достатка, либо стоит.
//
// NO CARRYING CAP (CANON S25, the owner's word): «ни константы-потолка, ни
// ёмкости как крышки быть не должно». The ceiling EMERGES from supply — a
// town that outgrows its fields and its trade pays less of its bill, its
// wellbeing falls and growth stops before the cliff. A town on a crossroads
// may outgrow one on black earth with no roads, and that is the right world.
// (The old kPopCarryingCap = 16384 was one lid for every town — audit III.6.)
//
// The rate is quoted PER SEASON (owner 2026-08-24): a month IS a season here
// — 32 days, the same epoch the forest grows by (kGrowthEpochDays) — and in
// a fantasy town of a thousand souls a birth is an event, not daily noise.
// The daily tick only ACCRUES the season rate into the fractional carry;
// whole people appear when the carry crosses one.
inline constexpr float kPopGrowthPerSeason = 1.0f / 8.0f;  // po2: +12.5%/season fully fed
inline constexpr float kPopGrowthRatePerDay =
    kPopGrowthPerSeason / float(kDaysPerSeason);

inline float population_delta_per_day(int population, float wellbeing) {
    if (population <= 0) return 0.0f;
    // Благополучие И ЕСТЬ ход роста: 1.0 — полные нужды и полные 12.5 % за
    // сезон, 0.5 — половина, 0 — стоим. Ни клампа сверху (предел
    // эмерджентен, S25), ни отрицательной ветки снизу (усушка = смерть).
    return kPopGrowthRatePerDay * float(population) * wellbeing;
}


// ── Birth stocks ─────────────────────────────────────────────────────────

// A place is born MID-LIFE, not at hour zero (owner's ruling): worldgen
// seeds its universal Inventory as if it had been living for years, so the
// market has wares on day one and nobody starves while the first caravans
// find their legs. One law, deterministic from population:
//   · the HUNGER row (hunger_commodity_ordinal — asked, never named) holds
//     kSeedVitalDays of the table — a larder, not a warehouse;
//   · every other need row holds a stretch of its daily demand — a season
//     in a crafting City, days in a gathering Village;
//   · raw stocks are a production buffer per head — doubled in a Village,
//     whose whole business is raw.
//   · КАЗНЫ ПРИ РОЖДЕНИИ НЕТ (M-139, вердикт владельца 2026-09-26): место
//     рождается со складом и БЕЗ МОНЕТ, потому что выдача монет из воздуха
//     снесена целиком. Отсюда и сигнатура без `factionIdx`/`seedSalt` —
//     чьи номиналы печатать и с каким разбросом, спрашивать больше некому.
// РОД ПРИХОДИТ СТРОКОЙ, А НЕ БУЛЕВЫМ ФЛАГОМ: `bool isCity` смуглил сюда
// ось рода, и дверь решала по нему два числа сама. Теперь числа — две
// колонки строки (`seedComfortDays`/`seedRawMult`), а дверь их только
// читает (ЗАКОН АГНОСТИЧНОСТИ: фундаментальная система не знает, кто и
// зачем её позвал). Оба прежних звонящих передавали ТАВТОЛОГИЮ —
// спрашивали род у слота там, где литералом только что его записали.
void seed_rooted_inventory(Inventory& inv, int population,
                            const SquadBirthRow& row);


} // namespace sm
