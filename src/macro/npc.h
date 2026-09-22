// NPC type registry — faithful port of `src/game/npc.ts`.
//
// Pure data tables. AI lives in macro/npc_ai.{h,cpp}.
// To add a new NPC type:
//   1. Add an enum value to `NPCType`.
//   2. Add an entry to `kNpcTypeDefs[]` with names, talk lines, combat.
//   3. (optional) Add an AI function in npc_ai.cpp and reference it via
//      the `AIBehaviour` enum below.
// No other file needs to change.
#pragma once
#include <array>
#include "core/table_guard.h"
#include "macro/behaviour.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "macro/army.h"
#include "macro/biomes.h"   // Biome — младшие биты ареала строки
#include "macro/damage_types.h"
#include "macro/spells.h"   // spell_ordinal — a casting row names its spell
#include "macro/sprite_rows.h"

namespace sm {

enum class NPCType : std::uint8_t {
    Peasant = 0, Merchant, Bandit, Guard, Witch, Sorceress,
    // The gatherer professions of the deposit rows (resources.md): a
    // profession per resource, appended so saved kinds stay stable.
    // ── and every creature, in the SAME space ─────────────────────────────
    // The monster catalog used to be a second table addressed by `0x100 | row`
    // — a second vocabulary written as a number. It is gone: a wolf is a row
    // here like a guard is, and "is this a monster" is not a question the
    // engine asks any more (owner, 2026-08-20). Appended, so saved ordinals
    // stay stable.
    Rabbit, Deer, Fox, Wolf, Bear, Boar, Snake, Hawk, Frog, Goat, Eagle, Croc,
    Goblin, Skeleton, Troll, SwampThing, IceWraith, SandScorpion, StoneGolem,
    // The PLAYER's own row (owner's ruling 2026-08-27: «игрок = обычный сквад,
    // просто с флажком»). His macro squad is an ordinary squad entity, and an
    // ordinary squad entity names a row of THIS table — so the player needed
    // one. Appended, so every saved ordinal stays where it was.
    Adventurer,
    // The village vendor (owner 2026-08-30): the crew that walks the home
    // surplus to the nearest city market. Appended, so saved ordinals stay.
    // The silver villages' man (CANON S10 чеканка): the same gatherer loop
    // as every profession — a row, never a branch. Appended.
    // The feudal graph's carrier (CANON S24): walks the town's tithe up to
    // its capital — «налог течёт по рёбрам носителями». Appended.
    TaxCollector,
    // The prologue's ambush (owner 2026-09-09): a bandit in every respect
    // that matters — same body, same fight, same loot — who WATCHES THE
    // WHOLE ROAD. Its own row rather than a tuned Bandit, so the world's
    // bandits keep their ordinary eyes and the opening scene keeps its
    // teeth: perception is a property of the creature, so a creature that
    // lies in wait is a creature, not a flag. Appended.
    RoadAmbusher,
    // The dragon (owner 2026-09-10, стол анкет): the first FLYING fighter —
    // cruiseM > 0 births ecs::Flying, the honest M&M flight the player has
    // (one envelope, one law), and the missile fireball is the ordinary
    // Missile columns every shooter row uses. Appended.
    Dragon,
    // ── The bestiary of the populated places (content, 2026-09-11) ────────
    // §42 gave every place its own population; a spire that fields three
    // hundred demons out of five species is a wall of the same silhouette.
    // These sixteen are the species that fill it — appended, so every saved
    // ordinal above stays where it is, and ordered weakest→strongest because
    // that is the order THE spawn law reads them in: a row's `spawn_strength`
    // is derived from its own hp × dps, and `danger_match` puts it on the
    // ground that deserves it. Nothing here is stronger than the dragon and
    // nothing is weaker than a rabbit, so the normalisation of the existing
    // rows is unmoved (verified row by row before authoring).
    GiantRat, CaveBat, Kobold, CaveSpider, Imp, Zombie, Orc, Ghoul, Harpy,
    Cultist, Gargoyle, Wraith, Ogre, Minotaur, Basilisk, Lich,
    // ЛОШАДЬ — ЮНИТ, А НЕ ПРЕДМЕТ (CANON S10, владелец 2026-09-19): the
    // pasture's yield is a CREATURE into the roster, so it is a row of THIS
    // table like every creature is — it eats by its upkeep column, hauls by
    // its haulMult, sells through the one hire door. Appended.
    Horse,
    Count,
};

// ЧТО ЭТА СТРОКА ЕСТЬ — ПРИРОДА, не роль (владелец, 2026-09-21: «это не роль
// в отряде, а типа природа... типа единая система»). ОДНА колонка на строку
// на все вопросы мира о том, чем является душа; роли из неё не торчат.
//
// Прежде здесь стоял тег РОЛИ (None/Mount), и он отвечал ровно на один
// вопрос — «ездовое ли». Природа отвечает на все сразу и каждым своим
// читателем:
//   Human — НАРОД мест: только эти души составляют население ландмарка,
//           размножаются и заселяют пустой город (владелец: «тип human, это
//           логично, потому что они все могут размножаться»);
//   Fauna — живность: не население, а имущество и МЯСО (нож излишка режет
//           именно её), и она же несёт спины под закон упряжки;
//   Void  — нежить и нечисть: ни народ, ни мясо.
// Ездовое НЕ стало четвёртым значением: «спина» — это уже своя колонка
// (haulMult), и лошадь ездовая потому, что её спина больше человеческой, а
// не потому, что кто-то назвал её ездовой вторым словом (S26).
enum class NpcNature : std::uint8_t {
    Human = 0,
    Fauna,
    Void,
};

// ЧТО ЭТА СТРОКА ДЕЛАЕТ В ОТРЯДЕ — РОЛЬ, и она отдельна от природы
// (владелец 2026-09-19 завёл тег, 2026-09-21 подтвердил: «по нему будем
// оценивать»). Природа говорит, ЧЕМ душа является; тег — КЕМ она служит в
// ростере. Роли не складываются: одна колонка, None = обычный боец.
//
// ПОЧЕМУ НЕ ВЫВОДИТСЯ ИЗ СПИНЫ (haulMult), как я было сделал: спина — это
// СКОЛЬКО он везёт, а не ЧЕМ он служит. Вывод «большая спина = вьючный»
// сразу потребовал приписки «и не человек» (строка Caravan несёт 32 спины),
// то есть выродился в правило с исключением. Ездовое называется словом.
enum class NpcTag : std::uint8_t {
    None = 0,
    // ЕЗДОВОЕ/ВЬЮЧНОЕ: не рука и не боец в первую очередь, а СПИНА под
    // человека. Отсюда закон упряжки — их столько же, сколько душ.
    Mount,
};

enum class NPCState : std::uint8_t {
    // `Patrolling` стояло шестым и умерло 2026-09-22 вместе с ai_patrol:
    // состояние без машины — колонка без писателя. Ординалы за ним
    // сдвинулись, и это безопасно: rt.state не едет ни в сейв, ни в снимок
    // (проверено), а все сравнения идут по имени.
    Idle = 0, Wandering, Traveling, Returning, Working, Chasing, Resting,
    // Running from a stronger hostile squad (Session 15): set by the universal
    // threat step in npc_ai.cpp, cleared by it when the threat is gone.
    // Runtime-only like every state here — the ECS is never serialized.
    Fleeing,
    // Walking to (and working) a NEW field parcel (owner 2026-08-31, CANON
    // S10 «фичи создаются сквадами»): the crew that found its field eaten
    // bare spends the rest of its bar ploughing the best cell near home.
    Plowing,
    // Walking to (and spanning) a one-cell water gap on the way to a vein
    // (owner 2026-08-31): the crew carries a day's gathering of timber or
    // stone and a day of work lays the bridge — the same feature system
    // the road planner builds with.
    Bridging,
};

enum class NPCTrait : std::uint8_t {
    Greedy = 0, Honorable, Cowardly, Brave, Aggressive, Generous, Suspicious, Curious,
    Count,
};

// (Реестр «нрав купца ↔ цена» — kTraitPriceRows — вырезан 2026-09-19 вместе
// с настроением места: коэффициент поверх цены это тот же спред ×0.7, от
// которого мир уже избавился. У сделки одна кривая и разница торговых сил.)


// Fixed-arity name / dialogue pools — POD-friendly.
constexpr std::size_t kMaxNpcNames     = 16;
constexpr std::size_t kMaxNpcTalkLines = 6;
constexpr int kNpcUpkeepNone = -1;

// The armour scale, the 9-type symmetry and THE mitigation law all live in
// macro/damage_types.h (kArmorHalving, ArmorProfile, mitigate_amount) — one
// home, because both laws of battle read them: the damage door
// (sub/damage.cpp) and the auto-resolve (auto_battle.h).

// ── БИТЫ АРЕАЛА: 0..10 — ординалы Biome, дальше производные классы ───────
// Стояли в fauna.h рядом со своей таблицей-спутником; переехали сюда
// 2026-09-22 вместе с колонкой, которую описывают (CANON S26 «Одна строка
// на род»). fauna.h их по-прежнему цитирует своими static_assert'ами —
// теперь как ЧИТАТЕЛЬ, а не как хозяин.
inline constexpr std::uint16_t kHabForest = 1u << 11; // forest-CLASS cell
inline constexpr std::uint16_t kHabRuin   = 1u << 12; // ruin denizen
inline constexpr std::uint16_t kHabSpire  = 1u << 13; // spire denizen
inline constexpr std::uint16_t kHabTown   = 1u << 14; // settlement crowd
inline constexpr std::uint16_t hab(Biome b) {
    return std::uint16_t(1u << std::uint16_t(b));
}

struct NpcTypeDef {
    // MUST equal the row's index in kNpcTypeDefs (guard below the table).
    NPCType         type;
    // Stable machine id — what the console, the spawn tables and any future
    // content file NAME this row by ("peasant", "wolf"). The creature rows
    // brought it with them; the humanoid rows had only a display label, and a
    // display string is not an id.
    const char*     id;
    const char*     label;
    // This kind's picture — a row of THE sprite table (macro/sprite_rows.h),
    // which decides drawn art vs procedural body. Kinds share rows on purpose:
    // every unremarkable townsman is a peasant to the eye. It replaced a dead
    // `portrait` path string that no code ever read — a fourth asset vocabulary
    // nobody was speaking.
    SpriteId        sprite;
    // NO baseHp column — the ONE hp floor a body fights with is its
    // CombatTemplate's `hp` (the sheet projection's own floor); a second
    // number here was never read.
    int             baseLevel;
    AIBehaviour     ai;
    CombatTemplate  combat;
    int             upkeepGoldPerDay;
    bool            hireable;
    // ONE XP law (owner, 2026-08-29): every kill pays npc_xp_reward =
    // xpReward + (level−1)·5, in the subworld and the auto-resolve alike —
    // the old exp_from_fight(lvl) = 10·lvl fallback for rows that named 0 is
    // dead. Creature rows author xpReward = 5·(baseLevel+1), DERIVED to keep
    // the old subworld feel at the row's own level:
    //     5·(L₀+1) + (L₀−1)·5 = 10·L₀  — exactly what 10·lvl used to pay.
    int             xpReward;

