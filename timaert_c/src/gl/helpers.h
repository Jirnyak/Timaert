// Minimal OpenGL helpers: shader compile, program link, mesh, FBO, texture upload.
#pragma once
#include "gl/gl.h"
#include <cstdio>
#include <cstdint>

namespace sm {

// Compile a single shader stage. Returns 0 on failure (logs to stderr).
GLuint gl_compile(GLenum stage, const char* src);

// Link a vertex+fragment program. Returns 0 on failure. Auto-deletes the shaders on success.
GLuint gl_link(const char* vsrc, const char* fsrc);

// Build a fullscreen-triangle VAO (single triangle covering NDC).
struct FullscreenQuad {
    GLuint vao = 0;
    GLuint vbo = 0;
    void create();
    void draw() const;
    void destroy();
};

// Generic indexed mesh.
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int    index_count = 0;
    void destroy();
};

// Build a 2D RGBA8 texture.
GLuint gl_make_texture_rgba8(int w, int h, const std::uint8_t* data,
                             GLint min_filter = GL_LINEAR,
                             GLint mag_filter = GL_LINEAR,
                             GLint wrap = GL_REPEAT);

// Build a single-channel R8 texture.
GLuint gl_make_texture_r8(int w, int h, const std::uint8_t* data,
                          GLint filter = GL_NEAREST,
                          GLint wrap = GL_REPEAT);

// Render-to-texture target.
struct FBO {
    GLuint fbo = 0;
    GLuint color = 0;
    int    w = 0, h = 0;
    bool create_rgba8(int w, int h, GLint filter = GL_LINEAR);
    void  bind() const;
    void  destroy();
};

// Read RGBA8 texels from current FBO.
void gl_read_pixels_rgba8(int w, int h, std::uint8_t* out);

inline void gl_check(const char* where) {
#ifndef NDEBUG
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        std::fprintf(stderr, "[GL] %s err=0x%04x\n", where, (unsigned)e);
    }
#else
    (void)where;
#endif
}

} // namespace sm
