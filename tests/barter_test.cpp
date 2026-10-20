// The package deal (owner ruling 2026-08-07; PER-STACK lines, owner verdict
// 2026-09-07 «торговля пер-стак»): trade is barter by PACKAGE — both sides
// staged, ONE settlement, all-or-nothing. Lines are keyed by SLOT, so a
// rolled instance and its bare twin are two lines with two prices. Pinned:
//   · barter_swap moves BOTH packages or neither, validated against the
//     PRE-DEAL bags — a short shelf on either side moves nothing;
//   · repeated lines of one SLOT sum for validation (no double-spend of one
//     stack across two lines);
//   · the same ware may travel both ways (food for food is a legal deal);
//   · coin is not special: a currency row swaps like any ware, and item
//     totals CONSERVE across every deal — nothing is minted;
//   · a staged ROLLED stack travels AS ITSELF (seed + affixes), never as
//     «a sword by id» — the Wear-bug class, now impossible by key shape;
//   · a slot re-filled with a DIFFERENT row refuses the stale line (def
//     rides the BarterLine as the sanity byte).
#include "check.h"

#include "macro/currency.h"
#include "macro/world_row.h"   // creature_slot / creatures_push_stack — вторая половина

namespace {

using namespace sm;

// The staging panels stand on the slot they drew; a test finds it the same
// honest way — by looking.
int slot_of(const Inventory& inv, const char* id) {
    const int def = item_index(id);
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (!inv.slots[std::size_t(i)].empty()
            && inv.slots[std::size_t(i)].def == std::uint16_t(def)) {
            return i;
        }
    }
    return -1;
}

BarterLine line(const Inventory& inv, const char* id, int n) {
    const int s = slot_of(inv, id);
    return BarterLine{s, n, std::uint16_t(item_index(id))};
}

void test_swap_moves_both_packages() {
    Inventory town, player;
    town.add("food", 10);
    town.add("cloth", 4);
    player.add("wood", 6);
    player.add("coin_empire_copper", 30);

    CHECK(barter_swap(player, town,
                      /*fromPlayer*/ {line(player, "wood", 5),
                                      line(player, "coin_empire_copper", 7)},
                      /*fromTown*/ {line(town, "food", 3),
                                    line(town, "cloth", 1)}),
          "a covered deal settles");
    CHECK(player.count("wood") == 1 && player.count("coin_empire_copper") == 23,
          "the given lines left the player");
    CHECK(player.count("food") == 3 && player.count("cloth") == 1,
          "the taken lines arrived");
    CHECK(town.count("food") == 7 && town.count("cloth") == 3,
          "the town gave exactly the package");
    CHECK(town.count("wood") == 5 && town.count("coin_empire_copper") == 7,
          "coin swaps like any ware - nothing minted, nothing lost");
}

void test_all_or_nothing() {
    Inventory a, b;
    a.add("food", 2);
    b.add("wood", 8);

    CHECK(!barter_swap(a, b, {line(a, "food", 3)}, {line(b, "wood", 1)}),
          "a short GIVE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {line(a, "food", 1)}, {line(b, "wood", 9)}),
          "a short TAKE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {line(a, "food", 0)}, {line(b, "wood", 1)}),
          "a non-positive line is not a deal");
    CHECK(a.count("food") == 2 && b.count("wood") == 8
              && a.count("wood") == 0 && b.count("food") == 0,
          "a refused deal moved NOTHING");
}

void test_repeated_lines_sum() {
    Inventory a, b;
    a.add("food", 5);

    CHECK(!barter_swap(a, b,
                       {line(a, "food", 3), line(a, "food", 3)}, {}),
          "two lines of one stack SUM - 6 from 5 refuses");
    CHECK(a.count("food") == 5, "and nothing moved");
    CHECK(barter_swap(a, b, {line(a, "food", 3), line(a, "food", 2)}, {}),
          "5 from 5 across two lines settles");
    CHECK(a.count("food") == 0 && b.count("food") == 5,
          "both lines travelled");
}

void test_same_ware_both_ways() {
    Inventory a, b;
    a.add("food", 3);
    b.add("food", 5);

    CHECK(barter_swap(a, b, {line(a, "food", 3)}, {line(b, "food", 5)}),
          "food for food is a legal deal");
    CHECK(a.count("food") == 5 && b.count("food") == 3,
          "the two stacks crossed whole");
}

