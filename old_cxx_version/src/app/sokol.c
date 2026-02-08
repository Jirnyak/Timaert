// Sokol implementation file
// This file must be compiled as C (not C++) and contains all sokol implementations

// Backend selection is done via CMake compile definitions
// SOKOL_GLCORE, SOKOL_GLES3, SOKOL_D3D11, or SOKOL_METAL

#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_gl.h"

// fontstash implementation
// Must include fontstash.h before sokol_fontstash.h
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define SOKOL_FONTSTASH_IMPL
#include "sokol_fontstash.h"
