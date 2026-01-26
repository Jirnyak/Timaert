cmake_minimum_required(VERSION 3.16)
project(samosbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============= Build Options =============
option(SAMOSBOR_ENABLE_DEBUG_UI "Enable ImGui debug/profiling UI (F3 to toggle)" ON)

# ============= EnTT Dependency =============
include(FetchContent)
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
    
    # ImGui core sources
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    )
    
    # SDL_Renderer backend works on all platforms (desktop and Emscripten)
    list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
endif()

# Find required packages (not needed for Emscripten - uses ports)
if(NOT EMSCRIPTEN)
    set(SDL2_USE_FRAMEWORKS OFF)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    # Audio: Using miniaudio instead of SDL_mixer for better threading
endif()

# Source files
set(SOURCES
    src/app/main.cpp
    src/core/game_context.cpp
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
        src/debug/debug_ui.cpp
    )
endif()

# Create executable
if(EMSCRIPTEN)
    add_executable(${PROJECT_NAME} ${SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# Add ImGui sources when debug UI is enabled (both desktop and Emscripten)
if(SAMOSBOR_ENABLE_DEBUG_UI)
    target_sources(${PROJECT_NAME} PRIVATE ${IMGUI_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE SAMOSBOR_DEBUG_UI)
    
    if(EMSCRIPTEN)
        # Tell ImGui to use OpenGL ES 3 on Emscripten
        target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_OPENGL_ES3)
    endif()
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link libraries
if(EMSCRIPTEN)
    # Ignore warnings from third-party libraries (EnTT)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-literal-operator -Wno-ignored-qualifiers)
    # Emscripten SDL2 ports (no SDL_mixer - using Web Audio API instead)
    set(USE_FLAGS "-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sUSE_SDL_TTF=2 -sSDL2_IMAGE_FORMATS=['png']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS}")
    
    # Emscripten-specific linker flags
    target_link_options(${PROJECT_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sDISABLE_EXCEPTION_CATCHING=0
        -sASYNCIFY
        -lidbfs.js
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/src/assets@/assets"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE __EMSCRIPTEN__)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
    )
    # Add miniaudio include path (header-only library)
    target_include_directories(${PROJECT_NAME} PRIVATE ${miniaudio_SOURCE_DIR})
endif()

# Link EnTT (header-only, but provides proper include paths)
target_link_libraries(${PROJECT_NAME} PRIVATE EnTT::EnTT)

# EnTT configuration for no-RTTI builds
target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NOEXCEPTION)
if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NO_ETO)
endif()

# SDL2main is Windows-only
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
endif()

# Compiler warnings and optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
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
    # Suppress warnings from third-party libraries (EnTT)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-literal-operator -Wno-ignored-qualifiers)
endif()

# Copy resources to build directory (not needed for Emscripten - uses --preload-file)
if(NOT EMSCRIPTEN)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/assets
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMENT "Copying game resources to build directory"
    )
endif()
