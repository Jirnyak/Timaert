// Universal combat + squad records.
//
// Soldiers are persistent NPC-kind records. Combat stats are read from
// the NPC registry's CombatTemplate; there is no separate unit schema.
#pragma once

#include "tables/actions.h"       // набор строки — ординалы таблицы действий (M-396)
#include "tables/damage_types.h"
#include "tables/progression.h"   // ЛЕСТНИЦА: kMaxTier + clamp_to_ladder

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <vector>

namespace sm {

// ── ЗДЕСЬ ЖИЛ `kMaxSoldierLevel`, ВТОРОЕ ИМЯ ОДНОГО ЧИСЛА (M-382) ──────────
//
// Шапка его гласила: «ЕДИНАЯ ЛЕСТНИЦА УРОВНЯ … та же лестница дальше ведёт
// зоны сложности и дроп по уровню», — то есть единство лестницы она объявляла
// сама, а исполняла его ВТОРОЙ копией числа 255 рядом с первой. Вердикт
// владельца 2026-11-03 («у нас есть УЖЕ ЗОНЫ СЛОЖНОСТИ И ТИРЫ ПРЕДМЕТОВ
// ОЧЕВИДНО ПРОСТО НАДО СЛИТЬ ЭТО В ОДНУ СИСТЕМУ … они уже u8 255 НА ЭТОМ И
// РЕШИЛИ») сделал обещание законом, и второе имя стало лишним спеллингом.
//
// Выжило имя ЛЕСТНИЦЫ, а не солдата: рода «солдат» в игре нет — есть сквады и
// головы инвентаря (ЗАКОН АНКЕТЫ, ЗАКОН ДВУХ ТАБЛИЦ). Одно число, один адрес:
// `kMaxTier@src/tables/progression.h`, и вывод 255 стоит ТАМ, у носителей.
// (Прежний кап 32767 был шириной int16 старого `SoldierSlot` — умер раньше.)

// THE default perception radius, in subworld metres: how far a body notices
// an enemy on its own when its row says nothing more specific. ONE home for
// one quantity (canon audit 2026-08-29): it also lived as sub/ai.h's
// kDetectionRadius = 200.0f — two houses for the same 200, free to drift —
// and that constant now reads this one. 200 m ≈ the far edge of the drawn
// scene: a body notices what the player could see.
inline constexpr float kNpcSightDefaultM = 200.0f;

struct CombatTemplate {
    enum AttackKind : std::uint8_t { Melee = 0, Missile = 1 };
    float       hp;
    // The row's natural weapon as DICE (CANON S13: урон = NdM строкой).
    // Scalar-era rows converted mechanically to Nd1 — the same fixed number
    // they always dealt, expectation AND variance preserved to the point
    // (owner verdict 2026-09-05); authored spreads (a troll's 4d12) are
    // content-stage work.
    Dice        dice;
    // How fast this row moves, as a FRACTION OF THE MARCH (owner's ruling,
    // 2026-08-30: «привести всех к маршу»). 1.0 is the world's own walking
    // pace — kSubworldWalkTilesPerSecond, itself derived from the 8 cells per
    // game hour the map marches at — so a peasant walks at exactly the speed
    // the map says a man walks, and everything else is stated against him:
    // a bandit runs, a rabbit bolts, a troll lumbers.
    //
    // It was an ABSOLUTE tiles/second until that ruling, on a scale nobody
    // had derived (peasant 20 against a march of 96), and the player was
    // fitted to it by a private ×0.4 in the engine — a second speed law for
    // one body, which is exactly what CANON S4 says cannot exist. The numbers
    // below are the old ones divided by the peasant's, so every relative
    // speed the fights were tuned around is preserved verbatim; what changed
    // is that they now mean something.
    float       speedMarchMult;
    float       attackRange;
    // БАЗОВОЕ РЕКОВЕРИ ЕСТЕСТВЕННОГО УДАРА, В ЦЕЛЫХ ТИКАХ — та же величина,
    // то же имя и та же единица, что `recoveryTicks` строки действия
    // (`ActionDef@src/tables/actions.h`): M-396, «У НАС ЕДИНАЯ СИСТЕМА
    // РЕКАВЕРИ», имя `cooldown` умерло. Строка СВОЕЙ строки действия не
    // имеет (вердикт владельца 2026-11-06, форма (а)): «у массовки всё из
    // его таблицы» (ЗАКОН ДВУХ ТАБЛИЦ). Авторится СЕКУНДАМИ через
    // constexpr `steps_from_seconds` на месте строки (вердикт M-396:
    // хранится ЦЕЛЫЙ тик, секунда живёт только в авторинге). Эффективное
    // считает одна дверь `recovery_steps@src/bus/anketa.h` (у аватаров —
    // через project_combat); голова инвентаря читается сырой колонкой
    // (`head_power@src/macro/auto_battle.h`).
    std::uint32_t recoveryTicks;
    const char* label;
    AttackKind  attackKind = Melee;
    float       missileSpeed = 0.0f;
    float       missileBlast = 0.0f;
    std::uint32_t missileColorRGBA = 0xFFFFFFFFu;
    // ── Spatial / perception, shared by every fighter table ────────────────
    // NO bodyRadius here — deliberately (damage-door track Inc 4, owner's
    // «единая система: просто число»). A body's WIDTH is one column of the
    // one body table, NpcTypeDef::radius (npc_body_radius resolves its
    // man-shaped default); this template's copy defaulted to the same 0.55,
    // was authored by zero rows and answered only when the real column was
    // silent — a second opinion waiting to drift. The ATTACK reach stays
    // `attackRange` above: one number per row, and when equipment lands
    // (work_vector §5) a spear modifies that number through the door.
    //
    // bodyHeight — how tall this thing is, in metres, for the eye (owner's
    // ruling, 2026-08-06: ONE column for humanoids and monsters alike), so a
    // dragon towers because of its row and not because of a branch in the
    // renderer. 0 = not stated; sub/body.h then derives it — a humanoid is a
    // person, a creature is as tall as the proportion the renderer used to
    // hardcode.
    float       bodyHeight = 0.0f;
    // sight — how far this fighter notices an enemy on its own. It is NOT an
    // aggro leash: awareness relays through a formation (see the alert chain in
    // sub/movement.h), so a rear rank charges because its front rank saw, while a
    // lone animal that noticed nothing stays put.
    float       sight = kNpcSightDefaultM;
    // The row's BASE MANA and BASE STAMINA — the floors the sheet law grows
    // the other two bars from, exactly as `hp` above is the first bar's
    // (CANON S14: the row is the floor, the sheet multiplies). 100 = the
    // world's bare level-1 base the whole bar law is tuned around (the same
    // 100 `hp` uses); a row that wants a different well states it here.
    // Until §41 root 2 these lived as DEFAULT ARGUMENTS of bar_ceilings —
    // a table row smuggled past the table, so every body's MP/SP base was
    // one hidden 100 no row could override. int16 by the type law: a base
    // bar is a design number in the hundreds, not a float.
    std::int16_t mp = 100;
    std::int16_t sp = 100;
    // Which of the nine columns this row's natural weapon argues with.
    // Authored Blunt everywhere by the mechanical translation; claws and
    // fangs pick their columns at content stage — the dragon's fire is the
    // first (it sat in the "filled, never authored" section below, which
    // its own comment contradicted).
    DamageType   dmgType = DamageType::Blunt;
    // ЛЕТУН (владелец 2026-09-10: «субмир 3D — надо чтобы все воспринимали
    // x/y/z»; полёт честный, как у игрока — M&M-реф). > 0 = тело рождается
    // с ecs::Flying (гравитация снята, конверт [опора, потолок] общий с
    // игроком) и КРЕЙСЕРСКОЙ высотой предпочтения в метрах — это характер,
    // не закон: мозг тянется к ней в роаме, уходит выше в побеге, снижается
    // в атаку (пике придёт с первым дерущимся меле-летуном). 0 = наземный.
    // На карте та же колонка делает марш полётным (try_move: рельеф не
    // платится, вода не требует корабля).
    float cruiseM = 0.0f;

