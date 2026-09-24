// Universal combat + squad records.
//
// Soldiers are persistent NPC-kind records. Combat stats are read from
// the NPC registry's CombatTemplate; there is no separate unit schema.
#pragma once

#include "macro/damage_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <vector>

namespace sm {

// ЕДИНАЯ ЛЕСТНИЦА УРОВНЯ (владелец, 2026-09-24, эпик единой таблицы):
// уровень экземпляра — колонка `level` слота В (items.h ItemRef), ширина u8,
// кап 255 назван вслух и принят; та же лестница дальше ведёт зоны сложности
// и дроп по уровню. Прежний кап 32767 был шириной int16 старого SoldierSlot.
inline constexpr int kMaxSoldierLevel = 255;

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
    float       cooldown;
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

    // ЧТО ЭТА СТРОКА КАСТУЕТ (owner verdict 2026-09-17, built 2026-09-19 —
    // РАСКЛЕЙКА КАСТА И ВЫСТРЕЛА): «каст у нас через систему спелов а есть
    // ещё система стрельбы (метание/луки/арбалеты/мушкеты и тд)».
    //
    // ORDINAL of the spell registry row, ≥ 0 = this creature ATTACKS BY
    // CASTING: its blow is then the SPELL's — the spell's dice, the spell's
    // damage type, the caster's INT, the rank of the spell's own school —
    // through the very doors the player's hand casts through. −1 = it does
    // not cast, and then a Missile row is a SHOT: dice + the shooting skill,
    // NO attribute add (CANON S14 «урон стрелкового БЕЗ добавки атрибута»).
    //
    // A NUMBER, not a string (owner, 2026-09-19: «а то засрём данные»): the
    // registry law is «strings stay the AUTHORING key in tables, the runtime
    // carries the ordinal» — the same law `faction_index` and every item row
    // already obey. Rows author it as `spell_ordinal("fireball")`, which is
    // constexpr, so the lookup happens in the COMPILER and a body's birth
    // (project_combat, run per spawn, thousands per scene) never walks the
    // spell table comparing strings.
    //
    // Until this column existed, `attackKind == Missile` MEANT "caster" —
    // every Missile row in the game happened to be one — so the first
    // NPC archer would have been handed an INT bonus to his arrows by a
    // column that never claimed to speak about magic.
    int castSpell = -1;

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
    // and auto_battle's fighter_power) — the largest «игрок == НПЦ»
    // asymmetry: the points the generator spent into weapon skills and
    // schools multiplied nothing.
    std::int16_t multPct = 100;
};

struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle; 0 =
                                // a GENERIC soul (no history, stackable)
    // WHAT this member is, in the ONE id space every body already shares with
    // the ECS (`ecs::NPCKind.type`): an ordinal of the one npc table — a wolf
    // is as legal a row as a spearman, so a wolf pack IS a squad, and the byte
    // this used to be could not say so (CANON.md S4/S16). The old
    // `0x100 | catalog index` monster encoding is dead with the second table
    // (npc.h). Sixteen bits, validated by npc.h `valid_npc_kind`.
    std::uint16_t kind     = 0;
    std::int16_t  level    = 1;
};

inline int normalize_soldier_level(int level) {
    if (level < 1) return 1;
    if (level > kMaxSoldierLevel) return kMaxSoldierLevel;
    return level;
}

inline bool operator==(const SoldierRecord& a, const SoldierRecord& b) {
    return a.entityId == b.entityId && a.kind == b.kind && a.level == b.level;
}
inline bool operator!=(const SoldierRecord& a, const SoldierRecord& b) {
    return !(a == b);
}

// ── СЛИЯНИЕ M-71 (2026-09-24): ПЛОТНЫЙ РОСТЕР УМЕР ─────────────────────────
// SoldierSquad / SoldierSlot / kMaxSquadSlots / souls()-развёртка вырезаны:
// существа лежат СТРОКАМИ МИРА в едином контейнере (Inventory, закон двух
// областей — items.h), все операции — дверями macro/world_row.h
// (creatures_push / pop_back / remove_one / move / add, головы, развёртка).
// Здесь остались боевой лист строки и МОНЕТА ПЕРЕНОСА души.

inline SoldierRecord make_soldier(std::uint16_t kind, int level,
                                  std::uint32_t entityId) {
    SoldierRecord s{};
    s.entityId = entityId;
    s.kind = kind;
    s.level = std::int16_t(normalize_soldier_level(level));
    return s;
}

inline int soldier_level_factor(int level) {
    const int safeLevel = normalize_soldier_level(level);
    return 1 + (safeLevel - 1) / 3;
}

} // namespace sm
