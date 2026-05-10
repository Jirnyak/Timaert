// Subworld engine — owns SeamlessSubworldManager + 2D renderer. Driven by
// the application loop. Holds raw pointers to macroworld state captured at
// enter() time; not copied.
#pragma once
#include <cstdint>
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
}

namespace sm::sub {

class SubworldEngine {
public:
    void init();
    void destroy();

    void enter(GameState& gs, const TerrainData& terrain,
               const FeatureLayer& features, ecs::World& ecs,
               EventBus& bus,
               const ZoneLayer* zones = nullptr);
    void leave();

    void tick(float dt);
    void render(int w, int h);

    bool active() const { return active_; }
    float player_x() const { return playerX_; }
    float player_y() const { return playerY_; }
    float cam_yaw() const { return cam_.yaw; }
    const SeamlessSubworldManager& mgr() const { return mgr_; }
    void  set_zoom(float z) { zoom_ = z; }
    void  move_player(float dx, float dy);
    void  toggle_3d() { view3D_ = !view3D_; }
    bool  is_3d() const { return view3D_; }
    void  rotate_camera(float dyaw, float dpitch);

private:
    bool active_ = false;
    bool inited_ = false;
    bool view3D_ = true;  // First-person 3D is the default subworld view; F toggles 2D top-down.
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
    float playerX_ = float(kFullSize / 2);
    float playerY_ = float(kFullSize / 2);
    float zoom_    = 0.5f;
    void sync_macro_player_to_center();
    void emit_world_cell_change(const char* action);
    float elapsed_ = 0.0f; // real seconds since enter() — drives sky animation
};

} // namespace sm::sub