    // НАБОР ДЕЙСТВИЙ СТРОКИ — ординалы ЕДИНОЙ таблицы действий (M-396;
    // вердикт владельца 2026-11-06, дословно: «существа берут что они могут
    // делать в свои таблицы и модули из неё»). Здесь жил `int castSpell =
    // −1` — ординал СПЕЛЛА с сентинелом, который владелец не любит; сентинел
    // умер в `kActionNone` (последнее значение u8, ЗАКОН УЗКОГО ИНДЕКСА), а
    // ординал стал ординалом ДЕЙСТВИЯ.
    //
    // ФОРМА — ФИКСИРОВАННЫЙ МАЛЫЙ МАССИВ, А НЕ ОТРЕЗОК (назначено рядом
    // M-369): лестница действий упорядочена по архетипу, поэтому укус (бой)
    // и дыхание (магия) одного существа непрерывными быть не могут. Сегодня
    // набор несёт МАГИЮ строки — её каст идёт кубами и типом урона СПЕЛЛА
    // через те же двери, что рука игрока (расклейка каста и выстрела,
    // 2026-09-17: строка без магии в наборе и с `Missile` — это ВЫСТРЕЛ,
    // кости + стрелковый скилл, БЕЗ добавки атрибута). Естественный удар
    // живёт колонкой `recoveryTicks` выше (форма (а), 2026-11-06), авторские
    // боевые строки придут в наборы со своими модулями.
    //
    // Авторится `action_slot("fireball")` — constexpr, резолв в КОМПИЛЯТОРЕ
    // (ЗАКОН СЛОВАРЯ), рождение тела строк не сравнивает. Кап 2 — наибольший
    // сегодняшний набор (один спелл) плюс слот второго действия; балансное
    // число, поднимается строкой (static_assert ширины — у `kActionNone`).
    std::uint8_t actions[kCreatureActionSlots] = {kActionNone, kActionNone};

