// КАТАЛОГ ПРЕДМЕТОВ — тела каталожных дверей (tables/items.h; наряд M-181,
// разрез items). Анкетная половина — двери над Inventory — живёт в macro/
// (craft/scrap/use — состояние, а не контент). Здесь состояния мира нет.
//
// All numeric constants, ids, chances, min/max ranges, and gold formulas
// are copied verbatim from the TS source (the port is closed).

#include "tables/items.h"
#include "tables/body_parts.h"   // part_bit — the slotMask columns of the rack
#include "tables/commodity.h"
#include "tables/faction.h"   // mint columns — what the neutrality witness walks
#include "tables/world_row.h"   // вторая половина пространства строк — value_of

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>   // catalog_map — the authoring-key index, this TU only

namespace sm {

namespace {

// ── Item catalog ───────────────────────────────────────────────

constexpr ItemDef kCatalog[] = {
    // ── ПОРЯДОК СТРОК ЕСТЬ ЛЕСТНИЦА, А НЕ ИСТОРИЯ (наряд M-295) ───────────
    // Строки идут ПО ТИПАМ, типы — ПО АРХЕТИПАМ (`tables/object_type.h`),
    // поэтому «все строки архетипа» есть НЕПРЕРЫВНЫЙ ДИАПАЗОН ординалов, а не
    // фильтр по колонке. Непрерывность прибита `static_assert`-ами сразу под
    // таблицей — вставка строки не в свою группу красит СБОРКУ.
    //
    // Здесь стояло «Order copied verbatim from TS ITEM_CATALOG», и это была
    // правда ровно до этого наряда: порядок был историей порта и дописок, а
    // новая строка шла В ХВОСТ, потому что сохранённый `ItemRef` вёз ординал.
    // Сейв больше ограничением не является (§10), поэтому порядок стал
    // ЗАКОНОМ: новая строка встаёт В СВОЮ ГРУППУ.
    //
    // СУЩЕСТВИТЕЛЬНЫЕ ЭКОНОМИКИ ЖИВУТ В ЭТОМ ЖЕ КАТАЛОГЕ (вердикт владельца об
    // одном словаре): ткань, которую ткёт город, и ткань в сумке игрока — ОДНА
    // строка. Id и веса сходятся с `tables/commodity.h` дословно, и закон
    // связи двух таблиц держит `econ_v1_test`.
    // ── АРХЕТИП Weapon: ЧЕМ БЬЮТ ──────────────────────────────────────────
    // BASE rows are BARE (owner verdict (а), 2026-09-07): the innate bonus
    // column stays a column — it is the legitimate voice of a future unique
    // or artifact — but a plain dagger says nothing through it. The «+2 STR»
    // and «+2 END» that stood here were placeholders from the day the door
    // was proved (2026-08-27), and every drop repeated them bit-for-bit;
    // what a rolled instance says now comes from grant_affixes. `slotMask`
    // names TYPES, so a dagger fits any body with a grip — an octopus
    // included, without the row knowing an octopus exists.
    //
    // ТЕМПО — НИЧЬЯ КОЛОНКА: килограмм строки оценивает замах единым законом
    // массы (`weapon_swing_seconds@src/tables/items.h` — кинжал 2.0 с … булава
    // 3.25 с), поэтому лестница ниже торгует СКОРОСТЬ за растущий куб. Здесь
    // стояла фраза «лук сознательно ОТСУТСТВУЕТ, ему нужен универсальный закон
    // стрельбы» — закон пришёл (`Delivery::Missile`), лук стоит в этой же
    // группе, и держать обе половины того диалога значило бы хранить
    // опровергнутое утверждение рядом с опровержением.
    //
    // ОДИН ТИП НА СТРОКУ ЗДЕСЬ — НЕ ДЕФЕКТ ЛЕСТНИЦЫ: оружие растёт видами
    // первым же ростом контента (бронзовый кинжал, стальной), а особенность
    // кинжала есть модуль ТИПА, а не колонка строки.
    {"wpn_dagger",  "Rusty Dagger",    ObjectType::Dagger,     30, 1000, "\xF0\x9F\x97\xA1",
        "A rusty sidearm, quick in the hand", {},
        /*slot*/part_bit(BodyPartId::Grip) | part_bit(BodyPartId::OffGrip),
        // 1d4 piercing through the Dagger skill: a rusty sticker — mean 2.5
        // against the fist's 1.5, and the point slips where a club cannot.
        // Its pace needs no column: the 1 kg above prices the swing through
        // the mass law (anatomy.h weapon_swing_seconds).
        /*blocks*/0, /*def*/{}, /*dice*/{1, 4},
        /*dmgType*/DamageType::Pierce, /*skill*/SkillId::Dagger},
    {"wpn_sword",   "Worn Sword",      ObjectType::Sword,      60, 2000, "\xE2\x9A\x94",
        "A soldier's blade, past its best years", {},
        /*slot*/part_bit(BodyPartId::Grip) | part_bit(BodyPartId::OffGrip),
        /*blocks*/0, /*def*/{}, /*dice*/{1, 6},
        /*dmgType*/DamageType::Pierce, /*skill*/SkillId::Sword},
    // The two-handers say so through the mask, not through code: they sit in
    // the main grip and take the off hand with them — the case blocksMask
    // exists for.
    {"wpn_spear",   "Crude Spear",     ObjectType::Spear,      50, 2500, "\xF0\x9F\x94\xB1",
        "A sharpened head on a long shaft; needs both hands", {},
        /*slot*/part_bit(BodyPartId::Grip),
        /*blocks*/part_bit(BodyPartId::OffGrip), /*def*/{}, /*dice*/{1, 8},
        /*dmgType*/DamageType::Pierce, /*skill*/SkillId::Spear},
    {"wpn_axe",     "Woodsman's Axe",  ObjectType::Axe,        70, 3000, "\xF0\x9F\xAA\x93",
        "Made for timber, willing to argue", {},
        /*slot*/part_bit(BodyPartId::Grip) | part_bit(BodyPartId::OffGrip),
        /*blocks*/0, /*def*/{}, /*dice*/{1, 10},
        /*dmgType*/DamageType::Pierce, /*skill*/SkillId::Axe},
    {"wpn_mace",    "Iron Mace",       ObjectType::Mace,       80, 3500, "\xE2\x9A\x92",
        "A blunt argument no armour fully wins", {},
        /*slot*/part_bit(BodyPartId::Grip) | part_bit(BodyPartId::OffGrip),
        /*blocks*/0, /*def*/{}, /*dice*/{1, 12},
        /*dmgType*/DamageType::Blunt, /*skill*/SkillId::Mace},
    {"wpn_staff",   "Quarterstaff",    ObjectType::Staff,      40, 2000, "\xF0\x9F\xAA\x84",
        "A traveller's stick and a caster's habit; both hands", {},
        /*slot*/part_bit(BodyPartId::Grip),
        /*blocks*/part_bit(BodyPartId::OffGrip), /*def*/{}, /*dice*/{1, 6},
        /*dmgType*/DamageType::Blunt, /*skill*/SkillId::Staff},
    // Лук: его урон НЕ берёт прибавки атрибута (кубы + навык + УДАЧА —
    // дальность и есть компенсация, вердикт владельца); его темп — тот же
    // килограмм через тот же закон массы; его стрела — та же дверь снаряда, в
    // которую стреляет любой НПЦ, и боеприпаса нет нигде. Обе руки, как у
    // копья.
    {"wpn_bow",     "Hunting Bow",     ObjectType::Bow,        70, 1000, "\xF0\x9F\x8F\xB9",
        "A self bow of yew; kills at a distance it never has to close", {},
        /*slot*/part_bit(BodyPartId::Grip),
        /*blocks*/part_bit(BodyPartId::OffGrip), /*def*/{}, /*dice*/{1, 8},
        /*dmgType*/DamageType::Pierce, /*skill*/SkillId::Bow,
        /*delivery*/Delivery::Missile, /*range*/80.0f},
    // ── АРХЕТИП Armor: ЧТО НАДЕВАЮТ ───────────────────────────────────────
    {"arm_leather", "Leather Armor",   ObjectType::BodyArmor,  60, 5000, "\xF0\x9F\x9B\xA1",
        "A boiled-leather coat over the torso", {},
        /*slot*/part_bit(BodyPartId::Torso), /*blocks*/0,
        // ПЕРЕКАЛИБРОВАНО с 3 на 10 (M-209): броня стала ПРОЦЕНТОМ, и
        // кожаная куртка честно читается как «снимает десятую часть»;
        // uniform across the nine types — the mechanical scalar-era
        // translation, until armour rows author their columns.
        // Boiled leather is LIGHT armour: the row states which skill governs
        // it in the same column a weapon states its own (2026-09-19).
        /*def*/uniform_defense(10, 1), /*dice*/{}, /*dmgType*/DamageType::Blunt,
        /*skill*/SkillId::LightArmor},
    // ── АРХЕТИП Potion: ЧТО ПОТРЕБЛЯЮТ ЗА ЭФФЕКТ ──────────────────────────
    // Тип один и зовётся `consumable` (вердикт владельца 2026-10-20): сюда же
    // придут свитки и жезлы — всё, что при потреблении даёт эффект спелла на
    // ресурсы анкеты.
    {"potion_hp",   "Health Potion",   ObjectType::Consumable, 50, 300, "\xE2\x9D\xA4",
        "Restores 30 HP", {{std::uint8_t(BonusId::HealHp), 30}},
        // Глоток в бою — ДЕЙСТВИЕ (CANON S13): секунда базы, делимая
        // дверью восстановления. До 2026-09-19 зелья пились даром.
        /*slot*/0, /*blocks*/0, /*def*/{}, /*dice*/{},
        /*dmgType*/DamageType::Blunt, /*skill*/SkillId::Count,
        /*delivery*/Delivery::Melee, /*range*/0.0f, /*useSeconds*/1.0f},
    {"potion_mp",   "Mana Potion",     ObjectType::Consumable, 75, 300, "\xE2\x9C\xA8",
        "Restores 15 MP", {{std::uint8_t(BonusId::HealMp), 15}},
        /*slot*/0, /*blocks*/0, /*def*/{}, /*dice*/{},
        /*dmgType*/DamageType::Blunt, /*skill*/SkillId::Count,
        /*delivery*/Delivery::Melee, /*range*/0.0f, /*useSeconds*/1.0f},
    // ── АРХЕТИП Food: ЧТО ПОТРЕБЛЯЮТ ЗА СЫТОСТЬ ───────────────────────────
    // Здесь живёт ГОЛОДНАЯ строка лестницы нужд, и она обязана быть РОВНО
    // одна (static_assert ниже по файлу).
    {"food_meat",   "Raw Meat",        ObjectType::Ration,     15, 500, "\xF0\x9F\x8D\x96",
        "Restores 15 HP", {{std::uint8_t(BonusId::HealHp), 15}}},
    // ПИЩА — архетип ЕДА, а не материал: ею гасят голод (econ_day.h
    // лестница), и она терминальна — её растят и добывают, а не варят.
    {"food",    "Provisions",      ObjectType::Ration,     5, 1000, "\xF0\x9F\x8C\xBE",
        "Raw provisions — grain from the field, meat from the hunt",
        // Съеденное лечит — как всякая еда каталога; величина от СТОИМОСТИ
        // строки (5), чтобы не заводить второго числа о той же вещи: хлеб
        // стоил 10 и лечил 10, мясо стоит 15 и лечит 15.
        {{std::uint8_t(BonusId::HealHp), 5}}},
    // ── АРХЕТИП Material: РЕСУРСЫ, ИЗ КОТОРЫХ КРАФТЯТ ────────────────────
    // МОНЕТНЫЕ МЕТАЛЛЫ (тип `ore`). Каждая стоимость ВЫВЕДЕНА, а не выбрана:
    // 128 монет из единицы (единый выход чеканки, kPartsAuthoring — выбор
    // владельца 2026-09-18) × номинал монеты — медь 128×1, серебро 128×10,
    // золото 128×100. Свидетель нейтральности (mint_is_value_neutral ниже)
    // прибивает все три к монетным строкам, поэтому денежная масса мира
    // остаётся геологией × одно число; свидетель массы
    // (mint_yield_is_mass_honest) прибивает выход к колонкам ВЕСА. Серебро
    // стоило 32, когда монета была номиналом 1; вердикт перекалибровки
    // владельца держит ценностный потолок мира, пересчитывая ЖИЛЫ
    // (deposit_layer.cpp), а не дешевя металл.
    {"iron",    "Iron Ore",        ObjectType::Ore,        15, 4000, "\xE2\x9B\x8F",
        "Smithing material", {}},
    {"copper",  "Copper Ore",      ObjectType::Ore,        128, 4000, "\xF0\x9F\x9F\xA0",
        "Mint metal", {}},
    {"silver",  "Silver Ore",      ObjectType::Ore,        1280, 4000, "\xE2\x9A\xAA",
        "Mint metal", {}},
    {"gold",    "Gold Ore",        ObjectType::Ore,        12800, 4000, "\xF0\x9F\x9F\xA1",
        "Mint metal", {}},
    {"wood",    "Wood",            ObjectType::Timber,     5, 2000, "\xF0\x9F\xAA\xB5",
        "Building material", {}},
    {"stone",   "Stone",           ObjectType::Stone,      5, 4000, "\xF0\x9F\xAA\xA8",
        "Quarried stone", {}},
    {"clay",    "Clay",            ObjectType::Clay,       5, 2000, "\xF0\x9F\xBA",
        "River clay for bricks", {}},
    // Волокно: стоимость вчетверо ниже ткани (20) — прядение и есть та
    // работа, что поднимает цену, и второго числа об этом не заводится.
    {"fibre",   "Fibre",           ObjectType::Fibre,      5, 1000, "\xF0\x9F\xAA\xA2",
        "Flax fibre — what cloth is spun from", {}},
    {"mat_bone",    "Bone",            ObjectType::Bone,       6, 500, "\xF0\x9F\xA6\xB4",
        "Crafting material from monsters", {}},
    {"mat_hide",    "Hide",            ObjectType::Hide,       12, 1000, "\xF0\x9F\xA7\xB5",
        "Tanned animal hide", {}},
    {"mat_herb",    "Herb",            ObjectType::Herb,       8, 50, "\xF0\x9F\x8C\xBF",
        "Alchemy ingredient", {}},
    // ── АРХЕТИП Goods: ТОРГОВАЯ КОРЗИНА ──────────────────────────────────
    // У ВЕЩИ ОДНА РОЛЬ (вердикт владельца 2026-10-20): всё, что лежит здесь и
    // только здесь, есть товар комфорта лестницы нужд. Повозка ушла в свой
    // архетип `Vehicle` и тем ЧЕСТНО вышла из корзины — это смысл переезда, а
    // не его побочный эффект.
    {"cloth",   "Cloth",           ObjectType::Cloth,      20, 1000, "\xF0\x9F\xA7\xB6",
        "Woven clothing", {}},
    {"bricks",  "Bricks",          ObjectType::Bricks,     10, 4000, "\xF0\x9F\xA7\xB1",
        "Fired bricks for housing", {}},
    {"tools",   "Tools",           ObjectType::Tools,      40, 2000, "\xF0\x9F\x94\xA8",
        "Iron tools of the trades", {}},
    {"furniture","Furniture",      ObjectType::Furniture,  40, 8000, "\xF0\x9F\xAA\x91",
        "Carpented furniture", {}},
    {"jewelry", "Jewelry",         ObjectType::Jewelry,    160, 1000, "\xF0\x9F\x92\x8D",
        "Fine ornaments", {}},
    {"carving", "Carving",         ObjectType::Carving,    80, 2000, "\xF0\x9F\xAA\x86",
        "Ornamental woodwork", {}},
    {"statue",  "Statue",          ObjectType::Statue,     320, 64000, "\xF0\x9F\x97\xBF",
        "A sculpted stone statue", {}},
    // ── АРХЕТИП Currency: ДЕНЬГИ ─────────────────────────────────────────
    // Вердикт владельца 2026-09-17, дословно: «монета это просто товар со
    // стоимостью 10 для серебра 100 для золота 1 для медяка — никаких особых
    // механик и ворот, всё через бартер; это буквально просто типы
    // предметов… просто дата в таблицу итемов». Три монеты на державу — медь
    // 1 / серебро 10 / золото 100 — двенадцать обычных строк ОДНОГО типа
    // `coin`: монета отличается номиналом и чеканкой, то есть КОЛОНКАМИ, а не
    // механикой, и это сильнейшая группировка каталога. Номинал И ЕСТЬ
    // колонка стоимости; чеканка И ЕСТЬ колонка состава ({металл 1} → 128,
    // kPartsAuthoring); КТО торгует какой семьёй — монетные колонки реестра
    // фракций. Единственное преимущество монеты над любым другим добром
    // арифметическое: минимальный вес при максимальной стоимости, поэтому
    // плотный по стоимости платёж тянется к ней первым.
    {"coin_empire_copper",  "Copper Crown",  ObjectType::Coin,       1, 10, "\xF0\x9F\xAA\x99",
        "Copper coin of the Empire of Light", {}},
    {"coin_empire_silver",  "Silver Crown",  ObjectType::Coin,       10, 10, "\xF0\x9F\xAA\x99",
        "Silver coin of the Empire of Light", {}},
    {"coin_empire_gold",    "Gold Crown",    ObjectType::Coin,       100, 10, "\xF0\x9F\xAA\x99",
        "Gold coin of the Empire of Light", {}},
    {"coin_magika_copper",  "Copper Sigil",  ObjectType::Coin,       1, 10, "\xF0\x9F\xAA\x99",
        "Copper coin of the Magika realms", {}},
    {"coin_magika_silver",  "Silver Sigil",  ObjectType::Coin,       10, 10, "\xF0\x9F\xAA\x99",
        "Silver coin of the Magika realms", {}},
    {"coin_magika_gold",    "Gold Sigil",    ObjectType::Coin,       100, 10, "\xF0\x9F\xAA\x99",
        "Gold coin of the Magika realms", {}},
    {"coin_timaert_copper", "Copper Mark",   ObjectType::Coin,       1, 10, "\xF0\x9F\xAA\x99",
        "Copper coin of the Republic of Timaert", {}},
    {"coin_timaert_silver", "Silver Mark",   ObjectType::Coin,       10, 10, "\xF0\x9F\xAA\x99",
        "Silver coin of the Republic of Timaert", {}},
    {"coin_timaert_gold",   "Gold Mark",     ObjectType::Coin,       100, 10, "\xF0\x9F\xAA\x99",
        "Gold coin of the Republic of Timaert", {}},
    {"coin_barbar_copper",  "Copper Ring",   ObjectType::Coin,       1, 10, "\xF0\x9F\xAA\x99",
        "Copper ring-money of the northern kingdoms", {}},
    {"coin_barbar_silver",  "Silver Ring",   ObjectType::Coin,       10, 10, "\xF0\x9F\xAA\x99",
        "Silver ring-money of the northern kingdoms", {}},
    {"coin_barbar_gold",    "Gold Ring",     ObjectType::Coin,       100, 10, "\xF0\x9F\xAA\x99",
        "Gold ring-money of the northern kingdoms", {}},
    // ── АРХЕТИП Vehicle: ПОВОЗКИ ─────────────────────────────────────────
    // Первый потребитель объект-модулей (M-296): предел обоза даст модуль
    // ТИПА, сводящий число повозок с числом живых спин, а непряжённая
    // повозка останется грузом в свои честные 32 кг каталога. Колонки
    // грузопереноса здесь НЕТ и до модуля не будет — вердикт владельца
    // (M-294): без предела она делает купленную один раз вместимость
    // навсегда бесплатной.
    {"wagon",   "Wagon",           ObjectType::Wagon,      80, 32000, "\xF0\x9F\x9B\x9E",
        "A cart for hauling goods", {}},
    // ── АРХЕТИП Misc: ЧЕСТНОЕ ПРОЧЕЕ, И ПОСЛЕДНЕЕ СРЕДИ ВЕЩЕЙ ────────────
    // Строка попадает сюда НАМЕРЕННО, а не потому, что её некуда было деть:
    // самоцвет не служит ни одной системе выше. Отрезок определён
    // ОТРИЦАНИЕМ, поэтому стоит последним — иначе каждый новый архетип вещи
    // раздвигал бы свалку.
    {"misc_gem",    "Gemstone",        ObjectType::Gem,        100, 50, "\xF0\x9F\x92\x8E",
        "Valuable gem, can be sold", {}},
};

// ── ЛЕСТНИЦА ПОД КОМПИЛЯТОРОМ: ОТРЕЗОК ЕСТЬ ОТРЕЗОК (наряд M-295) ─────────
// Это та половина закона, которую может проверить только ЭТА единица
// трансляции: `kCatalog` живёт здесь и наружу торчит только спаном, поэтому
// непрерывность предметных отрезков прибивается тут, а существных — в
// `tables/npc.h`, у своей таблицы. Два места, ОДИН закон — не два словаря: ни
// одно из них не ЗНАЕТ числа другого, каждое судит свою авторскую таблицу.
//
// Проверяется ровно то, на чём стоит вывод «все строки архетипа есть
// диапазон»: типы строк НЕ УБЫВАЮТ по каталогу. Вместе с «типы отсортированы
// по архетипу» (static_assert в object_type.h) это и даёт диапазон — и у типа,
// и у архетипа, — а вставка строки не в свою группу красит СБОРКУ.
constexpr bool catalog_rows_grouped_by_type() {
    for (std::size_t i = 1; i < std::size(kCatalog); ++i) {
        if (std::uint8_t(kCatalog[i].type)
            < std::uint8_t(kCatalog[i - 1].type)) {
            return false;
        }
    }
    return true;
}
static_assert(catalog_rows_grouped_by_type(),
              "строки каталога идут ПО ТИПАМ: иначе «все строки типа» и «все "
              "строки архетипа» перестают быть диапазоном и становятся "
              "фильтром");

// ВСЯКАЯ СТРОКА НАЗЫВАЕТ СУЩЕСТВУЮЩИЙ ТИП. Колонка без умолчания ловит
// забывчивость позиционным авторингом, но не ловит ординал, выдуманный
// приведением; этот ряд ловит и его.
constexpr bool catalog_types_exist() {
    for (const ItemDef& d : kCatalog) {
        if (std::size_t(d.type) >= std::size_t(ObjectType::Count)) return false;
    }
    return true;
}
static_assert(catalog_types_exist(), "строка каталога назвала тип вне оси");

// ПРЕДМЕТНАЯ ПОЛОВИНА НЕСЁТ ТОЛЬКО АРХЕТИПЫ ВЕЩЕЙ — то есть ЛЕСТНИЦА И АДРЕС
// СОГЛАСНЫ НА ЭТОЙ ПОЛОВИНЕ. Пространство ординалов устроено так, что ниже
// размера каталога лежат предметы (`world_row_is_item`), а воплощаемость
// спрашивается у архетипа; если бы строка каталога назвала воплощаемый
// архетип, два ответа на один вопрос мира разошлись бы молча. Вторая половина
// того же тождества стоит в `npc.h`, третья — свидетелем на всех 91 строке.
constexpr bool catalog_is_all_things() {
    for (const ItemDef& d : kCatalog) {
        if (object_embodied(object_type_archetype(d.type))) return false;
    }
    return true;
}
static_assert(catalog_is_all_things(),
              "строка предметного каталога назвала ВОПЛОЩАЕМЫЙ архетип: адрес "
              "строки мира и лестница разошлись бы молча");

// ── The matter table (owner verdicts 2026-09-11, CANON «Крафт/Скрап») ──────
// What each row is MADE OF, authored by id and resolved once into the flat
// per-ordinal table below. Rows absent here are TERMINAL (raw matter). The
// economy's numbers are LOAD-BEARING: the goods block (cloth..statue) moved
// VERBATIM from the retired kRecipes inputs — the self-play harness
// (econ_v1_test) balances
// against exactly these, so a change here is a balance change, not a tweak.
// Equipment numbers are new authoring under the same witnesses
// (item_parts_test: no-arbitrage, terminal refs).
struct PartsAuthoringRow {
    const char* id;
    struct { const char* mat; int n; } p[kMaxItemParts];
    // Items one batch of the composition makes. 1 everywhere but the coin
    // rows: «состав монеты и есть монетный двор» (owner 2026-09-12) — the
    // yield is pinned to the price anchor by the value-neutrality witness
    // (item_parts_test), so 32 here and silver's value 32 cannot drift.
    int yield = 1;
    // LABOUR: batches per person-day (items.h item_labour — the one column
    // the city's production day, the hand's SP price and a future craft
    // skill all read). The economy rows carry their retired kRecipes tempo
    // VERBATIM (self-play balance unmoved); the hand-craft rows are new
    // round po2 authoring under the same witnesses.
    int perDay = 1;
};
constexpr PartsAuthoringRow kPartsAuthoring[] = {
    // The MINT rows (CANON S10 unified 2026-09-12): one silver strikes 32
    // nominal-1 coins of any realm — the same matter table, the same scrap
    // door melts them back (64 coins → 1 silver by the pooled entropy law).
    // Labour 4 = the old mint tempo «4 металла на рабочий-день», the
    // emission-rate balance knob.
    // Coins: 1 metal → 128 coins of ITS metal (verdict №1, «1 металл → 32
    // монеты своего металла», raised to 128 by the owner 2026-09-18: «128
    // норм» — an ore grade of 32 % by the weight columns, see
    // mint_yield_is_mass_honest). One yield for all twelve rows;
    // nominal × 32 == the metal's value, held by mint_is_value_neutral.
    {"coin_empire_copper",  {{"copper", 1}}, 128, 4},
    {"coin_empire_silver",  {{"silver", 1}}, 128, 4},
    {"coin_empire_gold",    {{"gold",   1}}, 128, 4},
    {"coin_magika_copper",  {{"copper", 1}}, 128, 4},
    {"coin_magika_silver",  {{"silver", 1}}, 128, 4},
    {"coin_magika_gold",    {{"gold",   1}}, 128, 4},
    {"coin_timaert_copper", {{"copper", 1}}, 128, 4},
    {"coin_timaert_silver", {{"silver", 1}}, 128, 4},
    {"coin_timaert_gold",   {{"gold",   1}}, 128, 4},
    {"coin_barbar_copper",  {{"copper", 1}}, 128, 4},
    {"coin_barbar_silver",  {{"silver", 1}}, 128, 4},
    {"coin_barbar_gold",    {{"gold",   1}}, 128, 4},
    // Consumables: alchemy is herb-matter. ПИЩА СТРОКИ ЗДЕСЬ НЕ ИМЕЕТ — она
    // ТЕРМИНАЛЬНА (CANON S10 «ЕДА НЕ ПРОИЗВОДИТСЯ — ЕДА ДОБЫВАЕТСЯ»), и её
    // отсутствие И ЕСТЬ этот закон: без состава день производства не берёт
    // строку в кандидаты вовсе (econ_day.cpp item_parts().empty()).
    {"potion_hp",   {{"mat_herb", 2}}, 1, 8},
    {"potion_mp",   {{"mat_herb", 2}}, 1, 8},
    // The economy's goods — former kRecipes inputs AND tempos, verbatim.
    {"cloth",       {{"fibre", 2}}, 1, 4},
    {"bricks",      {{"clay", 1}}, 1, 8},
    {"tools",       {{"iron", 1}, {"wood", 1}}, 1, 2},
    {"furniture",   {{"wood", 2}}, 1, 2},
    {"wagon",       {{"wood", 4}, {"iron", 1}}, 1, 1},
    {"jewelry",     {{"iron", 1}, {"stone", 1}}, 1, 1},
    {"carving",     {{"wood", 1}}, 1, 2},
    {"statue",      {{"stone", 8}}, 1, 1},
    // Equipment: the metal ladder follows the swing-mass ladder (a mace is
    // more metal than a dagger); hafted rows carry their wood honestly so
    // crafting one EATS wood and scrapping one returns it. Labour: a blade
    // is half a smith-day (2/day), the light rows turn faster (4/day).
    {"wpn_dagger",  {{"iron", 1}}, 1, 4},
    {"wpn_sword",   {{"iron", 2}}, 1, 2},
    {"wpn_spear",   {{"wood", 1}, {"iron", 1}}, 1, 2},
    {"wpn_axe",     {{"iron", 2}, {"wood", 1}}, 1, 2},
    {"wpn_mace",    {{"iron", 3}}, 1, 2},
    {"wpn_staff",   {{"wood", 1}}, 1, 4},
    {"wpn_bow",     {{"wood", 1}}, 1, 2},
    {"arm_leather", {{"mat_hide", 3}}, 1, 2},
};

// ── The mint is value-neutral, BY LITERAL (owner 2026-09-12) ───────────────
// Coin is a commodity and minting is THE craft door — no gate, no second
// path («У НАС БАРТЕРНАЯ ЭКОНОМИКА... ПРОСТО ГОРОД ДЕЛАЕТ МОНЕТЫ ЧЕРЕЗ
// СИСТЕМУ КРАФТА»). What keeps a forger's bench harmless is arithmetic, not
// a check: one silver IS its yield in nominal coin, so striking creates no
// value. Two literals state that one fact (silver's catalog value, the coin
// rows' yield) — this guard is the product-of-two-knobs law: they cannot
// drift apart and compile.
constexpr std::int32_t catalog_mass_g_of(std::string_view id) {
    for (const ItemDef& d : kCatalog) {
        if (std::string_view(d.id) == id) return d.massG;
    }
    return -1;   // «строки нет» — авторская граница, не величина мира
}
constexpr int catalog_value_of(std::string_view id) {
    for (const ItemDef& d : kCatalog) {
        if (std::string_view(d.id) == id) return d.value;
    }
    return -1;
}
// The coins the witness walks are the FACTION REGISTRY's own mint columns —
// the one place that says what is a coin (data about factions, never a
// mechanic's gate: the old kCurrencyDefs list and its is_currency_item door
// died 2026-09-18 with verdict №1, «никаких особых механик и ворот»).
constexpr bool mint_is_value_neutral() {
    for (const FactionDef& f : kFactionDefs) {
        for (const char* coin : f.mint) {
            if (!coin || coin[0] == '\0') continue;
            int yield = 1;
            int batchValue = 0;
            bool found = false;
            for (const PartsAuthoringRow& r : kPartsAuthoring) {
                if (std::string_view(r.id) != coin) continue;
                found = true;
                yield = r.yield;
                for (const auto& p : r.p) {
                    if (p.mat) batchValue += p.n * catalog_value_of(p.mat);
                }
            }
            // A named coin with no composition would be a mint with no
            // metal — refuse to compile rather than strike from air.
            if (!found) return false;
            if (yield * catalog_value_of(coin) != batchValue) return false;
        }
    }
    return true;
}
static_assert(mint_is_value_neutral(),
              "the mint must be value-neutral: coin yield x nominal == the "
              "composition's value (S10) - these literals drifted apart");

// ── СЛОВАРЬ ПРОТИВ КАТАЛОГА: ДВА ЗАКОНА НУЖДЫ, ПРИБИТЫЕ КОМПИЛЯТОРОМ ──────
// Здесь, и только здесь, категории строк видны компилятору (каталог живёт в
// этой единице трансляции), поэтому оба закона формулы нужды (M-137)
// проверяются ДО ЗАПУСКА, а не свидетелем и не прогоном:
//   · голодная строка словаря РОВНО ОДНА — ноль означает «никто не может
//     умереть с голоду», две означают «кормящий обязан выбрать», и каждый рот
//     выбрал бы своё (прежде это держал static_assert по колонке умершей
//     лестницы `popPerUnitDay == 1`);
//   · строк комфорта ХОТЯ БЫ ОДНА — иначе доля бюджета делится на ноль, и
//     у мира не бывает неуюта вовсе.
// «СТРОКИ НЕТ» И «СВАЛОЧНАЯ ВЕЩЬ» — ДВА РАЗНЫХ ОТВЕТА, И ЭТО ПРАВКА M-295.
// Здесь возвращалось `ItemType::Misc` на id, которого в каталоге НЕТ ВОВСЕ:
// один знак отвечал на два вопроса мира, и второй ответ был немым. Теперь
// отсутствие говорит своим значением (`kObjectTypeNone`), и ни с одним
// настоящим типом оно не сравнится — то есть свидетели ниже начали ловить
// РАЗОШЕДШИЙСЯ СЛОВАРЬ (товар, которому нет строки каталога), чего прежде не
// умели: такой товар молча считался свалочным.
constexpr ObjectType catalog_type_of(std::string_view id) {
    for (const ItemDef& d : kCatalog) {
        if (std::string_view(d.id) == id) return d.type;
    }
    return kObjectTypeNone;
}
constexpr ObjectArchetype catalog_archetype_of(std::string_view id) {
    return object_type_archetype(catalog_type_of(id));
}
constexpr int commodity_rows_of_archetype(ObjectArchetype a) {
    int n = 0;
    for (const CommodityDef& c : kCommodities) {
        if (catalog_type_of(c.id) != kObjectTypeNone
            && catalog_archetype_of(c.id) == a) {
            ++n;
        }
    }
    return n;
}
// У КАЖДОГО ТОВАРА СЛОВАРЯ ЕСТЬ СТРОКА КАТАЛОГА — закон связи двух таблиц,
// который сентинел сделал ВЫРАЗИМЫМ. Пока отсутствие читалось как `Misc`,
// разъезд словаря с каталогом был невидим компилятору вовсе.
constexpr bool every_commodity_has_a_row() {
    for (const CommodityDef& c : kCommodities) {
        if (catalog_type_of(c.id) == kObjectTypeNone) return false;
    }
    return true;
}
static_assert(every_commodity_has_a_row(),
              "товар словаря без строки каталога: словарь и каталог разъехались "
              "(прежде такой товар молча считался свалочным)");
static_assert(commodity_rows_of_archetype(ObjectArchetype::Food) == 1,
              "the dictionary must carry EXACTLY ONE food row — it IS the "
              "hunger row every eater asks for (econ_day.h "
              "hunger_commodity_ordinal)");
static_assert(commodity_rows_of_archetype(ObjectArchetype::Goods) >= 1,
              "at least one comfort row must exist — the citizen's value "
              "budget is divided BY their count (econ_day.h "
              "season_comfort_units)");

// WHY THE YIELD IS 128 (owner's question and pick, 2026-09-18: «из 1 — 32
// обосновано? по массе, по стоимости? есть вариант 128» → «128 норм»): the
// grounding is MASS, and both numbers are already in the table. A coin
// weighs 10 g (the weight of a real silver penny) and a unit of ore weighs
// 4000 g, so a 128-coin batch is 1280 g of struck metal out of 4000 g of
// rock: an ORE GRADE of 32 %, a rich ore or a smelted concentrate.
// The guard below is the metal content itself: a struck batch may never
// outweigh the rock it came out of, so the yield can be retuned freely and
// the day it stops being physical the table refuses to compile.
// СЧЁТ ЦЕЛЫЙ, И ИМЕННО ЭТО ТЕПЕРЬ ДЕЛАЕТ СВИДЕТЕЛЯ ТОЧНЫМ: прежде он сравнивал
// два флоата, собранных из непредставимых 0.01 кг, то есть мог соврать на ulp
// у самой границы. В граммах граница есть граница: 128 × 10 г = 1280 г битой
// монеты из 4000 г руды — те самые 32 % содержания металла. `int64` взят под
// произведение (худшее 128 × 64 000 = 8.2·10⁶, но тип должен держать и будущую
// партию, а не нынешнюю).
constexpr bool mint_yield_is_mass_honest() {
    for (const PartsAuthoringRow& r : kPartsAuthoring) {
        if (r.yield <= 1) continue;          // not a struck row
        const std::int64_t out =
            std::int64_t(r.yield) * catalog_mass_g_of(r.id);
        std::int64_t in = 0;
        for (const auto& p : r.p) {
            if (p.mat) in += std::int64_t(p.n) * catalog_mass_g_of(p.mat);
        }
        if (in <= 0 || out > in) return false;
    }
    return true;
}
static_assert(mint_yield_is_mass_honest(),
              "a struck batch cannot outweigh its own ore: yield x coin "
              "weight <= the composition's weight (the 8% ore grade)");

// ── ЗАКОН ПЛОТНОСТИ НЕ МОЖЕТ ВЫРОДИТЬСЯ: ДВА ОТКАЗА СОБИРАТЬСЯ (M-298) ────
// Страж `item_mass_g` охраняет ОБЛАСТЬ ОПРЕДЕЛЕНИЯ порядка (при w = 0 обе
// ветви перекрёстного сравнения вырождаются, и строка выигрывает платёж
// ВСЕГДА). Но страж — вторая линия: настоящий носитель закона ЗДЕСЬ, потому
// что каталог виден компилятору только в этой единице трансляции (§8 п.6 —
// лучший свидетель есть нарушение, которое невозможно собрать).
//
// ПЕРВЫЙ отказ: ни одна строка мира не легче шага колонки, то есть вход, на
// котором M-298 вообще жил, НЕВЫРАЗИМ, а не «обработан». Строка с нулевой
// массой теперь не компилируется — и это сильнее всякого прогона.
// Отсчёт идёт от ПЕРВОЙ СТРОКИ, а не от именованного потолка: потолок был бы
// константой, которая ничего не покупает, — а такую ЗАКОН КОНСТАНТ велит
// устранять, а не обосновывать.
constexpr std::int32_t catalog_min_mass_g() {
    std::int32_t m = kCatalog[0].massG;
    for (const ItemDef& d : kCatalog) {
        if (d.massG < m) m = d.massG;
    }
    return m;
}
static_assert(catalog_min_mass_g() > kMassStepG,
              "строка каталога легче шага колонки массы: закон плотности "
              "вырождается на ней, и она выигрывала бы КАЖДЫЙ платёж мира "
              "(M-298). Масса строки есть колонка, а не умолчание");

// ВТОРОЙ отказ: страж ТОЖДЕСТВЕНЕН на живом каталоге, то есть порядок платежа
// он меняет РОВНО НА НОЛЬ. Это и есть то, что паритет `balance_run` может
// только НАБЛЮДАТЬ на трёх сидах, — здесь оно утверждено на всех 44 строках
// сразу и навсегда.
constexpr bool mass_guard_is_identity_on_catalog() {
    for (const ItemDef& d : kCatalog) {
        if (item_mass_g(d) != d.massG) return false;
    }
    return true;
}
static_assert(mass_guard_is_identity_on_catalog(),
              "страж массы сдвинул бы живой порядок платежа: на каталоге он "
              "обязан быть тождественным (M-298)");

const std::unordered_map<std::string, const ItemDef*>& catalog_map() {
    static const std::unordered_map<std::string, const ItemDef*> m = []{
        std::unordered_map<std::string, const ItemDef*> r;
        for (const auto& d : kCatalog) r.emplace(d.id, &d);
        return r;
    }();
    return m;
}

// ── Loot tables (copied verbatim from TS) ──────────────────────

struct LootEntry {
    const char* item;
    float       chance;
    int         min;
    int         max;
    int         minLevel;  // 0 = unrestricted
};

// ХАРДКОД-ТАБЛИЦ ЛУТА СУЩЕСТВ ЗДЕСЬ БОЛЬШЕ НЕТ (вердикт владельца
// 2026-09-26, M-139: «сноси все говны … захардкоженый говносрак на
// существо? конечно сноси»). Стояло ДЕСЯТЬ таблиц и 33 авторские строки
// «что роняет эта роль»: peasant, woodcutter, merchant, caravan, bandit,
// guard, witch, sorceress, wildlife, demons. Это был тот же род дефекта, что
// кошелёк: список вещей, приписанный РОЛИ, мимо единой таблицы объектов и
// мимо стоимости. Кто что несёт, будет решать ПУЛ ЛУТА как РЕЖИССЁР — по
// стоимости, весам единой таблицы, тегам и контексту («CONTENTUS EX NIHILO»,
// владелец). До него павшие роняют РОВНО то, что несли, а спавн кладёт в
// сумку пусто — дыра названа в M-139.

// PROP_LOOT — the world's own things, not its inhabitants. A felled tree, and
// later a broken boulder or a rifled cairn, resolves through the SAME registry
// a kill does: the prop's kind names a loot profile (`structure_loot_id()` in
// sub/map_data.h), the profile rolls items. Nothing about breaking a prop is
// code — add a row here and a name there and the thing drops.
// ONE exchange rate for gathered matter (owner verdict 2026-09-12, CANON
// «Вердикты ТРУДА»: «1 рабочий рубит 1 дерево… 32 древесины с 32 деревьев»):
// 1 felled tree = 1 wood — exactly what the macro crew's take already debits
// from the tree layer (field_move, 1:1). The 2–5 roll this
// replaces made the subworld axe ~3.5× more generous than the world's own
// law, which is a money printer with extra steps.
constexpr LootEntry kTreeLoot[] = {
    {"wood",  1.00f, 1, 1, 0},
};
// A wheat stand pays its one grain (the same 1:1 the macro harvest takes);
// the harvest door still scales by the stand's own height against the
// kind's reference (sub/map_data.h yieldRefHeightM) — so an unripe stalk
// honestly pays nothing.
constexpr LootEntry kCropLoot[] = {
    {"food", 1.00f, 1, 1, 0},
};

// ── Unified loot registry ──────────────────────────────────────
// ONE table keyed by stable string `lootId`. Every drop resolves through
// here. The 8 NPC roles reuse the kNpcLoot tables above; factions map to the
// fauna tables. `"bandits"` reuses the bandit NPC table so a Bandits-faction
// creature drops real items instead of nothing (the old zero-loot bug).
struct LootProfile {
    const char*      id;
    const LootEntry* data;
    std::size_t      n;
};

#define SM_LOOT_PROFILE(id_, tbl) {id_, tbl, sizeof(tbl) / sizeof(LootEntry)}
constexpr LootProfile kLootProfiles[] = {
    // ТОЛЬКО ВЕЩИ МИРА. Обе строки — не выдача, а ВЫПЛАТА ПЕРЕНОСА: срубленное
    // дерево платит ровно то дерево, которое ведомость макро-стока списала с
    // клетки (`macro_stock_apply(TreeCount, -1)`), сжатый стебель — ровно то
    // зерно (`CropCount, -1`). Профилей РОЛЕЙ здесь больше нет (M-139).
    SM_LOOT_PROFILE("tree",       kTreeLoot),
    SM_LOOT_PROFILE("crop",       kCropLoot),
};
#undef SM_LOOT_PROFILE

// ── The affix table (owner's design 2026-09-07) ───────────────────────────
// What the random half of the one issuance door (grant_affixes) rolls from.
// A row is a NAME over an address: which bonus row it writes, at what scale
// (step), how it prices (per unit, for value_of), and how likely it is on
// each item type. The weights are a FULL matrix with no zeros (owner:
// «на всё возможно всё — броня вполне может дать урон, просто нетипично»):
// mechanically every affix is just a modifier, so the table states taste,
// never possibility. A new item type is a new weight column; a new affix is
// a new row; neither is code.
//
// `bonusRow` 0 is the ONE class row, "of Mastery": it resolves to the
// item's own skill column at grant time (a sword's Sword, a future flail's
// whatever the flail row says — new weapons join the affix system by
// existing). No skill column means Unarmed, the same fallback the strike
// law already answers for a swung sack of grain.
struct AffixDef {
    const char*  key;
    const char*  name;          // what the item's title wears: "of Strength"
    std::uint8_t bonusRow;      // BonusId ordinal; 0 = the item's own skill
    std::uint8_t step;          // bonus points per rolled unit
    std::uint8_t pricePerUnit;  // value_of's column (coin per rolled unit)
    // ВЕС ВЫБОРА ПО АРХЕТИПУ ВЕЩИ, в порядке самой оси:
    //   Weapon, Armor, Potion, Food, Material, Goods, Currency, Vehicle, Misc.
    // Длина — ВЫВЕДЕННАЯ (`kThingArchetypeCount@src/tables/object_archetype.h`,
    // то есть граница воплощаемого отрезка), а не литерал и не второе
    // перечисление: строка аффикса обязана высказаться о КАЖДОМ архетипе вещи,
    // и молчаливый ноль здесь означал бы «этому архетипу аффиксы не
    // достаются», сказанное не вслух. Воплощаемых колонок здесь НЕТ ВОВСЕ, и
    // это не упущение: у строки существа нет `ItemDef`, поэтому до двери
    // выдачи она не доходит — колонка для неё была бы обещанием
    // несуществующей двери.
    //
    // ЧТО СДВИНУЛОСЬ ПРИ ПЕРЕЕЗДЕ НА ОСЬ (M-295), ЧЕСТНО: колонки
    // ПЕРЕСТАВЛЕНЫ, не переоценены. Прежний порядок был `ItemType` (…Material,
    // Misc, Goods, Currency), новый — порядок архетипов, и `Vehicle` получила
    // вес бывших товаров, потому что повозка им и была до этого наряда.
    //
    // ЦЕННОСТЬ — НОЛЬ, И ЭТО СКАЗАНО ВСЛУХ: монета не надевается (slotMask 0),
    // а дверь выдачи аффиксов принимает только носимое, так что ненулевой вес
    // здесь был бы обещанием, которого дверь не исполняет. Тем же молчанием
    // живут Potion/Food/Material/Vehicle — их единицы держат закон «без нулей»
    // честным на случай, если носимое зелье однажды появится.
    std::uint8_t weight[kThingArchetypeCount];
};

constexpr AffixDef kAffixDefs[] = {
    // ── attributes: at home anywhere, fondest of jewelry ──────────────────
    {"of_strength",  "of Strength",  std::uint8_t(BonusId::Str),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_endurance", "of Endurance", std::uint8_t(BonusId::End),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_will",      "of Will",      std::uint8_t(BonusId::Wil),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_intellect", "of Intellect", std::uint8_t(BonusId::Intl), 1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_wisdom",    "of Wisdom",    std::uint8_t(BonusId::Wis),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_luck",      "of Luck",      std::uint8_t(BonusId::Lck),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_charisma",  "of Charisma",  std::uint8_t(BonusId::Cha),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    {"of_speed",     "of Speed",     std::uint8_t(BonusId::Spd),  1, 25, { 6,  6,  1,  1,  1,  8,  0,  8,  8}},
    // ── the class row: the item's own skill ───────────────────────────────
    {"of_mastery",   "of Mastery",   0,                           1, 15, { 8,  1,  1,  1,  1,  1,  0,  1,  1}},
    // ── armour columns: the physical TWO at home on armour (Slash folded
    // into Pierce 2026-10-01, M-193 — «of Slash Warding» died with it) ────
    {"of_pierce_warding", "of Pierce Warding", std::uint8_t(BonusId::ArmorPierce), 1, 10, { 1,  6,  1,  1,  1,  2,  0,  2,  2}},
    {"of_blunt_warding",  "of Blunt Warding",  std::uint8_t(BonusId::ArmorBlunt),  1, 10, { 1,  6,  1,  1,  1,  2,  0,  2,  2}},
    // ...and the elemental six, rarer everywhere (the schools' wards)
    {"of_fire_warding",   "of Fire Warding",   std::uint8_t(BonusId::ArmorFire),   1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    {"of_water_warding",  "of Water Warding",  std::uint8_t(BonusId::ArmorWater),  1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    {"of_air_warding",    "of Air Warding",    std::uint8_t(BonusId::ArmorAir),    1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    {"of_earth_warding",  "of Earth Warding",  std::uint8_t(BonusId::ArmorEarth),  1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    {"of_arcane_warding", "of Arcane Warding", std::uint8_t(BonusId::ArmorArcane), 1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    {"of_void_warding",   "of Void Warding",   std::uint8_t(BonusId::ArmorVoid),   1, 10, { 1,  3,  1,  1,  1,  3,  0,  3,  3}},
    // ── the derived outputs ───────────────────────────────────────────────
    {"of_wounding",  "of Wounding",  std::uint8_t(BonusId::DmgFlat),  1, 20, { 8,  1,  1,  1,  1,  1,  0,  1,  1}},
    {"of_quickness", "of Quickness", std::uint8_t(BonusId::SwingPct), 5,  8, { 4,  1,  1,  1,  1,  2,  0,  2,  2}},
    {"of_the_wind",  "of the Wind",  std::uint8_t(BonusId::MovePct),  5,  8, { 1,  3,  1,  1,  1,  4,  0,  4,  4}},
    {"of_the_mule",  "of the Mule",  std::uint8_t(BonusId::CarryKg),  5,  3, { 1,  4,  1,  1,  1,  3,  0,  3,  3}},
};

// The class row's resolution: a SkillId to its rank's bonus ordinal, through
// the registry itself rather than a parallel map that could drift.
constexpr std::uint8_t skill_bonus_row(SkillId s) {
    const SkillId use = s != SkillId::Count ? s : SkillId::Unarmed;
    for (const BonusDef& d : kBonusDefs) {
        if (d.target == BonusTarget::SkillRank
            && d.index == std::uint8_t(use)) {
            return std::uint8_t(d.id);
        }
    }
    return 0;
}
// Every skill the catalog can name resolves to a registry row — at compile
// time, so a skill added without its bonus row trips here, not in a drop.
static_assert(skill_bonus_row(SkillId::Count) != 0,
              "the Unarmed fallback must exist in the bonus registry");

const LootProfile* loot_profile(const char* lootId) noexcept {
    if (!lootId || !lootId[0]) return nullptr;
    for (const auto& p : kLootProfiles) {
        if (std::strcmp(lootId, p.id) == 0) return &p;
    }
    return nullptr;
}

inline std::vector<ItemRef> roll_loot(const LootEntry* table, std::size_t n,
                                        int level, RngFn rng,
                                        std::uint8_t affixPower) {
    std::vector<ItemRef> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const LootEntry& e = table[i];
        if (e.minLevel > 0 && level < e.minLevel) continue;
        if (rng() < e.chance) {
            const int qty = e.min + static_cast<int>(rng() *
                static_cast<float>(e.max - e.min + 1));
            ItemRef r{};
            const int idx = item_index(e.item);
            if (idx < 0) continue;      // a profile naming an unknown row drops
            r.def = std::uint16_t(idx);
            r.count = qty;
            // Every wearable stack meets the one issuance door on its way
            // out (it refuses bread and ore itself: an affix is a worn
            // thing). The world's byte is the caller's word.
            grant_affixes(r, affixPower, rng);
            out.push_back(r);
        }
    }
    return out;
}

} // namespace

// ── Public API ────────────────────────────────────────────────

// THE ordinal of an authoring id. Resolved through the same map the def
// lookup uses, and cached by the caller — the runtime record carries the
// number, never the string.
int item_index(const char* id) noexcept {
    if (!id || id[0] == '\0') return -1;
    const auto& m = catalog_map();
    const auto it = m.find(id);
    return it == m.end() ? -1 : int(it->second - kCatalog);
}

int item_index(const std::string& id) noexcept { return item_index(id.c_str()); }

std::size_t loot_profile_count() noexcept {
    return sizeof(kLootProfiles) / sizeof(kLootProfiles[0]);
}

const char* loot_profile_id(std::size_t i) noexcept {
    return i < loot_profile_count() ? kLootProfiles[i].id : "";
}

const ItemDef* item_def_at(int idx) noexcept {
    return (idx >= 0 && idx < int(std::size(kCatalog))) ? &kCatalog[idx]
                                                        : nullptr;
}

// ГРАНИЦА РОДОВ СТРОКИ МИРА (M-73): предмет — всё, что ниже размера ЭТОГО
// каталога. Единственное определение; двери macro/world_row.h строятся на нём.
bool world_row_is_item(std::uint16_t row) noexcept {
    return row < std::uint16_t(std::size(kCatalog));
}

const ItemDef* item_def(const std::string& id) noexcept {
    const auto& m = catalog_map();
    const auto it = m.find(id);
    return it == m.end() ? nullptr : it->second;
}

std::span<const ItemDef> item_catalog() noexcept {
    return std::span<const ItemDef>(kCatalog, std::size(kCatalog));
}

// ── The matter law's resolved form and the reaction doors ──────────────────

namespace {

// kPartsAuthoring resolved once into per-ordinal rows — the same
// string-authors/ordinal-runs contract as the catalog map above. A row the
// authoring table does not name stays {n = 0} = terminal.
struct ResolvedParts {
    ItemPart p[kMaxItemParts]{};
    int      n = 0;
    int      yield = 1;
    int      perDay = 0;   // 0 = not made by work (terminal)
};

const std::array<ResolvedParts, std::size(kCatalog)>& parts_table() {
    static const auto kTable = [] {
        std::array<ResolvedParts, std::size(kCatalog)> t{};
        for (const PartsAuthoringRow& row : kPartsAuthoring) {
            const int idx = item_index(row.id);
            // An unknown id or material is a DEAD row here, and the witness
            // (item_parts_test) reddens on it — this builder must not fail a
            // process over an authoring typo, the test exists to.
            if (idx < 0) continue;
            ResolvedParts& out = t[std::size_t(idx)];
            out.yield = row.yield > 1 ? row.yield : 1;
            out.perDay = row.perDay > 0 ? row.perDay : 1;
            for (const auto& part : row.p) {
                if (!part.mat || part.n <= 0) continue;
                const int mi = item_index(part.mat);
                if (mi < 0) continue;
                out.p[out.n++] =
                    ItemPart{std::uint16_t(mi), std::uint8_t(part.n)};
            }
        }
        return t;
    }();
    return kTable;
}

} // namespace

std::span<const ItemPart> item_parts(int defIdx) noexcept {
    const auto& t = parts_table();
    if (defIdx < 0 || defIdx >= int(t.size())) return {};
    const ResolvedParts& r = t[std::size_t(defIdx)];
    return std::span<const ItemPart>(r.p, std::size_t(r.n));
}

int item_yield(int defIdx) noexcept {
    const auto& t = parts_table();
    if (defIdx < 0 || defIdx >= int(t.size())) return 1;
    return t[std::size_t(defIdx)].yield;
}

int item_labour(int defIdx) noexcept {
    const auto& t = parts_table();
    if (defIdx < 0 || defIdx >= int(t.size())) return 0;
    return t[std::size_t(defIdx)].perDay;
}

std::vector<ItemRef> roll_loot_profile(const char* lootId, int level, RngFn rng,
                                       std::uint8_t affixPower) {
    const LootProfile* p = loot_profile(lootId);
    if (!p) return {};
    return roll_loot(p->data, p->n, level, rng, affixPower);
}

std::uint8_t affix_power(int level, std::uint8_t danger) {
    // Two contributions, one byte: 8 per level past the first and the danger
    // byte verbatim. Здесь стоял ТРЕТИЙ член — «богатство места» со срезом 64,
    // — и он умер 2026-10-18 вместе со своим источником (M-223, вердикт
    // владельца 2026-10-06). Его обоснование ссылалось на «the purse law»,
    // которого нет с 2026-09-26: кошелёк строки снесён вместе с монетами из
    // воздуха (M-139), и модулировать стало нечего. Писателя у третьего входа
    // не было ни одного — все звонки передавали 1.0, то есть ноль прибавки.
    const int p = (level > 1 ? (level - 1) * 8 : 0) + int(danger);
    return std::uint8_t(p > 255 ? 255 : p);
}

void grant_affixes(ItemRef& item, std::uint8_t power, RngFn rng) {
    const ItemDef* def = item_def_at(int(item.def));
    // An affix is a WORN thing: the door refuses what cannot be worn, so
    // every caller may offer every stack without knowing the distinction.
    if (!def || def->slotMask == 0) return;
    const int n = coin_run(rng, kAffixCountBase + int(power),
                           kAffixChanceDen, kMaxItemAffixes);
    if (n <= 0) return;
    // Колонка выбора — АРХЕТИП вещи, спрошенный через лестницу (строка несёт
    // тип, тип несёт архетип). Носимое воплощаемым не бывает по построению
    // (`catalog_is_all_things` под каталогом), поэтому индекс в отрезок вещей
    // законен без гейта.
    const int t = int(object_type_archetype(def->type));
    int total = 0;
    for (const AffixDef& a : kAffixDefs) total += int(a.weight[t]);
    for (int i = 0; i < n; ++i) {
        // Weighted pick down the archetype's own column. The clamp covers the
        // rng contract hole (core/rng.h may return exactly 1.0f).
        int roll = int(rng() * float(total));
        if (roll >= total) roll = total - 1;
        const AffixDef* pick = &kAffixDefs[0];
        for (const AffixDef& a : kAffixDefs) {
            roll -= int(a.weight[t]);
            if (roll < 0) { pick = &a; break; }
        }
        std::uint8_t row = pick->bonusRow;
        if (row == 0) row = skill_bonus_row(def->skill);   // "of Mastery"
        const int units = 1 + coin_run(rng, kAffixValueBase + int(power),
                                       kAffixChanceDen,
                                       kAffixValueCapUnits - 1);
        item.set_affix(i, Bonus{row, std::int16_t(units * int(pick->step))});
    }
    // The seed marks a ROLLED instance: it never merges with the plain row,
    // and two same-day twins stay two items (the stacking law reads it).
    if (item.seed == 0) {
        const std::uint32_t hi = std::uint32_t(rng() * 65536.0f);
        const std::uint32_t lo = std::uint32_t(rng() * 65536.0f);
        item.seed = ((hi << 16) | lo) | 1u;
    }
}

std::size_t affix_def_count() noexcept {
    return sizeof(kAffixDefs) / sizeof(kAffixDefs[0]);
}

const char* affix_def_key(std::size_t i) noexcept {
    return i < affix_def_count() ? kAffixDefs[i].key : "";
}

namespace {

// The affix-table row that speaks for a stored bonus row — how a saved
// {row, value} finds its name and its price without carrying either. A
// skill row (what "of Mastery" writes: Sword, Staff, …) has no fixed row of
// its own and answers with the class row, exactly the one that wrote it.
const AffixDef* affix_def_for_row(std::uint8_t row) {
    if (row == 0 || row >= std::uint8_t(BonusId::Count)) return nullptr;
    for (const AffixDef& a : kAffixDefs) {
        if (a.bonusRow == row) return &a;
    }
    if (bonus_def(BonusId(row)).target == BonusTarget::SkillRank) {
        for (const AffixDef& a : kAffixDefs) {
            if (a.bonusRow == 0) return &a;
        }
    }
    return nullptr;
}

} // namespace

int affix_count(const ItemRef& item) noexcept {
    int n = 0;
    for (int i = 0; i < kMaxItemAffixes; ++i) {
        const Bonus b = item.affix_at(i);
        if (b.row != 0 && b.value != 0) ++n;
    }
    return n;
}

// ── СКОЛЬКО СТОИТ ОБЪЕКТ — ОДИН ВОПРОС, ЗНАЧИТ ОДНА ДВЕРЬ, И ОНА ОТВЕЧАЕТ ЗА
// ОБЕ ПОЛОВИНЫ ЕДИНОГО ПРОСТРАНСТВА СТРОК (M-292, вердикт владельца
// 2026-10-20) ─────────────────────────────────────────────────────────────
// Владелец, дословно: «это тупа стоимость у сущчества как у предмета (типа
// абсолютная стоимость) раз у предметов есть и у них единая таблица объектов
// то просто туда же пишем у существа»; и о найме — «ну и сразу беслпатно цнеа
// найма и тд отсюда».
//
// До этой правки дверь резолвила ТОЛЬКО предметный каталог (`item_def_at`), и
// на строке существа тот отдаёт `nullptr` → ноль. Ноль был не ответом, а
// немотой, и на нём стояла вся торговля: прилавок рисовал слот существа пустым
// фоном и некликабельным, счёт пакета молча пропускал строку, то есть
// существом нельзя было торговать ПО ПОСТРОЕНИЮ — при том, что контейнер один
// и число у существа есть (ЗАКОН КОНТЕЙНЕРА).
//
// ВТОРОГО ЧИСЛА НЕ ЗАВЕДЕНО: цена существа — колонка его строки `hireGold`
// (`src/tables/npc.h`, CANON S25: «a creature's price is a column of its row,
// exactly like a sword's»), а уровень множит её ТОЙ ЖЕ единственной дверью,
// какой множит найм, — `hire_price_for`. Поэтому «абсолютная стоимость» и
// «цена найма» здесь не два числа, которые надо согласовывать, а ОДНО,
// спрошенное из двух мест: владелец этого и просил словом «отсюда».
//
// АФФИКСОВ У СУЩЕСТВА НЕТ ПО ПОСТРОЕНИЮ (`grant_affixes` предметная: она
// отказывает по `slotMask`), поэтому хвост ниже обходить не надо — он
// молчит сам, и ветки «а у существа аффиксов не бывает» здесь нет.
//
// СТРОКА БЕЗ ЦЕНЫ ОСТАЁТСЯ НУЛЁМ, И ЭТО ТЕПЕРЬ ОТВЕТ, А НЕ НЕМОТА: 43 строки
// существ из 47 несут `hireGold == 0`, потому что их вывод (`upkeepGoldPerDay
// × 30`) на беззарплатном звере даёт ноль. Волк на полке ВИДЕН и КЛИКАБЕЛЕН,
// а стоит столько, сколько говорит его строка. Ценовка этих 43 строк —
// КОНТЕНТ с блёстом в живую экономику (`hireGold` читают цель охоты
// `npc_ai.cpp` и запах богатства), и она названа отдельной строкой реестра.
int value_of(const ItemRef& item) noexcept {
    if (world_row_is_creature(item.def)) {
        return hire_price_for(
            std::uint16_t(creature_of_world_row(item.def)), int(item.level));
    }
    const ItemDef* def = item_def_at(int(item.def));
    int v = def ? def->value : 0;
    for (int i = 0; i < kMaxItemAffixes; ++i) {
        const Bonus b = item.affix_at(i);
        if (b.row == 0 || b.value == 0) continue;
        const AffixDef* a = affix_def_for_row(b.row);
        if (!a || a->step == 0) continue;
        // Units recovered by the row's own step; a curse subtracts (and the
        // floor below keeps a ruined thing at worthless, never a debt).
        v += (int(b.value) / int(a->step)) * int(a->pricePerUnit);
    }
    return v < 0 ? 0 : v;
}

const char* affix_suffix(const ItemRef& item) noexcept {
    for (int i = 0; i < kMaxItemAffixes; ++i) {
        const Bonus b = item.affix_at(i);
        if (b.row == 0 || b.value == 0) continue;
        if (const AffixDef* a = affix_def_for_row(b.row)) return a->name;
    }
    return "";
}

} // namespace sm
