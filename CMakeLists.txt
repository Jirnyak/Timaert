cmake_minimum_required(VERSION 3.16)
project(timaert LANGUAGES CXX)

if(MSVC)
    # This bundled CMake emits GNU-style `-std:` flags for MSVC. Override the
    # standard option table before targets are created so cl receives native
    # `/std:` flags without override warnings.
    set(CMAKE_CXX17_STANDARD_COMPILE_OPTION "/std:c++17")
    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "/std:c++20")
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++latest")
    set(CMAKE_CXX17_EXTENSION_COMPILE_OPTION "/std:c++17")
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION "/std:c++20")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++latest")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TIMAERT_DEBUG_UI "Enable extra debug overlays; ImGui is required by the app shell" ON)

# ---- Performance / debugging build knobs (see debug.md) ----------------------
# Optimization and CPU time are the top priority. Optimized configs use -O3 +
# LTO, and the game binary adds fast-math (see the game target below). Legacy
# save, cross-build, cross-platform and TS-seed determinism are explicit
# non-goals; within one binary a seed still reproduces the same world (fast-math
# is deterministic per build), which is all save/load regen needs. Compile speed
# is irrelevant here.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TIMAERT_NATIVE "Tune for the local CPU (dev only; unsafe for shipped binaries)" OFF)
option(TIMAERT_ASAN "Build with Address + UB sanitizers" OFF)

