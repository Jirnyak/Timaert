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
#include "tables/squad_type.h"  // SquadType — ось рода (амбар рождения уехал
                                // в kSquadBirth@src/macro/characters.h)
#include "macro/upkeep_window.h"  // upkeep_season_window — ОДНА дверь суда границы (M-233)
#include "bus/world_row.h"   // raise_flock_into_container / count_human_souls
#include "bus/anketa.h"

#include <array>
#include <cstdio>
#include <cstring>

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

// ── 6. Produce: the first recipe may not hog the town ───────────────
// Mountains of food beside a little clay: before the fix the table's first
// row staffed ceil(stock/8) workers — the whole town — and bricks never saw a
// single worker-day. Output DIVERSITY is the law: with inputs for both,
// both are made.
void sect6_produce_fair_shares() {
    Inventory s{};
    s.remove_of(commodity_item_index(commodity_index("food")),
            s.count_of(commodity_item_index(commodity_index("food"))));
    s.add_of(commodity_item_index(commodity_index("food")), 1024);
    s.remove_of(commodity_item_index(commodity_index("clay")),
                  s.count_of(commodity_item_index(commodity_index("clay"))));
    s.add_of(commodity_item_index(commodity_index("clay")), 64);
    // population 0: no demand pass — pure fair shares, the exact surface
    // the old hog bug lived on.
    const int made =
        econ_produce_day(s, nullptr, CITY, 8, 0);
    CHECK(made > 0, "city with inputs and workers made nothing");
    CHECK(s.count_of(commodity_item_index(commodity_index("bricks"))) > 0,
          "first recipe hogged every worker - no output diversity");
    CHECK(s.count_of(commodity_item_index(commodity_index("food"))) > 0,
          "fair shares must not starve the FIRST recipe either");
}

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
    // Якорь спрашивается ДВЕРЬЮ ГОЛОДА, а не колонкой умершей лестницы:
    // сезон харча 32 душ, делённый на дни сезона, и есть дневная добыча.
    CHECK(season_hunger_units(32) / kDaysPerSeason == kGatherPerWorkerDay
              && season_hunger_units(1) == kDaysPerSeason,
          "the gatherer-day must feed exactly 32 souls for a day");
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
    econ_produce_day(inv, debt, VILLAGE, /*workers*/4,
                     /*population*/40);
    boundary_(inv, debt);
    CHECK(inv.count("potion_hp") == 3,
          "a day of economy disturbed what is not a commodity");
    CHECK(inv.count("food") <= 100, "consumption cannot create matter");
}

// ── 12. Zero workers craft nothing — the ghost-bench half of the
// honest-death law (owner, 2026-08-29; the population side lives in
// world_tick_parity_test, which links settle_rooted_day).
void sect12_ghost_bench() {
    Inventory ghost;
    ghost.add("food", 100);
    CHECK(econ_produce_day(ghost, nullptr, CITY, /*workers*/0,
                           /*population*/0) == 0,
          "zero workers produced something");
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
    // A store of metal and nothing else: no other City recipe can be fed,
    // so every write below belongs to the mint. Two workers on a hamlet's
    // population strike far less than the store holds — the leftover is
    // what makes a SECOND debit visible at all (drain the stack dry and
    // the extra remove_of fails silently and hides itself).
    Inventory store;
    store.add("silver", 40);
    econ_produce_day(store, nullptr, CITY, /*workers*/2,
                     /*population*/2, faction_index("empire"));

    const int spent = 40 - store.count("silver");
    const int coins = store.count("coin_empire_silver");
    CHECK(coins > 0, "the mint struck nothing from a store of silver");
    // (ЗДЕСЬ СТОЯЛО «the Minted fact and the coins on the shelf disagree»
    //  — закон О САМОМ КАНАЛЕ, и он умер вместе с ним 2026-11-03 (§8 п.6).
    //  Переводить нечего: ЯКОРЬ МИРА стоит строкой ниже и он сильнее —
    //  он судит монеты против МЕТАЛЛА по авторской колонке `item_yield`,
    //  а не доклад двери против её же собственной полки.)
    const bool metalMatchesCoins = spent * yield == coins;
    if (!metalMatchesCoins) {
        std::printf("  mint: silver spent=%d yield=%d coins struck=%d\n",
                    spent, yield, coins);
    }
    CHECK(metalMatchesCoins,
          "the mint's metal does not match the coins struck");

    // The refused strike. Every slot but one is taken by a distinct kind
    // (distinct seeds do not stack), the last holds more metal than one
    // day can strike — so the debited stack stays occupied, the coin has
    // nowhere to land, and add() refuses.
    Inventory full;
    bool fixtureFilled = true;
    for (int i = 0; i < kMaxInventorySlots - 1; ++i) {
        ItemRef junk{};
        junk.def = std::uint16_t(item_index("potion_hp"));
        junk.count = 1;
        junk.seed = std::uint32_t(i + 1);
        if (!full.add_ref(junk)) { fixtureFilled = false; break; }
    }
    // Один вердикт на всю засыпку, а не 1023: предусловие тут ОДНО — ларь
    // набит, — и записывать его тысячу раз значило бы топить счёт прибора в
    // одной фикстуре.
    CHECK_OR_RETURN(fixtureFilled, "could not fill the fixture's store");
    full.add("silver", 40);
    CHECK_OR_RETURN(full.full(), "the fixture's store is not full");
    econ_produce_day(full, nullptr, CITY, /*workers*/2,
                     /*population*/2,
                     faction_index("empire"));
    CHECK(full.count("coin_empire_silver") == 0,
          "a full shelf accepted coins it had no slot for");
    const int metalLeft = full.count("silver");
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
        // вопрос, та же дверь, что у дня производства.
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

int main() {
    sect1_dictionary_commodities();
    sect1_dictionary_recipes();
    sect1_dictionary_comfort_rows();
    sect_hunger_proportional();
    sect4_boundary_shortfall();
    sect5_half_season_starves_half();
    sect6_produce_fair_shares();
    sect7_no_matter_from_nothing();
    sect7b_overflow_rides_the_day();
    sect8_one_dictionary_link();
    sect9_birth_stocks();
    sect10_supply_ceiling_population();
    sect11_one_store();
    sect12_ghost_bench();
    sect13_mint_conservation();
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
