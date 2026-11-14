cmake_minimum_required(VERSION 3.25)

project(envbox
        VERSION 1.0.0
        DESCRIPTION "Capture, normalize, and restore application configurations across operating systems"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENVBOX_BUILD_TESTS "Build the envbox test suite" ON)
option(ENVBOX_BUILD_DESKTOP "Build the Dear ImGui desktop application" ON)

include(FetchContent)

find_package(nlohmann_json QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(SQLite3 REQUIRED)

find_package(zstd QUIET)
if(NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd AND NOT TARGET libzstd_static AND NOT TARGET zstd::libzstd_static)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.7
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(zstd)
endif()

if(TARGET zstd::libzstd_shared)
    set(ENVBOX_ZSTD_TARGET zstd::libzstd_shared)
elseif(TARGET zstd::libzstd)
    set(ENVBOX_ZSTD_TARGET zstd::libzstd)
elseif(TARGET libzstd_static)
    set(ENVBOX_ZSTD_TARGET libzstd_static)
elseif(TARGET zstd::libzstd_static)
    set(ENVBOX_ZSTD_TARGET zstd::libzstd_static)
else()
    find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)
    find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
    add_library(envbox_zstd UNKNOWN IMPORTED)
    set_target_properties(envbox_zstd PROPERTIES
        IMPORTED_LOCATION "${ZSTD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}")
    set(ENVBOX_ZSTD_TARGET envbox_zstd)
endif()

find_package(blake3 QUIET)
if(NOT TARGET BLAKE3::blake3 AND NOT TARGET blake3)
    set(BLAKE3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(blake3
        GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
        GIT_TAG 1.5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(blake3)
endif()

if(TARGET BLAKE3::blake3)
    set(ENVBOX_BLAKE3_TARGET BLAKE3::blake3)
elseif(TARGET blake3)
    set(ENVBOX_BLAKE3_TARGET blake3)
else()
    find_path(BLAKE3_INCLUDE_DIR blake3.h REQUIRED)
    find_library(BLAKE3_LIBRARY NAMES blake3 REQUIRED)
    add_library(envbox_blake3 UNKNOWN IMPORTED)
    set_target_properties(envbox_blake3 PROPERTIES
        IMPORTED_LOCATION "${BLAKE3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BLAKE3_INCLUDE_DIR}")
    set(ENVBOX_BLAKE3_TARGET envbox_blake3)
endif()

add_library(envbox_core
    src/compat/paths.cpp
    src/core/container.cpp
    src/core/os.cpp
    src/core/profile.cpp
    src/core/tar.cpp)
add_library(envbox::core ALIAS envbox_core)

add_library(envbox_platform STATIC src/platform/paths.cpp)
add_library(envbox::platform ALIAS envbox_platform)
target_include_directories(envbox_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_platform PUBLIC cxx_std_23)

add_library(envbox_engine STATIC
    src/engine/adapter.cpp
    src/engine/capture.cpp
    src/engine/diff.cpp
    src/engine/migration.cpp
    src/engine/restore.cpp)
add_library(envbox::engine ALIAS envbox_engine)
target_include_directories(envbox_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_engine PUBLIC cxx_std_23)
target_link_libraries(envbox_engine PUBLIC envbox::core envbox::platform)

add_library(envbox_adapter_git STATIC src/adapters/git.cpp)
add_library(envbox::adapter_git ALIAS envbox_adapter_git)
target_include_directories(envbox_adapter_git PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_adapter_git PUBLIC cxx_std_23)
target_link_libraries(envbox_adapter_git PUBLIC envbox::engine)

add_library(envbox_adapter_vscode STATIC src/adapters/vscode.cpp)
add_library(envbox::adapter_vscode ALIAS envbox_adapter_vscode)
target_include_directories(envbox_adapter_vscode PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_adapter_vscode PUBLIC cxx_std_23)
target_link_libraries(envbox_adapter_vscode PUBLIC envbox::engine)

add_library(envbox_adapter_firefox STATIC src/adapters/firefox.cpp)
add_library(envbox::adapter_firefox ALIAS envbox_adapter_firefox)
target_include_directories(envbox_adapter_firefox PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_adapter_firefox PUBLIC cxx_std_23)
target_link_libraries(envbox_adapter_firefox PUBLIC envbox::engine)

add_executable(envbox src/cli/main.cpp)
target_compile_features(envbox PRIVATE cxx_std_23)
target_link_libraries(envbox PRIVATE envbox::adapter_vscode envbox::adapter_git envbox::adapter_firefox envbox::storage)
include(GNUInstallDirs)
install(TARGETS envbox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_library(envbox_sdk STATIC
    src/sdk/adapter.cpp
    src/sdk/loader.cpp)
add_library(envbox::sdk ALIAS envbox_sdk)
target_include_directories(envbox_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_sdk PUBLIC cxx_std_23)
target_link_libraries(envbox_sdk PUBLIC envbox::engine PRIVATE ${CMAKE_DL_LIBS})

add_library(envbox_storage STATIC src/storage/library.cpp)
add_library(envbox::storage ALIAS envbox_storage)
target_include_directories(envbox_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(envbox_storage PUBLIC cxx_std_23)
target_link_libraries(envbox_storage PUBLIC envbox::core envbox::platform PRIVATE SQLite::SQLite3)

if(ENVBOX_BUILD_TESTS)
    add_library(envbox_example_adapter MODULE sdk/examples/example_adapter.c)
    target_include_directories(envbox_example_adapter PRIVATE include)
    set_target_properties(envbox_example_adapter PROPERTIES PREFIX "")

    add_library(envbox_sdk_bad_abi MODULE sdk/examples/bad_abi_adapter.c)
    target_include_directories(envbox_sdk_bad_abi PRIVATE include)
    set_target_properties(envbox_sdk_bad_abi PROPERTIES PREFIX "")

    add_library(envbox_sdk_missing_symbol MODULE sdk/examples/missing_symbol_adapter.c)
    set_target_properties(envbox_sdk_missing_symbol PROPERTIES PREFIX "")
endif()

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

foreach(extra_target envbox_platform envbox_engine envbox_adapter_git envbox_adapter_vscode envbox_adapter_firefox envbox envbox_sdk envbox_storage)
    if(MSVC)
        target_compile_options(${extra_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${extra_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(ENVBOX_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT TARGET GTest::gtest)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz)
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()

if(ENVBOX_BUILD_DESKTOP)
    include(FetchContent)
    find_package(OpenGL REQUIRED)

    find_package(glfw3 3.3 QUIET)
    if(NOT TARGET glfw)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glfw)
    endif()

    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)

    add_library(envbox_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(envbox_imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(envbox_imgui PUBLIC glfw OpenGL::GL)

    add_executable(envbox_desktop desktop/main.cpp)
    target_compile_features(envbox_desktop PRIVATE cxx_std_23)
    target_link_libraries(envbox_desktop PRIVATE
        envbox_imgui
        envbox::adapter_vscode
        envbox::adapter_git
        envbox::adapter_firefox
        envbox::storage)
    if(NOT MSVC)
        target_compile_options(envbox_desktop PRIVATE -Wall -Wextra -Wpedantic)
    else()
        target_compile_options(envbox_desktop PRIVATE /W4)
    endif()
endif()
