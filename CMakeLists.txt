cmake_minimum_required(VERSION 3.16)
project(samosbor LANGUAGES C CXX)

# Set CMP0135 to NEW behavior to avoid FetchContent timestamp warnings
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============= Build Options =============
option(SAMOSBOR_ENABLE_DEBUG_UI "Enable ImGui debug/profiling UI (F3 to toggle)" ON)

# Detect Emscripten
if(EMSCRIPTEN)
    message(STATUS "Building for Emscripten (WebAssembly)")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

# FetchContent for downloading dependencies
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ============= Sokol headers =============
FetchContent_Declare(
    sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sokol)

# Create sokol interface library
add_library(sokol_lib INTERFACE)
target_include_directories(sokol_lib SYSTEM INTERFACE 
    ${sokol_SOURCE_DIR}
    ${sokol_SOURCE_DIR}/util
)

# ============= stb (for stb_image, stb_truetype) =============
FetchContent_Declare(
    stb
    URL https://github.com/nothings/stb/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(stb)

add_library(stb_lib INTERFACE)
target_include_directories(stb_lib SYSTEM INTERFACE ${stb_SOURCE_DIR})

# ============= fontstash =============
FetchContent_Declare(
    fontstash
    GIT_REPOSITORY https://github.com/memononen/fontstash.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fontstash)

add_library(fontstash_lib INTERFACE)
target_include_directories(fontstash_lib SYSTEM INTERFACE ${fontstash_SOURCE_DIR}/src)

# ============= EnTT Dependency =============
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(EnTT)

# ============= Miniaudio (Desktop audio) =============
if(NOT EMSCRIPTEN)
    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.24
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(miniaudio)
endif()

# ============= ImGui Dependency (Optional) =============
if(SAMOSBOR_ENABLE_DEBUG_UI)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)
    
    # ImGui core sources (without SDL backends - we use sokol_imgui)
    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    target_include_directories(imgui_lib SYSTEM PUBLIC ${imgui_SOURCE_DIR})
endif()

# Source files
set(SOURCES
    src/app/main.cpp
    src/app/sokol.c
    src/core/game_context.cpp
    src/rendering/texture_manager.cpp
    src/rendering/text_renderer.cpp
    src/rendering/renderer.cpp
    src/systems/player.cpp
    src/systems/random_events.cpp
    src/states/gen_state.cpp
    src/states/play_state.cpp
    src/states/battle_state.cpp
    src/states/labyrinth_state.cpp
    src/systems/world_manager.cpp
    src/systems/landmark.cpp
    src/states/interaction_state.cpp
    src/ecs/systems/ai_system.cpp
    src/states/stat_state.cpp
    src/states/map_state.cpp
    src/systems/resource_generator.cpp
    src/ui/ui.cpp
)

# Audio implementation (combined file with platform-specific compilation)
list(APPEND SOURCES src/rendering/audio_impl.cpp)

# Debug UI sources (only when enabled)
if(SAMOSBOR_ENABLE_DEBUG_UI)
    list(APPEND SOURCES
        src/app/sokol_imgui_impl.cpp
        src/debug/debug_ui.cpp
    )
endif()

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    sokol_lib
    stb_lib
    fontstash_lib
    EnTT::EnTT
)

# ImGui when debug UI is enabled
if(SAMOSBOR_ENABLE_DEBUG_UI)
    target_link_libraries(${PROJECT_NAME} PRIVATE imgui_lib)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SAMOSBOR_DEBUG_UI)
endif()

# EnTT configuration for no-RTTI builds
target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NOEXCEPTION)

# Platform-specific settings
if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SOKOL_GLES3 __EMSCRIPTEN__)
    if(SAMOSBOR_ENABLE_DEBUG_UI)
        target_compile_definitions(imgui_lib PRIVATE SOKOL_GLES3)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NO_ETO)
    
    # Emscripten-specific settings for WebGL2
    set(EMSCRIPTEN_FLAGS "-sUSE_WEBGL2=1 -sALLOW_MEMORY_GROWTH=1 -sNO_EXIT_RUNTIME=1")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} -sFULL_ES3=1")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} --preload-file ${CMAKE_SOURCE_DIR}/src/assets@/assets")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} --shell-file ${CMAKE_SOURCE_DIR}/shell.html")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} -sEXPORTED_RUNTIME_METHODS=ccall,cwrap")
    set(EMSCRIPTEN_FLAGS "${EMSCRIPTEN_FLAGS} -sASYNCIFY -lidbfs.js")
    
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EMSCRIPTEN_FLAGS}"
    )
    
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-literal-operator -Wno-ignored-qualifiers)
    
elseif(WIN32)
    # Windows: Use D3D11 backend
    target_compile_definitions(${PROJECT_NAME} PRIVATE SOKOL_D3D11)
    if(SAMOSBOR_ENABLE_DEBUG_UI)
        target_compile_definitions(imgui_lib PRIVATE SOKOL_D3D11)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi kernel32 user32 gdi32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    
elseif(APPLE)
    # macOS: Use Metal backend
    target_compile_definitions(${PROJECT_NAME} PRIVATE SOKOL_METAL)
    if(SAMOSBOR_ENABLE_DEBUG_UI)
        target_compile_definitions(imgui_lib PRIVATE SOKOL_METAL)
    endif()
    find_library(COCOA_LIBRARY Cocoa)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
    find_library(METAL_LIBRARY Metal)
    find_library(METALKIT_LIBRARY MetalKit)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COCOA_LIBRARY}
        ${QUARTZCORE_LIBRARY}
        ${METAL_LIBRARY}
        ${METALKIT_LIBRARY}
    )
    target_compile_options(${PROJECT_NAME} PRIVATE -x objective-c++)
    
elseif(UNIX)
    # Linux: Use OpenGL backend with EGL for Wayland high-DPI support
    target_compile_definitions(${PROJECT_NAME} PRIVATE SOKOL_GLCORE SOKOL_FORCE_EGL)
    if(SAMOSBOR_ENABLE_DEBUG_UI)
        target_compile_definitions(imgui_lib PRIVATE SOKOL_GLCORE)
    endif()
    find_package(PkgConfig REQUIRED)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(X11 REQUIRED x11 xi xcursor)
    pkg_check_modules(EGL REQUIRED egl)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        OpenGL::GL
        ${X11_LIBRARIES}
        ${EGL_LIBRARIES}
        pthread dl m
    )
    target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIRS} ${EGL_INCLUDE_DIRS})
endif()

# Add miniaudio include path for desktop (header-only library)
if(NOT EMSCRIPTEN)
    target_include_directories(${PROJECT_NAME} PRIVATE ${miniaudio_SOURCE_DIR})
endif()

# Compiler warnings and optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
    # Disable sokol validation in release builds for better performance
    target_compile_definitions(${PROJECT_NAME} PRIVATE NDEBUG)
    if(EMSCRIPTEN)
        target_compile_options(${PROJECT_NAME} PRIVATE -msimd128 -flto)
        target_link_options(${PROJECT_NAME} PRIVATE -flto)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-omit-frame-pointer)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -fno-rtti -fno-exceptions)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-literal-operator -Wno-ignored-qualifiers)
endif()

# Custom target to sync assets directory on every build (not needed for Emscripten)
if(NOT EMSCRIPTEN)
    add_custom_target(sync_assets ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMENT "Syncing assets directory to build..."
    )
    add_dependencies(${PROJECT_NAME} sync_assets)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Samosbor Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Debug UI: ${SAMOSBOR_ENABLE_DEBUG_UI}")
message(STATUS "")
