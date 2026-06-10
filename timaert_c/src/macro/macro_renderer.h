// Macroworld renderer - single fullscreen pass. Reads master, feature, zone,
// landmark, and river maps; runs procedural per-biome ground synth plus
// painter-ordered overlays. Mirrors the TS map fragment shader contract.
#pragma once
#include "gl/gl.h"
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/zones.h"
#include "macro/state.h"

#include <cstdint>
#include <vector>

namespace sm {

struct MacroRenderer {
    GLuint prog = 0;
    GLuint featureTex = 0;
    GLuint zoneTex = 0;
    GLuint landmarkTex = 0;
    int landmarkW = 0, landmarkH = 0;
    GLuint vao = 0, vbo = 0;
    std::vector<std::uint8_t> featureUploadScratch;
    std::vector<std::uint8_t> zoneUploadScratch;
    std::vector<std::uint8_t> landmarkUploadScratch;

    // Night landmark glow: built in rebuild_landmarks, culled + uploaded in draw.
    struct LandmarkLight {
        float nx, ny;     // normalized cell-centre coords (shader L.xy)
        float cx, cy;     // cell-centre coords (view-frustum culling)
        float radius;     // glow radius in cells
        float intensity;  // 0..1
        float r, g, b;    // emitted colour
    };
    std::vector<LandmarkLight> landmarkLights;
    float lightMapW = 0.0f, lightMapH = 0.0f;

    bool init();
    void destroy();

    // Upload the current FeatureLayer / ZoneLayer to GPU.
    void upload_features(const FeatureLayer& fl);
    void upload_zones(const ZoneLayer& zl);
    // Upload landmark byte grid: 0=none, 1=city, 2=village.
    void upload_landmarks(int w, int h, const std::uint8_t* data);
    // Convenience: build landmark grid from GameState settlements/villages.
    void rebuild_landmarks(const GameState& gs);

    // Draw the world filling the viewport. cam = world-space pixel offset.
    void draw(const TerrainData& td,
              float camX, float camY, float zoom,
              int viewW, int viewH,
              const WorldTime& time,
              float seaLevel = 0.40f);
};

} // namespace sm
