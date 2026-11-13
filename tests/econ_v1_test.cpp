// The self-play harness for the v1 economy day-loop — the BALANCING ARBITER
// (work_vector №1, owner-approved methodology): the economy is not balanced
// by eye, it is balanced by laws asserted over a world that plays itself.
//
// Laws pinned here:
//   1. DICTIONARY INTEGRITY — unique ids, raw rows first, recipes and needs
//      reference real rows, recipe outputs are never raw, material masks only
//      address raw rows.
//   2. CONSERVATION — over 64 simulated days of a village+city pair, every
//      commodity's ledger balances to the unit:
//        gathered + produced == used_as_inputs + consumed + stock_remaining
//      Nothing is created from population, nothing vanishes.
//   3. NO SILENT STARVATION — the balanced scenario feeds everyone every day;
//      the famine scenario starves (the ONE hunger fact is Starved —
//      FamineStarted/FamineEnded were cut 2026-09-21 as emitterless,
//      TRANSITIONS, not every day).
//
// ФОРМА ВЕРДИКТА — ОДНА, И ЭТО `check.h` (§8 п.1). Здесь стоял свой
// `int fail(const char*)`, печатавший `line 0`, и ПЯТЬДЕСЯТ ШЕСТЬ законов
// вносили в счёт НОЛЬ проверок на зелёном прогоне: первый провал обрывал
// `main` до `report()`, а охват печатался константным литералом
// «dictionary=ok … mint_conservation=ok» — девятью словами «ok», ни одно из
// которых не было ответом прибора. Теперь каждая секция — void-функция, её
// законы судит `CHECK`, предусловие фикстуры — `CHECK_OR_RETURN` (провал
// записан ДО выхода, поэтому выход его не прячет), а охват печатается
// ЧИСЛАМИ, которые секции и так считают.

#include "macro/characters.h"   // squad_birth — руки рода
#include "check.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/economy.h"       // row_side / lot_take / lot_loss — оценка запаса
#include "tables/squad_type.h"  // SquadType — ось рода (амбар рождения уехал
                                // в kSquadBirth@src/macro/characters.h)
#include "macro/upkeep_window.h"  // upkeep_season_window — ОДНА дверь суда границы (M-233)
#include "macro/squad_act.h"      // squad_craft — акт крафта сквада (M-486)
#include "macro/npc_ai.h"         // village_craft_pick, city_craft_pick — решения модулей
#include "bus/squad_sheet.h"      // refresh_body_from_sheet — тело той же дверью, что мир
#include "bus/world_row.h"   // raise_flock_into_container / count_human_souls / work_units
#include "bus/anketa.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

// ── СУД ГРАНИЦЫ — ОДНА ДВЕРЬ МИРА, И ЭТО АДАПТЕР К НЕЙ (M-233) ───────────
// Вторая дверь содержания (`econ_debt_boundary`) УНИЧТОЖЕНА: в день границы
// она судила поселение ПЕРВОЙ, по старому счёту, а `upkeep_season_window` —
// ВТОРОЙ, по счёту, только что ею же перезаписанному, и принимала свежий
// сезонный счёт за недоимку (замер: 51 322 души за одну границу).
//
// НИ ОДИН ЗАКОН, УТВЕРЖДАЕМЫЙ БЛОКАМИ НИЖЕ, НЕ ИЗМЕНИЛСЯ. Все четыре
// переехали в выжившую дверь и там перестали зависеть от рода: прощение
// хвоста меньше одной рто-доли, комфортные строки счёта, благополучие,
// закрытие счёта у мёртвого. Формула смертей — та же буквально:
// `недоимка / (счёт/ртов)`.
//
// Вход у выжившей шире — пул ходоков, колонка благополучия, костёр платы.
// Блоки ниже судят ГОЛОД и КОМФОРТ, а плата у строки Peasant нулевая
// (`upkeepGoldPerDay` ноль), поэтому ходоков тут не бывает и пул с костром
// — приёмники, чьё содержимое ни одно утверждение не читает. Это сказано
// вслух, чтобы следующий читатель не принял локальный пул за дыру.
struct Boundary {
    int starved = 0;   // было ConsumeOutcome::starvedPop
    // Было `ConsumeOutcome::wellbeing`, float 0…1, который звонящий тут же
    // множил на 255. Теперь величина ЦЕЛАЯ и считается прямо в байт своей
    // колонки (вердикт владельца 2026-10-06 о флоате), поэтому и утверждения
    // ниже сравнивают целые: половина — это 128, а не 0.5f ± эпсилон.
    int wb255 = 0;
};
Boundary boundary_(sm::Inventory& store, std::int32_t* needDebt) {
    sm::Upkeep r{};
    for (int c = 0; c < sm::kCommodityCount; ++c) r.needDebt[c] = needDebt[c];
    sm::Wellbeing wb{};
    const sm::UpkeepWindowOutcome out =
        sm::upkeep_season_window(r, store, wb);
    for (int c = 0; c < sm::kCommodityCount; ++c) needDebt[c] = r.needDebt[c];
    return Boundary{out.starved, int(wb.seasonWellbeing)};
}
// Руки города и деревни — анкета их рода (characters.h), а не вид места.
static const sm::Skills& CITY =
    sm::squad_birth(sm::SquadType::City).sheet.skills;
static const sm::Skills& VILLAGE =
    sm::squad_birth(sm::SquadType::Village).sheet.skills;

// (ЛЕДЖЕР `Ledger` + СТОК `sink` СТОЯЛИ ЗДЕСЬ и умерли 2026-11-03 вместе со
//  вторым фактным путём мира (`EconFact`/`EconFactSink`, некролог и вердикт
//  владельца — в шапке `src/macro/econ_day.h`).
//
//  ТРИ ЕГО ПОЛЯ БЫЛИ МЕРТВЫ УЖЕ ДО СНОСА, И ЭТО НАДО СКАЗАТЬ ЧИСЛОМ:
//  `gathered`/`produced`/`consumed` не читало НИ ОДНО утверждение этого файла
//  — их единственным читателем была 96-дневная самоига, вырезанная
//  2026-10-04. Читался один `starvedEvents`, и его утверждение разобрано на
//  месте, ниже.)

// (ЗДЕСЬ СТОЯЛА `inputs_for_output` — ведомость входов производства за
// единицу выхода. Её единственным читателем была 96-дневная самоига,
// вырезанная вердиктом владельца 2026-10-04 («глупо гонять 100 дней
// экономику, которой ещё нет»), и с тех пор функция стояла без звонящего:
// предупреждение `-Wunused-function` ждало первой же пересборки этого TU.)

// ── ОХВАТ ПЕЧАТАЕТСЯ ЧИСЛАМИ, А НЕ СЛОВОМ «ok» ──────────────────────────
// Каждое поле — счёт ПРОЙДЕННЫХ рядов, то есть то, что усохнет, если свип
// усохнет: сузь границу цикла или выпиши вызов секции из `main`, и число в
// печатной строке поедет. Константный литерал «dictionary=ok …», стоявший
// здесь вместо них, не двигался ни от чего.
struct Coverage {
    int commodityRows   = 0;  // ряды словаря, судимые секцией 1
    int comfortRows     = 0;  // строки комфорта (счёт ВЫВОДИТСЯ проходом)
    int recipeRows      = 0;  // ряды каталога рецептов
    int recipeInputs    = 0;  // части состава, судимые на товарность
    int mintlessRecipes = 0;  // нечеканные выходы, судимые на состав
    int coinRows        = 0;  // монетные колонки реестра фракций
    int linkRows        = 0;  // ряды связи «товар = строка каталога»
    int bornNeedRows    = 0;  // потребляемые строки, судимые при рождении
    int hungerWindows   = 0;  // окна, в которых голод взыскал (закон: 4 из 5)
    int mintRows        = 0;  // монетные строки каталога рецептов
    int openToVillage   = 0;  // нечеканные рецепты, открытые деревне
    int closedToVillage = 0;  // ...и закрытые ей (закон: ноль)
};
Coverage cov{};

using namespace sm;

// ── 1. Dictionary integrity ─────────────────────────────────────────
void sect1_dictionary_commodities() {
    for (int i = 0; i < kCommodityCount; ++i) {
        for (int j = i + 1; j < kCommodityCount; ++j) {
            CHECK(std::strcmp(kCommodities[i].id, kCommodities[j].id) != 0,
                  "duplicate commodity id");
        }
        // СЫРЬЁ ИДЁТ ПЕРВЫМ И ПОДРЯД — закон ПОРЯДКА ОРДИНАЛОВ, и
        // спрашивается он у ЕДИНСТВЕННОГО словаря «что это за вещь»:
        // категории каталога (ярус товарной строки умер 2026-09-18,
        // materialMask — 2026-09-22). Раньше это держал static_assert по
        // ярусу; компилятору категория недоступна
        // — каталог виден только своей единице трансляции, — поэтому закон
        // переехал сюда целиком, не ослабнув.
        const ItemDef* cd = item_def(kCommodities[i].id);
        CHECK_OR_RETURN(cd, "commodity id names no catalog row");
        CHECK((i < kRawCommodityCount)
                  == (sm::item_archetype(*cd) == sm::ObjectArchetype::Material),
              "raw rows must be exactly the first kRawCommodityCount");
        ++cov.commodityRows;
        // The PRICE is not a column here: the one anchor is the same row's
        // ItemDef.value, pinned positive by the link law of section 8 below.
    }
    // ЯКОРЬ НЕПУСТОТЫ: свип по пустому словарю прошёл бы ВАКУУМНО, и зелёный
    // прогон значил бы «законов не нарушено», потому что их никто не спросил.
    CHECK(cov.commodityRows == kCommodityCount && cov.commodityRows > 0,
          "словарь судим ПОЛНОСТЬЮ — вакуумный свип ничего не доказывает");
}

