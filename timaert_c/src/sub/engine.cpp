#include "sub/engine.h"
#include "sub/spawn.h"
#include "sub/ai.h"
#include "sub/spatial_hash.h"
#include "sub/base_generator.h"
#include "ecs/systems.h"
#include "macro/state.h"
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/biomes.h"
#include "macro/zones.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

void SubworldEngine::init() {
    if (inited_) return;
    renderer_.init();
    renderer3d_.init();
    sky_.init();
    inited_ = true;
}

void SubworldEngine::destroy() {
    if (!inited_) return;
    renderer_.destroy();
    renderer3d_.destroy();
    sky_.destroy();
    inited_ = false;
}

void SubworldEngine::enter(GameState& gs, const TerrainData& terrain,
                           const FeatureLayer& features, ecs::World& ecs,
                           EventBus& bus,
                           const ZoneLayer* zones) {
    gs_ = &gs; terrain_ = &terrain; features_ = &features;
    ecs_ = &ecs; bus_ = &bus;
    int cx = int(gs.player.x);
    int cy = int(gs.player.y);

    int W = terrain.width, H = terrain.height;
    auto resolver = [W, H, &terrain, &features, &gs](int x, int y) {
        CellContext c{};
        int xi = ((x % W) + W) % W;
        int yi = ((y % H) + H) % H;
        std::size_t idx = std::size_t(yi) * W + xi;
        float h = float(terrain.rgba[idx * 4 + 0]) / 255.0f;
        float m = float(terrain.rgba[idx * 4 + 1]) / 255.0f;
        float t = float(terrain.rgba[idx * 4 + 2]) / 255.0f;
        std::uint8_t mask = terrain.rgba[idx * 4 + 3];
        c.cx = x; c.cy = y;
        c.macroHeight = h;
        c.biome   = mask ? biome_from_climate(t, m) : Biome::Water;
        c.feature = features.at(xi, yi);
        c.landmarkSettlementId = -1;
        c.landmarkSize = 0;
        // Resolve landmark (city/village) on this cell so generators and
        // fauna routing can react. Linear scan is fine — settlements are
        // a small set (< 100) and resolver is called O(9) times per enter.
        for (const auto& s : gs.settlements) {
            if (s.x == xi && s.y == yi) {
                c.landmarkSettlementId = s.id;
                c.landmarkSize = s.population;
                break;
            }
        }
        if (c.landmarkSettlementId < 0) {
            for (const auto& v : gs.villages) {
                if (v.x == xi && v.y == yi) {
                    c.landmarkSettlementId = v.id;
                    c.landmarkSize = v.population;
                    break;
                }
            }
        }
        c.seed = gs.worldSeed
               ^ (std::uint32_t(xi) * 73856093u)
               ^ (std::uint32_t(yi) * 19349663u);
        return c;
    };

    mgr_.init(cx, cy, resolver);
    renderer_.upload(mgr_);
    renderer3d_.upload(mgr_);
    active_  = true;
    playerX_ = playerY_ = float(kFullSize / 2);

    // Resolve centre cell again to pull biome + feature + landmark for the
    // initial fauna roll. We re-run the resolver (cheap) instead of
    // duplicating the inline math here.
    CellContext center = resolver(cx, cy);
    LandmarkKind lk = LandmarkKind::None;
    if (center.landmarkSettlementId >= 0) {
        lk = center.landmarkSize > 1500 ? LandmarkKind::City
                                        : LandmarkKind::Village;
    }
    respawn_subworld_npcs(ecs, center.biome, center.feature, lk, mgr_,
        gs.worldSeed ^ (std::uint32_t(cx) << 16) ^ std::uint32_t(cy),
        center.landmarkSize,
        zones && !zones->data.empty() ? int(zones->at(cx, cy)) : 0);
}

void SubworldEngine::sync_macro_player_to_center() {
    if (!gs_ || !terrain_ || terrain_->width <= 0 || terrain_->height <= 0) {
        return;
    }
    int nx = mgr_.center_cx() % terrain_->width;
    int ny = mgr_.center_cy() % terrain_->height;
    if (nx < 0) nx += terrain_->width;
    if (ny < 0) ny += terrain_->height;
    gs_->player.x = float(nx);
    gs_->player.y = float(ny);
}

