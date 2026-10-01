// КАТАЛОГ ПРЕДМЕТОВ — строки и форма ссылки на них (наряд M-181, разрез
// items: каталожная половина здесь, анкетная — Inventory и двери над ним —
// в macro/anketa.h). Контент, constexpr, одинаковый по обе стороны границы
// миров; состояния мира этот файл не знает и не смеет знать.
//
// `ItemRef` ЛЕЖИТ ЗДЕСЬ, И ЭТО РЕШЕНИЕ С ПРИЧИНОЙ: он не состояние, а ФОРМА
// ССЫЛКИ на строку каталога (ординал + материал + уровень + аффиксы), и его
// ПРОИЗВОДЯТ каталожные двери (roll_loot_profile возвращает ItemRef'ы,
// grant_affixes пишет в него). Каталог выдаёт ссылки на свои строки; анкета
// их ХРАНИТ.
//
// ItemType order is save-relevant and fixed:
//   ItemType { Weapon=0, Armor=1, Potion=2, Food=3, Material=4, Misc=5 }
// Stacking semantics: at most one entry per id (addItem stacks quantity).
//
// Catalog: the faction coin nominals, consumables (potion_hp/mp, food_meat),
// monster materials (mat_bone/hide/herb), equipment (wpn_dagger/sword/spear/
// axe/mace/staff, arm_leather), misc_gem — plus EVERY commodity noun of the
// economy (tables/commodity.h, owner's one-dictionary ruling). Составы и счёт
// НЕ ПЕРЕСКАЗЫВАЮТСЯ ЗДЕСЬ: списки в шапке — второй словарь, и этот уже
// разъезжался (он звал `grain` и `bread`, которых в словаре нет, и насчитал
// 14 товаров при выведенном `kCommodityCount`). Смотреть таблицы.
// Товар, который делает город, и товар в сумке игрока — ОДНА строка; id и веса
// сходятся с commodity.h дословно (закон связи в econ_v1_test).
// New rows APPEND — a saved ItemRef carries the ordinal.

#pragma once
#include "core/dice.h"
#include "tables/attributes.h"
#include "tables/bonus.h"
#include "tables/damage_types.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sm {

// КАТЕГОРИЯ ВЕЩИ — единственный словарь «что это такое» (владелец,
// 2026-09-18: «у нас теперь просто система итемов единая и точка»). Категория
// несёт ЯВНЫЙ СМЫСЛ, ведущий к канонической системе (AGENTS правило 9):
//
//   Material — РЕСУРСЫ: из них крафтят (составы kPartsAuthoring, добыча полей
//              ресурсов). Эта половина закона работала правильно и раньше.
//   Goods    — ТОВАРЫ: то, что мир ПРОИЗВОДИТ и в чём нуждается (лестница
//              нужд, дань, рынок). До 2026-09-18 восемь произведённых строк и
//              двенадцать монет лежали в Misc — то есть в категории «прочее»,
//              у которой смысла нет по определению, и вопрос «что это» имел
//              второй ответ в отдельной товарной таблице.
//   Food     — ЕДА: та категория, в которой живёт ГОЛОДНАЯ строка лестницы.
//   Weapon / Armor / Potion — что тело носит, надевает и пьёт.
//   Misc     — честное «прочее»: вещь, которая не служит ни одной из систем
//              выше (самоцвет из клада). Строка сюда попадает НАМЕРЕННО, а не
//              потому, что её некуда было деть.
enum class ItemType : std::uint8_t {
    Weapon   = 0,
    Armor    = 1,
    Potion   = 2,
    Food     = 3,
    Material = 4,
    Misc     = 5,
    Goods    = 6,
    // ЦЕННОСТЬ — РОД ВЕЩИ, А НЕ ФЛАГ НА ТОВАРЕ (владелец, 2026-09-26: «У
    // МОНЕТЫ БУДЕТ ТЕГ дорогое или куренси или драгоценность»; «почему нельзя
    // тип ценность?»). Заведён потому, что `Goods` отвечал СРАЗУ НА ДВА
    // вопроса: «это товар, которого место хочет по лестнице нужд» и «это
    // деньги». Пока монеты лежали в `Goods`, любой проход по товарам (спрос
    // места, M-137) заставлял бы горожанина НУЖДАТЬСЯ В МОНЕТАХ по своей доле
    // бюджета. Ничего «денежного» этот род не включает: платёж по-прежнему
    // идёт плотностью стоимости (currency.h pay_value_dense), и монета
    // выигрывает арифметикой веса, а не родом.
    Currency = 7,
};

