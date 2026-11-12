cmake_minimum_required(VERSION 3.25)

project(envbox
        VERSION 0.1.0
        DESCRIPTION "Capture, normalize, and restore application configurations across operating systems"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENVBOX_BUILD_TESTS "Build the envbox test suite" ON)

find_package(nlohmann_json REQUIRED)

find_package(zstd QUIET)
if(NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
    find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)
    find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
    add_library(envbox_zstd UNKNOWN IMPORTED)
    set_target_properties(envbox_zstd PROPERTIES
        IMPORTED_LOCATION "${ZSTD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}")
    set(ENVBOX_ZSTD_TARGET envbox_zstd)
elseif(TARGET zstd::libzstd_shared)
    set(ENVBOX_ZSTD_TARGET zstd::libzstd_shared)
else()
    set(ENVBOX_ZSTD_TARGET zstd::libzstd)
endif()

find_package(blake3 QUIET)
if(NOT TARGET BLAKE3::blake3)
    find_path(BLAKE3_INCLUDE_DIR blake3.h REQUIRED)
    find_library(BLAKE3_LIBRARY NAMES blake3 REQUIRED)
    add_library(envbox_blake3 UNKNOWN IMPORTED)
    set_target_properties(envbox_blake3 PROPERTIES
        IMPORTED_LOCATION "${BLAKE3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BLAKE3_INCLUDE_DIR}")
    set(ENVBOX_BLAKE3_TARGET envbox_blake3)
else()
    set(ENVBOX_BLAKE3_TARGET BLAKE3::blake3)
endif()

add_library(envbox_core
    src/core/container.cpp
    src/core/os.cpp
    src/core/profile.cpp
    src/core/tar.cpp)
add_library(envbox::core ALIAS envbox_core)

target_include_directories(envbox_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_core PUBLIC cxx_std_23)
target_link_libraries(envbox_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE ${ENVBOX_ZSTD_TARGET} ${ENVBOX_BLAKE3_TARGET})

if(MSVC)
    target_compile_options(envbox_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(envbox_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENVBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
