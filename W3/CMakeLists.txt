cmake_minimum_required(VERSION 3.16)
project(cmp2_container LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cmp2_container
    src/container_format.cpp
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

add_executable(test_container_format
    tests/test_container_format.cpp
)

target_link_libraries(test_container_format PRIVATE cmp2_container)

enable_testing()
add_test(NAME cmp2_container_tests COMMAND test_container_format)