// Can this kind be swallowed for its bonuses (use_item's gate, the UI's
// "Use" button)? ONE answer — it used to be written out twice, in items.cpp
// and in the inventory overlay.
inline constexpr bool item_type_consumable(ItemType t) {
    return t == ItemType::Potion || t == ItemType::Food;
}

// How a weapon row's blow TRAVELS (owner verdicts 2026-09-09, CANON S14
// «СТРЕЛЬБА/МЕТАНИЕ»). Melee swings at reach; Missile looses a projectile
// down the aim line the instant it is asked — NO ammo of any kind (the ARPG
// conceit: «даже в M&B стрелы восполняются каждый бой») — and pays the same
// body recovery gate from the same mass law as a swing; Thrown is the
// reserved third value (the item itself flies — awakens after the demo).
enum class Delivery : std::uint8_t {
    Melee   = 0,
    Missile = 1,
    Thrown  = 2,
};

// 8 (owner verdict 2026-09-07, affix track): the random ladder above 4 is
// astronomically rare (each further affix pays ÷4), but ARTIFACTS are fixed
// SETS through the same door and a designed set wants the room — cheaper to
// state once now than to move the save format twice. In bytes it is +8 per
// slot next to the pair-of-structs era (see the SoA note on ItemRef).
inline constexpr int kMaxItemAffixes = 8;

// What a row DOES, as rows of the one bonus registry (tables/bonus.h).
//
// It was a struct of six named ints — `hp, mp, sp, str, end, agi` — and half
// of it was fiction: nothing anywhere read `str`, `end` or `agi`, so the
// dagger's authored "+2 STR when equipped" and the leather's "+2 END" did
// nothing at all, and `agi` named an attribute the sheet does not even have
// (it is `spd`). As many cells as an item INSTANCE carries (kMaxItemAffixes):
// a catalog row and a rolled affix say the same kind of thing, so they say it
// in the same words — and a unique's innate set gets the same room a rolled
// set does.
inline constexpr int kMaxItemBonuses = kMaxItemAffixes;

// Static blueprint — `ItemDef` mirrors `ItemDef = Omit<Item, 'quantity'>`.
struct ItemDef {
    const char* id          = "";
    const char* name        = "";
    ItemType    type        = ItemType::Misc;
    int         value       = 0;     // gold price
    float       weight      = 0.0f;  // kg per single
    const char* icon        = "?";   // unicode glyph
    const char* description = "";
    // Zeroed cells are empty: BonusId::None is row 0, so a row that grants
    // nothing writes nothing.
    Bonus       bonus[kMaxItemBonuses] = {};

