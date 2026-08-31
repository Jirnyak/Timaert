// ECS components — small POD structs as per AGENTS.md.
#pragma once
#include "macro/army.h"
#include "macro/items.h"
#include "macro/anatomy.h"
#include <array>
#include <cstdint>
#include <string>
#include <entt/entt.hpp>

namespace sm::ecs {

// World-space position: x,y are tile coords; z is absolute world-space altitude
// in metres (same coordinate system as cam_.pos.y). Ground entities get z set to
// sample_height_m(x,y) by the per-tick ground-follow system. Flying entities and
// projectiles own their z through movement/velocity. Water surface sits at
// sub::kSeaLevelM ≈ 600 m — the whole vertical model is sub/height.h.
struct Position { float x, y, z; };

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
    float cooldown;         // authored seconds (the table's own number)
    // Steps left before this body may strike again (core/time.h). It was a
    // float decremented by dt — a rate quoted in REAL seconds inside a world
    // that runs on an integer tick, and one of three such exceptions where the
    // ladder claimed exactly one. Integer now, and a blow lands after the same
    // amount of FIGHT whether the clock above races or crawls.
    std::uint32_t cooldownSteps;
    enum Kind : std::uint8_t { Melee = 0, Missile = 1 } kind;
};

// Extra projectile data for Combat::Missile attackers.
struct MissileAttack {
    float speed;
    float blastRadius;
    std::uint32_t colorRGBA;
};

// Tag components. (An `Active` tag lived here with THIRTEEN emplace sites and
// zero readers — no view, no any_of, nothing ever asked. Deleted 2026-08-05:
// a tag nobody reads is not a state, it is noise every spawn site paid for.)
struct Dead {};
struct PlayerTag {};
// The flag above answers «кем я управляю СЕЙЧАС» and MOVES: possession puts it
// on a lord, a subworld session puts it on a body. This one answers a
// different question — «чей это отряд» — and never moves off the player's own
// macro squad. Two questions, two tags: a loop that means "another party"
// excludes THIS one, because excluding PlayerTag stops working the moment the
// player possesses someone or steps into a subworld, and his own squad becomes
// prey / a trade partner / an AI walker to every scan on the map.
struct PlayerSquadTag {};
struct PlayerSoldierTag {};
struct TempHostileToPlayer {};
// Marks an entity that lives only in the current subworld scene; cleared
// on enter/leave so we never destroy persistent macro NPCs by accident.
struct SubworldTag {};
struct Flying {};
// Lazy vertical state (sub/height.h vertical_step): emplaced the moment a
// non-flying body leaves its support surface (walked off a battlement, lost
// flight, a future jump), removed on landing. Grounded bodies — the thousands
// of common cases — carry nothing and cost nothing.
struct Airborne { float vz = 0.0f; };

// NPC link to type registry (NPCType enum value).
struct NPCKind { std::uint16_t type; std::uint16_t factionIdx; };

// Subworld behaviour state (mirrors `subworld/ai.ts`). Only attached to
// SubworldTag entities; engine dispatches Wander/Flee/Combat by `kind`.
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
    float wantVx = 0.0f, wantVy = 0.0f;
    // Decision counter, folded into the wander RNG seed. The seed used to be
    // entity-bits ⊕ position — safe while the brain moved its own body, fatal
    // once it stopped: a wanderer that rolled "stand" froze its position, and
    // a frozen position re-rolled the SAME "stand" forever. The counter
    // advances per decision, so a standing mind still changes its mind.
    std::uint32_t seq = 0;
};

// Per-NPC level (matches TS `npc.level`). Drives loot tables, combat
// scaling, and visual badges in the proximity panel.
struct NpcLevel { std::int16_t value; };

// Per-NPC inventory: what this body is CARRYING. Only a body whose belongings
// are state has one — a macro entity and the tracked body that embodies it
// (sub/spawn.h), or a body a quest handed a specific thing. A derived body has
// none on purpose: its loot is rolled from its row through the one registry at
// the moment it dies (macro/items.h roll_loot_profile), so a city of five
// thousand people costs five thousand integers and no bags.
struct NpcInventory { Inventory inv; };

