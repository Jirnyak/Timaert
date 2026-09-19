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

    // ── Filled by project_combat, never authored (a row has no sheet) ──────
    // The sheet's attribute ADD to every roll of the dice above (STR-derived
    // for melee rows, INT-derived for missile ones), floored to the int house.
    std::int16_t flatAdd = 0;
    // The sheet's LCK — the crit door's ask, once per strike (core/dice.h).
    std::uint8_t luck = 0;
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

// ── THE roster: an INVENTORY OF CREATURES (CANON S4, owner 2026-09-19) ─────
// «существа должны стаковаться, потому что они же из таблицы мобов/НПЦ» —
// the roster obeys the ITEM slot law: identical stacks, storied souls take a
// slot of their own. A slot is the record every reader iterates; a SOUL is
// what the subworld embodies and the ledger settles, and it is reached only
// through the explicit souls() door below.
//
//   entityId == 0 → a GENERIC stack: mass fighters of one kind and level,
//                   merged on push, `count` up to int32.
//   entityId != 0 → a SOUL WITH HISTORY (leader / named / possessed):
//                   count == 1, never merged.
//
// 256 slots — the roster MIRRORS the 256-stack item inventory (same form,
// not a similar one). The old form was 1024 × 8 B = 8 KiB and a HARD CAP of
// 1024 souls per squad; this one is 256 × 12 B = 3 KiB and the soul count is
// bounded by int32 — thousand-strong armies stop being impossible. Removing
// that project cap is the point; the memory is a side effect.
//
// Overflow is LOUD, never silent (CANON S26): `push` returns false and the
// caller decides. Nothing truncates behind anyone's back.
struct SoldierSlot {
    std::uint16_t kind     = 0;
    std::int16_t  level    = 1;
    std::int32_t  count    = 0;
    std::uint32_t entityId = 0;
};
static_assert(sizeof(SoldierSlot) == 12, "the slot is the canon's 12 bytes");

inline constexpr int kMaxSquadSlots = 256;

// One soul as seen through the souls() door: the record's fields plus WHERE
// it stands — (slot, index in the stack) is the address a face or a body
// loan derives from once generics have no entityId to key on.
struct SoulRef {
    std::uint16_t kind     = 0;
    std::int16_t  level    = 1;
    std::uint32_t entityId = 0;
    std::int32_t  slot     = 0;
    std::int32_t  index    = 0;   // [0, count) within the stack
};

class SoulIterator {
  public:
    SoulIterator(const SoldierSlot* slots, std::int32_t slot)
        : slots_(slots), slot_(slot) {}
    SoulRef operator*() const {
        const SoldierSlot& s = slots_[slot_];
        return SoulRef{s.kind, s.level, s.entityId, slot_, index_};
    }
    SoulIterator& operator++() {
        if (++index_ >= slots_[slot_].count) {
            index_ = 0;
            ++slot_;
        }
        return *this;
    }
    bool operator!=(const SoulIterator& o) const {
        return slot_ != o.slot_ || index_ != o.index_;
    }
  private:
    const SoldierSlot* slots_;
    std::int32_t       slot_  = 0;
    std::int32_t       index_ = 0;
};

struct SoulsRange {
    const SoldierSlot* slots = nullptr;
    std::int32_t       slotCount = 0;
    SoulIterator begin() const { return SoulIterator(slots, 0); }
    SoulIterator end() const { return SoulIterator(slots, slotCount); }
};

struct SoldierSquad {
    std::array<SoldierSlot, kMaxSquadSlots> slots{};
    std::int32_t slotCount = 0;

    // Iteration IS the slot walk — the inventory mirror. Counting readers
    // multiply by `count`; per-soul work goes through souls().
    SoldierSlot* begin() { return slots.data(); }
    SoldierSlot* end() { return slots.data() + slotCount; }
    const SoldierSlot* begin() const { return slots.data(); }
    const SoldierSlot* end() const { return slots.data() + slotCount; }
    SoldierSlot& operator[](int slot) { return slots[std::size_t(slot)]; }
    const SoldierSlot& operator[](int slot) const {
        return slots[std::size_t(slot)];
    }
    int slot_count() const { return int(slotCount); }

    // SOULS, derived — never a second cached truth beside the slots (the
    // hand-written fold-up law: a cached total silently drops a field).
    int size() const {
        int n = 0;
        for (int i = 0; i < slotCount; ++i) n += int(slots[std::size_t(i)].count);
        return n;
    }
    bool empty() const { return slotCount == 0; }
    // No FREE slot. A generic push into an existing stack still succeeds on
    // a full squad — full() gates only what would need a new slot.
    bool full() const { return slotCount >= kMaxSquadSlots; }

    // The explicit per-soul door: expands stacks, yields (slot, index) with
    // each soul so the reader has the address, not just the fields.
    SoulsRange souls() const { return SoulsRange{slots.data(), slotCount}; }

    // ── writers: every one keeps the invariants «no empty slot» and
    //    «a storied soul's count is exactly 1» ──────────────────────────────

    // One GENERIC stack in: merged into its {kind, level} twin when one
    // exists, a new slot otherwise. Refuses loudly on a full squad or an
    // int32 overflow of the stack.
    bool push_stack(std::uint16_t kind, std::int16_t level, std::int32_t n) {
        if (n <= 0) return false;
        level = std::int16_t(normalize_soldier_level(level));
        for (int i = 0; i < slotCount; ++i) {
            SoldierSlot& s = slots[std::size_t(i)];
            if (s.entityId != 0 || s.kind != kind || s.level != level)
                continue;
            if (s.count > std::numeric_limits<std::int32_t>::max() - n)
                return false;   // an int32 army is the one cap left
            s.count += n;
            return true;
        }
        if (full()) return false;
        slots[std::size_t(slotCount++)] = SoldierSlot{kind, level, n, 0};
        return true;
    }

