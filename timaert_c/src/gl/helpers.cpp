#include "gl/helpers.h"
#include <vector>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <SDL.h>
#endif

namespace sm {

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
namespace glp {
PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
PFNGLATTACHSHADERPROC AttachShader = nullptr;
PFNGLBINDBUFFERPROC BindBuffer = nullptr;
PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
PFNGLBUFFERDATAPROC BufferData = nullptr;
PFNGLBUFFERSUBDATAPROC BufferSubData = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
PFNGLCOMPILESHADERPROC CompileShader = nullptr;
PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
PFNGLCREATESHADERPROC CreateShader = nullptr;
PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
PFNGLDELETESHADERPROC DeleteShader = nullptr;
PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = nullptr;
PFNGLDRAWARRAYSINSTANCEDPROC DrawArraysInstanced = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
PFNGLGENBUFFERSPROC GenBuffers = nullptr;
PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
PFNGLUNIFORM1FPROC Uniform1f = nullptr;
PFNGLUNIFORM1IPROC Uniform1i = nullptr;
PFNGLUNIFORM1IVPROC Uniform1iv = nullptr;
PFNGLUNIFORM2FPROC Uniform2f = nullptr;
PFNGLUNIFORM3FPROC Uniform3f = nullptr;
PFNGLUNIFORM4FPROC Uniform4f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;
PFNGLUSEPROGRAMPROC UseProgram = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC VertexAttribDivisor = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
} // namespace glp

namespace {
template <class T>
bool load_proc(T& out, const char* name) {
    out = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!out) {
        std::fprintf(stderr, "[GL] missing proc: %s\n", name);
        return false;
    }
    return true;
}
} // namespace

bool gl_load_functions() {
    bool ok = true;
    ok = load_proc(glp::ActiveTexture, "glActiveTexture") && ok;
    ok = load_proc(glp::AttachShader, "glAttachShader") && ok;
    ok = load_proc(glp::BindBuffer, "glBindBuffer") && ok;
    ok = load_proc(glp::BindFramebuffer, "glBindFramebuffer") && ok;
    ok = load_proc(glp::BindVertexArray, "glBindVertexArray") && ok;
    ok = load_proc(glp::BufferData, "glBufferData") && ok;
    ok = load_proc(glp::BufferSubData, "glBufferSubData") && ok;
    ok = load_proc(glp::CheckFramebufferStatus, "glCheckFramebufferStatus") && ok;
    ok = load_proc(glp::CompileShader, "glCompileShader") && ok;
    ok = load_proc(glp::CreateProgram, "glCreateProgram") && ok;
    ok = load_proc(glp::CreateShader, "glCreateShader") && ok;
    ok = load_proc(glp::DeleteBuffers, "glDeleteBuffers") && ok;
    ok = load_proc(glp::DeleteFramebuffers, "glDeleteFramebuffers") && ok;
    ok = load_proc(glp::DeleteProgram, "glDeleteProgram") && ok;
    ok = load_proc(glp::DeleteShader, "glDeleteShader") && ok;
    ok = load_proc(glp::DeleteVertexArrays, "glDeleteVertexArrays") && ok;
    ok = load_proc(glp::DisableVertexAttribArray, "glDisableVertexAttribArray") && ok;
    ok = load_proc(glp::DrawArraysInstanced, "glDrawArraysInstanced") && ok;
    ok = load_proc(glp::EnableVertexAttribArray, "glEnableVertexAttribArray") && ok;
    ok = load_proc(glp::FramebufferTexture2D, "glFramebufferTexture2D") && ok;
    ok = load_proc(glp::GenBuffers, "glGenBuffers") && ok;
    ok = load_proc(glp::GenFramebuffers, "glGenFramebuffers") && ok;
    ok = load_proc(glp::GenVertexArrays, "glGenVertexArrays") && ok;
    ok = load_proc(glp::GetProgramInfoLog, "glGetProgramInfoLog") && ok;
    ok = load_proc(glp::GetProgramiv, "glGetProgramiv") && ok;
    ok = load_proc(glp::GetShaderInfoLog, "glGetShaderInfoLog") && ok;
    ok = load_proc(glp::GetShaderiv, "glGetShaderiv") && ok;
    ok = load_proc(glp::GetUniformLocation, "glGetUniformLocation") && ok;
    ok = load_proc(glp::LinkProgram, "glLinkProgram") && ok;
    ok = load_proc(glp::ShaderSource, "glShaderSource") && ok;
    ok = load_proc(glp::Uniform1f, "glUniform1f") && ok;
    ok = load_proc(glp::Uniform1i, "glUniform1i") && ok;
    ok = load_proc(glp::Uniform1iv, "glUniform1iv") && ok;
    ok = load_proc(glp::Uniform2f, "glUniform2f") && ok;
    ok = load_proc(glp::Uniform3f, "glUniform3f") && ok;
    ok = load_proc(glp::Uniform4f, "glUniform4f") && ok;
    ok = load_proc(glp::UniformMatrix4fv, "glUniformMatrix4fv") && ok;
    ok = load_proc(glp::UseProgram, "glUseProgram") && ok;
    ok = load_proc(glp::VertexAttribDivisor, "glVertexAttribDivisor") && ok;
    ok = load_proc(glp::VertexAttribPointer, "glVertexAttribPointer") && ok;
    return ok;
}
#endif

GLuint gl_compile(GLenum stage, const char* src) {
    GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::size_t(len) + 1, 0);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[GL] shader compile failed:\n%s\n", log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint gl_link(const char* vsrc, const char* fsrc) {
    GLuint vs = gl_compile(GL_VERTEX_SHADER, vsrc);
    GLuint fs = gl_compile(GL_FRAGMENT_SHADER, fsrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::size_t(len) + 1, 0);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "[GL] program link failed:\n%s\n", log.data());
        glDeleteProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

void FullscreenQuad::create() {
    static const float verts[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}
void FullscreenQuad::draw() const {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
void FullscreenQuad::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vbo = vao = 0;
}

void Mesh::destroy() {
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    ebo = vbo = vao = 0;
    index_count = 0;
}

GLuint gl_make_texture_rgba8(int w, int h, const std::uint8_t* data,
                             GLint min_filter, GLint mag_filter, GLint wrap) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return t;
}

GLuint gl_make_texture_r8(int w, int h, const std::uint8_t* data,
                          GLint filter, GLint wrap) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return t;
}

bool FBO::create_rgba8(int W, int H, GLint filter) {
    w = W; h = H;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}
void FBO::bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h); }
void FBO::destroy() {
    if (color) glDeleteTextures(1, &color);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    color = fbo = 0;
}

void gl_read_pixels_rgba8(int w, int h, std::uint8_t* out) {
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

} // namespace sm
