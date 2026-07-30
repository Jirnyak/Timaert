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
#include "ecs/world.h"
#include "sub/seamless_manager.h"
#include "sub/camera.h"
#include "sub/collide.h"
#include "sub/particles.h"
#include "sub/spell_effects.h"
#include "sub/battle.h"
#include "sub/vk_renderer_3d.h"
#include "events/event_bus.h"

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

constexpr int kCombatLogLimit = 20;
constexpr int kCombatLogMaxVisible = 5;
constexpr float kCombatLogVisibleSeconds = 4.0f;

struct CombatLogEntry {
    char text[96]{};
    float age = 0.0f;
};

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

    // `treeLayer` (optional): the mutable macro tree-count layer
    // (macro/tree_layer.h). When present it feeds CellContext.treeCount so
    // generation is count-driven, and felling a tree in here decrements the
    // owning cell's count (micro → macro writeback via gs.treeOverrides).
    void enter(GameState& gs, const TerrainData& terrain,
               const FeatureLayer& features, ecs::World& ecs,
               EventBus& bus,
               const ZoneLayer* zones = nullptr,
               TreeLayer* treeLayer = nullptr);
    void leave(bool force = false);
    bool interact();
    // Fell the nearest standing tree within `maxDist` of the player — the ONE
    // wood-cutting path (a no-target melee swing routes here; console `chop`
    // and smokes call it directly): the manager removes the Structure::Tree
    // from its owning cell + composite, and the owning macro cell's TreeLayer
    // count decrements through gs.treeOverrides (micro → macro writeback).
    // Optional outs report the owning macro cell and its count BEFORE the
    // decrement. Returns false when no tree is in reach.
    bool fell_tree_near_player(float maxDist, int* outCellX = nullptr,
                               int* outCellY = nullptr,
                               int* outPrevCount = nullptr);
    bool spawn_hostile_npc(const char* npcTypeId,
                           const char* displayName,
                           int level,
                           std::uint32_t seed,
                           const char* factionId = "bandits",
                           const ecs::NpcInventory* inventoryOverride = nullptr,
                           const ecs::NpcTraits* traitsOverride = nullptr,
                           const ecs::NpcCharacter* characterOverride = nullptr,
                           // Explicit {x, y} spawn tile instead of the default
                           // ring around the player. Needed to DEPLOY a body:
                           // an army spawned in one 34-unit ring is a pile, not
                           // a battle line, so the stress/battle paths place
                           // their blocks themselves.
                           const float* positionOverride = nullptr);

    void tick(float dt);
    void prepare_frame(VkCommandBuffer cmd);

    // Depth-only shadow casters, recorded BEFORE the main render pass.
    void record_shadow(VkCommandBuffer cmd);
    // Main-pass draws recorded inside the main render pass. `frameIndex` is the
    // renderer's currentFrame — it selects the per-frame light-SSBO ring slot in
    // the 3D renderer (see Renderer3DVk::record_main).
    void record_main(VkCommandBuffer cmd, VkExtent2D ext,
                     std::uint32_t frameIndex);

    bool active() const { return active_; }
    float player_x() const { return playerX_; }
    float player_y() const { return playerY_; }
    float player_z() const { return playerZ_; }
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
    float cam_height_m() const { return cam_.pos.y; }
    float flight_height_m() const { return flightCamY_; }
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
    const gpu::VulkanDevice* dev_ = nullptr;
    GameState*          gs_       = nullptr;
    const TerrainData*  terrain_  = nullptr;
    const FeatureLayer* features_ = nullptr;
    ecs::World*         ecs_      = nullptr;
    EventBus*           bus_      = nullptr;
    const ZoneLayer*    zones_    = nullptr;
    TreeLayer*          treeLayer_ = nullptr;
    float playerX_ = float(kFullSize / 2);
    float playerY_ = float(kFullSize / 2);
    float playerZ_ = 0.0f;

    bool  godMode_ = false;   // dev console: suppress incoming player damage
    bool  playerAttackHeld_ = false;
    float flightCamY_ = 0.0f;
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
    void spawn_player_entity();
    void clear_player_entity();
    void sync_player_entity_position();
    void reconcile_player_hp_to_macro();
    // 5a authority mirror: propagate the authoritative player-entity Position onto
    // the scalar mirror (pull) and vice-versa (push). Both are no-ops when no
    // PlayerTag+Position entity exists (0/1 entities, cheap). push_ is an
    // assignment so it is idempotent w.r.t. the seam rebase that also shifts the
    // SubworldTag-tagged player entity.
    void pull_player_entity_to_scalars();
    void push_scalars_to_player_entity();
    // ONE player vertical rule (walking feet-on-support / flying envelope),
    // shared by tick() and record_shadow(): updates flightCamY_ + playerZ_
    // from the terrain + structure support under (playerX_, playerY_).
    // Called from tick() so the feet are honest even when no frame is
    // rendered (headless smokes, tests) — record_shadow only adds the camera.
    void sync_player_vertical();
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
    // THE faction relation rule (macro matrix, or reputation when one side is the
    // player). A function pointer so the pair loop lives in the pure module.
    static int battle_relation_callback(void* user, const char* a, const char* b);
    void resolve_subworld_deaths(bool drainAll = false);
    void set_status(const char* msg);
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
