cmake_minimum_required(VERSION 3.25)
project(p02_a_libvpx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
qt_standard_project_setup()

qt_add_executable(p02_a_libvpx main.cpp decoder.cpp)
target_link_libraries(p02_a_libvpx PRIVATE Qt6::Widgets PkgConfig::FFMPEG)
