// Subworld engine — owns SeamlessSubworldManager + 2D renderer. Driven by
// the application loop. Holds raw pointers to macroworld state captured at
// enter() time; not copied.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "core/rng.h"
#include "ecs/world.h"
#include "sub/seamless_manager.h"
#include "sub/renderer_2d.h"
#include "sub/renderer_3d.h"
#include "sub/sky.h"
#include "events/event_bus.h"

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
    void init();
    void destroy();

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
    void render(int w, int h);

    bool active() const { return active_; }
    float player_x() const { return playerX_; }
    float player_y() const { return playerY_; }
    float cam_yaw() const { return cam_.yaw; }
    float cam_height_m() const { return cam_.pos.y; }
    float flight_height_m() const { return flightCamY_; }
    DangerLevel danger_level() const;
    const SeamlessSubworldManager& mgr() const { return mgr_; }
    void  set_zoom(float z) { zoom_ = z; }
    void  move_player(float dx, float dy);
    void  set_player_attack_held(bool held) { playerAttackHeld_ = held; }
    void  set_flying(bool enabled);
    bool  flying() const { return playerFlying_; }
    void  toggle_3d() { view3D_ = !view3D_; }
    bool  is_3d() const { return view3D_; }
    void  rotate_camera(float dyaw, float dpitch);
    float spell_rng01() { return spellRng_.next_f01(); }
    const char* status_line() const { return statusLine_.c_str(); }
    int combat_log_count() const { return combatLogCount_; }
    const CombatLogEntry* combat_log_entry(int index) const;

private:
    bool active_ = false;
    bool inited_ = false;
    bool view3D_ = true;  // First-person 3D is the default subworld view; F toggles 2D top-down.
    bool upload2dDirty_ = false;
    bool upload3dDirty_ = false;
    SeamlessSubworldManager mgr_;
    SubworldRenderer2D      renderer_;
    Renderer3D              renderer3d_;
    Sky                     sky_;
    Camera                  cam_;
    GameState*          gs_       = nullptr;
    const TerrainData*  terrain_  = nullptr;
    const FeatureLayer* features_ = nullptr;
    ecs::World*         ecs_      = nullptr;
    EventBus*           bus_      = nullptr;
    const ZoneLayer*    zones_    = nullptr;
    float playerX_ = float(kFullSize / 2);
    float playerY_ = float(kFullSize / 2);
    bool  playerFlying_ = false;
    bool  playerAttackHeld_ = false;
    float flightCamY_ = 0.0f;
    float playerAttackTimer_ = 0.0f;
    Rng   spellRng_{1u};
    float zoom_    = 0.5f;
    void sync_macro_player_to_center();
    bool exit_blocked_by_danger() const;
    bool has_hostile_near_player(float radius) const;
    void tick_player_melee(float dt);
    void tick_hit_flashes(float dt);
    void tick_subworld_combat(float dt);
    void resolve_projectile_hits_player();
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