    // ── The wild half of the row ──────────────────────────────────────────
    // These four columns arrived with the creature catalog when the two body
    // tables merged (owner, 2026-08-20: one system, "лорд может быть не только
    // человеком но и драконом"). They are not "monster fields": they are the
    // questions the world asks about ANY row, and a townsman simply answers
    // them with the default.
    //
    // `weight` — how commonly the world rolls this row when it is asked for
    // something by weight rather than by name. It stays on the CREATURE, not
    // on the spawn table (owner: otherwise every dungeon would have to name
    // its bestiary by hand, and "give me a plausible enemy" would be
    // unsayable). 0 = never rolled blind; a place must name this row to get it.
    std::uint16_t   weight     = 0;
    // NO faction column — deliberately (owner ruling 2026-08-27: «в записи
    // существа вообще не должно быть фракции»). Faction is an INSTANCE
    // property (ecs::NPCKind.factionIdx), assigned at birth by the SPAWNER's
    // context: a town dresses its crowd in its kingdom's colours, a landmark
    // in its spawnFaction, a squad in its leader's, the open land in the
    // spawn law's own wildFaction column (macro/fauna.cpp). The same wolf can
    // be wildlife in a meadow, a demon in a ruin, or the player's own.
    // Loot profile override; nullptr = the faction default of the one loot
    // registry (macro/items.h).
    const char*     lootId     = nullptr;
    // Body radius in metres — THE one width column of the one body table
    // (damage-door Inc 4: CombatTemplate's shadow copy is gone). 0 = the
    // man-shaped default (npc_body_radius below); the creature rows author it
    // because a rabbit is not a man-sized thing. The same number scales the
    // creature's sprite, so visual size and hit size cannot drift.
    float           radius     = 0.0f;

    // Pools — first `nameCount` / `talkCount` entries are valid.
    std::array<const char*, kMaxNpcNames>     names;
    std::uint8_t                               nameCount;
    std::array<const char*, kMaxNpcTalkLines> talkLines;
    std::uint8_t                               talkCount;

    // Optional carried point light (a torch, lantern or arcane glow). Pure DATA
    // — the subworld renderer already lights any entity that carries an
    // ecs::LightEmitter through one universal gather, so a lit NPC needs no
    // engine change, only a spawn-time attach keyed off these fields (see
    // maybe_emplace_carried_light in sub/spawn.cpp). Kept as plain floats here
    // (not an ecs::LightEmitter) so the macro data layer has no dependency on
    // the ECS component headers. `lightRadius <= 0` ⇒ this type carries no light
    // (the default for every row that omits the field), so lighting a type is
    // strictly opt-in and costs nothing for the rest. Colour is linear RGB, the
    // offset seats the light on the body (metres), intensity is the scalar gain
    // — the same knobs the player lantern uses.
    // Default-initialised to "no light" so every row that omits them (all but
    // the ones that opt in) is dark and warning-free — a class with default
    // member initialisers is still an aggregate (C++14+), so the constexpr
    // brace-init of kNpcTypeDefs below is unaffected.
    float lightRadius    = 0.0f;   // attenuation reach (m); 0 = no carried light
    float lightIntensity = 0.0f;   // scalar gain
    float lightR = 0.0f, lightG = 0.0f, lightB = 0.0f;   // linear RGB radiance
    float lightHeight    = 0.0f;   // metres up from the feet the light is seated

    // How many BACKS this row hauls with. The overload law is UNIVERSAL now
    // (owner ruling, 2026-08-27: «перегруз универсальный всем»), and it was
    // written for a man and his pack — capacity is STR and athletics. A
    // caravan is not a man with a big rucksack, it is wagons and mules, and
    // charging its cargo against one merchant's shoulders priced commerce out
    // of the world the hour the law went universal: the first honest run of it
    // stranded every caravan on the map, and the trade test said so.
    //
    // So the answer is a COLUMN, not an exemption — the same law, applied to
    // the back the row actually has. 1 = a person, and every row that omits
    // this is a person. The number is DATA and the owner's to retune; nothing
    // reads it but the one capacity call (squad.h refresh_leader_travel_stats).
    // Last field on purpose, beside the light block: an opt-in column at the
    // end costs no other row a comma.
    float haulMult = 1.0f;


    // ARMOUR THE ROW IS WEARING — the crowd's defence, as ROW DATA rather
    // than as instances (owner ruling, 2026-08-27: «броня массовки = ЧИСЛО ИЗ
    // СТРОКИ»). A troll's hide and a guard's plate are what those rows ARE;
    // giving sixteen thousand bodies an equipment container each to say so
    // would be one fact stored ten thousand times. A body that also WEARS
    // things adds them on top of this — the same shape the authored body
    // radius has, where a creature's own number and the default meet at one
    // reader. All-zero (every row that omits it) is a body in its own skin.
    //
    // Nine columns since the 9×9 symmetry (CANON S13) — one per DamageType,
    // units the damage's own, because the two meet in mitigate_amount().
    // Scalar-era rows convert with uniform_armor(x) (mechanical translation,
    // owner verdict 2026-09-05); per-column authoring is content-stage work.
    ArmorProfile armor{};

    // WHAT A BODY OF THIS ROW COSTS to take into a roster, in gold at its
    // level-1 worth (CANON S25: a creature's price is a column of its row,
    // exactly like a sword's — not a formula living beside the item prices).
    // DERIVATION: the row's own upkeepGoldPerDay × 30 days — a recruit is
    // bought for a month of his pay, which is byte-for-byte the price
    // `hire_price_for` computed inline until 2026-08-29 (Peasant 1×30 = 30,
    // Guard 3×30 = 90). Level scales it through THE one level law
    // (army.h soldier_level_factor), applied by the reader; 0 — every row
    // that omits it, i.e. everything with no upkeep — is not for sale.
    int hireGold = 0;

    // Роль строки в ростере (NpcTag выше). Умолчание None: всякая строка,
    // которая молчит, — обычный боец. Последняя на месте: opt-in колонка в
    // хвосте не стоит остальным строкам запятой.
    NpcTag tag = NpcTag::None;

    // ── ЧТО ЭТО ЗА СУЩЕСТВО: ВЛИТЫЕ СПУТНИКИ (CANON S26 «Одна строка на
    // род», владелец 2026-09-22: «просто надо сделать ЕДИНУЮ таблицу реестр
    // всех нпц, и каждый нпц/моб в игре это просто строка в ней»).
    // Здесь стояли ТРИ отдельных массива по тому же ординалу NPCType —
    // kNpcNature, kNpcPurse, kNpcMapColor. Ни один не был решением: все три
    // были обходом позиционной инициализации, которой в этой структуре
    // больше нет. Их свидетели `rows_in_enum_order` ушли вместе с ними —
    // колонке строки не нужен свидетель порядка, она И ЕСТЬ строка.

    // Человек / зверь / нежить. Спрашивается вместо границы ординала,
    // которая врала: человеческие рода дописаны в enum ПОСЛЕ звериного
    // блока, и мир считал их зверьём.
    NpcNature nature = NpcNature::Human;

    // СКОЛЬКО ЕСТ В ДЕНЬ, в единицах голодной строки — ровно та же единица,
    // что у населения (econ_day.h kNeeds: 1 = один житель-день), поэтому
    // счёт ростера и счёт населения складываются без переводного
    // множителя. Близнец `upkeepGoldPerDay`: тот говорит, сколько род
    // ПОЛУЧАЕТ, этот — сколько ПОТРЕБЛЯЕТ, и обе колонки читает одна дверь
    // счёта (roster_window.h roster_bill). 0 = не ест вовсе.
    int boardPerDay = 1;

    // Кошелёк рода: у зверя карманов нет под каким бы знаменем он ни дрался,
    // купец богат потому, что купец. Богатство МЕСТА модулирует сверху.
    int purseMin = 0;
    int purseMax = 0;

    // Цвет метки на карте мира.
    std::uint32_t mapColor = 0xFFFFFFFFu;

