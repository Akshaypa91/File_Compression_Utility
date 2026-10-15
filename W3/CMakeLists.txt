cmake_minimum_required(VERSION 3.16)
project(w3_crc32 LANGUAGES CXX)
enable_testing()

add_library(w3_crc32 src/crc32.cpp)
target_include_directories(w3_crc32 PUBLIC include)
target_compile_features(w3_crc32 PUBLIC cxx_std_17)

add_executable(test_crc32 tests/test_crc32.cpp)
target_link_libraries(test_crc32 PRIVATE w3_crc32)
add_test(NAME crc32 COMMAND test_crc32)
