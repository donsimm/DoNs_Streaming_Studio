# dss_render: Zeichnen eines Overlays ohne Fenster (P10-vor, Teil B 2). Qt Gui (QPainter, QImage,
# Schriften), aber kein Qt Widgets: der Zeichner der Leinwand (SourceRenderer, seit P09) und das
# Bild eines Overlays in seiner Auflösung (overlayimage.h). Das Fenster nutzt beides, später auch
# der zweite Zeichner (P11).
add_library(dss_render STATIC
  sourcerenderer.h sourcerenderer.cpp
  overlayimage.h overlayimage.cpp
)
target_include_directories(dss_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dss_render PUBLIC dss_common dss_media Qt6::Gui PRIVATE dss_warnings)
