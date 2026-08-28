// Subworld engine — owns SeamlessSubworldManager + Vulkan 3D renderer.
// Driven by the application loop. Holds raw pointers to macroworld state
// captured at enter() time; not copied. The subworld is always first-person
// 3D; the flat 2D view is the macro map / minimap, not a subworld mode.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "ecs/world.h"
#include "macro/chronicle.h"
#include "sub/seamless_manager.h"
#include "sub/camera.h"
#include "sub/collide.h"
#include "sub/particles.h"
#include "sub/spell_effects.h"
#include "sub/battle.h"
#include "sub/vk_renderer_3d.h"
#include "events/event_bus.h"
#include "macro/macro_stock.h"

#include <vulkan/vulkan.h>

namespace gpu { struct VulkanDevice; }

namespace sm {
struct GameState;
struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;
struct TreeLayer;
namespace ecs {
struct NpcCharacter;
struct NpcInventory;
struct NpcTraits;
}
}

namespace sm::sub {

enum class DangerLevel : std::uint8_t {
    Green,
    Yellow,
    Red,
};

// What the one engine is currently simulating: the seamless 3×3 overworld
// window, or a pocket interior (dungeon) projected behind a door. One engine,
// one ECS, one renderer — the dungeon REPLACES the overworld session (owner
// ruling 2026-08-12), it never runs beside it.
enum class SceneKind : std::uint8_t { Overworld, Dungeon };

// Which threshold the player came in by — the tile they materialise on when
// the scene is raised. A storey has up to three (street door, climbing shaft,
// descending shaft) and the level alone cannot say which was used.
enum class DungeonArrival : std::uint8_t { Door, ShaftUp, ShaftDown };

// Live dungeon session bookkeeping. The dungeon is a projection OF the
// subworld: identity is {door cell, ordinal, level} (see map_data.h
// DungeonRef), and everything here is derivable again from that identity —
// nothing is saved (macro-only persistence law).
struct DungeonSession {
    DungeonRef ref{};
    DungeonArrival arrival = DungeonArrival::Door;
    int doorCx = 0, doorCy = 0;   // wrapped macro cell of the entered door
    // Where the player stood when opening the door, cell-local to the door's
    // cell — the spot the exit walks back out to.
    float returnLocalX = 0.0f, returnLocalY = 0.0f;
    float floorHeight = 0.0f;     // door cell's macroHeight = interior floor
    // Door-cell settlement context, captured at enter — WHO lives behind this
    // door and HOW MANY of them. The interior populates through the SAME
    // spawner the street uses (one context, one law), and like the street it
    // reads no strength from the place: a body is its row (CANON.md S12).
    int settlementId = -1;        // landmark id; -1 = a wilderness building
    int landmarkPop = 0;          // settlement population (household-size term)
    bool landmarkIsVillage = false;  // which register settlementId is drawn on
    std::uint16_t faction = 0;    // owning kingdom's registry faction index
};

constexpr int kCombatLogLimit = 20;
constexpr int kCombatLogMaxVisible = 5;
constexpr float kCombatLogVisibleSeconds = 4.0f;

struct CombatLogEntry {
    char text[96]{};
    float age = 0.0f;
};

// The player's melee identity — PUBLIC because it is read in two places that
// must agree (the same-game guarantee): spawn_player_entity builds the
// subworld body's Combat from it, and the macro encounter composes the
// player's auto-battle side from it (hp × (base + rawPhysDamage) per
// cooldown). One set of numbers, or the auto-resolve and the fought fight
// would price the same player differently.
constexpr float kPlayerMeleeRange      = 5.0f;
constexpr float kPlayerMeleeCooldown   = 0.5f;
constexpr float kPlayerBaseMeleeDamage = 10.0f;

// Universal player-relationship on ONE continuous, signed axis — the single
// source of truth shared by the HUD (and any future threat UI), so a marker's
// colour can never disagree with who actually fights whom:
//     -1 = maximally hostile,   0 = neutral,   +1 = maximally allied.
// It reads the exact per-faction reputation the combat / AI-threat paths use
// (gs->player.reputation, seeded in macro/state.cpp: bandits/demons -100,
// wildlife/kingdoms 0, …), anchored to the SAME semantic thresholds: at or
// below kHostileThreshold it saturates to -1, at or above kAllyRepThreshold to
// +1, and 0 reputation maps to 0. The two hard overrides the combat code
// applies are preserved: a provoked entity (TempHostileToPlayer) pins to -1
// and the player's own side (PlayerTag / PlayerSoldierTag) to +1. Because the
// relationship is numeric, the HUD renders a smooth red→yellow→green gradient
// rather than discrete buckets. Extensible by construction: retune a threshold
// or add a per-faction shade here and every consumer updates at once.
float player_stance(entt::registry& reg, entt::entity e, const GameState* gs);

// One minimap marker: world tile-space position ([0..kFullSize], the same
// space as player_x()/player_y()) plus its signed player_stance() in [-1,+1].
// POD, so the HUD renderer stays a pure presentation layer that only maps the
// stance onto its gradient.
struct MinimapBlip {
    float x = 0.0f;
    float y = 0.0f;
    float stance = 0.0f;
};

class SubworldEngine {
public:
    void init(const gpu::VulkanDevice& dev, VkRenderPass mainPass);
    void destroy(const gpu::VulkanDevice& dev);

