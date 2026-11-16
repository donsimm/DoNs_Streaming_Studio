# dss_twitch: Anbindung an Twitch (P13, Plan 2.6): EventSub über WebSocket, Anmeldung mit Gerätecode,
# Helix. Qt Core, Network und WebSockets, kein Qt Gui und kein protocol.h (Regel 9); bindet den
# Ereignisfluss (dss_events) und die Geheimnisse (dss_secrets) ein.
add_library(dss_twitch STATIC
  eventsubprotocol.h eventsubprotocol.cpp
  chatsession.h chatsession.cpp
  twitchendpoints.h
  twitchauth.h twitchauth.cpp
  helixclient.h helixclient.cpp
  chatmessage.h chatmessage.cpp # P14 c1a: Send Chat Message
  helixactions.h helixactions.cpp # P14 c1b: Benutzerinfo, Shoutout, Ansage
  eventsubconnection.h eventsubconnection.cpp
  twitchservice.h twitchservice.cpp
)
target_include_directories(dss_twitch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_twitch PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets dss_events dss_secrets
  PRIVATE dss_warnings)
