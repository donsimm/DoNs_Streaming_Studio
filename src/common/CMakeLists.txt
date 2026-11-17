# dss_common: Logik und Daten für Kern und Fenster. Qt Core und Qt Network (lokale Verbindung
# zum Kern, P07), kein Qt Gui (CLAUDE.md Regel 9).
add_library(dss_common STATIC
  version.h version.cpp
  logging.h logging.cpp
  unixsignals.h unixsignals.cpp
  overlay.h overlay.cpp
  address.h address.cpp
  overlayjson.h overlayjson.cpp
  overlaystore.h overlaystore.cpp
  overlaylist.h overlaylist.cpp
  fontcatalog.h fontcatalog.cpp
  savescheduler.h savescheduler.cpp
  tokenstore.h tokenstore.cpp
  canvasview.h canvasview.cpp
  connectionstate.h connectionstate.cpp
  windowsize.h windowsize.cpp
  configpaths.h configpaths.cpp
  datalock.h datalock.cpp
  systemstats.h systemstats.cpp
  protocol.h protocol.cpp
  coreclient.h coreclient.cpp
  windowdata.h windowdata.cpp
  windowedit.h windowedit.cpp
  systemdnotify.h systemdnotify.cpp
  qmfile.h qmfile.cpp
  sourcegeometry.h sourcegeometry.cpp
  canvasedit.h canvasedit.cpp
  imageorientation.h imageorientation.cpp
  transform.h transform.cpp
  dataformat.h dataformat.cpp dataformattesting.h
  mediaimport.h mediaimport.cpp
  medialibrary.h medialibrary.cpp
  videoformat.h videoformat.cpp
  coresettings.h coresettings.cpp
  coresession.h coresession.cpp
  browsersource.h browsersource.cpp
  canvasimage.h canvasimage.cpp
  overlaylive.h overlaylive.cpp
  overlaylivestate.h overlaylivestate.cpp
  actionview.h actionview.cpp
  actionfields.h actionfields.cpp # P14 e2-8: Felder der rechten Spalte im Unterreiter «Aktionen»
  actionprotocol.h actionprotocol.cpp # P14 b3: Nachrichten des Aktionssystems
  chatbotlive.h chatbotlive.cpp # P14 e2-5: Ansichtslogik «Chat-Bot» › «Live»
  emotes.h emotes.cpp # P14 Emotes em-2: Listen von BTTV, 7TV, FFZ und Bild-Adressen
  emoteloader.h emoteloader.cpp # P14 Emotes em-4: Laden mit HTTP-Zwischenspeicher
  chatbotoverlay.h chatbotoverlay.cpp # P14 f2: mitgeliefertes Overlay «Chat-Bot»
  cameraprotocol.h cameraprotocol.cpp # P15 d2: Nachrichten zum Kamera-Dienst
)
# P14 f2: Platzhalterbild des Overlays «Chat-Bot», beim Bau von dss-platzhalterbild gezeichnet (Qt
# Gui nur in diesem Werkzeug, dss_common bleibt ohne) und als Qt-Ressource
# :/dss/vorlage/chatbot-platzhalter.png eingebunden (wie die Vorlage der Aktionen in dss_actions).
# https://doc.qt.io/qt-6.8/qt-add-resources.html
qt_add_executable(dss-platzhalterbild platzhalterbild.cpp)
target_link_libraries(dss-platzhalterbild PRIVATE Qt6::Gui dss_warnings)
set(DSS_PLACEHOLDER_DIR "${CMAKE_CURRENT_BINARY_DIR}/vorlage")
add_custom_command(OUTPUT "${DSS_PLACEHOLDER_DIR}/chatbot-platzhalter.png"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DSS_PLACEHOLDER_DIR}"
  COMMAND dss-platzhalterbild "${DSS_PLACEHOLDER_DIR}/chatbot-platzhalter.png"
  DEPENDS dss-platzhalterbild VERBATIM)
qt_add_resources(dss_common dss_common_vorlage PREFIX "/dss/vorlage" BASE "${DSS_PLACEHOLDER_DIR}"
  FILES "${DSS_PLACEHOLDER_DIR}/chatbot-platzhalter.png")
target_include_directories(dss_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Mitgelieferte Schrift Be Vietnam Pro (P08, Teil F): Kern (Standardschrift neuer Textquellen) und
# Fenster (Auswahl) finden sie in Arbeit hier im Quellbaum, wie die Symbole des Fensters (P05),
# im Einsatz im Installationsordner (P10a).
if(DSS_INSTALLED_EDITION)
  set(DSS_BUILTIN_FONT_DIR "${DSS_INSTALL_FULL_SHARE_DIR}/be-vietnam-pro")
  install(DIRECTORY schriften/be-vietnam-pro/ DESTINATION ${DSS_INSTALL_SHARE_DIR}/be-vietnam-pro)
else()
  set(DSS_BUILTIN_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schriften/be-vietnam-pro")
endif()
target_compile_definitions(dss_common PRIVATE DSS_VERSION="${PROJECT_VERSION}"
  DSS_COMMIT="${DSS_COMMIT}" DSS_BUILTIN_FONT_DIR="${DSS_BUILTIN_FONT_DIR}")
# dss_actions (P14 a): Prüfen der Datenformate (dataformat.cpp); seit e1 öffentlich, weil die
# Ansichtslogik des Editors (actionview.h) das Modell der Aktionen in ihrer Schnittstelle nutzt.
# dss_events (P13): Ereignisse in der Schnittstelle (subscribe_events, event, event_history)
target_link_libraries(dss_common PUBLIC Qt6::Core Qt6::Network dss_actions dss_events PRIVATE dss_warnings)
# Version in Arbeit oder im Einsatz (P10a, Teil B, Schalter in der obersten CMakeLists.txt)
target_compile_definitions(dss_common PRIVATE
  DSS_INSTALLED_EDITION=$<BOOL:${DSS_INSTALLED_EDITION}>)
