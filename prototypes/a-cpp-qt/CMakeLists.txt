cmake_minimum_required(VERSION 3.25)
project(p01_a_cpp_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
qt_standard_project_setup()

qt_add_executable(p01_a main.cpp)
target_link_libraries(p01_a PRIVATE Qt6::Widgets Qt6::Multimedia)
