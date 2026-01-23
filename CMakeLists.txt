cmake_minimum_required(VERSION 3.16)
project(samosbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages (not needed for Emscripten - uses ports)
if(NOT EMSCRIPTEN)
    set(SDL2_USE_FRAMEWORKS OFF)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(SDL2_mixer REQUIRED)
endif()

# Source files
set(SOURCES
    src/app/main.cpp
    src/core/game_context.cpp
    src/systems/player.cpp
    src/systems/random_events.cpp
)

# Create executable
if(EMSCRIPTEN)
    add_executable(${PROJECT_NAME} ${SOURCES})
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link libraries
if(EMSCRIPTEN)
    # Emscripten SDL2 ports
    set(USE_FLAGS "-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sUSE_SDL_TTF=2 -sUSE_SDL_MIXER=2 -sSDL2_IMAGE_FORMATS=['png'] -sSDL2_MIXER_FORMATS=['ogg','mp3']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS}")
    
    # Emscripten-specific linker flags
    target_link_options(${PROJECT_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sDISABLE_EXCEPTION_CATCHING=0
        -sASYNCIFY
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/src/assets@/assets"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE __EMSCRIPTEN__)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        SDL2_mixer::SDL2_mixer
    )
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
