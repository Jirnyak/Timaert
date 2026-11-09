// ECS components — small POD structs as per AGENTS.md.
#pragma once
#include "core/pools.h"
#include "core/dice.h"           // Dice — кости боевого листа тела (Combat)
#include "core/macro_handle.h"   // MacroHandle {слот, ID} — через шов (M-220)
// (`macro/upkeep.h` СНЯТ 2026-11-03, M-342: он стоял ради одного алиаса
//  `ecs::SquadUpkeep`, уехавшего к своему типу, — и доставлял макро-слой
//  транзитом в каждый TU субмира, включающий компоненты.)
// (`tables/commodity.h` и `<string>` СНЯТЫ ломтём 2а эпика пакетов v1: оба
//  стояли без единого употребления в файле — `kCommodityCount` уехал вместе с
//  `SquadUpkeep` ломтём 1г, а `std::string` здесь не рождался никогда, и это
//  закон, а не случай: POD-строка мира обязана переживать `memcpy` (§12).)
// (`core/torus.h`, `bus/anketa.h` и `<array>` СНЯТЫ ломтём 2б-I: все три
//  стояли ради КОЛОНОК СКВАДА, уехавших в `bus/squad_row.h` целиком —
//  адрес клетки с его тремя написаниями, контейнер и сбруя, маршрут. После
//  разреза в этом файле остались формы ТЕЛА и СЦЕНЫ плюс два шовных
//  компонента, и ни одной из трёх строк им не нужно.)
#include <cstdint>

namespace sm {

// (MacroHandle {слот, ID} живёт в core/macro_handle.h (переезд из macro/
// — ломоть 3 эпика фрейма: адресная форма ниже обоих миров, bus её несёт): его
// включает и сознательно лёгкий конверт мира macro_world.h, которому
// ecs-слой запрещён шапкой. Здесь — транзитивное включение ради всех,
// кто брал его отсюда; несёт его через шов компонента ecs::MacroOrigin.)

} // namespace sm

