cmake_minimum_required(VERSION 3.16)
project(timelapser)

set(CMAKE_CXX_STANDARD 17)

find_library(avutil REQUIRED)
find_library(avformat REQUIRED)
find_library(avcodec REQUIRED)
#find_library(avdevice REQUIRED)

find_library(x264 REQUIRED)
find_library(swscale REQUIRED)

find_library(X11 REQUIRED)

add_executable(timelapser main.cpp)
add_executable(encoder encoder.cpp)
add_executable(windowed windowed.cpp)

target_link_libraries(windowed X11)

add_executable(video_reader video_reader.cpp)
target_link_libraries(video_reader avformat avcodec avutil)
