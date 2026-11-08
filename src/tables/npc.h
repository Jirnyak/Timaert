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
#include "core/time.h"          // steps_from_seconds — авторские секунды → тики восстановления
#include "tables/attributes.h"   // AttrWeights/SkillWeights — колонки весов роли (M-263)
#include "tables/army.h"        // CombatTemplate — боевой лист строки существа
#include "tables/behaviour.h"
#include "tables/progression.h"  // exp_value_of_level/tier_of_power — лестница мира
#include "tables/body_parts.h"  // SlotMask — какие слоты экипировки у тела этой строки
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "tables/biomes.h"   // Biome — младшие биты ареала строки
#include "tables/damage_types.h"
#include "tables/object_type.h"   // ступени лестницы: тип строки и его архетип
#include "tables/spells.h"    // spell_ordinal — a casting row names its spell
#include "tables/sprite_rows.h"

namespace sm {

// ── ПОРЯДОК ОСИ ЕСТЬ ЛЕСТНИЦА, А НЕ ИСТОРИЯ ДОПИСОК (наряд M-295) ─────────
// Рода идут ПО ТИПАМ (`tables/object_type.h`), типы — ПО АРХЕТИПАМ, поэтому
// «все строки архетипа» есть НЕПРЕРЫВНЫЙ ДИАПАЗОН ординалов, а не фильтр по
// колонке. Непрерывность прибита `static_assert`-ами под таблицей рядов.
//
// ЗДЕСЬ В ВОСЬМИ МЕСТАХ СТОЯЛО «Appended, so saved ordinals stay stable», И ЭТО
// БЫЛО ПРАВДОЙ РОВНО ДО ЭТОГО НАРЯДА. Порядок был ИСТОРИЕЙ: каждый новый род
// дописывался В ХВОСТ, чтобы сохранённый ординал не переехал, — и за дюжину
// дописок человеческие рода рассыпались по всему перечислению вперемежку со
// зверьём и нежитью (`Goblin, Skeleton, Troll, SwampThing, IceWraith…`). Сейв
// ограничением не является вовсе (§10), поэтому ординалы переставлены ОДИН раз,
// и новый род теперь встаёт В СВОЮ ГРУППУ.
//
// ЧТО ЭТО СТОИЛО — НАЗВАНО ВСЛУХ, А НЕ УМОЛЧАНО: `roll_spawns@src/macro/
// fauna.cpp` выбирает строку ПРЕФИКСНЫМ НАКОПЛЕНИЕМ по ординалу, то есть тот же
// бросок при другом порядке попадает в ДРУГУЮ строку. Вес каждой строки не
// тронут (он выведен из её hp × dps), но дикий спавн мира СДВИНУЛСЯ, и
// побайтовый паритет с прежним миром недостижим ПО ПОСТРОЕНИЮ. Это принятый
// вердикт владельца 2026-10-20, а не упущение: структура старше чисел (ЗАКОН
// ПРИОРИТЕТА СТРУКТУРЫ — «НИКОГДА НЕЛЬЗЯ СИСТЕМУ ПОДГОНЯТЬ ПОД ЧИСЛА»).
//
// ВТОРОЙ ТАБЛИЦЫ СУЩЕСТВ НЕТ И НЕ БУДЕТ: каталог монстров когда-то жил второй
// таблицей по адресу `0x100 | row` — вторым словарём, записанным числом, — и
// умер 2026-08-20 (владелец). Волк здесь такая же строка, как страж, и вопроса
// «монстр ли это» движок больше не задаёт.
enum class NPCType : std::uint8_t {
    // ── Folk: население мест ──────────────────────────────────────────────
    Peasant = 0,
    Merchant,
    // Разбойник и дорожный засадник — ОДИН тип `bandit`: засадник дерётся как
    // бандит во всём, что важно (то же тело, тот же бой, тот же лут), и
    // отличается ОДНОЙ колонкой — он видит всю дорогу (владелец 2026-09-09).
    // Своя строка, а не подкрученный бандит: зрение есть свойство существа,
    // поэтому существо, лежащее в засаде, есть существо, а не флаг.
    Bandit,
    RoadAmbusher,
    Guard,
    // Ведьма и сорка — ОДИН тип `mage`: обе кастуют своим спеллом из одной
    // таблицы спеллов, а не своей третьей силой.
    Witch,
    Sorceress,
    // Культист — СВОЙ тип, и это вердикт владельца (2026-10-20): «да культисты
    // отдельный тип», «они точно не маги». У него своя фракция и свой ареал
    // подземелья, а не кафедра ведьмы.
    Cultist,
    // Строка ИГРОКА (вердикт владельца 2026-08-27: «игрок = обычный сквад,
    // просто с флажком»). Его макро-сквад — обычная сущность, а обычная
    // сущность называет строку ЭТОЙ таблицы, значит игроку строка была нужна.
    Adventurer,
    // Носитель феодального графа (CANON S24): несёт десятину города в столицу
    // — «налог течёт по рёбрам носителями».
    TaxCollector,

    // ── Monster: разумная нечеловеческая тварь ────────────────────────────
    Goblin,
    Kobold,
    Orc,
    Troll,
    Ogre,
    Minotaur,
    // ЙЕТИ — верхушка СНЕГА (M-382), тип `Giant`, поэтому стоит в группе
    // великанов, а не рядом с «другими верхушками»: группы по ТИПУ, и
    // «верхушка биома» типом не является — это высказывание КОЛОНКИ ТИРА.
    Yeti,
    Harpy,
    // Мантикора и грифон — верхушки СТЕПИ и ДОЛИНЫ, тип `Harpy` (крылатая
    // тварь), поэтому группа гарпии выросла с одной строки до трёх.
    Manticore,
    Griffin,
    // Дракон (владелец 2026-09-10, стол анкет): первый ЛЕТАЮЩИЙ боец —
    // cruiseM > 0 рождает ecs::Flying, тот же честный полёт, что у игрока
    // (один конверт, один закон), а огненный шар — обычные Missile-колонки,
    // которыми пользуется любой стрелок.
    Dragon,

    // ── Animal: животные нашего мира ──────────────────────────────────────
    // Порядок внутри архетипа — ПО ТИПАМ (грызуны, псовые, гады, птицы…), а не
    // «слабейший → сильнейший», как было до наряда. Прежний порядок объяснялся
    // тем, «в каком порядке их читает закон спавна», и это было неверно уже
    // тогда: землю строке назначает её КОЛОНКА ТИРА (`baseLevel`, дверь
    // `creature_tier`), которую `danger_match` сводит с опасностью клетки, —
    // ни одна из двух дверей места в перечислении не читает.
    Rabbit,
    GiantRat,
    Deer,
    Fox,
    Wolf,
    DireWolf,          // верхушка ТУНДРЫ — `Canine`
    Bear,
    Shatun,            // верхушка ТАЙГИ — `Bear`
    Boar,
    GreatBoar,         // верхушка ЛУГА — `Boar`
    Snake,
    Croc,
    Basilisk,
    SeaSerpent,
    Hydra,             // верхушка БОЛОТА — `Reptile`
    SandWyrm,          // верхушка ПУСТЫНИ — `Reptile`
    JungleTyrant,      // верхушка ТРОПИКОВ — `Reptile`
    Hawk,
    Eagle,
    Roc,               // верхушка ГОРЫ — `Bird`
    Frog,
    // ── Морские: население воды (M-382) ───────────────────────────────────
    // Порядок внутри архетипа — ПО ТИПАМ, поэтому морские стоят ОДНОЙ группой
    // за амфибией, а не в хвосте перечисления: «дописать в хвост, чтобы
    // ординал не переехал» — мёртвый довод (ЗАКОН ЛЕСТНИЦЫ п.3, формат сейва
    // ограничением не является). Внутри группы — лестницей тира.
    // МОРСКОГО ЗМЕЯ ЗДЕСЬ НЕТ, И ЭТО СКАЗАЛ КОМПИЛЯТОР: он `Reptile`, а «все
    // строки типа» обязаны быть НЕПРЕРЫВНЫМ отрезком, поэтому он стоит в
    // группе гадов выше — рядом со змеёй, крокодилом и базиликом.
    Squid,
    Kakula,
    Shark,
    Whale,
    CaveBat,
    CaveSpider,
    SandScorpion,

    // ── Livestock: скот и домашние животные ───────────────────────────────
    // ЛОШАДЬ — ЮНИТ, А НЕ ПРЕДМЕТ (CANON S10, владелец 2026-09-19): урожай
    // пастбища есть СУЩЕСТВО в существа, поэтому это строка ЭТОЙ таблицы, как
    // всякое существо — ест своей колонкой содержания, несёт своей `haulMult`,
    // продаётся одной дверью найма.
    Horse,
    // ОВЦА И КОЗА — ТАКИЕ ЖЕ СТРОКИ, И ЭТО ВЕРДИКТ ВЛАДЕЛЬЦА (2026-10-12,
    // дословно: «и ещё естественно лошадки и овехи будут просто новыми
    // строками в таблицей мобов - системный подход»). Ни своего ресурсного
    // ряда, ни своей системы: число голов живёт числом ФИЧИ
    // (`FeatureDef::numberRow`), а КАКИЕ это головы, говорит фича клетки —
    // ровно как хлеб и лён работают один ряд пашни.
    Sheep,
    Goat,

    // ── Void: хтонь и демоны ──────────────────────────────────────────────
    Skeleton,
    Zombie,
    Ghoul,
    Wraith,
    IceWraith,
    StoneGolem,
    Gargoyle,
    Imp,
    SwampThing,
    Lich,

    Count,
};

// ЧТО ЭТА СТРОКА ЕСТЬ — ОТВЕЧАЕТ ЛЕСТНИЦА, А НЕ СВОЁ ПЕРЕЧИСЛЕНИЕ.
//
// ЗДЕСЬ СТОЯЛА `NpcNature` {Human, Fauna, Void} И УМЕРЛА В НАРЯДЕ M-295 вместе
// с `ItemType` предметной половины: это была ОДНА И ТА ЖЕ ступень лестницы,
// названная в коде ДВАЖДЫ и разными словами, поэтому объект-модулям
// прицепиться было некуда ни к одной половине целиком. Вердикт владельца
// 2026-10-20, дословно: «ну и да ясно вижу что групировки существ и типы
// предметов это явно архетипы».
//
// ПРИ ПЕРЕЕЗДЕ ОСЬ РАСШИРИЛАСЬ С ТРЁХ ЗНАЧЕНИЙ ДО ПЯТИ, И ЭТО ТОЖЕ ЕГО
// ВЕРДИКТ («нужно архетипы сущесвут расшириь»; итог — «ФОЛК / МОНСТЕР / АНИМАЛ
// /ЛАЙВСТОК / ВОИД такие вот архнтипв»). Причина структурная: `Fauna` была
// СВАЛКОЙ «все нелюди кроме нежити» — 27 строк из 47, от кролика до дракона, —
// а прежний вердикт «этих 3 хватит для всего» был дан, когда свалку ещё не
// разбирали. Ось РАЗУМНОСТИ её разобрала: `Monster` — разумная нечеловеческая
// тварь, `Animal` — животные нашего мира, `Livestock` — домашние.
//
// ОСЕЙ У СТРОКИ ПО-ПРЕЖНЕМУ ТРИ, И ЧЕТВЁРТОЙ НЕ ЗАВОДИТСЯ: `haulMult` —
// сколько веса несёт, `AIBehaviour` — как действует, ТИП (ниже) — кто это.
// Расширяется это СТРОКОЙ `tables/object_type.h`, и никогда четвёртой
// колонкой. Четвёртая ось однажды была — тег роли (None/ездовое), чей
// единственный вопрос «ездовая ли строка» нужен был только закону упряжки; он
// умер вместе с ним (M-230). Лошадь ездовая потому, что её спина больше
// человеческой (`haulMult` 8.0), а не потому, что кто-то назвал её ездовой
// вторым словом (S26).

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

// ОСЬ НРАВА НЕСЁТ РОВНО ТО, ЧТО МИР ЧИТАЕТ (ревизия 2026-11-08, вердикт
// владельца «снести» по DOD п.9). Здесь стояли ВОСЕМЬ значений —
// `Greedy, Honorable, Cowardly, Brave, Aggressive, Generous, Suspicious,
// Curious`, — и ПЯТЬ из них были колонкой-сиротой чистейшего вида: ролл
// выдавал их каждому скваду при рождении (`npc_spawn.cpp`), они ехали в сейв
// на все 16384 слота — и читал их ТОЛЬКО серый тултип
// (`npc_trait_label@src/ui/overlays.cpp`). Механику читают трое, и они
// остались: `Cowardly`/`Brave` → `bravery_of@src/macro/npc_ai.cpp`,
// `Greedy` → `encounter_payoff_cost@src/app/main.cpp`.
//
// Новый нрав заводится ОДНОЙ строкой — тогда, когда у него появится
// читатель-механика, а не «на будущее»: это тот же класс дефекта, что
// `statusEffect = "burning"`, который читал один тултип.
enum class NPCTrait : std::uint8_t {
    Greedy = 0, Cowardly, Brave,
    Count,
};

// (Реестр «нрав купца ↔ цена» — kTraitPriceRows — вырезан 2026-09-19 вместе
// с настроением места: коэффициент поверх цены это тот же спред ×0.7, от
// которого мир уже избавился. У сделки одна кривая и разница торговых сил.)


// Fixed-arity name / dialogue pools — POD-friendly.
constexpr std::size_t kMaxNpcNames     = 16;
constexpr std::size_t kMaxNpcTalkLines = 6;
constexpr int kNpcUpkeepNone = -1;

// The defence scale, the 8-type symmetry and THE defence law all live in
// tables/damage_types.h (kPlainBlow, kArmorFull, Defense, mitigate_amount) — one
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

// ── ПОЛОСА СПАВНА — ОТВЕТ МОДУЛЯ КЛЕТКИ, А НЕ КОЛОНКА РОДА МЕСТА ─────────
// (M-371, вердикт владельца 2026-11-02: «фауна точно с фиачами не связана»;
// обитатели руины «могут спавниться и по модулую (рандом распред) и +
// контекст полей зоны».) Кто и сколько роллится на клетке, объявляет сам
// МОДУЛЬ — строка модуля генерации субмира (kFeatureGenRows) или род норы
// данжа (kDungeonKindRows); bit 0 = «земля отвечает»: лес/биом клетки и их
// же счётные строки. Полоса авторится ИМЕНАМИ битов kHab* выше — второго
// правописания нет по построению.
struct FaunaBand {
    std::uint16_t bit = 0;       // habitat-бит таблицы тел; 0 = земля
    std::uint8_t  minCount = 0;  // голов за визит, когда bit != 0
    std::uint8_t  maxCount = 0;
};

// ЕДИНИЦА КОЛОНКИ `haulMult` — ОДНА ЧЕЛОВЕЧЕСКАЯ СПИНА В КИЛОГРАММАХ, и она
// стоит здесь потому, что ЗДЕСЬ её ВЫВОД: вся колонка авторена одной цепочкой
// килограммов — человек ~15 кг, вьючный конь ~120 → 8, вьючная коза ~10 → 0.7,
// овца с панньерами ~8 → 0.5. Число есть ЗНАМЕНАТЕЛЬ этой цепочки, то есть
// единственное место, где она записана как величина, а не как пересказ.
//
// ПОЧЕМУ В КИЛОГРАММАХ (ЗАКОН ДВУХ ТАБЛИЦ, вердикт владельца 2026-10-20:
// «АНКЕТВ НЕ МОЖЕТ БЫТЬ У ОБЪЕКТОВ … объекты это буквально объекты для
// инвентарей сквадов (тех у кого анкеты)»): строка этой таблицы есть ОБЪЕКТ,
// и всё, что он несёт, — своя величина, а не доля чужой. ЗАМЕР, КУПИВШИЙ
// ПРАВКУ (M-297): прежде голова коня шла в обоз как `анкета сквада 154 кг × 8`
// = **1232 кг** против тех ~120, из которых 8 и выведено, — перекос **10.3×**,
// и жил он ровно потому, что коэффициент считали от килограммов, а применяли
// к анкете.
//
// Число — БАЛАНС (§3 ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ): менять его можно молча,
// переносить в строку — нельзя, иначе цепочка перестанет иметь знаменатель.
//
// В ГРАММАХ, ЦЕЛЫМ (вердикт владельца 2026-10-20: «у нас теперь кванты грамы
// это целове универсальное решение»). Единица сменилась, значит сменилось и
// ИМЯ — иначе оно врало бы про квант на три порядка, ровно та ложь, которую
// чинил `261df951` (ЗАКОН ИМЕНИ п.5: дефект чисто в имени лечится
// автосменой). Знаменатель цепочки не сдвинулся ни на грамм: 15 кг = 15 000 г.
inline constexpr std::int32_t kHaulBackG = 15'000;

struct NpcTypeDef {
    // СВОЙ ОРДИНАЛ — MUST equal the row's index in kNpcTypeDefs (guard below
    // the table). Колонка звалась `type`, и имя пришлось отдать: `type` есть
    // СТУПЕНЬ ЛЕСТНИЦЫ, одна на обе половины мира, а этот член отвечает на
    // другой вопрос — «которая я строка». Два разных вопроса одним словом и
    // есть то, из чего следующий читатель выводит механику, которой нет
    // (ЗАКОН ИМЕНИ); правка механическая, автосменой (M-295).
    NPCType         row;
    // Stable machine id — what the console, the spawn tables and any future
    // content file NAME this row by ("peasant", "wolf"). The creature rows
    // brought it with them; the humanoid rows had only a display label, and a
    // display string is not an id.
    const char*     id;
    const char*     label;
    // This kind's picture — a row of THE sprite table (tables/sprite_rows.h),
    // which decides drawn art vs procedural body. Kinds share rows on purpose:
    // every unremarkable townsman is a peasant to the eye. It replaced a dead
    // `portrait` path string that no code ever read — a fourth asset vocabulary
    // nobody was speaking.
    SpriteId        sprite;
    // NO baseHp column — the ONE hp floor a body fights with is its
    // CombatTemplate's `hp` (the sheet projection's own floor); a second
    // number here was never read.