void sect1_dictionary_recipes() {
    for (int r = 0; r < kRecipeCount; ++r) {
        // A recipe's output must be a real CATALOG row — that is what the
        // production day actually makes (composition + yield + labour). Being
        // a COMMODITY on top of that is a statement about the needs ladder,
        // and not every made thing is one: the mint's output is the caller's
        // faction coin (kMintOutput), and a potion is an item nobody eats by
        // the ladder («поушоны будем варить», 2026-09-18).
        const int out = commodity_index(kRecipes[r].output);
        const bool isMint = std::strcmp(kRecipes[r].output, kMintOutput) == 0;
        CHECK(isMint || item_index(kRecipes[r].output) >= 0,
              "recipe output names no catalog row");

        const ItemDef* od = item_def(kRecipes[r].output);
        CHECK(isMint || !od
                  || sm::item_archetype(*od) != sm::ObjectArchetype::Material,
              "recipe may not output raw (only deposits create raw)");
        for (int r2 = r + 1; r2 < kRecipeCount; ++r2) {
            CHECK(std::strcmp(kRecipes[r].output, kRecipes[r2].output) != 0,
                  "recipe outputs must be unique in v1");
        }
        // The recipe's matter = its output row's composition. Every part must
        // be a commodity row too — production moves matter the economy's own
        // dictionary can name (the mint's metal is checked in section 7).
        for (const ItemPart& part : item_parts(commodity_item_index(out))) {
            const ItemDef* d = item_def_at(int(part.def));
            CHECK(d && commodity_index(d->id) >= 0,
                  "recipe input is not a commodity row");
            CHECK(int(part.count) > 0, "recipe qty <= 0");
            ++cov.recipeInputs;
        }
        // ...and its tempo = the recipe's own ACTION row (M-369 «крафт +
        // работа», 2026-11-06): a scheduled output nobody's day can turn is
        // a dead recipe row. The compile-time walls already make a rate-less
        // Craft row unbuildable; this line keeps the runtime witness honest
        // about the same door the day actually reads.
        CHECK(action_def(int(kRecipes[r].action)).spRatePerDay > 0,
              "recipe action has no tempo (batches/person-day)");
        ++cov.recipeRows;
    }
    // ЯКОРЬ НЕПУСТОТЫ, И ОН ДВОЙНОЙ: пустой каталог рецептов и каталог без
    // единой части состава оба дают вакуумно-зелёный свип.
    CHECK(cov.recipeRows == kRecipeCount && cov.recipeRows > 0,
          "каталог рецептов судим ПОЛНОСТЬЮ — пустой свип зелен ни о чём");
    CHECK(cov.recipeInputs > 0,
          "негативный контроль: свип видел части состава, а не пустоту");
}

// ЛЕСТНИЦА kNeeds УМЕРЛА (M-137): нужда есть доля бюджета горожанина, а
// строка комфорта УЗНАЁТСЯ категорией каталога. Закон, который этот
// свидетель охраняет, тот же — «у всякой строки нужды есть каталожная
// строка и ненулевая стоимость, иначе делить долю нечем»; проверяется он
// теперь через ту же дверь, которой живёт мир.
void sect1_dictionary_comfort_rows() {
    int comfortRows = 0;
    for (int c = 0; c < kCommodityCount; ++c) {
        if (!commodity_is_comfort(c)) continue;
        ++comfortRows;
        const ItemDef* d = item_def_at(commodity_item_index(c));
        CHECK(d && d->value > 0,
              "строка комфорта обязана иметь каталожную строку и цену");
    }
    CHECK(comfortRows == comfort_row_count() && comfortRows > 0,
          "N строк комфорта ВЫВОДИТСЯ проходом и не бывает нулём");
    CHECK(hunger_commodity_ordinal() >= 0
              && !commodity_is_comfort(hunger_commodity_ordinal()),
          "голодная строка одна и она НЕ комфорт — за еду отвечает голод");
    cov.comfortRows = comfortRows;
}

// ── 2+3. Self-play: village gathers, city crafts, both eat ──────────
//
// (── 2+3. САМОИГРА 96 ДНЕЙ — ВЫРЕЗАНА ВЕРДИКТОМ ВЛАДЕЛЬЦА 2026-10-04:
// «глупо гонять 100 дней экономику, которой ещё нет — полировка ядра,
// до экономики не дошли». Тойская деревня+город гоняли живые двери
// econ_produce_day/суд границы/econ_pay_debt сквозь 96 дней и
// утверждали равновесие и сохранение леджера — арбитраж БАЛАНСА системы,
// которая режется (ломтик E) и строится заново (M-191). Законы ДВЕРЕЙ
// остались ниже своими блоками: окна голода, переполнение, призрачный
// станок, монетный двор. Новый арбитр принесёт эпик экономики.)

// ── ГОЛОД: СМЕРТЬ ПРОПОРЦИОНАЛЬНА, И ОНА ЖЕ ГАСИТ РОСТ ──────────────
// Долговой закон сдвигает голод на окно (первая граница только
// выставляет счёт — убить ей нечего), а дальше место, платящее
// полсчёта, каждый сезон хоронит половину: 32 → 16 → 8 → 4 → 2.
// И та же доля читается благополучием — ЕДИНСТВЕННОЙ мерой жизни
// (настроение вырезано 2026-09-19): оплатил половину еды — ход роста
// вдвое ниже, и голодавший больше не плодится на полной скорости.
void sect_hunger_proportional() {
    // ОДНА ПИЩЕВАЯ СТРОКА — ОДНО ИМЯ. Здесь стояли `grainIdx` и `breadIdx`,
    // оба `commodity_index("food")`: два мёртвых существительных на один
    // ординал, и именно они делали правдоподобной шапку «день производства
    // печёт хлеб из зерна». Ни зерна, ни хлеба в словаре нет (M-148).
    // Сырьевые ординалы (`wood`/`clay`/`iron`/`stone`) стояли рядом и умерли
    // с той же самоигрой: читателей у них не осталось ни одного.
    const int foodIdx = commodity_index("food");

    Inventory poor{};
    std::int32_t poorDebt[kCommodityCount] = {};
    // СЧЁТ ЕДЫ ЧИТАЕТСЯ С ГОЛОВ (v122: рацион — колонка строки, а не литерал
    // «юнит на душу»), поэтому души кладутся В КОНТЕЙНЕР — фикстура рождает
    // своё предусловие тем же законом, что мир. Взыскание снимает головы
    // сама дверь, значит население читается у контейнера, а не ведётся
    // рядом вторым счётом.
    raise_flock_into_container(poor, 32);
    int poorPop = count_human_souls(poor);
    // «ГОЛОД ГОВОРИТ В КАЖДОМ ОКНЕ» — ЗАКОН ЖИВ, НОСИТЕЛЬ СМЕНЁН (§8 п.6).
    // Здесь это утверждал счётчик фактов `Starved` снесённой ведомости
    // (`fled.starvedEvents != 4`); теперь считается ОТВЕТ САМОЙ ДВЕРИ
    // (`UpkeepWindowOutcome::starved`) — носитель живой, а утверждение то же
    // и даже сильнее: оно про суд, а не про доклад о суде.
    int windowsThatKilled = 0;
    for (int window = 0; window < 5; ++window) {
        poorPop = count_human_souls(poor);
        const Boundary o = boundary_(poor, poorDebt);
        if (o.starved > 0) ++windowsThatKilled;
        if (window == 0) {
            CHECK(o.starved == 0,
                  "the first bill cannot kill before it is due");
        }
        if (window > 0) {
            CHECK(o.starved == poorPop / 2,
                  "a half-paid season claims exactly half the souls");
            // Благополучие = доля еды × доля комфорта. Половина счёта
            // еды — не больше половины хода, НИКОГДА: это и есть
            // возвращённое «голодал — не плодись».
            CHECK(o.wb255 <= 128, "half-paid food can never buy full growth");
            // Пока место достаточно велико, чтобы иметь счёт по комфорту
            // (ткань — 1 на 32 души-дня), голая полка держит ход в НУЛЕ.
            // Усохнув ниже разрешения лестницы, место комфортного счёта не
            // получает вовсе — и тогда ход судит одна еда, это не дыра.
            CHECK(poorPop < 32 || o.wb255 == 0,
                  "a comfortless place does not grow — wellbeing 0");
        }
        // Умерших сняла дверь (их головы ушли из контейнера) — «как
        // settle_rooted_day», где наверху остаётся только паства.
        // Привоз в полсчёта: гасится СРАЗУ той же дверью, что в мире.
        poor.add_of(commodity_item_index(foodIdx),
                    poorDebt[foodIdx] / 2);
        econ_pay_debt(poor, poorDebt);
    }
    CHECK(windowsThatKilled == 4,
          "hunger claims souls in EVERY window but the first");
    cov.hungerWindows = windowsThatKilled;
    // Рельеф — привоз, кроющий счёт целиком: следующая граница не
    // взыскивает никого.
    poor.add_of(commodity_item_index(foodIdx), poorDebt[foodIdx]);
    econ_pay_debt(poor, poorDebt);
    const Boundary relief = boundary_(poor, poorDebt);
    CHECK(relief.starved == 0, "a paid season kills nobody");
}

// ── 4. Boundary: EVERY shortfall lands somewhere (Session 18) ───────
// A season of HARCH in full, everything else absent. Под долгом (CANON
// S10) недоплата видна ВЗЫСКАНИЕМ — на границе, следующей за счётом:
// первая граница выставляет счёт и ларь харча платит его на месте, вторая
// судит остаток — харчевой долг погашен (никто не умер), долг каждой
// прочей строки обязан лечь в unmetComfort целиком, не в пустоту.
void sect4_boundary_shortfall() {
    Inventory s{};
    std::int32_t debt[kCommodityCount] = {};
    const int pop = 256;
    // Души — В КОНТЕЙНЕР: счёт еды читается с голов по таблице
    // (v122), а не с переданного числа. У крестьянина рацион 1/день,
    // поэтому счёт совпадает с прежним «pop × сезон» — закон тот же,
    // спрошен у строки.
    raise_flock_into_container(s, pop);
    s.add_of(commodity_item_index(commodity_index("food")),
             pop * kDaysPerSeason);
    const Boundary first = boundary_(s, debt);
    CHECK(first.starved == 0, "the first bill cannot kill before it is due");
    CHECK(s.count_of(commodity_item_index(commodity_index("food"))) == 0,
          "the bill must eat the whole shelf on the spot");
    const Boundary o = boundary_(s, debt);
    CHECK(o.starved == 0, "harch-only pop must be fed in full");
    // Еда покрыта целиком, комфорт — ни одной строкой: благополучие
    // ноль, и место СТОИТ (вердикт владельца 2026-09-19). Недостача
    // комфорта не падает в пустоту — она и есть этот ноль.
    CHECK(o.wb255 == 0, "a non-daily shortfall fell into the void");
}

// ── 5. Half a season of HARCH starves HALF the town — a season late ─
// ПРОПОРЦИЯ ЖИВЁТ ВО ВЗЫСКАНИИ (CANON S10 + вердикт 2026-09-19
// «смерть — единственная кара»): полсезона хлеба гасят полсчёта,
// непокрытая половина уходит населением на СЛЕДУЮЩЕЙ границе — по
// душе за каждый непокрытый душевой сезон, и та же половина читается
// благополучием как доля оплаченной еды.
void sect5_half_season_starves_half() {
    Inventory s{};
    std::int32_t debt[kCommodityCount] = {};
    const int pop = 128;
    // Души — В КОНТЕЙНЕР: счёт еды читается с голов по таблице
    // (v122), а не с переданного числа. У крестьянина рацион 1/день,
    // поэтому счёт совпадает с прежним «pop × сезон» — закон тот же,
    // спрошен у строки.
    raise_flock_into_container(s, pop);
    const int half = pop * kDaysPerSeason / 2;
    s.add_of(commodity_item_index(commodity_index("food")), half);
    boundary_(s, debt);
    // Каждая единица на полке платит по счёту — склад пуст, долг
    // помнит ровно вторую половину.
    CHECK(s.count_of(commodity_item_index(commodity_index("food"))) == 0,
          "every unit on the shelf must pay the bill");
    CHECK(debt[commodity_index("food")] == half,
          "the debt must remember exactly the unpaid half");
    const Boundary o = boundary_(s, debt);
    CHECK(o.starved == pop - pop / 2,
          "half a season must starve exactly half the souls");
    // Хвост меньше душевого сезона ПРОЩАЕТСЯ на взыскании (зеркало
    // старого «кусок меньше сезона не кормит никого»): 31 хлеба гасят
    // 31 единицу счёта и снимают со смертей ровно одну душу.
    Inventory tail{};
    raise_flock_into_container(tail, pop);
    std::int32_t tailDebt[kCommodityCount] = {};
    tail.add_of(commodity_item_index(commodity_index("food")),
                kDaysPerSeason - 1);
    boundary_(tail, tailDebt);
    const Boundary ot = boundary_(tail, tailDebt);
    CHECK(ot.starved == pop - 1,
          "a sub-season scrap forgives exactly one death");
}