    // ── Where it goes, and what it costs to wear ──────────────────────────
    // `slotMask` is an OR of `part_bit(BodyPartId::X)` (tables/body_parts.h):
    // the TYPES of body part this can sit on. A ring says "Finger" ONCE and
    // fits an octopus with twenty of them; that is the whole reason the mask
    // names types and not indices. 0 = cannot be worn at all, which is every
    // potion and every sack of grain.
    std::uint64_t slotMask   = 0;
    // ...and what wearing it ALSO occupies. A two-hander is the case this
    // exists for: it sits in the main grip and takes the off hand with it.
    // 0 = takes only its own cell.
    std::uint64_t blocksMask = 0;
    // WHAT IT STOPS — the two-column `Defense` (tables/damage_types.h): eight
    // columns of percent armour and eight of flat block, same units as damage
    // and as a creature row's own defence, because all three meet in ONE law
    // (mitigate_amount). Rows authored as one number use uniform_armor(x) /
    // uniform_defense(armour, block); per-column authoring (a fire-warding
    // cloak) is what the columns are FOR. The ARMOUR half is a percent and
    // takes a rank multiplier happily; the BLOCK half is flat, so its authored
    // ceiling is 2·kArmorHalving = 20 and not the type's 255 (CANON S13).
    Defense       defense{};

    // ── What a WEAPON row deals (CANON S13: урон = NdM строкой предмета) ──
    // dice{0,1} (every non-weapon) rolls nothing; dmgType names the armour
    // column the blow argues with; skill names WHICH skill this row is
    // GOVERNED BY — one question, one column (owner verdict 2026-09-19):
    // on a Weapon row it is the weapon skill that multiplies a strike made
    // with it (SkillId::Count = none — the wielder falls back to Unarmed,
    // honest for a swung sack of grain), and on an ARMOR row it is the
    // armour skill whose rank multiplies what this piece STOPS (Heavy /
    // Light / Shield — CANON S14 «ранг множит защиту своего типа»).
    // A second `armorKind` column beside this one would be two dictionaries
    // answering the same question, which S26 forbids.
    // There is deliberately NO tempo column: a weapon's swing time is
    // DERIVED from its `weight` above (weapon_swing_seconds ниже
    // — owner verdict 2026-09-07: «скорость привязать к массе»), so every
    // future row gets its pace for free from the one kilogram figure the
    // carry law already prices, and the two can never disagree.
    Dice          dice{};
    DamageType    dmgType   = DamageType::Blunt;
    SkillId       skill     = SkillId::Count;
    // How the blow travels (enum above). A Missile row's damage deliberately
    // takes NO attribute add (hand_strike_fields, owner verdict 2026-09-09):
    // dice + typed skill + LCK only — range is the compensation, and a future
    // firearm is this exact law with fatter flat dice.
    Delivery      delivery  = Delivery::Melee;
    // How far a loosed missile is aimed and flies (grid units; the projectile
    // door derives its lifetime from this). 0 on melee rows — their reach
    // stays the engine's arm's-length constant.
    float         range     = 0.0f;
    // СКОЛЬКО ЗАНИМАЕТ ГЛОТОК, в секундах базы (CANON S13 «в субмире всё —
    // способность», вердикт владельца 2026-09-19). Выпить зелье в бою —
    // действие, а не бесплатный жест: база делится дверью восстановления
    // (Spd × Армсмастер — это руки), и в пошаговом режиме глоток честно
    // отдаёт миру свой ход. 0 у всего, что не пьют и не едят: ноль —
    // частный случай закона, а не отсутствие закона.
    //
    // СТОИТ В КОНЦЕ СТРУКТУРЫ НЕ СЛУЧАЙНО: каталог авторится ПОЗИЦИОННЫМИ
    // списками, и колонка, вставленная в середину, молча съезжает в соседа —
    // ровно это и случилось при первой попытке (дальность лука уехала в эту
    // ячейку, и range стал 0). Новая колонка ItemDef приписывается СЮДА.
    float         useSeconds = 0.0f;
};

// An affix IS a bonus (tables/bonus.h `Bonus`): a row of the one registry and
// how much of it. The name stays because "affix" is what a rolled modifier on
// an item is CALLED, but the type is the same one a perk and an aura carry.
// Since the SoA move (v82) it is the CURRENCY of affix_at/set_affix rather
// than the stored shape — the instance keeps rows and values in two flat
// arrays and hands them out as this pair.
using ItemAffix = Bonus;

