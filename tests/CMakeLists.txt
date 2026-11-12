find_package(GTest REQUIRED)

add_executable(envbox_core_tests
    core/test_container.cpp
    core/test_profile.cpp
    core/test_tar.cpp)

add_executable(envbox_engine_tests
    engine/test_engine.cpp
    adapters/test_git_adapter.cpp)

target_link_libraries(envbox_core_tests
    PRIVATE envbox::core GTest::gtest GTest::gtest_main)

target_link_libraries(envbox_engine_tests
    PRIVATE envbox::adapter_git envbox::engine envbox::core GTest::gtest GTest::gtest_main)
target_include_directories(envbox_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(envbox_engine_tests PRIVATE /W4)
else()
    target_compile_options(envbox_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(envbox_core_tests)
gtest_discover_tests(envbox_engine_tests)