if(NOT MSVC)
    add_compile_options($<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2>)
    if(TIMAERT_NATIVE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            add_compile_options(-mcpu=native)
        else()
            add_compile_options(-march=native)
        endif()
    endif()
    if(TIMAERT_ASAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
            -fno-sanitize=vptr -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
else()
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob3>)
    if(TIMAERT_ASAN)
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    endif()
endif()

# Link-time optimization for optimized configs (slow to link, faster at runtime).
include(CheckIPOSupported)
check_ipo_supported(RESULT _timaert_ipo OUTPUT _timaert_ipo_msg)
if(_timaert_ipo)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Use already populated FetchContent dependencies without network updates during incremental builds" FORCE)
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(EnTT)

# stb (single-header image / etc.) — needed for sprite PNG loading.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

set(TIMAERT_SDL2_MIXER_TARGET "")
if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    set(TIMAERT_SDL2_INCLUDE_DIRS "")
    if(TARGET SDL2::SDL2)
        get_target_property(_TIMAERT_SDL2_TARGET_INCLUDES
            SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        if(_TIMAERT_SDL2_TARGET_INCLUDES)
            list(APPEND TIMAERT_SDL2_INCLUDE_DIRS
                ${_TIMAERT_SDL2_TARGET_INCLUDES})
        endif()
    endif()
    if(TIMAERT_SDL2_INCLUDE_DIRS STREQUAL "" AND SDL2_DIR)
        get_filename_component(_TIMAERT_SDL2_ROOT "${SDL2_DIR}/.." ABSOLUTE)
        if(EXISTS "${_TIMAERT_SDL2_ROOT}/include/SDL2/SDL_opengl.h"
           OR EXISTS "${_TIMAERT_SDL2_ROOT}/include/SDL_opengl.h")
            list(APPEND TIMAERT_SDL2_INCLUDE_DIRS
                "${_TIMAERT_SDL2_ROOT}/include")
        endif()
    endif()
    if(NOT TIMAERT_SDL2_INCLUDE_DIRS STREQUAL "")
        include_directories(SYSTEM ${TIMAERT_SDL2_INCLUDE_DIRS})
    endif()
    find_package(SDL2_mixer CONFIG QUIET)
    if(TARGET SDL2_mixer::SDL2_mixer)
        set(TIMAERT_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer)
    elseif(TARGET SDL2_mixer::SDL2_mixer-static)
        set(TIMAERT_SDL2_MIXER_TARGET SDL2_mixer::SDL2_mixer-static)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2_MIXER QUIET IMPORTED_TARGET SDL2_mixer)
            if(TARGET PkgConfig::SDL2_MIXER)
                set(TIMAERT_SDL2_MIXER_TARGET PkgConfig::SDL2_MIXER)
            endif()
        endif()
    endif()
    if(TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
        message(FATAL_ERROR
            "SDL_mixer dependency missing: native audio requires SDL2_mixer. "
            "Install SDL2_mixer or build the Emscripten target.")
    endif()
    # Vulkan is the rendering/compute backend (macOS via MoltenVK). SDL stays
    # platform/input/audio only. See ARCHITECTURE.md Rendering & Compute Backend.
    find_package(Vulkan REQUIRED)
endif()

file(GLOB_RECURSE SAMOSBOR_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/core/*.cpp src/gpu/*.cpp src/ecs/*.cpp
    src/macro/*.cpp src/sub/*.cpp src/events/*.cpp
    src/content/*.cpp src/ui/*.cpp src/assets/*.cpp)

# GPU (Vulkan) sources, also reused standalone by the gpu_smoke bring-up target.
file(GLOB TIMAERT_GPU_SOURCES CONFIGURE_DEPENDS src/gpu/*.cpp)

add_executable(${PROJECT_NAME} ${SAMOSBOR_SOURCES})
if(EMSCRIPTEN)
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
endif()

target_sources(${PROJECT_NAME} PRIVATE ${IMGUI_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(TIMAERT_DEBUG_UI)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TIMAERT_DEBUG_UI)
endif()


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${stb_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE EnTT::EnTT)
target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NOEXCEPTION)
if(NOT TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TIMAERT_HAS_SDL_MIXER=1)
endif()

# Asset symlink: build/assets -> ../public/assets (TS reference assets).
set(_TIMAERT_TS_ASSETS "${CMAKE_SOURCE_DIR}/assets")
if(EXISTS "${_TIMAERT_TS_ASSETS}")
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/assets")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_TIMAERT_TS_ASSETS}" "${CMAKE_BINARY_DIR}/assets")
    endif()
endif()

if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 Vulkan::Vulkan)
    if(NOT TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
        target_link_libraries(${PROJECT_NAME} PRIVATE ${TIMAERT_SDL2_MIXER_TARGET})
    endif()
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>)
        if(TARGET SDL2_mixer::SDL2_mixer)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2_mixer::SDL2_mixer>
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>)
        endif()
    endif()
endif()

if(MSVC)
    foreach(_timaert_flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REPLACE "/GR" "" ${_timaert_flag_var} "${${_timaert_flag_var}}")
        string(REPLACE "/EHsc" "" ${_timaert_flag_var} "${${_timaert_flag_var}}")
    endforeach()
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /GR- /EHs-c- /fp:fast)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions
        # Fast-math for the shipped game only (tests keep strict math for stable
        # baselines). -fno-finite-math-only keeps NaN/Inf checks working.
        -ffast-math -fno-finite-math-only)
endif()

# Vulkan bring-up verification target (fast compile/run loop for the migration).
if(NOT EMSCRIPTEN)
    # Compile GLSL -> SPIR-V with glslc (located by find_package(Vulkan)).
    set(_timaert_spv_dir ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${_timaert_spv_dir})
    set(_timaert_spv_files "")
    set(_timaert_shader_incs
        ${CMAKE_SOURCE_DIR}/shaders/tree_sprite.glsl
        ${CMAKE_SOURCE_DIR}/shaders/npc_sprite.glsl
        ${CMAKE_SOURCE_DIR}/shaders/creature_sprite.glsl
        ${CMAKE_SOURCE_DIR}/shaders/shadow_common.glsl
        ${CMAKE_SOURCE_DIR}/shaders/lighting.glsl)
    foreach(_sh fullscreen.vert macro.frag mesh.vert mesh.frag billboard.vert billboard.frag sky.frag shadow_mesh.vert shadow_mesh.frag shadow_bb.vert shadow_bb.frag water.vert water.frag struct.vert struct.frag shadow_struct.vert shadow_struct.frag npc.vert npc.frag shadow_npc.vert shadow_npc.frag creature.vert creature.frag shadow_creature.vert shadow_creature.frag particle.vert particle.frag)
        add_custom_command(
            OUTPUT ${_timaert_spv_dir}/${_sh}.spv
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} -O
                ${CMAKE_SOURCE_DIR}/shaders/${_sh}
                -o ${_timaert_spv_dir}/${_sh}.spv
            DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${_sh} ${_timaert_shader_incs}
            COMMENT "glslc ${_sh}")
        list(APPEND _timaert_spv_files ${_timaert_spv_dir}/${_sh}.spv)
    endforeach()
    add_custom_target(timaert_shaders DEPENDS ${_timaert_spv_files})
    add_dependencies(${PROJECT_NAME} timaert_shaders)

    add_executable(gpu_smoke tests/gpu_smoke.cpp ${TIMAERT_GPU_SOURCES}
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    add_dependencies(gpu_smoke timaert_shaders)
    set_target_properties(gpu_smoke PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_include_directories(gpu_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(gpu_smoke PRIVATE SDL2::SDL2 Vulkan::Vulkan)
    if(NOT MSVC)
        target_compile_options(gpu_smoke PRIVATE -fno-rtti -fno-exceptions)
    endif()

    # Phase 5 subworld 3D foundation harness (depth + mesh pipeline + camera).
    add_executable(gpu_smoke3d tests/gpu_smoke3d.cpp ${TIMAERT_GPU_SOURCES})
    add_dependencies(gpu_smoke3d timaert_shaders)
    set_target_properties(gpu_smoke3d PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_include_directories(gpu_smoke3d PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(gpu_smoke3d PRIVATE SDL2::SDL2 Vulkan::Vulkan)
    if(NOT MSVC)
        target_compile_options(gpu_smoke3d PRIVATE -fno-rtti -fno-exceptions)
    endif()

    # Offscreen macro-map screenshot tool: renders the shipping macro.frag via
    # MacroRendererVk into an offscreen image and dumps PPM frames (headless
    # visual-regression capture for the 2D world map). Faithful mountain mask is
    # derived in-tool, so it links only the GPU + macro-render + terrain synth
    # sources -- no spawners/politik/pathfinding chain.
    add_executable(macro_shot tests/macro_shot.cpp ${TIMAERT_GPU_SOURCES}
        src/macro/vk_macro_renderer.cpp
        src/macro/map_generator.cpp)
    add_dependencies(macro_shot timaert_shaders)
    set_target_properties(macro_shot PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_include_directories(macro_shot PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(macro_shot PRIVATE SDL2::SDL2 Vulkan::Vulkan)
    if(NOT MSVC)
        target_compile_options(macro_shot PRIVATE -fno-rtti -fno-exceptions)
    endif()
endif()

add_executable(quest_lifecycle_test
    tests/quest_lifecycle_test.cpp
    src/events/event_bus.cpp
    src/events/effect_applicator.cpp
    src/events/logic_nodes.cpp
    src/events/node_registry.cpp
    src/events/quests/quest_engine.cpp
    src/content/plot/encounters.cpp
    src/content/plot/intro.cpp
    src/content/quests/procedural.cpp)
set_target_properties(quest_lifecycle_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(quest_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(quest_lifecycle_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(quest_lifecycle_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(quest_lifecycle_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(quest_lifecycle_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(quest_lifecycle_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(world_tick_parity_test
    tests/world_tick_parity_test.cpp
    src/macro/world_tick.cpp
    src/macro/economy.cpp)
set_target_properties(world_tick_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(world_tick_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(world_tick_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(world_tick_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(world_tick_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(world_tick_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(player_recovery_parity_test
    tests/player_recovery_parity_test.cpp
    src/macro/player_recovery.cpp)
set_target_properties(player_recovery_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(player_recovery_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(player_recovery_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(player_recovery_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(player_recovery_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(player_recovery_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(save_roundtrip_test
    tests/save_roundtrip_test.cpp
    src/events/event_bus.cpp
    src/events/quests/quest_engine.cpp
    src/macro/save.cpp)
set_target_properties(save_roundtrip_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(save_roundtrip_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(save_roundtrip_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(save_roundtrip_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(save_roundtrip_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(save_roundtrip_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(save_roundtrip_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(spell_casting_effects_test
    tests/spell_casting_effects_test.cpp
    src/events/event_bus.cpp
    src/content/spells/spell_book.cpp
    src/content/spells/spell_types.cpp
    src/content/spells/registry.cpp
    src/sub/spell_effects.cpp)
set_target_properties(spell_casting_effects_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(spell_casting_effects_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(spell_casting_effects_test PRIVATE EnTT::EnTT)
target_compile_definitions(spell_casting_effects_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(spell_casting_effects_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(spell_casting_effects_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(spell_casting_effects_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(combat_squad_test
    tests/combat_squad_test.cpp
    src/sub/ai.cpp
    src/sub/spawn.cpp
    src/sub/fauna.cpp)
set_target_properties(combat_squad_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(combat_squad_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(combat_squad_test PRIVATE EnTT::EnTT)
target_compile_definitions(combat_squad_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(combat_squad_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(combat_squad_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(combat_squad_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(audio_contract_test
    tests/audio_contract_test.cpp
    src/macro/audio.cpp)
set_target_properties(audio_contract_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(audio_contract_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(audio_contract_test PRIVATE SDL2::SDL2)
    if(NOT TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
        target_link_libraries(audio_contract_test PRIVATE ${TIMAERT_SDL2_MIXER_TARGET})
        target_compile_definitions(audio_contract_test PRIVATE TIMAERT_HAS_SDL_MIXER=1)
    endif()
endif()
target_compile_definitions(audio_contract_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(audio_contract_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(audio_contract_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(audio_contract_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(audio_runtime_test
    tests/audio_runtime_test.cpp
    src/macro/audio.cpp)
set_target_properties(audio_runtime_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(audio_runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(audio_runtime_test PRIVATE SDL2::SDL2)
    if(NOT TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
        target_link_libraries(audio_runtime_test PRIVATE ${TIMAERT_SDL2_MIXER_TARGET})
        target_compile_definitions(audio_runtime_test PRIVATE TIMAERT_HAS_SDL_MIXER=1)
    endif()
endif()
target_compile_definitions(audio_runtime_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(audio_runtime_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(audio_runtime_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(audio_runtime_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(npc_spawn_contract_test
    tests/npc_spawn_contract_test.cpp
    src/macro/npc_spawn.cpp
    src/macro/items.cpp)
set_target_properties(npc_spawn_contract_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(npc_spawn_contract_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(npc_spawn_contract_test PRIVATE EnTT::EnTT)
if(NOT EMSCRIPTEN)
    target_link_libraries(npc_spawn_contract_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(npc_spawn_contract_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(npc_spawn_contract_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(npc_spawn_contract_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(npc_spawn_contract_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(macro_npc_ai_parity_test
    tests/macro_npc_ai_parity_test.cpp
    src/macro/npc_ai.cpp)
set_target_properties(macro_npc_ai_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(macro_npc_ai_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(macro_npc_ai_parity_test PRIVATE EnTT::EnTT)
target_compile_definitions(macro_npc_ai_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(macro_npc_ai_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(macro_npc_ai_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(macro_npc_ai_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(macro_travel_parity_test
    tests/macro_travel_parity_test.cpp
    src/macro/travel.cpp
    src/macro/items.cpp)
set_target_properties(macro_travel_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(macro_travel_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(macro_travel_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(macro_travel_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(macro_travel_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(macro_travel_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(item_use_parity_test
    tests/item_use_parity_test.cpp
    src/macro/items.cpp)
set_target_properties(item_use_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(item_use_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(item_use_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(item_use_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(item_use_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(item_use_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(pathfinding_parity_test
    tests/pathfinding_parity_test.cpp
    src/macro/pathfinding.cpp
    src/macro/zones.cpp)
set_target_properties(pathfinding_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(pathfinding_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(pathfinding_parity_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(pathfinding_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(pathfinding_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(pathfinding_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(pathfinding_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(character_paperdoll_test
    tests/character_paperdoll_test.cpp
    src/assets/character_paperdoll.cpp)
set_target_properties(character_paperdoll_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(character_paperdoll_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(character_paperdoll_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(character_paperdoll_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(character_paperdoll_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(character_paperdoll_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()



add_executable(road_river_generation_test
    tests/road_river_generation_test.cpp
    src/macro/spawners.cpp
    src/macro/pathfinding.cpp
    src/macro/politik.cpp
    src/macro/language.cpp)
set_target_properties(road_river_generation_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(road_river_generation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(road_river_generation_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(road_river_generation_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(road_river_generation_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(road_river_generation_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(road_river_generation_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# River generation invariants (macro heap-A* trace + below-sea carve) and the
# honest subworld river descent. Pure CPU synth: generate_terrain() and
# generate_heightmap() need no GPU, so this target links neither SDL2 nor Vulkan.
add_executable(river_generation_test
    tests/river_generation_test.cpp
    src/macro/map_generator.cpp
    src/sub/base_generator.cpp)
set_target_properties(river_generation_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(river_generation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(river_generation_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(river_generation_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(river_generation_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(river_generation_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(feature_layer_parity_test
    tests/feature_layer_parity_test.cpp
    src/macro/spawners.cpp)
set_target_properties(feature_layer_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(feature_layer_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(feature_layer_parity_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(feature_layer_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(feature_layer_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(feature_layer_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(feature_layer_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Header-only classifier test (biomes.h): mountains are a biome by elevation.
add_executable(biome_classifier_test
    tests/biome_classifier_test.cpp)
set_target_properties(biome_classifier_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(biome_classifier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(biome_classifier_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(biome_classifier_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(biome_classifier_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(biome_classifier_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(biome_classifier_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(subworld_generator_parity_test
    tests/subworld_generator_parity_test.cpp
    src/sub/base_generator.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(subworld_generator_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(subworld_generator_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(subworld_generator_parity_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(subworld_generator_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(subworld_generator_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(subworld_generator_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(subworld_generator_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(city_distribution_test
    tests/city_distribution_test.cpp
    src/sub/base_generator.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(city_distribution_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(city_distribution_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(city_distribution_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(city_distribution_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(city_distribution_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(city_distribution_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(city_distribution_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(house_pad_flatten_test
    tests/house_pad_flatten_test.cpp
    src/sub/base_generator.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(house_pad_flatten_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(house_pad_flatten_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(house_pad_flatten_test PRIVATE SDL2::SDL2)
endif()
target_compile_definitions(house_pad_flatten_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(house_pad_flatten_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(house_pad_flatten_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(house_pad_flatten_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(mountain_mesh_smoothness_test
    tests/mountain_mesh_smoothness_test.cpp
    src/sub/base_generator.cpp)
set_target_properties(mountain_mesh_smoothness_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(mountain_mesh_smoothness_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(mountain_mesh_smoothness_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(mountain_mesh_smoothness_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(mountain_mesh_smoothness_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(mountain_mesh_smoothness_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(subworld_async_seam_test
    tests/subworld_async_seam_test.cpp
    src/sub/seamless_manager.cpp
    src/sub/base_generator.cpp
    src/sub/map_factory.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(subworld_async_seam_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(subworld_async_seam_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(subworld_async_seam_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(subworld_async_seam_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(subworld_async_seam_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(subworld_async_seam_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(subworld_spawn_parity_test
    tests/subworld_spawn_parity_test.cpp
    src/sub/spawn.cpp
    src/sub/fauna.cpp
    src/sub/seamless_manager.cpp
    src/sub/base_generator.cpp
    src/sub/map_factory.cpp
    src/sub/gens/dispatch.cpp
    src/macro/player_entity.cpp)
set_target_properties(subworld_spawn_parity_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(subworld_spawn_parity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(subworld_spawn_parity_test PRIVATE EnTT::EnTT)
target_compile_definitions(subworld_spawn_parity_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(subworld_spawn_parity_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(subworld_spawn_parity_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(subworld_spawn_parity_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Pure-logic unit tests for the RPG spine (character_sheet.h) + unified loot
# table (items.cpp). Header-only sheet code needs no extra deps; the loot
# functions live in items.cpp (self-contained — no SDL2/EnTT, same as
# item_use_parity_test).
add_executable(rpg_loot_test
    tests/rpg_loot_test.cpp
    src/macro/items.cpp)
set_target_properties(rpg_loot_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(rpg_loot_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(rpg_loot_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(rpg_loot_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(rpg_loot_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(rpg_loot_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Pure targeting primitive (Inc 5 possession groundwork). Links only the new
# targeting TU + EnTT — no engine/render code, so it stays independent of the
# subworld renderer.
add_executable(targeting_test
    tests/targeting_test.cpp
    src/sub/targeting.cpp)
set_target_properties(targeting_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(targeting_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(targeting_test PRIVATE EnTT::EnTT)
target_compile_definitions(targeting_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(targeting_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(targeting_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(targeting_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Global creature-registry contract (fauna.cpp). Self-contained data + lookups;
# no SDL2/EnTT, same as rpg_loot_test.
add_executable(fauna_registry_test
    tests/fauna_registry_test.cpp
    src/sub/fauna.cpp)
set_target_properties(fauna_registry_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(fauna_registry_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fauna_registry_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(fauna_registry_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(fauna_registry_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(fauna_registry_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Universal UI settings registry (ui_settings.cpp): spec-seeded defaults, the
# forgiving text-KV load/save round-trip, unknown-key / out-of-range tolerance,
# and reset_defaults(). Pure logic — it never opens a window, so it links only
# ui_settings.cpp plus the ImGui core TUs whose symbols that TU references
# (draw_ui_settings_panel is compiled but never called). No SDL2/Vulkan/EnTT.
add_executable(ui_settings_test
    tests/ui_settings_test.cpp
    src/ui/ui_settings.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
set_target_properties(ui_settings_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(ui_settings_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${imgui_SOURCE_DIR})
if(MSVC)
    target_compile_options(ui_settings_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(ui_settings_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ui_settings_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Universal macroworld night-lighting bake (macro_lighting.cpp): the data-driven
# emitter census (collect_macro_lights — settlements/villages/active spires, the
# depleted-spire and lightColor==0 opt-outs, population-scaled radius/intensity)
# and the radial glow-field rasteriser (bake_light_field — falloff, radial
# symmetry, torus wrap, colour fidelity, clamp/encode to kMacroGlowCeil). Pure
# CPU: the landmark table is header-inline, so it links only the lighting TU.
add_executable(macro_lighting_test
    tests/macro_lighting_test.cpp
    src/macro/macro_lighting.cpp)
set_target_properties(macro_lighting_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(macro_lighting_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT EMSCRIPTEN)
    target_link_libraries(macro_lighting_test PRIVATE SDL2::SDL2)
endif()
target_link_libraries(macro_lighting_test PRIVATE EnTT::EnTT)
target_compile_definitions(macro_lighting_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(macro_lighting_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(macro_lighting_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(macro_lighting_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Faction-relations data contract behind the universal subworld hostility
# relation (sub/engine.cpp entities_hostile). Links macro/state.cpp + its
# generation deps; no renderer/SDL. Proves demons/bandits are hostile to
# kingdoms while wildlife stays neutral and the matrix is symmetric.
add_executable(faction_relations_test
    tests/faction_relations_test.cpp
    src/macro/state.cpp
    src/macro/map_generator.cpp
    src/macro/language.cpp
    src/macro/politik.cpp
    src/macro/items.cpp
    src/macro/economy.cpp)
set_target_properties(faction_relations_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(faction_relations_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(faction_relations_test PRIVATE EnTT::EnTT)
target_compile_definitions(faction_relations_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(faction_relations_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(faction_relations_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(faction_relations_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Seasons data contract (macro/seasons.h). Header-only, pure derivation of the
# world day — no macro/renderer sources needed. Proves the season cycle starts
# at Spring on day 1, is periodic and total (wraps non-positive days), and that
# the temperature offsets are ordered the way the foliage consumer relies on.
add_executable(seasons_test tests/seasons_test.cpp)
set_target_properties(seasons_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(seasons_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(seasons_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(seasons_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(seasons_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(torus_geometry_test tests/torus_geometry_test.cpp)
set_target_properties(torus_geometry_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(torus_geometry_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(torus_geometry_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(torus_geometry_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(torus_geometry_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(celestial_test tests/celestial_test.cpp)
set_target_properties(celestial_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(celestial_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(celestial_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(celestial_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(celestial_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(point_light_cull_test tests/point_light_cull_test.cpp)
set_target_properties(point_light_cull_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(point_light_cull_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(point_light_cull_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(point_light_cull_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(point_light_cull_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(carried_light_spawn_test
    tests/carried_light_spawn_test.cpp
    src/sub/spawn.cpp
    src/sub/fauna.cpp
    src/sub/seamless_manager.cpp
    src/sub/base_generator.cpp
    src/sub/map_factory.cpp
    src/sub/gens/dispatch.cpp
    src/macro/player_entity.cpp)
set_target_properties(carried_light_spawn_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(carried_light_spawn_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(carried_light_spawn_test PRIVATE EnTT::EnTT)
target_compile_definitions(carried_light_spawn_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(carried_light_spawn_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(carried_light_spawn_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(carried_light_spawn_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Particle-sim contract (FX Inc A): the transient-VFX pool is Vulkan-free by
# design, so it links only particles.cpp + core and asserts every numeric
# invariant of the sim (table, emit, lifecycle, physics, pack, determinism).
add_executable(particle_sim_test
    tests/particle_sim_test.cpp
    src/sub/particles.cpp)
set_target_properties(particle_sim_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(particle_sim_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(particle_sim_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(particle_sim_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(particle_sim_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# Mass-battle AI contract: the steering module is Vulkan-free and ECS-free by
# design (that is what makes 16k-body combat testable headless), so it links
# only battle.cpp. Pins the no-collapse invariant with a negative control and
# MEASURES the O(N) bound instead of asserting it in a comment.
add_executable(battle_ai_test
    tests/battle_ai_test.cpp
    src/sub/battle.cpp)
set_target_properties(battle_ai_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(battle_ai_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(battle_ai_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(battle_ai_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(battle_ai_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

# ── CTest registration ─────────────────────────────────────────────────────
# The standalone *_test binaries were previously build-only: nothing called
# add_test(), so `ctest` in the build dir passed vacuously (registered zero
# tests). Register every logic-test executable so `ctest --output-on-failure`
# actually runs the suite. Each test returns non-zero on failure. The Vulkan
# smoke harnesses (gpu_smoke / gpu_smoke3d) and the game (timaert) need a GPU/
# display and are intentionally NOT registered here.
enable_testing()
foreach(_sm_test IN ITEMS
        quest_lifecycle_test
        world_tick_parity_test
        player_recovery_parity_test
        save_roundtrip_test
        spell_casting_effects_test
        combat_squad_test
        audio_contract_test
        audio_runtime_test
        npc_spawn_contract_test
        macro_npc_ai_parity_test
        macro_travel_parity_test
        item_use_parity_test
        pathfinding_parity_test
        character_paperdoll_test
        road_river_generation_test
        river_generation_test
        biome_classifier_test
        feature_layer_parity_test
        subworld_generator_parity_test
        city_distribution_test
        house_pad_flatten_test
        mountain_mesh_smoothness_test
        subworld_async_seam_test
        subworld_spawn_parity_test
        rpg_loot_test
        targeting_test
        fauna_registry_test
        ui_settings_test
        macro_lighting_test
        faction_relations_test
        seasons_test
        torus_geometry_test
        celestial_test
        point_light_cull_test
        carried_light_spawn_test
        particle_sim_test
        battle_ai_test)
    if(TARGET ${_sm_test})
        add_test(NAME ${_sm_test} COMMAND ${_sm_test})
    endif()
endforeach()