struct ItemRef {
    std::uint16_t def = 0;         // catalog ordinal
    // 1 + raw row of the commodity dictionary (tables/commodity.h); 0 = the
    // row's own default. The +1 exists because raw row 0 (wood) is a real
    // material and 0 must keep meaning "unset". At SCRAP the byte substitutes
    // part 0 of the row's composition (owner verdict 2026-09-11): a steel
    // sword returns steel, not the row's default iron.
    std::uint8_t  material = 0;
    // ЕДИНАЯ КОЛОНКА УРОВНЯ ЭКЗЕМПЛЯРА (владелец, 2026-09-24, эпик единой
    // таблицы): у предмета — прежнее «качество» (0 = обычный), у существа —
    // уровень. Одна лестница дальше ведёт зоны сложности и дроп («существо
    // уровня N сыплет вещи ~того же уровня, с весами»). Ширина u8 — кап 255.
    std::uint8_t  level = 0;
    std::int32_t  count = 0;       // 0 = THIS SLOT IS EMPTY
    std::uint32_t seed = 0;        // 0 = plain, not procedurally rolled
    // ИМЕННОЙ ЭКЗЕМПЛЯР (слот В эпика, владелец 2026-09-24): 0 = безликий
    // стак; ≠0 — экземпляр с историей, к которому мир обращается по номеру:
    // душа существа (К-3), артефакт, квестовая вещь. Раздельно от seed
    // СОЗНАТЕЛЬНО: превращение существо↔предмет (окаменевший лорд → статуя)
    // везёт идентичность сквозь смену рода, не перетолковывая её в прокат.
    // Именное не стакуется (закон в same_kind_as ниже).
    std::uint32_t entityId = 0;
    // The affixes, SoA: rows in one flat array, values in another. The array
    // of {u8 row, i16 value} pairs this replaces paid a padding byte per cell
    // to alignment — a third of the affix block spent on nothing, in the one
    // struct the game keeps 1024 × per container. Two arrays carry the same
    // facts at 3 bytes a cell exactly, and the save writes them without the
    // padding it used to (v82). Readers go through affix_at/set_affix, so the
    // layout is this struct's own business.
    std::uint8_t  affixRow[kMaxItemAffixes]{};
    std::int16_t  affixValue[kMaxItemAffixes]{};

    bool empty() const { return count == 0; }
    Bonus affix_at(int i) const {
        return Bonus{affixRow[i], affixValue[i]};
    }
    void set_affix(int i, Bonus b) {
        affixRow[i] = b.row;
        affixValue[i] = b.value;
    }
    // Everything except the count — the whole stacking rule. ИМЕННОЕ НЕ
    // СТАКУЕТСЯ НИКОГДА: экземпляр с историей (entityId ≠ 0) — это count == 1
    // по построению (К-3), и слить две истории в один стак значит потерять
    // одну из них молча.
    bool same_kind_as(const ItemRef& o) const {
        if (entityId != 0u || o.entityId != 0u) return false;
        if (def != o.def || material != o.material || level != o.level
            || seed != o.seed) {
            return false;
        }
        for (int i = 0; i < kMaxItemAffixes; ++i) {
            if (affixRow[i] != o.affixRow[i]
                || affixValue[i] != o.affixValue[i]) {
                return false;
            }
        }
        return true;
    }
};

// The byte price is a stated fact, not a discovery: 16 of header + 8 rows +
// 16 values = 40, no padding (слот В эпика единой таблицы, владелец
// 2026-09-24). A field added without reading this line trips here instead of
// silently growing every container in the game by kilobytes.
static_assert(sizeof(ItemRef) == 40, "ItemRef grew — reprice the containers");

// The catalog ordinal of an authoring id, or -1. Strings name rows in tables;
// nothing compares them per tick.
int item_index(const char* id) noexcept;
int item_index(const std::string& id) noexcept;
// The row an ordinal names; nullptr when the ordinal is out of the catalog.
const ItemDef* item_def_at(int idx) noexcept;

