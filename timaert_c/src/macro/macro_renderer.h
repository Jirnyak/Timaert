// Macroworld renderer — single fullscreen pass. Reads master + featureMap +
// optional zoneMap; runs procedural per-biome ground synth + overlays
// (roads, trees, mountains, night tint). Mirrors the TS map fragment shader.
#pragma once
#include "gl/gl.h"
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/zones.h"
#include "macro/state.h"

namespace sm {

struct MacroRenderer {
    GLuint prog = 0;
    GLuint featureTex = 0;
    GLuint zoneTex = 0;
    GLuint landmarkTex = 0;
    int landmarkW = 0, landmarkH = 0;
    GLuint vao = 0, vbo = 0;

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
              const WorldTime& time);
};

} // namespace sm
