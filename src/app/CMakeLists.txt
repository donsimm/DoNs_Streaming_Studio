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
  obssourcedialog.h obssourcedialog.cpp
  mainwindowedit.cpp
  translation.h translation.cpp
)
target_include_directories(dss_app_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Symbole werden zur Laufzeit aus dem Quellordner geladen, damit der Nutzer sie ohne neuen
# Bau ersetzen kann (Auftrag P05). SVG braucht zur Laufzeit qt6-svg-plugins.
# Die Schrift (Open Sans, P06) kommt ebenso aus dem Quellordner, das Programm-Symbol (P10a) aus
# assets/branding/symbol. Im Einsatz (P10a) liegen alle drei im Installationsordner.
if(DSS_INSTALLED_EDITION)
  set(DSS_SYMBOL_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/symbole")
  set(DSS_FONT_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/schriften")
  set(DSS_APP_ICON_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/branding")
  install(DIRECTORY symbole/ DESTINATION ${DSS_INSTALL_SHARE_DIR}/symbole)
  install(DIRECTORY schriften/ DESTINATION ${DSS_INSTALL_SHARE_DIR}/schriften)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets/branding/symbol/
          DESTINATION ${DSS_INSTALL_SHARE_DIR}/branding)
else()
  set(DSS_SYMBOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/symbole")
  set(DSS_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schriften")
  set(DSS_APP_ICON_DIR "${PROJECT_SOURCE_DIR}/assets/branding/symbol")
endif()
target_compile_definitions(dss_app_ui PRIVATE DSS_SYMBOL_DIR="${DSS_SYMBOL_DIR}"
                                              DSS_FONT_DIR="${DSS_FONT_DIR}")
target_link_libraries(dss_app_ui PUBLIC dss_common Qt6::Widgets PRIVATE dss_warnings)

# Bearbeiten im Fenster (P08, CLAUDE.md Regel 8). Aus: Docks nur zur Anzeige wie in P07.
option(DSS_APP_EDITING "Bearbeiten im Fenster (P08)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_EDITING=$<BOOL:${DSS_APP_EDITING}>)
# Befehle «Transformieren» wie OBS (P09b, E-25): Menü «Bearbeiten», Rechtsklick auf Leinwand und
# Dock «Quellen». Aus: Menü «Bearbeiten» leer und ausgegraut, kein Rechtsklick auf der Leinwand.
option(DSS_APP_TRANSFORM "Befehle Transformieren im Fenster (P09b)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_TRANSFORM=$<BOOL:${DSS_APP_TRANSFORM}>)
# Dialog «Werkzeuge → Einstellungen …» (P10-vor, Teil B 1): Port und Adresse des Kerns. Aus:
# Menü «Werkzeuge» leer und ausgegraut wie bis P10a; dss-cli kann die Werte weiter ändern.
option(DSS_APP_SETTINGS_DIALOG "Dialog Einstellungen des Kerns (P10-vor)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_SETTINGS_DIALOG=$<BOOL:${DSS_APP_SETTINGS_DIALOG}>)
if(DSS_APP_SETTINGS_DIALOG)
  target_sources(dss_app_ui PRIVATE settingsdialog.h settingsdialog.cpp)
endif()

# Browserquelle im Fenster (P10, Teil C): Feld mit Adresse, «Kopieren», «Neuer Token», Anzeige
# «Ausgabe: …» im Dock «Steuerung», Schalter «Ausgabe ein/aus» in der Liste «Overlays», Meldungen
# des Overlay-Servers. Aus: Felder ausgegraut wie bis P10-vor; dss-cli kann weiter alles.
option(DSS_APP_BROWSER_SOURCE "Browserquelle im Fenster (P10)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_BROWSER_SOURCE=$<BOOL:${DSS_APP_BROWSER_SOURCE}>)

# Dock «Medien» (P10b, E-28): Dateien im Datenordner mit Name, Ordner, Vorschau; Hinzufügen,
# Umbenennen, Ordner, Löschen, Ziehen auf die Leinwand und in die Liste «Quellen». Aus: kein Dock,
# Anordnung wie bis P10a; dss-cli kann die Medienliste weiter ändern.
option(DSS_APP_MEDIA_DOCK "Dock Medien im Fenster (P10b)" ON)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_MEDIA_DOCK=$<BOOL:${DSS_APP_MEDIA_DOCK}>)
if(DSS_APP_MEDIA_DOCK)
  target_sources(dss_app_ui PRIVATE mainwindowmedia.cpp)
endif()

# Zeichner der Leinwand (P09, Teil B, Schalter DSS_APP_RENDERER in der obersten CMakeLists.txt).
# Seit P10-vor in dss_render (src/render, ohne Qt Widgets).
if(DSS_APP_RENDERER)
  target_sources(dss_app_ui PRIVATE canvasmeasurement.h canvasmeasurement.cpp)
  target_link_libraries(dss_app_ui PUBLIC dss_render)
endif()
# «Datei → Leinwand als Bild speichern» (P10-vor, Teil B 2), braucht den Zeichner. Aus: kein
# Menüeintrag.
option(DSS_APP_CANVAS_IMAGE "Leinwand als Bild speichern (P10-vor)" ON)
target_compile_definitions(dss_app_ui PUBLIC
  DSS_APP_CANVAS_IMAGE=$<AND:$<BOOL:${DSS_APP_CANVAS_IMAGE}>,$<BOOL:${DSS_APP_RENDERER}>>)
target_compile_definitions(dss_app_ui PUBLIC DSS_APP_RENDERER=$<BOOL:${DSS_APP_RENDERER}>
                                             DSS_APP_CANVAS_EDIT=$<BOOL:${DSS_APP_CANVAS_EDIT}>)

# Übersetzung von Qt ohne «ß» (P09, Punkt 44): qtbase_de.qm beim Bau kopieren, «ß» → «ss».
# Quelle: Paket qt6-translations-l10n (Ordner der Übersetzungen von Qt, QtInstallPaths.cmake).
set(DSS_QT_TRANSLATION "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_de.qm"
    CACHE FILEPATH "Übersetzung von Qt, die ohne «ß» kopiert wird")
set(DSS_TRANSLATION_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/uebersetzung")
if(DSS_INSTALLED_EDITION)
  set(DSS_TRANSLATION_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/uebersetzung")
  install(FILES ${DSS_TRANSLATION_BUILD_DIR}/qtbase_de.qm DESTINATION ${DSS_INSTALL_SHARE_DIR}/uebersetzung)
else()
  set(DSS_TRANSLATION_DIR "${DSS_TRANSLATION_BUILD_DIR}")
endif()
qt_add_executable(dss-qm-ohne-ss qm-ohne-ss.cpp)
target_link_libraries(dss-qm-ohne-ss PRIVATE dss_common dss_warnings)
add_custom_command(OUTPUT ${DSS_TRANSLATION_BUILD_DIR}/qtbase_de.qm
  COMMAND dss-qm-ohne-ss ${DSS_QT_TRANSLATION} ${DSS_TRANSLATION_BUILD_DIR}/qtbase_de.qm
  DEPENDS dss-qm-ohne-ss ${DSS_QT_TRANSLATION}
  COMMENT "Übersetzung von Qt ohne «ß»")
add_custom_target(dss_uebersetzung ALL DEPENDS ${DSS_TRANSLATION_BUILD_DIR}/qtbase_de.qm)
add_dependencies(dss_app_ui dss_uebersetzung)
target_compile_definitions(dss_app_ui PRIVATE DSS_TRANSLATION_DIR="${DSS_TRANSLATION_DIR}")

qt_add_executable(dss-app main.cpp)
target_link_libraries(dss-app PRIVATE dss_app_ui dss_warnings)
# Programm-Symbol als Fenstersymbol beider Versionen (P10a)
target_compile_definitions(dss-app PRIVATE DSS_APP_ICON_DIR="${DSS_APP_ICON_DIR}")
if(DSS_INSTALLED_EDITION)
  install(TARGETS dss-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
