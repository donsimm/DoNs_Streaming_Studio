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

qt_add_executable(dss-app main.cpp)
target_link_libraries(dss-app PRIVATE dss_app_ui dss_warnings)
