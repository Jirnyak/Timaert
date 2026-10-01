// КАТАЛОГ атрибутов и скиллов — КОНТЕНТ, а не анкета (наряд M-181, фаза 2).
//
// Здесь живёт то, что одинаково по обе стороны границы двух миров: какие
// атрибуты и скиллы ЕСТЬ в игре, что каждый из них значит и сколько процентов
// даёт ранг. Одна строка каталога, сколько угодно носителей — ровно как гоблин
// одной строкой и куча гоблинов в мире.
//
// ЧИСЛА КОНКРЕТНОГО ТЕЛА здесь НЕ живут: `Attributes`, `Skills`, `LevelData`,
// `PerkMask` — колонки АНКЕТЫ (`macro/anketa.h`), то есть род 2 пакетной шины,
// гладкий массив сквадов. Направление включения одностороннее: анкета читает
// каталог, каталог об анкете не знает (AGENTS §11).
//
// Schema (CANON S14, finalized 2026-09-03): 8 attributes (str/end/int/wil/spd/
// lck/cha/wis — VIT and PER died in the canon session; their work is re-dealt:
// END owns HP AND half of SP, WILL owns MP and the other half), the skill list
// in registry groups.
//
// Naming: `int` is reserved in C++; the attribute is named `intl` (kept short
// since the sheet's array is hot data). `wil` reads WILL on the sheet.
#pragma once
#include "core/table_guard.h"
#include <cstddef>
#include <cstdint>

