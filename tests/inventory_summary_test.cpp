// СВОДКА КОНТЕЙНЕРА ТОЖДЕСТВЕННА ПОЛНОМУ ПЕРЕСЧЁТУ — ПОСЛЕ КАЖДОЙ ОПЕРАЦИИ
// (наряд M-296 ломоть А, 2026-10-20; закрывает M-267).
//
// ЧТО ЗДЕСЬ ОХРАНЯЕТСЯ — ЗАКОН, А НЕ ПОВЕДЕНИЕ (§8 п.5). Сводка есть восемь
// сумм, которые правит дверь записи за O(1) вместо прохода по 1024 слотам
// капа; её единственное обязательство — быть РАВНОЙ тому, что дал бы честный
// проход. Нарушь это — и мир получит кеш, который разъехался молча: вес
// сквада, стоимость сделки, число ртов и спины обоза станут враньём, и ни
// один сегодняшний прибор этого не увидит, потому что все они спрашивают
// ровно сводку.
//
// ПОЧЕМУ РАВЕНСТВО ТОЧНОЕ, А НЕ ЭПСИЛОННОЕ. Сводка целая, и единица её —
// ГРАММ (вердикт владельца 2026-10-20: «можно тогда все веса в граммах»;
// довод его же — СГС, и он назвал его честно: «это для людей преимущество
// больше а не машинное»). Машинного преимущества у грамма перед сотой
// килограмма и правда нет — оба точны для каталога (все 14 его весов кратны
// 0.01 кг), оба влезают в `int64` с запасом, — но грамм даёт в сто раз
// тоньше разрешение будущей строке (стрела в 20 г, зерно в 1 г) и не врёт
// именем, как врал мой «сантиграмм»: `× 100` давало СОТУЮ КИЛОГРАММА, то
// есть 10 граммов, и имя разошлось с арифметикой на три порядка. Будь она в `float`, инкремент отличался бы от пересчёта
// ПОРЯДКОМ сложения — три веса каталога (0.01, 0.05, 0.30) в двоичном float
// непредставимы, — и эпсилон пришлось бы выбирать на глаз, то есть ослеплять
// прибор ровно в том, что он сторожит. Это и проверяет раздел 6.
//
// И ОДНОГО ТОЖДЕСТВА МАЛО — ЭТО ДОКАЗАНО МУТАЦИЕЙ, А НЕ ПРЕДПОЛОЖЕНО.
// Сверка слепа к неверной `slot_tally`: обе её стороны считает ОДНА формула,
// поэтому ошибка в ней сходится сама с собой. Поймать её может только
// ЯКОРЬ — независимое утверждение о мире, — и первый же якорь нашёл живой
// дефект: страж пустоты спрашивал `def == 0`, а ординал 0 есть МЕДЯК, так
// что сто монет весили и стоили НОЛЬ. Поэтому у каждой из восьми колонок
// здесь свой якорь (разделы 2b, 6, 7), и таблица ниже проверяет КАЖДЫЙ.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (§8 п.6), прогнана 2026-10-20, обе полярности.
// ОБЯЗАНЫ КРАСНИТЬ (код возврата 1):
//   • снять `sum_ += slot_tally(cell, -count)` в `Container::write`
//       → 26 из 46 сверок разъехались, ctest КРАСНЫЙ (ловит ТОЖДЕСТВО)
//   • `folkHeads = n` безусловно вместо вопроса природе строки
//       → раздел 2b КРАСНЫЙ (ловит ЯКОРЬ; тождество молчало — 0 расхождений)
//   • `boardPerDay` занулить  → раздел 2b КРАСНЫЙ, тождество молчит
//   • `wagePerDay` занулить   → раздел 2b КРАСНЫЙ, тождество молчит
//   • `valueSouls` занулить   → раздел 2b КРАСНЫЙ, тождество молчит
//   • `massG` без `lround`, сразу в `double` → раздел 6 КРАСНЫЙ
// ОБЯЗАНЫ МОЛЧАТЬ (детектор, краснеющий на всё, — не детектор):
//   • переставить местами строки в `operator+=`            → ЗЕЛЁНЫЙ
//   • снять ранний выход `folkHeads == 0` в `bleed_flock`   → ЗЕЛЁНЫЙ
//       (он про цену прохода, а не про сводку)
#include "check.h"