// ── 6. (СТОЯЛО «первый рецепт не забирает весь город» — делёж рук дня между
// рецептами; умер 2026-11-13 вместе с лестницей `econ_produce_day`, ряд M-486:
// акт крафта есть ОДИН рецепт объёмом партий, что крафтить — решает модуль
// сквада по выгоде, и разнообразие выходов стало следствием дней, а не
// законом одного дня.) ──

// ── 7. A recipe with no inputs would mint matter — table law ────────
// Every output must carry a composition on its catalog row, or the day
// makes goods from nothing. The mint's output resolves per town (any
// faction coin), so ALL currency rows answer for it — coin matter (and
// its value-neutrality) is pinned in item_parts_test and by the
// static_assert beside the table.
void sect7_no_matter_from_nothing() {
    for (int i = 0; i < kRecipeCount; ++i) {
        if (std::strcmp(kRecipes[i].output, kMintOutput) == 0) continue;
        CHECK(!item_parts(item_index(kRecipes[i].output)).empty(),
              "recipe with no inputs mints matter from nothing");
        ++cov.mintlessRecipes;
    }
    // ЯКОРЬ НЕПУСТОТЫ: свип, у которого все строки чеканные (или ни одной
    // строки), проходит ВАКУУМНО и зелен ни о чём.
    CHECK(cov.mintlessRecipes > 0,
          "негативный контроль: свип видел нечеканные выходы");
    // Every coin row named by the faction registry's mint columns (the
    // kCurrencyDefs list died with verdict №1) carries matter.
    for (const FactionDef& f : kFactionDefs) {
        for (const char* coin : f.mint) {
            if (!coin || !coin[0]) continue;
            CHECK(!item_parts(item_index(coin)).empty(),
                  "a mint output (faction coin) has no composition");
            ++cov.coinRows;
        }
    }
    CHECK(cov.coinRows > 0,
          "негативный контроль: монетные колонки реестра непусты");
    // ЯКОРЬ ПРОИЗВОДИТЕЛЬНОСТИ (владелец: «1 добытчик кормит 32 душ»).
    // Раньше он держался ДВУМЯ числами — дневной добычей и трудовой колонкой
    // ХЛЕБА, — и закон читался как «каждое звено цепи поле→печь→рот равно
    // одному человеко-дню». Хлеб вырезан 2026-09-20, звено осталось одно, и
    // якорь замыкается на двух таблицах напрямую: день добытчика даёт
    // kGatherPerWorkerDay единиц пищи, а единица пищи кроет РОВНО ОДИН
    // житель-день (голодная строка лестницы, popPerUnitDay == 1). Произведение
    // и есть «кормит 32 душ»; разойдись любая из двух — переоценится весь мир.
    // Якорь спрашивается ДВЕРЬЮ СЧЁТА, какой харч выставляет окно сезона
    // (`upkeep_bill@src/macro/upkeep_window.h`: рацион голов × дни сезона), а
    // не колонкой умершей лестницы: сезон харча 32 душ, делённый на дни
    // сезона, и есть дневная добыча. (Здесь спрашивалась `season_hunger_units`
    // — счёт голода без контейнера, умерший рядом M-488 вместе с кривой, его
    // единственным читателем.)
    {
        Inventory crew{}, one{};
        raise_flock_into_container(crew, 32);
        raise_flock_into_container(one, 1);
        CHECK(count_human_souls(crew) == 32 && count_human_souls(one) == 1,
              "фикстура: души легли в контейнер дверью мира");
        CHECK(upkeep_bill(crew).board / kDaysPerSeason == kGatherPerWorkerDay
                  && upkeep_bill(one).board == kDaysPerSeason,
              "the gatherer-day must feed exactly 32 souls for a day");
    }
    // ...и голодная строка обязана быть ТЕРМИНАЛЬНОЙ материей мира: еду
    // растят и добывают, печь между полем и ртом больше не стоит.
    CHECK(item_parts(hunger_item_index()).empty(),
          "the hunger row must be world matter, not a recipe output");
}

// ── 7б. The overflow law rides the consume day (CANON «Крафт/Скрап») ─
// A store clogged past the half mark by non-fungible lut comes back to
// it within ONE daily tick — the wiring, not the door (the door's own
// laws live in item_parts_test).
void sect7b_overflow_rides_the_day() {
    Inventory s{};
    const int dagger = item_index("wpn_dagger");
    // Past the half mark BY THE CONSTANT the law reads, not a literal
    // calibrated to one capacity (the 256-slot era pinned 200 here).
    const int clogSlots = kAutoScrapSlots + kAutoScrapSlots / 2;
    for (int i = 0; i < clogSlots; ++i) {
        ItemRef r{};
        r.def = std::uint16_t(dagger);
        r.count = 1;
        r.seed = std::uint32_t(1 + i);   // distinct seeds: a slot each
        s.add_ref(r);
    }
    // Предусловие фикстуры: не переполнился ларь — утверждение ниже проходит
    // ДАРОМ (чистый ларь «не засорён» всегда), и это ровно та зелёная
    // пустота, против которой написан негативный контроль.
    CHECK_OR_RETURN(s.used_slots() > kAutoScrapSlots,
                    "clog fixture did not overflow (negative control)");
    econ_store_hygiene(s);
    CHECK(s.used_slots() <= kAutoScrapSlots,
          "daily hygiene left the store clogged past 50%");
}

// ── 8. ONE dictionary (owner's ruling): every commodity is an item ──
// The cloth a city weaves and the cloth in the player's bag are the same
// row — a commodity id must resolve in the item catalog. Сверки МАССЫ
// здесь больше нет, и её отсутствие — не ослабление: она сверяла две
// таблицы весов, а вторая (CommodityDef::weightKg) вырезана 2026-09-22.
// Правда о массе теперь ОДНА — ItemDef::massG, сверять её не с чем.
void sect8_one_dictionary_link() {
    for (int i = 0; i < kCommodityCount; ++i) {
        const ItemDef* item = item_def(kCommodities[i].id);
        CHECK_OR_RETURN(item, "commodity id missing from the item catalog");
        CHECK(item->massG > 0, "commodity item has no mass");
        CHECK(item->value > 0, "commodity item has no value");
        ++cov.linkRows;
    }
    // ЯКОРЬ НЕПУСТОТЫ: связь судима у КАЖДОГО ряда словаря, иначе зелёный
    // прогон говорит только о том, что спрашивать было нечего.
    CHECK(cov.linkRows == kCommodityCount && cov.linkRows > 0,
          "связь «товар = строка каталога» судима у всех рядов словаря");
}

// ── 9. Birth stocks: a place is born mid-life (W2a) ──────────────
void sect9_birth_stocks() {
    const int pop = 640;
    Inventory city;
    seed_rooted_inventory(city, pop, squad_birth(SquadType::City));
    CHECK(city.count("food") == pop * kDaysPerSeason,
          "birth larder must hold a SEASON of food — a place "
          "seeded thinner dies of arithmetic at its first "
          "window (S19.2)");
    for (int c = 0; c < kCommodityCount; ++c) {
        if (season_comfort_units(pop, c) <= 0) continue;
        CHECK(city.count_of(commodity_item_index(c)) > 0,
              "a consumed need row was born empty");
        ++cov.bornNeedRows;
    }
    // ЯКОРЬ НЕПУСТОТЫ: у места без ни одной потребляемой строки свип выше
    // проходит вакуумно.
    CHECK(cov.bornNeedRows > 0,
          "негативный контроль: свип видел потребляемые строки рождения");
    Inventory village;
    seed_rooted_inventory(village, pop, squad_birth(SquadType::Village));
    // ДЕЛО ДЕРЕВНИ — СЫРЬЁ, и мерить это надо СЫРЬЁМ (2026-09-20): пища
    // с этого дня не материал, а голодная строка, и её амбар рождения
    // равен сезону у всех — деревня перестала «держать больше еды» не
    // потому, что обеднела, а потому, что еда сменила поток.
    CHECK(village.count("wood") > city.count("wood"),
          "a village's whole business is raw - it holds more");
    CHECK(village.count("cloth") < city.count("cloth"),
          "a crafting city banks deeper crafted stocks");
    Inventory again;
    seed_rooted_inventory(again, pop, squad_birth(SquadType::City));
    CHECK(again.count("food") == city.count("food")
              && again.used_slots() == city.used_slots(),
          "birth stocks must be deterministic from population");
    // КАЗНА ПРИ РОЖДЕНИИ — НОЛЬ, И ЭТО ТЕПЕРЬ ЗАКОН (M-139, вердикт
    // владельца 2026-09-26). Здесь стояли четыре проверки посева казны
    // («в пределах четверти разброса», детерминизм по соли, «разброс
    // разбрасывает») — свидетели ВЫДАЧИ, а не закона мира: они
    // описывали печать монет из воздуха, и вместе с ней ушли. Свидетель
    // перевёрнут в негативный контроль: ни одной монетной строки у
    // новорождённого места быть не может, иначе выдача вернулась.
    CHECK(coin_census_value(city) == 0 && coin_census_value(village) == 0
              && coin_census_value(again) == 0,
          "новорождённое место рождается БЕЗ монет — выдача монет из "
          "воздуха снесена (M-139)");
}