// ГРАНИЦА РОДОВ СТРОКИ МИРА — У САМОГО КАТАЛОГА (M-73, одно пространство
// ординалов). Строка мира ниже размера предметного каталога есть ПРЕДМЕТ,
// всё выше — существо. Определение живёт рядом с каталогом (items.cpp);
// все ОСТАЛЬНЫЕ двери пространства ординалов (macro/world_row.h) строятся
// НА этой — второго вывода границы не существует. Контейнеру (macro/anketa.h
// Inventory) она нужна потому, что закон РАЗМЕЩЕНИЯ слота зависит от рода
// строки.
bool world_row_is_item(std::uint16_t row) noexcept;

// Catalog accessor. Returns nullptr if id is unknown (TS returns a
// dummy "Unknown item" — we expose the lookup so callers can decide).
const ItemDef* item_def(const std::string& id) noexcept;

// Enumerate the entire item catalog — the single source of truth for item
// ids. Callers (e.g. the dev console `give`) iterate this rather than
// re-listing ids, so a new catalog row is instantly available everywhere.
std::span<const ItemDef> item_catalog() noexcept;

// ── THE matter law: what a row is MADE OF (owner verdicts 2026-09-11) ──────
// CANON «Крафт/Скрап»: the game is resource-oriented — every item is
// materialised raw matter, and craft/scrap/production are ONE reversible
// reaction over ONE table. That table is the catalog itself: a row's
// composition lives HERE, as up to kMaxItemParts pairs {catalog ordinal,
// count}, so the 40-byte instance grows by nothing and the one answer feeds
// four consumers — the city's production day (econ_day), the craft door, the
// scrap door, and the AI's overflow scrap (macro/anketa.h).
//
// A row with NO parts is TERMINAL — raw matter that "consists of itself"
// (ore, hide, gems, meat, coins). Parts may reference ONLY terminal rows, so
// the reaction is always one step deep; recursion cannot exist to be guarded.
// 4 slots: the wagon wants wood+iron today, a composite armour wants
// hide+iron+cloth tomorrow — 12 cold bytes per catalog row buy the headroom
// «на века» (owner verdict; most rows author 1–2).
inline constexpr int kMaxItemParts = 4;
struct ItemPart {
    std::uint16_t def   = 0;   // catalog ordinal of a TERMINAL row
    std::uint8_t  count = 0;   // units of it in ONE batch (see item_yield)
};
// The row's composition; an empty span = terminal. Resolved once from the
// authoring table in items.cpp (strings author, ordinals run).
std::span<const ItemPart> item_parts(int defIdx) noexcept;
// How many items one BATCH of the composition makes (owner verdict
// 2026-09-12, «привести к единой системе»). 1 for nearly every row; the
// coin rows say {silver 1} → 32 — which UNIFIES the mint: «состав монеты и
// есть монетный двор», and melting coin is the same scrap door as melting a
// sword. The witness pins value-neutrality (yield × coin value == the
// composition's value), so this column and the price anchor cannot drift
// apart unseen.
int item_yield(int defIdx) noexcept;

// The row's LABOUR: batches one person-day of work runs (owner verdicts
// 2026-09-12, «единая SP-система труда»). ONE number serves three workers:
//   · the CITY prices its production day with it (econ_produce_day — a
//     person-day is what a population buys, «чем больше населения, тем
//     больше SP на дела»);
//   · the HAND pays SP with it: one person-day = one FULL SP bar (the
//     anchor kGatherPerWorkerDay is already derived from the quarter-bar
//     work cycle), so a batch costs maxSp / labour — into the negative,
//     with the march's own exhaustion bite («как в Elin», no second labour
//     law);
//   · a future harvest/craft SKILL multiplies through this same column
//     (owner: «потом расширяемо») — appended, not designed now.
// 0 = the row is not made by work (terminal raw matter).
int item_labour(int defIdx) noexcept;