#include <memory>
#include "macro/deal.h"
#include "macro/store.h"

#include "macro/anketa.h"
#include "macro/currency.h"
#include "macro/upkeep_window.h"
#include "macro/world_row.h"

#include <cstdio>

namespace {

using namespace sm;

int gSamples = 0;
int gDrift   = 0;

// ОДНА ТОЧКА СВЕРКИ. Зовётся после КАЖДОЙ операции мира над контейнером:
// закон говорит «всегда», значит и спрашивать надо всегда, а не в конце.
void settled(const Inventory& inv, const char* what) {
    ++gSamples;
    const InventorySummary truth = inventory_sum_recount(inv);
    if (truth == inv.sum()) return;
    ++gDrift;
    std::printf(
        "  РАСХОЖДЕНИЕ после «%s»: масса %lld против %lld, предметы %lld/%lld, "
        "души %lld/%lld, голов %lld/%lld, людей %lld/%lld, харч %lld/%lld, "
        "плата %lld/%lld, спины %lld/%lld\n",
        what,
        (long long)inv.sum().massG,      (long long)truth.massG,
        (long long)inv.sum().valueItems,  (long long)truth.valueItems,
        (long long)inv.sum().valueSouls,  (long long)truth.valueSouls,
        (long long)inv.sum().heads,       (long long)truth.heads,
        (long long)inv.sum().folkHeads,   (long long)truth.folkHeads,
        (long long)inv.sum().boardPerDay, (long long)truth.boardPerDay,
        (long long)inv.sum().wagePerDay,  (long long)truth.wagePerDay,
        (long long)inv.sum().haulG,      (long long)truth.haulG);
}

ItemRef item(const char* id, int n, std::uint32_t seed = 0u) {
    ItemRef r{};
    r.def   = std::uint16_t(item_index(id));
    r.count = n;
    r.seed  = seed;
    return r;
}

// ── 0. САМ ДЕТЕКТОР ВИДИТ РАСХОЖДЕНИЕ (§8 п.6: контроль утверждается) ─────
void test_the_detector_can_see_a_single_gram() {
    Inventory bag{};
    CHECK(bag.add_ref(item("wood", 3)), "фикстура наполняется");
    const InventorySummary truth = inventory_sum_recount(bag);
    CHECK(truth == bag.sum(), "целый контейнер сходится сам с собой");
    InventorySummary bent = truth;
    bent.massG += 1;
    CHECK(!(bent == bag.sum()),
          "детектор видит РАСХОЖДЕНИЕ В ОДИН ГРАММ — без этого "
          "равенство ниже доказывало бы только, что оператор == возвращает "
          "true");
    bent = truth;
    bent.folkHeads += 1;
    CHECK(!(bent == bag.sum()), "…и расхождение в одну душу тоже");
}

// ── 1. ПРЕДМЕТНАЯ ПОЛОВИНА: все двери прихода и убыли ────────────────────
void test_item_doors_keep_the_summary() {
    Inventory bag{};
    settled(bag, "пустой контейнер");

    CHECK(bag.add_ref(item("wood", 5)), "пять дерева");
    settled(bag, "add_ref: новый слот");
    CHECK(bag.add_ref(item("wood", 7)), "ещё семь — в ТОТ ЖЕ стак");
    settled(bag, "add_ref: слияние стака");
    CHECK(bag.add_of(item_index("iron"), 4), "add_of по ординалу");
    settled(bag, "add_of");
    CHECK(bag.add("food", 2), "add по авторскому имени");
    settled(bag, "add(id)");

    // Прокатанный экземпляр не сливается с голым — у него своя цена, и
    // сводка обязана это видеть (поэтому семя здесь не декорация).
    CHECK(bag.add_ref(item("wpn_dagger", 1, 77u)), "прокатанный кинжал");
    settled(bag, "add_ref: несливаемый экземпляр");

    CHECK(bag.remove_at(0, 3), "частичное снятие со стака");
    settled(bag, "remove_at: стак похудел");
    CHECK(bag.remove_of(item_index("iron"), 4), "remove_of подчистую");
    settled(bag, "remove_of: слот умер");
    CHECK(bag.remove("food", 1), "remove(id) частично");
    settled(bag, "remove(id)");

    bag.clear();
    settled(bag, "clear");
    CHECK(bag.sum().massG == 0 && bag.sum().valueItems == 0,
          "обнулённый контейнер весит ноль и стоит ноль");
}

// ── 2. ОБЛАСТЬ СУЩЕСТВ: плотность чинится — сводка не замечает ───────────
// Снятие головы из СЕРЕДИНЫ области двигает чужой слот (swap-with-newest), то
// есть одна операция пишет ТРИ слота. Ровно здесь инкремент и ломался бы.
void test_creature_area_repair_keeps_the_summary() {
    Inventory bag{};
    CHECK(creatures_push_stack(bag, NPCType::Peasant, 1, 6), "шесть крестьян");
    settled(bag, "существа: первый стак");
    CHECK(creatures_push_stack(bag, NPCType::Horse, 1, 3), "три лошади");
    settled(bag, "существа: второй стак");
    CHECK(creatures_push_stack(bag, NPCType::Wolf, 2, 2), "два волка");
    settled(bag, "существа: третий стак");

    const int heads = creature_count(bag);
    CHECK(heads == 11, "одиннадцать голов по счёту сводки");

    // Снятие СТАКА ЦЕЛИКОМ из середины — тот самый ремонт плотности.
    const int mid = bag.creature_first() + 1;
    const int midCount = bag.slots[std::size_t(mid)].count;
    CHECK(bag.remove_at(mid, midCount), "средний стак уходит целиком");
    settled(bag, "remove_at: ремонт плотности области");

    SoldierRecord out{};
    CHECK(creatures_pop_back(bag, out), "ходок уходит по закону свежести");
    settled(bag, "creatures_pop_back");

    const int bled = bleed_flock(bag, 2);
    CHECK(bled >= 0, "дверь паствы отвечает фактом");
    settled(bag, "bleed_flock");
    CHECK(bleed_creatures(bag, 1) >= 0, "дверь голов отвечает фактом");
    settled(bag, "bleed_creatures");

    const int killed = creatures_kill_all(bag);
    CHECK(killed >= 0, "все гибнут одной дверью");
    settled(bag, "creatures_kill_all");
    CHECK(creature_count(bag) == 0 && count_human_souls(bag) == 0,
          "после смерти области голов и душ ноль");
    CHECK(upkeep_bill(bag).board == 0 && upkeep_bill(bag).wage == 0,
          "…и счёт содержания тоже ноль: кормить некого");
}

// ── 2b. ЯКОРЯ КОЛОНОК СУЩЕСТВ — ТО, ЧЕГО ТОЖДЕСТВО НЕ ВИДИТ ──────────────
// Сверка сводки с пересчётом СЛЕПА к неверной `slot_tally`: обе её стороны
// считает одна формула. Доказано мутацией — `folkHeads = n` безусловно
// прошла тождество ЗЕЛЁНОЙ. Значит у каждой колонки обязан быть ЯКОРЬ:
// независимое утверждение о мире. Утверждаются ОТНОШЕНИЯ, не пересчитанные
// числа (§8 п.4).
void test_each_creature_column_has_an_anchor() {
    Inventory bag{};

    // АРХЕТИП: зверь есть голова, но не душа. Это ступень ЛЕСТНИЦЫ
    // (`npc_archetype`, M-295), а не счёт, — и ровно это мутация
    // «folkHeads = n» и ломала.
    CHECK(creatures_push_stack(bag, NPCType::Wolf, 1, 4), "четыре волка");
    settled(bag, "якоря: звери");
    CHECK(bag.sum().heads == 4 && bag.sum().folkHeads == 0,
          "ЗВЕРЬ — ГОЛОВА, НО НЕ ДУША: головы четыре, людских душ ноль");

    // ХАРЧ: едят ВСЕ, кто в контейнере, по колонке своей строки (ЗАКОН
    // КОНТЕЙНЕРА п.4, вердикт «никаких особенностей лошадей»). Поэтому
    // четыре волка УЖЕ дают счёт, и это якорь рациона.
    CHECK(bag.sum().boardPerDay > 0,
          "ХАРЧ СЧИТАЕТСЯ И ЗВЕРЮ: ветки «а лошадь не ест» нет");
    const std::int64_t beastBoard = bag.sum().boardPerDay;

    CHECK(creatures_push_stack(bag, NPCType::Peasant, 1, 3), "три крестьянина");
    settled(bag, "якоря: и люди");
    CHECK(bag.sum().heads == 7 && bag.sum().folkHeads == 3,
          "…а человек есть и голова, и душа: 7 голов при 3 душах");
    CHECK(bag.sum().boardPerDay > beastBoard, "и рты сложились, а не заместились");

    // ПЛАТА: колонка строки, а не счёт голов. У крестьянина и волка она
    // НОЛЬ, у стражи — нет; значит ноль здесь — авторское высказывание,
    // и якорь проверяет обе его стороны.
    CHECK(bag.sum().wagePerDay == 0,
          "ПЛАТА ЕСТЬ КОЛОНКА СТРОКИ: ни зверю, ни крестьянину не платят");
    CHECK(creatures_push_stack(bag, NPCType::Guard, 2, 2), "двое стражей");
    settled(bag, "якоря: наёмные");
    const std::int64_t twoGuards = bag.sum().wagePerDay;
    CHECK(twoGuards > 0, "…а наёмной строке платят");
    CHECK(creatures_push_stack(bag, NPCType::Guard, 2, 2), "ещё двое тех же");
    settled(bag, "якоря: вдвое больше наёмных");
    CHECK(bag.sum().wagePerDay == 2 * twoGuards, "и плата ЛИНЕЙНА по головам");

    // СТОИМОСТЬ ДУШ — вторая половина `container_worth`, и платёжной она не
    // становится: контейнер из одних существ платить не может ничем.
    CHECK(bag.sum().valueItems == 0 && bag.sum().valueSouls > 0,
          "души СТОЯТ, но платить ими нельзя — две колонки, два вопроса");
    CHECK(container_worth(bag) > inventory_value(bag),
          "…и разница двух дверей — ровно они");
}

// ── 3. ПЕРЕСАДКА ОБЛАСТИ — ДВА КОНТЕЙНЕРА СРАЗУ ──────────────────────────
// (Секция «КОПИЯ области» умерла с creatures_add в ломоть 2 M-72: копия
// МИНТИТ головы и дверью мира не является; прод-звонящих у неё не было.)
void test_moving_between_containers() {
    auto store = make_macro_store();
    Inventory& a = store->inventory[1].inv;
    Inventory& b = store->inventory[2].inv;
    CHECK(creatures_push_stack(a, NPCType::Peasant, 1, 4), "паства истока");
    CHECK(creatures_push_stack(a, NPCType::Horse, 1, 2), "табун истока");
    CHECK(a.add_ref(item("wood", 9)), "и дерево в предметной половине");
    settled(a, "исток собран");

    DealLine heads[2];
    heads[0] = DealLine{};
    heads[0].want = 1 << 20;
    heads[0].def = world_row_of_creature(NPCType::Peasant);
    heads[1] = DealLine{};
    heads[1].want = 1 << 20;
    heads[1].def = world_row_of_creature(NPCType::Horse);
    const DealFact mf = deal(*store, 1, 2, heads, {});
    CHECK(mf.settled && mf.unitsAB == 6, "пересадка увела все шесть");
    settled(a, "deal: исток опустел");
    settled(b, "deal: приёмник полон");
    CHECK(creature_count(a) == 0 && creature_count(b) == 6,
          "головы не размножились и не испарились");
    CHECK(a.count_of(item_index("wood")) == 9,
          "предметы переносом существ не тронуты");
}

// ── 4. СДЕЛКА, КРАФТ И СКРАП — ОТКАТ НА КОПИИ СТРУКТУРЫ ──────────────────
// Эти три двери работают на КОПИИ контейнера и присваивают её целиком. Сводка
// едет внутри структуры, поэтому согласована по построению — но проверить это
// надо, а не пообещать: именно здесь форма «сводка рядом, а не внутри» и
// разъехалась бы.
void test_copy_and_commit_doors() {
    Inventory bag{};
    CHECK(bag.add_of(item_index("wood"), 12), "дерево на верстак");
    CHECK(bag.add_of(item_index("iron"), 6), "и железо");
    settled(bag, "верстак накрыт");

    const int wagon = item_index("wagon");
    CHECK(wagon >= 0, "каталог знает повозку");
    CHECK(craft_item(bag, wagon, 1), "повозка собирается");
    settled(bag, "craft_item: приход и расход одной копией");

    int wagonSlot = -1;
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (bag.slots[std::size_t(i)].def == std::uint16_t(wagon)) {
            wagonSlot = i;
            break;
        }
    }
    CHECK(wagonSlot >= 0, "собранная повозка лежит в сумке");
    CHECK(scrap_at(bag, wagonSlot, 1), "и разбирается обратно");
    settled(bag, "scrap_at");

