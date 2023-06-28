cmake_minimum_required(VERSION 3.22)

project(dat-archive-test)

add_executable(dat-archive-test main.cpp)

target_link_libraries(dat-archive-test dat-archive)