void SubworldEngine::leave() {
    if (active_) {
        mgr_.snapshot_all_to_cache();
        // Sync the player's MACRO position from where they actually ended
        // up in the subworld. The seamless manager re-centres the 3×3 grid
        // when the player crosses a cell boundary, so `mgr_.center_cx()/cy()`
        // is the macro cell currently under the player. Macro convention
        // (see ui/macro_overlay.cpp player render) is `player.x` = INTEGER
        // cell index; the sprite is drawn at `player.x + 0.5` to centre it.
        // Writing a fractional sub-cell offset here would render the sprite
        // at `cellIdx + 0.5 + 0.5` = vertex of 4 cells. Snap to the centre
        // cell of the seamless 3×3 grid.
        sync_macro_player_to_center();
    }
    active_ = false;
    gs_ = nullptr;
    terrain_ = nullptr;
    features_ = nullptr;
    ecs_ = nullptr;
    bus_ = nullptr;
}

void SubworldEngine::move_player(float dx, float dy) {
    if (!active_) return;
    if (view3D_) {
        // First-person walk: dy = forward (UP arrow / W), dx = strafe right.
        // Camera-forward in tile-XY plane is (cos yaw, sin yaw); right is its
        // 90° rotation (-sin yaw, cos yaw). Compose world delta from those
        // basis vectors so UP always means "into the screen".
        const float speed = 0.4f;
        const float cy = std::cos(cam_.yaw), sy = std::sin(cam_.yaw);
        const float wx = dy * cy - dx * sy;
        const float wy = dy * sy + dx * cy;
        playerX_ += wx * speed;
        playerY_ += wy * speed;
    } else {
        playerX_ += dx; playerY_ += dy;
    }
    if (playerX_ < 0) playerX_ = 0;
    if (playerY_ < 0) playerY_ = 0;
    if (playerX_ > float(kFullSize)) playerX_ = float(kFullSize);
    if (playerY_ > float(kFullSize)) playerY_ = float(kFullSize);
}

void SubworldEngine::rotate_camera(float dyaw, float dpitch) {
    cam_.yaw   += dyaw;
    cam_.pitch += dpitch;
    if (cam_.pitch >  1.4f) cam_.pitch =  1.4f;
    if (cam_.pitch < -1.4f) cam_.pitch = -1.4f;
}

void SubworldEngine::tick(float dt) {
    if (!active_) return;
    elapsed_ += dt;
    int prevCx = mgr_.center_cx(), prevCy = mgr_.center_cy();
    mgr_.check_boundary(playerX_, playerY_);
    if (prevCx != mgr_.center_cx() || prevCy != mgr_.center_cy()) {
        sync_macro_player_to_center();
        renderer_.upload(mgr_);
        renderer3d_.upload(mgr_);
    }

    if (ecs_) {
        tick_npc_ai(*ecs_, playerX_, playerY_, 0u, dt);
        ecs::sys::tick_visual_interp(*ecs_, dt);
        ecs::sys::tick_combat_cooldowns(*ecs_, dt);
        ecs::sys::tick_projectiles(*ecs_, dt);
    }
}

void SubworldEngine::render(int w, int h) {
    if (!active_) return;
    if (gs_) {
        // Sky as celestial sphere — view ray reconstructed from camera in
        // shader, so rotating the camera does not rotate the sky.
        const float fogR = 0.62f, fogG = 0.72f, fogB = 0.84f; // matches horizDay
        sky_.render(w, h, gs_->worldTime, cam_, elapsed_,
                    gs_->worldSeed, fogR, fogG, fogB);
    }
    if (view3D_ && gs_) {
        // Sync camera to player tile position with eye height above terrain.
        float wx = 0, wz = 0;
        Renderer3D::tile_to_world(playerX_, playerY_, wx, wz);
        float groundM = renderer3d_.sample_height_m(playerX_, playerY_);
        const float kEyeM = 1.7f;
        cam_.pos = {wx, groundM + kEyeM, wz};
        // Visual water plane = `WATER_LEVEL` (single source of truth in
        // `base_generator.h`). The same constant drives heightmap remap
        // (water cells map to [0, WATER_LEVEL] via squared deep-ocean
        // curve; land cells map to [WATER_LEVEL + kLandMargin, 1.0] via
        // linear lift), structure culling in `renderer_3d`, and the
        // visible water surface here. Keeping these aligned eliminates
        // the "shore submerged" / "land below water" artefacts: every
        // land pixel sits at least `kLandMargin` above the plane after
        // bilinear blend, every water pixel sits at most WATER_LEVEL.
        renderer3d_.render(w, h, cam_, gs_->worldTime, WATER_LEVEL, &mgr_);
    } else {
        renderer_.render(w, h, playerX_, playerY_, zoom_);
    }
}

} // namespace sm::sub