// ── Закон МАССЫ удара и строка кулака (переехали из macro/anatomy.h, M-183:
// это законы КАТАЛОГА оружия — темп выводится из веса СТРОКИ, никогда не
// авторится второй колонкой) ───────────────────────────────────────────────
// The bare fist's row (owner verdict 2026-09-05): 1d2 Blunt through the
// Unarmed skill — a fist is useless against plate, and that is the hybrid
// law's threshold branch doing its work, not a bug.
inline constexpr Dice kFistDice{1, 2};

// A weapon's base tempo is DERIVED from its weight, never authored twice:
// «скорость привязать к массе — масса кулака бесконечно мала, он самый
// быстрый» (owner verdict 2026-09-07).
//   base = kHandSwingS + weight_kg × kSwingSecondsPerKg
// kHandSwingS — the EMPTY hand at a zero sheet, the game's slowest striker.
inline constexpr float kHandSwingS = 1.5f;
// One kilogram of steel costs half a second of swing: pins the 1 kg dagger
// at 2.0 s and the implied 2 kg one-hand sword at 2.5 s.
inline constexpr float kSwingSecondsPerKg = 0.5f;

// THE base swing of whatever the hand holds. nullptr = the bare, massless
// hand. Weapons only — a spell's base tempo is its own row's cooldown, and a
// creature's natural weapon carries an authored cooldown in ITS row.
inline float weapon_swing_seconds(const ItemDef* w) {
    return kHandSwingS + (w ? w->weight : 0.0f) * kSwingSecondsPerKg;
}

// Loot generation. `rng()` returns float in [0, 1).
using RngFn = float (*)();

// ── THE coin run — the one shape of "how many" and "how strong" ───────────
// (owner verdict 2026-09-07: «везде где можно — единые формулы-константы»).
// Flip a p = num/den coin until it fails or the cap stops it; the count of
// heads is the answer. Geometric, smooth, no tiers: the same law says how
// many affixes a drop grows and how strong each one rolls, and a deeper
// power does not open new shelves — it bends the one curve. Every user
// quotes p as (base + power) / kAffixChanceDen with its own base below.
inline int coin_run(RngFn rng, int num, int den, int cap) {
    int n = 0;
    while (n < cap && rng() * float(den) < float(num)) ++n;
    return n;
}

inline constexpr int kAffixChanceDen  = 512;
// How many: at power 0 a field hare's dagger rolls an affix 1-in-32; at 255
// every second deep drop carries one, and the SAME coin keeps flipping for
// the 2nd..8th (p² , p³ … — multi-affix finds live in the deep end's curve,
// not in a rule).
inline constexpr int kAffixCountBase  = 16;
// How strong: the value coin starts three times friendlier (25%..75% across
// the power span), so "+1 usually, the bigger the rarer" holds everywhere
// and the tail is exponential rather than shelved.
inline constexpr int kAffixValueBase  = 128;
// The roll's own ceiling, in UNITS of the affix row's step (one constant for
// every row — «на века»): 16 units ≈ a tenth of the endgame's ~100-point
// scores at step 1. It caps what LUCK can write, not what the format holds —
// an int16 value cell lets a designed artifact state numbers the coin never
// will.
inline constexpr int kAffixValueCapUnits = 16;

// The world's one affix-power byte: how loaded the dice are where this thing
// dropped. Level pulls 8/level, the danger byte rides in whole, the place's
// wealth adds up to 64 («богатство = стоимость, шире золота» — the third
// term is the owner's verdict, not a double-count of the purse law).
std::uint8_t affix_power(int level, std::uint8_t danger, float wealthMul);

