// Subworld engine — owns SeamlessSubworldManager + Vulkan 3D renderer.
// Driven by the application loop. Holds raw pointers to macroworld state
// captured at enter() time; not copied. The subworld is always first-person
// 3D; the flat 2D view is the macro map / minimap, not a subworld mode.
#pragma once
#include <array>
#include <cstdint>
#include <string>
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
    float cam_yaw() const { return cam_.yaw; }
    float cam_height_m() const { return cam_.pos.y; }
    float flight_height_m() const { return flightCamY_; }
    DangerLevel danger_level() const;
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
    // Player-as-entity lifecycle (Inc 4b). The player is a real ECS entity
    // carrying PlayerTag + Health + Combat + SubworldTag: a full combat actor
    // that hostiles target through the universal melee/projectile paths. These
    // keep exactly one such entity alive while a subworld is active, mirror the
    // macro-authoritative scalars onto it each tick (sync = Position + Health
    // pull), and push its post-combat Health back onto currentHp (reconcile).
    // Kept void / entt-free so this header stays free of ECS includes.
    void spawn_player_entity();
    void clear_player_entity();
    void sync_player_entity_position();
    void reconcile_player_hp_to_macro();
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
    std::string statusLine_;
    std::array<CombatLogEntry, kCombatLogLimit> combatLog_{};
    int combatLogCount_ = 0;
    float elapsed_ = 0.0f; // real seconds since enter() — drives sky animation
};

} // namespace sm::sub
