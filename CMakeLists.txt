# Oberste Build-Datei von DoNs Streaming Studio
cmake_minimum_required(VERSION 3.25)

# Die Version des Programms steht nur hier (PROJECT_VERSION) und kommt über dss_common in alle Programme.
project(DoNsStreamingStudio VERSION 0.1.0 LANGUAGES CXX)

# Schalter (CLAUDE.md Regel 8). Der Kern wird immer gebaut.
option(DSS_BUILD_APP "Fenster dss-app bauen" ON)
option(DSS_BUILD_TESTS "Tests bauen (ctest)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(DSS_QT_COMPONENTS Core)
if(DSS_BUILD_APP)
  list(APPEND DSS_QT_COMPONENTS Widgets)
endif()
if(DSS_BUILD_TESTS)
  list(APPEND DSS_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${DSS_QT_COMPONENTS})
# https://doc.qt.io/qt-6.8/qt-standard-project-setup.html
qt_standard_project_setup()

# Warnungen für die eigenen Ziele; Qt-Header kommen über importierte Ziele als System-Header.
add_library(dss_warnings INTERFACE)
target_compile_options(dss_warnings INTERFACE -Wall -Wextra)

add_subdirectory(src/common)
add_subdirectory(src/core)
if(DSS_BUILD_APP)
  add_subdirectory(src/app)
endif()
if(DSS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
