# Oberste Build-Datei von DoNs Streaming Studio
cmake_minimum_required(VERSION 3.25)

# Die Version des Programms steht nur hier (PROJECT_VERSION) und kommt über dss_common in alle Programme.
project(DoNsStreamingStudio VERSION 0.1.0 LANGUAGES CXX)

# Schalter (CLAUDE.md Regel 8). Der Kern wird immer gebaut.
option(DSS_BUILD_APP "Fenster dss-app bauen" ON)
option(DSS_BUILD_TESTS "Tests bauen (ctest)" ON)
option(DSS_BUILD_CLI "Werkzeug dss-cli bauen" ON)
# P09: Kern und dss-cli lesen die Grösse neuer Bild- und Videoquellen aus der Datei (Entscheid 6).
# Aus: neue Quellen bekommen die Grösse des Overlays wie in P08.
option(DSS_MEDIA_SIZE "Grösse neuer Bild- und Videoquellen aus der Datei lesen (P09)" ON)
# P09: Fenster zeichnet die Quellen auf der Leinwand (Teil B). Aus: leere Leinwand wie in P08.
option(DSS_APP_RENDERER "Quellen auf der Leinwand zeichnen (P09)" ON)
# P09: Quellen auf der Leinwand wählen, verschieben, skalieren, drehen (Teil C).
# Aus: Leinwand nur zur Anzeige.
option(DSS_APP_CANVAS_EDIT "Quellen auf der Leinwand bearbeiten (P09)" ON)
# P10a (Punkt 65): Drehung aus den Metadaten von Videos (Display-Matrix) in Kern und Zeichner.
# Aus: Videos wie gespeichert, Originalgrösse ohne Drehung (wie P09b).
option(DSS_VIDEO_ROTATION "Drehung aus den Metadaten von Videos beachten (P10a)" ON)
# P10a (Teil B): Version im Einsatz bauen (nur scripts/installieren.sh). Ein: echte Daten
# ~/.local/share/dons-streaming-studio, Einheit dss-core.service, Laufzeit-Dateien (Symbole,
# Schriften, Übersetzung) aus dem Installationsordner. Aus (Standard): Version in Arbeit mit
# Entwicklungsordner, Einheit dss-core-entwicklung.service, Laufzeit-Dateien aus dem Repo.
option(DSS_INSTALLED_EDITION "Version im Einsatz bauen (scripts/installieren.sh, P10a)" OFF)
# P10a (Teil B): Commit, aus dem gebaut wird; setzt scripts/installieren.sh. Leer: kein Commit
# in --version.
set(DSS_COMMIT "" CACHE STRING "Commit des Baus für --version (scripts/installieren.sh)")
# P10a (Punkt 41): Der Kern meldet sich regelmässig bei systemd (Watchdog). Aus: keine Meldung,
# WatchdogSec in der Einheit darf dann nicht gesetzt sein.
option(DSS_CORE_WATCHDOG "Kern meldet sich beim Watchdog von systemd (P10a)" ON)

# Orte der Installation (CMAKE_INSTALL_BINDIR, CMAKE_INSTALL_FULL_DATADIR):
# https://cmake.org/cmake/help/v3.31/module/GNUInstallDirs.html
include(GNUInstallDirs)
# Laufzeit-Dateien der Version im Einsatz: <Präfix>/share/dons-streaming-studio
set(DSS_INSTALL_SHARE_DIR "${CMAKE_INSTALL_DATADIR}/dons-streaming-studio")
set(DSS_INSTALL_FULL_SHARE_DIR "${CMAKE_INSTALL_FULL_DATADIR}/dons-streaming-studio")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(DSS_QT_COMPONENTS Core Network)
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
if(DSS_MEDIA_SIZE OR (DSS_BUILD_APP AND DSS_APP_RENDERER))
  add_subdirectory(src/media)
endif()
add_subdirectory(src/core)
if(DSS_BUILD_CLI)
  add_subdirectory(src/cli)
endif()
if(DSS_BUILD_APP)
  add_subdirectory(src/app)
endif()
if(DSS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
