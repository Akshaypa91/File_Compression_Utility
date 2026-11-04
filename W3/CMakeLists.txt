cmake_minimum_required(VERSION 3.16)
project(cmp2_container LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Core container-format library (container_format + benchmarking)
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
# CRC-32 library (Kanak's implementation, integrated on dev_varad for testing)
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
# Original tests (pre-existing on this branch)
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

add_executable(benchmark_cli
    benchmarks/benchmark_cli.cpp
)
target_link_libraries(benchmark_cli PRIVATE cmp2_container)

# ---------------------------------------------------------------------------
# NEW: Extended CRC-32 unit tests  (CRC-U-01 through CRC-U-10)
# ---------------------------------------------------------------------------
add_executable(test_crc32_extended
    tests/test_crc32_extended.cpp
)
target_link_libraries(test_crc32_extended PRIVATE w3_crc32)

# ---------------------------------------------------------------------------
# NEW: Extended Container Writer/Reader unit tests  (CW-U-xx / CR-U-xx)
# ---------------------------------------------------------------------------
add_executable(test_container_extended
    tests/test_container_extended.cpp
)
target_link_libraries(test_container_extended PRIVATE cmp2_container)

# ---------------------------------------------------------------------------
# CTest registration
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME cmp2_container_tests     COMMAND test_container_format)
add_test(NAME cmp2_benchmarking_tests  COMMAND test_benchmarking)
add_test(NAME w3_instructions_tests    COMMAND test_w3_instructions)
add_test(NAME crc32_extended_tests     COMMAND test_crc32_extended)
add_test(NAME container_extended_tests COMMAND test_container_extended)
