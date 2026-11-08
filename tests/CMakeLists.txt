# Tests mit Qt Test, ausgeführt über ctest. Nur Qt Core und Qt Test, kein Qt Gui
# (ausser den Tests des Fensters unter DSS_BUILD_APP).
# https://doc.qt.io/qt-6.8/qttest-index.html
function(dss_add_test name)
  qt_add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE dss_common dss_warnings Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

dss_add_test(tst_version)
target_compile_definitions(tst_version PRIVATE DSS_EXPECTED_VERSION="${PROJECT_VERSION}")

dss_add_test(tst_unixsignals)

dss_add_test(tst_overlaymodel)

dss_add_test(tst_canvasview)

dss_add_test(tst_connectionstate)

dss_add_test(tst_windowsize)

dss_add_test(tst_configpaths)

# P10a, Punkt 41: Watchdog-Zeit und Meldung an systemd
dss_add_test(tst_systemdnotify)

# Testdateien wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus
# prototypes/messung/media_erzeugen.sh, Schriftdatei aus dem System (fc-list).
set(DSS_TEST_MEDIA_DIR "${PROJECT_SOURCE_DIR}/prototypes/media" CACHE PATH "Testdateien (Bild, Video)")
set(DSS_TEST_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" CACHE FILEPATH "Schriftdatei für Tests")
dss_add_test(tst_overlaystore)
target_compile_definitions(tst_overlaystore PRIVATE
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}"
  DSS_FORMAT_DOC="${PROJECT_SOURCE_DIR}/docs/06-overlay-format.md")
# Erzeugt die Testdateien, falls sie fehlen. Braucht ffmpeg (installiert seit P01, nicht in scripts/pakete.txt).
add_test(NAME media_vorbereiten
  COMMAND sh -c "test -f '${DSS_TEST_MEDIA_DIR}/alpha.webm' || sh '${PROJECT_SOURCE_DIR}/prototypes/messung/media_erzeugen.sh' '${DSS_TEST_MEDIA_DIR}'")
set_tests_properties(media_vorbereiten PROPERTIES FIXTURES_SETUP dss_media)
set_tests_properties(tst_overlaystore PROPERTIES FIXTURES_REQUIRED dss_media)

# P10b: Namen und Ordner der Mediendateien (medien.json), ohne Qt Gui und ohne Kern-Prozess.
dss_add_test(tst_medienliste)
target_compile_definitions(tst_medienliste PRIVATE DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}"
  DSS_FORMAT_DOC="${PROJECT_SOURCE_DIR}/docs/06-overlay-format.md")
set_tests_properties(tst_medienliste PROPERTIES FIXTURES_REQUIRED dss_media)