    // Сделка идёт дверью deal по СЛОТАМ стора (ломоть 2 M-72).
    auto store = make_macro_store();
    Inventory& seller = store->inventory[1].inv;
    Inventory& buyer = store->inventory[2].inv;
    CHECK(seller.add_ref(item("wood", 10)), "товар продавца");
    CHECK(buyer.add_of(item_index("coin_empire_silver"), 40), "монета купца");
    const auto exact_line = [](const Inventory& inv, int slot, int n) {
        DealLine l{};
        l.want = n;
        l.def = inv.slots[std::size_t(slot)].def;
        l.slot = std::uint16_t(slot);
        l.exact = 1;
        return l;
    };
    DealLine give[] = {exact_line(seller, 0, 4)};
    DealLine take[] = {exact_line(buyer, 0, 12)};
    CHECK(deal(*store, 1, 2, give, take).settled, "сделка проходит целиком");
    settled(seller, "deal: продавец");
    settled(buyer, "deal: покупатель");

    // Отказанная сделка обязана оставить ОБЕ стороны ровно как были — и
    // сводку тоже.
    const InventorySummary before = seller.sum();
    DealLine absurd[] = {exact_line(seller, 0, 999999)};
    DealLine take2[] = {exact_line(buyer, 0, 12)};
    CHECK(!deal(*store, 1, 2, absurd, take2).settled,
          "непокрытая exact-сделка отказывает");
    settled(seller, "deal: откат");
    CHECK(before == seller.sum(), "отказ не сдвинул сводку ни на единицу");
}

