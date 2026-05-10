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

include(FetchContent)
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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
endif()

file(GLOB_RECURSE SAMOSBOR_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/core/*.cpp src/gl/*.cpp src/ecs/*.cpp
    src/macro/*.cpp src/sub/*.cpp src/events/*.cpp
    src/content/*.cpp src/ui/*.cpp src/assets/*.cpp)

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
if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_OPENGL_ES3)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${stb_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE EnTT::EnTT)
target_compile_definitions(${PROJECT_NAME} PRIVATE ENTT_NOEXCEPTION GL_SILENCE_DEPRECATION)

# Asset symlink: build/assets -> ../public/assets (TS reference assets).
set(_TIMAERT_TS_ASSETS "${CMAKE_SOURCE_DIR}/../public/assets")
if(EXISTS "${_TIMAERT_TS_ASSETS}")
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/assets")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_TIMAERT_TS_ASSETS}" "${CMAKE_BINARY_DIR}/assets")
    endif()
endif()

if(EMSCRIPTEN)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-deprecated-literal-operator)
    set(USE_FLAGS "-sUSE_SDL=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS}")
    target_link_options(${PROJECT_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sASYNCIFY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE __EMSCRIPTEN__)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    if(APPLE)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_FRAMEWORK})
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
    endif()
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>)
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

if(MSVC)
    foreach(_timaert_flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REPLACE "/GR" "" ${_timaert_flag_var} "${${_timaert_flag_var}}")
        string(REPLACE "/EHsc" "" ${_timaert_flag_var} "${${_timaert_flag_var}}")
    endforeach()
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(quest_lifecycle_test
    tests/quest_lifecycle_test.cpp
    src/events/event_bus.cpp
    src/events/effect_applicator.cpp
    src/events/logic_nodes.cpp
    src/events/node_registry.cpp
    src/events/quests/quest_engine.cpp
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

add_executable(subworld_village_gen_test
    tests/subworld_village_gen_test.cpp
    src/sub/base_generator.cpp
    src/sub/gens/city_generator.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(subworld_village_gen_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(subworld_village_gen_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(subworld_village_gen_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(subworld_village_gen_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(subworld_village_gen_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(subworld_village_gen_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(subworld_city_gen_test
    tests/subworld_city_gen_test.cpp
    src/sub/base_generator.cpp
    src/sub/gens/city_generator.cpp
    src/sub/gens/dispatch.cpp)
set_target_properties(subworld_city_gen_test PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(subworld_city_gen_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(subworld_city_gen_test PRIVATE ENTT_NOEXCEPTION)
if(MSVC)
    target_compile_options(subworld_city_gen_test PRIVATE /W3 /GR- /EHs-c-)
    target_compile_definitions(subworld_city_gen_test PRIVATE
        _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(subworld_city_gen_test PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()

add_executable(pathfinding_parity_test
    tests/pathfinding_parity_test.cpp
    src/macro/pathfinding.cpp)
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
