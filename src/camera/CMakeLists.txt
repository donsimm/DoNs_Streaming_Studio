# dss_camera: Anbindung an den Kamera-Dienst auf dem Streaming-PC (P15 d1, Plan 7.). Qt Core und
# Qt Network, kein Qt Gui (CLAUDE.md Regel 9). Bindet die Schnittstelle zum Fenster (protocol.h)
# nicht ein: dss_common fehlt hier absichtlich.
add_library(dss_camera STATIC
  sseparser.h sseparser.cpp
  cameraserviceclient.h cameraserviceclient.cpp
)
target_include_directories(dss_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_camera PUBLIC Qt6::Core Qt6::Network PRIVATE dss_warnings)
