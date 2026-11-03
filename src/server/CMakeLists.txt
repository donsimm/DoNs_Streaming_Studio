# dss_server: Overlay-Server im Kern für die Browserquelle (P10, Teil A, Schalter
# DSS_CORE_OVERLAY_SERVER). Qt Core und Qt Network, kein Qt Gui (CLAUDE.md Regel 9).
add_library(dss_server STATIC
  httpmessage.h httpmessage.cpp
  mediapatch.h mediapatch.cpp
  portowner.h portowner.cpp
  overlayserver.h overlayserver.cpp
)
target_include_directories(dss_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_server PUBLIC dss_common Qt6::Core Qt6::Network PRIVATE dss_warnings)
