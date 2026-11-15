# dss_events: Ereignisfluss des Kerns (P13, Plan 1.1, Regel 14, Punkt 92): Fluss, Typenliste mit
# Argumentnamen für das Aktionssystem, Verlauf. Nur Qt Core, ohne Qt Gui und ohne protocol.h
# (Regel 9): jedes Modul bindet sie ohne Kern ein.
add_library(dss_events STATIC
  event.h event.cpp
  eventtypes.h eventtypes.cpp
  eventbus.h eventbus.cpp
)
target_include_directories(dss_events PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_events PUBLIC Qt6::Core PRIVATE dss_warnings)
