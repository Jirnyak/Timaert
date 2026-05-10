// ECS components — small POD structs as per AGENTS.md.
#pragma once
#include "macro/items.h"
#include <cstdint>
#include <string>

namespace sm::ecs {

// World-space position on macroworld (cell coords + sub-cell visual offset).
struct Position { float x, y; };

// Smoothed render position (for visual interpolation).
struct VisualPos { float vx, vy, speed; };

// Health component.
struct Health { float hp, maxHp; };

// Combat stats — universal stat block (matches CombatTemplate in TS).
struct Combat {
    float damage;
    float speed;            // grid units / s
    float attackRange;
    float cooldown;
    float cooldownTimer;
    enum Kind : std::uint8_t { Melee = 0, Missile = 1 } kind;
};

// Tag components.
struct Active {};
struct Dead {};
struct PlayerTag {};
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

// Sprite (atlas index + tint).
struct Sprite { std::uint16_t atlasId; std::uint8_t r, g, b, a; float scale; };

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

// Static structure (tree, rock) — for subworld.
struct Structure {
    enum Kind : std::uint8_t { Tree = 0, Rock = 1, House = 2, Wall = 3 } kind;
    float x, y;
    float radius;
    float height;
};

// Spell projectile.
struct Projectile {
    float vx, vy;
    float radius;
    float lifeTimer;
    float damage;
    float blastRadius;
    std::uint32_t spellId;
    std::uint32_t ownerId;
    bool friendlyFire;
};

} // namespace sm::ecs