// ── THE affix writer (owner's design 2026-09-07) ──────────────────────────
// One door for every way an item gains modifiers. The RANDOM path — this
// call — is the loot roll's special case: pick rows from the affix table by
// the weight column of this item's type (no zeros — armour CAN roll damage,
// it is merely untypical), roll each value on the coin above, stamp the seed
// so the stacking law separates it. A FIXED issuance (artifact, quest
// reward, a script) writes the same cells through set_affix and never calls
// the coin — «рандом = частный случай выдачи».
// Non-wearables (slotMask 0) are refused: an affix is a WORN thing.
void grant_affixes(ItemRef& item, std::uint8_t power, RngFn rng);

// The affix table's own listing, for printers (the dev console) — same
// contract as loot_profile_count/loot_profile_id.
std::size_t affix_def_count() noexcept;
const char* affix_def_key(std::size_t i) noexcept;

// How many affix cells actually say something — the number the UI tint and
// the suffix read.
int affix_count(const ItemRef& item) noexcept;

// What THIS instance is worth: the row's price plus every affix cell priced
// per point of its bonus row («чем реже, тем ценнее» made arithmetic). A
// plain stack answers exactly def->value, so nothing in the economy moves.
// Floored at 0 — a cursed thing is worthless, not a debt.
int value_of(const ItemRef& item) noexcept;

// The title's tail, from the affix table's own name column: the FIRST
// speaking cell names the instance ("Rusty Dagger of Strength"). "" when
// plain.
const char* affix_suffix(const ItemRef& item) noexcept;

// МОНЕТЫ НА ТРУПЕ БОЛЬШЕ НЕ РОЖДАЮТСЯ (M-139, вердикт владельца 2026-09-26).
// Здесь стояла `generate_loot_gold` — кошелёк строки × опасность клетки ×
// богатство места — и две её константы (`kDangerLootGain`, `kLootLevelGain`),
// у которых других читателей не было. Контекст ЖИВ и переехал в свою дверь:
// `LootContext@src/macro/loot_pool.h`. Здесь он звался `CorpseLootContext` и
// не имел НИ ОДНОГО читателя во всём дереве — контекст без двери, которая его
// читает, то есть ровно та колонка-сирота, которую запрещает DOD п.9. Три
// его колонки сходятся в `affix_power` и отвечают на «насколько богата вещь»,
// а не «сколько монет из воздуха».
// (generate_settlement_inventory is gone: the unified-container moment it was
// kept for arrived — landmark stocks are seeded by the ECONOMY's own law,
// econ_day.h seed_landmark_inventory, from the one commodity dictionary.)

// ── Unified loot table ─────────────────────────────────────────
// ONE loot registry keyed by a stable string `lootId`. Зарегистрированы
// ТОЛЬКО вещи мира — `tree` и `crop`: их строки есть выплата переноса из
// поля, 1:1 с тем, что ведомость макро-стока списала с клетки. Профили РОЛЕЙ
// (peasant..sorceress, wildlife/demons/bandits) снесены вердиктом владельца
// 2026-09-26 (M-139) вместе с колонкой `lootId` строки существа: что несёт
// тварь, решит ПУЛ ЛУТА по стоимости, весам и контексту. Unknown / empty
// id => no items.
// `affixPower` — the context byte the wearable stacks roll their affixes on
// (affix_power above). NOT defaulted on purpose: a new call site must state
// what the world says there, and the compiler asks where a comment could not.
std::vector<ItemRef> roll_loot_profile(const char* lootId, int level, RngFn rng,
                                       std::uint8_t affixPower);

// The registry's own listing — for printers (the dev console's `loots`), so
// the id list is never restated anywhere. Index order is the table's.
std::size_t loot_profile_count() noexcept;
const char* loot_profile_id(std::size_t i) noexcept;

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла, и это не небрежность:
// заголовок пользуется контуром только в блоке ниже, а вставленная
// наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила бы
// каждую ссылку документов в этот файл (замер: одна такая вставка в 17
// заголовков сломала 10 ссылок в SKELETON.md и промтах). Номер строки —
// производное (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::ItemDef);
TIMAERT_ROW(sm::ItemRef);
TIMAERT_ROW(sm::ItemPart);