    // The subworld reads the world through THE layer envelope (macro_stock.h
    // MacroWorld, CANON S6). Before the door (2026-08-24) this signature was
    // its own parallel list of layer arguments, grown one parameter at a time
    // — and it never grew `deposits`, so the deposit rows refused silently
    // under every subworld (canon-audit C4). Required layers: gs, terrain,
    // features, world; everything else is an optional contribution and reads
    // fail-closed. `mw.trees` feeds CellContext.treeCount so generation is
    // count-driven, and felling a tree pays the owning cell's count back
    // (micro → macro writeback via the Trees row).
    // `posOverride` (optional, window tile coords {x, y}): land the player at
    // an exact spot instead of the entry-side placement — the dungeon exit
    // walks back out to the very tile the door was opened from, BEFORE the
    // squad ring / macro projections spawn around the player.
    void enter(const MacroWorld& mw, EventBus& bus,
               const float* posOverride = nullptr);
    void leave(bool force = false);
    bool interact();
    // True while the active scene is a dungeon interior (see SceneKind).
    bool in_dungeon() const {
        return active_ && sceneKind_ == SceneKind::Dungeon;
    }
    // Storey of the active interior (0 outside one): 0 = the level the door
    // opens onto, +1 up, -1 a cellar. Read by the HUD and the smokes.
    int dungeon_level() const {
        return in_dungeon() ? int(dungeon_.ref.level) : 0;
    }
    // Window-tile position of this storey's street threshold — the tile E
    // leaves the building from. False when the storey has none (an upper
    // room, a cellar: their only ways out are their shafts), or outside an
    // interior. Read by the HUD (way-out marker) and the smokes.
    bool dungeon_exit_point(float& x, float& y) const;
    // Search a chest: hands over ONE stack from the store of the place that
    // owns this interior (a landmark's store is its Inventory — owner ruling
    // W2), charging the theft to the player's standing with that realm. An
    // empty town has empty chests; nothing is conjured, so nothing can be
    // farmed by leaving and coming back. False when there is nothing to
    // take. Public because the harness searches without a reticle.
    bool search_chest(const Structure& chest);
    // Drink at a well: pays in the ONE currency travel spends. A draught is
    // worth an hour of rest — the same fraction of the bar the rest law
    // grants per game hour (macro/attributes.h), so a well shortens a journey
    // by exactly as much as sitting down for an hour would, and no law is
    // invented to say it. False when the bar is already full.
    bool drink_from_well();
    // Read a signpost: names the place it stands in. Costs nothing, changes
    // nothing, and proves the interaction table carries flavour as cheaply as
    // it carries force.
    bool read_sign();
    // The spire orb: flips the spire's depleted flag (the macro fact), burns
    // the orb out of the scene, and emits SpireDepleted — the app layer
    // resolves the spell ordinal and teaches the book (layering: only
    // content/ knows the registry).
    bool learn_from_spire_orb(const Structure& orb);
    // What pressing E right now would do, as the verb the HUD shows under the
    // crosshair ("Enter", "Loot", …). Empty string = nothing is being looked
    // at. Pure query — the same resolution the keypress runs, so the prompt
    // can never promise an action the key will not perform.
    const char* interact_prompt() const;
    // Walk onto the shaft this storey carries and take it — the harness face
    // of the stair the player reaches with E. `up` picks the climbing (NW)
    // shaft, else the descending (NE) one. False when this storey has no such
    // shaft (or we are not in an interior at all).
    bool debug_take_stairs(bool up);
    // Fell the nearest standing tree within `maxDist` of the player — the ONE
    // harvesting path (a no-target melee swing routes here; console `chop`
    // and smokes call it with a Tree filter): the manager removes the nearest
    // LOOTABLE prop from its owning cell + composite. A felled tree also
    // decrements the owning macro cell's TreeLayer count through the
    // registry's Trees row (micro → macro writeback); other kinds settle their
    // own ledgers as they grow them. `onlyKind` narrows the search to one
    // kind. Optional outs report the owning macro cell and (for trees) its
    // count BEFORE the decrement. Returns false when nothing is in reach.
    bool harvest_prop_near_player(float maxDist, int* outCellX = nullptr,
                                  int* outCellY = nullptr,
                                  int* outPrevCount = nullptr,
                                  const Structure::Kind* onlyKind = nullptr);
    // Pay out a broken world prop through the ONE loot registry: its kind
    // names a profile (map_data.h structure_loot_id), the profile rolls items,
    // the yield scales by the prop's own metric height. Items go straight to
    // the player's pack; the return value is the player-visible list ("+3
    // Wood"), empty when the kind drops nothing. Public so the console and
    // smokes can exercise the payout without swinging an axe.
    std::string grant_prop_loot(const Structure& prop);
    // Spawn ONE npc/creature body into the live subworld — the console, the
    // encounter events, the battle harness and the macro-encounter path all come
    // through here. It does NOT make the body hostile (it never did): hostility
    // is decided by the faction it wears, like every other body in the world.
    //
    // `factionId` null/empty means "the land decides" — the body takes the realm
    // that owns the macro cell it lands on (free folk in unclaimed wilds), so a
    // caller with no owner context still produces an honest citizen of the world
    // instead of a hardcoded guess. Creatures ignore it: a monster's faction is
    // its own row in the creature table.
    //
    // This is the DERIVED form (sub/spawn.h): the body is drawn from its table
    // row and its seed and remembers nothing. `displayName` labels the status
    // line and salts the seed — it does NOT name the body, because a procedural
    // body wears a name from its row's pool; a name of one's own belongs to a
    // macro entity, and a macro entity is embodied by the call below.
    bool spawn_npc_body(const char* npcTypeId,
                        const char* displayName,
                        int level,
                        std::uint32_t seed,
                        const char* factionId = nullptr,
                        // Exactly what this body carries, when a scenario or a
                        // quest wants a specific thing looted off it. Absent, it
                        // carries nothing and its loot is rolled from its row at
                        // the moment it dies (macro/items.h, one registry).
                        const ecs::NpcInventory* inventoryOverride = nullptr,
                        // Explicit {x, y} spawn tile instead of the default
                        // ring around the player. Needed to DEPLOY a body:
                        // an army spawned in one 34-unit ring is a pile, not
                        // a battle line, so the stress/battle paths place
                        // their blocks themselves.
                        const float* positionOverride = nullptr);