    // ── АВТОРСКИЙ ТИР СТРОКИ: «НАСКОЛЬКО ЭТА ТВАРЬ ВЫСОКО В МИРЕ» ─────────
    //
    // ОДНА ЛЕСТНИЦА 1..255 НА ВЕСЬ МИР — `kMaxTier@src/tables/progression.h`,
    // вердикт владельца 2026-11-03: ✓ «у нас есть УЖЕ ЗОНЫ СЛОЖНОСТИ И ТИРЫ
    // ПРЕДМЕТОВ ОЧЕВИДНО ПРОСТО НАДО СЛИТЬ ЭТО В ОДНУ СИСТЕМУ … они уже u8 255
    // НА ЭТОМ И РЕШИЛИ». На той же лестнице стоят опасность клетки
    // (`ZoneLayer@src/macro/zones.h`) и тир предмета
    // (`ItemRef::level@src/tables/items.h`), поэтому мир СОПОСТАВЛЯЕТ их без
    // переводного курса — сопоставление и есть причина единых единиц. Что тир
    // КАЖДОЙ строки стоит на лестнице, судит компилятор:
    // `npc_tiers_on_ladder` под таблицей рядов.
    //
    // ВЫВОДИТЬ ТИР ИЗ МОЩИ ЗАПРЕЩЕНО, И ЭТО ВЕРДИКТ, А НЕ ВКУС: ✓ «мы же
    // решили отказаться ваще от мощи и умножения хп на дпс … у нас же просто
    // таблица объектов и написан тир моба»; ✓ «могут быть твари мало хп куча
    // резистов и огромнвй урон не умничай где нюансы есть». `hp × удар/период`
    // не видит ни резистов (`Defense::armor` по восьми родам урона), ни блока,
    // ни дальнобойности, ни правил, ни того, что у кастера удар есть СПЕЛЛ, а
    // не его собственные кубы. Число пишется РУКАМИ, и `tier_of_power`,
    // пытавшаяся его считать, снесена в тот же день (M-384).
    //
    // ГДЕ СТОИТ КАТАЛОГ ПОСЛЕ ПЕРЕАВТОРИНГА (M-385): занято 1..120 из 255.
    // Якорь — КАНОНИЧЕСКИЙ «эндгейм ≈ 100» (CANON S14 «он про то, где живёт
    // игра»), поэтому народ и мелкая живность стоят у пола, лич встал РОВНО на
    // якоре (100), а дракон, вершина каталога, на 120 — ВЫШЕ якоря и далеко НЕ
    // у потолка: 135 ступеней (53 % лестницы) оставлены пустыми НАРОЧНО, иначе
    // первая же новая сильная строка упёрлась бы в 255 и стала бы равна
    // дракону В ЦЕНЕ. Полосы: 1 — не угроза вовсе · 2..6 — мелочь · 7..14 —
    // первая настоящая угроза · 16..28 — ветеранская · 38..50 — элита ·
    // 55..75 — чудовища · 100..120 — эндгейм. До M-385 занято было 1..10, цена
    // убитого шла 11 → 200 (разброс ×18); теперь 11 → 15 600 (×1418).
    //
    // КТО ЭТО ЧИТАЕТ (DOD п.9 — у колонки обязан быть названный читатель):
    //   · ЦЕНА УБИТОГО — `head_xp_reward` ниже, через `creature_tier`. Пол
    //     лестницы платит: тир 1 стоит 11 опыта, а не ноль;
    //   · ЭКОНОМИКА — `hire_price_for` и `soldier_upkeep` ниже, через
    //     `soldier_level_factor@src/tables/army.h` = 1 + (уровень−1)/3: тир
    //     едет уровнем головы в контейнер (`creature_slot@src/bus/world_row.h`),
    //     и множитель стражника пошёл 1 → 5 (найм 90 → 450, содержание 3 → 15
    //     золотых в день). Прочие платные строки не дрогнули ни на монету: у
    //     крестьянина, сборщика десятины и лошади тир ≤ 3, то есть множитель
    //     остался единицей, а 43 строки из 47 несут `hireGold == 0` вовсе;
    //   · СИД УРОВНЯ ЭКЗЕМПЛЯРА — `npc_spawn.cpp` (тир + rng%4, лист аватара) и
    //     `sub/spawn.cpp` (тир + rng%2, тело в сцене). Это ДОЛГ, названный
    //     остатком M-384 («колонка служит СРАЗУ двум вещам — сидом уровня
    //     экземпляра и тиром»), и переавторинг его ОБНАЖИЛ числом: средний hp
    //     аватара тролля 186 → 839 (×4.5), медведя 126 → 497 (×3.9), дракона
    //     538 → 740 (×1.4). Разводить два смысла — M-386/M-387, не здесь.
    //
    // ЧИСЛА — БАЛАНС (§3 ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ: «НИКОГДА НЕЛЬЗЯ СИСТЕМУ
    // ПОДГОНЯТЬ ПОД ЧИСЛА»), и двигать их можно молча. Авторская воля в них
    // одна — ПОРЯДОК; семя порядка есть `hp × удар/период` строки, а всякое
    // отклонение от семени суть суждение о том, чего семя не видит.
    int             baseLevel;
    AIBehaviour     ai;
    CombatTemplate  combat;
    int             upkeepGoldPerDay;
    bool            hireable;
    // ── ЗДЕСЬ ЖИЛА КОЛОНКА `xpReward`, И ОНА УНИЧТОЖЕНА 2026-11-03 ────────
    //
    // ЧЕМ ОНА БЫЛА: «ONE XP law» 2026-08-29 — каждое убийство платило
    // `xpReward + (level−1)·5`, а строки существ авторили `5·(baseLevel+1)`,
    // чтобы итог сошёлся в 10·L₀.
    //
    // ПОЧЕМУ УМЕРЛА. Во-первых, она была ВТОРЫМ ОТВЕТОМ на вопрос «чего стоит
    // эта тварь»: у строки уже есть `baseLevel`, то есть её ТИР, а цена жила
    // отдельным числом и расходилась с ним молча — восемь строк из сорока семи
    // отклонялись от собственной же формулы `5·(baseLevel+1)`, причём
    // `Adventurer` платил НОЛЬ, а дракон — в девять раз больше выводимого.
    // Во-вторых, закон был ЛИНЕЙНЫМ (10·L) против КВАДРАТИЧНОГО порога уровня,
    // и замер по формулам дал инфляцию гринда: убийств ровни на уровень
    // 110 → 1100 на дистанции 1..100.
    //
    // ЧТО ВМЕСТО: цена есть `exp_to_next_level(ТИР строки)/100`, а тир читается
    // ОДНОЙ дверью `creature_tier` ниже — из авторской колонки `baseLevel`.
    // Авторская воля не ушла никуда: она и была в этой колонке, просто цена
    // перестала быть её вторым, несогласованным спеллингом.

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
    // context, and the ladder is ONE rung high (M-39): a place's crowd and
    // garrison wear THAT PLACE's own factionIdx, a squad wears its avatar's,
    // and everything the LAND rolls wears this row's wildFaction column
    // (macro/fauna.cpp). The same wolf can be wildlife in a meadow or the
    // player's own; what it can no longer be is a demon because the KIND of
    // the place it stands near said so.
    // КОЛОНКИ ЛУТ-ПРОФИЛЯ БОЛЬШЕ НЕТ (M-139, вердикт владельца 2026-09-26):
    // `lootId` называл хардкод-таблицу «что роняет эта роль», и вместе с
    // десятью такими таблицами снесён. Что несёт тварь, назовёт ПУЛ ЛУТА по
    // стоимости, весам единой таблицы объектов и контексту.
    // Body radius in metres — THE one width column of the one body table
    // (damage-door Inc 4: CombatTemplate's shadow copy is gone). 0 = the
    // man-shaped default (npc_body_radius below); the creature rows author it
    // because a rabbit is not a man-sized thing. The same number scales the
    // creature's sprite, so visual size and hit size cannot drift.
    float           radius     = 0.0f;

    // КАКИЕ СЛОТЫ ЭКИПИРОВКИ У ЭТОГО ТЕЛА (M-183): маска глобальной
    // раскладки `тип×16+n` (tables/body_parts.h). Дефолт — гуманоид: сегодня
    // экипируются СКВАДЫ (вердикт владельца: «у нас экипируются сквады
    // именно, а не существа»), а лидер сквада — гуманоид. Звериные строки
    // держат тот же дефолт СОЗНАТЕЛЬНО, пока зверю нечего надеть: точная
    // маска зверя — авторская правка данных, не системы.
    SlotMask        slots      = kHumanoidSlots;

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
    // this is a person. The number is DATA and the owner's to retune.
    //
    // ЧИТАТЕЛЬ У КОЛОНКИ РОВНО ОДИН, И ОН ПРО ОБЪЕКТЫ В ИНВЕНТАРЕ:
    // `refresh_squad_carry@src/macro/squad.h` — спины ГОЛОВ контейнера, каждая
    // своей строкой, в килограммах через `kHaulBackKg` выше. Здесь стояло «its
    // readers are the two cargo doors of squad.h and nobody else», и это было
    // неверно ДВАЖДЫ (перепись M-297, 2026-10-20): читателей было ТРИ, а не
    // два (третий — скор крю в `npc_ai.cpp`), и один из них читал колонку не о
    // том — множил на неё АНКЕТУ самого сквада. Спина сквада колонки СТРОКИ не
    // знает вовсе: её даёт анкета и её СИЛА (ЗАКОН ДВУХ ТАБЛИЦ — анкета у
    // сквада, объект в инвентаре; наверху массовка есть циферки, а не актор).
    // Last field on purpose, beside the light block: an opt-in column at the
    // end costs no other row a comma.
    //
    // ── БЕЗРУКИЕ СТРОКИ ВЫСКАЗЫВАЮТСЯ САМИ (вердикт владельца 2026-10-12) ──
    // Умолчание «1 = человек» ВЕРНО и остаётся, но до этого дня молчали ВСЕ
    // звери, то есть волк, кролик и змея в контейнере заявляли полную
    // человеческую спину. Ось здесь НЕ «природа строки»: гоблин, орк, тролль,
    // огр, минотавр, кобольд и гарпия лежали в свалочной `NpcNature::Fauna`
    // вместе с кроликом, но у них ЕСТЬ РУКИ, и умолчание им верно. С наряда
    // M-295 они архетипа `Monster`, и ось эту свалку разобрала — но РУКИ ей
    // всё равно не ось: она про РАЗУМ (вердикт владельца), а безрукость есть
    // вопрос ЭТОЙ колонки. Высказываются те, у кого рук нет:
    // четыре лапы, крылья, хвост.
    //
    // ЧИСЛА — ТОЙ ЖЕ ЦЕПОЧКОЙ КИЛОГРАММОВ, ЧТО У ЛОШАДИ (человек ~15 кг,
    // вьючный конь ~120 → 8), и СПИНА ЕСТЬ ТАМ, ГДЕ САДИТСЯ УПРЯЖЬ:
    //   · конь 8 — вьючное седло;
    //   · коза 0.7 — вьючная коза несёт ~10 кг, то есть 10/15;
    //   · овца 0.5 — панньеры ~8 кг (владелец: «овечке можно дать немножко»);
    //   · всё прочее безрукое — 0: ни кролика, ни волка, ни ястреба, ни
    //     дракона в этом мире не запрягают, и выдумывать им число значило бы
    //     взять его с потолка. Запрягут — строка получит его одной правкой.
    // ОСТАТОК НАЗВАН: рукастые тяжёлые строки (тролль, огр, минотавр) несут
    // ровно человеческую спину, хотя по массе должны больше. Это КОНТЕНТ, не
    // дефект формы: лестница масс для них не выведена, и до вывода 1.0 —
    // честное «как человек», а не молчание.
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
    // TWO columns of eight since the 8×8 symmetry (CANON S13) — percent
    // armour and flat block, units the damage's own, because all of them meet
    // in mitigate_amount(). Rows authored as one number use uniform_armor(x);
    // a row with rigid shell authors block too (uniform_defense) — a HIDE has
    // armour and no block, a GOLEM is what the block column is for (owner,
    // 2026-09-30). Per-column authoring is content-stage work.
    //
    // РЕКОВЕРИ У СТРОКИ СУЩЕСТВА НЕТ ВОВСЕ (owner, 2026-09-30: «0 для строки
    // существа — у неё вообще нет рековери брони НО ЕСТЬ колонка блока»):
    // вросшую шкуру не сбивают, и простой (M-194) живёт только у НАДЕТОГО.
    Defense defense{};