    // One SOUL in — the record is the coin every transfer (hire, walker,
    // save row) pays with. entityId == 0 routes through the stack law.
    bool push(const SoldierRecord& s) {
        if (s.entityId == 0) return push_stack(s.kind, s.level, 1);
        if (full()) return false;
        slots[std::size_t(slotCount++)] = SoldierSlot{
            s.kind, std::int16_t(normalize_soldier_level(s.level)), 1,
            s.entityId};
        return true;
    }

    // A whole slot in (roster folds into garrison, pool absorbs a band):
    // generic merges, storied takes its own slot. All-or-nothing per slot.
    bool push_slot(const SoldierSlot& s) {
        if (s.count <= 0) return false;
        if (s.entityId == 0) return push_stack(s.kind, s.level, s.count);
        if (full()) return false;
        slots[std::size_t(slotCount++)] = SoldierSlot{
            s.kind, std::int16_t(normalize_soldier_level(s.level)), 1,
            s.entityId};
        return true;
    }

    void clear() { slotCount = 0; }

    // Order is not meaningful in a roster, so removal is a swap with the last
    // live slot — O(1) instead of a shift.
    bool remove_slot_at(int slot) {
        if (slot < 0 || slot >= slotCount) return false;
        slots[std::size_t(slot)] = slots[std::size_t(slotCount - 1)];
        --slotCount;
        return true;
    }

    // n souls off a slot; the slot dies with its last soul (no empty slots).
    bool remove_from_slot(int slot, std::int32_t n) {
        if (slot < 0 || slot >= slotCount || n <= 0) return false;
        SoldierSlot& s = slots[std::size_t(slot)];
        if (n >= s.count) return remove_slot_at(slot);
        s.count -= n;
        return true;
    }

    // One soul OUT of a named slot, as the transfer coin: a storied soul
    // leaves with its entityId, a generic leaves as {kind, level, 0}.
    bool take_soul_at(int slot, SoldierRecord& out) {
        if (slot < 0 || slot >= slotCount) return false;
        const SoldierSlot& s = slots[std::size_t(slot)];
        out = SoldierRecord{s.entityId, s.kind, s.level};
        return remove_from_slot(slot, 1);
    }

    // One soul off the BACK — the walker law (desertion, patrol packets):
    // the freshest arrivals stand in the last slot and leave first, exactly
    // the order the per-soul array kept.
    bool pop_soul_back(SoldierRecord& out) {
        return take_soul_at(slotCount - 1, out);
    }
};

inline SoldierSquad default_squad() { return {}; }

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
    for (const SoldierSlot& s : squad) {
        if (s.kind == kind) n += int(s.count);
    }
    return n;
}

inline int count_soldiers_with_entity_id(const SoldierSquad& squad,
                                         std::uint32_t entityId) {
    int n = 0;
    for (const SoldierSlot& s : squad) {
        if (s.entityId == entityId) n += int(s.count);
    }
    return n;
}

// The ledger's by-name strike. A storied soul is found by its entityId; a
// generic death cannot arrive here by construction until auto-battle learns
// slots (increment 5) — generics have no name to be settled under.
inline bool remove_one_soldier_by_entity_id(SoldierSquad& squad,
                                            std::uint32_t entityId) {
    if (entityId == 0) return false;   // a nameless strike names nobody
    for (int i = 0; i < squad.slot_count(); ++i) {
        if (squad[i].entityId == entityId)
            return squad.remove_from_slot(i, 1);
    }
    return false;
}

// THE settle strike (one door for the ledger and the battle): a NAMED death
// removes the very soul that fell; a GENERIC death removes one soul of the
// same kind and level — generics are interchangeable by construction, that
// is what a stack MEANS.
inline bool remove_one_soldier(SoldierSquad& squad, const SoldierRecord& r) {
    if (r.entityId != 0)
        return remove_one_soldier_by_entity_id(squad, r.entityId);
    for (int i = 0; i < squad.slot_count(); ++i) {
        const SoldierSlot& s = squad[i];
        if (s.entityId == 0 && s.kind == r.kind && s.level == r.level)
            return squad.remove_from_slot(i, 1);
    }
    return false;
}

inline int soldier_level_factor(int level) {
    const int safeLevel = normalize_soldier_level(level);
    return 1 + (safeLevel - 1) / 3;
}

// Append one roster onto another, slot-wise, stopping at the ceiling.
// Returns how many SOULS were actually taken, so a caller that must not
// lose men can see it did. `src` is untouched — the rollback shape.
inline int add_squad(SoldierSquad& target, const SoldierSquad& src) {
    const int n = src.slot_count();    // read first: src may BE target
    int taken = 0;
    for (int i = 0; i < n; ++i) {
        const SoldierSlot s = src[i];  // copy: push may grow target == src
        if (!target.push_slot(s)) break;
        taken += int(s.count);
    }
    return taken;
}

// MOVE souls between rosters, slot-wise, as many as the destination takes:
// a moved slot leaves `src`, a refused one STAYS — nobody is destroyed for
// standing past a cap (CANON S26). Returns souls moved.
inline int move_squad(SoldierSquad& dst, SoldierSquad& src) {
    int moved = 0;
    for (int i = 0; i < src.slot_count();) {
        const SoldierSlot s = src[i];
        if (!dst.push_slot(s)) {
            ++i;                       // stays; try the next slot
            continue;
        }
        moved += int(s.count);
        src.remove_slot_at(i);         // swap-with-last: same i is a new slot
    }
    return moved;
}

} // namespace sm
