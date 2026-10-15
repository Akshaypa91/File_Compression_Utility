cmake_minimum_required(VERSION 3.16)
project(w1_huffman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(W1_SANITIZE "Build W1 with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# W1 library: bit-level I/O + Huffman. The shared contract lives in ../shared/include.
add_library(w1 STATIC
    src/bit_io.cpp
    src/min_heap.cpp
    src/huffman_tree.cpp
    src/huffman.cpp
)
target_include_directories(w1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/include
)

if(MSVC)
    target_compile_options(w1 PRIVATE /W4 /permissive-)
else()
    target_compile_options(w1 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(W1_SANITIZE AND NOT MSVC)
    target_compile_options(w1 PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(w1 PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()
foreach(t test_bit_io test_tree test_huffman)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE w1)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(w1_demo examples/demo.cpp)
target_link_libraries(w1_demo PRIVATE w1)
add_test(NAME w1_demo COMMAND w1_demo)
