# dss-app: Fenster. Dünn, ohne Logik (CLAUDE.md Regel 9).
qt_add_executable(dss-app
  main.cpp
  mainwindow.h mainwindow.cpp
  theme.h theme.cpp
  symbols.h symbols.cpp
  headerbar.h headerbar.cpp
  canvaswidget.h canvaswidget.cpp
  docks.h docks.cpp
)
# Symbole werden zur Laufzeit aus dem Quellordner geladen, damit der Nutzer sie ohne neuen
# Bau ersetzen kann (Auftrag P05). SVG braucht zur Laufzeit qt6-svg-plugins.
target_compile_definitions(dss-app PRIVATE DSS_SYMBOL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/symbole")
target_link_libraries(dss-app PRIVATE dss_common dss_warnings Qt6::Widgets)