    // Embody a MACRO ENTITY here — the TRACKED form (sub/spawn.h). The lord you
    // struck on the map becomes a body wearing his face, his wounds and his
    // belongings, with the `MacroOrigin` backlink that carries what happens to
    // him back up. Idempotent: if this macro entity is already standing in the
    // scene (enter() projects everyone in the 3×3 window), the existing body is
    // the answer — one entity above cannot have two bodies below.
    //
    // Before this existed, the map-attack path copied the face and the bag by
    // hand into a body that was a STRANGER to its original: killing it killed
    // nobody, and the encounter could be farmed for as long as the player had
    // patience. Returns false only if `macro` is not a body-shaped entity.
    bool spawn_tracked_npc_body(entt::entity macro);

    void tick(float dt);
    void prepare_frame(VkCommandBuffer cmd);
    // SMOKE/DIAGNOSTIC door: drain the tick's queued GPU uploads through a
    // one-shot fenced submit. A smoke that ticks the sim without rendering
    // frames must call this between ticks (the real loop drains them in
    // prepare_frame) — see Renderer3DVk::flush_uploads_blocking.
    void debug_flush_gpu_uploads();

    // Depth-only shadow casters, recorded BEFORE the main render pass.
    void record_shadow(VkCommandBuffer cmd);
    // Main-pass draws recorded inside the main render pass. `frameIndex` is the
    // renderer's currentFrame — it selects the per-frame light-SSBO ring slot in
    // the 3D renderer (see Renderer3DVk::record_main).
    void record_main(VkCommandBuffer cmd, VkExtent2D ext,
                     std::uint32_t frameIndex);

    bool active() const { return active_; }
    // Give this scene a place that means something. Generators call it; the
    // engine watches for the player crossing in.
    void add_sub_zone(float x, float y, float radius, FactKind kind,
                      std::int32_t amount = 0, bool onceEver = true);
    int sub_zone_count() const { return subZoneCount_; }