void test_empty_packages_are_a_noop() {
    Inventory a, b;
    a.add("food", 1);
    CHECK(barter_swap(a, b, {}, {}), "an empty deal is trivially settled");
    CHECK(a.count("food") == 1 && b.count("food") == 0, "and moved nothing");
}

// ── The rolled instance trades AS ITSELF (verdict 2026-09-07) ─────────────
void test_rolled_stack_travels_whole() {
    Inventory seller, buyer;
    ItemRef bare{};
    bare.def = std::uint16_t(item_index("wpn_sword"));
    bare.count = 1;
    ItemRef rolled = bare;
    rolled.seed = 99;
    rolled.set_affix(0, {std::uint8_t(BonusId::Str), 2});
    CHECK(seller.add_ref(bare) && seller.add_ref(rolled),
          "the bare blade and its rolled twin sit in two slots of one id");
    CHECK(seller.used_slots() == 2, "the stacking law kept them apart");

    CHECK(value_of(rolled) > value_of(bare),
          "the rolled instance is worth MORE than its bare row");
    CHECK(inventory_value(seller) == value_of(bare) + value_of(rolled),
          "the bag's universal value is the sum of its instances, not "
          "count x bare row");

    // Stage the ROLLED slot; the bare one stays.
    const int rolledSlot =
        seller.slots[0].seed == 99u ? 0 : 1;
    CHECK(barter_swap(seller, buyer,
                      {BarterLine{rolledSlot, 1, rolled.def}}, {}),
          "the rolled line settles");
    CHECK(buyer.used_slots() == 1
              && buyer.slots[std::size_t(slot_of(buyer, "wpn_sword"))]
                     .affix_at(0).value == 2,
          "the AFFIX arrived with the blade - it travelled as itself");
    CHECK(seller.count("wpn_sword") == 1
              && seller.slots[std::size_t(slot_of(seller, "wpn_sword"))]
                         .seed == 0u,
          "and the bare twin stayed exactly where it was");
}

// A shelf slot re-filled with a DIFFERENT row under an open panel: the
// stale line refuses instead of shipping the stranger.
void test_stale_slot_refuses() {
    Inventory a, b;
    a.add("food", 2);
    const BarterLine stale = line(a, "food", 1);
    a = Inventory{};
    a.add("wood", 5);   // wood now sits where the food sat
    CHECK(!barter_swap(a, b, {stale}, {}),
          "a re-filled slot refuses the stale line");
    CHECK(a.count("wood") == 5 && b.total() == 0, "and nothing moved");
}

// ── СУЩЕСТВО ТОРГУЕТСЯ ТЕМ ЖЕ ПРИЛАВКОМ, ЧТО МЕЧ (M-292, вердикт владельца
// 2026-10-20: «это тупа стоимость у сущчества как у предмета (типа абсолютная
// стоимость)»; «да это по сути одна колона у них») ────────────────────────
//
// ЧТО ЗДЕСЬ ОХРАНЯЕТСЯ — ЗАКОН КОНТЕЙНЕРА: контейнер ОДИН, сословий внутри
// него нет, значит у вопроса «сколько стоит объект» одна дверь на обе
// половины единого пространства строк. До M-292 `value_of` резолвила только
// предметный каталог, и на строке существа отдавала НОЛЬ — не ответ, а
// немоту, на которой стояла вся торговля: слот существа рисовался пустым и
// не нажимался, а застейдженная голова давала ноль к сумме сделки.
//
// УТВЕРЖДАЕТСЯ ОТНОШЕНИЕ И ТА ЖЕ ДВЕРЬ, А НЕ ПЕРЕСКАЗАННОЕ ЧИСЛО (§8 п.4):
// цена существа ОБЯЗАНА совпасть с `hire_price_for` байт в байт, потому что
// второго числа в мире нет — владелец этого и просил словом «ну и сразу
// беслпатно цнеа найма и тд отсюда». Литерал «30» здесь был бы ложью при
// первой же перекалибровке строки.
void test_a_creature_is_priced_by_its_own_row() {
    const int lvl = 1;
    ItemRef head = creature_slot(NPCType::Peasant, lvl, 1);
    CHECK(value_of(head)
              == hire_price_for(std::uint16_t(NPCType::Peasant), lvl),
          "цена головы ЕСТЬ цена найма: одно число, одна дверь, второго в "
          "мире нет");
    CHECK(value_of(head) > 0,
          "строка с ценой перестала быть немой — на этом нуле стоял весь "
          "прилавок");

    // ЗАКОН УРОВНЯ — ТОТ ЖЕ, ЧТО У НАЙМА, и это не косметика: снесённая
    // `creatures_worth` читала `hireGold` БЕЗ него, то есть ветеран пахнул
    // богатством как новобранец.
    const int high = 7;   // > 3: soldier_level_factor делит на 3
    ItemRef veteran = creature_slot(NPCType::Peasant, high, 1);
    CHECK(value_of(veteran) > value_of(head),
          "ветеран дороже новобранца ТЕМ ЖЕ законом уровня, что у найма");
    CHECK(value_of(veteran)
              == hire_price_for(std::uint16_t(NPCType::Peasant), high),
          "и ровно настолько, насколько дороже его наём");

    // Строка БЕЗ цены остаётся нулём, и это теперь ОТВЕТ, а не немота:
    // 43 строки существ из 47 не несут `hireGold`, потому что их вывод
    // (`upkeepGoldPerDay x 30`) на беззарплатном звере даёт ноль.
    ItemRef beast = creature_slot(NPCType::Wolf, 1, 1);
    CHECK(value_of(beast)
              == hire_price_for(std::uint16_t(NPCType::Wolf), 1),
          "беззарплатный зверь стоит столько, сколько говорит ЕГО строка — "
          "дверь одна и для него");
}