    // ГДЕ ЭТОТ РОД ВОДИТСЯ — битовая маска ареала (kHab* выше). Шестая и
    // последняя таблица-спутник (`kSpawnHabitats`, fauna.cpp) влита сюда
    // 2026-09-22: она жила в .cpp, то есть была невидима отсюда, и колонка
    // рода стояла в двух файлах сразу.
    std::uint16_t habitat = 0;

    // Профессия стоит в толпе только там, где её земля: DepositKind, чья
    // живая жила в досягаемости открывает эту строку. -1 = гейта нет.
    std::int8_t depositGate = -1;

    // Под чьим знаменем строка встаёт, когда её поднимает ОТКРЫТАЯ ЗЕМЛЯ:
    // фракция — свойство экземпляра, а не рода, и дикая земля назначает её
    // сама. nullptr = открытая земля этот род не поднимает (человеческая
    // полоса: города и макро-спавны называют знамя сами).
    const char* wildFaction = nullptr;

    // ЗДЕСЬ СТОЯЛО ОБЪЯСНЕНИЕ, ПОЧЕМУ ПРИРОДА ЛЕЖИТ НЕ ЗДЕСЬ: «эта колонка
    // стоит в ХВОСТЕ длинного ряда, и чтобы назвать её, строке пришлось бы
    // выписать десяток промежуточных умолчаний руками». Довод был ЧЕСТНЫЙ и
    // ровно поэтому опасный — он оправдывал обход, а не чинил причину.
    // Причина — позиционная инициализация; ряды переведены на ИМЕНОВАННЫЕ
    // поля 2026-09-22, и вместе с ней исчез сам вопрос: новая колонка стоит
    // одну строку здесь и НОЛЬ правок в сорока шести рядах, а строка
    // называет только своё. Три спутника влиты выше.
};

// ── «ИМЕНОВАННОСТЬ» — субъектность рода (owner verdict 2026-09-10) ─────────
// The MMORPG model: a NAMED kind's every individual OWNS his character sheet
// — an ecs::CharacterSheet component born with the body (make_npc), levelled
// in place and ridden by the save — «он как игрок». A transient crew
// (rotation professions, caravans — «они уничтожаются своим ландмарком»)
// derives its generic sheet from its row on the spot and stores nothing.
// The door that speaks this split is sheet_of (macro/squad.h).
//
// A TABLE, not an engine branch: adding a named kind is one line here. It
// sits beside the rows instead of being a NpcTypeDef column only because
// the rows are positional aggregates and a trailing opt-in bool would cost
// every named row the whole light/haul/armor tail spelled by hand.
inline constexpr NPCType kNamedKinds[] = {
    NPCType::Merchant,     // торговец-одиночка
    NPCType::Bandit,       // вожак банды — банда не растворяется ландмарком
    NPCType::Witch,        // фигуры, не массовка
    NPCType::Sorceress,
    NPCType::Adventurer,   // герой/игрок (его лист приедет посадкой Б)
};
inline constexpr bool npc_named(NPCType t) {
    for (const NPCType k : kNamedKinds) {
        if (k == t) return true;
    }
    return false;
}

// Humanoid cooldowns are authored ON THE MASS CURVE (anatomy.h
// weapon_swing_seconds, owner 2026-09-07): a humanoid's natural attack IS an
// implied weapon, so its tempo = kHandSwingS + implied_kg × kSwingSecondsPerKg
// — the same second a real ItemDef of that heft would cost anyone's hand, so
// the day these rows hold real inventories nothing about their pace changes.
// Implied hefts: peasant/merchant/caravaner club-or-knife-and-fear ~3 kg (an
// untrained wide swing), woodcutter's felling axe ~2.5 kg, bandit blade and
// guard sword ~2 kg. Casters are not mass: their bolt tempo sits on the SPELL
// rows' own scale (combat spells author 1.5–4 s cooldowns — spells.h).
// Beast rows below keep their own authored tempos: a fang has no kilograms.
inline constexpr CombatTemplate kPeasantCombat   {25,{3,1}, 1.0f, 2.0f, 3.0f, "Psr", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWoodcutterCombat{30,{8,1}, 1.0f, 2.0f, 2.75f, "Wdc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kMerchantCombat  {30,{5,1}, 1.25f, 2.0f, 3.0f, "Mrc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kBanditCombat    {50,{12,1}, 2.25f, 3.0f, 2.5f, "Bnd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kGuardCombat     {55,{14,1}, 1.75f, 3.0f, 2.5f, "Grd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
// The adventurer's own template — the PLAYER's row. Base hp 100: the bare
// level-1 bar the sheet law always built for him (bar_ceilings' default
// base). Until landing 4 that base hid in a default ARGUMENT while his row
// shared the guard's 55 — a player-special number smuggled through a
// signature; now the row carries it like every other body's floor, and his
// ceilings go through body_max_hp like every other body's do.
inline constexpr CombatTemplate kAdventurerCombat{100,{14,1}, 1.75f, 3.0f, 2.5f, "Adv", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
// THE canonical base bars (CANON S14). The whole bar law — creation, the
// rescale door, the rest law's 8-hour night — is tuned around a bare
// level-1 body of 100/100/100, and the adventurer's row IS that bare body.
// If a retune moves these, it must move the LAW, not drift one row: this
// guard makes the drift loud (product-of-two-knobs lesson, 2026-09-03).
static_assert(kAdventurerCombat.hp == 100.0f && kAdventurerCombat.mp == 100
                  && kAdventurerCombat.sp == 100,
              "the adventurer's row is the canonical bare 100/100/100 body "
              "the bar law is tuned around");
// The ambusher fights EXACTLY like a bandit — every number above is his —
// and differs in one column: he sees the whole road. 1000 m against a
// prologue block three cells wide means there is nowhere in that scene to
// walk unseen, which is what an ambush is. The trailing two are spelled out
// because this template is positionally initialised: bodyHeight (0 = the
// body table decides) then sight, or the number would land in the wrong
// field without a word from the compiler.
// HP 100 (owner's number) against the bandit's 50: the scene ENDS in his
// death — the witch is the pocket's only exit — so a level-1 character must
// not be able to fight his way out of the story. He opens with FISTS (the
// creation kit carries coin, bread and two potions, no weapon, and there is
// no shop before the road), so 1d2 + STR against three of these is 60-odd
// connected blows while they answer.
// Their DAMAGE is the bandit's, untouched (owner: «но не ваншотеров»): 12 a
// blow every 2.5 s against ~130 player HP is eleven blows — a real fight
// that he loses, not an execution.
inline constexpr CombatTemplate kAmbusherCombat  {100,{12,1}, 2.25f, 3.0f, 2.5f, "Amb", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu, /*bodyHeight*/0.0f, /*sight*/1000.0f};
// КАСТУЮЩИЕ РЯДЫ НАЗЫВАЮТ СВОЙ СПЕЛЛ (вердикт владельца 2026-09-17,
// построено 2026-09-19): «каст у нас через систему спелов». Их собственные
// кубы больше не читаются — как у игрока, у кастера есть ЛИСТ и СПЕЛЛ, а
// не своя третья сила. Спеллы подобраны по СМЫСЛУ и близости прежней силы:
// ведьма 18 → magic_bolt 12, сорка 22 → lightning_chain 22 (в точку),
// дракон 3d20≈31 → fireball 36 (его огонь и его бласт — колонками спелла),
// культист 3d6≈10 → magic_bolt 12, лич 4d12=26 → energy_beam 28.
// ДОЛГ: у Void нет спелла с уроном, поэтому лич (Void) временно кастует
// Arcane-луч — это строка КОНТЕНТА, которой Void ещё не написали (S15
// «школа = набор спеллов»), а не дыра закона.
inline constexpr CombatTemplate kWitchCombat     {60,{18,1}, 1.5f, 20.0f,4.0f, "Wtc", CombatTemplate::Missile, 180, 0, 0xFFA070D0u, /*bodyHeight*/0.0f, kNpcSightDefaultM, /*mp*/100, /*sp*/100, DamageType::Blunt, /*cruiseM*/0.0f, /*castSpell*/spell_ordinal("magic_bolt")};
inline constexpr CombatTemplate kSorceressCombat {70,{22,1}, 1.25f, 25.0f,3.6f, "Src", CombatTemplate::Missile, 200, 6, 0xFF70C0E0u, /*bodyHeight*/0.0f, kNpcSightDefaultM, /*mp*/100, /*sp*/100, DamageType::Blunt, /*cruiseM*/0.0f, /*castSpell*/spell_ordinal("lightning_chain")};
// Дракон (владелец 2026-09-10): «маленькая армия в одном теле» — hp 500,
// огненный шар = обычные Missile-колонки (бласт 2.5 м — АоЕ, цвет огня),
// урон 3d20 Fire (тип — колонка dmgType ниже дефолтов, авторится в строке
// project_combat не трогается). ЛЕТУН: cruiseM 10 — честный полёт (конверт
// игрока), рождается с ecs::Flying; на карте марш не платит рельеф.
// bodyHeight 6 — башня, не человек (одна колонка, не ветка рендера).
inline constexpr CombatTemplate kDragonCombat    {500,{3,20}, 1.6f, 40.0f,3.0f, "Drg", CombatTemplate::Missile, 260, 2.5f, 0xFF3060FFu, /*bodyHeight*/6.0f, /*sight*/60.0f, /*mp*/100, /*sp*/100, DamageType::Fire, /*cruiseM*/10.0f, /*castSpell*/spell_ordinal("fireball")};

// КРЕСТЬЯНЕ РАБОТАЮТ ЗА ЕДУ (владелец 2026-09-18: «пусть будут 0, чтобы не
// нарушать единство систем — их зп 0 в деньгах»): upkeepGoldPerDay = 0 у
// крестьянских родов значит «на содержании ЕДОЙ, жалованья не берёт» —
// жалованье есть цена НАЁМНОЙ службы (гарнизоны, варбанды, ростер игрока).
// kNpcUpkeepNone (−1) остаётся «не на содержании вовсе» (звери, монстры,
// бандиты). Цена найма — своя колонка hireGold, от нуля жалованья не
// зависит.
inline constexpr NpcTypeDef kNpcTypeDefs[std::size_t(NPCType::Count)] = {

    // Peasant
    {
        .type = NPCType::Peasant,
        .id = "peasant",
        .label = "Peasant",
        .sprite = SpriteId::Peasant,
        .baseLevel = 1,
        .ai = AIBehaviour::Gatherer,
        .combat = kPeasantCombat,
        .upkeepGoldPerDay = 0,
        .hireable = true,
        .xpReward = 10,
        .weight = 55,
        .lootId = "peasant",
        .names = {{"Ivan","Pyotr","Sergey","Dmitry","Alexei","Nikolai","Vasily","Grigory",
          "Fedor","Andrei","Olga","Natalya","Katya","Masha","Dasha"}},
        .nameCount = 15,
        .talkLines = {{"The harvest has been poor this year...",
          "Have you heard? Bandits roam the roads at night.",
          "Blessings upon you, traveler.",
          "I sell nothing of interest, but the merchant might.",
          "Stay safe out there. The wilderness is harsh."}},
        .talkCount = 5,
        // Dark, one back, own skin — the defaults, spelled out only to reach
        // the price column at the row's end: upkeep 1 × 30 days.
        .hireGold = 30,
        .nature = NpcNature::Human,
        .purseMin = 1,
        .purseMax = 10,
        .mapColor = 0xDCC8A0u,
        .habitat = kHabTown,
    },

    // Merchant
    {
        .type = NPCType::Merchant,
        .id = "merchant",
        .label = "Merchant",
        .sprite = SpriteId::Caravan,
        .baseLevel = 3,
        .ai = AIBehaviour::Trader,
        .combat = kMerchantCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 30,
        .weight = 21,
        .lootId = "merchant",
        .names = {{"Kartash","Bazukin","Torgin","Menkov","Skaldin"}},
        .nameCount = 5,
        .talkLines = {{"Looking to trade? I have fine wares!",
          "Gold makes the world go round, friend.",
          "I travel between settlements. The roads are dangerous.",
          "Business has been slow. Perhaps you need something?"}},
        .talkCount = 4,
        .nature = NpcNature::Human,
        .purseMin = 50,
        .purseMax = 200,
        .mapColor = 0xF0C850u,
    },

    // Bandit
    {
        .type = NPCType::Bandit,
        .id = "bandit",
        .label = "Bandit",
        .sprite = SpriteId::Bandit,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = kBanditCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 20,
        .lootId = "bandit",
        .names = {{"Razboy","Diki","Grozny","Slyak","Khvat"}},
        .nameCount = 5,
        .talkLines = {{"Your gold or your life!",
          "Heh, another fool wandering the wilds.",
          "I take what I want. Got a problem with that?",
          "The strong survive. The weak feed us."}},
        .talkCount = 4,
        .nature = NpcNature::Human,
        .purseMin = 5,
        .purseMax = 30,
        .mapColor = 0xDC3C3Cu,
    },

    // Guard
    {
        // Колонка `ai` стала `Aggressive` 2026-09-22 вместе со сносом
        // `AIBehaviour::Patrol`, и это НЕ смена характера в сцене: стойку
        // тела даёт `combatant_behaviour`, а она отвечала `true` и на
        // Patrol, и на Aggressive — `subworld_ai_for` даст тот же
        // `SubworldAi::Combat` байт в байт. Дать сюда `Flee`, как получили
        // Merchant и Peasant, было бы нельзя: страж побежал бы от драки.
        .type = NPCType::Guard,
        .id = "guard",
        .label = "Guard",
        .sprite = SpriteId::Peasant,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = kGuardCombat,
        .upkeepGoldPerDay = 3,
        .hireable = true,
        .xpReward = 30,
        .lootId = "guard",
        .names = {{"Strazhnik","Boyar","Vityaz","Desyatnik","Druzhina"}},
        .nameCount = 5,
        .talkLines = {{"Move along, citizen. Nothing to see here.",
          "The settlement is safe under our watch.",
          "Report any bandit sightings to the elder.",
          "Stay on the roads if you value your life."}},
        .talkCount = 4,
        // Night-watch torch: a warm carried light, a touch smaller and dimmer
        // than the player's lantern (radius 16 / intensity 1.35) so the player's
        // own pool still reads as primary and a patrolled street gains pools of
        // firelight that move with the guards. Additive over the directional
        // term ⇒ a warm pool at night, washed out by day, exactly like the
        // lantern — no day/night special-casing. Seated 1.1 m up (chest/held).
        .lightRadius = 11.0f,
        .lightIntensity = 1.15f,
        .lightR = 1.00f,
        .lightG = 0.66f,
        .lightB = 0.34f,
        .lightHeight = 1.1f,
        // The disciplined tank of the table wears what his row already
        // describes. `haulMult` is spelled out only because the plate after it
        // is not the default; kArmorHalving is what makes 10 legible — it
        // HALVES a plain blow.
        .armor = uniform_armor(10),
        // The price column: upkeep 3 × 30 days.
        .hireGold = 90,
        .nature = NpcNature::Human,
        .purseMin = 5,
        .purseMax = 20,
        .mapColor = 0x508CDCu,
        .habitat = kHabTown,
    },

    // Witch
    {
        .type = NPCType::Witch,
        .id = "witch",
        .label = "Witch",
        .sprite = SpriteId::Witch,
        .baseLevel = 5,
        .ai = AIBehaviour::Teleporter,
        .combat = kWitchCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 50,
        .weight = 3,
        .lootId = "witch",
        .names = {{"Yaga","Vedma","Znakharka","Koldunia","Volshebnitsa"}},
        .nameCount = 5,
        .talkLines = {{"The spirits whisper of your coming...",
          "I see great trials ahead for you.",
          "Herbs and potions are my trade. Interested?",
          "The forest knows all. Listen carefully."}},
        .talkCount = 4,
        .nature = NpcNature::Human,
        .purseMin = 10,
        .purseMax = 40,
        .mapColor = 0xB464C8u,
    },

    // Sorceress
    {
        .type = NPCType::Sorceress,
        .id = "sorceress",
        .label = "Sorceress",
        .sprite = SpriteId::Sorceress,
        .baseLevel = 6,
        .ai = AIBehaviour::Wanderer,
        .combat = kSorceressCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 60,
        .lootId = "sorceress",
        .names = {{"Charodejka","Zaklinatelnitsa","Mistika","Runara","Svetozara"}},
        .nameCount = 5,
        .talkLines = {{"The arcane currents shift around you...",
          "Few mortals seek me out willingly.",
          "I deal in mysteries beyond your understanding.",
          "Power has a price. Are you willing to pay?"}},
        .talkCount = 4,
        .nature = NpcNature::Human,
        .purseMin = 10,
        .purseMax = 40,
        .mapColor = 0x78C8E6u,
    },

    // Rabbit
    {
        .type = NPCType::Rabbit,
        .id = "rabbit",
        .label = "Rabbit",
        .sprite = SpriteId::Rabbit,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {5, {0,1}, 2.75f, 0, 9.0f, "Rbt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 15,
        .radius = 0.4f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | kHabForest,
        .wildFaction = "wildlife",
    },

    // Deer
    {
        .type = NPCType::Deer,
        .id = "deer",
        .label = "Deer",
        .sprite = SpriteId::Deer,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {15, {2,1}, 2.5f, 2, 2.0f, "Der"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 12,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | hab(Taiga) | kHabForest,
        .wildFaction = "wildlife",
    },

    // Fox
    {
        .type = NPCType::Fox,
        .id = "fox",
        .label = "Fox",
        .sprite = SpriteId::Fox,
        .baseLevel = 1,
        .ai = AIBehaviour::Wanderer,
        .combat = {12, {4,1}, 2.25f, 2, 1.2f, "Fox"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 8,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Taiga) | hab(Tundra) | kHabForest,
        .wildFaction = "wildlife",
    },

    // Wolf
    {
        .type = NPCType::Wolf,
        .id = "wolf",
        .label = "Wolf",
        .sprite = SpriteId::Wolf,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {30, {10,1}, 2.5f, 3, 1.0f, "Wlf"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 6,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | hab(Mountain)
                          | kHabForest,
        .wildFaction = "wildlife",
    },

    // Bear
    {
        .type = NPCType::Bear,
        .id = "bear",
        .label = "Bear",
        .sprite = SpriteId::Bear,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {80, {18,1}, 1.75f, 3, 1.5f, "Ber"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 3,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Taiga) | kHabForest,
        .wildFaction = "wildlife",
    },

    // Boar
    {
        .type = NPCType::Boar,
        .id = "boar",
        .label = "Boar",
        .sprite = SpriteId::Boar,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {40, {12,1}, 2.0f, 3, 1.2f, "Bor"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 5,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | kHabForest,
        .wildFaction = "wildlife",
    },

    // Snake
    {
        .type = NPCType::Snake,
        .id = "snake",
        .label = "Snake",
        .sprite = SpriteId::Snake,
        .baseLevel = 1,
        .ai = AIBehaviour::Aggressive,
        .combat = {10, {8,1}, 1.5f, 2, 0.8f, "Snk"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 4,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Desert) | hab(Steppe) | hab(Swamp)
                          | hab(Tropics) | kHabRuin,
        .wildFaction = "wildlife",
    },

    // Hawk
    {
        .type = NPCType::Hawk,
        .id = "hawk",
        .label = "Hawk",
        .sprite = SpriteId::Hawk,
        .baseLevel = 1,
        // ЛЕТУН: cruiseM 5 — ястреб честно в воздухе (полёт-посадка
        // 2026-09-10), гравитации нет, конверт общий с игроком.
        .ai = AIBehaviour::Wanderer,
        .combat = {8, {5,1}, 3.0f, 3, 1.0f, "Hwk", CombatTemplate::Melee, 0, 0, 0xFFFFFFFFu, 0.0f, kNpcSightDefaultM, 100, 100, DamageType::Blunt, 5.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 3,
        .radius = 0.4f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Desert)
                          | hab(Steppe),
        .wildFaction = "wildlife",
    },

    // Frog
    {
        .type = NPCType::Frog,
        .id = "frog",
        .label = "Frog",
        .sprite = SpriteId::Frog,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {3, {0,1}, 1.5f, 0, 9.0f, "Frg"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 10,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp),
        .wildFaction = "wildlife",
    },

    // Mountain Goat
    {
        .type = NPCType::Goat,
        .id = "goat",
        .label = "Mountain Goat",
        .sprite = SpriteId::Goat,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {20, {5,1}, 2.0f, 2, 1.5f, "Mgt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 8,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain),
        .wildFaction = "wildlife",
    },

    // Eagle
    {
        .type = NPCType::Eagle,
        .id = "eagle",
        .label = "Eagle",
        .sprite = SpriteId::Eagle,
        .baseLevel = 2,
        // ЛЕТУН: cruiseM 6 — орёл выше ястреба, тот же закон.
        .ai = AIBehaviour::Wanderer,
        .combat = {12, {7,1}, 3.25f, 3, 1.0f, "Egl", CombatTemplate::Melee, 0, 0, 0xFFFFFFFFu, 0.0f, kNpcSightDefaultM, 100, 100, DamageType::Blunt, 6.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 4,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain),
        .wildFaction = "wildlife",
    },

    // Crocodile
    {
        .type = NPCType::Croc,
        .id = "crocodile",
        .label = "Crocodile",
        .sprite = SpriteId::Crocodile,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {50, {15,1}, 1.25f, 3, 1.5f, "Crc"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 4,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp) | hab(Tropics),
        .wildFaction = "wildlife",
    },

    // Goblin
    {
        .type = NPCType::Goblin,
        .id = "goblin",
        .label = "Goblin",
        .sprite = SpriteId::Goblin,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {25, {8,1}, 2.0f, 3, 1.0f, "Gbl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 4,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 1,
        .purseMax = 12,
        .mapColor = 0xC8C8C8u,
        .habitat = kHabForest | kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },

    // Skeleton
    {
        .type = NPCType::Skeleton,
        .id = "skeleton",
        .label = "Skeleton",
        .sprite = SpriteId::Skeleton,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {35, {10,1}, 1.5f, 3, 1.2f, "Skl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 3,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },

    // Troll
    {
        .type = NPCType::Troll,
        .id = "troll",
        .label = "Troll",
        .sprite = SpriteId::Troll,
        .baseLevel = 5,
        .ai = AIBehaviour::Aggressive,
        .combat = {120, {25,1}, 1.25f, 4, 2.0f, "Trl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/30,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },

    // Swamp Thing
    {
        .type = NPCType::SwampThing,
        .id = "swamp_thing",
        .label = "Swamp Thing",
        .sprite = SpriteId::SwampThing,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {60, {14,1}, 1.0f, 4, 1.5f, "Swt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 3,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp),
        .wildFaction = "demons",
    },

    // Ice Wraith
    {
        .type = NPCType::IceWraith,
        .id = "ice_wraith",
        .label = "Ice Wraith",
        .sprite = SpriteId::IceWraith,
        .baseLevel = 4,
        .ai = AIBehaviour::Aggressive,
        .combat = {45, {16,1}, 1.75f, 5, 1.3f, "Iwr"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/25,
        .weight = 2,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Tundra) | hab(Snow) | kHabSpire,
        .wildFaction = "demons",
    },

    // Sand Scorpion
    {
        .type = NPCType::SandScorpion,
        .id = "sand_scorpion",
        .label = "Sand Scorpion",
        .sprite = SpriteId::SandScorpion,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {35, {12,1}, 1.75f, 3, 1.0f, "Ssc"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 5,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Desert),
        .wildFaction = "demons",
    },

    // Stone Golem
    {
        .type = NPCType::StoneGolem,
        .id = "stone_golem",
        .label = "Stone Golem",
        .sprite = SpriteId::StoneGolem,
        .baseLevel = 5,
        .ai = AIBehaviour::Aggressive,
        .combat = {150, {20,1}, 0.75f, 4, 2.5f, "Glm"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/30,
        .weight = 1,
        .radius = 1.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain) | kHabSpire,
        .wildFaction = "demons",
    },

    // The player. An ordinary row of the ordinary table (owner, 2026-08-27),
    // because his macro squad is an ordinary squad and a squad names a row
    // here. `weight` 0: the world never rolls an adventurer out of thin air —
    // this row is reached BY NAME, by the one entity that wears the flag. Not
    // hireable, worth no XP (his death is a game-over, not a kill), and his
    // loot is the bag he actually carries rather than a rolled profile.
    {
        .type = NPCType::Adventurer,
        .id = "adventurer",
        .label = "Adventurer",
        .sprite = SpriteId::Peasant,
        .baseLevel = 1,
        .ai = AIBehaviour::Wanderer,
        .combat = kAdventurerCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 0,
        .lootId = "peasant",
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Human,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xC8C8C8u,
    },

    // Tax-collector — the feudal graph's own courier (owner 2026-08-30)
    {
        .type = NPCType::TaxCollector,
        .id = "tax_collector",
        .label = "Tax-collector",
        .sprite = SpriteId::Peasant,
        .baseLevel = 2,
        .ai = AIBehaviour::TaxRun,
        .combat = kWoodcutterCombat,
        .upkeepGoldPerDay = 0,
        .hireable = true,
        .xpReward = 12,
        .weight = 21,
        .lootId = "merchant",
        .names = {{"Foka","Yeremey","Lavrenty","Sofron","Nikanor"}},
        .nameCount = 5,
        .talkLines = {{"The crown's eighth, weighed and sealed.",
          "Rob me and you rob the capital - think on that.",
          "Every realm stands on carried coin."}},
        .talkCount = 3,
        .hireGold = 30,
        .nature = NpcNature::Human,
        .purseMin = 1,
        .purseMax = 10,
        .mapColor = 0xC8C8C8u,
    },

    // Road ambusher — the prologue's teeth (owner 2026-09-09). A bandit in
    // body, sprite, behaviour and loot; his row exists for two columns the
    // world's bandits must NOT inherit: he watches the whole road
    // (kAmbusherCombat sight) and he cannot be beaten at level 1 (its HP).
    // Appended, so every saved ordinal stays where it was.
    {
        .type = NPCType::RoadAmbusher,
        .id = "road_ambusher",
        .label = "Ambusher",
        .sprite = SpriteId::Bandit,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = kAmbusherCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 20,
        // He drops what a bandit drops, named in his own column: his ordinal
        // sits in the creature stripe of the table, where the
        // per-role loot list no longer answers.
        .lootId = "bandit",
        .names = {{"Krivoy","Sukhoy","Gnily","Ryaboy","Tishina"}},
        .nameCount = 5,
        .talkLines = {{"We have been watching this road all morning.",
          "Nothing personal, traveller. The road is ours.",
          "Down. Stay down and it goes easier."}},
        .talkCount = 3,
        .nature = NpcNature::Human,
        .purseMin = 5,
        .purseMax = 30,
        .mapColor = 0xDC3C3Cu,
    },

    // Dragon (owner 2026-09-10, стол анкет): первый ЛЕТУН-боец —
    // kDragonCombat несёт cruiseM 10 (честный полёт, конверт игрока) и
    // огненный шар обычными Missile-колонками (3d20 Fire, бласт 2.5 м).
    // Ряд Wanderer по умолчанию — дизайн-дракон думает своей моделью
    // (LairSorties) через ступень анкеты; дикий, если когда-то родится
    // спавн-таблицей, будет просто кружить. weight 0 — вслепую мир его
    // не выбрасывает: дракон приходит только по имени (анкета, данж).
    {
        .type = NPCType::Dragon,
        .id = "dragon",
        .label = "Dragon",
        .sprite = SpriteId::Dragon,
        .baseLevel = 10,
        .ai = AIBehaviour::Wanderer,
        .combat = kDragonCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = 500,
        .radius = 1.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xB03030u,
    },

    // ── THE BESTIARY OF THE POPULATED PLACES (content, 2026-09-11) ────────
    //
    // Sixteen species authored against each other, not in isolation. Three
    // things decide where each of them ends up standing, and all three are
    // columns of this row — no spawn table names any of them by hand:
    //
    //   · `combat` decides its STRENGTH. `spawn_strength` (fauna.cpp) is
    //     log₂(hp × dice/cooldown) normalised over this table, and
    //     `danger_match` halves a row's odds per 25 bytes of mismatch with
    //     the cell's danger. So authoring a stat block IS authoring a
    //     habitat band: an ogre cannot wander into a starting meadow because
    //     of what an ogre is, not because a list forbade it.
    //   · `weight` decides how COMMON it is where it belongs — fodder 4-6,
    //     the heavies 1-2, exactly the scale the older rows use.
    //   · the habitat mask (fauna.cpp kSpawnHabitats) decides WHOSE ground
    //     it is at all.
    //
    // The dice are authored spreads, which is what the scalar-era rows are
    // owed (`CombatTemplate::dice`, «authored spreads are content-stage
    // work»): a rat's 2d4 and an ogre's 3d12 have honest variance, so two
    // fights with the same species are not the same fight. Damage COLUMNS
    // are authored too — fang, blade, cold and void argue with different
    // rows of the 9×9 armour table, which is what makes a mixed crowd a
    // tactical problem instead of one damage number wearing costumes.
    //
    // Three of them FLY (`cruiseM` > 0, the v93 law): the bat cruises low
    // enough to keep under a cave's ceiling, the harpy owns the crags and
    // the gargoyle patrols a tower's airspace. They ride the same envelope
    // the player does — no flying-monster system, one column.

    // Giant rat — what lives under a floor nobody sweeps. Fast, weak, many.
    {
        .type = NPCType::GiantRat,
        .id = "giant_rat",
        .label = "Giant Rat",
        .sprite = SpriteId::GiantRat,
        .baseLevel = 1,
        .ai = AIBehaviour::Aggressive,
        .combat = {14, {2,4}, 2.5f, 2.0f, 1.0f, "Rat", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.5f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 6,
        .radius = 0.35f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x6A5A4Au,
        .habitat = kHabRuin | hab(Mountain),
        .wildFaction = "wildlife",
    },

    // Cave bat — the first thing a torch finds. Cruises at 2.5 m: high
    // enough to be a nuisance, low enough that a cave's ceiling still holds
    // it (the envelope is shared, so a row that cruised into the rock would
    // simply be pinned against it — a number, not a bug).
    {
        .type = NPCType::CaveBat,
        .id = "cave_bat",
        .label = "Cave Bat",
        .sprite = SpriteId::CaveBat,
        .baseLevel = 1,
        .ai = AIBehaviour::Aggressive,
        .combat = {8, {1,6}, 3.0f, 2.0f, 0.9f, "Bat", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.4f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, /*cruiseM*/2.5f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 6,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x4A4048u,
        .habitat = kHabRuin | hab(Mountain),
        .wildFaction = "wildlife",
    },

    // Kobold — the goblin's smaller cousin, and the first thing in the game
    // that carries a purse worth taking off it.
    {
        .type = NPCType::Kobold,
        .id = "kobold",
        .label = "Kobold",
        .sprite = SpriteId::Kobold,
        .baseLevel = 1,
        .ai = AIBehaviour::Aggressive,
        .combat = {20, {2,4}, 2.2f, 3.0f, 1.1f, "Kbd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.1f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .weight = 5,
        .radius = 0.45f,
        .names = {{"Skree","Yip","Gnash","Tikka","Vess"}},
        .nameCount = 5,
        .talkLines = {{"Not yours! Not yours!",
          "Down the hole with you.",
          "Sharp! Sharp and quick!"}},
        .talkCount = 3,
        .nature = NpcNature::Fauna,
        .purseMin = 1,
        .purseMax = 6,
        .mapColor = 0x8A6A3Au,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "demons",
    },

    // Cave spider — slow to notice, fast to close. Pierce fangs.
    {
        .type = NPCType::CaveSpider,
        .id = "cave_spider",
        .label = "Cave Spider",
        .sprite = SpriteId::CaveSpider,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {26, {2,6}, 2.4f, 2.0f, 1.0f, "Spd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.7f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 4,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x2A2A3Au,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "wildlife",
    },

    // Imp — a spire's smallest servant: quick, burning, and never alone.
    {
        .type = NPCType::Imp,
        .id = "imp",
        .label = "Imp",
        .sprite = SpriteId::Imp,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {18, {2,5}, 2.8f, 2.0f, 0.9f, "Imp", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Fire},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 5,
        .radius = 0.35f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xA03050u,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },

    // Zombie — the slowest thing that will still kill you. Its threat is the
    // 2.0 s cooldown meeting 55 hp: you cannot out-trade it, you walk away.
    {
        .type = NPCType::Zombie,
        .id = "zombie",
        .label = "Zombie",
        .sprite = SpriteId::Zombie,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {55, {2,8}, 0.8f, 3.0f, 2.0f, "Zmb", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.75f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/15,
        .weight = 4,
        .radius = 0.55f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x6A7A5Au,
        .habitat = kHabRuin | kHabSpire | hab(Swamp),
        .wildFaction = "demons",
    },

    // Orc — the raider of the open land, and the one new row that belongs
    // outdoors as much as underground.
    {
        .type = NPCType::Orc,
        .id = "orc",
        .label = "Orc",
        .sprite = SpriteId::Orc,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {60, {2,8}, 1.8f, 3.0f, 1.4f, "Orc", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Slash},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 4,
        .radius = 0.6f,
        .names = {{"Gruth","Mazgar","Ukk","Snaga","Dorgul","Brakk"}},
        .nameCount = 6,
        .talkLines = {{"Blood for the warband.",
          "You walk where you should not.",
          "Come on then. Come on!"}},
        .talkCount = 3,
        .nature = NpcNature::Fauna,
        .purseMin = 2,
        .purseMax = 12,
        .mapColor = 0x5A7A4Au,
        .habitat = kHabForest | kHabRuin | hab(Steppe) | hab(Valley),
        .wildFaction = "demons",
    },

    // Ghoul — the zombie's opposite reading of the same corpse: fast,
    // lighter, three smaller dice instead of two big ones.
    {
        .type = NPCType::Ghoul,
        .id = "ghoul",
        .label = "Ghoul",
        .sprite = SpriteId::Ghoul,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {48, {3,6}, 2.4f, 3.0f, 1.2f, "Ghl", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.7f, kNpcSightDefaultM, 100, 100,
         DamageType::Slash},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 3,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x9A8A7Au,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },

    // Harpy — the crags' own. Cruises at 8 m, which is above a man's reach
    // and below a tower's crown: she is fought by looking UP.
    {
        .type = NPCType::Harpy,
        .id = "harpy",
        .label = "Harpy",
        .sprite = SpriteId::Harpy,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {30, {2,7}, 2.9f, 3.0f, 1.0f, "Hrp", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.6f, kNpcSightDefaultM, 100, 100,
         DamageType::Slash, /*cruiseM*/8.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/20,
        .weight = 3,
        .radius = 0.55f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0xB07850u,
        .habitat = kHabSpire | hab(Mountain),
        .wildFaction = "demons",
    },

    // Cultist — the man who serves the spire. The crowd's first SHOOTER:
    // ordinary Missile columns, arcane bolt, no blast — he is dangerous
    // because he stands behind the ogres, not because his numbers are big.
    {
        .type = NPCType::Cultist,
        .id = "cultist",
        .label = "Cultist",
        .sprite = SpriteId::Cultist,
        .baseLevel = 4,
        .ai = AIBehaviour::Aggressive,
        .combat = {45, {3,6}, 1.5f, 22.0f, 2.2f, "Cul", CombatTemplate::Missile, 190,
         0.0f, 0xFFA060E0u, /*bodyHeight*/1.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Arcane, /*cruiseM*/0.0f, /*castSpell*/spell_ordinal("magic_bolt")},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/25,
        .weight = 3,
        .lootId = "bandit",
        .radius = 0.55f,
        .names = {{"Brother Vas","Sister Ilm","Novice Korr","The Pale Hand","Acolyte Zeb"}},
        .nameCount = 5,
        .talkLines = {{"The tower drinks, and we pour.",
          "You are late. It has already begun.",
          "Kneel, and it will be quick."}},
        .talkCount = 3,
        .nature = NpcNature::Human,
        .purseMin = 2,
        .purseMax = 14,
        .mapColor = 0x50306Au,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },

    // Gargoyle — a tower's airborne masonry. Earth damage against the armour
    // table, Hulk silhouette, 90 hp: the first new row that must be planned
    // for rather than met.
    {
        .type = NPCType::Gargoyle,
        .id = "gargoyle",
        .label = "Gargoyle",
        .sprite = SpriteId::Gargoyle,
        .baseLevel = 5,
        .ai = AIBehaviour::Aggressive,
        .combat = {90, {3,8}, 1.4f, 3.0f, 1.6f, "Grg", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.2f, kNpcSightDefaultM, 100, 100,
         DamageType::Earth, /*cruiseM*/7.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/30,
        .weight = 2,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x60605Au,
        .habitat = kHabSpire | hab(Mountain),
        .wildFaction = "demons",
    },

    // Wraith — the ice wraith's rootless cousin: Void instead of cold, so a
    // player kitted against one is not kitted against the other.
    {
        .type = NPCType::Wraith,
        .id = "wraith",
        .label = "Wraith",
        .sprite = SpriteId::Wraith,
        .baseLevel = 5,
        .ai = AIBehaviour::Aggressive,
        .combat = {55, {3,8}, 2.0f, 4.0f, 1.4f, "Wrh", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Void},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/30,
        .weight = 2,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Void,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x7060A0u,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },

    // Ogre — the wall of the crowd. 3d12 at 2.2 s: it hits like a siege and
    // misses a fleeing man completely.
    {
        .type = NPCType::Ogre,
        .id = "ogre",
        .label = "Ogre",
        .sprite = SpriteId::Ogre,
        .baseLevel = 6,
        .ai = AIBehaviour::Aggressive,
        .combat = {140, {3,12}, 1.2f, 4.0f, 2.2f, "Ogr", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/3.0f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/35,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x8A7050u,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "demons",
    },

    // Minotaur — the ogre's answer for a player who thought running was the
    // answer: nearly the same weight of blow, at a man's speed.
    {
        .type = NPCType::Minotaur,
        .id = "minotaur",
        .label = "Minotaur",
        .sprite = SpriteId::Minotaur,
        .baseLevel = 6,
        .ai = AIBehaviour::Aggressive,
        .combat = {130, {4,10}, 1.9f, 4.0f, 1.8f, "Min", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.6f, kNpcSightDefaultM, 100, 100,
         DamageType::Slash},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/35,
        .weight = 1,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x6A3A2Au,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },

    // Basilisk — the swamp's and the desert's heavy. Serpent silhouette, so
    // the eye reads it instantly among the uprights.
    {
        .type = NPCType::Basilisk,
        .id = "basilisk",
        .label = "Basilisk",
        .sprite = SpriteId::Basilisk,
        .baseLevel = 6,
        .ai = AIBehaviour::Aggressive,
        .combat = {95, {3,10}, 1.6f, 3.0f, 1.5f, "Bsk", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.4f, kNpcSightDefaultM, 100, 100,
         DamageType::Earth},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/35,
        .weight = 1,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x3A6A4Au,
        .habitat = kHabRuin | hab(Swamp) | hab(Desert),
        .wildFaction = "demons",
    },

    // Lich — what is at the top of the climb. The only new row that both
    // shoots and blasts (3 m), the only one that hoards a purse, and the
    // strongest thing in the table below the dragon.
    {
        .type = NPCType::Lich,
        .id = "lich",
        .label = "Lich",
        .sprite = SpriteId::Lich,
        .baseLevel = 8,
        .ai = AIBehaviour::Aggressive,
        .combat = {110, {4,12}, 1.1f, 28.0f, 2.6f, "Lch", CombatTemplate::Missile, 210,
         3.0f, 0xFF90FFB0u, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Void, /*cruiseM*/0.0f, /*castSpell*/spell_ordinal("void_arrow")},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .xpReward = /*xp = 5*(baseLevel+1)*/45,
        .weight = 1,
        .radius = 0.6f,
        .names = {{"Vashkar","The Grey Crown","Ozimandel","Neth-Ur"}},
        .nameCount = 4,
        .talkLines = {{"I was old when your kingdom was a camp.",
          "Breathe. It is a habit you will lose.",
          "Come closer. I want to see it happen."}},
        .talkCount = 3,
        .nature = NpcNature::Void,
        .purseMin = 8,
        .purseMax = 40,
        .mapColor = 0xC0D0B0u,
        .habitat = kHabSpire,
        .wildFaction = "demons",
    },

    // Horse — the roster's beast of burden (CANON S10 «ЛОШАДЬ — ЮНИТ»).
    // A sturdy flighty grazer: hooves 1d4, faster than any march. It EATS
    // (upkeep 0 = a mouth on the board law, no wage — the column humans
    // use, the beast default kNpcUpkeepNone is exactly what this row must
    // NOT say), and it is hireable: the town's herd sells through the one
    // hire door, no horse-shop path.
    {
        .type = NPCType::Horse,
        .id = "horse",
        .label = "Horse",
        .sprite = SpriteId::Deer,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {40, {1,4}, 2.2f, 1.2f, 1.6f, "Hrs"},
        .upkeepGoldPerDay = 0,
        .hireable = true,
        .xpReward = /*xp = 5*(baseLevel+1)*/10,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        // One horse carries eight men's backs — the pack saddle against the
        // rucksack (a man hauls ~15 kg on foot, a pack horse ~120).
        .haulMult = 8.0f,
        // The price of the backs it replaces: eight peasant hires (30 each,
        // the row above) — dearer than a soldier, cheaper than a house.
        .hireGold = 240,
        .tag = NpcTag::Mount,
        .nature = NpcNature::Fauna,
        .purseMin = 0,
        .purseMax = 0,
        .mapColor = 0x8A6A42u,
        .habitat = hab(Steppe) | hab(Meadow) | hab(Valley),
        .wildFaction = "wildlife",
    },
};
static_assert(rows_in_enum_order(kNpcTypeDefs, &NpcTypeDef::type),
              "kNpcTypeDefs row order must mirror NPCType");

inline constexpr const NpcTypeDef& npc_def(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)];
}

// ── ДВЕРИ К КОЛОНКАМ СТРОКИ ──────────────────────────────────────────────
// Здесь стояли ТРИ таблицы-спутника (kNpcNature / kNpcPurse / kNpcMapColor)
// со своими свидетелями порядка. Влиты колонками в строку каталога
// 2026-09-22 (CANON S26 «Одна строка на род»). Двери остались: читателей у
// колонки много, и спрашивать они обязаны одним голосом.

inline constexpr NpcNature npc_nature(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)].nature;
}

// СКОЛЬКО ЭТА СТРОКА ЕСТ В ДЕНЬ, в единицах голодной строки.
inline constexpr int npc_board_per_day(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)].boardPerDay;
}

// THE man-shaped half-width (world units ≈ metres): what a row that authors
// no radius IS — a person. This default lived twice (here as the humanoid
// rows' silence, and as CombatTemplate::bodyRadius's 0.55 that no row ever
// authored); the template copy is dead, the number lives beside the column
// it defaults.
inline constexpr float kNpcBodyRadiusDefault = 0.55f;

// A row's body radius, default resolved — the ONE answer every consumer
// derives from (sub/body.h body_radius for live entities, the spawners for
// the footprint they stamp, the auto-battle fixture for its bodies).
inline constexpr float npc_body_radius(const NpcTypeDef& def) {
    return def.radius > 0.0f ? def.radius : kNpcBodyRadiusDefault;
}

// ── THE purse: how much coin a body of this row carries ───────────────────
// Кошелёк рода — колонка строки. Дверь возвращает пару как было, чтобы её
// читателям (npc_spawn) не пришлось знать про переезд.
struct NpcPurse { int min, max; };
inline constexpr NpcPurse npc_purse(NPCType t) {
    const NpcTypeDef& r = kNpcTypeDefs[std::size_t(t)];
    return NpcPurse{r.purseMin, r.purseMax};
}

// Цвет метки на карте — колонка строки.
inline constexpr std::uint32_t npc_map_color(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)].mapColor;
}

// THE id space, and it has one half now. Any "kind" that travels — a roster
// record, an ECS NPCKind, a save — is an ordinal of the one table above, and a
// wolf is as legal as a spearman (CANON.md S16). The `0x100 | catalog row`
// encoding that used to mark "monster" is gone with the second table.
inline bool valid_npc_kind(std::uint16_t raw) {
    return raw < static_cast<std::uint16_t>(NPCType::Count);
}

// The last remnant of the old split, and it is TEMPORARY: two births still
// exist below (sub/spawn.cpp), one that projects a sheet and one that reads a
// row's raw combat line. They merge in the next step and this predicate dies
// (Здесь стояла ГРАНИЦА ОРДИНАЛА — `is_creature_row` / `is_monster_kind`:
// «всё, что в enum после Rabbit, — зверь». Снесена 2026-09-21 вместе с
// вопросом, на который отвечала: природу рода говорит его строка
// (kNpcNature), а не место в перечислении. Она врала на человеческих родах,
// дописанных в хвост, и молча переехала бы на новый род завтра.)

// The row behind a record, or Peasant for a number that names none. One
// resolver, three spellings: the raw kind is the identity, the record and
// the slot are the two containers a soldier arrives in.
inline NPCType soldier_npc_type(std::uint16_t kind) {
    return kind < std::uint16_t(NPCType::Count) ? NPCType(kind)
                                                : NPCType::Peasant;
}
inline NPCType soldier_npc_type(const SoldierRecord& s) {
    return soldier_npc_type(s.kind);
}
inline NPCType soldier_npc_type(const SoldierSlot& s) {
    return soldier_npc_type(s.kind);
}

inline bool npc_hireable(NPCType t) {
    const auto& def = npc_def(t);
    return def.hireable && def.upkeepGoldPerDay >= 0;
}

inline int npc_upkeep_base(NPCType t) {
    const int upkeep = npc_def(t).upkeepGoldPerDay;
    return upkeep < 0 ? 0 : upkeep;
}

// A beast draws no pay — and the ROW already says so: every creature line
// authors `kNpcUpkeepNone`, which npc_upkeep_base reads as zero. The special
// case that used to stand here asked whether the record was a monster, which
// is a question about a class of thing rather than about this thing's column.
inline int soldier_upkeep(std::uint16_t kind, int level) {
    return npc_upkeep_base(soldier_npc_type(kind)) * soldier_level_factor(level);
}
inline int soldier_upkeep(const SoldierRecord& s) {
    return soldier_upkeep(s.kind, s.level);
}
inline int soldier_upkeep(const SoldierSlot& s) {
    return soldier_upkeep(s.kind, s.level);
}

// ── ПРИРОДА, СПРОШЕННАЯ У ЗАПИСИ РОСТЕРА ──────────────────────────────────
// Три двери, один столбец данных. Имя рода ни в одной из них не звучит:
// верблюд, мул и овца-вьюк становятся ездовыми, назвав свою спину, а народ
// нового вида — назвав свою природу.
inline bool is_folk_kind(std::uint16_t kind) {
    return valid_npc_kind(kind)
        && npc_nature(NPCType(kind)) == NpcNature::Human;
}
inline bool is_fauna_kind(std::uint16_t kind) {
    return valid_npc_kind(kind)
        && npc_nature(NPCType(kind)) == NpcNature::Fauna;
}

// ЕЗДОВАЯ ЛИ ЭТА СТРОКА — ОДНА дверь тега, чтобы «лошадь» нигде не
// называлась по имени рода (верблюд, мул и овца-вьюк станут ездовыми
// строкой данных).
inline bool is_mount_kind(std::uint16_t kind) {
    return valid_npc_kind(kind) && npc_def(NPCType(kind)).tag == NpcTag::Mount;
}

// Сколько ездовых стоит в ростере — вторая половина закона упряжки.
inline int count_mount_souls(const SoldierSquad& squad) {
    int n = 0;
    for (const SoldierSlot& s : squad) {
        if (is_mount_kind(s.kind)) n += int(s.count);
    }
    return n;
}

// ЗАКОН УПРЯЖКИ (владелец, 2026-09-19: «по лошадке на душу»): отряд ведёт
// столько ездовых, сколько в нём НЕ-ездовых душ — по одной на душу, и ни
// одной лишней. Лидер — своя душа, он тоже ведёт коня, поэтому +1.
//
// Это МЕРА ВЫДАЧИ, а не право собственности: табун принадлежит МЕСТУ
// (ДВУХТАКТНЫЙ ОБОЗ, вердикт владельца 2026-09-19) — на приходе отряд
// сдаёт в стойло ВСЁ ездовое, на выходе место выдаёт ему столько, сколько
// говорит эта мера и сколько стоит в стойле. Отсюда даром: табун можно
// угнать в набеге, продать караваном и увидеть в анкете места, а тяглом
// пользуется тот, кого дом сегодня послал за тяжёлым.
inline int mount_allowance(const SoldierSquad& squad) {
    int riders = 1;   // лидер
    for (const SoldierSlot& s : squad) {
        if (!is_mount_kind(s.kind)) riders += int(s.count);
    }
    return riders;
}

// The roster's PEOPLE — the souls that are hands, mouths of the labour
// ledger and subjects of the crew суд. A beast in the roster is a BACK
// (haulMult) and a mouth (upkeep column), never a hand: a horse does not
// mine, does not count toward a crew's want, and must not dissolve into a
// town's population as a person.
// СПРАШИВАЕТ ПРИРОДУ (kNpcNature), а не границу ординала: до 2026-09-21
// здесь стоял `!is_monster_kind`, и пять человеческих родов, дописанных в
// enum после звериного блока, молча не считались людьми.
inline int count_human_souls(const SoldierSquad& squad) {
    int n = 0;
    for (const SoldierSlot& s : squad) {
        if (is_folk_kind(s.kind)) n += int(s.count);
    }
    return n;
}

// Upkeep is MAINTENANCE, not a deal (owner 2026-09-17, сессия сезонов): the
// CHA/Trade discount that used to haggle the player's payroll down was a
// player-special path and died with the unified season window — bargaining
// belongs to HIRE, which already prices through the trade law. One law, one
// number, whoever's roster it is.
inline int calculate_squad_upkeep(const SoldierSquad& squad) {
    int base = 0;
    for (const SoldierSlot& s : squad) base += soldier_upkeep(s) * s.count;
    return base;
}

// The row's price column × THE one level law (soldier_level_factor) — the
// same product the old inline `upkeep × 30` computed, read from data
// (CANON S25). An unpriced row (hireGold 0) costs nothing and npc_hireable
// already refuses it.
inline int hire_price_for(std::uint16_t kind, int level) {
    return npc_def(soldier_npc_type(kind)).hireGold
           * soldier_level_factor(level);
}
inline int hire_price_for(const SoldierRecord& s) {
    return hire_price_for(s.kind, s.level);
}
inline int hire_price_for(const SoldierSlot& s) {
    return hire_price_for(s.kind, s.level);
}

inline int npc_hire_price_base(NPCType t) {
    const SoldierRecord preview = make_soldier(
        static_cast<std::uint8_t>(t), npc_def(t).baseLevel, 0u);
    return hire_price_for(preview);
}

inline int npc_xp_reward(NPCType t, int level) {
    const int base = npc_def(t).xpReward;
    const int safeLevel = normalize_soldier_level(level);
    return base + (safeLevel - 1) * 5;
}

// (garrison_soldier_id_base — the high-bit garrison id space — died with
// §42 Инк 7: a garrison soul draws its identity from THE one macro
// ordinal issuer (gs.nextMacroSpawnOrdinal), same as every other soldier.
// One soul, one name space — a record that walked garrison → patrol →
// garrison keeps one identity for its whole life.)

// ── ПАСТВА ВСТАЁТ В РОСТЕР ОДНОЙ СТРОКОЙ ─────────────────────────────────
// Здесь стоял `generate_garrison` (и тип `GarrisonResult` при нём): бросок
// монетки на КАЖДУЮ душу, 60 % Guard / 40 % Peasant. Вырезан 2026-09-22 по
// вердикту владельца: «в мире только СКВАДЫ и РОСТЕРЫ… пока никаких
// стражников, это усложняет систему; потом будем думать, когда вернём
// патрули и стражу».
//
// ЧТО ЭТИМ УМЕРЛО, КРОМЕ СТРАЖИ:
//   · ТРИ ПОТОКА RNG. Состав ростера был жребием — на генезисе города, на
//     генезисе деревни и в ежедневном наборе. Теперь состав — факт, а не
//     бросок, и три `Rng grng(...)` у вызывающих ушли вместе с ним.
//   · ПОСЛЕДНИЙ ПЛАТЕЛЬЩИК ЖАЛОВАНЬЯ В МИРЕ. У крестьянина
//     `upkeepGoldPerDay` = 0 («работают за еду»), у лошади 0, у зверья −1.
//     Значит `wageDebt` мира становится СТРУКТУРНЫМ НУЛЁМ, а с ним —
//     колонки прибора `soulsUnpaid`/`crewsUnpaid` и ветка `byWage` в окне
//     ростера. Колонка НЕ СНОСИТСЯ: жалованье вернётся с наёмниками, и это
//     известная спящая половина (§55), а не забытая.
//   · СТОК МОНЕТЫ. «Уплаченное сгорает в пул лута» перестаёт качать монету
//     из мира этой дверью.
//
// Строка каталога тел `NPCType::Guard` ОСТАЁТСЯ — прецедент тот же, что у
// патрульной строки реестра мест: «вырезан не вид, а то, что город его
// спавнит». Её носят авто-бой, сцена и двадцать тестов.
//
// Возвращает СКОЛЬКО ВСТАЛО: ростер полон (256 слотов) — место оставляет
// душу себе, и это отказ вслух, а не молчаливая потеря.
inline int raise_flock_into_roster(SoldierSquad& roster, int souls) {
    if (souls <= 0) return 0;
    const NpcTypeDef& row = npc_def(NPCType::Peasant);
    return roster.push_stack(std::uint16_t(NPCType::Peasant),
                             std::int16_t(row.baseLevel), souls)
               ? souls
               : 0;
}

inline int hire_npc(SoldierSquad& playerSquad, SoldierSquad& garrison,
                    NPCType kind, int& playerGold) {
    if (!npc_hireable(kind)) return 0;
    // A recruit MOVES between two rosters — and the move can be refused at
    // either end: an empty garrison has nobody, a full squad has no room. The
    // ceiling is the same one every squad has (kMaxSquadSlots): the player's
    // army used to have none at all, which walked straight into the save's
    // 8192-record wall and made the whole file refuse to write.
    for (int i = 0; i < garrison.slot_count(); ++i) {
        if (garrison[i].kind != static_cast<std::uint8_t>(kind)) continue;
        const int cost = hire_price_for(garrison[i]);
        if (playerGold < cost) return 0;
        SoldierRecord recruit{};
        if (!garrison.take_soul_at(i, recruit)) return 0;
        if (!playerSquad.push(recruit)) {
            garrison.push(recruit);   // no room: the man stays home
            return 0;
        }
        playerGold -= cost;
        return cost;
    }
    return 0;
}

// Case-insensitive token → registry row, matched against the row's stable
// machine `id` FIRST ("tax_collector" → NPCType::TaxCollector) — that column
// exists precisely to be what content names a row by — with the display
// `label` kept as a convenience fallback ("Tax-collector" still works at the
// console). Purely data-driven off kNpcTypeDefs: a new type is matchable the
// moment its row exists, no per-type branch. Returns false on no match — the
// CALLER decides its own fallback (the subworld console spawner keeps its
// historical silent-Bandit default; the SpawnEntity consumer refuses to
// spawn).
inline bool npc_type_from_label(const char* token, NPCType& out) {
    if (!token || token[0] == '\0') return false;
    const auto matches = [](const char* t, const char* name) {
        std::size_t k = 0;
        while (t[k] != '\0' && name[k] != '\0') {
            const int a = std::tolower(static_cast<unsigned char>(t[k]));
            const int b = std::tolower(static_cast<unsigned char>(name[k]));
            if (a != b) return false;
            ++k;
        }
        return t[k] == '\0' && name[k] == '\0';
    };
    for (int i = 0; i < int(NPCType::Count); ++i) {
        if (matches(token, npc_def(NPCType(i)).id)) {
            out = NPCType(i);
            return true;
        }
    }
    for (int i = 0; i < int(NPCType::Count); ++i) {
        if (matches(token, npc_def(NPCType(i)).label)) {
            out = NPCType(i);
            return true;
        }
    }
    return false;
}

} // namespace sm
