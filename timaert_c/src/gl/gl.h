// Universal OpenGL header. macOS = OpenGL 3.2 Core (built-in linkage).
// Emscripten = WebGL2/GLES3. No external loader required.
#pragma once

#ifdef __APPLE__
    #ifndef GL_SILENCE_DEPRECATION
        #define GL_SILENCE_DEPRECATION
    #endif
    #include <OpenGL/gl3.h>
    #include <OpenGL/gl3ext.h>
#elif defined(__EMSCRIPTEN__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl3ext.h>
#else
    #ifndef _WIN32
        #define GL_GLEXT_PROTOTYPES 1
    #endif
    #if __has_include(<SDL2/SDL_opengl.h>)
        #include <SDL2/SDL_opengl.h>
    #else
        #include <SDL_opengl.h>
    #endif
#endif

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
namespace sm {
bool gl_load_functions();
namespace glp {
extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLBINDBUFFERPROC BindBuffer;
extern PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
extern PFNGLBINDVERTEXARRAYPROC BindVertexArray;
extern PFNGLBUFFERDATAPROC BufferData;
extern PFNGLBUFFERSUBDATAPROC BufferSubData;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLDELETEBUFFERSPROC DeleteBuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
extern PFNGLDELETEPROGRAMPROC DeleteProgram;
extern PFNGLDELETESHADERPROC DeleteShader;
extern PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
extern PFNGLDRAWARRAYSINSTANCEDPROC DrawArraysInstanced;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
extern PFNGLGENBUFFERSPROC GenBuffers;
extern PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
extern PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
extern PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLUNIFORM1FPROC Uniform1f;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM1IVPROC Uniform1iv;
extern PFNGLUNIFORM2FPROC Uniform2f;
extern PFNGLUNIFORM3FPROC Uniform3f;
extern PFNGLUNIFORM4FPROC Uniform4f;
extern PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLVERTEXATTRIBDIVISORPROC VertexAttribDivisor;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
} // namespace glp
} // namespace sm

#define glActiveTexture sm::glp::ActiveTexture
#define glAttachShader sm::glp::AttachShader
#define glBindBuffer sm::glp::BindBuffer
#define glBindFramebuffer sm::glp::BindFramebuffer
#define glBindVertexArray sm::glp::BindVertexArray
#define glBufferData sm::glp::BufferData
#define glBufferSubData sm::glp::BufferSubData
#define glCheckFramebufferStatus sm::glp::CheckFramebufferStatus
#define glCompileShader sm::glp::CompileShader
#define glCreateProgram sm::glp::CreateProgram
#define glCreateShader sm::glp::CreateShader
#define glDeleteBuffers sm::glp::DeleteBuffers
#define glDeleteFramebuffers sm::glp::DeleteFramebuffers
#define glDeleteProgram sm::glp::DeleteProgram
#define glDeleteShader sm::glp::DeleteShader
#define glDeleteVertexArrays sm::glp::DeleteVertexArrays
#define glDisableVertexAttribArray sm::glp::DisableVertexAttribArray
#define glDrawArraysInstanced sm::glp::DrawArraysInstanced
#define glEnableVertexAttribArray sm::glp::EnableVertexAttribArray
#define glFramebufferTexture2D sm::glp::FramebufferTexture2D
#define glGenBuffers sm::glp::GenBuffers
#define glGenFramebuffers sm::glp::GenFramebuffers
#define glGenVertexArrays sm::glp::GenVertexArrays
#define glGetProgramInfoLog sm::glp::GetProgramInfoLog
#define glGetProgramiv sm::glp::GetProgramiv
#define glGetShaderInfoLog sm::glp::GetShaderInfoLog
#define glGetShaderiv sm::glp::GetShaderiv
#define glGetUniformLocation sm::glp::GetUniformLocation
#define glLinkProgram sm::glp::LinkProgram
#define glShaderSource sm::glp::ShaderSource
#define glUniform1f sm::glp::Uniform1f
#define glUniform1i sm::glp::Uniform1i
#define glUniform1iv sm::glp::Uniform1iv
#define glUniform2f sm::glp::Uniform2f
#define glUniform3f sm::glp::Uniform3f
#define glUniform4f sm::glp::Uniform4f
#define glUniformMatrix4fv sm::glp::UniformMatrix4fv
#define glUseProgram sm::glp::UseProgram
#define glVertexAttribDivisor sm::glp::VertexAttribDivisor
#define glVertexAttribPointer sm::glp::VertexAttribPointer
#endif
