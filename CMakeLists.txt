cmake_minimum_required(VERSION 3.20)

project(ll_data_feed)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find all source and header files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_executable(ll_data_feed ${SOURCES})

# Include the src directory for header files
target_include_directories(ll_data_feed PRIVATE src)
