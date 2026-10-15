cmake_minimum_required(VERSION 3.16)
project(cmp2_container LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cmp2_container
    src/container_format.cpp
    src/benchmarking.cpp
)

target_include_directories(cmp2_container
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(cmp2_container PRIVATE /W4 /permissive-)
else()
    target_compile_options(cmp2_container PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Original container format tests
add_executable(test_container_format
    tests/test_container_format.cpp
)
target_link_libraries(test_container_format PRIVATE cmp2_container)

# Worklet 3 benchmarking unit tests
add_executable(test_benchmarking
    tests/test_benchmarking.cpp
)
target_link_libraries(test_benchmarking PRIVATE cmp2_container)

# Worklet 3 instructions requirement tests
add_executable(test_w3_instructions
    tests/test_w3_instructions.cpp
)
target_link_libraries(test_w3_instructions PRIVATE cmp2_container)

# Benchmarking CLI runner
add_executable(benchmark_cli
    benchmarks/benchmark_cli.cpp
)
target_link_libraries(benchmark_cli PRIVATE cmp2_container)

enable_testing()
add_test(NAME cmp2_container_tests COMMAND test_container_format)
add_test(NAME cmp2_benchmarking_tests COMMAND test_benchmarking)
add_test(NAME w3_instructions_tests COMMAND test_w3_instructions)
