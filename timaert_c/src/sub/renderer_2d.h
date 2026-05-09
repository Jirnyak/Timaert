// Minimal subworld 2D renderer — uploads composite tile map as a single RG8
// texture and draws it through a fullscreen shader. Sprites/structures are
// drawn as instanced quads (kept simple here).
#pragma once
#include "gl/gl.h"
#include "core/math.h"
#include "sub/map_data.h"
#include "sub/seamless_manager.h"

namespace sm::sub {

class SubworldRenderer2D {
public:
    void init();
    void destroy();
    void upload(const SeamlessSubworldManager& mgr);
    void render(int viewW, int viewH, float camX, float camY, float zoom);

private:
    GLuint prog_      = 0;
    GLuint vao_       = 0, vbo_ = 0;
    GLuint tileTex_   = 0;
    GLuint heightTex_ = 0;
    int    w_ = 0, h_ = 0;
};

} // namespace sm::sub