// ── 10. The SUPPLY-CEILING population law (owner, CANON S25 +
//        2026-08-24: «все росты раз в сезон», no carrying cap) ────────
void sect10_supply_ceiling_population() {
    // Well-fed towns grow; nothing caps plenty — supply is the only
    // ceiling (wellbeing already turns growth around when the fields and
    // the trade fall short), and the rate is QUOTED PER SEASON: a month
    // is a season here, and in a town of a thousand souls a birth is an
    // event, not daily noise.
    const float small = population_delta_per_day(100, 1.0f);
    CHECK(small > 0.0f, "a well-fed hamlet must grow");
    // A hamlet gains less than a head a day — the season quote spread
    // over kDaysPerSeason keeps daily change fractional.
    CHECK(small < 1.0f, "a hamlet cannot gain a whole head a day");
    // A fully fed town gains its season quote over one season of days.
    const float aSeason =
        population_delta_per_day(1000, 1.0f) * float(kDaysPerSeason);
    const float quote = 1000.0f * kPopGrowthPerSeason;
    CHECK(aSeason > quote * 0.99f && aSeason < quote * 1.01f,
          "a season of full feeding pays the season quote");
    // No cap: growth scales with heads all the way up (S25 — the town on
    // the crossroads may outgrow the black earth without roads).
    CHECK(population_delta_per_day(20000, 1.0f)
              > population_delta_per_day(1000, 1.0f),
          "no lid: a bigger fed town grows by more heads");
    // УСУШКИ В ЭТОМ ЗАКОНЕ БОЛЬШЕ НЕТ (владелец 2026-09-19): голод
    // убивает на границе долга, а рост просто останавливается — двух
    // кар за один голод не бывает. Негативный контроль: ни при каком
    // населении закон не отдаёт отрицательного числа.
    CHECK(population_delta_per_day(16000, 0.0f) == 0.0f
              && population_delta_per_day(1024, 0.0f) == 0.0f,
          "hunger stops growth, it never shrinks twice");
    // БЛАГОПОЛУЧИЕ И ЕСТЬ ХОД (владелец 2026-09-19): ватерлинии нет,
    // ноль — это «стоим», половина — половина хода, единица — полный.
    CHECK(population_delta_per_day(1000, 0.0f) == 0.0f,
          "zero wellbeing stands still: no drift either way");
    CHECK(population_delta_per_day(1000, 0.5f) * 2.0f
              == population_delta_per_day(1000, 1.0f),
          "growth is LINEAR in wellbeing — half needs, half pace");
}

// ── 11. The economy works the ONE store, and only its own rows ─────
// There is no adapter any more: the second container (a flat Stockpile of
// 14 commodity counts) and the twice-a-day conversion to and from it died
// with the second index space they bridged. What the law still owes is
// the same promise the adapter used to make — a day of economy must not
// disturb what is not the economy's.
void sect11_one_store() {
    Inventory inv;
    inv.add("food", 100);
    inv.add("potion_hp", 3);   // NOT a commodity — must ride untouched
    std::int32_t debt[kCommodityCount] = {};
    // (Здесь же стоял день производства, умерший 2026-11-13 с лестницей,
    //  ряд M-486; на этом складе он был холостым — ни один рецепт не кормится
    //  едой и зельем. Что акт крафта трогает только свои строки — блок 14, А4.)
    boundary_(inv, debt);
    CHECK(inv.count("potion_hp") == 3,
          "a day of economy disturbed what is not a commodity");
    CHECK(inv.count("food") <= 100, "consumption cannot create matter");
}

// ── 12. (СТОЯЛО «ноль рук — ноль выпуска», призрачный станок; умер 2026-11-13
// с квотой рук лестницы дня, ряд M-486: рабочих юнитов у сквада не бывает
// меньше одного — аватар тоже юнит (`work_units@src/bus/world_row.h`), и
// одиночка крафтит той же дверью, что город, — блок 14, А1.) ──

// ── ФИКСТУРА АКТА: ГОЛАЯ СВОЯ СТРОКА СКВАДА ─────────────────────────────
// Четыре колонки слота, которые читает акт (`ActSelf@src/macro/squad_act.h`),
// и больше ничего: мира у акта нет. Тело рождается ТОЙ ЖЕ дверью, что у
// всякого рождения поселения (`refresh_body_from_sheet` над строкой Peasant —
// как `birth_squad@src/macro/birth_squad.h`), бар полон, гейт свободен. Люди
// ложатся в контейнер дверью мира (`raise_flock_into_container`), и рабочих
// юнитов становится 1 + людей — фикстура рожает своё предусловие сама.
struct Actor {
    ecs::Pools pools{};
    ecs::MacroNpcRuntime rt{};
    CharacterSheet sheet{};
    Inventory bag{};
};
Actor born_actor(SquadType type, int souls) {
    Actor a{};
    a.sheet = squad_birth(type).sheet;
    refresh_body_from_sheet(a.pools, &a.rt, a.sheet, NPCType::Peasant);
    a.pools.hp = a.pools.maxHp;
    a.pools.mp = a.pools.maxMp;
    a.pools.sp = a.pools.maxSp;
    if (souls > 0) raise_flock_into_container(a.bag, souls);
    return a;
}

// ПОКОРМИТЬ ЛАРЬ НА `batches` ПАРТИЙ строки `outIdx`: состав читается у самой
// строки каталога (`item_parts`), а не пересказывается числом.
bool feed(Inventory& bag, int outIdx, int batches) {
    bool ok = !item_parts(outIdx).empty();
    for (const ItemPart& p : item_parts(outIdx))
        ok = bag.add_of(int(p.def), batches * int(p.count)) && ok;
    return ok;
}

// СНИМОК СВОЕЙ СТРОКИ АКТА. Все три колонки стоят под `TIMAERT_PACKED`
// (каждый байт принадлежит названному члену), поэтому побайтовое равенство и
// есть «мир не тронут» — без перечня полей, который забыл бы одно.
struct ActSnap {
    ecs::Pools pools;
    ecs::MacroNpcRuntime rt;
    Inventory bag;
};
ActSnap snap(const Actor& a) { return ActSnap{a.pools, a.rt, a.bag}; }
bool untouched(const Actor& a, const ActSnap& s) {
    return std::memcmp(&a.pools, &s.pools, sizeof(ecs::Pools)) == 0
        && std::memcmp(&a.rt, &s.rt, sizeof(ecs::MacroNpcRuntime)) == 0
        && std::memcmp(&a.bag, &s.bag, sizeof(Inventory)) == 0;
}

// ЛАРЬ, НАБИТЫЙ ДО ПОСЛЕДНЕГО СЛОТА: каждый слот, кроме одного, занят своим
// родом (разные семена не складываются). Последний займёт вход фикстуры.
bool fill_all_but_one(Inventory& bag) {
    for (int i = 0; i < kMaxInventorySlots - 1; ++i) {
        ItemRef junk{};
        junk.def = std::uint16_t(item_index("potion_hp"));
        junk.count = 1;
        junk.seed = std::uint32_t(i + 1);
        if (!bag.add_ref(junk)) return false;
    }
    return true;
}

// ── 13. THE MINT is inside the conservation law ────────────────────
// Law №2 at the top of this file is asserted over the commodity ledger,
// and the sink skipped the mint's fact with «no mint in this fixture» —
// so the one recipe that turns a stack into MONEY sat outside the law
// that says nothing vanishes. The metal is what is pinned here, twice,
// because it can be lost two ways: a strike that succeeds must consume
// the metal ONCE, and a strike the shelf refuses must leave it where it
// was (the promise add_ref's own comment makes, and the promise the
// non-mint path already keeps by putting its inputs back).
void sect13_mint_conservation() {
    const ItemDef* metal = item_def_at(item_index("silver"));
    CHECK_OR_RETURN(metal && metal->value > 0,
                    "silver carries no catalog value for the mint to pay");
    // COINS PER UNIT OF METAL is the coin row's own yield column («1
    // металл → 32 монеты своего металла», verdict №1); the metal's price
    // is that yield × the coin's nominal, which mint_is_value_neutral
    // pins at compile time.
    const int coinRow = item_index("coin_empire_silver");
    // `item_def_at` сторожит обе границы (idx < 0 и idx ≥ размера каталога),
    // поэтому ОДИН указатель отвечает и за «строка нашлась», и за право её
    // разыменовать строкой ниже.
    const ItemDef* coinDef = item_def_at(coinRow);
    CHECK_OR_RETURN(coinDef,
                    "the mint's coin row must resolve in the item catalog");
    const int yield = item_yield(coinRow);
    CHECK(yield > 0 && metal->value == yield * coinDef->value,
          "the mint metal's price is not yield x nominal");
    // ЧЕКАНКА — АКТ КРАФТА СКВАДА, как всякий рецепт (ряд M-486): сколько
    // партий, решает модуль, а здесь судится одна механика — что металл и
    // монета сходятся. Ларь — металл и ничего больше, поэтому всякая запись
    // ниже принадлежит двору. Партий — вдвое меньше, чем кормит ларь: остаток
    // и делает ВТОРОЕ списание видимым (высуши стак — лишний remove_of
    // откажет молча и спрячет себя).
    const auto coinParts = item_parts(coinRow);
    CHECK_OR_RETURN(coinParts.size() == 1
                        && int(coinParts[0].def) == item_index("silver")
                        && coinParts[0].count > 0,
                    "предусловие: монета чеканится из одного серебра");
    const int perBatch = int(coinParts[0].count);
    const int batches = 40 / perBatch / 2;
    CHECK_OR_RETURN(batches > 0, "предусловие: ларь кормит хоть одну партию");
    // Крю из `batches − 1` людей — юнитов ровно `batches`, и акт стоит цену
    // одной партии одиночки: чеканка не уводит бар в долг, судится материя.
    Actor city = born_actor(SquadType::City, batches - 1);
    CHECK_OR_RETURN(work_units(city.bag) == batches,
                    "предусловие: люди крю легли в контейнер");
    city.bag.add("silver", 40);
    ActSelf cs{city.pools, city.rt, city.sheet, city.bag};
    CHECK(squad_craft(cs, coinRow, batches) == batches,
          "город с правом и металлом чеканит названные партии");

    const int spent = 40 - city.bag.count("silver");
    const int coins = city.bag.count("coin_empire_silver");
    CHECK(coins > 0, "the mint struck nothing from a store of silver");
    // (ЗДЕСЬ СТОЯЛО «the Minted fact and the coins on the shelf disagree»
    //  — закон О САМОМ КАНАЛЕ, и он умер вместе с ним 2026-11-03 (§8 п.6).
    //  Переводить нечего: ЯКОРЬ МИРА стоит строкой ниже и он сильнее —
    //  он судит монеты против МЕТАЛЛА по авторской колонке `item_yield`,
    //  а не доклад двери против её же собственной полки.)
    const bool metalMatchesCoins =
        spent == batches * perBatch && coins == batches * yield;
    if (!metalMatchesCoins) {
        std::printf("  mint: batches=%d silver spent=%d yield=%d coins struck=%d\n",
                    batches, spent, yield, coins);
    }
    CHECK(metalMatchesCoins,
          "the mint's metal does not match the coins struck");

    // The refused strike. Every slot but one is taken by a distinct kind
    // (distinct seeds do not stack), the last holds more metal than the
    // strike eats — so the debited stack stays occupied, the coin has
    // nowhere to land, and add() refuses.
    Actor full = born_actor(SquadType::City, 0);
    // Один вердикт на всю засыпку, а не 1023: предусловие тут ОДНО — ларь
    // набит, — и записывать его тысячу раз значило бы топить счёт прибора в
    // одной фикстуре.
    CHECK_OR_RETURN(fill_all_but_one(full.bag),
                    "could not fill the fixture's store");
    full.bag.add("silver", 40);
    CHECK_OR_RETURN(full.bag.full() && 40 > batches * perBatch,
                    "the fixture's store is not full, or the strike drains "
                    "the metal's slot");
    ActSelf fs{full.pools, full.rt, full.sheet, full.bag};
    CHECK(squad_craft(fs, coinRow, batches) == 0,
          "a full shelf struck a lot it had no slot for");
    CHECK(full.bag.count("coin_empire_silver") == 0,
          "a full shelf accepted coins it had no slot for");
    const int metalLeft = full.bag.count("silver");
    if (metalLeft != 40) {
        std::printf("  refused mint: silver left=%d of 40\n", metalLeft);
    }
    CHECK(metalLeft == 40,
          "a refused strike ate the metal — the inputs were not put back");
}

