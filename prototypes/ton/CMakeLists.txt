# Prototyp P12a: Ton von Videos (Messtest). Eigene Build-Datei, nicht Teil des Projekts und nicht
# in ctest. Bauen:
#   cmake -S prototypes/ton -B prototypes/ton/build && cmake --build prototypes/ton/build -j8
# ton-messen: Qt Core, Network und Multimedia mit QCoreApplication (wie der Kern, M1 prüft genau
#             das), dazu libavformat/libavcodec/libswresample für den Ton der Testdateien.
# seite-zeigen: Qt WebEngine Widgets, zeigt die Messseite für M2 (wie tst_browserquelle).
# pw-messen (P12b, b1): nur Qt Core und libpipewire-0.3 (pkg-config), QCoreApplication, ohne Qt Gui
#             und ohne Qt Multimedia; Messung Z3 (pw-lauf.sh).
cmake_minimum_required(VERSION 3.25)
project(p12a_ton LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Multimedia WebEngineWidgets)
qt_standard_project_setup()
find_package(PkgConfig REQUIRED)
pkg_check_modules(TON_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)

qt_add_executable(ton-messen
  main.cpp
  zeit.h
  tondatei.h tondatei.cpp
  klickmessung.h klickmessung.cpp
  meldeserver.h meldeserver.cpp
  folgen.h folgen.cpp
  auswertung.h auswertung.cpp
)
# Ordner der Messseite (messseite.html liegt neben den Quellen)
target_compile_definitions(ton-messen PRIVATE TON_QUELLORDNER="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ton-messen PRIVATE -Wall -Wextra)
target_link_libraries(ton-messen PRIVATE Qt6::Core Qt6::Network Qt6::Multimedia PkgConfig::TON_FFMPEG)

qt_add_executable(seite-zeigen seite-zeigen.cpp)
target_compile_options(seite-zeigen PRIVATE -Wall -Wextra)
target_link_libraries(seite-zeigen PRIVATE Qt6::WebEngineWidgets)

pkg_check_modules(TON_PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
qt_add_executable(pw-messen
  pw-messen.cpp
  zeit.h
  pwsitzung.h pwsitzung.cpp
  pwklick.h pwklick.cpp
  auswertung.h auswertung.cpp
)
target_compile_options(pw-messen PRIVATE -Wall -Wextra)
target_link_libraries(pw-messen PRIVATE Qt6::Core PkgConfig::TON_PIPEWIRE)
