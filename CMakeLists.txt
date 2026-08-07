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
endif()

# ── THE compile flags, in ONE place, for the game AND its tests ────────────
# What this interface target buys, beyond not repeating itself: the tests are
# compiled by the same arithmetic as the shipped game. They were not, until
# 2026-08-06 — `-ffast-math` went to the game alone (under a comment saying
# "tests keep strict math for stable baselines"), so 48 of the 70 translation
# units were compiled twice with different floating-point semantics, and every
# numeric assertion in the suite described a binary nobody plays. A baseline
# that is stable but wrong is not a baseline.
#
# Owner's ruling 2026-08-06: the tests follow the game. Where a tolerance has
# to widen for FMA contraction or reassociation, it widens honestly — measured
# against what the game actually computes.
add_library(timaert_build_flags INTERFACE)
if(MSVC)
    target_compile_options(timaert_build_flags INTERFACE /W3 /GR- /EHs-c- /fp:fast)
    target_compile_definitions(timaert_build_flags INTERFACE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(timaert_build_flags INTERFACE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions
        # -fno-finite-math-only keeps NaN/Inf checks working under fast-math.
        -ffast-math -fno-finite-math-only)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE timaert_build_flags)

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
    foreach(_sh fullscreen.vert macro.frag mesh.vert mesh.frag billboard.vert billboard.frag sky.frag shadow_mesh.vert shadow_mesh.frag shadow_bb.vert shadow_bb.frag water.vert water.frag struct.vert struct.frag struct_cyl.vert shadow_cyl.vert shadow_struct.vert shadow_struct.frag npc.vert npc.frag shadow_npc.vert shadow_npc.frag creature.vert creature.frag shadow_creature.vert shadow_creature.frag particle.vert particle.frag)
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
    # Carries the real PaperdollAtlas (npc.frag set1 contract) — hence the two
    # assets TUs and the stb include dir.
    add_executable(gpu_smoke3d tests/gpu_smoke3d.cpp ${TIMAERT_GPU_SOURCES}
        src/assets/paperdoll_atlas.cpp
        src/assets/character_paperdoll.cpp)
    add_dependencies(gpu_smoke3d timaert_shaders)
    set_target_properties(gpu_smoke3d PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_include_directories(gpu_smoke3d PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${stb_SOURCE_DIR})
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
        src/macro/map_generator.cpp
        src/macro/tree_layer.cpp)
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