// What this body is WEARING — opt-in, and deliberately rare. A creature's own
// defence is a number on its row (macro/npc.h `NpcTypeDef::armor`, the owner's
// «броня массовки = число из строки»), because a troll's hide is what a troll
// IS; only a body that actually owns things — the player, a named lord —
// carries this. So the sixteen thousand macro squads pay nothing for a system
// they do not use, and the ones that do use it get the whole of it.
struct BodyEquipment { Equipment gear; };

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
    // The same 16-bit kind a roster record carries (macro/army.h): a humanoid
    // ordinal below 0x100, a monster catalog row at or above it. A beast in a
    // squad is an ordinary member, so its link has to be able to say which
    // beast.
    std::uint16_t kind;
    std::int16_t  level;
};

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
    std::int32_t  subject;        // landmark id (ONE space, v54), -1 = the cell
    std::int16_t  cellX;
    std::int16_t  cellY;
    std::uint16_t amount;         // how much of the stock this one thing is
    // Which row WITHIN the subject this thing stands for, when the stock is a
    // TABLE rather than a count: the roster row stores the member's
    // SoldierRecord::entityId here (as a bit pattern — ids may use the high
    // bit), so a death removes the very soldier who fell, not "one of them".
    // -1 = the stock is anonymous (population, trees) and needs no name.
    std::int32_t  detail = -1;
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

// One-shot "a hit just landed on this body" marker (transient VFX seam). Every
// damage site that already stamps HitFlash also stamps this; ONE engine pass
// (tick_damage_fx) drains it the same tick and turns it into a blood/dust burst
// at the body's position, classifying the spray from the victim's own sprite
// archetype — so no damage site needs to know about particles (the spell TU
// stays renderer-free) and there is no per-creature hardcoding. `lethal` lets
// the drain throw a bigger burst on the killing blow. Removed as soon as it is
// consumed, so it never lingers on a surviving body.
struct DamageFx { bool lethal; };

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

// Sprite (atlas index + tint). `spriteRow` is this body's row in THE sprite
// table (macro/sprite_rows.h) held as a raw ordinal, because the ECS layer may
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
struct Sprite { std::uint16_t atlasId; std::uint8_t r, g, b, a; float scale;
                std::uint8_t spriteRow = 0; float height = 0.0f; };

// Positional point-light emitter (graphics only). Any subworld entity carrying
// one casts a point light: the 3D renderer gathers view<Position, LightEmitter,
// SubworldTag> each frame and packs it into the set-0/binding-1 light SSBO that
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

