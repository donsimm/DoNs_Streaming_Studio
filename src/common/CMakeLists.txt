# dss_common: Logik und Daten für Kern und Fenster. Nur Qt Core (CLAUDE.md Regel 9).
add_library(dss_common STATIC
  version.h version.cpp
  logging.h logging.cpp
  unixsignals.h unixsignals.cpp
  overlay.h overlay.cpp
  address.h address.cpp
  overlayjson.h overlayjson.cpp
  overlaystore.h overlaystore.cpp
  tokenstore.h tokenstore.cpp
)
target_include_directories(dss_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dss_common PRIVATE DSS_VERSION="${PROJECT_VERSION}")
target_link_libraries(dss_common PUBLIC Qt6::Core PRIVATE dss_warnings)
