cmake_minimum_required(VERSION 3.16)
project(samosbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages (not needed for Emscripten - uses ports)
if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_ttf REQUIRED)
endif()

# Source files
set(SOURCES
    src/sac.cpp
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
    set(USE_FLAGS "-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sUSE_SDL_TTF=2 -sSDL2_IMAGE_FORMATS=['png']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS}")
    
    # Emscripten-specific linker flags
    target_link_options(${PROJECT_NAME} PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAX_WEBGL_VERSION=2
        -sASYNCIFY
        -sASSERTIONS
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/src/sprites@/sprites"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/src/backgrounds@/backgrounds"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/src/Roboto-Black.ttf@/Roboto-Black.ttf"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE __EMSCRIPTEN__)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2::SDL2
        SDL2::SDL2main
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
    )
endif()

# Compiler warnings and optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-omit-frame-pointer)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Copy resources to build directory (not needed for Emscripten - uses --preload-file)
if(NOT EMSCRIPTEN)
    file(GLOB_RECURSE SPRITE_FILES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/sprites/*
    )
    file(GLOB_RECURSE BACKGROUND_FILES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/backgrounds/*
    )
    set(RESOURCE_FILES
        ${SPRITE_FILES}
        ${BACKGROUND_FILES}
        ${CMAKE_SOURCE_DIR}/src/Roboto-Black.ttf
    )

    set(RESOURCE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/resources.stamp)
    add_custom_command(
        OUTPUT ${RESOURCE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/sprites
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/sprites
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/backgrounds
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/backgrounds
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/src/Roboto-Black.ttf
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/Roboto-Black.ttf
        COMMAND ${CMAKE_COMMAND} -E touch ${RESOURCE_STAMP}
        DEPENDS ${RESOURCE_FILES}
        COMMENT "Copying game resources to build directory"
    )

    add_custom_target(copy_resources ALL DEPENDS ${RESOURCE_STAMP})
endif()
