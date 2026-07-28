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
#include "sub/vk_renderer_3d.h"
#include "events/event_bus.h"

#include <vulkan/vulkan.h>

namespace gpu { struct VulkanDevice; }

namespace sm {
struct GameState;
struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;
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

    void enter(GameState& gs, const TerrainData& terrain,
               const FeatureLayer& features, ecs::World& ecs,
               EventBus& bus,
               const ZoneLayer* zones = nullptr);
    void leave(bool force = false);
    bool interact();
    bool spawn_hostile_npc(const char* npcTypeId,
                           const char* displayName,
                           int level,
                           std::uint32_t seed,
                           const ecs::NpcInventory* inventoryOverride = nullptr,
                           const ecs::NpcTraits* traitsOverride = nullptr,
                           const ecs::NpcCharacter* characterOverride = nullptr);

    void tick(float dt);
    void prepare_frame(VkCommandBuffer cmd);

    // Depth-only shadow casters, recorded BEFORE the main render pass.
    void record_shadow(VkCommandBuffer cmd);
    // Main-pass draws recorded inside the main render pass.
    void record_main(VkCommandBuffer cmd, VkExtent2D ext);

    bool active() const { return active_; }
    float player_x() const { return playerX_; }
    float player_y() const { return playerY_; }
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
    bool  flying() const { return playerFlying_; }
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
    Camera                  cam_;
    const gpu::VulkanDevice* dev_ = nullptr;
    GameState*          gs_       = nullptr;
    const TerrainData*  terrain_  = nullptr;
    const FeatureLayer* features_ = nullptr;
    ecs::World*         ecs_      = nullptr;
    EventBus*           bus_      = nullptr;
    const ZoneLayer*    zones_    = nullptr;
    float playerX_ = float(kFullSize / 2);
    float playerY_ = float(kFullSize / 2);
    bool  playerFlying_ = false;
    bool  godMode_ = false;   // dev console: suppress incoming player damage
    bool  playerAttackHeld_ = false;
    float flightCamY_ = 0.0f;
    float playerAttackTimer_ = 0.0f;
    Rng   spellRng_{1u};
    void sync_macro_player_to_center();
    // Inc 5e-1: exit-position remap. If the body currently carrying the player
    // flag was PROJECTED from a macro NPC (it has a `MacroOrigin` backlink — i.e.
    // the player possessed a lord/bandit/peasant), land the macro player on THAT
    // macro entity's cell so you "exit AS" the body you possessed. Returns false
    // (leaving `gs.player` for `sync_macro_player_to_center` to set) for a normal
    // un-possessed exit — the hero husk and ambient/citizen bodies carry no
    // backlink. Runtime-only (MacroOrigin is not serialised) ⇒ save stays v9.
    bool remap_macro_player_to_origin();
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
    bool exit_blocked_by_danger() const;
    bool has_hostile_near_player(float radius) const;
    void tick_player_melee(float dt);
    void tick_hit_flashes(float dt);
    void tick_subworld_combat(float dt);
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