# P07: Schnittstelle, Leistungswerte, Aufbereitung für das Fenster (ohne Qt Gui).
dss_add_test(tst_protocol)
dss_add_test(tst_windowdata)
dss_add_test(tst_systemstats)
target_compile_definitions(tst_systemstats PRIVATE DSS_TEST_PROC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/proc")
# P10-vor: Einstellungen des Kerns (kern.ini), ohne Qt Gui.
dss_add_test(tst_kerneinstellungen)
target_compile_definitions(tst_kerneinstellungen PRIVATE DSS_TEST_PROC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/proc")

# P07: Tests mit dss-core (und dss-cli) als eigenen Prozessen, eigener Datenordner und eigene
# Verbindung in $XDG_RUNTIME_DIR. Nur Qt Core, Network und Test.
dss_add_test(tst_sperre)
target_compile_definitions(tst_sperre PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>")
if(DSS_BUILD_CLI)
  target_compile_definitions(tst_sperre PRIVATE DSS_CLI_BIN="$<TARGET_FILE:dss-cli>")
  add_dependencies(tst_sperre dss-cli)
endif()
add_dependencies(tst_sperre dss-core)
dss_add_test(tst_kernverbindung)
target_link_libraries(tst_kernverbindung PRIVATE dss_obs) # P10c: Testserver von OBS
target_compile_definitions(tst_kernverbindung PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}")
add_dependencies(tst_kernverbindung dss-core)
target_compile_definitions(tst_kernverbindung PRIVATE
  DSS_CORE_OVERLAY_SERVER=$<BOOL:${DSS_CORE_OVERLAY_SERVER}>)
if(DSS_BUILD_CLI) # P10-vor: dss-cli einstellungen
  target_compile_definitions(tst_kernverbindung PRIVATE DSS_CLI_BIN="$<TARGET_FILE:dss-cli>")
  add_dependencies(tst_kernverbindung dss-cli)
endif()

# P10a, Teil B 4: Datenformate prüfen und umwandeln, mit Testdaten im Format 1 bis 3
dss_add_test(tst_datenformat)
target_compile_definitions(tst_datenformat PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
  DSS_TEST_FORMAT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/formate")
add_dependencies(tst_datenformat dss-core)
set_tests_properties(tst_kernverbindung PROPERTIES FIXTURES_REQUIRED dss_media)

# Schrift des Fensters (P06): lädt src/app/schriften/ ohne Fenster (Plattform offscreen).
# Braucht Qt Gui, deshalb nur mit dem Fenster (DSS_BUILD_APP).
if(DSS_BUILD_APP)
  qt_add_executable(tst_fonts tst_fonts.cpp ${PROJECT_SOURCE_DIR}/src/app/fonts.cpp)
  target_include_directories(tst_fonts PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
  target_compile_definitions(tst_fonts PRIVATE DSS_FONT_DIR="${PROJECT_SOURCE_DIR}/src/app/schriften")
  target_link_libraries(tst_fonts PRIVATE dss_warnings Qt6::Gui Qt6::Test)
  add_test(NAME tst_fonts COMMAND tst_fonts)
  set_tests_properties(tst_fonts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Symbole einfarbig (P06, Punkt 28), in der X11-Sitzung; ohne X11 übersprungen (77).
  qt_add_executable(tst_symbols tst_symbols.cpp ${PROJECT_SOURCE_DIR}/src/app/symbols.cpp)
  target_include_directories(tst_symbols PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
  target_compile_definitions(tst_symbols PRIVATE DSS_SYMBOL_DIR="${PROJECT_SOURCE_DIR}/src/app/symbole"
    DSS_TEST_APP_ICON_DIR="${PROJECT_SOURCE_DIR}/assets/branding/symbol")
  target_link_libraries(tst_symbols PRIVATE dss_common dss_warnings Qt6::Widgets Qt6::Test)
  add_test(NAME tst_symbols COMMAND tst_symbols)
  # Eigener Ordner der Einstellungen, damit nie die Symbole des Nutzers gelesen werden (P07).
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/symbole-config)
  set_tests_properties(tst_symbols PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/symbole-config")
endif()

# Bildschirmfoto des Fensters in der X11-Sitzung (P05). Nur mit dem Fenster (DSS_BUILD_APP).
# Ohne X11-Sitzung meldet ctest den Test als übersprungen (Exit-Code 77).
if(DSS_BUILD_APP)
  find_program(DSS_PYTHON3 python3 REQUIRED)
  add_test(NAME bildschirmfoto
    COMMAND ${DSS_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/bildschirmfoto_pruefen.py
            $<TARGET_FILE:dss-app> ${CMAKE_CURRENT_BINARY_DIR}/bildschirmfoto)
  set_tests_properties(bildschirmfoto PROPERTIES SKIP_RETURN_CODE 77)
endif()

# P08: Reihenfolge und Ordner der Overlay-Liste (liste.json), ohne Qt Gui.
dss_add_test(tst_overlayliste)

# P08: Schriften (fc-list, fc-query, Auflösung Name und Schnitt → Datei), ohne Qt Gui.
dss_add_test(tst_schriften)
target_compile_definitions(tst_schriften PRIVATE DSS_TEST_FONT_DATA="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften")

# P08: Bearbeiten über den Kern mit zwei Verbindungen, Neustart (dss-core als eigener Prozess).
dss_add_test(tst_bearbeiten)
target_compile_definitions(tst_bearbeiten PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}")
add_dependencies(tst_bearbeiten dss-core)
set_tests_properties(tst_bearbeiten PROPERTIES FIXTURES_REQUIRED dss_media)

# P08: Änderungen (Nachrichten, Abweisungen, Modell, Zusammenfassen beim Speichern), ohne Qt Gui.
dss_add_test(tst_aenderungen)
target_compile_definitions(tst_aenderungen PRIVATE
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}")
set_tests_properties(tst_aenderungen PROPERTIES FIXTURES_REQUIRED dss_media)

# P08: Rechnungen des Fensters fürs Bearbeiten (Quellen, Adresse, Drehung, Farbe, Schrift), ohne Qt Gui.
dss_add_test(tst_windowedit)
target_compile_definitions(tst_windowedit PRIVATE DSS_TEST_FONT_DATA="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften")

# P08: Bearbeiten im echten Fenster (MainWindow aus dss_app_ui) mit eigenem dss-core, in der
# X11-Sitzung; ohne X11 übersprungen (77). Eigener Ordner der Einstellungen wie bei tst_symbols.
if(DSS_BUILD_APP)
  qt_add_executable(tst_fensterbearbeiten tst_fensterbearbeiten.cpp)
  target_link_libraries(tst_fensterbearbeiten PRIVATE dss_app_ui dss_obs dss_warnings Qt6::Test)
  target_compile_definitions(tst_fensterbearbeiten PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
    DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}"
    DSS_TEST_LARGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/gross-daten"
    DSS_CORE_OVERLAY_SERVER=$<BOOL:${DSS_CORE_OVERLAY_SERVER}>)
  # Kern liest die Originalgrösse nur mit DSS_MEDIA_SIZE (P09b, Seitenverhältnis an den Ecken)
  if(DSS_MEDIA_SIZE)
    target_compile_definitions(tst_fensterbearbeiten PRIVATE DSS_MEDIA_SIZE=1)
  else()
    target_compile_definitions(tst_fensterbearbeiten PRIVATE DSS_MEDIA_SIZE=0)
  endif()
  add_dependencies(tst_fensterbearbeiten dss-core)
  add_test(NAME tst_fensterbearbeiten COMMAND tst_fensterbearbeiten)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fenster-config)
  set_tests_properties(tst_fensterbearbeiten PROPERTIES SKIP_RETURN_CODE 77
    FIXTURES_REQUIRED dss_media
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/fenster-config")
endif()

# P09: Rechnungen des Zeichners und des Bearbeitens auf der Leinwand, ohne Qt Gui.
dss_add_test(tst_sourcegeometry)
target_compile_definitions(tst_sourcegeometry PRIVATE
  DSS_TEST_FONT_DATA="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften" DSS_TEST_FONT="${DSS_TEST_FONT}"
  DSS_TEST_BUILTIN_FONT="${PROJECT_SOURCE_DIR}/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf")
dss_add_test(tst_canvasedit)
# P09b: Befehle «Transformieren» wie OBS (E-25), ohne Qt Gui
dss_add_test(tst_transform)
# P09b: EXIF-Ausrichtung (Lesen und Rechnen), ohne Qt Gui; Bilder aus tests/daten/exif/erzeugen.py
dss_add_test(tst_imageorientation)
target_compile_definitions(tst_imageorientation PRIVATE
  DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif" DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}")
set_tests_properties(tst_imageorientation PROPERTIES FIXTURES_REQUIRED dss_media)

# P09: Übersetzung von Qt ohne «ß» (Punkt 44), ohne Qt Gui.
dss_add_test(tst_qmfile)
if(DSS_BUILD_APP)
  target_compile_definitions(tst_qmfile PRIVATE DSS_QT_TRANSLATION="${DSS_QT_TRANSLATION}"
    DSS_BUILT_TRANSLATION="${CMAKE_BINARY_DIR}/src/app/uebersetzung/qtbase_de.qm")
  add_dependencies(tst_qmfile dss_uebersetzung)
else()
  target_compile_definitions(tst_qmfile PRIVATE
    DSS_QT_TRANSLATION="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_de.qm")
endif()

# P09: Bilder und Videos (dss_media): Grösse, Videospieler mit Ende, dss-cli. Ohne Qt Gui.
if(TARGET dss_media)
  dss_add_test(tst_medien)
  target_link_libraries(tst_medien PRIVATE dss_media)
  # prozesshilfe.h braucht DSS_CORE_BIN, auch wenn hier nur dss-cli läuft.
  target_compile_definitions(tst_medien PRIVATE DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}"
    DSS_CORE_BIN="$<TARGET_FILE:dss-core>" DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
    DSS_TEST_VIDEO_ROTATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/video-drehung"
    DSS_VIDEO_ROTATION=$<BOOL:${DSS_VIDEO_ROTATION}>
    DSS_MEDIA_SIZE=$<BOOL:${DSS_MEDIA_SIZE}>)
  if(DSS_BUILD_CLI AND DSS_MEDIA_SIZE)
    target_compile_definitions(tst_medien PRIVATE DSS_CLI_BIN="$<TARGET_FILE:dss-cli>")
    add_dependencies(tst_medien dss-cli)
  endif()
  add_dependencies(tst_medien dss-core) # P10b: Kern für Videoformate und Lage
  set_tests_properties(tst_medien PROPERTIES FIXTURES_REQUIRED dss_media)
endif()

# P09: Zeichner der Leinwand in der X11-Sitzung (Bild, gedreht, Text, WebM-Alpha, verborgen,
# variable Schrift); ohne X11 übersprungen (77). Nur mit dem Zeichner (DSS_APP_RENDERER).
if(DSS_BUILD_APP AND DSS_APP_RENDERER)
  qt_add_executable(tst_leinwand tst_leinwand.cpp)
  target_link_libraries(tst_leinwand PRIVATE dss_app_ui dss_media dss_warnings Qt6::Test)
  target_compile_definitions(tst_leinwand PRIVATE DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}"
    DSS_TEST_FONT="${DSS_TEST_FONT}"
    DSS_TEST_VARIABLE_FONT="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften/urbanist/Urbanist[wght].ttf"
    DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
    DSS_TEST_VIDEO_ROTATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/video-drehung"
    DSS_VIDEO_ROTATION=$<BOOL:${DSS_VIDEO_ROTATION}>)
  add_test(NAME tst_leinwand COMMAND tst_leinwand)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/leinwand-config)
  set_tests_properties(tst_leinwand PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED dss_media
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/leinwand-config")
endif()

# P10-vor, Teil B 2: Bild der Leinwand ohne Fenster (dss_render, QGuiApplication, kein Qt
# Widgets) in der X11-Sitzung.
if(DSS_BUILD_APP AND DSS_APP_RENDERER)
  qt_add_executable(tst_leinwandbild tst_leinwandbild.cpp)
  target_link_libraries(tst_leinwandbild PRIVATE dss_render dss_warnings Qt6::Test)
  target_compile_definitions(tst_leinwandbild PRIVATE DSS_TEST_FONT="${DSS_TEST_FONT}"
    DSS_TEST_VARIABLE_FONT="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften/urbanist/Urbanist[wght].ttf"
    DSS_TEST_BUILTIN_FONT_DIR="${PROJECT_SOURCE_DIR}/src/common/schriften/be-vietnam-pro"
    DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
    DSS_TEST_VIDEO_ROTATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/video-drehung"
    DSS_VIDEO_ROTATION=$<BOOL:${DSS_VIDEO_ROTATION}>)
  add_test(NAME tst_leinwandbild COMMAND tst_leinwandbild)
  set_tests_properties(tst_leinwandbild PROPERTIES SKIP_RETURN_CODE 77)
endif()

# P10-vor (Punkte 67, 68): Übernehmen im eigenen Thread, Aufräumen beim Start, ohne Qt Gui;
# Kern als eigener Prozess mit eigenem Datenordner.
dss_add_test(tst_uebernehmen)
target_compile_definitions(tst_uebernehmen PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
  DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}")
add_dependencies(tst_uebernehmen dss-core)
set_tests_properties(tst_uebernehmen PROPERTIES FIXTURES_REQUIRED dss_media)

# P10, Teil A: Overlay-Server ohne Fenster (Token, alte Adressen, Ereignisse, Medien, Port belegt,
# Adresse fehlt, nur IPv4). Nur mit dem Server (DSS_CORE_OVERLAY_SERVER) und dss_media.
if(DSS_CORE_OVERLAY_SERVER AND TARGET dss_media)
  dss_add_test(tst_overlayserver)
  target_link_libraries(tst_overlayserver PRIVATE dss_server dss_media)
  target_compile_definitions(tst_overlayserver PRIVATE DSS_WEB_DIR="${PROJECT_SOURCE_DIR}/web"
    DSS_TEST_FONT="${DSS_TEST_FONT}" DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
    DSS_TEST_VIDEO_ROTATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/video-drehung"
    DSS_VIDEO_ROTATION=$<BOOL:${DSS_VIDEO_ROTATION}>)
endif()

# P10c: Protokoll von obs-websocket, ohne Verbindung und ohne Qt Gui.
qt_add_executable(tst_obsprotokoll tst_obsprotokoll.cpp)
target_link_libraries(tst_obsprotokoll PRIVATE dss_obs dss_warnings Qt6::Test)
add_test(NAME tst_obsprotokoll COMMAND tst_obsprotokoll)
# P10c: Verbindung zu OBS gegen den Testserver (obstestserver.h) auf 127.0.0.1, ohne Kern.
qt_add_executable(tst_obsverbindung tst_obsverbindung.cpp obstestserver.h)
target_link_libraries(tst_obsverbindung PRIVATE dss_obs dss_warnings Qt6::Test)
add_test(NAME tst_obsverbindung COMMAND tst_obsverbindung)
# P10c: Passwort von OBS mit systemd-creds --user, nur zufällige Testwerte.
qt_add_executable(tst_obspasswort tst_obspasswort.cpp)
target_link_libraries(tst_obspasswort PRIVATE dss_obs dss_warnings Qt6::Test)
add_test(NAME tst_obspasswort COMMAND tst_obspasswort)

qt_add_executable(tst_obsquellen tst_obsquellen.cpp obstestserver.h)
target_link_libraries(tst_obsquellen PRIVATE dss_obs dss_warnings Qt6::Test)
add_test(NAME tst_obsquellen COMMAND tst_obsquellen)

# P11: Vergleichslogik der zwei Zeichner (Masszahlen, Kantenmaske), Qt Gui nur für QImage, ohne
# Fenster. Genutzt von tst_browserquelle und tst_bildvergleich.
if(DSS_BUILD_APP)
  add_library(dss_bildvergleich STATIC bildvergleich.cpp bildvergleich.h)
  target_include_directories(dss_bildvergleich PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(dss_bildvergleich PUBLIC Qt6::Gui PRIVATE dss_warnings)
  qt_add_executable(tst_bildvergleichlogik tst_bildvergleichlogik.cpp)
  target_link_libraries(tst_bildvergleichlogik PRIVATE dss_bildvergleich dss_warnings Qt6::Test)
  add_test(NAME tst_bildvergleichlogik COMMAND tst_bildvergleichlogik)
endif()

# P10, Stufe 1 der Prüfung der Browserquelle: Qt WebEngine (Chromium) lädt die Seite eines eigenen
# Kerns über die Adresse im Heimnetz, Abgriff mit Durchsichtigkeit, Vergleich mit dss_render. In
# der X11-Sitzung; ohne X11 oder ohne Adresse im Heimnetz übersprungen (77). Nur mit dem Server, dem
# Zeichner und Qt WebEngine (Paket qt6-webengine-dev, seit P01 installiert).
if(DSS_CORE_OVERLAY_SERVER AND TARGET dss_media AND DSS_BUILD_APP AND DSS_APP_RENDERER)
  find_package(Qt6 QUIET COMPONENTS WebEngineWidgets QuickWidgets)
  if(TARGET Qt6::WebEngineWidgets AND TARGET Qt6::QuickWidgets)
    qt_add_executable(tst_browserquelle tst_browserquelle.cpp)
    target_link_libraries(tst_browserquelle PRIVATE dss_render dss_media dss_bildvergleich dss_warnings
      Qt6::Test Qt6::WebEngineWidgets Qt6::QuickWidgets)
    target_compile_definitions(tst_browserquelle PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
      DSS_TEST_MEDIA_DIR="${DSS_TEST_MEDIA_DIR}" DSS_TEST_FONT="${DSS_TEST_FONT}"
      DSS_TEST_LARGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/gross-daten"
      DSS_TEST_BUILTIN_FONT_DIR="${PROJECT_SOURCE_DIR}/src/common/schriften/be-vietnam-pro"
      DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
      DSS_TEST_VIDEO_ROTATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/video-drehung")
    add_dependencies(tst_browserquelle dss-core)
    add_test(NAME tst_browserquelle COMMAND tst_browserquelle)
    set_tests_properties(tst_browserquelle PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED dss_media)

    # P11, K-08: Bildvergleich der zwei Zeichner mit den Testdaten tests/daten/bildvergleich/, noch
    # ohne feste Grenze. Werte je Lauf unter build/tests/bildvergleich/. Mit DSS_BILDVERGLEICH_OBS
    # auch gegen OBS; ohne die Variable läuft derselbe Weg gegen obstestserver.h (kein echtes OBS).
    qt_add_executable(tst_bildvergleich tst_bildvergleich.cpp obstestserver.h)
    target_link_libraries(tst_bildvergleich PRIVATE dss_render dss_media dss_obs dss_bildvergleich dss_warnings
      Qt6::Test Qt6::WebEngineWidgets Qt6::QuickWidgets)
    target_compile_definitions(tst_bildvergleich PRIVATE DSS_CORE_BIN="$<TARGET_FILE:dss-core>"
      DSS_TEST_BILDVERGLEICH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/bildvergleich"
      DSS_TEST_EXIF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/daten/exif"
      DSS_TEST_FONT="${DSS_TEST_FONT}"
      DSS_TEST_VARIABLE_FONT="${CMAKE_CURRENT_SOURCE_DIR}/daten/schriften/urbanist/Urbanist[wght].ttf"
      DSS_TEST_BUILTIN_FONT_DIR="${PROJECT_SOURCE_DIR}/src/common/schriften/be-vietnam-pro"
      DSS_BILDVERGLEICH_AUSGABE="${CMAKE_CURRENT_BINARY_DIR}/bildvergleich")
    add_dependencies(tst_bildvergleich dss-core)
    add_test(NAME tst_bildvergleich COMMAND tst_bildvergleich)
    set_tests_properties(tst_bildvergleich PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