// Macroworld NPC runtime — per-NPC mutable state for the AI tick
// (mirrors fields on TS `NPC` not already covered by Position / NPCKind).
// Pure POD, ~36 bytes. The home/target landmark ids draw on the ONE landmark
// id space (GameState::nextLandmarkOrdinal, v54) — a home may be a city or a
// village, and the id alone says which place it is (-1 = none). The v27
// homeIsVillage register bit died with the second id space it existed to
// disambiguate.
struct MacroNpcRuntime {
    std::int32_t  homeSettlementId;
    std::int32_t  targetSettlementId;
    // The trading run (owner 2026-08-30: caravans walk CITY to CITY, station
    // by station, deciding from the market they stand in — no omniscience).
    // stationsLeft counts the stops before the run turns home; prevStationId
    // keeps the leg from ping-ponging between two neighbours. Runtime-only.
    std::uint8_t  stationsLeft = 0;
    std::int32_t  prevStationId = -1;
    // The squad's MAINTENANCE counter, hunger half (owner 2026-08-30, ?34:
    // one counter for pay and bread): days the crew went short of its
    // bread. Runtime-only.
    std::uint8_t  hungerDays = 0;
    // The tithe riding in the bag toward the suzerain (the feudal tax graph,
    // CANON S24): set at departure, paid out at the market. Runtime-only.
    std::int32_t  taxCarried = 0;
    float         targetX, targetY;
    std::int16_t  stateTimer;
    std::int16_t  teleportCooldown;
    std::int16_t  sp;            // stamina; may go NEGATIVE — the exhaustion
                                 // debt the player's bar also carries
    // The leader's sheet, cached as the four scalars the macro march actually
    // reads (Session 21). The full CharacterSheet is derived, never stored:
    // make_npc and the level-up recompute both re-derive it from
    // leader_sheet_seed(MacroSpawnId) and refresh these through ONE helper
    // (squad.h refresh_leader_travel_stats), so the cache cannot drift from
    // the sheet law. maxSp: the END bar (calculate_combat_stats), the ceiling
    // regen fills and auto-battle fatigue divides by. travelRank/marathonRank:
    // the two SP skills (cost discount / recovery rate). moveMult: spd ×
    // athletics, the sheet's own pace.
    std::int16_t  maxSp = 100;
    std::uint8_t  travelRank = 0;
    std::uint8_t  marathonRank = 0;
    float         moveMult = 1.0f;
    // What this leader's back can hold (attributes.h get_carry_capacity), the
    // fifth scalar of the same sheet cache — and the SP surcharge his current
    // load is costing him, recomputed each think from his bag because a bag
    // changes at every market while a sheet does not. Owner's ruling
    // 2026-08-27: «перегруз универсальный всем» — a laden squad marches like a
    // laden man, and the pack a caravan carries is finally part of the price
    // of carrying it.
    float         carryCap = 0.0f;
    std::int16_t  overloadCost = 0;
    // Fractional SP carry (both directions: march costs and rest regen), the
    // THE fractional carry (movement_cost.h settle_sp_carry), signed and
    // bidirectional, and the player keeps his HERE too — this is his squad's
    // runtime like any lord's.
    // Runtime-only like the rest of this struct.
    float         spCarry = 0.0f;
    // Fractional CELLS banked toward the next whole step: the march is quoted
    // in cells per game hour (kMacroWalkCellsPerHour) but a think is discrete,
    // so slow ground (water at a third of road pace) banks part-cells across
    // thinks instead of rounding them away.
    float         moveBudget = 0.0f;
    std::uint8_t  state;         // NPCState
    // Entry-side context (macro/entry_context.h): the packed signed step of the
    // last cell change (0xFF = none: spawned here / teleported), and a
    // saturating count of AI ticks since it. Stamped by try_move, consumed by
    // the macro→subworld projection to place this NPC's body on the side of the
    // map it actually walked in from. Runtime-only — the ECS is never
    // serialized, so no save cost.
    std::uint8_t  entryDir  = 0xFF;
    std::uint8_t  entryTicks = 0;
    float         visualSpeed;   // last-tick travelled distance, cells/real s
    std::uint32_t tickAccum;     // WORLD ticks accumulated toward the next think
    // Experience toward the leader's next level (Session 15): auto-battle
    // victories pay XP through the one npc_xp_reward law, and
    // award_leader_xp (macro/squad.h) consumes it into NpcLevel by the same
    // exp_to_next_level curve the player climbs. Runtime-only like the rest
    // of this struct — persistent leader progression is the S17 snapshot's.
    std::int32_t  xp = 0;
    // WHAT THE WORLD THINKS THIS BAND HAS DONE (macro/chronicle.h). A squad
    // starts as one more band on the map: its deeds are weather, remembered by
    // the ring for a season and then forgotten. Do enough, and it becomes a
    // FIGURE — from that day its deeds go into the annals for good (owner,
    // 2026-08-27: «безымянный сквад может стать именным, как лорд, если
    // накопится деяний»).
    //
    // ONE number, not a counter plus a flag: "is it named" is DERIVED
    // (`renown >= kRenownToBeNamed`), so the two can never disagree about the
    // same band. Persistent — a figure stays a figure across a save, because
    // the runtime rides the macro snapshot.
    //
    // UNSIGNED, and the reason is the value and not a habit: renown is a
    // COUNT of what a band has done, it is never negative, and it is not an
    // index nor a term of a subtraction — so the real hazards of unsigned
    // arithmetic (a reverse loop that never ends, `a - b` wrapping) do not
    // touch it. Where a field CAN go below zero the type says so, and the
    // fact record next door proves the rule is applied and not recited:
    // `WorldFact::amount` is signed because reputation and coin move both
    // ways, and `day` is signed because -1 means "no day yet".
    std::uint32_t renown = 0;
};

