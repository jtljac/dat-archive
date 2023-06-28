cmake_minimum_required(VERSION 3.22)

project(dat-archive)

add_library(dat-archive STATIC)

# Libraries
# ZLib for compression
find_package(ZLIB REQUIRED)
target_link_libraries(dat-archive ZLIB::ZLIB)

target_include_directories(dat-archive PUBLIC ./include)

target_sources(dat-archive PRIVATE
        source/dat-archive.cpp
)

add_subdirectory(examples)