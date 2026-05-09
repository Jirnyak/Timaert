cmake_minimum_required(VERSION 3.16)
project(timaert LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TIMAERT_DEBUG_UI "Enable ImGui debug overlays" ON)

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

if(TIMAERT_DEBUG_UI)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

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

if(TIMAERT_DEBUG_UI)
    target_sources(${PROJECT_NAME} PRIVATE ${IMGUI_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TIMAERT_DEBUG_UI)
    if(EMSCRIPTEN)
        target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_OPENGL_ES3)
    endif()
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
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -fno-rtti -fno-exceptions)
endif()