// ── ДВА ВОПРОСА, ДВЕ ДВЕРИ, И РАЗНИЦА МЕЖДУ НИМИ — РОВНО ДУШИ ────────────
// «Сколько контейнер стоит весь» и «чем он может заплатить» — РАЗНЫЕ вопросы
// (DOD п.6 про второй ОТВЕТ, а не про второй вопрос), и разошлись они ровно
// на душах: крестьянин города есть ценность мира, но не монета в его
// кошельке. Почему не монета — у автоматического платежа нет двери паствы:
// он зовётся из пятнадцати мест живой экономики, и ни одно не знает, чей дом
// считает проданного.
void test_worth_counts_souls_and_payment_does_not() {
    Inventory bag;
    bag.add("food", 4);
    const int goods = inventory_value(bag);
    CHECK(goods > 0, "предметная половина оценена");
    CHECK(container_worth(bag) == goods,
          "без душ обе двери отвечают ОДИНАКОВО — расхождение не выдумано, "
          "оно наступает только там, где души есть");

    CHECK(creatures_push_stack(bag, NPCType::Peasant, 1, 3),
          "свидетель рождает своё предусловие сам (§8 п.11)");
    const int headPrice = hire_price_for(std::uint16_t(NPCType::Peasant), 1);
    CHECK(container_worth(bag) == goods + 3 * headPrice,
          "стоимость контейнера ЦЕЛИКОМ выросла ровно на цену трёх голов");
    CHECK(inventory_value(bag) == goods,
          "а платёжное число не дрогнуло: иначе вассалу начислялась бы "
          "неоплатная дань, а жалованье гасилось бы БЕЗ оплаты");
}

// НЕГАТИВНЫЙ КОНТРОЛЬ, И ОН ПРО ЖИВОЙ МИР, А НЕ ПРО АРИФМЕТИКУ: контейнер из
// ОДНИХ душ не может заплатить ничем. Если этот CHECK упадёт, значит души
// стали платёжной материей — и первый же сборщик дани увезёт крестьян мимо
// двери паствы (`shift_home_flock@src/macro/squad.h`), то есть место
// продолжит кормить и облагать данью проданного.
void test_a_container_of_only_souls_cannot_pay() {
    Inventory bag;
    CHECK(creatures_push_stack(bag, NPCType::Peasant, 1, 50),
          "пятьдесят душ и ни одного предмета");
    CHECK(container_worth(bag) > 0, "добра в нём много");
    CHECK(inventory_value(bag) == 0,
          "...а заплатить нечем: платёж по плотности душ не носит");

    CHECK(densest_value_slot(bag).slot < 0,
          "и платёжная дверь не находит в нём НИ ОДНОГО слота — не потому, "
          "что он пуст, а потому, что душа не монета");
    CHECK(pay_value_dense(bag, 1000) == 0,
          "попытка списать с него стоимость платит НОЛЬ");
    CHECK(creature_count(bag) == 50,
          "и не уносит ни одной головы: платёж их не касается вовсе");
}

