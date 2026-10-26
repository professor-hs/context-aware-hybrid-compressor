cmake_minimum_required(VERSION 3.20)
project(cacz VERSION 2.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codecs
)

# Core Library Sources
set(CACZ_CORE_SOURCES
    src/codecs/codec_registry.cpp
    src/codecs/raw.cpp
    src/codecs/rle.cpp
    src/codecs/huffman.cpp
    src/codecs/lz77.cpp
    src/codecs/delta.cpp
    src/core/analyzer.cpp
    src/core/container.cpp
    src/media/media_detector.cpp
    src/orchestration/model_adapter.cpp
    src/orchestration/orchestrator.cpp
    src/server/server.cpp
)

add_library(cacz_core STATIC ${CACZ_CORE_SOURCES})

if(WIN32)
    target_link_libraries(cacz_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(cacz_core PUBLIC Threads::Threads)
endif()

# CLI Executable
add_executable(cacz src/cli/main.cpp)
target_link_libraries(cacz PRIVATE cacz_core)

# C++ Unit Tests Executable
enable_testing()
add_executable(cacz_test tests/cpp/test_main.cpp)
target_link_libraries(cacz_test PRIVATE cacz_core)
add_test(NAME CppUnitTests COMMAND cacz_test)