// ── 5. ЭКИПИРОВКА: РАСЩЕПЛЕНИЕ И ОТКАТ — СОСТАВ НЕ МЕНЯЕТСЯ ──────────────
void test_equip_split_is_summary_neutral() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kHumanoidSlots);
    CHECK(bag.add_ref(item("wpn_dagger", 3)), "стак из трёх кинжалов");
    const InventorySummary before = bag.sum();
    settled(bag, "стак до расщепления");

    const int cell = equip(g, bag, 0);
    CHECK(cell >= 0, "один надевается, расщепившись в свой слот");
    settled(bag, "equip: расщепление");
    CHECK(before == bag.sum(),
          "РАСЩЕПЛЕНИЕ НЕ МЕНЯЕТ СВОДКУ: единица переехала, а не родилась");

    CHECK(unequip(g, bag, cell), "снимается");
    settled(bag, "unequip");
    CHECK(before == bag.sum(), "…и снятие тоже её не двигает");

    // Прямой откат: расщепить и слить обратно руками.
    const int one = bag.split_one(0);
    CHECK(one >= 0, "расщепление дверью");
    settled(bag, "split_one");
    CHECK(bag.merge_one(one, 0), "слияние обратно");
    settled(bag, "merge_one");
    CHECK(before == bag.sum(), "круг расщепление→слияние замкнулся В НОЛЬ");
}