// ── ЗАКОН ПЛОТНОСТИ: МАССА ЦЕЛАЯ, ПОРОГ ЕСТЬ ШАГ КОЛОНКИ (M-298) ──────────
// ЯКОРЯ, А НЕ ПЕРЕСЧЁТ ПРОДАКШЕН-ФОРМУЛЫ (§8 п.6, шестая форма слепоты): ни
// одно утверждение ниже не повторяет арифметику двери — каждое называет
// независимо известный факт мира или ОТНОШЕНИЕ двух строк.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ — ПРОГНАНА, А НЕ ОБЕЩАНА
// (2026-10-20; §8 п.6: не записанный контроль доказывает лишь, что детектор
// сработал один раз в голове автора):
//   ОБЯЗАНЫ КРАСНИТЬ:
//     · страж снят (`item_mass_g` → `return d.massG;`): сборка ЧИСТАЯ, 0
//        ошибок, оба static_assert каталога ЗЕЛЕНЫ (на каталоге без нулей
//        страж и так тождественен) — и ловит ровно один прибор:
//        `test_the_guard_answers_one_step_on_a_massless_row`, 2 из 58,
//        barter_test КРАСНЫЙ. Вот зачем безмассовая строка собирается ЗДЕСЬ
//        руками: мир её больше не рождает, и без локальной строки снос
//        стража прошёл бы зелёным;
//     · порог `kMassStepG = 1000` (тот самый запрещённый «1 кг»): ОТКАЗ
//        СБОРКИ, и сразу ДВА — `catalog_min_mass_g() > kMassStepG` и
//        `mass_guard_is_identity_on_catalog` (items.cpp:552 и :567);
//     · строка каталога с `massG = 0` (медяк обнулён): ОТКАЗ СБОРКИ, те же
//        два — то есть живой вход M-298 стал НЕВЫРАЗИМ, а не «обработан»;
//     · колонка переименована обратно (`massG` → `weight`): 60 отказов
//        компилятора. Это и есть проба на немых читателей — у колонки их
//        НЕ ОСТАЛОСЬ, все 20 прод-сайтов названы по имени.
//   ОБЯЗАНЫ МОЛЧАТЬ (детектор, краснеющий на всё, — не детектор):
//     · порог 1 → 9 г: 58/58 ЗЕЛЕНО, и это ПРАВИЛЬНО — минимум каталога
//        10 г, страж остаётся тождественным, а любой порог НИЖЕ самой
//        лёгкой строки мира законен;
//     · перекалибровка массы (харч 1000 → 1500 г): 58/58 ЗЕЛЕНО — якоря
//        держат ОТНОШЕНИЯ («монета легче буханки»), а не пересказанные
//        числа, поэтому баланс свободен (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
//        Перестановка СТРОК каталога в эту графу не годится: её краснит
//        лестница архетипов M-295, то есть совсем другой закон.
void test_mass_is_integral_and_the_coin_is_the_lightest_row() {
    // ЯКОРЬ: монета есть САМАЯ ЛЁГКАЯ материя мира — это вердикт владельца
    // («у них минимальный вес при макс стоимости»), а не свойство таблицы.
    const ItemDef* coin = item_def("coin_empire_copper");
    CHECK(coin != nullptr, "у монеты фракции есть строка каталога");
    const ItemDef* bread = item_def("food");
    CHECK(bread != nullptr, "у харча есть строка каталога");
    CHECK(coin->massG < bread->massG,
          "монета легче буханки — иначе вердикт «дефолт списания в монетах» "
          "держался бы не на весе, а на вере");

    // ЯКОРЬ: квант НАЗВАН, и названное имя сходится с арифметикой. Сто монет
    // весят РОВНО сто своих масс — ни ulp больше (ЗАКОН ИМЕНИ: имя колонки
    // `massG` обязано отвечать граммам, а не «сотым килограмма»).
    Inventory purse;
    CHECK(purse.add("coin_empire_copper", 100),
          "сто монет в кошель");
    CHECK(purse.sum().massG == std::int64_t(coin->massG) * 100,
          "сто монет весят ровно сто масс монеты — целое умножение, а не "
          "сумма непредставимых флоатов");
    // И ДВЕРЬ МАССЫ ОТДАЁТ ТЕ ЖЕ ГРАММЫ БЕЗ ЕДИНОГО ДЕЛЕНИЯ. Здесь стояло
    // утверждение о КИЛОГРАММАХ («делится ровно один раз») — и деления
    // больше нет вовсе: с переводом обоза в целое (2026-10-20) `float`
    // ушёл из цепочки целиком, а килограмм остался только у показа в `ui/`.
    CHECK(inventory_mass_g(purse) == std::int64_t(coin->massG) * 100,
          "дверь массы есть сводка, а не пересчёт: ни деления, ни флоата");
}