// ── ДЕРЕВНЯ УМЕЕТ ВСЁ, КРОМЕ МОНЕТНОГО ДВОРА (владелец, 2026-09-21) ──
// «Деревня производит по той же модели… просто чеканить монеты не может
// — типа крутой рецепт, не хардкод, а потолок по скилам». Связь живёт
// между ДВУМЯ таблицами в разных единицах трансляции — столом анкет
// (characters.h) и каталогом рецептов (econ_day.h), — и компилятору она
// недоступна: отсюда свидетель, а не static_assert.
//
// Пинится ЗАКОН, а не числа: ни одна нечеканная строка не должна быть
// закрыта деревне, и монетная — должна. Поднимется ранг украшений или
// появится новое ремесло — тест скажет, что анкета отстала, вместо того
// чтобы мир молча перестал что-то делать (ровно так и умерла деревенская
// выпечка: рецепт вырезали, а ремесленный ранг остался — и стоял полгода,
// ничего не открывая, пока само ремесло не вырезали вслед, 2026-09-28).
void sect_village_all_but_the_mint() {
    int closedToVillage = 0, mintRows = 0, openToVillage = 0;
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeDef& r = kRecipes[i];
        const bool isMint = std::strcmp(r.output, kMintOutput) == 0;
        // Право рецепта — колонки его Craft-строки (M-369): тот же
        // вопрос, та же дверь, что у акта крафта (`craft_item`).
        const ActionDef& act = action_def(int(r.action));
        const bool village =
            recipe_known(VILLAGE, act.gateSkill, int(act.minRank));
        const bool city =
            recipe_known(CITY, act.gateSkill, int(act.minRank));
        if (isMint) {
            ++mintRows;
            CHECK(!village, "деревня НЕ чеканит — ранг ниже монетного");
            CHECK(city, "город чеканит — иначе монеты нет ни у кого");
            continue;
        }
        if (village) ++openToVillage; else ++closedToVillage;
    }
    CHECK(mintRows > 0, "негативный контроль: монетная строка найдена");
    CHECK(openToVillage > 0,
          "негативный контроль: свип видел открытые деревне строки");
    CHECK(closedToVillage == 0,
          "деревне открыты ВСЕ нечеканные рецепты — разница с городом "
          "выходит из населения, а не из второй стены по виду места");
    cov.mintRows = mintRows;
    cov.openToVillage = openToVillage;
    cov.closedToVillage = closedToVillage;
}

// Здесь печаталось «dictionary=ok famine_transitions=ok … mint_conservation=ok»
// — КОНСТАНТНЫЙ литерал на девять секций, одинаковый при полном прогоне и при
// усохшем свипе. Печатаются ЧИСЛА пройденных рядов: усохни охват — поедет
// строка, и это видно в дифе рядом с правкой.
void print_coverage() {
    std::fprintf(stderr,
                 "[econ_v1] commodities=%d comfort=%d recipes=%d "
                 "recipe_inputs=%d mintless_recipes=%d coin_rows=%d "
                 "link_rows=%d born_need_rows=%d hunger_windows=%d "
                 "mint_rows=%d open_to_village=%d closed_to_village=%d\n",
                 cov.commodityRows, cov.comfortRows, cov.recipeRows,
                 cov.recipeInputs, cov.mintlessRecipes, cov.coinRows,
                 cov.linkRows, cov.bornNeedRows, cov.hungerWindows,
                 cov.mintRows, cov.openToVillage, cov.closedToVillage);
}

} // namespace

