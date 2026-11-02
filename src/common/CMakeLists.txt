# dss_common: Logik und Daten für Kern und Fenster. Qt Core und Qt Network (lokale Verbindung
# zum Kern, P07), kein Qt Gui (CLAUDE.md Regel 9).
add_library(dss_common STATIC
  version.h version.cpp
  logging.h logging.cpp
  unixsignals.h unixsignals.cpp
  overlay.h overlay.cpp
  address.h address.cpp
  overlayjson.h overlayjson.cpp
  overlaystore.h overlaystore.cpp
  overlaylist.h overlaylist.cpp
  tokenstore.h tokenstore.cpp
  canvasview.h canvasview.cpp
  connectionstate.h connectionstate.cpp
  windowsize.h windowsize.cpp
  configpaths.h configpaths.cpp
  datalock.h datalock.cpp
  systemstats.h systemstats.cpp
  protocol.h protocol.cpp
  coreclient.h coreclient.cpp
  windowdata.h windowdata.cpp
  systemdnotify.h systemdnotify.cpp
)
target_include_directories(dss_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dss_common PRIVATE DSS_VERSION="${PROJECT_VERSION}")
target_link_libraries(dss_common PUBLIC Qt6::Core Qt6::Network PRIVATE dss_warnings)