void test_the_guard_answers_one_step_on_a_massless_row() {
    // Каталог такой строки не несёт и НЕ СМОЖЕТ (static_assert в items.cpp),
    // поэтому страж спрашивается НАПРЯМУЮ — на строке, собранной здесь. Это
    // единственный способ увидеть ветку, которую компилятор сделал
    // недостижимой для мира: без него снос стража прошёл бы зелёным.
    ItemDef massless{};
    massless.massG = 0;
    CHECK(item_mass_g(massless) == kMassStepG,
          "безмассовая строка весит ОДИН ШАГ КОЛОНКИ — иначе перекрёстное "
          "сравнение вырождается и она выигрывает всякий платёж мира");
    CHECK(item_mass_g(massless) > 0,
          "и этого довольно, чтобы порядок остался порядком");

    ItemDef heavy{};
    heavy.massG = 64000;
    CHECK(item_mass_g(heavy) == heavy.massG,
          "а на всякой взвешенной строке страж ТОЖДЕСТВЕНЕН: он охраняет "
          "область определения формулы, а не правит баланс");
}

void test_coin_outranks_bread_by_density() {
    // ЯКОРЬ ОТНОШЕНИЯ, пройденный ЖИВОЙ дверью: в кошеле и монета, и харч —
    // платёж обязан потянуться к монете. Это вердикт владельца о монетах,
    // утверждённый без единого пересчёта `v·w`.
    Inventory bag;
    CHECK(bag.add("food", 10), "десять харчей");
    CHECK(bag.add("coin_empire_copper", 10), "десять монет");

    const DensestSlot d = densest_value_slot(bag);
    CHECK(d.slot >= 0, "платить есть чем");
    CHECK(bag.slots[std::size_t(d.slot)].def
              == std::uint16_t(item_index("coin_empire_copper")),
          "самый плотный слот — МОНЕТА, и это арифметика веса, а не ветка "
          "по роду (S10: спецпутей для монет не существует)");
    CHECK(d.massG == item_def("coin_empire_copper")->massG,
          "дверь отдала массу той же строки — её читает погрузка дома, и "
          "второй раз взвешивать неоткуда");

    // НЕГАТИВНЫЙ КОНТРОЛЬ ЗАПРЕЩЁННОГО ПОРОГА, ЧИСЛОМ: порог «1 кг» сравнял
    // бы монету с буханкой по массе, и тогда победил бы ХАРЧ — он дороже
    // поштучно. Утверждаем ровно это, чтобы запрет не остался прозой.
    const std::int64_t floorG = 1000;   // тот самый запрещённый порог
    const std::int64_t coinV  = item_def("coin_empire_copper")->value;
    const std::int64_t breadV = item_def("food")->value;
    CHECK(coinV * floorG < breadV * floorG,
          "под порогом 1 кг монета проигрывает буханке — вот почему порог "
          "есть ШАГ КОЛОНКИ, а не круглый килограмм");
    CHECK(item_def("coin_empire_copper")->massG < floorG,
          "и монета действительно ушла бы под этот порог целиком");
}

} // namespace

int main() {
    test_swap_moves_both_packages();
    test_all_or_nothing();
    test_repeated_lines_sum();
    test_same_ware_both_ways();
    test_empty_packages_are_a_noop();
    test_rolled_stack_travels_whole();
    test_stale_slot_refuses();
    test_a_creature_is_priced_by_its_own_row();
    test_worth_counts_souls_and_payment_does_not();
    test_a_container_of_only_souls_cannot_pay();
    test_mass_is_integral_and_the_coin_is_the_lightest_row();
    test_the_guard_answers_one_step_on_a_massless_row();
    test_coin_outranks_bread_by_density();
    return sm::test::report("barter_test");
}
