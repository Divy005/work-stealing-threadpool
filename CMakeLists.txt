cmake_minimum_required(VERSION 3.14)
project(WorkStealingPool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
elseif(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -g -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Header-only library
add_library(worksteal INTERFACE)
target_include_directories(worksteal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(worksteal INTERFACE pthread)

# Google Test (if available via FetchContent or system install)
find_package(GTest QUIET)

if(GTest_FOUND)
    enable_testing()
    add_executable(pool_tests
        tests/global_queue_tests.cpp
        tests/work_stealing_tests.cpp
    )
    target_link_libraries(pool_tests PRIVATE worksteal GTest::gtest_main)
    add_test(NAME pool_tests COMMAND pool_tests)
else()
    message(STATUS "GTest not found -- using built-in minitest framework")
    # Minitest-based tests (self-contained, no external deps)
    add_executable(pool_tests
        tests/global_queue_tests.cpp
        tests/work_stealing_tests.cpp
        tests/test_main.cpp
    )
    target_compile_definitions(pool_tests PRIVATE USE_MINITEST)
    target_link_libraries(pool_tests PRIVATE worksteal)
endif()

# Benchmarks
add_executable(pool_bench benchmarks/bench_main.cpp)
target_link_libraries(pool_bench PRIVATE worksteal)
