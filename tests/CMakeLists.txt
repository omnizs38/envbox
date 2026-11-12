find_package(GTest REQUIRED)

add_executable(envbox_core_tests
    core/test_container.cpp
    core/test_profile.cpp
    core/test_tar.cpp)

target_link_libraries(envbox_core_tests
    PRIVATE envbox::core GTest::gtest GTest::gtest_main)

if(MSVC)
    target_compile_options(envbox_core_tests PRIVATE /W4)
else()
    target_compile_options(envbox_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(envbox_core_tests)