    float player_x() const { return playerX_; }
    float player_y() const { return playerY_; }
    // Terrain difficulty (macro/movement_cost.h cell_sp_weight) of the ground
    // the player is standing on — what a step HERE costs in stamina. The
    // subworld charges travel by the same law as the map (one journey, one
    // price); this is the map's own weight, resolved for the cell underfoot.
    // 0 when there is no world to ask.
    float player_ground_travel_weight() const;
    float player_z() const { return playerZ_; }
    // Where the player's shots LEAVE FROM — feet plus height.h kBodyEyeM, which
    // is the very point the camera looks from. Ask for this, never player_z(),
    // when spawning anything the player aimed: a projectile born at the feet
    // flies along a line 1.7 m under the crosshair and ploughs into the first
    // rise of ground. Kept out of line so the header owes height.h nothing.
    float player_muzzle_z() const;
    // Height of the terrain surface under a composite-window tile, in metres —
    // the same authority the vertical rule uses every tick. Exposed so a
    // harness can assert the obvious: a body that just arrived is standing ON
    // the ground, not above it.
    float ground_height_at(float x, float y) const;
    // Integral id (index+version) of the subworld player entity carrying
    // PlayerTag — stamps player-cast spell projectiles with a real owner
    // (Inc 4d), exactly as NPC missiles carry their firer's id. Returns the
    // entt::null integral when no player entity exists (never mid-cast).
    std::uint32_t player_entity_id() const;
    // Possession / вселение (Inc 5c). Take over the live body under the
    // first-person reticle: pick the nearest enemy body inside a forward cone
    // (targeting.h aim_target, using cam yaw), move the single player flag onto
    // it (possess_entity), then snap the scalar mirror to it. Body-native (D3):
    // the new body fights with its OWN Health/Combat; gs.player is preserved as
    // the revert target. Returns true if a body was possessed; a no-op false
    // outside a subworld or with nothing in the cone. The cone defaults to a
    // ~45° half-angle reticle; a test may pass cosHalfAngle=-1 for the nearest
    // body in any direction.
    bool possess_aim(float cosHalfAngle = 0.70710678f, float maxRange = 120.0f);
    // Debug possession by explicit entity id (the macro `control <id>` analogue,
    // D1). Returns false if the id is not a live positioned scene body.
    bool possess_by_id(std::uint32_t entityId);
    // Current HP of the body the player currently INHABITS, for HUD / hit-flash
    // feedback that must follow possession (Inc 5c, D3) WITHOUT mutating
    // gs.player (the frozen revert target). The flagged body always carries a
    // Health: for the hero it mirrors combatStats; for a possessed foreign body
    // it is that body's own pool. Falls back to the macro scalar only when no
    // flagged Health exists (never expected mid-subworld).
    int player_display_hp() const;
    float cam_yaw() const { return cam_.yaw; }
    float cam_pitch() const { return cam_.pitch; }
    // Diagnostic: freeze the WorldTime the RENDERER sees (sun/moon stop; the
    // simulation and real clock keep running — a real-time subworld has no
    // gameplay pause). Console command `sunfreeze`; see render_time().
    void set_sun_freeze(bool on);
    bool sun_freeze() const { return sunFreeze_; }
    // Diagnostic: `lightdbg` bisect of the sun-visibility product (see
    // Renderer3DVk::set_light_debug_mask for the bit meanings).
    void set_light_debug_mask(std::uint32_t m) {
        renderer3dVk_.set_light_debug_mask(m);
    }
    std::uint32_t light_debug_mask() const {
        return renderer3dVk_.light_debug_mask();
    }
    // Diagnostics are per-run TOOLS, not settings: every game session boots
    // the universal default — everything on, nothing frozen. Called from the
    // one session door (boot_world) so no console toggle can leak into a new
    // game or a load (owner rule 2026-08-11; same bug class as playerZ_,
    // problems.md engine-state-not-reset).
    void reset_render_diagnostics() {
        sunFreeze_ = false;
        renderer3dVk_.set_light_debug_mask(0);
    }
    float cam_height_m() const { return cam_.pos.y; }
    // Player feet altitude (metres). Kept under its historical name for the
    // flight smoke, but flight no longer has its own camera scalar — flying
    // is plain 3D movement of playerZ_ with gravity switched off.
    float flight_height_m() const { return playerZ_; }
    // Jump: an upward impulse (height.h kJumpSpeedMps) through the SAME
    // vertical integrator as everything else — only from solid footing, inert
    // while flying or already airborne.
    void jump();
    DangerLevel danger_level() const;
    // Fill and return one blip per live subworld NPC / monster — the SAME
    // candidate set as targeting/melee (view<Position,Health,NPCKind,
    // SubworldTag> minus Dead, hp>0). Each blip's stance comes from the shared
    // player_stance() axis, so the HUD's gradient dots track real combat
    // stance. Reused internal buffer: no per-frame allocation after warm-up.
    // Empty outside a subworld or when no ECS world is attached.
    const std::vector<MinimapBlip>& collect_minimap_blips() const;
    // Stance (player_stance axis, [-1..+1]) of the nearest live entity under
    // the camera reticle — ANY faction (friendly/neutral/hostile). Returns NaN
    // when nothing is aimed at (no entity in cone + range). Used by the
    // crosshair to tint by faction relation colour.
    float crosshair_stance() const;
    const SeamlessSubworldManager& mgr() const { return mgr_; }
    void  move_player(float dx, float dy);
    // Dev console: absolute teleport inside the current subworld window. The
    // next tick re-centres the seamless manager and repopulates if we crossed
    // a cell. Clamped to the walkable window by the implementation.
    void  set_player_pos(float x, float y);
    // Dev console: rebuild the whole 3×3 scene via the enter() path (clear +
    // per-cell re-derive). Fauna is deterministic from each cell's absolute
    // macro seed, so this reproduces the current scene rather than re-rolling.
    void  respawn_fauna();
    // Dev console: invulnerability. While set, the subworld combat path applies
    // no incoming damage to the player (projectiles still vanish on contact).
    void  set_god_mode(bool on) { godMode_ = on; }
    bool  god_mode() const { return godMode_; }
    // Dev console: kill every hostile in the current scene as if the player
    // struck the killing blow — grants XP + loot through the normal death path.
    // Returns the number killed; 0 outside a subworld.
    int   dev_kill_all_hostiles();
    void  set_player_attack_held(bool held) { playerAttackHeld_ = held; }
    void  set_flying(bool enabled);
    bool  flying() const;
    entt::entity player_entity() const;
    void  rotate_camera(float dyaw, float dpitch);
    float spell_rng01() { return spellRng_.next_f01(); }
    const char* status_line() const { return statusLine_.c_str(); }
    int combat_log_count() const { return combatLogCount_; }
    const CombatLogEntry* combat_log_entry(int index) const;

private:
    bool active_ = false;
    bool inited_ = false;
    SceneKind sceneKind_ = SceneKind::Overworld;
    DungeonSession dungeon_{};
    // Accumulated composite-dirty scope awaiting the next renderer upload. The
    // manager's dirty state is cleared the moment we consume it, so a deferred
    // upload (prepare_frame / record_shadow) must hold the union of every
    // consume since the last actual upload. `.any` is the "upload pending" bit.
    CompositeDirty pendingUpload3d_{};
    SeamlessSubworldManager mgr_;
    Renderer3DVk            renderer3dVk_;
    // Body-vs-structure solidity (sub/collide.h): the spatial index of every
    // solid wall/house volume in the composite. Rebuilt in tick() whenever the
    // composite's structure set changes (same CompositeDirty.structs signal the
    // renderer re-uploads on); queried by the ground-follow/flight passes
    // (support: standing ON structures), the player mover, wander/flee AI, the
    // battle pass and spell projectiles (blocking) — one solidity authority.
    StructureIndex structIndex_;
    bool structIndexDirty_ = true;
    // Bodies carrying the lights of lit props (rebuild_prop_cache). Held so a
    // rebuild can retire the previous set; they are ordinary scene entities
    // otherwise, reaped with everything else on leave.
    std::vector<entt::entity> propLights_;
    // Copies of every prop in the composite whose kind carries an interaction
    // (doors, stairs — a handful per scene next to tens of thousands of
    // trees). Copies, not indices: the composite reindexes on every seam.
    std::vector<Structure> interactProps_;
    // Universal transient-VFX pool (spell trails, impacts, blood, embers). Pure
    // CPU sim (sub/particles.h): ticked each frame in tick(), packed + handed to
    // the renderer's additive pass in prepare_frame(). Lives on the engine, not
    // the renderer, so it can read combat/spell state and stays GPU-free.
    ParticleSystem          particles_;
    // Scratch buffer for packing live particles → GPU instances each frame.
    // Sized once to the pool ceiling; reused (no per-frame allocation).
    std::vector<ParticleInstance> particleScratch_;
    // Mass-battle state (sub/battle.h). One SoA snapshot plus its two grids,
    // allocated once and reused every tick — the combat pass never allocates.
    // The ECS stays the authority for damage/death/loot; this is only "where do
    // bodies want to be", so the same code runs one bandit and 16384 soldiers.
    BattleUnits             battle_;
    // Two bucket grids at two scales: bodies are ~1 unit wide, weapons reach up
    // to 25, and one cell size cannot serve both queries without going quadratic
    // or blind. Cell sizes are derived from the crowd's own data, not constants.
    UnitGrid                battleFine_;   // body separation
    UnitGrid                battlePick_;   // contact / target search
    InfluenceField          battleField_;
    BattleParams            battleParams_{};
    // Parallel to battle_: the entity each SoA index came from.
    std::vector<entt::entity> battleEnts_;
    // The spell broad phase's honesty bits (spell_neighbors_callback). The
    // gather sets truncated when the 16k ceiling cut bodies out of the grids —
    // the callback then answers -1 and the spell tick falls back to its full
    // scan, because a body missing from the grid must not be missing from the
    // world. maxStep is the farthest any body can move between the grid build
    // and the spell tick (steering runs in between): the callback pads every
    // query with it plus the fattest body radius.
    bool  battleGatherTruncated_ = false;
    float battleMaxStepM_ = 0.0f;
    // Faction identity is the id STRING (the universal key gs->factions,
    // reputation and loot profiles already use), interned per tick into dense
    // indices. There is no faction roster in the engine and no limit on the
    // world's factions — only on how many stand in one window at once.
    FactionSet              battleFactions_;
    int                     battlePlayerFaction_ = -1;
    // Squared 3D distance to the nearest body hostile to the player, folded out
    // of the battle gather (which already resolves that exact rule). The HUD
    // danger gem and the subworld exit gate read it every frame, so it must be a
    // cached scalar and not an all-entity scan with string faction lookups.
    float                   playerThreatD2_ = kNoThreatDistance2;
    Camera                  cam_;
    // sunfreeze diagnostic (set_sun_freeze / render_time).
    WorldTime render_time() const;
    bool      sunFreeze_ = false;
    WorldTime sunFreezeTime_{};
    const gpu::VulkanDevice* dev_ = nullptr;
    // THE envelope, captured at enter() — the one copy the whole session reads
    // and pays through (macro_stock rows want a MacroWorld&, and now every one
    // of them gets ALL the layers, deposits included). The named pointers
    // below are VIEWS of it, assigned in enter() only — kept because half the
    // engine reads them by these names; they never diverge from mw_ because
    // nothing else ever writes them.
    MacroWorld          mw_{};
    // The 3×3 window's SP weights, resolved once per (enter / re-centre /
    // dungeon scene) — the door's performance contract (macro/cell_facts.h):
    // the per-tick walk price is an array read, never a facts assembly. A
    // negative slot = not cached (fail-open to a live resolve).
    std::array<float, 9> winStepWeight_{};
    bool winStepWeightValid_ = false;
    void refresh_window_step_weights();
    GameState*          gs_       = nullptr;
    const TerrainData*  terrain_  = nullptr;
    const FeatureLayer* features_ = nullptr;
    ecs::World*         ecs_      = nullptr;
    EventBus*           bus_      = nullptr;
    const ZoneLayer*    zones_    = nullptr;
    TreeLayer*          treeLayer_ = nullptr;
    // ── PLACES THAT MEAN SOMETHING (owner, 2026-08-27) ──────────────────
    // «В субмире есть локальные интеракции и ЗОНЫ — например войти в круг
    // определённого радиуса, по смыслу это что посетил круг силы».
    //
    // A zone is a place with a MEANING: a circle of power, a cursed grove, a
    // threshold. Crossing into it is a fact of the world, filed at the macro
    // cell that contains this subworld — so a thing done in one square metre
    // underground is readable from the map above.
    //
    // Flat and capped, like everything the world is made of: a scene holds a
    // handful of meanings, and a generator that wants one adds a row rather
    // than a mechanism.
    struct SubZone {
        float x = 0.0f, y = 0.0f;   // subworld tiles
        float radius = 0.0f;        // 0 = this slot is empty
        FactKind kind = FactKind::None;
        std::int32_t amount = 0;
        // ONCE PER WORLD, not once per visit: the dedup is the world's own
        // memory (chronicle_near_kind), because a place already remembers that
        // somebody stood here. A separate "visited" flag would be a second
        // truth about the same past, and it would have to be saved.
        bool onceEver = true;
    };
    static constexpr int kMaxSubZones = 32;
    // `subZones_`, not `zones_`: this codebase already uses ZoneLayer for the
    // MACRO danger field, and two different meanings must not share a name.
    std::array<SubZone, kMaxSubZones> subZones_{};
    int subZoneCount_ = 0;

