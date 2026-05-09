#include "sub/renderer_2d.h"
#include "gl/helpers.h"

namespace sm::sub {

static const char* kVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUv;
uniform vec4 uRect; // xy = offset (NDC), zw = size (NDC)
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(uRect.xy + aPos * uRect.zw, 0.0, 1.0);
}
)";

static const char* kFS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTiles;   // R = tile id
uniform sampler2D uHeight;  // R = height 0..1
void main() {
    float tid = texture(uTiles, vUv).r * 255.0;
    float h   = texture(uHeight, vUv).r;
    vec3 col;
    int id = int(tid + 0.5);
    if      (id == 1)  col = vec3(0.30,0.55,0.20);   // grass
    else if (id == 2)  col = vec3(0.70,0.60,0.30);   // field
    else if (id == 3)  col = vec3(0.10,0.30,0.05);   // tree
    else if (id == 4)  col = vec3(0.45,0.40,0.30);   // road
    else if (id == 5)  col = vec3(0.55,0.40,0.25);   // house
    else if (id == 6)  col = vec3(0.55,0.55,0.55);   // wall
    else if (id == 7)  col = vec3(0.10,0.25,0.55);   // water
    else if (id == 8)  col = vec3(0.85,0.80,0.55);   // shore
    else if (id == 9)  col = vec3(0.65,0.55,0.40);   // square
    else if (id == 10) col = vec3(0.50,0.48,0.45);   // rock
    else               col = vec3(0.10,0.10,0.10);
    col *= 0.4 + 0.6 * h;
    frag = vec4(col, 1.0);
}
)";

void SubworldRenderer2D::init() {
    prog_ = gl_link(kVS, kFS);
    static const float quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}

void SubworldRenderer2D::destroy() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_)  glDeleteVertexArrays(1, &vao_);
    if (vbo_)  glDeleteBuffers(1, &vbo_);
    if (tileTex_)   glDeleteTextures(1, &tileTex_);
    if (heightTex_) glDeleteTextures(1, &heightTex_);
    prog_ = vao_ = vbo_ = tileTex_ = heightTex_ = 0;
}

void SubworldRenderer2D::upload(const SeamlessSubworldManager& mgr) {
    w_ = h_ = kFullSize;
    if (!tileTex_)   glGenTextures(1, &tileTex_);
    if (!heightTex_) glGenTextures(1, &heightTex_);

    glBindTexture(GL_TEXTURE_2D, tileTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w_, h_, 0, GL_RED, GL_UNSIGNED_BYTE,
                 mgr.tiles().data());

    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w_, h_, 0, GL_RED, GL_FLOAT,
                 mgr.heightmap().data());
}

void SubworldRenderer2D::render(int viewW, int viewH, float camX, float camY, float zoom) {
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, viewW, viewH);
    glUseProgram(prog_);
    // Map a sub-rect of the composite to fullscreen.
    float visW = viewW / zoom, visH = viewH / zoom;
    float u0 = (camX - visW * 0.5f) / float(w_);
    float v0 = (camY - visH * 0.5f) / float(h_);
    float u1 = u0 + visW / float(w_);
    float v1 = v0 + visH / float(h_);
    // Pass UV via uRect: trick — abuse uRect to just full viewport, then sample
    // via custom shader uniform. Simpler: blit fullscreen and let shader scale.
    GLint loc = glGetUniformLocation(prog_, "uRect");
    glUniform4f(loc, 0.0f, 0.0f, 1.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tileTex_);
    glUniform1i(glGetUniformLocation(prog_, "uTiles"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glUniform1i(glGetUniformLocation(prog_, "uHeight"), 1);
    (void)u0; (void)v0; (void)u1; (void)v1;  // reserved for sub-rect sampling
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

} // namespace sm::sub
