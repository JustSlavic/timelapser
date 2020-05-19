cmake_minimum_required(VERSION 3.16)
project(timelapser)

set(CMAKE_CXX_STANDARD 17)

add_executable(timelapser main.cpp)


add_executable(encoder encoder.cpp)
