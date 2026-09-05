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
#include <vector>

namespace sm {

inline constexpr int kMaxSoldierLevel = 32767;

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

    // ── Filled by project_combat, never authored (a row has no sheet) ──────
    // The sheet's attribute ADD to every roll of the dice above (STR-derived
    // for melee rows, INT-derived for missile ones), floored to the int house.
    std::int16_t flatAdd = 0;
    // The sheet's LCK — the crit door's ask, once per strike (core/dice.h).
    std::uint8_t luck = 0;
    // Which of the nine columns this row's natural weapon argues with.
    // Authored Blunt everywhere by the mechanical translation; claws and
    // fangs pick their columns at content stage.
    DamageType   dmgType = DamageType::Blunt;
};

struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle
    // WHAT this member is, in the ONE id space every body already shares with
    // the ECS (`ecs::NPCKind.type`): an ordinal of the one npc table — a wolf
    // is as legal a row as a spearman, so a wolf pack IS a squad, and the byte
    // this used to be could not say so (CANON.md S4/S16). The old
    // `0x100 | catalog index` monster encoding is dead with the second table
    // (npc.h). Sixteen bits, validated by npc.h `valid_npc_kind`.
    std::uint16_t kind     = 0;
    std::int16_t  level    = 1;
};

// ── THE roster: flat, fixed, capped ────────────────────────────────────────
// Owner's number, 2026-08-27: a squad holds up to 1024 members. It is a plain
// array with a count, not a vector — one squad component was 24 bytes of
// heap-pointing header on EVERY macro entity (all 16384 of them, and the
// common case is EMPTY), with a non-trivial destructor in the ECS pools and a
// deep copy on every snapshot. A member is 8 bytes; the flat form costs
// 8 KiB per squad and buys zero allocations, a memcpy-able component and a
// save block that is a straight byte run.
//
// Overflow is LOUD, never silent (CANON S26): `push` returns false and the
// caller decides. Nothing truncates behind anyone's back.
inline constexpr int kMaxSquadMembers = 1024;

struct SoldierSquad {
    std::array<SoldierRecord, kMaxSquadMembers> members{};
    std::int32_t count = 0;

    int size() const { return int(count); }
    bool empty() const { return count == 0; }
    bool full() const { return count >= kMaxSquadMembers; }
    SoldierRecord* begin() { return members.data(); }
    SoldierRecord* end() { return members.data() + count; }
    const SoldierRecord* begin() const { return members.data(); }
    const SoldierRecord* end() const { return members.data() + count; }
    SoldierRecord& operator[](int i) { return members[std::size_t(i)]; }
    const SoldierRecord& operator[](int i) const {
        return members[std::size_t(i)];
    }

    // Returns false when the squad is full — the caller must decide what a
    // refused recruit means; it is never dropped quietly.
    bool push(const SoldierRecord& s) {
        if (full()) return false;
        members[std::size_t(count++)] = s;
        return true;
    }

    void clear() { count = 0; }

    // Order is not meaningful in a roster, so removal is a swap with the last
    // live slot — O(1) instead of the vector's O(n) shift.
    bool remove_at(int i) {
        if (i < 0 || i >= count) return false;
        members[std::size_t(i)] = members[std::size_t(count - 1)];
        --count;
        return true;
    }
};

inline SoldierSquad default_squad() { return {}; }

inline int normalize_soldier_level(int level) {
    if (level < 1) return 1;
    if (level > kMaxSoldierLevel) return kMaxSoldierLevel;
    return level;
}

inline SoldierRecord make_soldier(std::uint16_t kind, int level,
                                  std::uint32_t entityId) {
    SoldierRecord s{};
    s.entityId = entityId;
    s.kind = kind;
    s.level = std::int16_t(normalize_soldier_level(level));
    return s;
}

inline int total_soldiers(const SoldierSquad& squad) { return squad.size(); }

inline int count_soldiers_of_kind(const SoldierSquad& squad, std::uint16_t kind) {
    int n = 0;
    for (const auto& s : squad) {
        if (s.kind == kind) ++n;
    }
    return n;
}

inline int count_soldiers_with_entity_id(const SoldierSquad& squad,
                                         std::uint32_t entityId) {
    int n = 0;
    for (const auto& s : squad) {
        if (s.entityId == entityId) ++n;
    }
    return n;
}

inline bool remove_one_soldier_by_entity_id(SoldierSquad& squad,
                                            std::uint32_t entityId) {
    for (int i = 0; i < squad.size(); ++i) {
        if (squad[i].entityId == entityId) return squad.remove_at(i);
    }
    return false;
}

inline int soldier_level_factor(int level) {
    const int safeLevel = normalize_soldier_level(level);
    return 1 + (safeLevel - 1) / 3;
}

// Append one roster onto another, stopping at the ceiling. Returns how many
// were actually taken, so a caller that must not lose men can see it did.
inline int add_squad(SoldierSquad& target, const SoldierSquad& src) {
    const int n = src.size();          // read first: src may BE target
    int taken = 0;
    for (int i = 0; i < n; ++i) {
        if (!target.push(src[i])) break;
        ++taken;
    }
    return taken;
}

} // namespace sm