    float playerX_ = float(kFullSize / 2);
    float playerY_ = float(kFullSize / 2);
    float playerZ_ = 0.0f;

    bool  godMode_ = false;   // dev console: suppress incoming player damage
    bool  playerAttackHeld_ = false;
    // Player vertical velocity (m/s) — the scalar twin of ecs::Airborne::vz,
    // fed through the same height.h vertical_step. Zero while grounded or
    // flying (flight is gravity-free direct 3D movement).
    float playerVz_ = 0.0f;
    // Feet-on-support this tick (sync_player_vertical) — the jump gate.
    bool  playerGrounded_ = false;
    float playerAttackTimer_ = 0.0f;
    Rng   spellRng_{1u};
    void sync_macro_player_to_center();
    // Inc 5e-1/5e-2: exit-position remap. If the body currently carrying the
    // player flag was PROJECTED from a macro NPC (it has a `MacroOrigin` backlink
    // — i.e. the player possessed a lord/bandit/peasant), land the macro player on
    // THAT macro entity's cell so you "exit AS" the body you possessed, and RETURN
    // that macro entity so leave() can adopt it as the persistent player (5e-2).
    // Returns entt::null (leaving `gs.player` for `sync_macro_player_to_center` to
    // set) for a normal un-possessed exit — the hero husk and ambient/citizen
    // bodies carry no backlink.
    entt::entity remap_macro_player_to_origin();
    CellContext resolve_context(int x, int y) const;
    // Terrain difficulty of the macro cell under a composite-window tile. Same
    // question as ground_faction_at, asked of the terrain instead of the crown.
    float ground_travel_weight_at(float fx, float fy) const;
    // Registry faction of the realm owning the macro cell under a composite-
    // window tile (kNoFaction with no GameState).
    std::uint16_t ground_faction_at(float fx, float fy) const;
    // Per-cell subworld population (seamless persistence). Each of the 3×3
    // window cells owns its creatures, spawned from that cell's ABSOLUTE macro
    // context (so a city fills even off-centre). `spawn_all_cells` is the clean
    // fill on enter; `repopulate_after_recenter` handles a seam crossing by
    // shifting existing entities to track the window, evicting only the cells
    // that left, and spawning only the cells newly brought in — so content in
    // the overlapping cells is never wiped/rebuilt (fixes the vanishing city).
    void spawn_all_cells();
    void spawn_cell(int ox, int oy);
    void repopulate_after_recenter(int dx, int dy);
    // Player-as-entity lifecycle (Inc 4b + 5a). The player is a real ECS entity
    // carrying PlayerTag + Health + Combat + SubworldTag: a full combat actor
    // that hostiles target through the universal melee/projectile paths. These
    // keep exactly one such entity alive while a subworld is active.
    //
    // As of Inc 5a the entity's Position is AUTHORITATIVE inside the subworld and
    // the playerX_/playerY_ scalars are its working mirror (every legacy reader —
    // camera, melee origin, proximity, seam, HUD — still reads the scalars, so
    // they keep working unchanged). The seam (check_boundary) is the one path that
    // legitimately moves the scalars ∓cell; the tick commits that back onto the
    // entity so it stays the single source of truth. HP stays macro-authoritative
    // (combatStats -> entity in sync; entity -> currentHp in reconcile).
    // ── Dungeon session (SceneKind::Dungeon) ──
    // Dungeon interact() face: E on the exit pad walks back out to the very
    // spot the door was opened from. Gated by the same danger law as any
    // subworld exit.
    bool try_exit_dungeon();
    // E on a stair shaft: same interior identity, level ± 1 — a dungeon→
    // dungeon scene swap that never touches the overworld. Gated by the same
    // danger law (a stair is a way out too).
    bool try_take_dungeon_stairs();
    // Raise the interior scene: static 3×3 window (door cell = the interior,
    // ring = sealed Void filler) through the ordinary manager/renderer path.
    // No fauna, no squad, no macro projections — the player enters alone
    // (owner ruling 2026-08-12).
    // Takes THE envelope (like enter): the interior pays its ledgers through
    // mw_, so a scene raised with only the named views left the deposit and
    // population rows fail-closed — a house with no residents and kills that
    // never thinned the town (found by the dungeon_house smoke, 2026-08-24).
    void enter_dungeon_scene(const MacroWorld& mw, EventBus& bus);
    // Rebuild what the engine keeps ABOUT props, from the one composite:
    // the lights of lit props (hung as ordinary `LightEmitter` bodies, so a
    // street lantern and a carried torch reach the shader through the same
    // path) and the short list of INTERACTIVE props, so the per-frame aim
    // scan walks a handful of doors instead of every tree in the window.
    // Runs on the same "structures changed" signal as the solidity index.
    void rebuild_prop_cache();
    // The prop the player is looking at, or nullptr. Pure query over the
    // interactive cache: forward cone on the camera yaw, within the verb's
    // own reach measured to the prop's SURFACE.
    const Structure* aimed_prop() const;
    // The corpse under the reticle (entt::null if none) — same cone, the
    // Loot verb's own reach.
    entt::entity aimed_corpse() const;
    // Step through a door prop: resolves the building it belongs to (its
    // `tag` is that house's ordinal within the window cell) and raises the
    // interior. False if the building cannot be resolved.
    bool enter_dungeon_by_door(const Structure& door);
    void spawn_player_entity();
    void clear_player_entity();
    void sync_player_entity_position();
    void reconcile_player_hp_to_macro();
    // Wounds of TRACKED bodies (sub/spawn.h) written back to the macro entities
    // they embody, every tick, as a fraction. The player's sibling of this has
    // existed all along; nobody else had one, so the map healed everyone you
    // failed to finish the moment you climbed out.
    void reconcile_tracked_bodies_to_macro();
    // 5a authority mirror: propagate the authoritative player-entity Position onto
    // the scalar mirror (pull) and vice-versa (push). Both are no-ops when no
    // PlayerTag+Position entity exists (0/1 entities, cheap). push_ is an
    // assignment so it is idempotent w.r.t. the seam rebase that also shifts the
    // SubworldTag-tagged player entity.
    void pull_player_entity_to_scalars();
    void push_scalars_to_player_entity();
    // ONE player vertical rule, run from tick() (headless-honest — the feet
    // are simulated whether or not a frame renders; record_shadow only adds
    // the camera at playerZ_ + eye height). Walking: the same height.h
    // vertical_step every NPC uses — rest on the support surface, fall with
    // gravity when it drops away (losing flight mid-air simply starts a
    // fall). Flying: gravity-free, clamped to [support, window ceiling].
    void sync_player_vertical(float dt);
    bool exit_blocked_by_danger() const;
    bool has_hostile_near_player(float radius) const;
    void tick_player_melee(float dt);
    void tick_hit_flashes(float dt);
    // Drain the one-shot ecs::DamageFx markers stamped by every damage site this
    // tick into blood / dust particle bursts, then remove them. ONE place turns a
    // "hit landed" signal into VFX: the spray archetype (red blood vs grey dust)
    // is classified from the victim's Sprite.archetype (Undead / Hulk = dust,
    // everything else = flesh = blood) so there is no per-creature code, and the
    // damage sites — including the renderer-free spell TU — stay particle-free.
    // Runs BEFORE resolve_subworld_deaths so a killing blow still sprays from the
    // body's live Position. Skips the player body (its feedback is the HUD flash;
    // a burst at the camera would clip the near plane).
    void tick_damage_fx();
    void tick_subworld_combat(float dt);
    // THE faction relation rule: one lookup in the macro matrix for ANY pair,
    // the player included (he is an ordinary row). A function pointer so the
    // pair loop lives in the pure, Vulkan-free module.
    static int battle_relation_callback(void* user, const FactionSet& set,
                                        int a, int b);
    void resolve_subworld_deaths(bool drainAll = false);
    void set_status(const char* msg);

