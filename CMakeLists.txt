cmake_minimum_required(VERSION 3.14)

project(MyGTestProject)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Enable testing
enable_testing()

# 2. Find GTest (ensure you have gtest installed on Arch: sudo pacman -S gtest)
find_package(GTest REQUIRED)

# Collect all test files automatically
file(GLOB_RECURSE TEST_SOURCES "tests/*_test.cpp")

# Create ONE executable containing ALL tests
add_executable(my_tests ${TEST_SOURCES})

target_link_libraries(my_tests GTest::gtest GTest::gtest_main)
gtest_discover_tests(my_tests)

# 5. Discover tests (helps tools find them)
include(GoogleTest)
gtest_discover_tests(my_tests)

