# Minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

# Project name
project(VideoStreamer CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GStreamer, GObject, and GLib libraries using pkg-config
# This is the modern, recommended way to find libraries with CMake.
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

# Find all the GStreamer components and GLib/GObject
pkg_check_modules(GSTREAMER_1_0 REQUIRED gstreamer-1.0 gstreamer-base-1.0 gio-2.0 gstreamer-app-1.0)

# Set include directories
include_directories(${GSTREAMER_1_0_INCLUDE_DIRS})
include_directories(${OpenCV_INCLUDE_DIRS})

# Add the executable from your source file
add_executable(video-streamer video-streamer.cpp)

# Link the executable with the found libraries
target_link_libraries(video-streamer PRIVATE ${GSTREAMER_1_0_LIBRARIES})
target_link_libraries(video-streamer PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(video-streamer PRIVATE ${OpenCV_LIBS})
target_compile_options(video-streamer PRIVATE -pthread)