// ── 14. АКТ КРАФТА СКВАДА — МЕХАНИКА ОДНА НА ВСЕХ (ряд M-486) ────────────
// Вердикт владельца 2026-11-13 (CANON S25, «СКВАД, МОДУЛЬ И ЕДИНАЯ МЕХАНИКА
// ДЕЙСТВИЯ»): что и сколько крафтить, решает ИИ-модуль сквада; механика акта
// одна — право по анкете, материя «всё или ничего», цена SP строки, делённая
// на рабочих юнитов, и восстановление ровно сделанному («он будет рековери
// ровно столько скольок требовалось за крафт»). Здесь судится МЕХАНИКА на
// голой своей строке (`squad_craft@src/macro/squad_act.h`); ожидания выводятся
// из тех же строк и дверей, что читает код, а не из литералов.
//
// (ЗДЕСЬ СТОЯЛИ 14г «неполный день», 14з «лот дня — не ступень лестницы
//  половин», 14д «руки перетекают» и 14ж «скудные руки делятся»; умерли
//  2026-11-13 вместе с рамкой лестницы дня, ряд M-486: рабочего дня руки, КПД
//  от log2 населения и дележа рук между рецептами за день больше нет — акт
//  есть один рецепт одним объёмом, а объём называет модуль.)
void sect14_squad_craft_act() {
    const int clothIdx = item_index("cloth");
    const int fibreIdx = item_index("fibre");
    const int clothRecipe = recipe_of_item(clothIdx);
    CHECK_OR_RETURN(clothRecipe >= 0 && fibreIdx >= 0,
                    "предусловие: у ткани есть рецепт, у волокна — строка");
    const ActionDef& weave = action_def(int(kRecipes[clothRecipe].action));
    const auto clothParts = item_parts(clothIdx);
    CHECK_OR_RETURN(clothParts.size() == 1
                        && int(clothParts[0].def) == fibreIdx
                        && clothParts[0].count > 0,
                    "предусловие: ткань ткётся из одного волокна");
    const int fibrePerBatch = int(clothParts[0].count);
    const int clothYield = item_yield(clothIdx);
    const int rate = int(weave.spRatePerDay);

    // А1. ЦЕНА ДЕЛИТСЯ НА ЮНИТОВ. Бар у сквада один и от ростера не растёт;
    // растёт то, сколько работы он покупает.
    Actor lone = born_actor(SquadType::City, 0);
    const int maxSp = lone.pools.maxSp;
    const int rowPrice = sp_price(maxSp, rate);
    CHECK_OR_RETURN(work_units(lone.bag) == 1 && rate > 1 && rowPrice >= 2
                        && lone.pools.sp == maxSp
                        && lone.rt.recoverySteps == 0,
                    "предусловие: одиночка — один юнит, бар полон, гейт "
                    "свободен; темп строки больше одной партии в день и цена "
                    "партии не меньше двух (иначе «меньше дня» и «меньше "
                    "одиночки» невыразимы)");
    CHECK_OR_RETURN(feed(lone.bag, clothIdx, 1),
                    "предусловие: волокно на партию легло в ларь");
    ActSelf ls{lone.pools, lone.rt, lone.sheet, lone.bag};
    CHECK(squad_craft(ls, clothIdx, 1) == 1,
          "одиночка с правом и волокном ткёт партию");
    const int loneCost = maxSp - lone.pools.sp;
    CHECK(loneCost == rowPrice,
          "ОДИНОЧКА ПЛАТИТ ЦЕНУ СТРОКИ: партия стоит бар, делённый на темп");

    constexpr int kCrew = 7;
    Actor crew = born_actor(SquadType::City, kCrew);
    const int crewUnits = work_units(crew.bag);
    CHECK_OR_RETURN(crewUnits == kCrew + 1 && crew.pools.maxSp == maxSp,
                    "предусловие: люди крю легли в контейнер — юнитов 1 + "
                    "людей; бар тот же, что у одиночки");
    CHECK_OR_RETURN(feed(crew.bag, clothIdx, crewUnits),
                    "предусловие: волокно на N+1 партию легло в ларь");
    ActSelf cs{crew.pools, crew.rt, crew.sheet, crew.bag};
    CHECK(squad_craft(cs, clothIdx, crewUnits) == crewUnits,
          "сквад с крю ткёт N+1 партию");
    const int crewCost = maxSp - crew.pools.sp;
    CHECK(crewCost == rowPrice,
          "N+1 ЮНИТ ДЕЛАЕТ N+1 ПАРТИЮ ПО ЦЕНЕ ОДНОЙ: бар падает ровно на цену "
          "строки");

    Actor crewOne = born_actor(SquadType::City, kCrew);
    CHECK_OR_RETURN(feed(crewOne.bag, clothIdx, 1),
                    "предусловие: волокно на партию легло в ларь");
    ActSelf c1{crewOne.pools, crewOne.rt, crewOne.sheet, crewOne.bag};
    CHECK(squad_craft(c1, clothIdx, 1) == 1, "сквад с крю ткёт одну партию");
    const int crewOneCost = maxSp - crewOne.pools.sp;
    CHECK(crewOneCost > 0 && crewOneCost < loneCost,
          "ПАРТИЯ МНОГОЛЮДНОГО ДЕШЕВЛЕ, НО НЕ ДАРОВАЯ: платит не ноль и "
          "меньше одиночки");
    std::printf("  [цена акта] бар %d, темп %d: одиночка 1 партия −%d, крю %d "
                "юнитов %d партий −%d, крю 1 партия −%d\n",
                maxSp, rate, loneCost, crewUnits, crewUnits, crewCost,
                crewOneCost);

    // А2. ВОССТАНОВЛЕНИЕ РАВНО СДЕЛАННОМУ: эффективное время одной партии
    // (база строки через ту же дверь `recovery_steps`) × объём ÷ юниты.
    const int oneBatchRec = recovery_steps(
        weave.recoveryTicks, lone.sheet.attributes, lone.sheet.skills,
        action_tempo_generic(weave));
    // А2а. Одна партия одиночки — время одной партии, и это доля дня.
    CHECK(oneBatchRec > 0 && lone.rt.recoverySteps == std::uint32_t(oneBatchRec),
          "ВОССТАНОВЛЕНИЕ РАВНО СДЕЛАННОМУ: одна партия висит временем одной "
          "партии строки");
    CHECK(std::uint64_t(lone.rt.recoverySteps) < kTicksPerDay,
          "…и это доля рабочего дня, а не весь день: темп строки больше одной "
          "партии");
    // А2б. Пропорция объёму: k партий одиночки висят в k раз дольше (юнит
    // один — делить нечего, округлению негде встать).
    constexpr int kBatches = 3;
    Actor loneK = born_actor(SquadType::City, 0);
    CHECK_OR_RETURN(feed(loneK.bag, clothIdx, kBatches),
                    "предусловие: волокно на k партий легло в ларь");
    ActSelf lk{loneK.pools, loneK.rt, loneK.sheet, loneK.bag};
    CHECK(squad_craft(lk, clothIdx, kBatches) == kBatches,
          "одиночка ткёт k партий");
    CHECK(loneK.rt.recoverySteps == std::uint32_t(kBatches * oneBatchRec),
          "ПРОПОРЦИЯ ОБЪЁМУ: k партий одиночки висят ровно в k раз дольше "
          "одной");
    // А2в. Деление на юнитов: N+1 партия на N+1 юнита — время одной партии.
    CHECK(crew.rt.recoverySteps == lone.rt.recoverySteps,
          "ДЕЛЕНИЕ НА ЮНИТОВ: N+1 юнит висит за N+1 партию столько же, "
          "сколько одиночка за одну");
    // А2г. Человеко-день: полный бар одиночки покупает темп строки партий, и
    // эта работа занимает рабочий день — с точностью до остатка деления дня
    // на темп. Утверждение о НЕЙТРАЛЬНОМ работнике: резвость и генерик
    // домена базу не правят — это предусловие, а не допуск.
    const std::int64_t dayLot = act_sp_reach(maxSp, rate, maxSp, 1);
    CHECK_OR_RETURN(dayLot == rate
                        && oneBatchRec == int(weave.recoveryTicks),
                    "предусловие: полный бар одиночки покупает ровно темп "
                    "строки, а лист нейтрален к базе восстановления");
    Actor loneDay = born_actor(SquadType::City, 0);
    CHECK_OR_RETURN(feed(loneDay.bag, clothIdx, int(dayLot)),
                    "предусловие: волокно на человеко-день легло в ларь");
    ActSelf ld{loneDay.pools, loneDay.rt, loneDay.sheet, loneDay.bag};
    CHECK(squad_craft(ld, clothIdx, int(dayLot)) == int(dayLot),
          "одиночка ткёт человеко-день");
    const std::uint64_t dayRec = loneDay.rt.recoverySteps;
    CHECK(dayRec <= kTicksPerDay
              && kTicksPerDay - dayRec < std::uint64_t(rate),
          "ЧЕЛОВЕКО-ДЕНЬ: полный бар работы висит рабочим днём (до остатка "
          "деления дня на темп)");
    std::printf("  [восстановление] партия %d тиков, %d партий %u, крю %d "
                "партий %u, человеко-день (%lld партий) %llu из %llu\n",
                oneBatchRec, kBatches, unsigned(loneK.rt.recoverySteps),
                crewUnits, unsigned(crew.rt.recoverySteps),
                (long long)dayLot, (unsigned long long)dayRec,
                (unsigned long long)kTicksPerDay);

    // А3. ОТКАЗ — МИР НЕ ТРОНУТ: возврат 0, и бар, гейт занятости и
    // инвентарь побайтово те же. Три причины, у каждой — противоположная
    // полярность, что отказала именно она.
    {   // (1) материала не хватает одной единицы.
        Actor a = born_actor(SquadType::City, 0);
        CHECK_OR_RETURN(a.bag.add_of(fibreIdx, fibrePerBatch - 1)
                            && a.bag.count_of(fibreIdx) == fibrePerBatch - 1,
                        "предусловие: волокна на единицу меньше партии");
        const ActSnap before = snap(a);
        ActSelf s{a.pools, a.rt, a.sheet, a.bag};
        CHECK(squad_craft(s, clothIdx, 1) == 0 && untouched(a, before),
              "ОТКАЗ ПО МАТЕРИАЛУ: партий ноль, бар, гейт и ларь не тронуты");
        CHECK(a.bag.add_of(fibreIdx, 1) && squad_craft(s, clothIdx, 1) == 1,
              "…а с последней единицей волокна та же партия ткётся");
    }
    {   // (2) навык ниже минранга строки: деревня и монетный двор.
        const int coinRow = item_index("coin_empire_silver");
        const int mintRecipe = recipe_of_item(coinRow);
        CHECK_OR_RETURN(mintRecipe >= 0,
                        "предусловие: у монеты есть рецепт — монетный двор");
        const ActionDef& mint = action_def(int(kRecipes[mintRecipe].action));
        Actor v = born_actor(SquadType::Village, 0);
        CHECK_OR_RETURN(!recipe_known(v.sheet.skills, mint.gateSkill,
                                      int(mint.minRank))
                            && feed(v.bag, coinRow, 1),
                        "предусловие: металл на партию есть, а ранг деревни "
                        "ниже монетной строки");
        const ActSnap before = snap(v);
        ActSelf s{v.pools, v.rt, v.sheet, v.bag};
        CHECK(squad_craft(s, coinRow, 1) == 0 && untouched(v, before),
              "ОТКАЗ ПО ПРАВУ: деревня не чеканит, бар, гейт и ларь не "
              "тронуты");
        Actor c = born_actor(SquadType::City, 0);
        CHECK_OR_RETURN(feed(c.bag, coinRow, 1),
                        "предусловие: металл на партию легло в ларь города");
        ActSelf cc{c.pools, c.rt, c.sheet, c.bag};
        CHECK(squad_craft(cc, coinRow, 1) == 1,
              "…а тот же металл у города чеканится: отказал ранг, а не ларь");
    }
    {   // (3) контейнер полон: кирпичу лечь некуда.
        const int bricksIdx = item_index("bricks");
        const auto brickParts = item_parts(bricksIdx);
        Actor a = born_actor(SquadType::City, 0);
        CHECK_OR_RETURN(fill_all_but_one(a.bag),
                        "could not fill the fixture's store");
        a.bag.add("clay", 40);
        CHECK_OR_RETURN(a.bag.full() && recipe_of_item(bricksIdx) >= 0
                            && brickParts.size() == 1
                            && int(brickParts[0].def) == item_index("clay")
                            && 40 > int(brickParts[0].count),
                        "предусловие: ларь набит, глина кормит партию и её "
                        "слот после партии не освобождается");
        const ActSnap before = snap(a);
        ActSelf s{a.pools, a.rt, a.sheet, a.bag};
        CHECK(squad_craft(s, bricksIdx, 1) == 0 && untouched(a, before),
              "ОТКАЗ ПОЛНОГО КОНТЕЙНЕРА: кирпичу лечь некуда — партий ноль, "
              "глина, бар и гейт не тронуты");
    }

    // А4. МАТЕРИЯ: входы уходят ровно партии × счёт каждой части, выход
    // приходит ровно партии × выход строки, чужая строка не тронута. Рецепт из
    // ДВУХ частей — чтобы «каждая часть» было вопросом, а не одной строкой.
    {
        int outIdx = -1;
        for (int r = 0; r < kRecipeCount && outIdx < 0; ++r) {
            if (std::strcmp(kRecipes[r].output, kMintOutput) == 0) continue;
            const ActionDef& act = action_def(int(kRecipes[r].action));
            const int idx = item_index(kRecipes[r].output);
            if (idx >= 0 && item_parts(idx).size() >= 2
                && recipe_known(CITY, act.gateSkill, int(act.minRank)))
                outIdx = idx;
        }
        const int foodIdx = hunger_item_index();
        CHECK_OR_RETURN(outIdx >= 0 && foodIdx >= 0
                            && item_parts(foodIdx).empty(),
                        "предусловие: есть рецепт из двух частей, открытый "
                        "городу, и терминальная чужая строка — еда");
        constexpr int kLot = 3;
        Actor a = born_actor(SquadType::City, kLot - 1);
        CHECK_OR_RETURN(feed(a.bag, outIdx, kLot + 2)
                            && a.bag.add_of(foodIdx, 100),
                        "предусловие: состав на лот с остатком и чужая строка "
                        "легли в ларь");
        const auto parts = item_parts(outIdx);
        int partBefore[kMaxItemParts] = {};
        for (std::size_t k = 0; k < parts.size(); ++k)
            partBefore[k] = a.bag.count_of(int(parts[k].def));
        const int outBefore = a.bag.count_of(outIdx);
        const int soulsBefore = count_human_souls(a.bag);
        ActSelf s{a.pools, a.rt, a.sheet, a.bag};
        CHECK(squad_craft(s, outIdx, kLot) == kLot,
              "город крафтит лот из двух частей");
        bool partsExact = true;
        for (std::size_t k = 0; k < parts.size(); ++k)
            partsExact = partsExact
                && partBefore[k] - a.bag.count_of(int(parts[k].def))
                       == kLot * int(parts[k].count);
        CHECK(partsExact,
              "МАТЕРИЯ: каждая часть ушла ровно партии × свой счёт");
        CHECK(a.bag.count_of(outIdx) - outBefore == kLot * item_yield(outIdx),
              "МАТЕРИЯ: выход пришёл ровно партии × выход строки");
        CHECK(a.bag.count_of(foodIdx) == 100
                  && count_human_souls(a.bag) == soulsBefore,
              "акт трогает только свои строки: еда и люди не тронуты");
    }

    // А5. В ДОЛГ ДВЕРЬ НЕ ОТКАЗЫВАЕТ: отказа по усталости у механики нет —
    // трудиться ли в долг, решает модуль. Пустой бар уходит в минус ровно на
    // цену строки (укус HP — закон двери траты, не этой).
    {
        Actor a = born_actor(SquadType::City, 0);
        a.pools.sp = 0;
        CHECK_OR_RETURN(a.pools.maxSp == maxSp && feed(a.bag, clothIdx, 1),
                        "предусловие: бар пуст, тот же потолок, волокно на "
                        "партию есть");
        ActSelf s{a.pools, a.rt, a.sheet, a.bag};
        CHECK(squad_craft(s, clothIdx, 1) == 1
                  && a.bag.count_of(clothIdx) == clothYield,
              "В ДОЛГ ДВЕРЬ НЕ ОТКАЗЫВАЕТ: при пустом баре партия соткана");
        CHECK(a.pools.sp == -rowPrice,
              "…и бар ушёл в минус ровно на цену строки");
    }

    // А6. СТРАЖ ШИРИНЫ ДВЕРИ МАТЕРИИ: лот, чей выход не ложится в `int`,
    // отказан, и ларь не тронут. Без стража произведение переполнялось,
    // отрицательный приход принимался за «нечего добавить», и металл исчезал
    // (ревью 2026-11-13). Монета — самый широкий выход каталога.
    {
        const int coinRow = item_index("coin_empire_silver");
        const int recipe = recipe_of_item(coinRow);
        const auto parts = item_parts(coinRow);
        const int yield = item_yield(coinRow);
        CHECK_OR_RETURN(recipe >= 0 && yield > 1 && parts.size() == 1
                            && parts[0].count > 0,
                        "предусловие: монета — один металл и выход больше "
                        "одной штуки");
        const std::uint8_t mintAct = kRecipes[recipe].action;
        const ActionDef& mint = action_def(int(mintAct));
        constexpr int kIntMax = std::numeric_limits<int>::max();
        const int wide = kIntMax / yield + 1;   // наименьший лот не по ширине
        CHECK_OR_RETURN(wide <= kIntMax / int(parts[0].count)
                            && recipe_known(CITY, mint.gateSkill,
                                            int(mint.minRank)),
                        "предусловие: вход лота в int ложится и право есть — "
                        "отказать может только страж выхода");
        const int metal = wide * int(parts[0].count);
        Inventory bag{};
        CHECK_OR_RETURN(bag.add_of(int(parts[0].def), metal)
                            && bag.count_of(int(parts[0].def)) == metal,
                        "предусловие: стак держит металл на весь лот");
        Inventory one = bag;
        CHECK(craft_item(one, coinRow, 1, CITY, mintAct)
                  && one.count_of(coinRow) == yield,
              "…партия из того же ларя чеканится: отказывает ширина, а не "
              "ларь");
        const Inventory before = bag;
        CHECK(!craft_item(bag, coinRow, wide, CITY, mintAct),
              "СТРАЖ ШИРИНЫ: лот, чей выход не ложится в int, отказан");
        CHECK(std::memcmp(&bag, &before, sizeof(Inventory)) == 0,
              "…и ларь не тронут: металл не исчез");
        std::printf("  [страж ширины] выход %d, отказан лот %d партий из %d "
                    "металла\n", yield, wide, metal);
    }
}

