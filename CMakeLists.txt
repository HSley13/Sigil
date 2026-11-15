cmake_minimum_required(VERSION 4.1.1)

# Project metadata
project(
    sigil
    VERSION 1.0
    DESCRIPTION "Signal Daemon is a local-first, natural language interface for any signal in the physical world."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# GoogleTest via FetchContent
include(cmake/Dependencies.cmake)

# Enable CTest
enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