// ── 6. ТОЧНОСТЬ — ТО, РАДИ ЧЕГО СВОДКА ЦЕЛАЯ ─────────────────────────────
// Сто монет по 0.01 кг, положенных и снятых ПОШТУЧНО, обязаны оставить
// РОВНО ноль. Во float этот круг оставляет остаток, потому что 0.01
// непредставима, и именно он копился бы в весе каждого сквада мира.
void test_mass_is_exact_over_a_round_trip() {
    Inventory bag{};
    const int coin = item_index("coin_empire_copper");
    CHECK(coin >= 0, "каталог знает медяк");
    bool allIn = true;
    for (int i = 0; i < 100; ++i) allIn = bag.add_of(coin, 1) && allIn;
    CHECK(allIn, "сто монет легли поштучно, каждая своей дверью");
    settled(bag, "сто монет поштучно");
    CHECK(bag.sum().massG == 1000,
          "сто монет по 0.01 кг весят РОВНО 1000 граммов");
    // ЯКОРЬ ВТОРОЙ КОЛОНКИ, И ОН ЗДЕСЬ НЕ ДЛЯ СИММЕТРИИ. Тождество сводки
    // с пересчётом слепо к НЕВЕРНОЙ `slot_tally` — обе его стороны считает
    // одна формула. Поймать её может только независимый якорь, и один такой
    // уже сработал: медяк есть строка НОЛЬ каталога, а страж пустоты
    // спрашивал `def == 0`, то есть сто монет весили и стоили ноль.
    CHECK(bag.sum().valueItems == 100 * value_of(bag.slots[0]),
          "…и стоят ровно сто своих номиналов: строка 0 каталога — медяк, "
          "а не «строки нет»");
    bool allOut = true;
    for (int i = 0; i < 100; ++i) allOut = bag.remove_of(coin, 1) && allOut;
    CHECK(allOut, "и ушли поштучно, каждая своей дверью");
    settled(bag, "и сняты поштучно");
    CHECK(bag.sum().massG == 0,
          "круг замкнулся В НОЛЬ: остатка нет, потому что величина целая");
    CHECK(inventory_mass_g(bag) == 0, "и дверь массы отвечает тем же нулём");
}

