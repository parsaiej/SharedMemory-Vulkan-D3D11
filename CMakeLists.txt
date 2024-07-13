cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(PROJECT_NAME SharedMemory-Vulkan-D3D11)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Packages
# ---------------------------------

find_package(Vulkan REQUIRED)

# Executable
# ---------------------------------

add_executable(${PROJECT_NAME} Source/Main.cpp)

# Include
# ---------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# Link
# ---------------------------------

target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARIES} dxgi d3d11)