    // ── Filled by project_combat, never authored (a row has no sheet) ──────
    // The sheet's attribute ADD to every roll of the dice above (STR-derived
    // for melee rows, INT-derived for missile ones), floored to the int house.
    std::int16_t flatAdd = 0;
    // The sheet's LCK — the crit door's ask, once per strike (core/dice.h).
    std::uint8_t luck = 0;
    // The TYPED skill percent over the dice (CANON S13: (бросок + добавка) ·
    // скилл-процент) — the sheet's best-trained skill of the attack's domain
    // (sheet_strike_mult_pct), NEVER authored on a row. Session Е 2026-09-19:
    // this was a hardcoded 100 at BOTH consumers (spawn's combat_from_sheet
    // and auto_battle's avatar_power/head_power) — the largest «игрок == НПЦ»
    // asymmetry: the points the generator spent into weapon skills and
    // schools multiplied nothing.
    std::int16_t multPct = 100;
};

// ЧЕМ СТРОКА КАСТУЕТ — первое МАГИЧЕСКОЕ действие её набора; kActionNone =
// не кастует вовсе. Вопрос решает ОТРЕЗОК лестницы, не новая колонка (ЗАКОН
// ЛЕСТНИЦЫ п.2); одна дверь, чтобы «первый маг-ординал набора» не стал
// арифметикой по месту вызова.
inline constexpr std::uint8_t row_cast_action(const CombatTemplate& row) {
    for (std::uint8_t a : row.actions)
        if (a != kActionNone
            && archetype_of(action_def(int(a)).type) == ActionArchetype::Magic)
            return a;
    return kActionNone;
}

// ── ЗАКОН УРОВНЯ СТРОКИ: ОСТАЛСЯ ОДИН, И ОН ПРО ЭКОНОМИКУ ─────────────────
// Здесь стояли ДВЕ двери — «посади уровень на лестницу» и «во сколько раз он
// дороже». Первая (`normalize_soldier_level`) уехала к самой лестнице
// (`clamp_to_ladder@src/tables/progression.h`, M-382): граница лестницы есть
// свойство ЛЕСТНИЦЫ, а не каталога найма, и рукописных копий того же условия в
// дереве было ещё три. Имя сменилось вместе с домом — «солдат» родом не
// является (ЗАКОН ИМЕНИ: имя — слабейший свидетель, и чинится автосменой).
//
// Вторая осталась: множитель цены есть закон ЭКОНОМИКИ — найм, содержание,
// стоимость (ЗАКОН ДВУХ ТАБЛИЦ: «уровень это строка уровня в таблице объектов
// (типа тир) а стоиомтсь это стоимость для экономики»). В БОЮ он не читается
// ни разу, и читать его там запрещено.
inline int soldier_level_factor(int level) {
    return 1 + (clamp_to_ladder(level) - 1) / 3;
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
TIMAERT_ROW(sm::CombatTemplate);