// ── 7. ВКЛАД ОБЪЕКТА ЛИНЕЕН И ЧИТАЕТ АВТОРСКИЙ НОЛЬ (M-297) ──────────────
// Утверждается ОТНОШЕНИЕ, не пересчитанное число (§8 п.4): безрукая голова
// не растит обоз вовсе, а каждая следующая упряжная растит его на одну и ту
// же величину. Копия продакшен-формулы здесь была бы проверкой умения
// копировать.
void test_hauling_is_linear_and_reads_the_authored_zero() {
    Inventory bag{};
    CHECK(creatures_push_stack(bag, NPCType::Wolf, 1, 5), "пять волков");
    settled(bag, "безрукие головы");
    CHECK(bag.sum().haulG == 0,
          "БЕЗРУКАЯ ГОЛОВА НЕ РАСТИТ ОБОЗ: авторский ноль колонки значит ноль");
    CHECK(bag.sum().heads == 5, "…хотя головами они считаются");

    CHECK(creatures_push_stack(bag, NPCType::Horse, 1, 1), "одна лошадь");
    settled(bag, "первая упряжная");
    const std::int64_t one = bag.sum().haulG;
    CHECK(one > 0, "упряжная голова обоз растит");
    CHECK(creatures_push_stack(bag, NPCType::Horse, 1, 2), "ещё две");
    settled(bag, "три упряжных");
    CHECK(bag.sum().haulG == 3 * one,
          "вклад ЛИНЕЕН по головам — ни множителя от анкеты, ни фоллбэка");

    CHECK(bag.remove_at(bag.creature_first(), 3), "упряжные уходят");
    settled(bag, "табун снят");
    CHECK(bag.sum().haulG == 0, "и обоз возвращается ровно к нулю");
}