# ── ONE way to declare a test ──────────────────────────────────────────────
# Every logic test used to be the same 20-24 lines with a different name: 1160
# of this file's lines were that block, copied 49 times. Two things follow from
# collapsing it, and neither is about line count:
#
#   * compile flags now live in ONE place (timaert_build_flags above), so
#     "the tests are compiled like the game" is a fact rather than 49 chances
#     to differ; and
#   * add_test() happens HERE, next to the target. The old scheme kept a
#     second, hand-written list of 49 names at the bottom of this file and
#     registered them with `if(TARGET <name>)` - so a typo, or a renamed
#     target, silently dropped a test from ctest and left the suite green with
#     fewer tests than it thinks it has.
#
# A test target is `tests/<name>.cpp` plus the shipping TUs it exercises; those
# source lists (and the comments explaining WHY each test links what it links)
# are the only thing that varies, so they are the only thing spelled out.
#
#   timaert_test(<name> [SOURCES ...] [LINK_SDL] [LINK_MIXER] [LINK_IMGUI])
enable_testing()
function(timaert_test _name)
    cmake_parse_arguments(_t "LINK_SDL;LINK_MIXER;LINK_IMGUI;LINK_ENTT" "" "SOURCES" ${ARGN})
    add_executable(${_name} tests/${_name}.cpp ${_t_SOURCES})
    set_target_properties(${_name} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_include_directories(${_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${_name} PRIVATE ENTT_NOEXCEPTION)
    target_link_libraries(${_name} PRIVATE timaert_build_flags)
    if(_t_LINK_IMGUI)
        target_include_directories(${_name} PRIVATE ${imgui_SOURCE_DIR})
    endif()
    if(_t_LINK_ENTT)
        target_link_libraries(${_name} PRIVATE EnTT::EnTT)
    endif()
    if(NOT EMSCRIPTEN)
        if(_t_LINK_SDL OR _t_LINK_MIXER)
            target_link_libraries(${_name} PRIVATE SDL2::SDL2)
        endif()
        if(_t_LINK_MIXER AND NOT TIMAERT_SDL2_MIXER_TARGET STREQUAL "")
            target_link_libraries(${_name} PRIVATE ${TIMAERT_SDL2_MIXER_TARGET})
            target_compile_definitions(${_name} PRIVATE TIMAERT_HAS_SDL_MIXER=1)
        endif()
    endif()
    # Declared here, so a BUILT test is a RUN test. No second list to forget.
    add_test(NAME ${_name} COMMAND ${_name})
    # ...and remembered here, so `check` below can depend on every one of them
    # without a third list to forget.
    set_property(GLOBAL APPEND PROPERTY TIMAERT_TEST_TARGETS ${_name})
endfunction()

timaert_test(quest_lifecycle_test
    SOURCES
        src/events/event_bus.cpp
        src/events/effect_applicator.cpp
        src/events/logic_nodes.cpp
        src/events/node_registry.cpp
        src/events/quests/quest_engine.cpp
        src/content/plot/encounters.cpp
        src/content/plot/intro.cpp
        src/content/quests/procedural.cpp
    LINK_SDL)

timaert_test(world_tick_parity_test
    SOURCES
        src/macro/world_tick.cpp
        src/macro/economy.cpp)

timaert_test(player_recovery_parity_test
    SOURCES
        src/macro/player_recovery.cpp)

timaert_test(save_roundtrip_test
    SOURCES
        src/events/event_bus.cpp
        src/events/quests/quest_engine.cpp
        src/macro/save.cpp
    LINK_SDL LINK_ENTT)

timaert_test(macro_snapshot_test
    SOURCES
        src/macro/save.cpp
        src/macro/macro_snapshot.cpp
        # spawn_squad goes through make_npc — the one creation door — so the
        # test links the spawner and the loot it rolls.
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
    LINK_SDL LINK_ENTT)

timaert_test(spell_casting_effects_test
    SOURCES
        src/events/event_bus.cpp
        src/content/spells/spell_book.cpp
        src/content/spells/spell_types.cpp
        src/content/spells/registry.cpp
        # Projectile contact now asks sub/body.h for a body's width, and that reads
        # the MONSTER table (creature_def_from_kind) like melee always has — so this
        # test links the table rather than a second opinion about how wide a wolf is.
        src/sub/fauna.cpp
        src/sub/spell_effects.cpp
    LINK_ENTT)

timaert_test(combat_squad_test
    SOURCES
        src/sub/ai.cpp
        src/sub/spawn.cpp
        src/sub/fauna.cpp
    LINK_ENTT)

timaert_test(audio_contract_test
    SOURCES
        src/macro/audio.cpp
    LINK_SDL LINK_MIXER)

timaert_test(audio_runtime_test
    SOURCES
        src/macro/audio.cpp
    LINK_SDL LINK_MIXER)

timaert_test(spawn_entity_event_test
    SOURCES
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
    LINK_SDL LINK_ENTT)

timaert_test(npc_spawn_contract_test
    SOURCES
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
    LINK_SDL LINK_ENTT)

# THE macro entity is a squad (Session 15): every macro NPC carries a
# SquadRoster and is born a squad of one. Same links as the spawn contract
# above — the doctrine is pinned at the same two spawn doors.
timaert_test(squad_roster_test
    SOURCES
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
    LINK_SDL LINK_ENTT)

timaert_test(macro_npc_ai_parity_test
    SOURCES
        src/macro/npc_ai.cpp
        # The squad threat step settles auto-battles through THE ledger
        # (macro/squad.h → macro_stock), so the AI now links the rows too.
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
    LINK_ENTT)

# Squad↔squad war on the macro map (Session 15 Inc 5): perception, flee /
# pursue by the one strength law, and same-cell battles settled through the
# ledger. Same links as the parity test above — it drives the same AI.
timaert_test(squad_war_test
    SOURCES
        src/macro/npc_ai.cpp
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
        # spawn_squad (Inc 7) is born through make_npc — the one creation
        # door — so the test links the spawner and the loot it rolls.
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
    LINK_SDL LINK_ENTT)

timaert_test(macro_travel_parity_test
    SOURCES
        src/macro/travel.cpp
        src/macro/player_recovery.cpp
        src/macro/items.cpp)

# The macro march pays the map (Session 21): greedy steering on the baked
# cost grid, water avoided / forded / drowning by data, the 8-hour road
# anchor for squads. Drives the same AI as the war test, same links.
timaert_test(squad_travel_test
    SOURCES
        src/macro/npc_ai.cpp
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
    LINK_ENTT)

timaert_test(item_use_parity_test
    SOURCES
        src/macro/items.cpp)

timaert_test(pathfinding_parity_test
    SOURCES
        src/macro/pathfinding.cpp
        src/macro/zones.cpp
    LINK_SDL)

timaert_test(character_paperdoll_test
    SOURCES
        src/assets/character_paperdoll.cpp)

timaert_test(road_river_generation_test
    SOURCES
        src/macro/spawners.cpp
        src/macro/pathfinding.cpp
        src/macro/politik.cpp
        src/macro/language.cpp
    LINK_SDL)
# River generation invariants (macro heap-A* trace + below-sea carve) and the
# honest subworld river descent. Pure CPU synth: generate_terrain() and
# generate_heightmap() need no GPU, so this target links neither SDL2 nor Vulkan.

timaert_test(river_generation_test
    SOURCES
        src/macro/map_generator.cpp
        src/sub/base_generator.cpp)

timaert_test(feature_layer_parity_test
    SOURCES
        src/macro/spawners.cpp
    LINK_SDL)
# Header-only classifier test (biomes.h): mountains are a biome by elevation.

timaert_test(biome_classifier_test
    LINK_SDL)

timaert_test(subworld_generator_parity_test
    SOURCES
        src/sub/base_generator.cpp
        src/sub/gens/dispatch.cpp
    LINK_SDL)

timaert_test(tree_layer_test
    SOURCES
        src/macro/tree_layer.cpp
        src/sub/base_generator.cpp
    LINK_SDL)

timaert_test(city_distribution_test
    SOURCES
        src/sub/base_generator.cpp
        src/sub/gens/dispatch.cpp
    LINK_SDL)

timaert_test(material_seam_test
    SOURCES
        src/sub/material.cpp)

timaert_test(structure_collide_test
    SOURCES
        src/sub/collide.cpp
        src/sub/base_generator.cpp
        src/sub/gens/dispatch.cpp
    LINK_SDL)

timaert_test(house_pad_flatten_test
    SOURCES
        src/sub/base_generator.cpp
        src/sub/gens/dispatch.cpp
    LINK_SDL)

timaert_test(mountain_mesh_smoothness_test
    SOURCES
        src/sub/base_generator.cpp)

timaert_test(subworld_async_seam_test
    SOURCES
        src/sub/seamless_manager.cpp
        src/sub/base_generator.cpp
        src/sub/map_factory.cpp
        src/sub/gens/dispatch.cpp)

timaert_test(subworld_spawn_parity_test
    SOURCES
        src/sub/spawn.cpp
        src/sub/fauna.cpp
        src/sub/seamless_manager.cpp
        src/sub/base_generator.cpp
        src/sub/map_factory.cpp
        src/sub/gens/dispatch.cpp
        src/macro/player_entity.cpp
    LINK_ENTT)
# Pure-logic unit tests for the RPG spine (character_sheet.h) + unified loot
# table (items.cpp). Header-only sheet code needs no extra deps; the loot
# functions live in items.cpp (self-contained — no SDL2/EnTT, same as
# item_use_parity_test).

timaert_test(rpg_loot_test
    SOURCES
        src/macro/items.cpp)
# Pure targeting primitive (Inc 5 possession groundwork). Links only the new
# targeting TU + EnTT — no engine/render code, so it stays independent of the
# subworld renderer.

timaert_test(targeting_test
    SOURCES
        src/sub/targeting.cpp
    LINK_ENTT)
# Global creature-registry contract (fauna.cpp). Self-contained data + lookups;
# no SDL2/EnTT, same as rpg_loot_test.

timaert_test(fauna_registry_test
    SOURCES
        src/sub/fauna.cpp)
# Universal UI settings registry (ui_settings.cpp): spec-seeded defaults, the
# forgiving text-KV load/save round-trip, unknown-key / out-of-range tolerance,
# and reset_defaults(). Pure logic — it never opens a window, so it links only
# ui_settings.cpp plus the ImGui core TUs whose symbols that TU references
# (draw_ui_settings_panel is compiled but never called). No SDL2/Vulkan/EnTT.

timaert_test(ui_settings_test
    SOURCES
        src/ui/ui_settings.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    LINK_IMGUI)
# Universal macroworld night-lighting bake (macro_lighting.cpp): the data-driven
# emitter census (collect_macro_lights — settlements/villages/active spires, the
# depleted-spire and lightColor==0 opt-outs, population-scaled radius/intensity)
# and the radial glow-field rasteriser (bake_light_field — falloff, radial
# symmetry, torus wrap, colour fidelity, clamp/encode to kMacroGlowCeil). Pure
# CPU: the landmark table is header-inline, so it links only the lighting TU.

timaert_test(macro_lighting_test
    SOURCES
        src/macro/macro_lighting.cpp
    LINK_SDL LINK_ENTT)
# Faction-relations data contract behind the universal subworld hostility
# relation (sub/engine.cpp entities_hostile). Links macro/state.cpp + its
# generation deps; no renderer/SDL. Proves demons/bandits are hostile to
# kingdoms while wildlife stays neutral and the matrix is symmetric.

timaert_test(faction_relations_test
    SOURCES
        src/macro/state.cpp
        src/macro/map_generator.cpp
        src/macro/language.cpp
        src/macro/politik.cpp
        src/macro/items.cpp
        src/macro/economy.cpp
    LINK_ENTT)
# THE time ladder (core/time.h). Header-only, no world state — the clock is a
# pure function of one integer. Walks all 8192 ticks of a day to prove the
# minute and the hour can be DERIVED instead of stored: in range, gap-free,
# onto every minute, mutually consistent, invertible at every hh:mm, and with
# whole-day durations converting at zero residue.

timaert_test(time_ladder_test)
# Seasons data contract (macro/seasons.h). Header-only, pure derivation of the
# world day — no macro/renderer sources needed. Proves the season cycle starts
# at Spring on day 1, is periodic and total (wraps non-positive days), and that
# the temperature offsets are ordered the way the foliage consumer relies on.

timaert_test(seasons_test)

timaert_test(trade_law_test
    SOURCES
        src/macro/economy.cpp
        src/macro/world_tick.cpp
        src/macro/state.cpp
        src/macro/politik.cpp
        src/macro/language.cpp)

timaert_test(rpg_rules_test
    SOURCES
        src/macro/economy.cpp)

timaert_test(econ_v1_test
    SOURCES
        src/macro/econ_day.cpp
        # The one-dictionary link law: every commodity is an item-catalog row.
        src/macro/items.cpp)

timaert_test(deposit_layer_test
    SOURCES
        src/macro/deposit_layer.cpp)

timaert_test(woodcutter_gather_test
    SOURCES
        src/macro/npc_ai.cpp
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
        src/macro/npc_spawn.cpp
        src/macro/items.cpp
        src/macro/econ_day.cpp
    LINK_SDL LINK_ENTT)

timaert_test(rng_contract_test)

timaert_test(torus_geometry_test)

timaert_test(celestial_test)

timaert_test(entry_context_test)

timaert_test(point_light_cull_test)
# Settlement allegiance: one kingdom→faction resolver, and the subworld citizen
# spawn that must obey it (negative control: a Magica city spawns NO imperials).

timaert_test(settlement_faction_test
    SOURCES
        src/sub/spawn.cpp
        src/sub/fauna.cpp
        src/sub/seamless_manager.cpp
        src/sub/base_generator.cpp
        src/sub/map_factory.cpp
        src/sub/gens/dispatch.cpp
        src/macro/player_entity.cpp
    LINK_ENTT)

timaert_test(carried_light_spawn_test
    SOURCES
        src/sub/spawn.cpp
        src/sub/fauna.cpp
        src/sub/seamless_manager.cpp
        src/sub/base_generator.cpp
        src/sub/map_factory.cpp
        src/sub/gens/dispatch.cpp
        src/macro/player_entity.cpp
    LINK_ENTT)
# A settlement's people live inside the settlement: the citizen spawner must
# fill the generated town's built-up disk (sub/city_layout.h), not scatter the
# population uniformly over the whole 1024×1024 macro cell. Same link set as the
# other spawn tests — it drives the SHIPPING generator + spawn path.

timaert_test(city_population_inside_walls_test
    SOURCES
        src/sub/spawn.cpp
        src/sub/fauna.cpp
        src/sub/seamless_manager.cpp
        src/sub/base_generator.cpp
        src/sub/map_factory.cpp
        src/sub/gens/dispatch.cpp
        src/macro/player_entity.cpp
    LINK_ENTT)
# Particle-sim contract (FX Inc A): the transient-VFX pool is Vulkan-free by
# design, so it links only particles.cpp + core and asserts every numeric
# invariant of the sim (table, emit, lifecycle, physics, pack, determinism).

timaert_test(particle_sim_test
    SOURCES
        src/sub/particles.cpp)
# THE component contract every subworld humanoid must satisfy, asserted over the
# bodies the shipping spawner really makes. Written the day an invisible squad
# proved that a body can be built with a component missing and nothing complains.
timaert_test(body_contract_test
    SOURCES
        src/sub/spawn.cpp
        src/sub/material.cpp
        src/sub/fauna.cpp
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
        src/macro/items.cpp
    LINK_ENTT)

# The ledger between the layers (macro/macro_stock.{h,cpp}): what the subworld
# borrows from the map above it and how it pays back. Pure state + table, no
# renderer and no SDL — it links the stock rows, the tree layer they write
# through, and EnTT for the receipt component.
timaert_test(macro_stock_test
    SOURCES
        src/macro/macro_stock.cpp
        src/macro/tree_layer.cpp
    LINK_ENTT)

# Mass-battle AI contract: the steering module is Vulkan-free and ECS-free by
# design (that is what makes 16k-body combat testable headless), so it links
# only battle.cpp. Pins the no-collapse invariant with a negative control and
# MEASURES the O(N) bound instead of asserting it in a comment.
timaert_test(battle_ai_test
    SOURCES
        src/sub/battle.cpp)

# The agreement between the two laws of battle (macro/auto_battle.h): the
# resolver is header-only sheet math; battle.cpp is linked so the fought
# version can be simulated headless with the shipping steering.
timaert_test(auto_battle_test
    SOURCES
        src/sub/battle.cpp)

# ── CTest registration ─────────────────────────────────────────────────────
# There is nothing here any more, and that is the point: timaert_test() calls
# add_test() for every target it creates, so "built" and "registered" cannot
# come apart. The Vulkan smoke harnesses (gpu_smoke / gpu_smoke3d / macro_shot)
# and the game need a GPU and a display, so they are declared as plain
# executables above and are deliberately NOT tests.

# ── `check` — the only honest way to run the suite ─────────────────────────
#
#     cmake --build build --target check
#
# WHY THIS EXISTS. `ctest` does not build anything; it runs whatever binaries
# happen to be lying in the build directory. Caught live on 2026-08-06: a test
# stopped compiling (a link dependency was lost during a refactor) and ctest
# immediately reported 49/49 green — it had run the executable from the
# PREVIOUS build. A green suite did not prove the tree even compiles.
#
# `check` builds the game and every test first, and runs ctest only if that
# succeeded. The plain `ctest` path still works for re-running one test while
# you iterate; `check` is what a verdict is quoted from.
get_property(_timaert_tests GLOBAL PROPERTY TIMAERT_TEST_TARGETS)
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building the game and all tests, then running the suite"
    USES_TERMINAL)
add_dependencies(check ${PROJECT_NAME} ${_timaert_tests})
