// THE damage door (work_vector §4, owner-approved form 2026-08-27).
//
// This is the ONLY code allowed to subtract a subworld body's hp. Before it,
// five combat sites and eight harness copies each carried their own version of
// the same protocol, and every pair disagreed somewhere: the spell path had no
// already-dead guard and stamped its components before subtracting; a spell
// that killed the player emitted NpcDeath into quest kill-tallies; a spell
// that killed a kindless body emitted nothing; gravity and the dev cheat
// skipped HitFlash while stamping DamageFx, breaking the components.h
// invariant that the two travel together. One door, one order, one guard set —
// death from any weapon is indistinguishable by protocol.
//
// What the door does, in one fixed order:
//   1. the ONE already-dead guard (a corpse takes no second blow);
//   2. mitigation — INSIDE the door, so equipment (work_vector §5) modifies
//      every weapon by construction. Today it is the identity: a creature
//      without armour is the limiting case of the law, not a branch around it;
//   3. subtract, judge lethality by the RESULT (no predictions);
//   4. stamp the protocol: LastHit when the kind attributes a killer,
//      HitFlash + DamageFx always and always together;
//   5. on the killing blow: Dead once, and NpcDeath with the ONE PlayerTag
//      guard — a dead player is a game-over, never an NPC kill, whatever
//      weapon did it.
//
// The door only STRIKES. Settlement of a death — XP, loot, reputation, the
// macro writeback — stays where it always was, in the one reaper
// (SubworldEngine::resolve_subworld_deaths). Attack-side state (cooldowns,
// logs, status lines) stays with the attacker: it describes the weapon, not
// the wound.
#pragma once

#include <cstdint>
#include <entt/entt.hpp>

namespace sm { class EventBus; }

namespace sm::sub {

// One duration for the on-hit flash, whatever weapon landed it. Lived in
// spell_effects.h while melee and spells were the only stampers; the door owns
// the stamp now, so it owns the number.
inline constexpr float kHitFlashDuration = 0.15f;

// A damage KIND is a row, and the row is the whole difference between weapons.
// The one column so far: whether the blow names a killer (LastHit is what the
// reaper pays XP and reputation by). "No XP for gravity" is DATA here, not a
// skipped component at one call site.
enum class DamageKind : std::uint8_t {
    Melee = 0,
    Spell,   // spells, player missiles and NPC missiles — one projectile law
    Fall,
    Script,  // scripted/harness settlement: pays its stocks, names no killer
    Dev,     // dev cheat; attributes the player so it exercises the award path
};

struct DamageKindRow {
    const char* label;
    bool attributesKiller;
};

inline constexpr DamageKindRow kDamageKinds[] = {
    {"melee", true},
    {"spell", true},
    {"fall", false},
    {"script", false},
    {"dev", true},
};

// Who struck. attackerId is the entity bits (or a projectile's ownerId); 0 is
// the established "the player himself" convention. spellId rides into
// NpcDeath.iy so spell kills stay tellable apart downstream (0 = not a spell).
struct DamageSource {
    std::uint32_t attackerId = 0;
    bool playerOwned = false;
    std::uint32_t spellId = 0;
};

struct DamageResult {
    float applied = 0.0f;  // post-mitigation hp actually subtracted; 0 = no-op
    bool lethal = false;   // this blow drove hp to zero
};

DamageResult apply_damage(entt::registry& reg, entt::entity target,
                          const DamageSource& src, float amount,
                          DamageKind kind, EventBus* bus);

// An execution: one blow of exactly the target's remaining hp, through the
// same door. The dev cheat and the harness kill-scenarios use this instead of
// hand-writing hp = 0 with a private copy of the protocol.
DamageResult apply_lethal_damage(entt::registry& reg, entt::entity target,
                                 const DamageSource& src, DamageKind kind,
                                 EventBus* bus);

} // namespace sm::sub
