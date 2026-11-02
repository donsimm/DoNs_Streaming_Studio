cmake_minimum_required(VERSION 3.25)
project(p02_b_capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets QuickWidgets)
find_package(Threads REQUIRED)
qt_standard_project_setup()

qt_add_executable(p02_b_capture main.cpp convert.cpp)
target_link_libraries(p02_b_capture PRIVATE Qt6::Widgets Qt6::WebEngineWidgets Qt6::QuickWidgets Threads::Threads)