    // WHAT A BODY OF THIS ROW COSTS to take into a creatures, in gold at its
    // level-1 worth (CANON S25: a creature's price is a column of its row,
    // exactly like a sword's — not a formula living beside the item prices).
    // DERIVATION: the row's own upkeepGoldPerDay × 30 days — a recruit is
    // bought for a month of his pay, which is byte-for-byte the price
    // `hire_price_for` computed inline until 2026-08-29 (Peasant 1×30 = 30,
    // Guard 3×30 = 90). Level scales it through THE one level law
    // (army.h soldier_level_factor), applied by the reader; 0 — every row
    // that omits it, i.e. everything with no upkeep — is not for sale.
    int hireGold = 0;

    // ── ЧТО ЭТО ЗА СУЩЕСТВО: ВЛИТЫЕ СПУТНИКИ (CANON S26 «Одна строка на
    // род», владелец 2026-09-22: «просто надо сделать ЕДИНУЮ таблицу реестр
    // всех нпц, и каждый нпц/моб в игре это просто строка в ней»).
    // Здесь стояли ТРИ отдельных массива по тому же ординалу NPCType —
    // kNpcNature, kNpcPurse, kNpcMapColor. Ни один не был решением: все три
    // были обходом позиционной инициализации, которой в этой структуре
    // больше нет. Их свидетели `rows_in_enum_order` ушли вместе с ними —
    // колонке строки не нужен свидетель порядка, она И ЕСТЬ строка.

    // ВВЕРХ ПО ЛЕСТНИЦЕ — КОЛОНКА: строка несёт ТИП, тип несёт АРХЕТИП
    // (`tables/object_type.h`). Одна ось на обе половины мира, поэтому
    // объект-модуль ключуется ею и у предмета, и у существа.
    //
    // УМОЛЧАНИЯ НЕТ СОЗНАТЕЛЬНО (как и у `ItemDef::type`): здесь стояло
    // `= NpcNature::Human`, то есть строка, забывшая высказаться, МОЛЧА
    // становилась народом — считалась населением места и размножалась. Теперь
    // ряд обязан назвать тип, и все 47 называют.
    //
    // Спрашивается ВМЕСТО ГРАНИЦЫ ОРДИНАЛА, которая врала: до наряда M-295
    // человеческие рода были рассыпаны по перечислению вперемежку со зверьём,
    // и «всё, что после Rabbit, — зверь» считало их зверьём. Теперь граница
    // честна по построению (рода идут по архетипам), но спрашивать её вместо
    // колонки всё равно нельзя: это АДРЕС, а не классификация.
    ObjectType type;

    // (КОЛОНКА `named` СНЕСЕНА 2026-11-08, M-436, И СНЕСЕНА НЕ КАК СИРОТА, А
    //  КАК ПОСЛЕДНИЙ НОСИТЕЛЬ ОТМЕНЁННОЙ МОДЕЛИ. Она говорила «ИМЕНОВАННОСТЬ —
    //  субъектность рода»: у именованного рода каждый экземпляр ВЛАДЕЕТ своим
    //  листом, а массовка выводит генерик-лист на месте и не хранит ничего. Эту
    //  модель владелец отменил вердиктом того же дня — РОДОВ АНКЕТЫ НЕ ДВА
    //  (ЗАКОН АНКЕТЫ п.3), — и предикат владения `owned_sheet`/`sheet_owned_at`
    //  был снесён (M-433 п.3, коммит `3eef37ba`). Колонка пережила свою модель
    //  ровно потому, что её единственный читатель — дверь `npc_named` — сам
    //  никем не звался: ноль звонящих в `src/` и в `tests/`, то есть ось
    //  делилась на два рода только на бумаге.)

    // ── ВЕСА РОЛИ: КУДА ЭТА СТРОКА ТРАТИТ БЮДЖЕТ АНКЕТЫ ─────────────────
    // Второй и ПОСЛЕДНИЙ спутник по ключу `NPCType`, влитый 2026-10-12
    // (M-263, вопрос владельца: «а почему обе таблицы? где единая таблица
    // объектов? не пон»). Таблица `kRoleWeights` жила отдельным файлом не по
    // замыслу, а по истории: слияние анкеты вынесло её из
    // `macro/character_sheet.h` в `tables/` и не довело до строки. Читатель у
    // неё в продакшене был ОДИН (`make_character_sheet@src/bus/anketa.h`), а
    // ключ — тот же ординал, то есть ровно «у двух таблиц один и тот же ключ»
    // (DOD п.9). Со слиянием исчез и её страж порядка `rows_in_enum_order`:
    // расходиться стало НЕЧЕМУ — колонка И ЕСТЬ строка.
    //
    // Веса ОТНОСИТЕЛЬНЫ: бюджет уровня раскладывается по ним, поэтому важны
    // только отношения, не величины. Полный набор обязателен у обеих колонок
    // (`role_attrs`/`role_skills` в `tables/attributes.h` — неполный набор
    // НЕСОБИРАЕМ, M-251); пустые веса законны и означают «этот род о навыках
    // думает ноль» — дверь генерации выходит из обучения сама.
    AttrWeights  attrWeights{};
    SkillWeights skillWeights{};

    // СКОЛЬКО ЕСТ В ДЕНЬ, в единицах голодной строки — ровно та же единица,
    // что у населения (econ_day.h kNeeds: 1 = один житель-день), поэтому
    // счёт контейнера и счёт населения складываются без переводного
    // множителя. Близнец `upkeepGoldPerDay`: тот говорит, сколько род
    // ПОЛУЧАЕТ, этот — сколько ПОТРЕБЛЯЕТ, и обе колонки читает одна дверь
    // счёта (upkeep_window.h upkeep_bill). 0 = не ест вовсе.
    int boardPerDay = 1;

    // КОШЕЛЬКА У РОДА БОЛЬШЕ НЕТ (M-139, вердикт владельца 2026-09-26):
    // колонки `purseMin`/`purseMax` печатали монеты из воздуха и читались
    // ровно двумя снесёнными выдачами (кошелёк при спавне, золото трупа).
    // Сколько стоимости несёт тварь, по контексту и весам универсальной
    // таблицы решит ПУЛ ЛУТА — авторской пары чисел на роль здесь не будет.

    // Цвет метки на карте мира.
    std::uint32_t mapColor = 0xFFFFFFFFu;

    // ГДЕ ЭТОТ РОД ВОДИТСЯ — битовая маска ареала (kHab* выше). Шестая и
    // последняя таблица-спутник (`kSpawnHabitats`, fauna.cpp) влита сюда
    // 2026-09-22: она жила в .cpp, то есть была невидима отсюда, и колонка
    // рода стояла в двух файлах сразу.
    std::uint16_t habitat = 0;

