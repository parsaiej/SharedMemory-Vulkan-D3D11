cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/External/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

set(PROJECT_NAME SharedMemory-Vulkan-D3D11)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Packages
# ---------------------------------

find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED)
find_package(Stb    REQUIRED)
find_package(volk   REQUIRED)

# Executable
# ---------------------------------

add_executable(${PROJECT_NAME} Source/Main.cpp)

# Include
# ---------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})

# Link
# ---------------------------------

target_link_libraries(${PROJECT_NAME} 
    volk::volk_headers
    spdlog::spdlog_header_only 
    dxgi 
    d3d11
)