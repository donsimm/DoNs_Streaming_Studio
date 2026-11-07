# dss_obs: Anbindung an OBS über obs-websocket (P10c, T-05, E-29). Qt Core, Qt Network und Qt
# WebSockets, kein Qt Gui (CLAUDE.md Regel 9). Bindet die Schnittstelle zum Fenster (protocol.h)
# nicht ein: dss_common fehlt hier absichtlich.
add_library(dss_obs STATIC
  obsprotocol.h obsprotocol.cpp
  obsconnection.h obsconnection.cpp
)
target_include_directories(dss_obs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_obs PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets PRIVATE dss_warnings)
