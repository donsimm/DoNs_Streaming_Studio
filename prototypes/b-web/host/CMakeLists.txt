cmake_minimum_required(VERSION 3.25)
project(p01_b_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)
qt_standard_project_setup()

qt_add_executable(p01_b_host main.cpp)
target_link_libraries(p01_b_host PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
