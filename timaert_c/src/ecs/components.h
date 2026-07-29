// ECS components — small POD structs as per AGENTS.md.
#pragma once
#include "macro/items.h"
#include <cstdint>
#include <string>
#include <entt/entt.hpp>

namespace sm::ecs {

// World-space position on macroworld (cell coords + sub-cell visual offset).
struct Position { float x, y; };

// Smoothed render position (for visual interpolation).
struct VisualPos { float vx, vy, speed; };

// Health component.
struct Health { float hp, maxHp; };

// Explicit combat body radius — the distance at which this entity is struck by
// melee, projectiles, and blasts (see the sub-layer target_radius()). It is the
// universal, preferred source for that radius; the combat code falls back to
// SubworldAi.radius, then Sprite.scale, then a coarse default only when this is
// absent. Carried by any entity that is neither an AI mover nor a billboard yet
// must still present a sane hit size — notably the player, who is the camera
// (no Sprite) and is input-driven (no SubworldAi). Nothing here is
// player-specific: it is a plain spatial property any actor may hold.
struct BodyRadius { float radius; };

// Combat stats — universal stat block (matches CombatTemplate in TS).
struct Combat {
    float damage;
    float speed;            // grid units / s
    float attackRange;
    float cooldown;
    float cooldownTimer;
    enum Kind : std::uint8_t { Melee = 0, Missile = 1 } kind;
};

// Extra projectile data for Combat::Missile attackers.
struct MissileAttack {
    float speed;
    float blastRadius;
    std::uint32_t colorRGBA;
};

// Tag components.
struct Active {};
struct Dead {};
struct PlayerTag {};
struct PlayerSoldierTag {};
struct TempHostileToPlayer {};
// Marks an entity that lives only in the current subworld scene; cleared
// on enter/leave so we never destroy persistent macro NPCs by accident.
struct SubworldTag {};

// NPC link to type registry (NPCType enum value).
struct NPCKind { std::uint16_t type; std::uint16_t factionIdx; };

// Subworld behaviour state (mirrors `subworld/ai.ts`). Only attached to
// SubworldTag entities; engine dispatches Wander/Flee/Combat by `kind`.
// `vx/vy` are the current velocity (units / s); `aiTimer` counts down to
// the next direction change for wander/flee; `wanderSpeed` is the slow
// idle pace (Combat uses Combat::speed instead).
struct SubworldAi {
    enum Kind : std::uint8_t { Wander = 0, Flee = 1, Combat = 2 };
    Kind  kind;
    float aiTimer;
    float vx, vy;
    float wanderSpeed;
    float radius;
};

// Per-NPC level (matches TS `npc.level`). Drives loot tables, combat
// scaling, and visual badges in the proximity panel.
struct NpcLevel { std::int16_t value; };

// Per-NPC inventory (loot pool). Populated at spawn from
// `generate_npc_inventory(type, level, rng)` so kills produce the
// right items deterministically per seed.
struct NpcInventory { Inventory inv; };

// Per-NPC personality traits. TS assigns 1-2 unique traits from the
// `NPCTrait` registry; store raw enum ids here to keep ECS free of a
// hard dependency on the macro registry header.
struct NpcTraits {
    std::uint8_t count = 0;
    std::uint8_t traits[2]{};
};

// Link from a subworld entity back to the macro soldier record. The macro
// roster remains authoritative; the ECS entity is a session projection.
struct SoldierLink {
    std::uint32_t entityId;
    std::uint8_t  kind;
    std::int16_t  level;
};

// Backlink from a PROJECTED subworld body to the persistent macro NPC entity it
// mirrors (Inc 5d). A macro NPC standing in (or beside) the entered cell is
// projected into the 3×3 window as a full combat body so the player can meet —
// and, via possession, take over — the very lords/bandits/peasants that roam the
// overworld. The macro entity stays authoritative and untouched for the whole
// session (the macro tick is frozen while a subworld is active); this handle is
// how leave() (Inc 5e) maps the flagged body back to a macro cell on exit, and
// how the reaper (clear_subworld_world_entities) knows to leave projections be.
// Runtime-only: never serialized, so it does not bump kSaveVersion.
struct MacroOrigin { entt::entity macro; };

// Last damaging owner. Used for player/squad XP attribution in normal
// subworld combat.
struct LastHit {
    std::uint32_t attackerId;
    bool          playerOwned;
};

// Short-lived red damage flash for subworld actors. Mirrors TS `hitTimer`.
struct HitFlash { float timer; };

// Per-NPC visual identity. POD reinterpretation of TS `CharacterData`
// (which is HTML-canvas-targeted: name + sprite-layer indices + palette
// state). For C++ we keep only the gameplay-visible variation needed
// for the macro overlay figures: a deterministic procedural seed that
// drives sprite-variant + body shape, plus an accessory tint and a
// stable display-name index into the per-type names[16] pool.
struct NpcCharacter {
    std::uint32_t visualSeed;   // drives procedural sprite variation
    std::uint8_t  bodyShape;    // 0..3 (small / med / large / huge)
    std::uint8_t  nameIdx;      // index into NpcTypeDef::names[16]
    std::uint8_t  tintR;
    std::uint8_t  tintG;
    std::uint8_t  tintB;
    std::uint8_t  pad0, pad1, pad2;
};

// Sprite (atlas index + tint). `archetype` selects the procedural creature
// body plan for the subworld 3D billboard pass (see shaders/creature_sprite.glsl
// and sm::sub::CreatureArchetype). 0xFF = "not a procedural creature": town
// NPCs (paper-doll), spell projectiles and engine sprites keep the default and
// are rendered by their own passes / not proceduralised. This is the seed of
// the universal sprite resolver — a drawn atlas/image sprite will override the
// procedural body later without an engine change.
struct Sprite { std::uint16_t atlasId; std::uint8_t r, g, b, a; float scale;
                std::uint8_t archetype = 0xFF; };

// Macroworld NPC runtime — per-NPC mutable state for the AI tick
// (mirrors fields on TS `NPC` not already covered by Position / NPCKind).
// Pure POD, ~32 bytes. The home/target settlement ids index into
// GameState::settlements (-1 = none).
struct MacroNpcRuntime {
    std::int32_t  homeSettlementId;
    std::int32_t  targetSettlementId;
    float         targetX, targetY;
    std::int16_t  stateTimer;
    std::int16_t  teleportCooldown;
    std::int16_t  sp;            // stamina
    std::uint8_t  state;         // NPCState
    std::uint8_t  pad;
    float         visualSpeed;   // last-tick travelled distance / TICK_SEC
    float         tickAccum;     // seconds accumulated toward next 0.5s tick
};

// Deterministic spawn ordinal for a persistent macro NPC (Inc 5e-2). The ECS
// registry is never serialized — macro NPCs regenerate from `worldSeed` via
// spawn_macro_npcs on every boot, in a fixed creation order. This ordinal is
// exactly that order (0,1,2,…), assigned by the SOLE creation path (make_npc),
// so it is the one identity that survives save/load. Possession persistence
// stores the possessed body's ordinal (PlayerState::possessedMacroSpawnId) and
// re-finds the same NPC after regeneration (reattach_player_to_macro_spawn).
// It is never written to the save blob itself — only the chosen ordinal is —
// so it does not participate in the ECS-serialization ban and needs no
// kSaveVersion bump of its own.
struct MacroSpawnId { std::uint32_t index = 0; };

// Static structure (tree, rock) — for subworld.
struct Structure {
    enum Kind : std::uint8_t { Tree = 0, Rock = 1, House = 2, Wall = 3, Corpse = 4 } kind;
    float x, y;
    float radius;
    float height;
};

struct CorpseLoot {
    Inventory inv;
    int       gold;
};

// Spell projectile.
struct Projectile {
    enum Kind : std::uint8_t { Bolt = 0, Beam = 1 };
    float vx, vy;
    float radius;
    float lifeTimer;
    float maxLifeTimer;
    float damage;
    float blastRadius;
    float originX;
    float originY;
    float beamLength;
    float chainDecay;
    float chainRadius;
    std::uint32_t spellId;
    std::uint32_t ownerId;
    std::int16_t chainRemaining;
    Kind kind;
    bool friendlyFire;
    bool visualOnly;
    bool explodeOnExpiry;
};

} // namespace sm::ecs