// Deterministic spawn ordinal for a persistent macro NPC (Inc 5e-2), assigned
// in creation order (0,1,2,…) by the SOLE creation path (make_npc) — the one
// identity that survives save/load. Possession persistence stores the
// possessed body's ordinal (PlayerState::possessedMacroSpawnId) and re-finds
// the same NPC after a load (reattach_player_to_macro_spawn).
//
// AND IT IS SAVE FORMAT. Since v23 the macro-ECS snapshot serializes every
// persistent macro NPC whole (macro/macro_snapshot.h → save.cpp
// write_macro_npc): this ordinal rides the blob, and the POD components
// around it — MacroNpcRuntime included — are written verbatim, so their
// LAYOUT is the format. A "runtime-only" field added to any of them is not
// free: it changes the blob and pays a kSaveVersion bump like Skills does.
struct MacroSpawnId { std::uint32_t index = 0; };

// The PLAYER's squad ordinal — reserved at the top of the space so it can
// never collide with the 0,1,2… the world spawner hands out. His squad is an
// ordinary macro squad (owner, 2026-08-27) and therefore needs an ordinal like
// any other: it is what the roster ledger keys casualties by, and what finds
// him again after a load regenerates the world.
// NOT 0xFFFFFFFF: the stock ledger carries a subject as a SIGNED int32, where
// -1 already means "the cell itself" (MacroDebt above) — the all-ones ordinal
// would have collided with that meaning and silently lost the player's
// casualties. INT32_MAX is as far from the spawner's 0,1,2… as an ordinal can
// be while staying a positive number.
inline constexpr std::uint32_t kPlayerSquadOrdinal = 0x7FFFFFFFu;

// THE macro entity is a SQUAD, not a person (owner's design, macrosim.md
// "Squad as THE macro entity"). This component is that ruling made structural:
// every macro NPC carries one, and `members` holds everyone EXCEPT the leader
// — the leader IS the carrying entity, with its sheet, inventory, wounds and
// MacroSpawnId exactly as before. An empty roster is not a special case, it is
// the common one: a peasant on the road is a squad of one, its own leader.
// The player is the same shape — his entity is the leader, PlayerState::army
// is his roster — so possession of a leader is acquisition of its squad by
// construction, not by code.
//
// Members are procedural rows (macro/army.h SoldierRecord), embodied through
// the one body birth as DERIVED bodies; only the leader is TRACKED. The roster
// is a macro STOCK (macro/macro_stock.h): members embodied below are borrowed
// from it and their deaths are paid back up, and a squad whose members are
// gone and whose leader is dead is gone from the map by that general rule.
// Runtime-only until the macro snapshot save (Session 17) — the ECS is never
// serialized, so no kSaveVersion cost today.
struct SquadRoster {
    // Flat and capped (macro/army.h SoldierSquad, 1024 members): a memcpy-able
    // component with no heap header on every one of the 16384 macro entities,
    // and the SAME type the player's own squad, a garrison and the deserter
    // pool are — one roster shape for the whole world.
    SoldierSquad squad{};
};

// A waypoint route a squad was ORDERED onto (Session 15, Inc 7) — OPT-IN,
// and the route's presence IS the order (owner's ruling: no second knob):
// a squad with waypoints walks them in a loop instead of its type row's ai
// — a guard in the player's patrol walks the player's route, not his
// hometown beat — and a squad without this component (or with an empty
// route) never changed. New KINDS of squad AI are rows (NpcTypeDef.ai +
// a behaviour function), never fields here. Flat po2 array of macro
// cells: 8 × (x, y). Runtime-only until the S17 snapshot.
struct SquadOrders {
    std::uint8_t waypointCount = 0;
    std::uint8_t currentWaypoint = 0;
    std::array<std::int16_t, 16> waypoints{};   // 8 × (x, y) macro cells
};

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
    float vx, vy, vz;
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