// ── 8. ДВЕРЬ АДРЕСА (путь загрузки сейва) СВОДИТ КОНТЕЙНЕР САМА ──────────
// Читатель сейва кладёт стаки ПО АДРЕСАМ, мимо закона стакования: позиция
// там носитель закона ходока. Значит пересобранный послотно контейнер обязан
// иметь ТУ ЖЕ сводку, что оригинал, — иначе загрузка рождала бы мир с
// враньём в каждом кармане.
void test_the_address_door_rebuilds_the_same_summary() {
    Inventory origin{};
    CHECK(origin.add_ref(item("wood", 5)), "предметы");
    CHECK(origin.add_ref(item("wpn_dagger", 1, 42u)), "прокатанный экземпляр");
    CHECK(creatures_push_stack(origin, NPCType::Peasant, 2, 7), "паства");
    CHECK(creatures_push_stack(origin, NPCType::Horse, 1, 2), "табун");
    settled(origin, "оригинал собран");

    Inventory loaded{};
    int  placed = 0;
    bool allPlaced = true;
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& s = origin.slots[std::size_t(i)];
        if (s.empty()) continue;
        allPlaced = loaded.put_at(i, s) && allPlaced;
        ++placed;
    }
    CHECK(placed > 0 && allPlaced,
          "стаки оригинала легли по своим адресам, и их было не ноль");
    settled(loaded, "пересобран послотно");
    CHECK(origin.sum() == loaded.sum(),
          "ПОСЛОТНАЯ ПЕРЕСБОРКА ДАЁТ ТУ ЖЕ СВОДКУ — поэтому сейв её не "
          "возит: производное выводится, а не хранится");
    CHECK(!loaded.put_at(0, item("wood", 1)),
          "…и занятый слот дверь отказывает ГРОМКО: это условие читатель "
          "сейва проверял руками, теперь оно живёт в двери");
}

} // namespace

int main() {
    test_the_detector_can_see_a_single_gram();
    test_item_doors_keep_the_summary();
    test_creature_area_repair_keeps_the_summary();
    test_each_creature_column_has_an_anchor();
    test_moving_between_containers();
    test_copy_and_commit_doors();
    test_equip_split_is_summary_neutral();
    test_mass_is_exact_over_a_round_trip();
    test_hauling_is_linear_and_reads_the_authored_zero();
    test_the_address_door_rebuilds_the_same_summary();

    std::printf("inventory_summary: сверок после операций %d, расхождений %d\n",
                gSamples, gDrift);
    CHECK(gSamples > 0 && gDrift == 0,
          "СВОДКА ТОЖДЕСТВЕННА ПОЛНОМУ ПЕРЕСЧЁТУ ПОСЛЕ КАЖДОЙ ОПЕРАЦИИ — и "
          "сверок было не ноль (цикл, который ничего не померил, обязан "
          "проваливаться, §8 п.2)");
    return sm::test::report("inventory_summary_test");
}