// ── 14Б. ЧТО И СКОЛЬКО КРАФТИТЬ — РЕШЕНИЯ МОДУЛЕЙ ДЕРЕВНИ И ГОРОДА (M-486) ──
// Закон мира (AGENTS «ЗАКОН ЕДИНОЙ ЦЕНОМОДЕЛИ» п.2 и п.4; вердикт владельца
// 2026-11-13: крафт — сделка с самим собой). Выгода лота есть оценка
// пришедшего выхода минус оценка ушедших входов, той же оценкой запаса, какой
// меряет сделка; предел наступает обратной связью — выход копится, масса
// против спины роняет его оценку; вход тает — его оценка растёт. Решение
// судится на голых числах своей строки, и у деревни и у города оно своё
// (`village_craft_pick@src/macro/squad_ai/village.cpp`,
// `city_craft_pick@src/macro/squad_ai/city.cpp`; AGENTS §4 п.5), поэтому каждое
// утверждение ниже стоит над обоими; выгода всякого другого объёма — теми же
// дверями ядра цены.
namespace {

// СТОРОНА СТРОКИ ГЛАЗАМИ СВОЕГО ЛАРЯ — мост, который читает и решение
// (`row_side@src/macro/economy.h`): стоимость и масса — колонки строки.
RowSide own_side(const Inventory& inv, int idx, const std::int32_t* debt,
                 std::int64_t backG) {
    const ItemDef* d = item_def_at(idx);
    return row_side(idx, d ? d->value : 0, debt, inv,
                    d ? std::int64_t(item_mass_g(*d)) : 1, backG);
}
// ВЫГОДА ЛОТА `b` ПАРТИЙ строки `outIdx` от нынешнего ларя.
std::int64_t lot_gain(const Inventory& inv, int outIdx, std::int64_t b,
                      const std::int32_t* debt, std::int64_t backG) {
    std::int64_t spent = 0;
    for (const ItemPart& p : item_parts(outIdx))
        spent += lot_loss(own_side(inv, int(p.def), debt, backG),
                          b * int(p.count));
    return lot_take(own_side(inv, outIdx, debt, backG),
                    b * item_yield(outIdx)) - spent;
}
// ПРЕДЕЛ МАТЕРИАЛОВ — сколько партий кормит ларь.
std::int64_t material_lots(const Inventory& inv, int outIdx) {
    std::int64_t most = std::numeric_limits<std::int64_t>::max();
    for (const ItemPart& p : item_parts(outIdx))
        most = std::min<std::int64_t>(most,
                                      inv.count_of(int(p.def)) / int(p.count));
    return item_parts(outIdx).empty() ? 0 : most;
}
// ОЦЕНКА СВОИХ СТРОК ЛАРЯ — сумма оценок запаса (`stock_worth`), цель сквада
// «растить свою оценку своего инвентаря» буквально.
std::int64_t rows_worth(const Inventory& inv, const int* rows, int n,
                        const std::int32_t* debt, std::int64_t backG) {
    std::int64_t w = 0;
    for (int k = 0; k < n; ++k) {
        const RowSide r = own_side(inv, rows[k], debt, backG);
        w += stock_worth(r, r.net);
    }
    return w;
}

// Спина фикстуры — тонна: против неё масса запаса роняет его оценку.
constexpr std::int64_t kFixtureBackG = 1000000;
// Спина малая — пятьдесят килограммов: завал наступает на сотне партий.
constexpr std::int64_t kSmallBackG = 50000;
// Спина без края: завала нет вовсе, выгода партии постоянна.
constexpr std::int64_t kBoundlessBackG = std::int64_t(1) << 40;
// Юнитов столько, что бар заведомо не предел (свидетель утверждает это сам).
constexpr int kManyUnits = 256;

// РЕШЕНИЕ МОДУЛЯ, КОТОРОЕ СУДИТ СЕКЦИЯ: у деревни своё, у города своё, и
// набор проверок один на оба.
using CraftPickFn = CraftPick (*)(const Inventory&, const std::int32_t*,
                                  const Skills&, std::int64_t, int, int, int,
                                  int);
struct ModulePick {
    CraftPickFn pick;
    const char* who;
};
constexpr ModulePick kModulePicks[] = {
    {&village_craft_pick, "деревня"},
    {&city_craft_pick, "город"},
};

} // namespace

