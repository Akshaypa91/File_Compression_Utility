cmake_minimum_required(VERSION 3.16)
project(worklet3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Core container-format library  (Varad / Tejas)
# Includes the benchmarking module that was added in dev_varad.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Tests: container format, benchmarking, W3 instructions  (Varad / Tejas)
# ---------------------------------------------------------------------------
add_executable(test_container_format
    tests/test_container_format.cpp
)
target_link_libraries(test_container_format PRIVATE cmp2_container)

add_executable(test_benchmarking
    tests/test_benchmarking.cpp
)
target_link_libraries(test_benchmarking PRIVATE cmp2_container)

add_executable(test_w3_instructions
    tests/test_w3_instructions.cpp
)
target_link_libraries(test_w3_instructions PRIVATE cmp2_container)

# Benchmarking CLI runner
add_executable(benchmark_cli
    benchmarks/benchmark_cli.cpp
)
target_link_libraries(benchmark_cli PRIVATE cmp2_container)

# ---------------------------------------------------------------------------
# Pipeline layer  (Husain) — requires W1 (Huffman) + shared/symbols.hpp
# NOTE: also requires a w3_crc32 target provided externally (dev_kanak).
# Enabled automatically when the W1 tree is present alongside W3.
# ---------------------------------------------------------------------------
set(W1_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../W1"
    CACHE PATH "Path to the W1 source tree")
set(SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared/include"
    CACHE PATH "Path to the shared worklet headers")

if(EXISTS "${SHARED_INCLUDE_DIR}/symbols.hpp" AND
   EXISTS "${W1_DIR}/src/huffman.cpp"         AND
   EXISTS "${W1_DIR}/include/huffman.h")

    message(STATUS "W1 found — building w3_pipeline and test_pipeline")
    add_subdirectory(${W1_DIR} ${CMAKE_CURRENT_BINARY_DIR}/w1)

    add_library(w3_pipeline
        src/pipeline_controller.cpp
        src/command_line_interface.cpp
    )
    target_include_directories(w3_pipeline PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${W1_DIR}/include
        ${SHARED_INCLUDE_DIR}
    )
    # w3_crc32 is expected to be provided by dev_kanak's module
    target_link_libraries(w3_pipeline PUBLIC w1 cmp2_container w3_crc32)

    add_executable(test_pipeline tests/test_pipeline.cpp)
    target_link_libraries(test_pipeline PRIVATE w3_pipeline)

else()
    message(STATUS "W1/shared not found — pipeline targets skipped; container and benchmarking tests remain enabled")
endif()

# ---------------------------------------------------------------------------
# CTest registration
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME cmp2_container_tests    COMMAND test_container_format)
add_test(NAME cmp2_benchmarking_tests COMMAND test_benchmarking)
add_test(NAME w3_instructions_tests   COMMAND test_w3_instructions)
if(TARGET test_pipeline)
    add_test(NAME pipeline_tests COMMAND test_pipeline)
endif()
