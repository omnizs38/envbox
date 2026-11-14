add_executable(envbox_core_tests
    core/test_compat.cpp
    core/test_container.cpp
    core/test_profile.cpp
    core/test_tar.cpp)

add_executable(envbox_engine_tests
    engine/test_diff.cpp
    engine/test_migration.cpp
    engine/test_engine.cpp
    adapters/test_firefox_adapter.cpp
    adapters/test_git_adapter.cpp
    adapters/test_vscode_adapter.cpp)

add_executable(envbox_sdk_tests sdk/test_sdk.cpp)
target_link_libraries(envbox_sdk_tests PRIVATE envbox::sdk envbox::engine envbox::core GTest::gtest GTest::gtest_main)
target_include_directories(envbox_sdk_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(envbox_sdk_tests PRIVATE
    ENVBOX_EXAMPLE_ADAPTER_PATH="$<TARGET_FILE:envbox_example_adapter>"
    ENVBOX_BAD_ABI_ADAPTER_PATH="$<TARGET_FILE:envbox_sdk_bad_abi>"
    ENVBOX_MISSING_SYMBOL_ADAPTER_PATH="$<TARGET_FILE:envbox_sdk_missing_symbol>")
add_dependencies(envbox_sdk_tests envbox_example_adapter envbox_sdk_bad_abi envbox_sdk_missing_symbol)

add_executable(envbox_storage_tests storage/test_library.cpp)
target_link_libraries(envbox_storage_tests PRIVATE envbox::storage GTest::gtest GTest::gtest_main)
target_include_directories(envbox_storage_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(envbox_core_tests
    PRIVATE envbox::core GTest::gtest GTest::gtest_main)

target_link_libraries(envbox_engine_tests
    PRIVATE envbox::adapter_vscode envbox::adapter_git envbox::adapter_firefox envbox::engine envbox::core GTest::gtest GTest::gtest_main)
target_include_directories(envbox_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(envbox_engine_tests PRIVATE /W4)
else()
    target_compile_options(envbox_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(envbox_sdk_tests PRIVATE /W4)
else()
    target_compile_options(envbox_sdk_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(envbox_storage_tests PRIVATE /W4)
else()
    target_compile_options(envbox_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(envbox_core_tests)
gtest_discover_tests(envbox_engine_tests)
gtest_discover_tests(envbox_sdk_tests)
gtest_discover_tests(envbox_storage_tests)
