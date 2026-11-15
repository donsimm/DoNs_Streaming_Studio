# dss_actions: Modell des Aktionssystems (P14 a, Plan 2 und 3.1). Nur Qt Core, kein Qt Gui, kein
# Netz und kein protocol.h (CLAUDE.md Regel 9 und 14): dss_common fehlt hier absichtlich.
# Ablauf der Läufe (b1): actionengine, actionrun, Uhr und Schnittstellen nach aussen.
# Auslöser ohne Netz (b2): triggerevent (Ereignis → Argumente), triggerdispatcher (Filter, Befehle,
# Zeitnachrichten). b3: actionedit (Änderungen an aktionen.json); die Anbindung an den Ereignisfluss
# und an den Kern steht in src/core/actionservice.cpp, darum bleibt dss_events hier draussen.
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
  actionclock.h actionclock.cpp
  actionservices.h
  actionengine.h actionengine.cpp
  actionrun.h actionrun.cpp
  triggerevent.h triggerevent.cpp
  triggerdispatcher.h triggerdispatcher.cpp
  actionedit.h actionedit.cpp # b3: Änderungen an aktionen.json (Kern)
  chatoutbox.h chatoutbox.cpp # c1a: Chat senden (Grenzen, Kürzen, Schleifen), ohne Netz
)
target_include_directories(dss_actions PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_actions PUBLIC Qt6::Core PRIVATE dss_warnings)