namespace sm::ecs {

// World-space position: x,y are tile coords; z is absolute world-space altitude
// in metres (same coordinate system as cam_.pos.y). Ground entities get z set to
// the window height field's sample(x,y) by the per-tick ground-follow system. Flying entities and
// projectiles own their z through movement/velocity. Water surface sits at
// sub::sea_level_m(the scene's plane) ≈ 600 m in the default world — the
// whole vertical model is sub/height.h.
struct Position { float x, y, z; };

// Smoothed render position (for visual interpolation) — SCENE units (tiles
// of the composite window; the scale split, 2026-09-10). Scene bodies only.
struct VisualPos { float vx, vy, speed; };

// THE POOLS OF A BODY — moved to its own entt-free header (core/pools.h) so
// the recovery law and the stamina bookkeeping compile without the component
// creatures; the story of the block lives with the block.

// (An explicit `BodyRadius` override lived here and died in M-150 кусок 0:
// the census found ZERO production writers — only test fixtures fed it, so
// the override had outlived every author it was built for. A body's width is
// the ladder in sub/body.h: table row → SubworldAi.radius → Sprite.scale →
// fallback.)

// Combat stats — universal stat block, the ECS face of CombatTemplate.
struct Combat {
    // The strike as DATA for the one assembly law (tables/damage_types.h
    // roll_strike): dice + attribute add + skill percent + LCK. dmgType is
    // the DamageType ORDINAL raw — the ecs layer stores ordinals, not macro
    // enums (the NPCKind.type idiom); the strike site casts it back.
    Dice dice;
    std::int16_t flatAdd;
    std::int16_t multPct;   // 100 = untrained; 100 + rank·pctPerRank in hand
    std::uint8_t luck;
    std::uint8_t dmgType;
    float speed;            // grid units / s
    float attackRange;
    // ЭФФЕКТИВНАЯ база удара ЭТОГО тела, в целых тиках: выход двери
    // восстановления (project_combat / hand_strike_fields), уже поделённый
    // резвостью Spd и генериком домена. Каждый замах заряжает гейт ниже
    // ровно этим числом — секунды на пути восстановления не живут нигде
    // (M-396, ЗАКОН ТИПА п.5; имя cooldown умерло вместе с флоатом).
    std::uint32_t strikeTicks;
    // THE recovery gate — the body's ONE «occupied» clock (owner verdict
    // 2026-09-09): steps left before this body may ACT again. A swing charges
    // it, a cast charges it (spellbook_cast), a future shot will charge it —
    // and every action checks the same field first, so a hand busy with a
    // sword cannot also be throwing fire. Integer steps (core/time.h): a blow
    // lands after the same amount of FIGHT whether the clock races or crawls.
    // Movement deliberately never reads it — recovering legs still walk.
    std::uint32_t recoverySteps;
    enum Kind : std::uint8_t { Melee = 0, Missile = 1 } kind;
    // ВТОРОЙ СУБЪЕКТ ТОГО ЖЕ ЗАКОНА — ПРОСТОЙ БРОНИ (CANON S13, AGENTS ЗАКОН
    // СПОСОБНОСТИ п.5; вердикт владельца 2026-09-30: «рековери от брони не
    // останавливет дейтсвия а отключает броню на время рековери (урон
    // проходит)»). Шаги, пока НАДЕТАЯ броня выбита из строя: пока их больше
    // нуля, процентная колонка тела равна нулю, а плоский блок остаётся.
    //
    // ЭТО НЕ ВТОРОЙ ГЕЙТ, и разница несущая: `recoverySteps` выше отвечает
    // «занято ли ТЕЛО» и запирает действия, этот — «в строю ли БРОНЯ» и не
    // запирает НИЧЕГО. Закон один (`recovery_steps@src/bus/anketa.h`), база
    // своя (вес надетой брони), сливает обоих один `tick_body_recovery`.
    //
    // ПОЧЕМУ ЗДЕСЬ, А НЕ В НОВОЙ КОЛОНКЕ: это второй субъект ОДНОГО закона, и
    // жить он обязан рядом с первым — разведи их по двум домам, и следующий
    // читатель найдёт два закона восстановления вместо одного. Уедет вместе с
    // этим листом целиком (M-150), а не отдельно.
    //
    // uint16 по ЗАКОНУ ТИПА: при 64 шагах/с (core/time.h) 65535 шагов = 1024
    // игровые секунды простоя, то есть три порядка над любой правдоподобной
    // базой (самая тяжёлая сбруя каталога — десятки килограммов, то есть
    // десятки секунд ДО деления на быстроту и Армсмастера).
    //
    // СТОИТ В КОНЦЕ СТРУКТУРЫ, И ЭТО НЕ ВКУС — ЭТО ШРАМ ЭТОЙ ЖЕ ПРАВКИ. Тела
    // конструируются ПОЗИЦИОННЫМИ списками (`sub/spawn.cpp`, `sub/engine.cpp`),
    // и первая версия колонки встала ПЕРЕД `kind`: последнее переданное
    // значение уехало в неё, а `kind` занулился — то есть КАЖДОЕ стрелковое
    // тело молча стало рукопашным. Поймало это не ревью, а
    // `-Wmissing-field-initializers` (закон «ноль предупреждений», §9). Новая
    // колонка `Combat` приписывается СЮДА, в хвост. Ровно эта же надпись уже
    // стоит в `ItemDef` (`tables/items.h`) по ровно той же причине.
    //
    // И БАЙТОВ ЭТО НЕ СТОИЛО: `uint16` после `uint8 kind` садится на смещение
    // 26 в хвостовой паддинг, поэтому сторож `sizeof(Combat) == 28` ниже не
    // дрогнул — простой брони приехал миру бесплатно.
    std::uint16_t armorSteps;
};
// Прибито по DOD п.10: у структуры, чей размер назван, размер закрепляется.
// Сторож охраняет не ВЫБОР байт, а тишину: новая колонка здесь стоит ×кап
// сцены субмира (kMaxSubObjects), и такое решение обязано быть осознанным,
// а не замеченным через месяц. 2026-10-01 (M-194) оно СДЕЛАНО осознанно: `armorSteps` лёг в
// хвостовой паддинг рядом с `kind`, поэтому 28 Б остались 28 Б — простой брони
// не стоил миру ни байта, и сторож это доказал, не дрогнув.
static_assert(sizeof(Combat) == 28, "боевой лист тела — 28 Б");

// Extra projectile data for Combat::Missile attackers.
struct MissileAttack {
    float speed;
    float blastRadius;
    std::uint32_t colorRGBA;
};

// Tag components. (An `Active` tag lived here with THIRTEEN emplace sites and
// zero readers — no view, no any_of, nothing ever asked. Deleted 2026-08-05:
// a tag nobody reads is not a state, it is noise every spawn site paid for.)
// (Dead / PlayerSoldierTag / TempHostileToPlayer / Flying УМЕРЛИ 2026-10-05,
// M-150 ломоть 1б: четыре тега тел — БИТЫ маски flags единого массива
// объектов (sub/objects.h); читаются/пишутся дверями object_flag*
// (sub/record.h). Трое живы битами; TempHostileToPlayer умер ВТОРОЙ РАЗ и
// насовсем — 2026-11-09, M-422: вражда сцены есть отношение ФРАКЦИЙ, а не
// свойство ударенного тела, и бита у неё больше нет вовсе.)
// «Кем я на КАРТЕ» — the macro flag, and MACRO ONLY (scale split, owner
// verdict 2026-09-10): it rides the player's own squad by default and a
// possessed lord's while he wears one. С 1е (кластер 5) оба макро-вопроса —
// «кем я на карте» и «чей это отряд» — НЕ компоненты: это два packed-хэндла
// `GameState::playerFlagBits`/`playerSquadBits` (macro/player_entity.h,
// битовые двери). Теги PlayerTag/PlayerSquadTag умерли вместе со сканами
// view<...> — ответ стал распаковкой поля мира.
// (AvatarTag УМЕР 2026-10-05, M-150 ломоть 2 кусок 2 — вердикт владельца
// «активность — ссылка, не свойство тела»: «моё тело в сцене» — ССЫЛКА
// сцены {слот, ID} в SubObjects.avatar, пишет её одна дверь
// set_avatar_slot@sub/doors.h. Одержимость и будущее переключение сквадов
// фракции — перезапись одной ссылки; «двух аватаров» не существует по
// построению.)
// (SubworldTag умер ломтём 6 M-150: «жилец сцены» = слот арены, биекция
// «живой слот ⟷ сущность с ObjectSlot» — свидетель в body_contract_test;
// тег был вторым ответом на вопрос, на который уже отвечал слот.)
// `Airborne { float vz }` СНЕСЕНА 2026-11-08 (ревизия, DOD п.9): ноль
// конструкций, ноль чтений, ноль записей во всём дереве. Величину несут бит
// `kObjAirborne` + колонка `airborneVz@src/sub/objects.h`, двери —
// `src/sub/doors.h`; некролог той замены стоял рядом с битом ещё тогда, а
// структуру забыли. Опциональность компоненты, за которую она жила, отменена
// ЗАКОНОМ СТРОКИ КАТАЛОГА и единым плоским массивом тел.

// (ObjectSlot УМЕР ломтём D M-150, 2026-10-16: слот стал САМОЙ
// идентичностью тела — жильцы сцены живут в едином массиве sub/objects.h,
// сущностей у тел больше нет, адрес = слот, идентичность = ID рождения.)

// Subworld behaviour state (mirrors `subworld/ai.ts`). Scene bodies only
// (жильцы арены); engine dispatches Wander/Flee/Combat by `kind`.
// ONE owner of legs. `wantVx/wantVy` are the brain's INTENT — the wander
// amble or the flee sprint, written ONLY by tick_npc_ai (the Combat mind
// wants nothing here; its drive is the influence field). `vx/vy` are the
// body's ACTUAL velocity, written back each tick by the battle steering
// pass, which is the only thing that ever moves a body. The two used to be
// one field, and two integrators fought over it: steering decayed the
// brain's chosen pace toward zero within ~6 ticks and wrote the corpse of
// it back, so wanderers twitched for a tenth of a second per decision and
// stood the rest. `aiTimer` counts down to the next decision; `wanderSpeed`
// is the slow idle pace (Combat uses Combat::speed instead).
struct SubworldAi {
    enum Kind : std::uint8_t { Wander = 0, Flee = 1, Combat = 2 };
    Kind  kind;
    float aiTimer;
    float vx, vy;
    float wanderSpeed;
    float radius;
    // НАМЕРЕНИЕ — ТРИ ОСИ (владелец 2026-09-10: «субмир 3D — все
    // воспринимают x/y/z»; 2D-мозг был пережитком старого субмира).
    // wantVz читает только тело с ecs::Flying — ходока к земле прижимает
    // закон опоры (vertical_step), не другой мозг: один mover, одна рамка.
    float wantVx = 0.0f, wantVy = 0.0f, wantVz = 0.0f;
    // Decision counter, folded into the wander RNG seed. The seed used to be
    // entity-bits ⊕ position — safe while the brain moved its own body, fatal
    // once it stopped: a wanderer that rolled "stand" froze its position, and
    // a frozen position re-rolled the SAME "stand" forever. The counter
    // advances per decision, so a standing mind still changes its mind.
    std::uint32_t seq = 0;
};
// DOD п.10, тот же сторож, что у Combat: 40 Б × кап сцены субмира
// (kMaxSubObjects) = 640 КиБ, поэтому новая колонка мозга — решение, а не мелочь.
static_assert(sizeof(SubworldAi) == 40, "мозг тела субмира — 40 Б");

// (ЗДЕСЬ ЖИЛ GoingHome — «идёт домой» ходока помпы дня. УМЕР 2026-11-07
//  вместе со всей помпой, вердикт владельца: «лучше выжеч весь кал и
//  сделать заново по правильному» — жизнь улицы сквозь время вернётся
//  АИ-модулем места, не компонентой ядра. Ряд M-419.)

// (SoldierLink УМЕР 2026-10-05, M-150 ломоть 2 кусок 1: писатель один,
// читателей в src НОЛЬ — идентичность солдата {entityId, kind, level} УЖЕ
// едет займом контейнера MacroDebt{detail, detailKind, detailLevel}, второй
// носитель того же факта; свидетели переякорены на займ.)

// What this body/prop was BORROWED FROM in the macro world (macro/macro_stock.h).
//
// The subworld is a context of the map above it: a citizen is a unit of a
// settlement's population made visible, a tree is one of the cell's trees. This
// component is the receipt — it names the stock, where that stock lives, and
// how much of it this thing is worth — so that when the thing dies, is felled
// or is given back, ONE settler can pay the macro world without knowing what
// kind of thing it was looking at.
//
// `stock` is a plain byte on purpose: the ECS layer sits below macro/ and must
// not learn the enum. The row it indexes lives in macro/macro_stock.h.
struct MacroDebt {
    std::uint8_t  stock;          // MacroStock row index
    std::int32_t  subject;        // subject ordinal (ONE space, M-37), 0 = the cell
    std::int16_t  cellX;
    std::int16_t  cellY;
    std::uint16_t amount;         // how much of the stock this one thing is
    // Which row WITHIN the subject this thing stands for, when the stock is a
    // TABLE rather than a count: the creature row stores the member's
    // SoldierRecord::entityId here (as a bit pattern — ids may use the high
    // bit), so a death removes the very soldier who fell, not "one of them".
    // -1 = no name. A GENERIC soul (creatures-as-inventory, CANON S4) HAS no
    // entityId — its name IS {kind, level}, carried in the two fields below
    // (detailLevel > 0 marks them live; kind alone cannot, Peasant == 0), and
    // its death honestly removes "one of that stack".
    std::int32_t  detail = -1;
    std::uint16_t detailKind = 0;
    std::int16_t  detailLevel = 0;
};

// Backlink from a PROJECTED subworld body to the persistent macro record it
// stands for. A macro NPC standing in (or beside) the entered cell is
// projected into the 3×3 window as a full combat body so the player can meet —
// and, with the possession spell, wear — the very lords/bandits/peasants that
// roam the overworld.
//
// THIS HANDLE IS THE SEAM'S ONE DOOR (sub/record.h macro_record_of / pools_of): the
// record is AUTHORITATIVE and the body is its MIRROR, re-pulled every tick
// top, so a wound or a spend written on the body evaporates a frame later and
// must go to the record instead. The line that stood here — «the macro entity
// stays authoritative and untouched for the whole session» — was true of the
// fold-up architecture that preceded the mirror and false after it: untouched
// is exactly what the record is not.
//
// Also how leave() maps the flagged body back to a macro cell on exit, and how
// the reaper (clear_subworld_world_entities) knows to leave projections be.
// Runtime-only: never serialized, so it does not bump kSaveVersion.
//
// ХЭНДЛОМ STORE, не entt-сущностью (эпик 2 шаг 1е): запись — слот MacroStore
// {slot,gen}; протухание = поколение слота ушло (store_death), и дверь шва
// (sub/record.h macro_record_of) честно деградирует тело в «сам себе запись».
struct MacroOrigin { MacroHandle macro; };

// (LastHit / HitFlash / DamageFx УМЕРЛИ 2026-10-05, M-150 ломоть 1а: «кто
// бил» и событие «попали» — колонки единого массива объектов
// lastHitBy / damageFx (sub/objects.h); их законы — там же, у колонок,
// вместе с законом «никто» для атакера. Вспышка тела снесена ЦЕЛИКОМ тем же
// днём — вердикт владельца «пока не нужна снесём»: колонкой она писалась и
// гасла, но визуального читателя не имела никогда.)

// Sprite (atlas index + tint). `spriteRow` is this body's row in THE sprite
// table (tables/sprite_rows.h) held as a raw ordinal, because the ECS layer may
// not include macro/ — the same reason the procedural archetype it replaced was
// a bare byte. The row decides everything about the look: drawn art if the
// artist drew this kind, a procedural body plan if he did not, and the renderer
// asks the row rather than branching on what sort of thing this is. Row 0
// (SpriteId::None) is the default and means "not drawn as a body at all" —
// spell projectiles and engine sprites keep it and no body pass claims them.
// `scale` is the body's physical half-width (the same number sub/body.h calls
// the combat radius); `height` is how TALL it is drawn, in metres. Two numbers,
// because a billboard has two dimensions and a tree has said so all along
// (TreeInstance carries halfWidth AND height). Height used to be a literal
// `2.0f` for every humanoid in the renderer and `scale * 1.5f` for every
// creature — so a body's size was authored data for physics and a constant for
// the eye, which is the same defect as "a tree's height was never drawn".
// 0 = not stated: the draw path derives it the old way, so anything that is not
// a body (a projectile card) is unaffected.
// Альфы нет: поле `a` умерло ломтём 3 M-150 — читателей у него не было ни
// одного во всём дереве (писатели клали 255 в никуда; колонка-сирота,
// DOD п.9). sizeof не сдвинулся — байт съедал паддинг.
struct Sprite { std::uint16_t atlasId; std::uint8_t r, g, b; float scale;
                std::uint8_t spriteRow = 0; float height = 0.0f; };

// Positional point-light emitter (graphics only). Any subworld entity carrying
// one casts a point light: the 3D renderer sweeps слоты с битом kObjHasLight
// (колонка light@src/sub/objects.h) each frame and packs it into the set-0/binding-1 light SSBO that
// shaders/lighting.glsl's point_lights() sums (see src/sub/lighting.h GpuLight).
// `off*` is a world-space metres offset added on top of the entity's ground
// position (e.g. a torch held at chest height, a spell glow at the projectile).
// Radius is the attenuation reach in metres; intensity the linear gain. This is
// the ONE universal light source — the player, NPC torches, spell/projectile
// glows and lit windows all attach the same component, no per-emitter code.
struct LightEmitter {
    float offX, offY, offZ;    // world-space offset (m) from the entity position
    float r, g, b;             // linear RGB radiance
    float radius;              // attenuation reach (m)
    float intensity;           // scalar gain
};

// (`SquadUpkeep` — алиас ведомости содержания — УЕХАЛ в `bus/upkeep.h`
//  2026-11-03, M-342 ломоть 1г, вместе со всей своей доктриной. Он стоял
//  здесь и был ЕДИНСТВЕННОЙ причиной, по которой этот заголовок включал
//  `macro/upkeep.h`, — а значит тащил макро-слой транзитом в каждый TU
//  субмира, включающий компоненты, при том что в `src/sub` ни одного символа
//  содержания не употребляется ни разу. Читатели имени — `macro/stacks.h`,
//  `bus/store.h`, `macro/macro_snapshot.cpp`, `macro/npc_spawn.cpp` — все
//  приходят к типу и так. Тело компоненты не изменилось ни на байт: это был
//  алиас, а не носитель, и `static_assert(sizeof(ecs::SquadUpkeep) == 80)` в
//  `macro/stacks.h` сторожит это по-прежнему.)

// (`Structure` УМЕРЛА 2026-10-09, M-150 ломоть 1 — ВТОРОЕ СЛОВО ОБ ОДНОЙ
// ВЕЩИ, И ОНО НЕ ОТВЕЧАЛО НИ НА ЧТО. Писателей у неё не было НИ ОДНОГО: ни
// `emplace`, ни `insert`, ни `patch` во всём дереве, — а читателя было два,
// оба в отладочной панели F3 (`src/app/main.cpp`), и оба по построению
// видели ноль: ветка «(structure)», недостижимая никогда, и строка переписи,
// всегда печатавшая 0. Живой носитель этого вопроса — `Structure@src/sub/map_data.h`,
// хребет пропов, которым пользуются все генераторы `src/sub/gens/`. Её
// собственный род уже умирал тем же доводом 2026-10-05 — «строка без
// производителя есть мёртвый путь» (ЗАКОН СТРОКИ КАТАЛОГА); теперь довод
// дошёл до самой строки. Снос проверен КОМПИЛЯТОРОМ, а не грепом.)

// СТРУКТУРА `CorpseLoot` СНЕСЕНА 2026-10-05 — вердикт владельца «трупы-
// контейнеры снять совсем, ждём ПУЛ ЛУТА». Наследника у неё НЕТ, и это
// намеренно: лут с убитых не падает, пока не встанет пул, «режиссёр лута всей
// игры». Заглушку на её место ставить запрещено (игра не играбельна, идёт
// полировка ядра) — дыра стоит открытой и названа в реестре.
//
// ПОЧЕМУ НЕ КОЛОНКА АРЕНЫ. `sizeof(Inventory)` = 40 960 Б: колонка на 16384
// слота стоила бы 671 МиБ, но дело даже не в них — зеркальный закон
// (`sub/record.h`) говорит, что тело субмира НЕ ВЛАДЕЕТ НИЧЕМ, его сумка есть
// сумка макро-записи, которую оно отражает. Инвентарь на теле был бы вторым
// носителем одного имущества — ровно та форма, ради убийства которой
// зеркальный закон и написан.

// Spell projectile.
struct Projectile {
    enum Kind : std::uint8_t { Bolt = 0, Beam = 1 };
    float vx, vy, vz;
    float radius;
    float lifeTimer;
    float maxLifeTimer;
    // The wound, rolled at cast/loose — integer like every combat quantity
    // (the geometry around it stays float: distances are physics, not law).
    std::int32_t damage;
    float blastRadius;
    float originX;
    float originY;
    float beamLength;
    // Per-jump damage retention in whole percent (70 = each hop keeps 70%) —
    // the int face of the spell row's own column.
    std::uint8_t chainDecayPct;
    // Владелец — ПАРА ГОЛЫХ КОЛОНОК {ownerSlot, ownerId}: ссылка сцены по
    // закону ObjRef (ломоть 7 (iii-б)), но БЕЗ типа — ecs/ не включает sub/
    // (вердикт 2026-10-10: «ObjRef в components.h не нужен»). ownerId — ID
    // рождения арены, 0 = «ничей» (кастер умер, фикстура); судит ТОЛЬКО
    // читатель через ref_slot. ownerSlot сел в дыру выравнивания 45-47
    // (офсет 46; байт 45 остался паддингом) — sizeof 68 НЕ вырос.
    std::uint16_t ownerSlot;
    float chainRadius;
    std::uint32_t spellId;
    std::uint32_t ownerId;
    std::int16_t chainRemaining;
    Kind kind;
    bool visualOnly;
    bool explodeOnExpiry;
    // The blow's armour column (DamageType ordinal) and the crit verdict —
    // both rolled at cast/loose, carried to the damage door like the wound.
    std::uint8_t dmgType;
    bool critical;
};
// DOD п.10. Самая широкая из трёх (68 Б), и растёт она легче всех — каждый
// новый род снаряда просит себе поле. При капе тел субмира это 1088 КиБ, так
// что сторож стоит здесь именно чтобы рост был ВИДЕН в дифе.
static_assert(sizeof(Projectile) == 68, "снаряд — 68 Б");

} // namespace sm::ecs

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
// Тринадцать строк КОЛОНОК СКВАДА уехали в контур `bus/squad_row.h` ломтём
// 2б-I (MacroCell, CellSub, NPCKind, NpcCharacter, SquadName, NpcInventory,
// BodyEquipment, NpcTraits, MacroNpcRuntime, SquadGoal, MacroSpawnId,
// SquadOrders, DesignCharacterTag): стена контура ЕДЕТ СО СТРОКОЙ, иначе
// уехавшая форма осталась бы без судьи.
TIMAERT_ROW(sm::MacroHandle);
TIMAERT_ROW(sm::ecs::Position);
TIMAERT_ROW(sm::ecs::VisualPos);
TIMAERT_ROW(sm::ecs::Combat);
TIMAERT_ROW(sm::ecs::MissileAttack);
TIMAERT_ROW(sm::ecs::SubworldAi);
TIMAERT_ROW(sm::ecs::MacroDebt);
TIMAERT_ROW(sm::ecs::MacroOrigin);
TIMAERT_ROW(sm::ecs::Sprite);
TIMAERT_ROW(sm::ecs::LightEmitter);
TIMAERT_ROW(sm::ecs::Projectile);
