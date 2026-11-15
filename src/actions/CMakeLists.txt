# dss_actions: Modell des Aktionssystems (P14 a, Plan 2 und 3.1). Nur Qt Core, kein Qt Gui, kein
# Netz und kein protocol.h (CLAUDE.md Regel 9 und 14): dss_common fehlt hier absichtlich.
# Die Anbindung an den Ereignisfluss (dss_events, P13) kommt mit b2, die an den Kern mit b3.
add_library(dss_actions STATIC
  fieldspec.h fieldspec.cpp
  steptypes.h steptypes.cpp
  triggertypes.h triggertypes.cpp
  actionmodel.h actionmodel.cpp
  actionjson.h actionjson.cpp
  actionrefs.h actionrefs.cpp
  placeholders.h placeholders.cpp
  variables.h variables.cpp
  viewerdata.h viewerdata.cpp
)
target_include_directories(dss_actions PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_actions PUBLIC Qt6::Core PRIVATE dss_warnings)
