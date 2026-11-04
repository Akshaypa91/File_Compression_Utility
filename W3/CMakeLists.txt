cmake_minimum_required(VERSION 3.16)
project(worklet3 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Core container‑format library (Varad / Tejas)
# Includes the benchmarking module added in dev_varad.
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
# CRC‑32 library – Kanak's implementation
# ---------------------------------------------------------------------------
add_library(w3_crc32
    src/crc32.cpp
)

target_include_directories(w3_crc32
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(w3_crc32 PRIVATE /W4 /permissive-)
else()
    target_compile_options(w3_crc32 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests: container format, benchmarking, W3 instructions
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

# CRC‑32 test (Kanak's)
add_executable(test_crc32
    tests/test_crc32.cpp
)
target_link_libraries(test_crc32 PRIVATE w3_crc32)

# Benchmarking CLI runner
add_executable(benchmark_cli
    benchmarks/benchmark_cli.cpp
)
target_link_libraries(benchmark_cli PRIVATE cmp2_container)

# ---------------------------------------------------------------------------
# Pipeline layer (your code) – requires W1 (Huffman) + shared symbols
# ---------------------------------------------------------------------------
set(W1_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../W1"
    CACHE PATH "Path to the W1 source tree")
set(W2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../W2"
    CACHE PATH "Path to the W2 source tree")
set(SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared/include"
    CACHE PATH "Path to the shared worklet headers")

if(EXISTS "${SHARED_INCLUDE_DIR}/symbols.hpp" AND
   EXISTS "${W1_DIR}/src/huffman.cpp"         AND
   EXISTS "${W1_DIR}/include/huffman.h"       AND
   EXISTS "${W2_DIR}/src/LZ77.cpp"            AND
   EXISTS "${W2_DIR}/include/LZ77.h")

    message(STATUS "W1, W2, and shared symbols found — building compressor pipeline")

    add_library(w1
        ${W1_DIR}/src/bit_io.cpp
        ${W1_DIR}/src/huffman.cpp
        ${W1_DIR}/src/huffman_tree.cpp
        ${W1_DIR}/src/min_heap.cpp
    )
    target_include_directories(w1 PUBLIC
        ${W1_DIR}/include
        ${SHARED_INCLUDE_DIR}
    )

    add_library(w2_lz77
        ${W2_DIR}/src/HashChain.cpp
        ${W2_DIR}/src/LZ77.cpp
        ${W2_DIR}/src/Token.cpp
    )
    target_include_directories(w2_lz77 PUBLIC
        ${W2_DIR}/include
        ${SHARED_INCLUDE_DIR}
    )

    add_library(w3_pipeline
        src/pipeline_controller.cpp
        src/command_line_interface.cpp
        src/production_adapters.cpp
    )
    target_include_directories(w3_pipeline PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${W1_DIR}/include
        ${W2_DIR}/include
        ${SHARED_INCLUDE_DIR}
    )
    target_link_libraries(w3_pipeline PUBLIC
        w1 w2_lz77 cmp2_container w3_crc32
    )

    add_executable(compressor src/compressor_main.cpp)
    target_link_libraries(compressor PRIVATE w3_pipeline)

    add_executable(test_pipeline
        tests/test_pipeline.cpp
    )
    target_link_libraries(test_pipeline PRIVATE w3_pipeline)

else()
    message(STATUS "W1/W2/shared sources not found — pipeline and compressor targets skipped")
endif()

# ---------------------------------------------------------------------------
# CTest registration
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME cmp2_container_tests    COMMAND test_container_format)
add_test(NAME cmp2_benchmarking_tests COMMAND test_benchmarking)
add_test(NAME w3_instructions_tests   COMMAND test_w3_instructions)
add_test(NAME crc32_tests             COMMAND test_crc32)
if(TARGET test_pipeline)
    add_test(NAME pipeline_tests COMMAND test_pipeline)
endif()