void sect14b_module_craft_pick(const ModulePick& m) {
    std::printf("  [решение модуля: %s]\n", m.who);
    const CraftPickFn pick = m.pick;
    const int clothIdx = item_index("cloth");
    const int fibreIdx = item_index("fibre");
    const int bricksIdx = item_index("bricks");
    const int clothRecipe = recipe_of_item(clothIdx);
    CHECK_OR_RETURN(clothRecipe >= 0 && recipe_of_item(bricksIdx) >= 0
                        && fibreIdx >= 0,
                    "предусловие: у ткани и кирпича есть рецепты");
    const ActionDef& weave = action_def(int(kRecipes[clothRecipe].action));
    const int rate = int(weave.spRatePerDay);
    const int maxSp = born_actor(SquadType::City, 0).pools.maxSp;
    CHECK_OR_RETURN(rate > 0 && maxSp > 0,
                    "предусловие: у строки ткани есть темп, у тела — бар");

    // Б1. ВЫБОР ПО ВЫГОДЕ: волокно на одну ткань и глина на один кирпич;
    // ткань растит оценку больше — берётся ткань.
    {
        Inventory s{};
        s.add("fibre", 2);
        s.add("clay", 1);
        const std::int64_t gCloth = lot_gain(s, clothIdx, 1, nullptr, kFixtureBackG);
        const std::int64_t gBricks = lot_gain(s, bricksIdx, 1, nullptr, kFixtureBackG);
        CHECK_OR_RETURN(gCloth > gBricks && gBricks > 0
                            && material_lots(s, clothIdx) == 1
                            && material_lots(s, bricksIdx) == 1,
                        "предусловие: обе партии растят оценку, ткань — "
                        "больше; ларь кормит по одной");
        const CraftPick p = pick(
            s, nullptr, CITY, kFixtureBackG, -1, maxSp, maxSp, 1);
        CHECK(p.outIdx == clothIdx && p.batches == 1 && p.gain == gCloth,
              "ВЫБОР ПО ВЫГОДЕ: из двух возможных крафтов взят тот, что "
              "растит оценку больше, и выгода названа честно");
        // ПРОТИВОПОЛОЖНАЯ ПОЛЯРНОСТЬ: тот же ларь, но счёт на кирпич —
        // нужда поднимает оценку его выхода.
        std::int32_t debt[kCommodityCount] = {};
        debt[commodity_index("bricks")] = 9;
        CHECK_OR_RETURN(lot_gain(s, bricksIdx, 1, debt, kFixtureBackG)
                            > lot_gain(s, clothIdx, 1, debt, kFixtureBackG),
                        "предусловие: со счётом на кирпич его партия "
                        "выгоднее ткани");
        const CraftPick n = pick(
            s, debt, CITY, kFixtureBackG, -1, maxSp, maxSp, 1);
        CHECK(n.outIdx == bricksIdx,
              "НУЖДА ПОДНИМАЕТ ОЦЕНКУ ВЫХОДА: со счётом на кирпич выбран "
              "кирпич");
    }

    // Б2. НАСЫЩЕНИЕ: волокна завал, спина мала, бар заведомо не предел. Лот
    // меньше предела материалов, и ни один другой объём того же рецепта
    // оценку не растит больше (полный перебор дверями ядра).
    {
        Inventory s{};
        s.add("fibre", 400);
        const std::int64_t limit = material_lots(s, clothIdx);
        CHECK_OR_RETURN(limit > 1
                            && act_sp_reach(maxSp, rate, maxSp, kManyUnits) >= limit
                            && lot_gain(s, clothIdx, 1, nullptr, kSmallBackG) > 0,
                        "предусловие: первая партия растит оценку, а бар "
                        "покупает весь предел материалов");
        std::int64_t best = 0, bestGain = 0;
        for (std::int64_t b = 1; b <= limit; ++b) {
            const std::int64_t g = lot_gain(s, clothIdx, b, nullptr, kSmallBackG);
            if (g > bestGain) { bestGain = g; best = b; }
        }
        const CraftPick p = pick(
            s, nullptr, CITY, kSmallBackG, -1, maxSp, maxSp, kManyUnits);
        CHECK(p.outIdx == clothIdx && p.batches > 0 && p.batches < limit,
              "НАСЫЩЕНИЕ: лот меньше предела материалов — предел дала спина, "
              "а не полка");
        CHECK(p.gain == lot_gain(s, clothIdx, p.batches, nullptr, kSmallBackG)
                  && p.gain >= bestGain,
              "ЛОТ ЛУЧШИЙ: ни один другой объём того же рецепта оценку не "
              "растит больше");
        std::printf("  [насыщение] предел %lld партий, лучший перебором %lld "
                    "(выгода %lld), выбрано %d (выгода %lld)\n",
                    (long long)limit, (long long)best, (long long)bestGain,
                    p.batches, (long long)p.gain);
    }

    // Б3. ВХОД ПОД СЧЁТОМ НЕ ДОЕДАЕТСЯ: на волокно счёт, волокна вдвое больше
    // счёта. Каждая партия сверх излишка ест волокно, которого стороне уже не
    // хватает, и оно дорожает быстрее, чем прибавляет ткань.
    {
        std::int32_t debt[kCommodityCount] = {};
        debt[commodity_index("fibre")] = 30;
        Inventory s{};
        s.add("fibre", 60);
        const std::int64_t limit = material_lots(s, clothIdx);
        std::int64_t best = 0, bestGain = 0;
        for (std::int64_t b = 1; b <= limit; ++b) {
            const std::int64_t g = lot_gain(s, clothIdx, b, debt, kFixtureBackG);
            if (g > bestGain) { bestGain = g; best = b; }
        }
        CHECK_OR_RETURN(best > 0 && best < limit
                            && lot_gain(s, clothIdx, limit, debt, kFixtureBackG) < bestGain
                            && act_sp_reach(maxSp, rate, maxSp, kManyUnits) >= limit,
                        "предусловие: лучший лот есть, он меньше всего "
                        "волокна, и бар не предел");
        const CraftPick p = pick(
            s, debt, CITY, kFixtureBackG, -1, maxSp, maxSp, kManyUnits);
        CHECK(p.outIdx == clothIdx && p.batches > 0 && p.batches < limit
                  && p.gain >= bestGain,
              "ВХОД ПОД СЧЁТОМ НЕ ДОЕДАЕТСЯ: выбран лучший лот, волокно "
              "осталось");
        std::printf("  [вход под счётом] лучший лот %lld (выгода %lld), выбрано "
                    "%d (выгода %lld) из %lld\n",
                    (long long)best, (long long)bestGain, p.batches,
                    (long long)p.gain, (long long)limit);
    }

    // Б4. ЧЕКАНКА — ВЫГОДА ТОЛЬКО В ТОМ, ЧТО МОНЕТА ЛЕГЧЕ РУДЫ. Монета стоит
    // ровно свой металл; при спине без края выгода всей руды меньше кванта, и
    // руда монетой не становится. При тесной спине — становится.
    const int empire = faction_index("empire");
    const int coinRow = item_index("coin_empire_silver");
    const std::array<int, 3>& mintRows = faction_mint_rows(empire);
    const int mintRecipe = recipe_of_item(coinRow);
    CHECK_OR_RETURN(mintRecipe >= 0
                        && (mintRows[0] == coinRow || mintRows[1] == coinRow
                            || mintRows[2] == coinRow),
                    "предусловие: серебряная крона — номинал монетного двора "
                    "империи");
    const ActionDef& mint = action_def(int(kRecipes[mintRecipe].action));
    {
        Inventory s{};
        s.add("silver", 40);
        CHECK_OR_RETURN(recipe_known(CITY, mint.gateSkill, int(mint.minRank))
                            && material_lots(s, coinRow) > 0
                            && act_sp_reach(maxSp, int(mint.spRatePerDay),
                                            maxSp, kManyUnits)
                                   >= material_lots(s, coinRow),
                        "предусловие: у города есть право, металл и бар — "
                        "отказ ниже есть решение, а не неумение");
        const CraftPick loose = pick(
            s, nullptr, CITY, kBoundlessBackG, empire, maxSp, maxSp, kManyUnits);
        CHECK(loose.outIdx != coinRow,
              "РУДА БЕЗ ЗАВАЛА НЕ ЧЕКАНИТСЯ: монета стоит ровно свой металл, и "
              "выгода чеканки меньше кванта");
        const CraftPick tight = pick(
            s, nullptr, CITY, kFixtureBackG, empire, maxSp, maxSp, kManyUnits);
        CHECK(tight.outIdx == coinRow && tight.batches > 0 && tight.gain > 0,
              "ПРИ ТЕСНОЙ СПИНЕ РУДА ЧЕКАНИТСЯ: монета легче руды");
        // Б7. ПРАВО: тот же металл и та же спина у деревни — чеканки нет,
        // потому что ранг ниже монетной строки (противоположная полярность —
        // город строкой выше).
        CHECK_OR_RETURN(!recipe_known(VILLAGE, mint.gateSkill, int(mint.minRank)),
                        "предусловие: кузнечный ранг деревни ниже монетной "
                        "строки");
        const CraftPick village = pick(
            s, nullptr, VILLAGE, kFixtureBackG, empire, maxSp, maxSp, kManyUnits);
        CHECK(village.outIdx != coinRow,
              "ПРАВО: рецепт выше ранга не выбирается, хотя сырьё есть");
    }

    // Б5. БАР ОГРАНИЧИВАЕТ ОБЪЁМ: спина без края, выгода растёт с каждой
    // партией, а бар покупает две. Пустой бар — выбирать нечего.
    {
        Inventory s{};
        s.add("fibre", 400);
        const int sp = 2 * sp_price(maxSp, rate) + 1;
        const std::int64_t reach = act_sp_reach(maxSp, rate, sp, 1);
        CHECK_OR_RETURN(reach >= 1 && reach < material_lots(s, clothIdx)
                            && lot_gain(s, clothIdx, reach + 1, nullptr, kBoundlessBackG)
                                   > lot_gain(s, clothIdx, reach, nullptr, kBoundlessBackG),
                        "предусловие: без бара модуль взял бы больше — "
                        "материалы и выгода дальше бара есть");
        const CraftPick p = pick(
            s, nullptr, CITY, kBoundlessBackG, -1, maxSp, sp, 1);
        CHECK(p.outIdx == clothIdx && p.batches > 0 && p.batches <= reach,
              "БАР ОГРАНИЧИВАЕТ ОБЪЁМ: не больше, чем покупает нынешний бар");
        const CraftPick spent = pick(
            s, nullptr, CITY, kBoundlessBackG, -1, maxSp, 0, 1);
        const CraftPick owed = pick(
            s, nullptr, CITY, kBoundlessBackG, -1, maxSp, -sp, kManyUnits);
        CHECK(spent.outIdx == -1 && owed.outIdx == -1,
              "ПУСТОЙ БАР — ВЫБИРАТЬ НЕЧЕГО: в долг поселение не трудится");
    }

    // Б6. ЮНИТЫ РАСШИРЯЮТ ОБЪЁМ: тот же бар и тот же ларь, юнитов больше —
    // бар покупает больше, и модуль берёт больше.
    {
        Inventory s{};
        s.add("fibre", 400);
        constexpr int kFew = 1, kMore = 8;
        const std::int64_t reachFew = act_sp_reach(maxSp, rate, maxSp, kFew);
        const std::int64_t reachMore = act_sp_reach(maxSp, rate, maxSp, kMore);
        const CraftPick few = pick(
            s, nullptr, CITY, kBoundlessBackG, -1, maxSp, maxSp, kFew);
        CHECK_OR_RETURN(reachFew < reachMore
                            && reachMore < material_lots(s, clothIdx)
                            && few.outIdx == clothIdx
                            && few.batches == reachFew,
                        "предусловие: объём одиночки упёрся именно в бар");
        const CraftPick more = pick(
            s, nullptr, CITY, kBoundlessBackG, -1, maxSp, maxSp, kMore);
        CHECK(more.outIdx == clothIdx && more.batches > few.batches
                  && more.batches <= reachMore,
              "ЮНИТЫ РАСШИРЯЮТ ОБЪЁМ: тот же бар у многолюдного покупает и "
              "берёт больше");
        std::printf("  [бар и юниты] бар %d: %d юнит — %lld партий, %d юнитов "
                    "— %lld (выбрано %d и %d)\n",
                    maxSp, kFew, (long long)reachFew, kMore,
                    (long long)reachMore, few.batches, more.batches);
    }

    // Б8. ПУСТОЙ ИНВЕНТАРЬ — КРАФТИТЬ НЕЧЕГО.
    {
        const Inventory empty{};
        CHECK(pick(empty, nullptr, CITY, kFixtureBackG, empire,
                   maxSp, maxSp, kManyUnits).outIdx == -1,
              "пустой ларь — выбирать нечего");
    }
}

// ── 14В. РЕШЕНИЕ → АКТ: модуль назвал лот, механика его исполнила ─────────
// Оценка своих строк выросла ровно на выгоду решения, бар упал на цену акта,
// сквад висит восстановлением ровно сделанному.
void sect14c_pick_then_act(const ModulePick& m) {
    std::printf("  [решение модуля: %s]\n", m.who);
    const CraftPickFn pick = m.pick;
    constexpr int kCrew = 7;
    Actor a = born_actor(SquadType::City, kCrew);
    a.bag.add("fibre", 400);
    const int units = work_units(a.bag);
    CHECK_OR_RETURN(units == kCrew + 1 && a.rt.recoverySteps == 0,
                    "предусловие: крю в контейнере, гейт свободен");
    const CraftPick p = pick(
        a.bag, nullptr, a.sheet.skills, kSmallBackG, -1, a.pools.maxSp,
        a.pools.sp, units);
    CHECK_OR_RETURN(p.outIdx >= 0 && p.batches > 0 && p.gain > 0,
                    "предусловие: модуль назвал лот, растящий оценку");
    const int recipe = recipe_of_item(p.outIdx);
    CHECK_OR_RETURN(recipe >= 0, "предусловие: у выхода есть рецепт");
    const ActionDef& act = action_def(int(kRecipes[recipe].action));
    int rows[1 + kMaxItemParts] = {p.outIdx};
    int rowCount = 1;
    for (const ItemPart& part : item_parts(p.outIdx)) rows[rowCount++] = int(part.def);
    const std::int64_t worthBefore =
        rows_worth(a.bag, rows, rowCount, nullptr, kSmallBackG);
    const int spBefore = a.pools.sp;
    ActSelf s{a.pools, a.rt, a.sheet, a.bag};
    CHECK(squad_craft(s, p.outIdx, p.batches) == p.batches,
          "АКТ ИСПОЛНИЛ РЕШЕНИЕ: названные партии сделаны");
    const std::int64_t worthAfter =
        rows_worth(a.bag, rows, rowCount, nullptr, kSmallBackG);
    CHECK(worthAfter - worthBefore == p.gain,
          "ОЦЕНКА ИНВЕНТАРЯ ВЫРОСЛА РОВНО НА ВЫГОДУ РЕШЕНИЯ");
    CHECK(std::int64_t(spBefore - a.pools.sp)
              == act_sp_price(a.pools.maxSp, act.spRatePerDay, p.batches, units),
          "БАР УПАЛ НА ЦЕНУ АКТА: цена строки на объём, делённая на юнитов");
    CHECK(squad_recovering(a.rt)
              && a.rt.recoverySteps == squad_act_recovery(s, act, p.batches),
          "СКВАД В ВОССТАНОВЛЕНИИ РОВНО СДЕЛАННОМУ");
    std::printf("  [решение → акт] %d партий строки %d: оценка %lld → %lld "
                "(выгода %lld), бар %d → %d, восстановление %u\n",
                p.batches, p.outIdx, (long long)worthBefore,
                (long long)worthAfter, (long long)p.gain, spBefore,
                a.pools.sp, unsigned(a.rt.recoverySteps));
}


int main() {
    sect1_dictionary_commodities();
    sect1_dictionary_recipes();
    sect1_dictionary_comfort_rows();
    sect_hunger_proportional();
    sect4_boundary_shortfall();
    sect5_half_season_starves_half();
    sect7_no_matter_from_nothing();
    sect7b_overflow_rides_the_day();
    sect8_one_dictionary_link();
    sect9_birth_stocks();
    sect10_supply_ceiling_population();
    sect11_one_store();
    sect13_mint_conservation();
    sect14_squad_craft_act();
    for (const ModulePick& m : kModulePicks) sect14b_module_craft_pick(m);
    for (const ModulePick& m : kModulePicks) sect14c_pick_then_act(m);
    sect_village_all_but_the_mint();
    print_coverage();
    // Границы у этого прибора НЕТ, и это не упущение: `check.h` даёт форму без
    // неё поведенческому свидетелю прямо («он судит ПОВЕДЕНИЕ, а не охват
    // дерева, и приписывать ему „границу“ значило бы выдумывать число»), и так
    // же кончаются соседи — `movement_steering_test`,
    // `feature_layer_parity_test`. Охват этого файла печатает строка выше —
    // числами, которые считали сами секции.
    return sm::test::report("econ_v1_test");
}