    // THE micro→macro door (CANON S20.1): a subworld act with lasting meaning
    // is filed in the world's ONE memory, at the macro cell that contains this
    // subworld. One memory, and the place is the seam between the layers.
    void record_world_fact(FactKind kind, int cellX, int cellY, int amount);

    // Watch for the player crossing INTO a zone. Called from tick().
    void tick_zones();

    // Where the player is standing, in MACRO cells — the seam's own
    // coordinate, and the only translation the layers need between them.
    void player_macro_cell(int& cx, int& cy) const;

    // Zones the player has entered THIS session, so a circle files its fact on
    // the step that crosses into it and not on every step inside it.
    std::uint32_t subZonesEntered_ = 0;   // bitmask over kMaxSubZones
    void push_combat_log(const char* msg);
    void push_player_hit_log(std::uint32_t targetEntityId,
                             float damage,
                             bool lethal);
    static void spell_damage_log_callback(void* user,
                                          std::uint32_t targetEntityId,
                                          float damage,
                                          bool lethal);
    static bool spell_can_hit_callback(void* user,
                                       const ecs::Projectile& projectile,
                                       std::uint32_t targetEntityId);
    static float spell_height_callback(void* user, float x, float y);
    // Spell broad phase over battlePick_ (the contact grid): full-radius walk,
    // NO visit budget — a projectile miss from an exhausted budget would be a
    // bug, not an approximation, unlike the AI's budgeted contact scan.
    static int spell_neighbors_callback(void* user, float x, float y, float r,
                                        std::uint32_t* out, int maxOut);
    // Terrain hook for the battle pass (sub/battle.h stays Vulkan-free, so it
    // reaches the CPU heightfield through a function pointer, not an include).
    static float battle_height_callback(void* user, float x, float y);
    // Solidity hooks (sub/collide.h through the same fn-pointer idiom): the
    // battle pass and wander/flee AI ask "may a body stand here", spell bolts
    // ask "is this point inside masonry".
    static bool solid_can_stand_callback(void* user, float x, float y,
                                         float r, float z);
    static bool spell_solid_callback(void* user, float x, float y, float z);
    // Turns a spell-tick FX event (bolt trail / impact) into a particle burst.
    // Static + void* user so spell_effects.cpp needs no SubworldEngine type.
    static void spell_fx_emit_callback(void* user,
                                       SpellFxEvent event,
                                       std::uint32_t entity,
                                       float ax, float ay, float az,
                                       float bx, float by, float bz,
                                       float blastRadius);
    static bool player_threat_callback(void* user,
                                       std::uint32_t entityId);
    float statusTimer_ = 0.0f;
    // Reused scratch for collect_minimap_blips() — rebuilt each call, kept
    // allocated across frames so the HUD never allocates in the draw path.
    mutable std::vector<MinimapBlip> minimapBlips_;
    std::string statusLine_;
    std::array<CombatLogEntry, kCombatLogLimit> combatLog_{};
    int combatLogCount_ = 0;
    float elapsed_ = 0.0f; // real seconds since enter() — drives sky animation
};

} // namespace sm::sub