    // (Колонка `depositGate` УМЕРЛА 2026-10-23: ноль читателей за всю жизнь
    // (DOD п.9), а её ось — род жилы — снята вердиктом «жилы единые».)

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

// Humanoid recovery bases are authored ON THE MASS CURVE
// (`weapon_mass_recovery_ticks@src/tables/items.h`, owner 2026-09-07): a
// humanoid's natural attack IS an implied weapon, so its tempo = the strike
// row's base + implied_kg × kSwingTicksPerKg — the same ticks a real ItemDef
// of that heft would cost anyone's hand, so the day these rows hold real
// inventories nothing about their pace changes.
// Implied hefts: peasant/merchant/caravaner club-or-knife-and-fear ~3 kg (an
// untrained wide swing), woodcutter's felling axe ~2.5 kg, bandit blade and
// guard sword ~2 kg. Casters are not mass: their bolt tempo sits on the SPELL
// rows' own scale (combat spells author 1.5–4 s cooldowns — spells.h).
// Beast rows below keep their own authored tempos: a fang has no kilograms.
inline constexpr CombatTemplate kPeasantCombat   {25,{3,1}, 1.0f, 2.0f, steps_from_seconds(3.0f), "Psr", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWoodcutterCombat{30,{8,1}, 1.0f, 2.0f, steps_from_seconds(2.75f), "Wdc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kMerchantCombat  {30,{5,1}, 1.25f, 2.0f, steps_from_seconds(3.0f), "Mrc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kBanditCombat    {50,{12,1}, 2.25f, 3.0f, steps_from_seconds(2.5f), "Bnd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kGuardCombat     {55,{14,1}, 1.75f, 3.0f, steps_from_seconds(2.5f), "Grd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
// The adventurer's own template — the PLAYER's row. Base hp 100: the bare
// level-1 bar the sheet law always built for him (bar_ceilings' default
// base). Until landing 4 that base hid in a default ARGUMENT while his row
// shared the guard's 55 — a player-special number smuggled through a
// signature; now the row carries it like every other body's floor, and his
// ceilings go through body_max_hp like every other body's do.
inline constexpr CombatTemplate kAdventurerCombat{100,{14,1}, 1.75f, 3.0f, steps_from_seconds(2.5f), "Adv", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
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
inline constexpr CombatTemplate kAmbusherCombat  {100,{12,1}, 2.25f, 3.0f, steps_from_seconds(2.5f), "Amb", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu, /*bodyHeight*/0.0f, /*sight*/1000.0f};
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
inline constexpr CombatTemplate kWitchCombat     {60,{18,1}, 1.5f, 20.0f,steps_from_seconds(4.0f), "Wtc", CombatTemplate::Missile, 180, 0, 0xFFA070D0u, /*bodyHeight*/0.0f, kNpcSightDefaultM, /*mp*/100, /*sp*/100, DamageType::Blunt, /*cruiseM*/0.0f, /*actions*/{action_slot("magic_bolt"), kActionNone}};
inline constexpr CombatTemplate kSorceressCombat {70,{22,1}, 1.25f, 25.0f,steps_from_seconds(3.6f), "Src", CombatTemplate::Missile, 200, 6, 0xFF70C0E0u, /*bodyHeight*/0.0f, kNpcSightDefaultM, /*mp*/100, /*sp*/100, DamageType::Blunt, /*cruiseM*/0.0f, /*actions*/{action_slot("lightning_chain"), kActionNone}};
// Дракон (владелец 2026-09-10): «маленькая армия в одном теле» — hp 500,
// огненный шар = обычные Missile-колонки (бласт 2.5 м — АоЕ, цвет огня),
// урон 3d20 Fire (тип — колонка dmgType ниже дефолтов, авторится в строке
// project_combat не трогается). ЛЕТУН: cruiseM 10 — честный полёт (конверт
// игрока), рождается с ecs::Flying; на карте марш не платит рельеф.
// bodyHeight 6 — башня, не человек (одна колонка, не ветка рендера).
inline constexpr CombatTemplate kDragonCombat    {500,{3,20}, 1.6f, 40.0f,steps_from_seconds(3.0f), "Drg", CombatTemplate::Missile, 260, 2.5f, 0xFF3060FFu, /*bodyHeight*/6.0f, /*sight*/60.0f, /*mp*/100, /*sp*/100, DamageType::Fire, /*cruiseM*/10.0f, /*actions*/{action_slot("fireball"), kActionNone}};

// КРЕСТЬЯНЕ РАБОТАЮТ ЗА ЕДУ (владелец 2026-09-18: «пусть будут 0, чтобы не
// нарушать единство систем — их зп 0 в деньгах»): upkeepGoldPerDay = 0 у
// крестьянских родов значит «на содержании ЕДОЙ, жалованья не берёт» —
// жалованье есть цена НАЁМНОЙ службы (гарнизоны, варбанды, контейнер игрока).
// kNpcUpkeepNone (−1) остаётся «не на содержании вовсе» (звери, монстры,
// бандиты). Цена найма — своя колонка hireGold, от нуля жалованья не
// зависит.
inline constexpr NpcTypeDef kNpcTypeDefs[std::size_t(NPCType::Count)] = {

    // ══ АРХЕТИП Folk — НАСЕЛЕНИЕ МЕСТ ════════════════════════════════════
    // Только эти души составляют население ландмарка, размножаются и заселяют
    // пустой город. Владелец, дословно (2026-10-20): «гуманоид это фолк да я
    // не прав» — имя `Humanoid` отклонено в пользу `Folk`, потому что архетип
    // отвечает «народ ли», а не «человекообразен ли».


    // Peasant
    {
        .row = NPCType::Peasant,
        .id = "peasant",
        .label = "Peasant",
        .sprite = SpriteId::Peasant,
        .baseLevel = 1,
        .ai = AIBehaviour::Gatherer,
        .combat = kPeasantCombat,
        .upkeepGoldPerDay = 0,
        .hireable = true,
        .weight = 55,
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
        .type = ObjectType::Peasant,
        .attrWeights = role_attrs(3, 3, 1, 1, 1, 1, 1, 2),
        .skillWeights = role_skills(0,0,2,1,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 1,0, 3,0,3,1,2, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xDCC8A0u,
        .habitat = kHabTown,
    },


    // Merchant
    {
        .row = NPCType::Merchant,
        .id = "merchant",
        .label = "Merchant",
        .sprite = SpriteId::Caravan,
        .baseLevel = 2,
        .ai = AIBehaviour::Trader,
        .combat = kMerchantCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 21,
        .names = {{"Kartash","Bazukin","Torgin","Menkov","Skaldin"}},
        .nameCount = 5,
        .talkLines = {{"Looking to trade? I have fine wares!",
          "Gold makes the world go round, friend.",
          "I travel between settlements. The roads are dangerous.",
          "Business has been slow. Perhaps you need something?"}},
        .talkCount = 4,
        .type = ObjectType::Merchant,
        .attrWeights = role_attrs(1, 2, 2, 1, 1, 3, 4, 3),
        .skillWeights = role_skills(0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,1,0,2, 2,0,0, 4, 0, 0,0,0,0),
        .mapColor = 0xF0C850u,
    },


    // Bandit
    {
        .row = NPCType::Bandit,
        .id = "bandit",
        .label = "Bandit",
        .sprite = SpriteId::Bandit,
        .baseLevel = 9,
        .ai = AIBehaviour::Aggressive,
        .combat = kBanditCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .names = {{"Razboy","Diki","Grozny","Slyak","Khvat"}},
        .nameCount = 5,
        .talkLines = {{"Your gold or your life!",
          "Heh, another fool wandering the wilds.",
          "I take what I want. Got a problem with that?",
          "The strong survive. The weak feed us."}},
        .talkCount = 4,
        .type = ObjectType::Bandit,
        .attrWeights = role_attrs(4, 3, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2, 0, 0, 0,0,0,0),
        .mapColor = 0xDC3C3Cu,
    },


    // Road ambusher — the prologue's teeth (owner 2026-09-09). A bandit in
    // body, sprite, behaviour and loot; his row exists for two columns the
    // world's bandits must NOT inherit: he watches the whole road
    // (kAmbusherCombat sight) and he cannot be beaten at level 1 (its HP).
    // Appended, so every saved ordinal stays where it was.
    {
        .row = NPCType::RoadAmbusher,
        .id = "road_ambusher",
        .label = "Ambusher",
        .sprite = SpriteId::Bandit,
        .baseLevel = 12,
        .ai = AIBehaviour::Aggressive,
        .combat = kAmbusherCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        // He drops what a bandit drops, named in his own column: his ordinal
        // sits in the creature stripe of the table, where the
        // per-role loot list no longer answers.
        .names = {{"Krivoy","Sukhoy","Gnily","Ryaboy","Tishina"}},
        .nameCount = 5,
        .talkLines = {{"We have been watching this road all morning.",
          "Nothing personal, traveller. The road is ours.",
          "Down. Stay down and it goes easier."}},
        .talkCount = 3,
        .type = ObjectType::Bandit,
        .attrWeights = role_attrs(4, 3, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2, 0, 0, 0,0,0,0),
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
        .row = NPCType::Guard,
        .id = "guard",
        .label = "Guard",
        .sprite = SpriteId::Peasant,
        .baseLevel = 14,
        .ai = AIBehaviour::Aggressive,
        .combat = kGuardCombat,
        .upkeepGoldPerDay = 3,
        .hireable = true,
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
        // is not the default. БРОНЯ ТЕПЕРЬ ПРОЦЕНТ (M-209), поэтому число
        // ПЕРЕКАЛИБРОВАНО с 10 на 40: прежняя десятка означала «срезает
        // половину плоского удара» по старой гиперболе, а на процентной шкале
        // то же намерение читается как «срезает 40 % — дисциплинированный танк
        // в выданных латах, но до полосы иммунитета ему далеко» (игрок живёт в
        // 0…60, CANON S13). Это калибровка, а не структура, и менять её можно
        // молча (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
        // И БЛОК У НЕГО ЕСТЬ, потому что выданные латы и есть тот жёсткий
        // панцирь, для которого колонка блока существует: 5 — половина плоского
        // удара (kPlainBlow), при балансной верхушке 2·kPlainBlow = 20 у лучшей
        // плиты. Шкура же блока не несёт — она гнётся, а не отводит.
        .defense = uniform_defense(40, 5),
        // The price column: upkeep 3 × 30 days.
        .hireGold = 90,
        .type = ObjectType::Guard,
        .attrWeights = role_attrs(4, 4, 1, 1, 2, 1, 2, 2),
        .skillWeights = role_skills(3,0,2,1,0,0,0, 3,0,0,3, 0,0,0,0,0,0, 2,0, 3,0,2,1,1, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x508CDCu,
        .habitat = kHabTown,
    },


    // Witch
    {
        .row = NPCType::Witch,
        .id = "witch",
        .label = "Witch",
        .sprite = SpriteId::Witch,
        .baseLevel = 26,
        .ai = AIBehaviour::Teleporter,
        .combat = kWitchCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .names = {{"Yaga","Vedma","Znakharka","Koldunia","Volshebnitsa"}},
        .nameCount = 5,
        .talkLines = {{"The spirits whisper of your coming...",
          "I see great trials ahead for you.",
          "Herbs and potions are my trade. Interested?",
          "The forest knows all. Listen carefully."}},
        .talkCount = 4,
        .type = ObjectType::Mage,
        .attrWeights = role_attrs(1, 2, 4, 4, 1, 2, 2, 3),
        .skillWeights = role_skills(0,0,0,0,1,0,2, 0,0,2,0, 2,2,0,2,0,1, 0,4, 0,4,1,1,0, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xB464C8u,
    },


    // Sorceress
    {
        .row = NPCType::Sorceress,
        .id = "sorceress",
        .label = "Sorceress",
        .sprite = SpriteId::Sorceress,
        .baseLevel = 55,
        .ai = AIBehaviour::Wanderer,
        .combat = kSorceressCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .names = {{"Charodejka","Zaklinatelnitsa","Mistika","Runara","Svetozara"}},
        .nameCount = 5,
        .talkLines = {{"The arcane currents shift around you...",
          "Few mortals seek me out willingly.",
          "I deal in mysteries beyond your understanding.",
          "Power has a price. Are you willing to pay?"}},
        .talkCount = 4,
        .type = ObjectType::Mage,
        .attrWeights = role_attrs(1, 2, 5, 4, 1, 3, 3, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,2, 0,1,2,0, 3,0,2,0,3,2, 0,5, 0,4,1,1,0, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x78C8E6u,
    },


    // Cultist — the man who serves the spire. The crowd's first SHOOTER:
    // ordinary Missile columns, arcane bolt, no blast — he is dangerous
    // because he stands behind the ogres, not because his numbers are big.
    {
        .row = NPCType::Cultist,
        .id = "cultist",
        .label = "Cultist",
        .sprite = SpriteId::Cultist,
        .baseLevel = 22,
        .ai = AIBehaviour::Aggressive,
        .combat = {45, {3,6}, 1.5f, 22.0f, steps_from_seconds(2.2f), "Cul", CombatTemplate::Missile, 190,
         0.0f, 0xFFA060E0u, /*bodyHeight*/1.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Arcane, /*cruiseM*/0.0f, /*actions*/{action_slot("magic_bolt"), kActionNone}},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.55f,
        .names = {{"Brother Vas","Sister Ilm","Novice Korr","The Pale Hand","Acolyte Zeb"}},
        .nameCount = 5,
        .talkLines = {{"The tower drinks, and we pour.",
          "You are late. It has already begun.",
          "Kneel, and it will be quick."}},
        .talkCount = 3,
        .type = ObjectType::Cultist,
        .attrWeights = role_attrs(2, 2, 4, 4, 2, 2, 2, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,2, 0,1,0,0, 2,0,0,0,3,1, 0,4, 1,3,0,1,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x50306Au,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },


    // The player. An ordinary row of the ordinary table (owner, 2026-08-27),
    // because his macro squad is an ordinary squad and a squad names a row
    // here. `weight` 0: the world never rolls an adventurer out of thin air —
    // this row is reached BY NAME, by the one entity that wears the flag. Not
    // hireable, worth no XP (his death is a game-over, not a kill), and his
    // loot is the bag he actually carries rather than a rolled profile.
    {
        .row = NPCType::Adventurer,
        .id = "adventurer",
        .label = "Adventurer",
        .sprite = SpriteId::Peasant,
        .baseLevel = 1,
        .ai = AIBehaviour::Wanderer,
        .combat = kAdventurerCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Adventurer,
        .attrWeights = role_attrs(2, 2, 2, 2, 2, 2, 2, 2),
        .skillWeights = role_skills(1,1,1,1,1,1,1, 1,1,1,1, 1,1,1,1,1,1, 1,1, 1,1,1,1,1, 1,1,1, 1, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
    },


    // Tax-collector — the feudal graph's own courier (owner 2026-08-30)
    {
        .row = NPCType::TaxCollector,
        .id = "tax_collector",
        .label = "Tax-collector",
        .sprite = SpriteId::Peasant,
        .baseLevel = 2,
        .ai = AIBehaviour::TaxRun,
        .combat = kWoodcutterCombat,
        .upkeepGoldPerDay = 0,
        .hireable = true,
        .weight = 21,
        .names = {{"Foka","Yeremey","Lavrenty","Sofron","Nikanor"}},
        .nameCount = 5,
        .talkLines = {{"The crown's eighth, weighed and sealed.",
          "Rob me and you rob the capital - think on that.",
          "Every realm stands on carried coin."}},
        .talkCount = 3,
        .hireGold = 30,
        .type = ObjectType::Taxman,
        .attrWeights = role_attrs(2, 3, 2, 2, 3, 1, 2, 2),
        .skillWeights = role_skills(0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,2,3,2, 3,0,0, 2, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
    },

    // ══ АРХЕТИП Monster — РАЗУМНАЯ НЕЧЕЛОВЕЧЕСКАЯ ТВАРЬ ══════════════════
    // Ось — РАЗУМ, А НЕ РУКИ, и это дословная поправка владельца 2026-10-20:
    // «дракон это будет монстер типа это будут не по смылсу рук а по
    // разумности - монстры они обладают разумом (огры троли гоблины драконы и
    // тд)»; и отдельно: «все орки в монстер».


    // Goblin
    {
        .row = NPCType::Goblin,
        .id = "goblin",
        .label = "Goblin",
        .sprite = SpriteId::Goblin,
        .baseLevel = 7,
        .ai = AIBehaviour::Aggressive,
        .combat = {25, {8,1}, 2.0f, 3, steps_from_seconds(1.0f), "Gbl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Goblinoid,
        .attrWeights = role_attrs(3, 3, 2, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,1,0,2,1,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 2,0,2,3,1, 2,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = kHabForest | kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },


    // Kobold — the goblin's smaller cousin, and the first thing in the game
    // that carries a purse worth taking off it.
    {
        .row = NPCType::Kobold,
        .id = "kobold",
        .label = "Kobold",
        .sprite = SpriteId::Kobold,
        .baseLevel = 4,
        .ai = AIBehaviour::Aggressive,
        .combat = {20, {2,4}, 2.2f, 3.0f, steps_from_seconds(1.1f), "Kbd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.1f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 5,
        .radius = 0.45f,
        .names = {{"Skree","Yip","Gnash","Tikka","Vess"}},
        .nameCount = 5,
        .talkLines = {{"Not yours! Not yours!",
          "Down the hole with you.",
          "Sharp! Sharp and quick!"}},
        .talkCount = 3,
        .type = ObjectType::Goblinoid,
        .attrWeights = role_attrs(2, 2, 2, 1, 4, 3, 1, 1),
        .skillWeights = role_skills(0,0,1,0,2,0,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 1,0,1,3,0, 1,2,2, 0, 0, 0,0,0,0),
        .mapColor = 0x8A6A3Au,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "demons",
    },


    // Orc — the raider of the open land, and the one new row that belongs
    // outdoors as much as underground.
    {
        .row = NPCType::Orc,
        .id = "orc",
        .label = "Orc",
        .sprite = SpriteId::Orc,
        .baseLevel = 18,
        .ai = AIBehaviour::Aggressive,
        .combat = {60, {2,8}, 1.8f, 3.0f, steps_from_seconds(1.4f), "Orc", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.6f,
        .names = {{"Gruth","Mazgar","Ukk","Snaga","Dorgul","Brakk"}},
        .nameCount = 6,
        .talkLines = {{"Blood for the warband.",
          "You walk where you should not.",
          "Come on then. Come on!"}},
        .talkCount = 3,
        .type = ObjectType::Goblinoid,
        .attrWeights = role_attrs(4, 4, 1, 2, 3, 2, 1, 1),
        .skillWeights = role_skills(1,3,0,0,0,0,0, 2,0,1,0, 0,0,0,0,0,0, 3,0, 3,0,2,2,0, 1,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0x5A7A4Au,
        .habitat = kHabForest | kHabRuin | hab(Steppe) | hab(Valley),
        .wildFaction = "demons",
    },


    // Troll
    {
        .row = NPCType::Troll,
        .id = "troll",
        .label = "Troll",
        .sprite = SpriteId::Troll,
        .baseLevel = 72,
        .ai = AIBehaviour::Aggressive,
        .combat = {120, {25,1}, 1.25f, 4, steps_from_seconds(2.0f), "Trl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Giant,
        .attrWeights = role_attrs(5, 5, 1, 1, 1, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },


    // Ogre — the wall of the crowd. 3d12 at 2.2 s: it hits like a siege and
    // misses a fleeing man completely.
    {
        .row = NPCType::Ogre,
        .id = "ogre",
        .label = "Ogre",
        .sprite = SpriteId::Ogre,
        .baseLevel = 70,
        .ai = AIBehaviour::Aggressive,
        .combat = {140, {3,12}, 1.2f, 4.0f, steps_from_seconds(2.2f), "Ogr", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/3.0f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Giant,
        .attrWeights = role_attrs(6, 6, 1, 1, 1, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,0,2, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x8A7050u,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "demons",
    },


    // Minotaur — the ogre's answer for a player who thought running was the
    // answer: nearly the same weight of blow, at a man's speed.
    {
        .row = NPCType::Minotaur,
        .id = "minotaur",
        .label = "Minotaur",
        .sprite = SpriteId::Minotaur,
        .baseLevel = 75,
        .ai = AIBehaviour::Aggressive,
        .combat = {130, {4,10}, 1.9f, 4.0f, steps_from_seconds(1.8f), "Min", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.6f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Giant,
        .attrWeights = role_attrs(6, 5, 2, 2, 3, 1, 1, 1),
        .skillWeights = role_skills(1,2,0,0,0,0,0, 1,0,2,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x6A3A2Au,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },


    // ВЕРХУШКА СНЕГА — йети, 90. Снег и тундра стояли с потолком 42, то есть самые
    // мёртвые земли мира были и самыми безопасными в нём.
    {
        .row = NPCType::Yeti,
        .id = "yeti",
        .label = "Yeti",
        .sprite = SpriteId::Yeti,
        .baseLevel = 90,
        .ai = AIBehaviour::Aggressive,
        .combat = {200, {4,11}, 1.5f, 2.4f, steps_from_seconds(1.9f), "Yti", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.6f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.1f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Giant,
        .attrWeights = role_attrs(8, 4, 2, 2, 5, 2, 1, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 3,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0xE0E8F0u,
        .habitat = hab(Snow),
        .wildFaction = "demons",
    },


    // Harpy — the crags' own. Cruises at 8 m, which is above a man's reach
    // and below a tower's crown: she is fought by looking UP.
    {
        .row = NPCType::Harpy,
        .id = "harpy",
        .label = "Harpy",
        .sprite = SpriteId::Harpy,
        .baseLevel = 9,
        .ai = AIBehaviour::Aggressive,
        .combat = {30, {2,7}, 2.9f, 3.0f, steps_from_seconds(1.0f), "Hrp", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.6f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, /*cruiseM*/8.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.55f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Harpy,
        .attrWeights = role_attrs(2, 3, 1, 2, 5, 3, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,5,0, 0,3,2, 0, 0, 0,0,0,0),
        .mapColor = 0xB07850u,
        .habitat = kHabSpire | hab(Mountain),
        .wildFaction = "demons",
    },


    // ВЕРХУШКА СТЕПИ — мантикора, 78 (прежний потолок степи 18).
    {
        .row = NPCType::Manticore,
        .id = "manticore",
        .label = "Manticore",
        .sprite = SpriteId::Manticore,
        .baseLevel = 78,
        .ai = AIBehaviour::Aggressive,
        .combat = {118, {4,8}, 2.2f, 2.2f, steps_from_seconds(1.3f), "Mnt", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, 4.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Harpy,
        .attrWeights = role_attrs(6, 6, 2, 2, 4, 2, 1, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x8A5A3Au,
        .habitat = hab(Steppe),
        .wildFaction = "demons",
    },


    // ВЕРХУШКА ДОЛИНЫ — грифон, 75 (прежний потолок долины 18). Тир И ЕСТЬ полоса:
    // на обжитой долине его не будет, на дикой — будет, и запрета для этого не
    // нужно ни одного.
    {
        .row = NPCType::Griffin,
        .id = "griffin",
        .label = "Griffin",
        .sprite = SpriteId::Griffin,
        .baseLevel = 75,
        .ai = AIBehaviour::Aggressive,
        .combat = {110, {3,11}, 2.4f, 2.0f, steps_from_seconds(1.4f), "Grf", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.7f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, 5.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Harpy,
        .attrWeights = role_attrs(6, 7, 2, 2, 3, 2, 1, 4),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0xB09050u,
        .habitat = hab(Valley),
        .wildFaction = "wildlife",
    },


    // Dragon (owner 2026-09-10, стол анкет): первый ЛЕТУН-боец —
    // kDragonCombat несёт cruiseM 10 (честный полёт, конверт игрока) и
    // огненный шар обычными Missile-колонками (3d20 Fire, бласт 2.5 м).
    // Ряд Wanderer по умолчанию — дизайн-дракон думает МОДУЛЕМ рода
    // Dragon (колонка type его строки стола → ai_dragon), лестница None
    // его не спрашивает; дикий, если когда-то родится спавн-таблицей,
    // будет просто кружить. weight 0 — вслепую мир его не выбрасывает:
    // дракон приходит только по имени (анкета, данж).
    {
        .row = NPCType::Dragon,
        .id = "dragon",
        .label = "Dragon",
        .sprite = SpriteId::Dragon,
        .baseLevel = 120,
        .ai = AIBehaviour::Wanderer,
        .combat = kDragonCombat,
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .radius = 1.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Dragon,
        .attrWeights = role_attrs(8, 6, 4, 5, 3, 2, 1, 4),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,0,0, 0,0,0,0,0,0, 0,0, 0,0,0,0,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xB03030u,
    },

    // ══ АРХЕТИП Animal — «ДЕФОЛТ ЖИВОТНЫЕ НАШЕГО МИРА КРОЛИКИ ВОЛКИ» ═════
    // (владелец, 2026-10-20). Прежняя колонка `NpcNature::Fauna` была СВАЛКОЙ
    // «все нелюди кроме нежити»: в ней лежали дракон и кролик, гоблин и овца —
    // 27 строк из 47. Ось разумности её и разобрала.


    // Rabbit
    {
        .row = NPCType::Rabbit,
        .id = "rabbit",
        .label = "Rabbit",
        .sprite = SpriteId::Rabbit,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {5, {0,1}, 2.75f, 0, steps_from_seconds(9.0f), "Rbt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 15,
        .radius = 0.4f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Rodent,
        .attrWeights = role_attrs(1, 4, 1, 1, 5, 3, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 0,0, 1,0,3,5,0, 2,3,2, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | kHabForest,
        .wildFaction = "wildlife",
    },


    // ── THE BESTIARY OF THE POPULATED PLACES (content, 2026-09-11) ────────
    //
    // Sixteen species authored against each other, not in isolation. Three
    // things decide where each of them ends up standing, and all three are
    // columns of this row — no spawn table names any of them by hand:
    //
    //   · `baseLevel` decides its RUNG, and `danger_match` halves a row's
    //     odds per 25 bytes of mismatch with the cell's danger. So authoring
    //     a TIER IS authoring a habitat band: an ogre cannot wander into a
    //     starting meadow because of how high an ogre stands, not because a
    //     list forbade it. (Это читалось «`combat` decides its STRENGTH» и
    //     звало выведенную `spawn_strength`; она снесена M-387 — ступень есть
    //     авторское высказывание, а не производное от hp × dps.)
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
        .row = NPCType::GiantRat,
        .id = "giant_rat",
        .label = "Giant Rat",
        .sprite = SpriteId::GiantRat,
        .baseLevel = 3,
        .ai = AIBehaviour::Aggressive,
        .combat = {14, {2,4}, 2.5f, 2.0f, steps_from_seconds(1.0f), "Rat", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.5f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 6,
        .radius = 0.35f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Rodent,
        .attrWeights = role_attrs(1, 3, 1, 1, 5, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,4,0, 0,2,2, 0, 0, 0,0,0,0),
        .mapColor = 0x6A5A4Au,
        .habitat = kHabRuin | hab(Mountain),
        .wildFaction = "wildlife",
    },


    // Deer
    {
        .row = NPCType::Deer,
        .id = "deer",
        .label = "Deer",
        .sprite = SpriteId::Deer,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {15, {2,1}, 2.5f, 2, steps_from_seconds(2.0f), "Der"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 12,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Deer,
        .attrWeights = role_attrs(2, 4, 1, 1, 5, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 2,0,3,5,0, 2,2,2, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | hab(Taiga) | kHabForest,
        .wildFaction = "wildlife",
    },


    // Fox
    {
        .row = NPCType::Fox,
        .id = "fox",
        .label = "Fox",
        .sprite = SpriteId::Fox,
        .baseLevel = 2,
        .ai = AIBehaviour::Wanderer,
        .combat = {12, {4,1}, 2.25f, 2, steps_from_seconds(1.2f), "Fox"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 8,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Canine,
        .attrWeights = role_attrs(2, 3, 2, 1, 4, 3, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,4,0, 2,1,3, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Taiga) | hab(Tundra) | kHabForest,
        .wildFaction = "wildlife",
    },


    // Wolf
    {
        .row = NPCType::Wolf,
        .id = "wolf",
        .label = "Wolf",
        .sprite = SpriteId::Wolf,
        .baseLevel = 10,
        .ai = AIBehaviour::Aggressive,
        .combat = {30, {10,1}, 2.5f, 3, steps_from_seconds(1.0f), "Wlf"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 6,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Canine,
        .attrWeights = role_attrs(4, 3, 1, 1, 4, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 4,0, 2,0,2,3,0, 2,0,3, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Taiga)
                          | hab(Tundra) | hab(Snow) | hab(Mountain)
                          | kHabForest,
        .wildFaction = "wildlife",
    },


    // ВЕРХУШКА ТУНДРЫ — лютый волк, 80 (прежний потолок тундры 42).
    {
        .row = NPCType::DireWolf,
        .id = "dire_wolf",
        .label = "Dire Wolf",
        .sprite = SpriteId::DireWolf,
        .baseLevel = 80,
        .ai = AIBehaviour::Aggressive,
        .combat = {115, {4,7}, 2.3f, 1.8f, steps_from_seconds(1.0f), "DWf", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.2f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Canine,
        .attrWeights = role_attrs(6, 6, 1, 2, 4, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x8A8A92u,
        .habitat = hab(Tundra),
        .wildFaction = "wildlife",
    },


    // Bear
    {
        .row = NPCType::Bear,
        .id = "bear",
        .label = "Bear",
        .sprite = SpriteId::Bear,
        .baseLevel = 50,
        .ai = AIBehaviour::Aggressive,
        .combat = {80, {18,1}, 1.75f, 3, steps_from_seconds(1.5f), "Ber"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bear,
        .attrWeights = role_attrs(5, 5, 1, 1, 2, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Taiga) | kHabForest,
        .wildFaction = "wildlife",
    },


    // ВЕРХУШКА ТАЙГИ — шатун, 82 (прежний потолок тайги 50).
    {
        .row = NPCType::Shatun,
        .id = "shatun",
        .label = "Shatun",
        .sprite = SpriteId::Shatun,
        .baseLevel = 82,
        .ai = AIBehaviour::Aggressive,
        .combat = {150, {3,14}, 1.7f, 2.0f, steps_from_seconds(1.8f), "Sht", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.0f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bear,
        .attrWeights = role_attrs(8, 3, 1, 2, 5, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 3,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x3A2A1Au,
        .habitat = hab(Taiga),
        .wildFaction = "wildlife",
    },


    // Boar
    {
        .row = NPCType::Boar,
        .id = "boar",
        .label = "Boar",
        .sprite = SpriteId::Boar,
        .baseLevel = 16,
        .ai = AIBehaviour::Aggressive,
        .combat = {40, {12,1}, 2.0f, 3, steps_from_seconds(1.2f), "Bor"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 5,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Boar,
        .attrWeights = role_attrs(4, 4, 1, 1, 3, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 4,0,1,2,0, 1,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Steppe)
                          | hab(Tropics) | kHabForest,
        .wildFaction = "wildlife",
    },


    // ВЕРХУШКА ЛУГА — вепрь-исполин, 72. Луг имел потолок 16, а замер показал, что
    // опасность гасят ПОСЕЛЕНИЯ (26.2 на клетке → 138.8 на радиусе 64), а не
    // биом: значит дикий луг вдали от жилья обязан кусать.
    {
        .row = NPCType::GreatBoar,
        .id = "great_boar",
        .label = "Great Boar",
        .sprite = SpriteId::GreatBoar,
        .baseLevel = 72,
        .ai = AIBehaviour::Aggressive,
        .combat = {105, {3,10}, 2.0f, 1.7f, steps_from_seconds(1.5f), "GBr", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.3f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Boar,
        .attrWeights = role_attrs(7, 4, 1, 1, 5, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x4A3A2Au,
        .habitat = hab(Meadow),
        .wildFaction = "wildlife",
    },


    // Snake
    {
        .row = NPCType::Snake,
        .id = "snake",
        .label = "Snake",
        .sprite = SpriteId::Snake,
        .baseLevel = 4,
        .ai = AIBehaviour::Aggressive,
        .combat = {10, {8,1}, 1.5f, 2, steps_from_seconds(0.8f), "Snk"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(3, 2, 1, 1, 3, 3, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 4,0, 1,0,1,2,0, 1,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Desert) | hab(Steppe) | hab(Swamp)
                          | hab(Tropics) | kHabRuin,
        .wildFaction = "wildlife",
    },


    // Crocodile
    {
        .row = NPCType::Croc,
        .id = "crocodile",
        .label = "Crocodile",
        .sprite = SpriteId::Crocodile,
        .baseLevel = 28,
        .ai = AIBehaviour::Aggressive,
        .combat = {50, {15,1}, 1.25f, 3, steps_from_seconds(1.5f), "Crc"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(4, 4, 1, 1, 2, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 4,0,1,1,0, 1,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp) | hab(Tropics),
        .wildFaction = "wildlife",
    },


    // Basilisk — the swamp's and the desert's heavy. Serpent silhouette, so
    // the eye reads it instantly among the uprights.
    {
        .row = NPCType::Basilisk,
        .id = "basilisk",
        .label = "Basilisk",
        .sprite = SpriteId::Basilisk,
        .baseLevel = 65,
        .ai = AIBehaviour::Aggressive,
        .combat = {95, {3,10}, 1.6f, 3.0f, steps_from_seconds(1.5f), "Bsk", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.4f, kNpcSightDefaultM, 100, 100,
         DamageType::Earth},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 0.8f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(4, 4, 1, 2, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,2,0,0, 4,0, 3,0,1,1,0, 0,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0x3A6A4Au,
        .habitat = kHabRuin | hab(Swamp) | hab(Desert),
        .wildFaction = "demons",
    },


    // Морской змей — «хай тир» воды, 110. ТИП у него `Reptile`, а не `Marine`,
    // и это не описка: ярус группирует по РОДУ ВЕЩИ, а где змей живёт, говорит
    // колонка `habitat` — спрашивать место ещё и у яруса значило бы завести
    // вторую дверь к одному вопросу.
    {
        .row = NPCType::SeaSerpent,
        .id = "sea_serpent",
        .label = "Sea Serpent",
        .sprite = SpriteId::SeaSerpent,
        .baseLevel = 110,
        .ai = AIBehaviour::Aggressive,
        .combat = {300, {4,14}, 2.0f, 3.4f, steps_from_seconds(1.8f), "SSr", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.2f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.4f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(7, 6, 2, 2, 4, 2, 1, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,2,0,0, 5,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x2A6A6Au,
        .habitat = hab(Water),
        .wildFaction = "wildlife",
    },


    // ВЕРХУШКА БОЛОТА — гидра, 105 (прежний потолок болота 65).
    {
        .row = NPCType::Hydra,
        .id = "hydra",
        .label = "Hydra",
        .sprite = SpriteId::Hydra,
        .baseLevel = 105,
        .ai = AIBehaviour::Aggressive,
        .combat = {280, {5,11}, 1.6f, 3.0f, steps_from_seconds(1.7f), "Hyd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.4f, kNpcSightDefaultM, 100, 100,
         DamageType::Water},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(7, 5, 2, 2, 6, 2, 1, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 5,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x2A4A2Au,
        .habitat = hab(Swamp),
        .wildFaction = "demons",
    },


    // ВЕРХУШКА ПУСТЫНИ — песчаный вьюн, 95 (прежний потолок пустыни 65).
    {
        .row = NPCType::SandWyrm,
        .id = "sand_wyrm",
        .label = "Sand Wyrm",
        .sprite = SpriteId::SandWyrm,
        .baseLevel = 95,
        .ai = AIBehaviour::Aggressive,
        .combat = {210, {4,12}, 1.8f, 2.8f, steps_from_seconds(1.9f), "SWm", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Earth},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(8, 4, 1, 2, 5, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC2A868u,
        .habitat = hab(Desert),
        .wildFaction = "demons",
    },


    // ВЕРХУШКА ТРОПИКОВ — тиран джунглей, 88. Тропики держали САМЫЙ низкий потолок
    // среди земель — 28 — при самой густой жизни (2..6 голов за визит).
    {
        .row = NPCType::JungleTyrant,
        .id = "jungle_tyrant",
        .label = "Jungle Tyrant",
        .sprite = SpriteId::JungleTyrant,
        .baseLevel = 88,
        .ai = AIBehaviour::Aggressive,
        .combat = {170, {4,10}, 2.1f, 2.2f, steps_from_seconds(1.4f), "JTy", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.2f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Reptile,
        .attrWeights = role_attrs(7, 6, 1, 2, 4, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x1A5A1Au,
        .habitat = hab(Tropics),
        .wildFaction = "wildlife",
    },




    // Hawk
    {
        .row = NPCType::Hawk,
        .id = "hawk",
        .label = "Hawk",
        .sprite = SpriteId::Hawk,
        .baseLevel = 2,
        // ЛЕТУН: cruiseM 5 — ястреб честно в воздухе (полёт-посадка
        // 2026-09-10), гравитации нет, конверт общий с игроком.
        .ai = AIBehaviour::Wanderer,
        .combat = {8, {5,1}, 3.0f, 3, steps_from_seconds(1.0f), "Hwk", CombatTemplate::Melee, 0, 0, 0xFFFFFFFFu, 0.0f, kNpcSightDefaultM, 100, 100, DamageType::Blunt, 5.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.4f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bird,
        .attrWeights = role_attrs(2, 3, 2, 1, 5, 3, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,5,0, 2,0,3, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Meadow) | hab(Valley) | hab(Desert)
                          | hab(Steppe),
        .wildFaction = "wildlife",
    },


    // Eagle
    {
        .row = NPCType::Eagle,
        .id = "eagle",
        .label = "Eagle",
        .sprite = SpriteId::Eagle,
        .baseLevel = 3,
        // ЛЕТУН: cruiseM 6 — орёл выше ястреба, тот же закон.
        .ai = AIBehaviour::Wanderer,
        .combat = {12, {7,1}, 3.25f, 3, steps_from_seconds(1.0f), "Egl", CombatTemplate::Melee, 0, 0, 0xFFFFFFFFu, 0.0f, kNpcSightDefaultM, 100, 100, DamageType::Blunt, 6.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bird,
        .attrWeights = role_attrs(2, 3, 2, 1, 5, 3, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 3,0, 1,0,2,5,0, 2,0,3, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain),
        .wildFaction = "wildlife",
    },


    // ВЕРХУШКА ГОРЫ — рух, 100. Гора была САМОЙ опасной полосой мира и всё равно
    // упиралась в 70, поэтому её верхушка — самая высокая из земель.
    {
        .row = NPCType::Roc,
        .id = "roc",
        .label = "Roc",
        .sprite = SpriteId::Roc,
        .baseLevel = 100,
        .ai = AIBehaviour::Aggressive,
        .combat = {230, {4,13}, 2.5f, 2.6f, steps_from_seconds(1.6f), "Roc", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/3.0f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, 9.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.2f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bird,
        .attrWeights = role_attrs(8, 6, 2, 2, 5, 2, 1, 4),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x6A6A72u,
        .habitat = hab(Mountain),
        .wildFaction = "wildlife",
    },


    // Frog
    {
        .row = NPCType::Frog,
        .id = "frog",
        .label = "Frog",
        .sprite = SpriteId::Frog,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {3, {0,1}, 1.5f, 0, steps_from_seconds(9.0f), "Frg"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 10,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Amphibian,
        .attrWeights = role_attrs(1, 3, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,2,3,0, 1,3,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp),
        .wildFaction = "wildlife",
    },


    // ══ НАСЕЛЕНИЕ ВОДЫ — ПЯТЬ СТРОК НА 21 % МИРА (M-382) ═════════════════
    // Вердикт владельца 2026-11-04, дословно: «в воде пусть живут акулы какулы
    // и морские змеи (хай ттиР) / и средний низкий кальмары всякие киты
    // (высокий мирный и тд)»; и о воплощении — «будет плавание но пока воплощай
    // как есть».
    //
    // ПОЧЕМУ ЭТО КРУПНЕЙШАЯ ДЫРА ОХВАТА, А НЕ УКРАШЕНИЕ: биом `Water` занимает
    // 222 158 клеток (21 % мира) и по средней опасности ВТОРОЙ после горы
    // (136.2 из 255, доля клеток ≥96 — 83 %), а строк с `hab(Water)` в каталоге
    // было РОВНО НОЛЬ — то есть пятая часть мира роллила пустоту.
    //
    // ТИРЫ ИДУТ ЛЕСТНИЦЕЙ, А НЕ ПАРОЙ КРАЙНОСТЕЙ: кальмар 25 → какула 55 →
    // акула 85 → кит 95 → морской змей 110. `danger_match` делит шансы строки
    // вдвое на каждые 25 байт расхождения с опасностью клетки, поэтому
    // авторить тир ЕСТЬ авторить полосу: змей садится только на самую злую
    // воду, кальмар — на тихую отмель, и ни один список этого не запрещает.
    //
    // СТАРШИХ ФОРМ НЕ ЗАВОДИТСЯ (вердикт владельца): каждая строка — СВОЁ
    // существо, а не подкрученная соседняя. Какула — не «большая акула», а
    // стайный охотник, и отличает её не множитель, а КОЛОНКИ: своя кость, своя
    // частота, свой оттенок.
    //
    // ПЛАВАНИЯ ЕЩЁ НЕТ, И ЭТО НАЗВАННАЯ ВРЕМЕННАЯ ЛОЖЬ МИРА (ряд M-404): тело
    // встаёт на тайл воды и идёт бродом, потому что флаг плавания делается по
    // аналогии с полётом и это отдельный наряд. Костыля под это здесь не
    // ставится — колонка `habitat` говорит правду уже сегодня.

    // Кальмар — тихая отмель. Мелочь, которой в воде положено быть МНОГО:
    // `weight` 8 против единицы у змея, то есть он и есть «текстура» воды.
    {
        .row = NPCType::Squid,
        .id = "squid",
        .label = "Squid",
        .sprite = SpriteId::Squid,
        .baseLevel = 25,
        .ai = AIBehaviour::Aggressive,
        .combat = {28, {2,6}, 1.1f, 1.6f, steps_from_seconds(2.0f), "Sqd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 8,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Marine,
        .attrWeights = role_attrs(2, 5, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 3,0,2,2,0, 1,2,1, 0, 0, 0,0,0,0),
        .mapColor = 0xA06A8Au,
        .habitat = hab(Water),
        .wildFaction = "wildlife",
    },


    // Какула — СВОЁ существо, а не крупная акула (вердикт владельца
    // 2026-11-04: «отдельная тварь, авторь сам»). Стайный охотник: берёт не
    // костью, а числом и напором, поэтому у неё частая атака, узкий разброс и
    // вес выше акульего. Стоит ступенью между кальмаром и акулой — без неё
    // лестница воды прыгала бы с 25 сразу на 85.
    {
        .row = NPCType::Kakula,
        .id = "kakula",
        .label = "Kakula",
        .sprite = SpriteId::Kakula,
        .baseLevel = 55,
        .ai = AIBehaviour::Aggressive,
        .combat = {70, {4,5}, 1.9f, 2.0f, steps_from_seconds(0.9f), "Kkl", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.1f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Marine,
        .attrWeights = role_attrs(4, 6, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 3,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x7A5A6Au,
        .habitat = hab(Water),
        .wildFaction = "wildlife",
    },


    // Акула — одиночный хищник открытой воды. Против какулы она ровно
    // обратная: редкая, бьёт реже и БОЛЬНЕЕ (3d12 против 4d5).
    {
        .row = NPCType::Shark,
        .id = "shark",
        .label = "Shark",
        .sprite = SpriteId::Shark,
        .baseLevel = 85,
        .ai = AIBehaviour::Aggressive,
        .combat = {120, {3,12}, 2.1f, 2.2f, steps_from_seconds(1.6f), "Shk", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.3f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Marine,
        .attrWeights = role_attrs(6, 6, 1, 1, 3, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 1,1,1, 0, 0, 0,0,0,0),
        .mapColor = 0x5A6A78u,
        .habitat = hab(Water),
        .wildFaction = "wildlife",
    },


    // Кит — «высокий мирный» из вердикта владельца, и мирность у него та же
    // КОЛОНКА, что у оленя: `AIBehaviour::Flee`. Высокий тир при бегстве — не
    // противоречие, а смысл: он огромен и дорог, но не ищет драки; тронувший
    // его получает 5d10.
    {
        .row = NPCType::Whale,
        .id = "whale",
        .label = "Whale",
        .sprite = SpriteId::Whale,
        .baseLevel = 95,
        .ai = AIBehaviour::Flee,
        .combat = {260, {5,10}, 1.4f, 3.0f, steps_from_seconds(2.6f), "Whl", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/4.0f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 2.0f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Marine,
        .attrWeights = role_attrs(8, 2, 1, 2, 6, 3, 1, 3),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 2,0,1,1,0, 3,4,2, 0, 0, 0,0,0,0),
        .mapColor = 0x3A4A5Au,
        .habitat = hab(Water),
        .wildFaction = "wildlife",
    },


    // Cave bat — the first thing a torch finds. Cruises at 2.5 m: high
    // enough to be a nuisance, low enough that a cave's ceiling still holds
    // it (the envelope is shared, so a row that cruised into the rock would
    // simply be pinned against it — a number, not a bug).
    {
        .row = NPCType::CaveBat,
        .id = "cave_bat",
        .label = "Cave Bat",
        .sprite = SpriteId::CaveBat,
        .baseLevel = 2,
        .ai = AIBehaviour::Aggressive,
        .combat = {8, {1,6}, 3.0f, 2.0f, steps_from_seconds(0.9f), "Bat", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.4f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce, /*cruiseM*/2.5f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 6,
        .radius = 0.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Bat,
        .attrWeights = role_attrs(1, 2, 1, 1, 6, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,0,5,0, 0,3,3, 0, 0, 0,0,0,0),
        .mapColor = 0x4A4048u,
        .habitat = kHabRuin | hab(Mountain),
        .wildFaction = "wildlife",
    },


    // Cave spider — slow to notice, fast to close. Pierce fangs.
    {
        .row = NPCType::CaveSpider,
        .id = "cave_spider",
        .label = "Cave Spider",
        .sprite = SpriteId::CaveSpider,
        .baseLevel = 6,
        .ai = AIBehaviour::Aggressive,
        .combat = {26, {2,6}, 2.4f, 2.0f, steps_from_seconds(1.0f), "Spd", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.7f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Arachnid,
        .attrWeights = role_attrs(3, 3, 1, 1, 4, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1, 0, 0, 0,0,0,0),
        .mapColor = 0x2A2A3Au,
        .habitat = kHabRuin | kHabForest | hab(Mountain),
        .wildFaction = "wildlife",
    },


    // Sand Scorpion
    {
        .row = NPCType::SandScorpion,
        .id = "sand_scorpion",
        .label = "Sand Scorpion",
        .sprite = SpriteId::SandScorpion,
        .baseLevel = 24,
        .ai = AIBehaviour::Aggressive,
        .combat = {35, {12,1}, 1.75f, 3, steps_from_seconds(1.0f), "Ssc"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 5,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.0f,
        .type = ObjectType::Arachnid,
        .attrWeights = role_attrs(3, 3, 1, 1, 3, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 2,0,1, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Desert),
        .wildFaction = "demons",
    },

    // ══ АРХЕТИП Livestock — «СКОТ И ВАЩЕ ЛЮБЫЕ ДОМАШНИЕ ЖИВОТНЫЕ» ═══════
    // (владелец, 2026-10-20). СПИНА ЭТОМУ АРХЕТИПУ НЕ ПРИНАДЛЕЖИТ — его же
    // поправка той же фразой: «но спина это не их механика спина есть у всего
    // по таблице объектов». Поэтому `haulMult` остаётся колонкой ВСЯКОЙ строки
    // (у коня 8, у волка 0), и будущий модуль скота её не получит.


    // Horse — the creatures's beast of burden (CANON S10 «ЛОШАДЬ — ЮНИТ»).
    // A sturdy flighty grazer: hooves 1d4, faster than any march. It EATS
    // (upkeep 0 = a mouth on the board law, no wage — the column humans
    // use, the beast default kNpcUpkeepNone is exactly what this row must
    // NOT say), and it is hireable: the town's herd sells through the one
    // hire door, no horse-shop path.
    {
        .row = NPCType::Horse,
        .id = "horse",
        .label = "Horse",
        .sprite = SpriteId::Deer,
        .baseLevel = 2,
        .ai = AIBehaviour::Flee,
        .combat = {40, {1,4}, 2.2f, 1.2f, steps_from_seconds(1.6f), "Hrs"},
        .upkeepGoldPerDay = 0,
        .hireable = true,
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
        .type = ObjectType::Horse,
        .attrWeights = role_attrs(4, 5, 1, 1, 4, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0, 0, 0, 0,0,0,0),
        .mapColor = 0x8A6A42u,
        .habitat = hab(Steppe) | hab(Meadow) | hab(Valley),
        .wildFaction = "wildlife",
    },


    // Sheep — ВТОРОЙ РОД ГОЛОВ НА ОДНОЙ ТРАВЕ (вердикт владельца 2026-10-12:
    // «лошадки и овехи будут просто новыми строками в таблицей мобов»).
    // Авторено ОТНОСИТЕЛЬНО соседей по столу, как требует шапка стола ниже
    // («sixteen species authored against each other, not in isolation»):
    // коза {20 hp, 2.0 марша, досягаемость 2} — верхолаз с рогами, кролик
    // {5 hp, 0 урона} — дно стола. Овца стоит между ними и ближе к козе: тот
    // же корпус, ни рогов, ни скал.
    //   · hp 12 — выше кролика (5), ниже козы (20): та же масса без её боя;
    //   · 1d3 тычок — слабейшее НЕнулевое оружие стола (у кролика 0d1 ноль,
    //     у козы фиксированные 5): овца толкает, а не бодает;
    //   · 1.3 марша — самый медленный травоядный стола: стадо дрейфует и
    //     человека (1.0) почти не обгоняет, против 2.0 у козы;
    //   · досягаемость 1.0 — корпус, а не рога (у козы 2.0);
    //   · перезарядка 3.0 — между козьими 1.5 и кроличьими 9.0: овца
    //     огрызается, но не дерётся;
    //   · weight 5 — середина названного полосой «fodder 4-6» диапазона:
    //     стадо на лугу обычно, но реже кролика (15);
    //   · upkeep 0, А НЕ `kNpcUpkeepNone` — РОВНО по строке лошади: овца
    //     СКОТИНА, то есть рот на законе рациона без жалованья (владелец:
    //     «все кто в нём едят по таблице по системе»);
    //   · `hireable = false` и цены НЕТ: назвать её нечем, пока со стада
    //     ничего не снимают — ни шерсти, ни мяса (товара `wool` в словаре
    //     нет, и выдумывать его запрещено, §1). Появится съём — появится и
    //     вывод цены, одной колонкой;
    //   · haulMult 0.5 — ПОЛСПИНЫ ЧЕЛОВЕКА, панньеры ~8 кг против его 15
    //     (вердикт владельца 2026-10-12: «овечке кстати можно дать немножко»,
    //     «баряся станет полезной»). Та же цепочка килограммов, что у коня,
    //     и ровно «немножко»: везти овцами выгоднее не станет — они ещё и
    //     едят по рациону;
    //   · спрайт козы — тот же четвероногий травоядный лист, ровно тем же
    //     прецедентом, каким лошадь берёт лист оленя;
    //   · ареал — та же трава, которую оценивает ряд стада.
    {
        .row = NPCType::Sheep,
        .id = "sheep",
        .label = "Sheep",
        .sprite = SpriteId::Goat,
        .baseLevel = 1,
        .ai = AIBehaviour::Flee,
        .combat = {12, {1,3}, 1.3f, 1.0f, steps_from_seconds(3.0f), "Shp"},
        .upkeepGoldPerDay = 0,
        .hireable = false,
        .weight = 5,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.5f,
        .type = ObjectType::Smallstock,
        .attrWeights = role_attrs(1, 4, 1, 1, 2, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 0,0, 2,0,3,1,0, 3,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xE8E4D8u,   // шерсть: на тон светлее козьего камня
        .habitat = hab(Steppe) | hab(Meadow) | hab(Valley),
        .wildFaction = "wildlife",
    },


    // Mountain Goat
    {
        .row = NPCType::Goat,
        .id = "goat",
        .label = "Mountain Goat",
        .sprite = SpriteId::Goat,
        .baseLevel = 2,
        .ai = AIBehaviour::Flee,
        .combat = {20, {5,1}, 2.0f, 2, steps_from_seconds(1.5f), "Mgt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 8,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .haulMult = 0.7f,
        .type = ObjectType::Smallstock,
        .attrWeights = role_attrs(2, 4, 1, 1, 4, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain),
        .wildFaction = "wildlife",
    },

    // ══ АРХЕТИП Void — «ДЛЯ ВСЯКОЙ ХТОНИ И ДЕМОНОВ» ═════════════════════
    // (владелец, 2026-10-20). Отдельный архетип, а не тип внутри зверья —
    // вердикт того же дня: «про нежить да будет у них особый модуль но это не
    // „модуль воедения“ не путайте аи модули сквадов и модули объктов (их
    // особенноси и механики)». ВОПЛОЩАЕМОСТЬ — НЕ «ЖИВОЙ»: нежить встаёт телом
    // и считается головой ровно как крестьянин.


    // Skeleton
    {
        .row = NPCType::Skeleton,
        .id = "skeleton",
        .label = "Skeleton",
        .sprite = SpriteId::Skeleton,
        .baseLevel = 11,
        .ai = AIBehaviour::Aggressive,
        .combat = {35, {10,1}, 1.5f, 3, steps_from_seconds(1.2f), "Skl"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Skeleton,
        .attrWeights = role_attrs(3, 4, 1, 1, 2, 1, 1, 1),
        .skillWeights = role_skills(2,0,1,0,0,0,0, 1,0,1,1, 0,0,0,0,0,0, 3,0, 3,0,1,1,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },


    // Zombie — the slowest thing that will still kill you. Its threat is the
    // 2.0 s cooldown meeting 55 hp: you cannot out-trade it, you walk away.
    {
        .row = NPCType::Zombie,
        .id = "zombie",
        .label = "Zombie",
        .sprite = SpriteId::Zombie,
        .baseLevel = 10,
        .ai = AIBehaviour::Aggressive,
        .combat = {55, {2,8}, 0.8f, 3.0f, steps_from_seconds(2.0f), "Zmb", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.75f, kNpcSightDefaultM, 100, 100,
         DamageType::Blunt},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 4,
        .radius = 0.55f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Zombie,
        .attrWeights = role_attrs(4, 5, 1, 1, 1, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 3,0, 5,0,1,0,1, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x6A7A5Au,
        .habitat = kHabRuin | kHabSpire | hab(Swamp),
        .wildFaction = "demons",
    },


    // Ghoul — the zombie's opposite reading of the same corpse: fast,
    // lighter, three smaller dice instead of two big ones.
    {
        .row = NPCType::Ghoul,
        .id = "ghoul",
        .label = "Ghoul",
        .sprite = SpriteId::Ghoul,
        .baseLevel = 24,
        .ai = AIBehaviour::Aggressive,
        .combat = {48, {3,6}, 2.4f, 3.0f, steps_from_seconds(1.2f), "Ghl", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.7f, kNpcSightDefaultM, 100, 100,
         DamageType::Pierce},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.5f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Zombie,
        .attrWeights = role_attrs(3, 3, 1, 1, 4, 2, 1, 1),
        .skillWeights = role_skills(0,0,0,0,1,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1, 0, 0, 0,0,0,0),
        .mapColor = 0x9A8A7Au,
        .habitat = kHabRuin | kHabSpire,
        .wildFaction = "demons",
    },


    // Wraith — the ice wraith's rootless cousin: Void instead of cold, so a
    // player kitted against one is not kitted against the other.
    {
        .row = NPCType::Wraith,
        .id = "wraith",
        .label = "Wraith",
        .sprite = SpriteId::Wraith,
        .baseLevel = 40,
        .ai = AIBehaviour::Aggressive,
        .combat = {55, {3,8}, 2.0f, 4.0f, steps_from_seconds(1.4f), "Wrh", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Void},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.6f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Wraith,
        .attrWeights = role_attrs(3, 3, 3, 4, 4, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,1,3, 2,3, 1,3,0,3,0, 0,1,0, 0, 0, 0,0,0,0),
        .mapColor = 0x7060A0u,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },


    // Ice Wraith
    {
        .row = NPCType::IceWraith,
        .id = "ice_wraith",
        .label = "Ice Wraith",
        .sprite = SpriteId::IceWraith,
        .baseLevel = 42,
        .ai = AIBehaviour::Aggressive,
        .combat = {45, {16,1}, 1.75f, 5, steps_from_seconds(1.3f), "Iwr"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.7f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Wraith,
        .attrWeights = role_attrs(3, 3, 3, 4, 3, 2, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,3,1,0,0,2, 1,3, 1,3,1,3,0, 1,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Tundra) | hab(Snow) | kHabSpire,
        .wildFaction = "demons",
    },


    // Stone Golem
    {
        .row = NPCType::StoneGolem,
        .id = "stone_golem",
        .label = "Stone Golem",
        .sprite = SpriteId::StoneGolem,
        .baseLevel = 68,
        .ai = AIBehaviour::Aggressive,
        .combat = {150, {20,1}, 0.75f, 4, steps_from_seconds(2.5f), "Glm"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 1.3f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Golem,
        .attrWeights = role_attrs(5, 5, 1, 1, 1, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 3,0, 5,0,1,1,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Mountain) | kHabSpire,
        .wildFaction = "demons",
    },


    // Gargoyle — a tower's airborne masonry. Earth damage against the armour
    // table, Hulk silhouette, 90 hp: the first new row that must be planned
    // for rather than met.
    {
        .row = NPCType::Gargoyle,
        .id = "gargoyle",
        .label = "Gargoyle",
        .sprite = SpriteId::Gargoyle,
        .baseLevel = 45,
        .ai = AIBehaviour::Aggressive,
        .combat = {90, {3,8}, 1.4f, 3.0f, steps_from_seconds(1.6f), "Grg", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/2.2f, kNpcSightDefaultM, 100, 100,
         DamageType::Earth, /*cruiseM*/7.0f},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 2,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Golem,
        .attrWeights = role_attrs(5, 5, 1, 2, 2, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,2,0,0, 3,0, 4,0,1,2,1, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0x60605Au,
        .habitat = kHabSpire | hab(Mountain),
        .wildFaction = "demons",
    },


    // Imp — a spire's smallest servant: quick, burning, and never alone.
    {
        .row = NPCType::Imp,
        .id = "imp",
        .label = "Imp",
        .sprite = SpriteId::Imp,
        .baseLevel = 5,
        .ai = AIBehaviour::Aggressive,
        .combat = {18, {2,5}, 2.8f, 2.0f, steps_from_seconds(0.9f), "Imp", CombatTemplate::Melee, 0, 0,
         0xFFFFFFFFu, /*bodyHeight*/0.8f, kNpcSightDefaultM, 100, 100,
         DamageType::Fire},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 5,
        .radius = 0.35f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Demon,
        .attrWeights = role_attrs(2, 2, 3, 2, 5, 3, 1, 2),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,1,0, 3,0,0,0,0,0, 1,2, 1,0,0,4,0, 0,3,1, 0, 0, 0,0,0,0),
        .mapColor = 0xA03050u,
        .habitat = kHabSpire | kHabRuin,
        .wildFaction = "demons",
    },


    // Swamp Thing
    {
        .row = NPCType::SwampThing,
        .id = "swamp_thing",
        .label = "Swamp Thing",
        .sprite = SpriteId::SwampThing,
        .baseLevel = 38,
        .ai = AIBehaviour::Aggressive,
        .combat = {60, {14,1}, 1.0f, 4, steps_from_seconds(1.5f), "Swt"},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 3,
        .radius = 0.9f,
        .names = {{}},
        .nameCount = 0,
        .talkLines = {{}},
        .talkCount = 0,
        .type = ObjectType::Elemental,
        .attrWeights = role_attrs(4, 4, 1, 2, 1, 1, 1, 1),
        .skillWeights = role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,2,0,1,0,0, 3,1, 4,1,1,1,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC8C8C8u,
        .habitat = hab(Swamp),
        .wildFaction = "demons",
    },


    // Lich — what is at the top of the climb. The only new row that both
    // shoots and blasts (3 m), the only one that hoards a purse, and the
    // strongest thing in the table below the dragon.
    {
        .row = NPCType::Lich,
        .id = "lich",
        .label = "Lich",
        .sprite = SpriteId::Lich,
        .baseLevel = 100,
        .ai = AIBehaviour::Aggressive,
        .combat = {110, {4,12}, 1.1f, 28.0f, steps_from_seconds(2.6f), "Lch", CombatTemplate::Missile, 210,
         3.0f, 0xFF90FFB0u, /*bodyHeight*/1.9f, kNpcSightDefaultM, 100, 100,
         DamageType::Void, /*cruiseM*/0.0f, /*actions*/{action_slot("void_arrow"), kActionNone}},
        .upkeepGoldPerDay = kNpcUpkeepNone,
        .hireable = false,
        .weight = 1,
        .radius = 0.6f,
        .names = {{"Vashkar","The Grey Crown","Ozimandel","Neth-Ur"}},
        .nameCount = 4,
        .talkLines = {{"I was old when your kingdom was a camp.",
          "Breathe. It is a habit you will lose.",
          "Come closer. I want to see it happen."}},
        .talkCount = 3,
        .type = ObjectType::Lich,
        .attrWeights = role_attrs(2, 3, 6, 6, 1, 2, 2, 5),
        .skillWeights = role_skills(0,0,0,0,0,0,2, 0,0,0,0, 1,0,0,0,3,4, 0,5, 1,4,0,0,0, 0,0,0, 0, 0, 0,0,0,0),
        .mapColor = 0xC0D0B0u,
        .habitat = kHabSpire,
        .wildFaction = "demons",
    },
};
static_assert(rows_in_enum_order(kNpcTypeDefs, &NpcTypeDef::row),
              "kNpcTypeDefs row order must mirror NPCType");

// ── ЛЕСТНИЦА ПОД КОМПИЛЯТОРОМ: ОТРЕЗОК ЕСТЬ ОТРЕЗОК (наряд M-295) ─────────
// Половина закона, которую можно проверить только ЗДЕСЬ: `kNpcTypeDefs` видна
// в этом заголовке, а предметный каталог — нет (он живёт в `items.cpp` и
// наружу торчит спаном), поэтому непрерывность существных отрезков прибита
// тут, а предметных — у своей таблицы. Два места, ОДИН закон, и ни одно не
// знает числа другого: каждое судит свою авторскую таблицу.
constexpr bool npc_rows_grouped_by_type() {
    for (std::size_t i = 1; i < std::size(kNpcTypeDefs); ++i) {
        if (std::uint8_t(kNpcTypeDefs[i].type)
            < std::uint8_t(kNpcTypeDefs[i - 1].type)) {
            return false;
        }
    }
    return true;
}
static_assert(npc_rows_grouped_by_type(),
              "рода идут ПО ТИПАМ: иначе «все строки типа» и «все строки "
              "архетипа» перестают быть диапазоном и становятся фильтром");

// ВСЯКИЙ РОД НАЗЫВАЕТ СУЩЕСТВУЮЩИЙ ТИП. Колонка без умолчания ловит
// забывчивость именованного авторинга, но не ловит ординал, выдуманный
// приведением; этот ряд ловит и его.
constexpr bool npc_types_exist() {
    for (const NpcTypeDef& d : kNpcTypeDefs) {
        if (std::size_t(d.type) >= std::size_t(ObjectType::Count)) return false;
    }
    return true;
}
static_assert(npc_types_exist(), "род назвал тип вне оси");

// СУЩЕСТВНАЯ ПОЛОВИНА НЕСЁТ ТОЛЬКО ВОПЛОЩАЕМЫЕ АРХЕТИПЫ — то есть ЛЕСТНИЦА И
// АДРЕС СОГЛАСНЫ НА ЭТОЙ ПОЛОВИНЕ. Пространство ординалов строки мира устроено
// так, что выше размера предметного каталога лежат существа, а воплощаемость
// спрашивается у АРХЕТИПА; назови строка существа архетип вещи — два ответа на
// один вопрос мира разошлись бы молча. Вторая половина тождества стоит в
// `items.cpp`, третья — свидетелем на всех 91 строке мира.
constexpr bool npc_rows_are_all_embodied() {
    for (const NpcTypeDef& d : kNpcTypeDefs) {
        if (!object_embodied(object_type_archetype(d.type))) return false;
    }
    return true;
}
static_assert(npc_rows_are_all_embodied(),
              "строка существа назвала архетип ВЕЩИ: адрес строки мира и "
              "лестница разошлись бы молча");

// НАРОД — СПЛОШНОЙ ОТРЕЗОК В НАЧАЛЕ СУЩЕСТВНОЙ ПОЛОВИНЫ. Следствие порядка
// архетипов, и оно названо отдельно, потому что у него самый горячий читатель:
// `is_folk_kind` ниже спрашивается на каждую голову контейнера.
constexpr bool folk_is_a_prefix() {
    bool left = false;
    for (const NpcTypeDef& d : kNpcTypeDefs) {
        const bool folk =
            object_type_archetype(d.type) == ObjectArchetype::Folk;
        if (!folk) left = true;
        else if (left) return false;   // народ после не-народа — дыра в отрезке
    }
    return true;
}
static_assert(folk_is_a_prefix(),
              "рода народа — сплошной отрезок в начале каталога существ");

// ТИР КАЖДОЙ СТРОКИ СТОИТ НА ЛЕСТНИЦЕ, И ЭТО СУДИТ КОМПИЛЯТОР (M-382).
// Колонка `baseLevel` авторская, а `clamp_to_ladder` — СТРАЖ области
// определения: он подрезает описку МОЛЧА, и тем страшен. Автор, переавторивший
// тиры (M-385), узнаёт о выезде за лестницу от сборки, а не от ровной цены
// двух разных тварей: 300 и 255 дали бы одну и ту же цену 67 575.
// Пол здесь 1, а не 0, по той же причине, что у двери: нулевой ступени нет.
constexpr bool npc_tiers_on_ladder() {
    for (const NpcTypeDef& d : kNpcTypeDefs) {
        if (d.baseLevel < 1 || d.baseLevel > kMaxTier) return false;
    }
    return true;
}
static_assert(npc_tiers_on_ladder(),
              "тир строки существа вне лестницы 1..255: страж подрезал бы его "
              "молча, и две разных твари стали бы равны в цене");

inline constexpr const NpcTypeDef& npc_def(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)];
}

// ── ДВЕРИ К КОЛОНКАМ СТРОКИ ──────────────────────────────────────────────
// Здесь стояли ТРИ таблицы-спутника (kNpcNature / kNpcPurse / kNpcMapColor)
// со своими свидетелями порядка. Влиты колонками в строку каталога
// 2026-09-22 (CANON S26 «Одна строка на род»). Двери остались: читателей у
// колонки много, и спрашивать они обязаны одним голосом.

// ВВЕРХ ПО ЛЕСТНИЦЕ: строка → тип → архетип. Здесь стояла `npc_nature`, и её
// читатели спрашивали «какая природа»; вопрос тот же, ось другая и шире.
inline constexpr ObjectType npc_type_of(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)].type;
}
inline constexpr ObjectArchetype npc_archetype(NPCType t) {
    return object_type_archetype(npc_type_of(t));
}

// (`npc_named` СНЕСЕНА 2026-11-08 вместе со своей колонкой, M-436. Её шапка
//  уверяла, что «ни один звонящий не тронут: менялось ХРАНИЛИЩЕ ответа, а не
//  вопрос», — и это было верно в день переезда M-263 и ложно с тех пор:
//  звонящих у неё не было НИ ОДНОГО. Вопроса тоже больше нет — родов анкеты не
//  два, вердикт владельца 2026-11-08.)

// ВЕСА РОЛИ — ДВЕ ДВЕРИ К КОЛОНКАМ, И ОБЕ С ГРАНИЦЕЙ ОРДИНАЛА. Граница
// приехала вместе с весами: снесённая `role_weights()` отвечала строкой 0 на
// ординал вне диапазона, и терять этот fail-safe при переезде было нельзя —
// `npc_def` его не имеет, потому что у неё другой контракт.
inline constexpr const AttrWeights& npc_attr_weights(NPCType t) {
    return kNpcTypeDefs[std::size_t(t) < std::size_t(NPCType::Count)
                            ? std::size_t(t) : 0].attrWeights;
}
inline constexpr const SkillWeights& npc_skill_weights(NPCType t) {
    return kNpcTypeDefs[std::size_t(t) < std::size_t(NPCType::Count)
                            ? std::size_t(t) : 0].skillWeights;
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

// Цвет метки на карте — колонка строки.
inline constexpr std::uint32_t npc_map_color(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)].mapColor;
}

// THE id space, and it has one half now. Any "kind" that travels — a creatures
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
// resolver, two spellings: the raw kind is the identity, the record is the
// transfer coin. (Слот единого контейнера несёт СТРОКУ МИРА, не сырой род —
// его читают дверями bus/world_row.h, третьего спеллинга здесь нет.)
inline NPCType soldier_npc_type(std::uint16_t kind) {
    return kind < std::uint16_t(NPCType::Count) ? NPCType(kind)
                                                : NPCType::Peasant;
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

// ── ПРИРОДА, СПРОШЕННАЯ У ЗАПИСИ КОНТЕЙНЕРА ──────────────────────────────────
// Три двери, один столбец данных. Имя рода ни в одной из них не звучит:
// верблюд, мул и овца-вьюк становятся ездовыми, назвав свою спину, а народ
// нового вида — назвав свою природу.
inline bool is_folk_kind(std::uint16_t kind) {
    return valid_npc_kind(kind)
        && npc_archetype(NPCType(kind)) == ObjectArchetype::Folk;
}

// (`is_fauna_kind` СНЕСЕНА НАРЯДОМ M-295, и не потому, что её вопрос умер, а
//  потому что у неё НЕ БЫЛО НИ ОДНОГО продакшен-читателя — только два
//  утверждения в `fauna_registry_test`. Это колонка-сирота в виде двери
//  (DOD п.9). Её вопрос «живность ли» к тому же перестал иметь один ответ:
//  прежняя `Fauna` была свалкой «все нелюди кроме нежити», и после разбора
//  свалки честный вопрос звучит «какой архетип», а на него отвечает
//  `npc_archetype` — одним сравнением и без третьей двери.)

// (count_human_souls / calculate_squad_upkeep переехали в bus/world_row.h
// слиянием M-71: они — взгляды на область существ единого контейнера, а
// пересчёт строка ↔ род живёт только там. Колонки, которые они читают,
// остались здесь. `is_mount_kind` стоял рядом и умер вместе с законом
// упряжки 2026-10-06, M-230: вопрос «ездовая ли строка» был нужен только
// ему, и спина лошади с тех пор выражается одной колонкой haulMult.)

// The row's price column × THE one level law (soldier_level_factor) — the
// same product the old inline `upkeep × 30` computed, read from data
// (CANON S25). An unpriced row (hireGold 0) costs nothing and npc_hireable
// already refuses it.
inline int hire_price_for(std::uint16_t kind, int level) {
    return npc_def(soldier_npc_type(kind)).hireGold
           * soldier_level_factor(level);
}


// ── ТИР СТРОКИ — АВТОРСКАЯ КОЛОНКА, ОДНА ДВЕРЬ ЧТЕНИЯ ──────────────────────
//
// Носитель — `baseLevel` этой же строки (её разбор и полосы каталога стоят
// ТАМ, у колонки): число, которое автор пишет РУКАМИ и которым говорит
// «насколько эта тварь высоко в мире». Оно есть цена в опыте, оно же едет
// уровнем головы в контейнер, и оно лежит на ОДНОЙ лестнице 1..255 с
// опасностью клетки и тиром предмета — пола 0 у лестницы нет, первая ступень
// платит (вердикт владельца о поле: «чтобы чтото было»).
//
// ПОСЛЕ ПЕРЕАВТОРИНГА M-385 КАТАЛОГ ЗАНИМАЕТ 1..120 ИЗ 255 (было 1..10): пол —
// народ и мелкая живность, якорь «эндгейм ≈ 100» — лич, вершина — дракон 120,
// и 135 ступеней оставлены свободными под то, чего в мире ещё нет.
//
// И ОНО ЖЕ ДАЁТ ВЕСА РАССЕЛЕНИЯ ПО ЗОНАМ — С 2026-11-04, НАРЯДОМ M-387.
// Здесь стояло, что это неверно, и тогда так и было: расселение судило
// ВЫВЕДЕННОЙ МОЩЬЮ (`spawn_strength`, нормированный по крайним строкам
// каталога `log₂(hp×dps)`), а тир о нём не знал ВОВСЕ — два ответа на один
// вопрос «насколько эта тварь высоко» (DOD п.6). Выведенная мощь УНИЧТОЖЕНА,
// и теперь `roll_spawns@src/tables/fauna.cpp` спрашивает ЭТУ колонку через
// `creature_tier` ниже. Практическое следствие для автора тиров ровно
// обратное прежнему: правка этой колонки двигает И цену убитого, И землю, на
// которой тварь водится, — разойтись им больше нечем.
//
// ЗДЕСЬ СТОЯЛ ВЫВОД `тир = √(hp × удар/период)`, И ОН СНЕСЁН В ТОТ ЖЕ ДЕНЬ,
// 2026-11-03, вердиктом владельца: ✓ «мы же решили отказаться ваще от мощи и
// умножения хп на дпс … у нас же просто таблица объектов и написан тир моба»;
// ✓ «мне кажется лучше тир число ВСЁ РАВНО ОН НУЖЕН ДЛЯ ВЕСОВ зон».
//
// ПОЧЕМУ ВЫВОД БЫЛ НЕВЕРЕН — ОН НЕ ВИДИТ ПОЛОВИНЫ СТРОКИ, и владелец
// предупредил об этом ПРЯМО, ещё обсуждая свидетеля монотонности: ✓ «могут быть
// твари мало хп куча резистов и огромнвй урон не умничай где нюансы есть».
// `hp × удар / период` не знает ни резистов (`Defense::armor` по восьми родам
// урона), ни блока, ни дальнобойности, ни правил — значит стеклянная пушка
// получала бы низкий тир, а жирный безобидный мешок высокий.
//
// УРОВНЯ ЭКЗЕМПЛЯРА В ЦЕНЕ НЕТ ВОВСЕ, и это ЗАКОН ДВУХ ТАБЛИЦ: у объекта анкеты
// нет, «уровень объекта есть ТИР, и тир выражается СВОЕЙ СТРОКОЙ», а гоблин и
// гоблин-вождь суть две СТРОКИ (CANON S12).
// Посадка на лестницу — ОДНОЙ дверью (`clamp_to_ladder@src/tables/progression.h`,
// M-382): здесь стояло рукописное тройное условие, четвёртая копия того же
// вопроса. Авторская колонка вне лестницы есть ОПИСКА АВТОРА, и страж её
// подрезает молча — громко об этом говорит `static_assert` каталога.
inline int creature_tier(NPCType t) {
    return clamp_to_ladder(npc_def(t).baseLevel);
}

// ЦЕНА УБИТОГО. Вход — УРОВЕНЬ убитого: у головы инвентаря это ТИР ЕЁ СТРОКИ,
// у аватара — его настоящий уровень из анкеты, и звонящий передаёт ровно то,
// чем убитое является. Ветки по роду здесь нет.
//
// ЗДЕСЬ СТОЯЛО `xpReward + (level−1)·5`, то есть 10·L — ЛИНЕЙНАЯ награда против
// КВАДРАТИЧНОГО порога. Замер по формулам: убийств ровни на уровень росло
// 110 → 1100 на дистанции 1..100, а сильнейшая строка каталога на пятидесятом
// уровне платила 0.25 % уровня. Вердикт владельца 2026-11-03: ✓ «тут ошибка в
// коде и каноне а манифест прав ОПЫТ … по мощи … (ценности экспа по
// квадратичной динамике)».
// ДВЕРЕЙ ДВЕ, ПОТОМУ ЧТО ВОПРОСОВ ДВА, А ЗАКОН ОДИН.
//
// `head_xp_reward` — цена ГОЛОВЫ инвентаря: её тир, и больше ничего. Уровня
// экземпляра она не принимает ВОВСЕ, и это нарочно: прежняя `npc_xp_reward`
// брала его параметром, а сила головы его не читала никогда
// (`head_power@src/macro/auto_battle.h` принимает только строку) — платилось за
// силу, которой у головы нет.
//
// Цена АВАТАРА считается его настоящим уровнем и зовётся прямо —
// `exp_value_of_level@src/tables/progression.h`. Отдельной двери ей не нужно:
// уровень у неё уже на руках, а выдумывать строку-посредника значит заводить
// третий спеллинг одного закона.
inline int head_xp_reward(NPCType t) {
    return exp_value_of_level(creature_tier(t));
}

// (garrison_soldier_id_base — the high-bit garrison id space — died with
// §42 Инк 7: a garrison soul draws its identity from THE one macro
// ordinal issuer (gs.nextMacroSpawnOrdinal), same as every other soldier.
// One soul, one name space — a record that walked garrison → patrol →
// garrison keeps one identity for its whole life.)

// ── ПАСТВА ВСТАЁТ В КОНТЕЙНЕР ОДНОЙ СТРОКОЙ ─────────────────────────────────
// Здесь стоял `generate_garrison` (и тип `GarrisonResult` при нём): бросок
// монетки на КАЖДУЮ душу, 60 % Guard / 40 % Peasant. Вырезан 2026-09-22 по
// вердикту владельца: «в мире только СКВАДЫ и РОСТЕРЫ… пока никаких
// стражников, это усложняет систему; потом будем думать, когда вернём
// патрули и стражу».
//
// ЧТО ЭТИМ УМЕРЛО, КРОМЕ СТРАЖИ:
//   · ТРИ ПОТОКА RNG. Состав контейнера был жребием — на генезисе города, на
//     генезисе деревни и в ежедневном наборе. Теперь состав — факт, а не
//     бросок, и три `Rng grng(...)` у вызывающих ушли вместе с ним.
//   · ПОСЛЕДНИЙ ПЛАТЕЛЬЩИК ЖАЛОВАНЬЯ В МИРЕ. У крестьянина
//     `upkeepGoldPerDay` = 0 («работают за еду»), у лошади 0, у зверья −1.
//     Значит `wageDebt` мира становится СТРУКТУРНЫМ НУЛЁМ, а с ним —
//     колонки прибора `soulsUnpaid`/`crewsUnpaid` и ветка `byWage` в окне
//     контейнера. Колонка НЕ СНОСИТСЯ: жалованье вернётся с наёмниками, и это
//     известная спящая половина (§55), а не забытая.
//   · СТОК МОНЕТЫ. «Уплаченное сгорает в пул лута» перестаёт качать монету
//     из мира этой дверью.
//
// Строка каталога тел `NPCType::Guard` ОСТАЁТСЯ — прецедент тот же, что у
// патрульной строки реестра мест: «вырезан не вид, а то, что город его
// спавнит». Её носят авто-бой, сцена и двадцать тестов.
//
// (raise_flock_into_container переехала в bus/world_row.h слиянием M-71;
// найм стал exact-лотом одной сделки — deal@src/macro/deal.h, ломоть 2 M-72.)

// Case-insensitive token → registry row, matched against the row's stable
// machine `id` FIRST ("tax_collector" → NPCType::TaxCollector) — that column
// exists precisely to be what content names a row by — with the display
// `label` kept as a convenience fallback ("Tax-collector" still works at the
// console). Purely data-driven off kNpcTypeDefs: a new type is matchable the
// moment its row exists, no per-type branch. Returns false on no match — the
// CALLER decides its own fallback (the subworld console spawner keeps its
// historical silent-Bandit default; the macro spawn door `spawn_npc_at@src/
// macro/npc_spawn.h` refuses to spawn — здесь стояло «the SpawnEntity
// consumer», а события этого нет с M-410, правка шапки M-436).
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
TIMAERT_ROW(sm::NpcTypeDef);
TIMAERT_ROW(sm::FaunaBand);
