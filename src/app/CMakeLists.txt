# dss-app: Fenster. Dünn, ohne Logik (CLAUDE.md Regel 9).
# Alles ausser main.cpp liegt in dss_app_ui, damit der Test des Fensters (tst_fensterbearbeiten,
# P08) dasselbe Fenster baut wie dss-app.
add_library(dss_app_ui STATIC
  mainwindow.h mainwindow.cpp
  theme.h theme.cpp
  symbols.h symbols.cpp
  fonts.h fonts.cpp
  headerbar.h headerbar.cpp
  gaugewidget.h gaugewidget.cpp
  canvaswidget.h canvaswidget.cpp
  docks.h docks.cpp
  dockviews.h dockviews.cpp
  dialogs.h dialogs.cpp
  mainwindowedit.cpp
  translation.h translation.cpp
)
target_include_directories(dss_app_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Symbole werden zur Laufzeit aus dem Quellordner geladen, damit der Nutzer sie ohne neuen
# Bau ersetzen kann (Auftrag P05). SVG braucht zur Laufzeit qt6-svg-plugins.
# Die Schrift (Open Sans, P06) kommt ebenso aus dem Quellordner.
target_compile_definitions(dss_app_ui PRIVATE DSS_SYMBOL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/symbole"
                                              DSS_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schriften")
target_link_libraries(dss_app_ui PUBLIC dss_common Qt6::Widgets PRIVATE dss_warnings)

# Bearbeiten im Fenster (P08, CLAUDE.md Regel 8). Aus: Docks nur zur Anzeige wie in P07.
option(DSS_APP_EDITING "Bearbeiten im Fenster (P08)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_EDITING=$<BOOL:${DSS_APP_EDITING}>)
# Befehle «Transformieren» wie OBS (P09b, E-25): Menü «Bearbeiten», Rechtsklick auf Leinwand und
# Dock «Quellen». Aus: Menü «Bearbeiten» leer und ausgegraut, kein Rechtsklick auf der Leinwand.
option(DSS_APP_TRANSFORM "Befehle Transformieren im Fenster (P09b)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_TRANSFORM=$<BOOL:${DSS_APP_TRANSFORM}>)

# Zeichner der Leinwand (P09, Teil B, Schalter DSS_APP_RENDERER in der obersten CMakeLists.txt).
if(DSS_APP_RENDERER)
  target_sources(dss_app_ui PRIVATE sourcerenderer.h sourcerenderer.cpp
                                    canvasmeasurement.h canvasmeasurement.cpp)
  target_link_libraries(dss_app_ui PUBLIC dss_media)
endif()
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_RENDERER=$<BOOL:${DSS_APP_RENDERER}>
                                             DSS_APP_CANVAS_EDIT=$<BOOL:${DSS_APP_CANVAS_EDIT}>)

# Übersetzung von Qt ohne «ß» (P09, Punkt 44): qtbase_de.qm beim Bau kopieren, «ß» → «ss».
# Quelle: Paket qt6-translations-l10n (Ordner der Übersetzungen von Qt, QtInstallPaths.cmake).
set(DSS_QT_TRANSLATION "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_de.qm"
    CACHE FILEPATH "Übersetzung von Qt, die ohne «ß» kopiert wird")
set(DSS_TRANSLATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/uebersetzung")
qt_add_executable(dss-qm-ohne-ss qm-ohne-ss.cpp)
target_link_libraries(dss-qm-ohne-ss PRIVATE dss_common dss_warnings)
add_custom_command(OUTPUT ${DSS_TRANSLATION_DIR}/qtbase_de.qm
  COMMAND dss-qm-ohne-ss ${DSS_QT_TRANSLATION} ${DSS_TRANSLATION_DIR}/qtbase_de.qm
  DEPENDS dss-qm-ohne-ss ${DSS_QT_TRANSLATION}
  COMMENT "Übersetzung von Qt ohne «ß»")
add_custom_target(dss_uebersetzung ALL DEPENDS ${DSS_TRANSLATION_DIR}/qtbase_de.qm)
add_dependencies(dss_app_ui dss_uebersetzung)
target_compile_definitions(dss_app_ui PRIVATE DSS_TRANSLATION_DIR="${DSS_TRANSLATION_DIR}")

qt_add_executable(dss-app main.cpp)
target_link_libraries(dss-app PRIVATE dss_app_ui dss_warnings)
