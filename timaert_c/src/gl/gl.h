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
    #define GL_GLEXT_PROTOTYPES 1
    #include <SDL2/SDL_opengl.h>
#endif