namespace sm {

// ── The id spaces ──────────────────────────────────────────────
// Declared FIRST because the blocks below are addressed BY them: a sheet's
// ranks are a flat array and its meanings are rows, and both index by these.
enum class AttributeId : std::uint8_t {
    Str, End, Intl, Wil, Spd, Lck, Cha, Wis,
    Count
};

// The canon skill list (S14, 2026-09-03), in registry groups. ORDINALS ARE
// FOREVER from kSaveVersion 78 on — append at the END, never insert.
// Leadership is deliberately absent: its work is undecided (NOT a squad cap —
// CANON S14), and a row without a law would be a liar; it appends later
// without moving the save. The old `Fighter` became `Armsmaster` (same idea,
// the canon name): the generic multiplier ON TOP of the weapon skills.
enum class SkillId : std::uint8_t {
    // weapons (7) — rank multiplies damage DONE WITH that weapon type
    Sword, Axe, Spear, Mace, Dagger, Bow, Staff,
    // armor (4) — rank multiplies protection OF that armor type
    HeavyArmor, LightArmor, Unarmored, Shield,
    // magic schools (6, S15) — rank multiplies power of the school's spells
    FireMagic, WaterMagic, AirMagic, EarthMagic, ArcaneMagic, VoidMagic,
    // the generic pair — a SMALLER percent on top of the typed skills
    Armsmaster, Spellcraft,
    // body (5)
    Bodybuilding, Meditation, Marathon, Athletics, Weightlifting,
    // road & world (4)
    Travel, Acrobatics, Scouting, Prospecting,
    // husbandry (4)
    Trade, Quartermaster, Foraging, Learning,
    // the EIGHTH weapon (owner verdict 2026-09-05, appended v79 — ordinals
    // are forever): the bare fist is a weapon type like any other, so an
    // unarmed monk is a build and not a gap in the law.
    Unarmed,
    // РЕМЁСЛА (владелец, 2026-09-18) — ЧЕТЫРЕ строки. Это НЕ новая система:
    // система скиллов и система крафта уже есть, а эти четыре — та колонка, по
    // рангу которой открывается рецепт (econ_day.h kRecipes: craft + minRank).
    // Ремесло названо по МАТЕРИИ, с которой работают руки, а не по товару: одно
    // ремесло владеет многими рецептами, и «сколько их и на каких рангах» —
    // контент, который дорастёт.
    //
    // ГОТОВКИ ЗДЕСЬ БОЛЬШЕ НЕТ — ВЫРЕЗАНА 2026-09-28 (вердикт владельца, M-148).
    // Её единственным рецептом был хлеб, а после «ЕДА НЕ ПРОИЗВОДИТСЯ — ЕДА
    // ДОБЫВАЕТСЯ» (CANON S10) рецепта у неё не могло появиться НИКОГДА: она не
    // «ждала контента», она была мертва ПО ЗАКОНУ — и полгода стояла рангом 30
    // у города и деревни, не открывая ничего. Ординалы ремёсел при этом
    // СДВИНУЛИСЬ, и это законно: формат сейва ограничением не является.
    Blacksmith, Tailoring, Masonry, Alchemy,
    Count
};

// ── Attributes: WHAT A SCORE MEANS ─────────────────────────────

// What an attribute IS, in the sheet's own words. The character panel walks
// these rows instead of keeping a sixth copy of the same eight facts.
struct AttributeDef {
    // MUST equal the row's index in kAttributeDefs (guard below the table).
    AttributeId id;
    const char* key;      // authoring id; runtime addresses by ordinal
    const char* label;    // the short name a player reads
    const char* effect;   // what one point buys
};

// The canon eight (S14, 2026-09-03), in the canon's own order. END and WILL
// each feed half of SP — the one bar with two owners, deliberately: the
// warrior and the mage come to stamina from opposite sides, the hybrid wins.
// LCK's reader is the dice door (S13; lands with the dice phase).
inline constexpr AttributeDef kAttributeDefs[] = {
    {AttributeId::Str,  "str",  "STR", "+1 physical damage, +10 kg carry per point"},
    {AttributeId::End,  "end",  "END", "+10 max HP, +5 max SP per point"},
    {AttributeId::Intl, "intl", "INT", "+1 spell damage per point"},
    {AttributeId::Wil,  "wil",  "WILL", "+10 max MP, +5 max SP per point"},
    {AttributeId::Spd,  "spd",  "SPD", "Asymptotic movement speed"},
    {AttributeId::Lck,  "lck",  "LCK", "Shifts the game's dice in your favor"},
    {AttributeId::Cha,  "cha",  "CHA", "1% off prices and payroll per point"},
    {AttributeId::Wis,  "wis",  "WIS", "+1% EXP bonus per point"},
};
static_assert(sizeof(kAttributeDefs) / sizeof(kAttributeDefs[0])
                  == std::size_t(AttributeId::Count),
              "kAttributeDefs must carry one row per AttributeId");
static_assert(rows_in_enum_order(kAttributeDefs, &AttributeDef::id),
              "kAttributeDefs rows must stand in AttributeId order");

inline constexpr const AttributeDef& attribute_def(AttributeId id) {
    return kAttributeDefs[std::size_t(id)];
}

// ── THE SKILL LAW ──────────────────────────────────────────────
//
// Attributes are what a body IS; skills are what it has been TRAINED to do.
// So attributes add and skills multiply — mastery framing raw nature — and the
// multiplier is stated the same way for every skill in the game:
//
//     ONE RANK = ONE PERCENT, and a rank is capped at kMaxSkillRank (100).
//
// A rank therefore reads directly as the percentage it grants: "Travel 37" is
// -37% terrain stamina, "Athletics 37" is +37% speed, no formula in the reader's
// head and none in the balancer's. Linear and capped on purpose: an asymptotic
// curve (which two of these used to be) cannot be balanced by reading it, and a
// cap makes the ceiling a design decision instead of an accident.
//
// 100 rather than a power of two: nothing indexes an array by rank, so a
// po2 bound buys nothing here, while "rank == percent" buys legibility every
// time anyone reads a sheet. (It still fits a byte if ranks are ever packed.)
//
// The cap is reachable, and meant to be: the player earns ONE skill point per
// level across eight skills, so rank 100 is a hundred levels poured into a
// single mastery. What it grants at that point — up to doubling what the skill
// governs, or, for a cost skill, removing that cost entirely — is a capstone,
// not an exploit.
constexpr int kMaxSkillRank = 100;

// What ONE RANK of a skill is worth, and which way it pushes.
//
// `pctPerRank` is the column that made the law honest. CANON S14 (бывший rpg.md) and the canon
// audit (A7) both record the debt it settles: the law said "one rank is one
// percent, ceiling ×2", and four of the most expensive numbers in the game —
// maxHp, maxMp, and both raw damages — were computed inline at 0.05 per rank
// with no clamp, so bodybuilding 100 gave ×6 HP while the doc promised ×2.
// The owner's ruling (2026-08-27) was to LEGITIMISE the per-skill multiplier
// as a column rather than flatten every skill to 1 %. So the ceiling is now a
// DERIVED number and differs per row — bodybuilding tops out at ×6 because its
// row says 5 — and there is exactly one place that turns a rank into a
// multiplier, which is what the law was always about.
struct SkillDef {
    // MUST equal the row's index in kSkillDefs (guard below the table).
    SkillId      id;
    const char*  key;          // authoring id; runtime addresses by ordinal
    const char*  label;        // what a human reads on the sheet
    const char*  effect;       // what it does, in the sheet's own words
    std::uint8_t pctPerRank;
    // A COST skill buys a price DOWN (1 - rank·pct/100, never past free); every
    // other skill multiplies a bonus UP (1 + rank·pct/100). One flag rather
    // than two helpers, because "which direction" is a property of the skill
    // and belongs in its row.
    bool         buysCostDown = false;
};

// Percent verdicts (owner, 2026-09-03 evening): TYPED skills (weapons, armor,
// schools) = 10 %/rank — capstone ×11 on your own type; the GENERIC pair =
// 5 %/rank, and what it buys is TEMPO, not power (вердикт 2026-09-07 «один рычаг
// на ручку» снял прежнюю роль «процент поверх итогового урона»; шапка обещала её
// до 2026-10-01 и врала — M-195). World-skill rows whose
// reader is a later phase (weapons → the damage door, schools → S15 wiring,
// Acrobatics/Scouting/Prospecting/Trade/… → the world readers) still state
// their law here: the row IS the design, the reader arrives once.
inline constexpr SkillDef kSkillDefs[] = {
    {SkillId::Sword,       "sword",       "Sword",
     "sword damage per rank",                 10},
    {SkillId::Axe,         "axe",         "Axe",
     "axe damage per rank",                   10},
    {SkillId::Spear,       "spear",       "Spear",
     "spear damage per rank",                 10},
    {SkillId::Mace,        "mace",        "Mace",
     "mace damage per rank",                  10},
    {SkillId::Dagger,      "dagger",      "Dagger",
     "dagger damage per rank",                10},
    {SkillId::Bow,         "bow",         "Bow",
     "bow damage per rank",                   10},
    {SkillId::Staff,       "staff",       "Staff",
     "staff damage per rank",                 10},
    {SkillId::HeavyArmor,  "heavy_armor", "Heavy Armor",
     "heavy armor protection per rank",       10},
    {SkillId::LightArmor,  "light_armor", "Light Armor",
     "light armor protection per rank",       10},
    // СПИТ (вердикт владельца 2026-09-19, сессия Е): три брата множат ВКЛАД
    // носимого рода, а у голого тела вклад НОЛЬ — множить нечего, и «ранг
    // множит защиту своего типа» на этой строке не читается ни одним телом.
    // Чтобы оно заработало, скилл должен СКЛАДЫВАТЬ броню (вторая форма
    // закона) или читать уклонение — механики, которой мир ещё не сделал.
    // Закон рамки скилла (S14) это прямо разрешает: строка ждёт свою систему
    // с pctPerRank = 0, как ждут ремёсла, а не лжёт процентом в тултипе.
    {SkillId::Unarmored,   "unarmored",   "Unarmored",
     "sleeps until the world has the mechanic it reads", 0},
    // ЩИТОВ В КАТАЛОГЕ ПРЕДМЕТОВ НЕТ НИ ОДНОГО (замер 2026-10-01, M-195:
    // офф-грип строки `items.cpp` — это оружие с пустой защитой). Строка живёт и
    // закон её читает (`sheet_armor_mult_pct@src/macro/anketa.h` и ранг рода
    // надетой вещи), но управлять ей сегодня нечем: это ожидание КОНТЕНТА, а не
    // спящая механика, и сказано вслух, чтобы ноль не был молчаливым.
    {SkillId::Shield,      "shield",      "Shield",
     "shield block per rank",                 10},
    {SkillId::FireMagic,   "fire_magic",  "Fire Magic",
     "fire spell power per rank",             10},
    {SkillId::WaterMagic,  "water_magic", "Water Magic",
     "water spell power per rank",            10},
    {SkillId::AirMagic,    "air_magic",   "Air Magic",
     "air spell power per rank",              10},
    {SkillId::EarthMagic,  "earth_magic", "Earth Magic",
     "earth spell power per rank",            10},
    {SkillId::ArcaneMagic, "arcane_magic", "Arcane Magic",
     "arcane spell power per rank",           10},
    {SkillId::VoidMagic,   "void_magic",  "Void Magic",
     "void spell power per rank",             10},
    // ГЕНЕРИК-ПАРА — ТЕМП, А НЕ УРОН, и строки это говорят с 2026-10-01 (M-195).
    // Прежний текст обещал «ALL physical damage per rank», и это была ЛОЖЬ: роль
    // «процент поверх итогового урона» (вердикт 2026-09-03) ОТМЕНЕНА вердиктом
    // 2026-09-07 «один рычаг на ручку» — одна ручка, посчитанная и в силу, и в
    // темп, есть скрытый квадрат, и причина записана на месте
    // (`calculate_derived@src/macro/anketa.h`). Единственные читатели сегодня —
    // дверь восстановления: Армсмастер ускоряет ЛЮБОЕ физическое действие
    // (замах, выстрел, глоток) и сокращает ПРОСТОЙ НАДЕТОЙ БРОНИ (M-194),
    // Спеллкрафт ускоряет любой каст.
    //
    // ОГОВОРКА, БЕЗ КОТОРОЙ ПРАВКА БЫЛА БЫ НЕВЕРНОЙ: у зверя, чья ОРУЖЕЙНАЯ
    // колонка и есть `Armsmaster` (`kRoleWeights@src/tables/role_weights.h` —
    // «its body IS the weapon»), он множит урон ЗАКОННО, как типовой скилл того
    // тела, а не как генерик.
    {SkillId::Armsmaster,  "armsmaster",  "Armsmaster",
     "speed of ALL physical acts, and armour's downtime, per rank", 5},
    {SkillId::Spellcraft,  "spellcraft",  "Spellcraft",
     "speed of ALL casting per rank",          5},
    {SkillId::Bodybuilding, "bodybuilding", "Bodybuilding",
     "max HP per rank",                        5},
    {SkillId::Meditation,  "meditation",  "Meditation",
     "max MP per rank",                        5},
    // Owner ruling, Session 21: the BAR belongs to attributes alone (END and
    // WILL by half each since the canon eight), so this skill shortens the
    // REST instead. (Was `endurance`, +5 % max SP — a multiplier that
    // double-counted the attribute.)
    {SkillId::Marathon,    "marathon",    "Marathon",
     "SP recovery rate per rank",              1},
    {SkillId::Athletics,   "athletics",   "Athletics",
     "move speed per rank",                    1},
    {SkillId::Weightlifting, "weightlifting", "Weightlifting",
     "carry capacity per rank",               10},
    // How FAR you get on one bar, never how fast (movement_cost.h): a cost
    // skill, and the reason the flag exists.
    {SkillId::Travel,      "travel",      "Travel",
     "terrain stamina cost per rank",          1, /*buysCostDown*/true},
    {SkillId::Acrobatics,  "acrobatics",  "Acrobatics",
     "jump height per rank",                   1},
    {SkillId::Scouting,    "scouting",    "Scouting",
     "track-field reading per rank",           1},
    {SkillId::Prospecting, "prospecting", "Prospecting",
     "deposit sense per rank",                 1},
    {SkillId::Trade,       "trade",       "Trade",
     "final price edge per rank",              1},
    {SkillId::Quartermaster, "quartermaster", "Quartermaster",
     "squad payroll per rank",                 1, /*buysCostDown*/true},
    {SkillId::Foraging,    "foraging",    "Foraging",
     "provision drain per rank",               1, /*buysCostDown*/true},
    {SkillId::Learning,    "learning",    "Learning",
     "experience gained per rank",             1},
    // Appended v79 with its enum row — a weapon skill like the seven above.
    {SkillId::Unarmed,     "unarmed",     "Unarmed",
     "unarmed damage per rank",               10},
    // ── РЕМЁСЛА (2026-09-18) ────────────────────────────────────────────
    // pctPerRank = 0 СОЗНАТЕЛЬНО: ремесло не множит число, оно ОТКРЫВАЕТ
    // рецепт. Проценты — язык скиллов, которые усиливают удар или дешевят
    // цену; у этих строк власть иная, и врать процентом, которого нет, эта
    // таблица не станет (колонка `effect` говорит, чем строка на самом деле
    // распоряжается). Когда крафченая вещь станет ЛУЧШЕ от ранга — вот тогда
    // у ремесла появится свой процент, и появится он здесь.
    {SkillId::Blacksmith,  "blacksmith",  "Blacksmith",
     "unlocks smithing recipes by rank",       0},
    {SkillId::Tailoring,   "tailoring",   "Tailoring",
     "unlocks tailoring recipes by rank",      0},
    {SkillId::Masonry,     "masonry",     "Masonry",
     "unlocks masonry recipes by rank",        0},
    {SkillId::Alchemy,     "alchemy",     "Alchemy",
     "unlocks alchemy recipes by rank",        0},
};
static_assert(sizeof(kSkillDefs) / sizeof(kSkillDefs[0])
                  == std::size_t(SkillId::Count),
              "kSkillDefs must carry one row per SkillId");
// The table CARRIES its enum as a column, so a drifted row refuses to compile
// — the same guard biomes, moons and creature roles already stand behind.
static_assert(rows_in_enum_order(kSkillDefs, &SkillDef::id),
              "kSkillDefs rows must stand in SkillId order");

inline constexpr const SkillDef& skill_def(SkillId id) {
    return kSkillDefs[std::size_t(id)];
}

// THE skill law, and the ONE place a rank becomes a multiplier. Everything
// that a skill governs asks this and nothing else — no formula keeps a private
// curve, and no formula spells a percent inline. The direction and the percent
// are the row's; the CAP is the law's.
//
// Стоит В КАТАЛОГЕ, а не в анкете, и это не произвол: функция читает СТРОКУ и
// число, анкеты не касается вовсе. Перегрузки, которые берут `Skills`, живут в
// `macro/anketa.h` — там, где живёт сама анкета.
inline float skill_mult_of(SkillId id, int rank) {
    if (rank < 0) rank = 0;
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    const SkillDef& d = skill_def(id);
    const float step = float(rank) * float(d.pctPerRank) * 0.01f;
    if (!d.buysCostDown) return 1.0f + step;
    return step >= 1.0f ? 0.0f : 1.0f - step;   // a cost never goes past free
}

// The SAME law in whole percent, for the integer house (the strike assembly
// multiplies by multPct/100; the world readers scale counts and radii by
// pct/100). Both directions, exactly as the float door: a cost-down row
// walks DOWN and floors at free — Foraging 100 is a squad fed off the land,
// not a negative loaf.
inline int skill_mult_pct_of(SkillId id, int rank) {
    if (rank < 0) rank = 0;
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    const SkillDef& d = skill_def(id);
    const int step = rank * int(d.pctPerRank);
    if (!d.buysCostDown) return 100 + step;
    return step >= 100 ? 0 : 100 - step;        // a cost never goes past free
}

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
TIMAERT_ROW(sm::AttributeDef);
TIMAERT_ROW(sm::SkillDef);
